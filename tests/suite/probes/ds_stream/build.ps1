# Build the stream probe with an exact Xbox XDK 4627 toolchain.
$ErrorActionPreference = "Stop"

$xdk = $env:CXBX_XDK_4627
if ([string]::IsNullOrWhiteSpace($xdk)) {
    throw "Set CXBX_XDK_4627 to an Xbox XDK 4627 root containing xbox/bin and xbox/lib."
}

$vc = Join-Path $xdk "xbox\bin\vc7"
$bin = Join-Path $PSScriptRoot "bin"
New-Item -ItemType Directory -Force $bin | Out-Null

& "$vc\CL.Exe" /nologo /c /O2 /Gy /D_XBOX /DNDEBUG /ML /W3 `
    /I"$xdk\xbox\include" /I"$PSScriptRoot\..\..\common" `
    /Fo"$bin\main.obj" "$PSScriptRoot\main.cpp"
if ($LASTEXITCODE) {
    throw "CL failed ($LASTEXITCODE)"
}

& "$vc\Link.Exe" /nologo /MACHINE:I386 /FIXED:NO /SUBSYSTEM:XBOX /INCREMENTAL:NO `
    /LIBPATH:"$xdk\xbox\lib" `
    /OUT:"$bin\ds_stream.exe" /MAP:"$bin\ds_stream.map" `
    "$bin\main.obj" xapilib.lib dsound.lib xboxkrnl.lib
if ($LASTEXITCODE) {
    throw "Link failed ($LASTEXITCODE)"
}

& (Join-Path $xdk "xbox\bin\imagebld.exe") /NOLIBWARN /DONTMOUNTUD `
    /STACK:0x10000 /TESTNAME:ds_stream /TESTID:0xFFFF0038 `
    /IN:"$bin\ds_stream.exe" /OUT:"$bin\default.xbe"
if ($LASTEXITCODE) {
    throw "imagebld failed ($LASTEXITCODE)"
}

Write-Host "OK: $bin\default.xbe"
