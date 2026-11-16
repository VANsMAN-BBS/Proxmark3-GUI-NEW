param(
    [string]$Range,
    [switch]$Json
)

$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $root

$audit = Join-Path $root "scripts/git-push-audit.ps1"
$arguments = @("-RepoRoot", $root)
if ($Range) { $arguments += @("-Range", $Range) }
if ($Json) { $arguments += "-Json" }

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $audit @arguments
exit $LASTEXITCODE
