# Build d3d_debug with the Xbox XDK 5849 debug libraries.
$ErrorActionPreference = "Stop"

$xdk = "D:\projects\cxbx\other\sdk\XDKSetup5849.15_extracted\XDK"
$vc  = Join-Path $xdk "xbox\bin\vc71"
$bin = Join-Path $PSScriptRoot "bin"
New-Item -ItemType Directory -Force $bin | Out-Null

& "$vc\CL.Exe" /nologo /c /Od /Gy /D_XBOX /D_DEBUG /MLd /W3 `
    /I"$xdk\xbox\include" /I"$PSScriptRoot\..\..\common" `
    /Fo"$bin\main.obj" "$PSScriptRoot\main.cpp"
if ($LASTEXITCODE) { throw "CL failed ($LASTEXITCODE)" }

& "$vc\Link.Exe" /nologo /MACHINE:I386 /FIXED:NO /SUBSYSTEM:XBOX /INCREMENTAL:NO `
    /LIBPATH:"$xdk\xbox\lib" `
    /OUT:"$bin\d3d_debug.exe" /MAP:"$bin\d3d_debug.map" `
    "$bin\main.obj" xperf.lib xbdm.lib xapilibd.lib d3d8d.lib xboxkrnl.lib
if ($LASTEXITCODE) { throw "Link failed ($LASTEXITCODE)" }

& (Join-Path $xdk "xbox\bin\imagebld.exe") /NOLIBWARN /DONTMOUNTUD `
    /STACK:0x10000 /TESTNAME:d3d_debug /TESTID:0xFFFF0011 `
    /IN:"$bin\d3d_debug.exe" /MAP:"$bin\d3d_debug.map" /OUT:"$bin\default.xbe"
if ($LASTEXITCODE) { throw "imagebld failed ($LASTEXITCODE)" }

Write-Host "OK: $bin\default.xbe"
