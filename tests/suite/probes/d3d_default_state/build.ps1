# Build d3d_default_state with the recovered official Xbox XDK 5849 toolchain.
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
    /LIBPATH:"$xdk\xbox\lib" `
    /OUT:"$bin\d3d_default_state.exe" /MAP:"$bin\d3d_default_state.map" `
    "$bin\main.obj" xapilib.lib d3d8.lib xboxkrnl.lib
if ($LASTEXITCODE) { throw "Link failed ($LASTEXITCODE)" }

& (Join-Path $xdk "xbox\bin\imagebld.exe") /NOLIBWARN /DONTMOUNTUD `
    /STACK:0x10000 /TESTNAME:d3d_default_state /TESTID:0xFFFF0048 `
    /IN:"$bin\d3d_default_state.exe" /MAP:"$bin\d3d_default_state.map" `
    /OUT:"$bin\default.xbe"
if ($LASTEXITCODE) { throw "imagebld failed ($LASTEXITCODE)" }

Write-Host "OK: $bin\default.xbe"
