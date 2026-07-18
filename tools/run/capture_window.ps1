# capture_window.ps1 - PrintWindow-capture a specific guest process's window to PNG.
# Targets a process BY PID (the guest default.exe found via parent PID by
# run_title.py), so it never grabs an unrelated Cxbx session's window.
# Note: PrintWindow can legitimately return an all-black frame when a title
# renders through paths it doesn't expose to GDI -- use --dump-frames for the
# emulator's ground-truth backbuffer BMPs in that case.
param(
    [Parameter(Mandatory = $true)][int]$TargetPid,
    [Parameter(Mandatory = $true)][string]$Out
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WinCap {
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
  [DllImport("user32.dll", SetLastError=true)] private static extern bool GetWindowRect(IntPtr hwnd, out RECT r);
  [DllImport("user32.dll", SetLastError=true)] private static extern bool GetClientRect(IntPtr hwnd, out RECT r);
  [DllImport("user32.dll", SetLastError=true)] private static extern bool ClientToScreen(IntPtr hwnd, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
  public struct RECT { public int Left, Top, Right, Bottom; }
  public struct POINT { public int X, Y; }
  public static RECT WindowRect(IntPtr hwnd) {
    RECT r;
    if (!GetWindowRect(hwnd, out r)) throw new System.ComponentModel.Win32Exception();
    return r;
  }
  public static RECT ClientRect(IntPtr hwnd) {
    RECT r;
    if (!GetClientRect(hwnd, out r)) throw new System.ComponentModel.Win32Exception();
    return r;
  }
  public static POINT ClientOrigin(IntPtr hwnd) {
    POINT p = new POINT();
    if (!ClientToScreen(hwnd, ref p)) throw new System.ComponentModel.Win32Exception();
    return p;
  }
}
"@ -ReferencedAssemblies System.Drawing | Out-Null
Add-Type -AssemblyName System.Drawing

# User32 virtualizes coordinates for DPI-unaware PowerShell, while
# Graphics.CopyFromScreen consumes physical pixels. Query all geometry in the
# window's native DPI coordinate space so the desktop fallback stays aligned.
[WinCap]::SetThreadDpiAwarenessContext([IntPtr]::new(-4)) | Out-Null

$proc = Get-Process -Id $TargetPid -ErrorAction SilentlyContinue
if (-not $proc -or $proc.MainWindowHandle -eq 0) { Write-Output "NOWINDOW"; exit 1 }
$h = $proc.MainWindowHandle

$r = [WinCap]::WindowRect($h)
$w = $r.Right - $r.Left
$ht = $r.Bottom - $r.Top
if ($w -le 0 -or $ht -le 0) { Write-Output "ZERORECT"; exit 1 }

$cr = [WinCap]::ClientRect($h)
$origin = [WinCap]::ClientOrigin($h)
if ($null -eq $cr -or $null -eq $origin) {
    Write-Output "NOCLIENTRECT"
    exit 1
}
$clientX = $origin.X - $r.Left
$clientY = $origin.Y - $r.Top
$clientW = $cr.Right - $cr.Left
$clientH = $cr.Bottom - $cr.Top
if ($clientW -le 0 -or $clientH -le 0) { Write-Output "ZEROCLIENT"; exit 1 }

$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
$ok = [WinCap]::PrintWindow($h, $hdc, 2)   # flags=2 = PW_RENDERFULLCONTENT
$g.ReleaseHdc($hdc); $g.Dispose()

# PrintWindow often returns the non-client frame plus an all-black D3D client.
# Sample the real client rectangle, then fall back to the desktop rectangle so
# the result includes the NV2A child overlay used by raw-pushbuffer titles.
$clientBlack = $true
for ($y = $clientY; $y -lt ($clientY + $clientH) -and $clientBlack; $y += 16) {
    for ($x = $clientX; $x -lt ($clientX + $clientW); $x += 16) {
        if (($bmp.GetPixel($x, $y).ToArgb() -band 0x00FFFFFF) -ne 0) {
            $clientBlack = $false
            break
        }
    }
}
$source = "printwindow"
if ($clientBlack) {
    [WinCap]::SetForegroundWindow($h) | Out-Null
    # Foreground activation can be denied to a background PowerShell process.
    # Briefly make the target topmost so the desktop fallback cannot classify an
    # overlapping browser or terminal as guest output, then restore it below.
    [WinCap]::SetWindowPos($h, [IntPtr]::new(-1), 0, 0, 0, 0, 0x0043) | Out-Null
    try {
        Start-Sleep -Milliseconds 40
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        # Keep PrintWindow's non-client frame and replace only the client area.
        $clientSize = New-Object System.Drawing.Size $clientW, $clientH
        $g.CopyFromScreen($origin.X, $origin.Y, $clientX, $clientY, $clientSize)
    }
    finally {
        if ($null -ne $g) { $g.Dispose() }
        [WinCap]::SetWindowPos($h, [IntPtr]::new(-2), 0, 0, 0, 0, 0x0003) | Out-Null
    }
    $source = "desktopclient"
}

# Emit stable, sampled client-area metrics. They make title runs machine-readable
# without adding an image dependency to the Python harness.
$colors = New-Object 'System.Collections.Generic.HashSet[int]'
$samples = 0
$nonblack = 0
$minX = $clientW
$minY = $clientH
$maxX = -1
$maxY = -1
for ($cy = 0; $cy -lt $clientH; $cy += 4) {
    for ($cx = 0; $cx -lt $clientW; $cx += 4) {
        $rgb = $bmp.GetPixel($clientX + $cx, $clientY + $cy).ToArgb() -band 0x00FFFFFF
        [void]$colors.Add($rgb)
        $samples++
        if ($rgb -ne 0) {
            $nonblack++
            if ($cx -lt $minX) { $minX = $cx }
            if ($cy -lt $minY) { $minY = $cy }
            if ($cx -gt $maxX) { $maxX = $cx }
            if ($cy -gt $maxY) { $maxY = $cy }
        }
    }
}
$ratio = if ($samples -gt 0) { $nonblack / [double]$samples } else { 0.0 }
$bbox = if ($nonblack -gt 0) { "$minX,$minY,$maxX,$maxY" } else { "none" }
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output ("SAVED width={0} height={1} source={2} client={3}x{4} samples={5} nonblack={6:F6} colors={7} bbox={8} screen={9},{10} window={11},{12}" -f `
    $w, $ht, $source, $clientW, $clientH, $samples, $ratio, $colors.Count, $bbox, `
    $origin.X, $origin.Y, $r.Left, $r.Top)
