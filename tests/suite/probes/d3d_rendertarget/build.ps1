# Build with the archived Xbox XDK 4627 toolchain so the probe contains the
# exact retail SetRenderTarget function body used by Turok - Evolution.
$ErrorActionPreference = "Stop"

$repo = Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")
$archive = Join-Path $repo "other\xbox-sdks\extracted"
$xdk = Get-ChildItem -LiteralPath $archive -Directory -Filter "2002-06_-_4627_*" |
    ForEach-Object {
        Get-ChildItem -LiteralPath $_.FullName -Recurse -Directory -Filter XDK |
            Where-Object { Test-Path (Join-Path $_.FullName "xbox\lib\d3d8.lib") }
    } | Select-Object -First 1 -ExpandProperty FullName

if (-not $xdk) { throw "Archived XDK 4627 was not found under $archive" }

$xboxbin = Join-Path $xdk "xbox\bin"
$vc  = Join-Path $xboxbin "vc7"
$bin = Join-Path $PSScriptRoot "bin"
New-Item -ItemType Directory -Force $bin | Out-Null

& "$vc\CL.Exe" /nologo /c /O2 /Gy /D_XBOX /DNDEBUG /ML /W3 `
    /I"$xdk\xbox\include" /I"$PSScriptRoot\..\..\common" `
    /Fo"$bin\main.obj" "$PSScriptRoot\main.cpp"
if ($LASTEXITCODE) { throw "CL failed ($LASTEXITCODE)" }

& "$vc\Link.Exe" /nologo /MACHINE:I386 /FIXED:NO /SUBSYSTEM:XBOX /INCREMENTAL:NO `
    /LIBPATH:"$xdk\xbox\lib" `
    /OUT:"$bin\d3d_rendertarget.exe" /MAP:"$bin\d3d_rendertarget.map" `
    "$bin\main.obj" xapilib.lib d3d8.lib xboxkrnl.lib
if ($LASTEXITCODE) { throw "Link failed ($LASTEXITCODE)" }

& (Join-Path $xboxbin "imagebld.exe") /NOLIBWARN /DONTMOUNTUD `
    /STACK:0x10000 /TESTNAME:d3d_rendertarget /TESTID:0xFFFF000B `
    /IN:"$bin\d3d_rendertarget.exe" /OUT:"$bin\default.xbe"
if ($LASTEXITCODE) { throw "imagebld failed ($LASTEXITCODE)" }

Write-Host "OK: $bin\default.xbe"
