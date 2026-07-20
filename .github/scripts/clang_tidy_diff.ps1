# Run clang-tidy only on changed lines that belong to compiled translation units.
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,

    [Parameter(Mandatory = $true)]
    [string]$BaseRevision,

    [string]$HeadRevision = "HEAD"
)

$ErrorActionPreference = "Stop"

$databasePath = Join-Path $BuildDirectory "compile_commands.json"
if (!(Test-Path -LiteralPath $databasePath)) {
    throw "Compilation database not found: $databasePath"
}

$database = Get-Content -Raw -LiteralPath $databasePath | ConvertFrom-Json
$compiledFiles = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase
)
foreach ($entry in $database) {
    $entryPath = if ([System.IO.Path]::IsPathRooted($entry.file)) {
        $entry.file
    } else {
        Join-Path $entry.directory $entry.file
    }
    [void]$compiledFiles.Add([System.IO.Path]::GetFullPath($entryPath))
}

$diffLines = & git diff --unified=0 --diff-filter=ACMR $BaseRevision $HeadRevision -- src tests
if ($LASTEXITCODE -ne 0) {
    throw "git diff failed with exit code $LASTEXITCODE"
}

$rangesByFile = [ordered]@{}
$currentFile = $null
foreach ($line in $diffLines) {
    if ($line -match '^\+\+\+ b/(.+)$') {
        $candidate = $Matches[1]
        if ($candidate -match '\.(c|cc|cpp|cxx)$') {
            $currentFile = $candidate
            if (!$rangesByFile.Contains($currentFile)) {
                $rangesByFile[$currentFile] = [System.Collections.Generic.List[object]]::new()
            }
        } else {
            $currentFile = $null
        }
        continue
    }

    if ($currentFile -and $line -match '^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@') {
        $startLine = [int]$Matches[1]
        $lineCount = if ($Matches[2]) { [int]$Matches[2] } else { 1 }
        if ($lineCount -gt 0) {
            $range = @($startLine, ($startLine + $lineCount - 1))
            $rangesByFile[$currentFile].Add($range)
        }
    }
}

$sourceFiles = [System.Collections.Generic.List[string]]::new()
$filters = [System.Collections.Generic.List[object]]::new()
foreach ($file in $rangesByFile.Keys) {
    $absoluteFile = [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $file))
    if (!$compiledFiles.Contains($absoluteFile)) {
        Write-Output "Skipping changed source not present in compile_commands.json: $file"
        continue
    }

    $sourceFiles.Add($absoluteFile)
    $filters.Add([pscustomobject]@{
        name = $absoluteFile
        lines = @($rangesByFile[$file])
    })
}

if ($sourceFiles.Count -eq 0) {
    Write-Output "No changed compiled C/C++ lines require clang-tidy."
    exit 0
}

$lineFilter = ConvertTo-Json -InputObject @($filters) -Compress -Depth 5
# PowerShell removes embedded quotes when constructing a native Windows command
# line unless they are escaped for the receiving C runtime argument parser.
$escapedLineFilter = $lineFilter.Replace('"', '\"')
& clang-tidy "-p=$BuildDirectory" "--line-filter=$escapedLineFilter" "--warnings-as-errors=*" @sourceFiles
if ($LASTEXITCODE -ne 0) {
    throw "clang-tidy failed with exit code $LASTEXITCODE"
}
