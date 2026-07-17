param(
    [string]$Xdk = $env:CXBX_XDK
)

# Build the stream probe with a recovered Xbox XDK containing the synchronous
# playback pause mode used by this probe.
$ErrorActionPreference = "Stop"

if (-not $Xdk) {
    $repo = Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")
    $Xdk = Join-Path $repo "other\xbox-sdks\extracted\2005-03_-_5849.6_5849.17_-_RecoveryEXE_SDK\XDKSetup5849.17_extracted\XDK"
}
if (-not $Xdk -or -not (Test-Path (Join-Path $Xdk "xbox\lib\dsound.lib"))) {
    throw "Set CXBX_XDK or pass -Xdk with an Xbox XDK root containing xbox/bin and xbox/lib."
}

$vc = Join-Path $Xdk "xbox\bin\vc71"
if (-not (Test-Path (Join-Path $vc "CL.Exe"))) {
    $vc = Join-Path $Xdk "xbox\bin\vc7"
}
if (-not (Test-Path (Join-Path $vc "CL.Exe"))) {
    throw "The selected XDK does not contain a vc7 or vc71 Xbox compiler."
}
$bin = Join-Path $PSScriptRoot "bin"
New-Item -ItemType Directory -Force $bin | Out-Null

& "$vc\CL.Exe" /nologo /c /O2 /Gy /D_XBOX /DNDEBUG /ML /W3 `
    /I"$Xdk\xbox\include" /I"$PSScriptRoot\..\..\common" `
    /Fo"$bin\main.obj" "$PSScriptRoot\main.cpp"
if ($LASTEXITCODE) {
    throw "CL failed ($LASTEXITCODE)"
}

& "$vc\Link.Exe" /nologo /MACHINE:I386 /FIXED:NO /SUBSYSTEM:XBOX /INCREMENTAL:NO `
    /LIBPATH:"$Xdk\xbox\lib" `
    /OUT:"$bin\ds_stream.exe" /MAP:"$bin\ds_stream.map" `
    "$bin\main.obj" xapilib.lib dsound.lib xboxkrnl.lib
if ($LASTEXITCODE) {
    throw "Link failed ($LASTEXITCODE)"
}

& (Join-Path $Xdk "xbox\bin\imagebld.exe") /NOLIBWARN /DONTMOUNTUD `
    /STACK:0x10000 /TESTNAME:ds_stream /TESTID:0xFFFF0038 `
    /IN:"$bin\ds_stream.exe" /OUT:"$bin\default.xbe"
if ($LASTEXITCODE) {
    throw "imagebld failed ($LASTEXITCODE)"
}

Write-Host "OK: $bin\default.xbe"
