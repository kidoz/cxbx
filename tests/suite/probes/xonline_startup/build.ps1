# Build the non-secure XOnline 5849 startup probe.
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
    /LIBPATH:"$xdk\xbox\lib" /OUT:"$bin\xonline_startup.exe" `
    /MAP:"$bin\xonline_startup.map" "$bin\main.obj" `
    xapilib.lib xonline.lib xboxkrnl.lib
if ($LASTEXITCODE) { throw "Link failed ($LASTEXITCODE)" }

& (Join-Path $xdk "xbox\bin\imagebld.exe") /NOLIBWARN /DONTMOUNTUD `
    /STACK:0x10000 /TESTNAME:xonline_startup /TESTID:0xFFFF0040 `
    /IN:"$bin\xonline_startup.exe" /MAP:"$bin\xonline_startup.map" `
    /OUT:"$bin\default.xbe"
if ($LASTEXITCODE) { throw "imagebld failed ($LASTEXITCODE)" }

Write-Host "OK: $bin\default.xbe"
