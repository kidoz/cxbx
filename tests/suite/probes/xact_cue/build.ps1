# Build the XACT 5849 cue prepare/play/stop probe.
$ErrorActionPreference = "Stop"

if (-not $env:CXBX_XDK_ROOT) {
    throw "Set CXBX_XDK_ROOT to an Xbox XDK root containing xbox/lib"
}

$xdk = (Resolve-Path -LiteralPath $env:CXBX_XDK_ROOT).Path
$vc = Join-Path $xdk "xbox\bin\vc71"
$bin = Join-Path $PSScriptRoot "bin"
New-Item -ItemType Directory -Force $bin | Out-Null

$wavePath = Join-Path $bin "probe.wav"
$stream = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter($stream)
$writer.Write([Text.Encoding]::ASCII.GetBytes("RIFF"))
$writer.Write([UInt32]548)
$writer.Write([Text.Encoding]::ASCII.GetBytes("WAVEfmt "))
$writer.Write([UInt32]16)
$writer.Write([UInt16]1)
$writer.Write([UInt16]1)
$writer.Write([UInt32]22050)
$writer.Write([UInt32]44100)
$writer.Write([UInt16]2)
$writer.Write([UInt16]16)
$writer.Write([Text.Encoding]::ASCII.GetBytes("data"))
$writer.Write([UInt32]512)
for ($sample = 0; $sample -lt 256; ++$sample) {
    $writer.Write([Int16]0)
}
$writer.Flush()
[IO.File]::WriteAllBytes($wavePath, $stream.ToArray())
$writer.Dispose()
$stream.Dispose()

& (Join-Path $xdk "xbox\bin\xactbld.exe") /L `
    (Join-Path $PSScriptRoot "probe.xap") $bin
if ($LASTEXITCODE) { throw "xactbld failed ($LASTEXITCODE)" }

function ConvertTo-CArray([string]$path, [string]$name) {
    $bytes = [IO.File]::ReadAllBytes($path)
    $lines = New-Object System.Collections.Generic.List[string]
    for ($offset = 0; $offset -lt $bytes.Length; $offset += 16) {
        $last = [Math]::Min($offset + 15, $bytes.Length - 1)
        $values = for ($index = $offset; $index -le $last; ++$index) {
            "0x{0:X2}" -f $bytes[$index]
        }
        $lines.Add("    " + ($values -join ", ") + ",")
    }
    return "static const unsigned char $name[] = {`r`n" +
        ($lines -join "`r`n") + "`r`n};`r`n"
}

$banks = "#pragma once`r`n`r`n"
$banks += ConvertTo-CArray (Join-Path $bin "probe.xsb") "g_ProbeSoundBank"
$banks += "`r`n"
$banks += ConvertTo-CArray (Join-Path $bin "probe.xwb") "g_ProbeWaveBank"
[IO.File]::WriteAllText((Join-Path $bin "probe_banks.h"), $banks)

& "$vc\CL.Exe" /nologo /c /O2 /Gy /D_XBOX /DNDEBUG /ML /W3 `
    /I"$xdk\xbox\include" /I"$PSScriptRoot\..\..\common" /I"$bin" `
    /Fo"$bin\main.obj" "$PSScriptRoot\main.cpp"
if ($LASTEXITCODE) { throw "CL failed ($LASTEXITCODE)" }

& "$vc\Link.Exe" /nologo /MACHINE:I386 /FIXED:NO /SUBSYSTEM:XBOX /INCREMENTAL:NO `
    /LIBPATH:"$xdk\xbox\lib" /OUT:"$bin\xact_cue.exe" `
    /MAP:"$bin\xact_cue.map" "$bin\main.obj" `
    xacteng.lib dsound.lib xapilib.lib xboxkrnl.lib
if ($LASTEXITCODE) { throw "Link failed ($LASTEXITCODE)" }

& (Join-Path $xdk "xbox\bin\imagebld.exe") /NOLIBWARN /DONTMOUNTUD `
    /STACK:0x10000 /TESTNAME:xact_cue /TESTID:0xFFFF0046 `
    /IN:"$bin\xact_cue.exe" /MAP:"$bin\xact_cue.map" `
    /OUT:"$bin\default.xbe"
if ($LASTEXITCODE) { throw "imagebld failed ($LASTEXITCODE)" }

Write-Host "OK: $bin\default.xbe"
