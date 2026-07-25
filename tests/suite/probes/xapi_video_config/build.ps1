# Build the XAPI dashboard video configuration probe.
$ErrorActionPreference = "Stop"

if (-not $env:CXBX_XDK_ROOT) {
    throw "Set CXBX_XDK_ROOT to an Xbox XDK root containing xbox/lib"
}

$xdk = (Resolve-Path -LiteralPath $env:CXBX_XDK_ROOT).Path
$vc = Join-Path $xdk "xbox\bin\vc71"
$bin = Join-Path $PSScriptRoot "bin"
New-Item -ItemType Directory -Force $bin | Out-Null

& "$vc\CL.Exe" /nologo /c /O2 /Gy /D_XBOX /DNDEBUG /ML /W3 `
    /I"$xdk\xbox\include" /I"$PSScriptRoot\..\..\common" `
    /Fo"$bin\main.obj" "$PSScriptRoot\main.cpp"
if ($LASTEXITCODE) { throw "CL failed ($LASTEXITCODE)" }

& "$vc\Link.Exe" /nologo /MACHINE:I386 /FIXED:NO /SUBSYSTEM:XBOX /INCREMENTAL:NO `
    /LIBPATH:"$xdk\xbox\lib" /OUT:"$bin\xapi_video_config.exe" `
    /MAP:"$bin\xapi_video_config.map" "$bin\main.obj" xapilib.lib xboxkrnl.lib
if ($LASTEXITCODE) { throw "Link failed ($LASTEXITCODE)" }

& (Join-Path $xdk "xbox\bin\imagebld.exe") /NOLIBWARN /DONTMOUNTUD `
    /STACK:0x10000 /TESTNAME:xapi_video_config /TESTID:0xFFFF0047 `
    /IN:"$bin\xapi_video_config.exe" /MAP:"$bin\xapi_video_config.map" `
    /OUT:"$bin\default.xbe"
if ($LASTEXITCODE) { throw "imagebld failed ($LASTEXITCODE)" }

Write-Host "OK: $bin\default.xbe"
