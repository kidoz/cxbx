# Build xmv_decode with the real Xbox XDK 5849 toolchain (links xmv.lib) and
# stage the SDK's Test.xmv so the guest can open D:\Media\Videos\Test.xmv.
$ErrorActionPreference = "Stop"

$xdk = "D:\projects\cxbx\other\sdk\XDKSetup5849.15_extracted\XDK"
$vc  = Join-Path $xdk "xbox\bin\vc71"
$bin = Join-Path $PSScriptRoot "bin"
New-Item -ItemType Directory -Force $bin | Out-Null

& "$vc\CL.Exe" /nologo /c /O2 /Gy /D_XBOX /DNDEBUG /ML /W3 `
    /I"$xdk\xbox\include" /I"$PSScriptRoot\..\..\common" `
    /Fo"$bin\main.obj" "$PSScriptRoot\main.cpp"
if ($LASTEXITCODE) { throw "CL failed ($LASTEXITCODE)" }

& "$vc\Link.Exe" /nologo /MACHINE:I386 /FIXED:NO /SUBSYSTEM:XBOX /INCREMENTAL:NO `
    /LIBPATH:"$xdk\xbox\lib" `
    /OUT:"$bin\xmv_decode.exe" /MAP:"$bin\xmv_decode.map" `
    "$bin\main.obj" xmv.lib xapilib.lib d3d8.lib dsound.lib xboxkrnl.lib
if ($LASTEXITCODE) { throw "Link failed ($LASTEXITCODE)" }

& (Join-Path $xdk "xbox\bin\imagebld.exe") /NOLIBWARN /DONTMOUNTUD `
    /STACK:0x10000 /TESTNAME:xmv_decode /TESTID:0xFFFF0010 `
    /IN:"$bin\xmv_decode.exe" /MAP:"$bin\xmv_decode.map" /OUT:"$bin\default.xbe"
if ($LASTEXITCODE) { throw "imagebld failed ($LASTEXITCODE)" }

$mediaDir = Join-Path $bin "Media\Videos"
New-Item -ItemType Directory -Force $mediaDir | Out-Null
Copy-Item "$xdk\Samples\Xbox\Video\SimpleXMV\Media\Videos\test.xmv" (Join-Path $mediaDir "Test.xmv") -Force

Write-Host "OK: $bin\default.xbe"
