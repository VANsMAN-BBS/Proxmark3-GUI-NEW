param(
    [switch]$All
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildRoot = Join-Path $ProjectRoot "build"

$targets = @(
    (Join-Path $BuildRoot "qt515-mingw64\debug"),
    (Join-Path $BuildRoot "qt515-mingw64\release"),
    (Join-Path $BuildRoot "qt515-mingw64\tests")
)
if ($All) {
    $targets += (Join-Path $BuildRoot "legacy")
}

foreach ($target in $targets) {
    if (Test-Path $target) {
        Remove-Item $target -Recurse -Force
        Write-Output "removed $target"
    }
}

Write-Output "Artifacts and published releases were not touched."
exit 0
