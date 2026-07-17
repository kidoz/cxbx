param(
    [string]$Xdk = $env:CXBX_XDK,
    [string]$OutputDir = (Join-Path $PSScriptRoot "bin"),
    [switch]$CppMakeSpace
)

$ErrorActionPreference = "Stop"

if (-not $Xdk) {
    $repo = Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")
    $archive = Join-Path $repo "other\xbox-sdks\extracted"
    $Xdk = Get-ChildItem -LiteralPath $archive -Directory -Filter "2002-06_-_4627_*" |
        ForEach-Object {
            Get-ChildItem -LiteralPath $_.FullName -Recurse -Directory -Filter XDK |
                Where-Object { Test-Path (Join-Path $_.FullName "xbox\lib\d3d8.lib") }
        } | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $Xdk -or -not (Test-Path (Join-Path $Xdk "xbox\lib\d3d8.lib"))) {
    throw "Set CXBX_XDK or pass -Xdk with an Xbox XDK root containing xbox/bin and xbox/lib."
}

# The 4627 library exports MakeSpace under its D3D C++ namespace.
if (-not $PSBoundParameters.ContainsKey("CppMakeSpace")) {
    $CppMakeSpace = $true
}

$vc = Join-Path $Xdk "xbox\bin\vc71"
if (-not (Test-Path (Join-Path $vc "CL.Exe"))) {
    $vc = Join-Path $Xdk "xbox\bin\vc7"
}
if (-not (Test-Path (Join-Path $vc "CL.Exe"))) {
    throw "The selected XDK does not contain a vc7 or vc71 Xbox compiler."
}
New-Item -ItemType Directory -Force $OutputDir | Out-Null
$defines = @()
if ($CppMakeSpace) {
    $defines += "/DCXBX_MAKESPACE_CPP"
}

& "$vc\CL.Exe" /nologo /c /O2 /Gy /D_XBOX /DNDEBUG /ML /W3 $defines `
    /I"$Xdk\xbox\include" /I"$PSScriptRoot\..\..\common" `
    /Fo"$OutputDir\main.obj" "$PSScriptRoot\main.cpp"
if ($LASTEXITCODE) { throw "CL failed ($LASTEXITCODE)" }

& "$vc\Link.Exe" /nologo /MACHINE:I386 /FIXED:NO /SUBSYSTEM:XBOX /INCREMENTAL:NO `
    /LIBPATH:"$Xdk\xbox\lib" `
    /OUT:"$OutputDir\d3d_makespace.exe" /MAP:"$OutputDir\d3d_makespace.map" `
    "$OutputDir\main.obj" xapilib.lib d3d8.lib xboxkrnl.lib
if ($LASTEXITCODE) { throw "Link failed ($LASTEXITCODE)" }

& (Join-Path $Xdk "xbox\bin\imagebld.exe") /NOLIBWARN /DONTMOUNTUD `
    /STACK:0x10000 /TESTNAME:d3d_makespace /TESTID:0xFFFF0022 `
    /IN:"$OutputDir\d3d_makespace.exe" /OUT:"$OutputDir\default.xbe"
if ($LASTEXITCODE) { throw "imagebld failed ($LASTEXITCODE)" }

Write-Host "OK: $OutputDir\default.xbe"
