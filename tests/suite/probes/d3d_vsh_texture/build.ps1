# Build with the recovered Xbox XDK 5849.17 toolchain.
$ErrorActionPreference = "Stop"

$repo = Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")
$xdk = Join-Path $repo "other\xbox-sdks\extracted\2005-03_-_5849.6_5849.17_-_RecoveryEXE_SDK\XDKSetup5849.17_extracted\XDK"
$vc = Join-Path $xdk "xbox\bin\vc71"
$bin = Join-Path $PSScriptRoot "bin"
New-Item -ItemType Directory -Force $bin | Out-Null

& (Join-Path $xdk "xbox\bin\xsasm.exe") /nologo /h /hname g_CpuBridgeShader `
    "$PSScriptRoot\shader.vsh" "$bin\cpu_bridge.h"
if($LASTEXITCODE) { throw "xsasm failed ($LASTEXITCODE)" }

& "$vc\CL.Exe" /nologo /c /O2 /Gy /D_XBOX /DNDEBUG /ML /W3 `
    /I"$xdk\xbox\include" /I"$PSScriptRoot\..\..\common" /I"$bin" `
    /Fo"$bin\main.obj" "$PSScriptRoot\main.cpp"
if($LASTEXITCODE) { throw "CL failed ($LASTEXITCODE)" }

& "$vc\Link.Exe" /nologo /MACHINE:I386 /FIXED:NO /SUBSYSTEM:XBOX /INCREMENTAL:NO `
    /LIBPATH:"$xdk\xbox\lib" `
    /OUT:"$bin\d3d_vsh_texture.exe" /MAP:"$bin\d3d_vsh_texture.map" `
    "$bin\main.obj" xapilib.lib d3d8.lib xboxkrnl.lib
if($LASTEXITCODE) { throw "Link failed ($LASTEXITCODE)" }

& (Join-Path $xdk "xbox\bin\imagebld.exe") /NOLIBWARN /DONTMOUNTUD `
    /STACK:0x10000 /TESTNAME:d3d_vsh_texture /TESTID:0xFFFF000D `
    /IN:"$bin\d3d_vsh_texture.exe" /MAP:"$bin\d3d_vsh_texture.map" /OUT:"$bin\default.xbe"
if($LASTEXITCODE) { throw "imagebld failed ($LASTEXITCODE)" }

Write-Host "OK: $bin\default.xbe"
