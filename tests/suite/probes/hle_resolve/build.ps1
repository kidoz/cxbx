# Build hle_resolve with the real Xbox XDK 5849 toolchain (not nxdk).
# Links the genuine d3d8.lib/dsound.lib so the OOVPA/HLE scan has real
# library code to resolve. Output: bin\default.xbe (suite convention).
$ErrorActionPreference = "Stop"

$xdk = "D:\projects\cxbx\other\sdk\XDKSetup5849.15_extracted\XDK"
$vc  = Join-Path $xdk "xbox\bin\vc71"
$bin = Join-Path $PSScriptRoot "bin"
New-Item -ItemType Directory -Force $bin | Out-Null

& "$vc\CL.Exe" /nologo /c /O2 /Gy /D_XBOX /DNDEBUG /ML /W3 `
    /I"$xdk\xbox\include" `
    /Fo"$bin\main.obj" "$PSScriptRoot\main.cpp"
if ($LASTEXITCODE) { throw "CL failed ($LASTEXITCODE)" }

& "$vc\Link.Exe" /nologo /MACHINE:I386 /FIXED:NO /SUBSYSTEM:XBOX /INCREMENTAL:NO `
    /LIBPATH:"$xdk\xbox\lib" `
    /OUT:"$bin\hle_resolve.exe" /MAP:"$bin\hle_resolve.map" `
    "$bin\main.obj" xapilib.lib d3d8.lib dsound.lib xboxkrnl.lib
if ($LASTEXITCODE) { throw "Link failed ($LASTEXITCODE)" }

& (Join-Path $xdk "xbox\bin\imagebld.exe") /NOLIBWARN /DONTMOUNTUD `
    /STACK:0x10000 /TESTNAME:hle_resolve /TESTID:0xFFFF0002 `
    /IN:"$bin\hle_resolve.exe" /MAP:"$bin\hle_resolve.map" /OUT:"$bin\default.xbe"
if ($LASTEXITCODE) { throw "imagebld failed ($LASTEXITCODE)" }

Write-Host "OK: $bin\default.xbe"
