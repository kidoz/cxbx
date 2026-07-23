# Run the bounded native x64 ASan lane and retain reproducible diagnostics.
param(
    [Parameter(Mandatory = $true)]
    [string]$SourceDirectory,

    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,

    [Parameter(Mandatory = $true)]
    [string]$DiagnosticsDirectory
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Invoke-LoggedCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [string]$LogPath,

        [Parameter(Mandatory = $true)]
        [scriptblock]$Command
    )

    Write-Output "Running $Name"
    & $Command 2>&1 | Tee-Object -FilePath $LogPath
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "$Name failed with exit code $exitCode"
    }
}

$sourcePath = [System.IO.Path]::GetFullPath($SourceDirectory)
$buildPath = [System.IO.Path]::GetFullPath($BuildDirectory)
$diagnosticsPath = [System.IO.Path]::GetFullPath($DiagnosticsDirectory)
New-Item -ItemType Directory -Force $diagnosticsPath | Out-Null

try {
    $env:CC = "clang"
    $env:CXX = "clang++"

    $requiredTools = @(
        "clang",
        "clang++",
        "llvm-symbolizer",
        "meson",
        "ninja"
    )
    $tools = Get-Command $requiredTools -ErrorAction Stop
    $symbolizer = Get-Command "llvm-symbolizer" -ErrorAction Stop

    $resourceDirectory = (& clang++ -print-resource-dir).Trim()
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($resourceDirectory)) {
        throw "Unable to resolve the Clang resource directory."
    }

    $runtimeDirectory = Join-Path $resourceDirectory "lib\windows"
    $requiredRuntimeFiles = @(
        "clang_rt.asan_dynamic-x86_64.dll",
        "clang_rt.asan_dynamic-x86_64.lib",
        "clang_rt.asan_static_runtime_thunk-x86_64.lib"
    )
    $runtimeInventory = foreach ($fileName in $requiredRuntimeFiles) {
        $path = Join-Path $runtimeDirectory $fileName
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Missing required ASan runtime file: $path"
        }
        $file = Get-Item -LiteralPath $path
        $hash = Get-FileHash -LiteralPath $path -Algorithm SHA256
        [pscustomobject]@{
            Name = $file.Name
            Length = $file.Length
            SHA256 = $hash.Hash
        }
    }

    $toolchainReport = @(
        "source_directory=$sourcePath",
        "build_directory=$buildPath",
        "clang_resource_directory=$resourceDirectory",
        "asan_runtime_directory=$runtimeDirectory",
        "llvm_symbolizer=$($symbolizer.Source)",
        "meson_version=$(meson --version)",
        "ninja_version=$(ninja --version)",
        "clang_version=$((& clang++ --version)[0])",
        "",
        ($tools | Select-Object Name, Source | Format-Table -AutoSize | Out-String),
        ($runtimeInventory | Format-Table -AutoSize | Out-String)
    )
    $toolchainReport |
        Tee-Object -FilePath (Join-Path $diagnosticsPath "toolchain.txt")

    Invoke-LoggedCommand `
        -Name "Meson setup" `
        -LogPath (Join-Path $diagnosticsPath "configure.log") `
        -Command {
            meson setup $buildPath $sourcePath `
                -Dbuild_host_asan=true `
                -Db_sanitize=address `
                -Db_vscrt=static_from_buildtype `
                --buildtype=debugoptimized
        }

    Invoke-LoggedCommand `
        -Name "Meson compile" `
        -LogPath (Join-Path $diagnosticsPath "compile.log") `
        -Command {
            meson compile -C $buildPath
        }

    Invoke-LoggedCommand `
        -Name "Meson ASan suite" `
        -LogPath (Join-Path $diagnosticsPath "test-console.log") `
        -Command {
            meson test -C $buildPath `
                --print-errorlogs `
                --suite cxbx:sanitizer
        }

    "status=passed" |
        Set-Content -LiteralPath (Join-Path $diagnosticsPath "result.txt")
} catch {
    @(
        "status=failed",
        "message=$($_.Exception.Message)"
    ) | Set-Content -LiteralPath (Join-Path $diagnosticsPath "result.txt")
    throw
}
