# Build xdk_smoke with the real Xbox XDK 5849 toolchain (not nxdk).
# Pipeline: vc71 CL -> vc71 Link (against XDK static libs) -> imagebld -> XBE.
# Output follows the suite convention: bin\default.xbe
$ErrorActionPreference = "Stop"

$xdk = "D:\projects\cxbx\other\sdk\XDKSetup5849.15_extracted\XDK"
$vc  = Join-Path $xdk "xbox\bin\vc71"
$bin = Join-Path $PSScriptRoot "bin"
New-Item -ItemType Directory -Force $bin | Out-Null

& "$vc\CL.Exe" /nologo /c /O2 /Gy /D_XBOX /DNDEBUG /ML /W3 `
    /I"$xdk\xbox\include" `
    /Fo"$bin\main.obj" "$PSScriptRoot\main.cpp"
if ($LASTEXITCODE) { throw "CL failed ($LASTEXITCODE)" }

& "$vc\Link.Exe" /nologo /MACHINE:I386 /FIXED:NO /SUBSYSTEM:XBOX `
    /LIBPATH:"$xdk\xbox\lib" `
    /OUT:"$bin\xdk_smoke.exe" /MAP:"$bin\xdk_smoke.map" `
    "$bin\main.obj" xapilib.lib xboxkrnl.lib
if ($LASTEXITCODE) { throw "Link failed ($LASTEXITCODE)" }

& (Join-Path $xdk "xbox\bin\imagebld.exe") /NOLIBWARN /DONTMOUNTUD `
    /STACK:0x10000 /TESTNAME:xdk_smoke /TESTID:0xFFFF0001 `
    /IN:"$bin\xdk_smoke.exe" /MAP:"$bin\xdk_smoke.map" /OUT:"$bin\default.xbe"
if ($LASTEXITCODE) { throw "imagebld failed ($LASTEXITCODE)" }

Write-Host "OK: $bin\default.xbe"
