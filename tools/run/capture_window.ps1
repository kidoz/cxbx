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
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
  public struct RECT { public int Left, Top, Right, Bottom; }
}
"@ -ReferencedAssemblies System.Drawing | Out-Null
Add-Type -AssemblyName System.Drawing

$proc = Get-Process -Id $TargetPid -ErrorAction SilentlyContinue
if (-not $proc -or $proc.MainWindowHandle -eq 0) { Write-Output "NOWINDOW"; exit 1 }
$h = $proc.MainWindowHandle

$r = New-Object WinCap+RECT
[WinCap]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.Right - $r.Left
$ht = $r.Bottom - $r.Top
if ($w -le 0 -or $ht -le 0) { Write-Output "ZERORECT"; exit 1 }

$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
$ok = [WinCap]::PrintWindow($h, $hdc, 2)   # flags=2 = PW_RENDERFULLCONTENT
$g.ReleaseHdc($hdc); $g.Dispose()

# PrintWindow often returns the non-client frame plus an all-black D3D client.
# If the sampled client is black, capture the actual desktop rectangle instead;
# this also includes the NV2A child overlay used by raw-pushbuffer titles.
$clientBlack = $true
for ($y = 48; $y -lt $ht -and $clientBlack; $y += 16) {
    for ($x = 8; $x -lt $w; $x += 16) {
        if (($bmp.GetPixel($x, $y).ToArgb() -band 0x00FFFFFF) -ne 0) {
            $clientBlack = $false
            break
        }
    }
}
if ($clientBlack) {
    [WinCap]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 40
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
    $g.Dispose()
}
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output ("SAVED {0}x{1}" -f $w, $ht)
