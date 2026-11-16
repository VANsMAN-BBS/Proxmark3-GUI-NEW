param(
    [switch]$Force,
    [string]$RepoRoot
)

$ErrorActionPreference = "Stop"

function Resolve-GitExecutable {
    $cmd = Get-Command git -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($candidate in @("C:\msys64\usr\bin\git.exe", "C:\Program Files\Git\cmd\git.exe")) {
        if (Test-Path $candidate) { return $candidate }
    }
    throw "git executable not found"
}

$GitExe = Resolve-GitExecutable

function Convert-ToWindowsPath {
    param([string]$Path)
    if ($Path -match '^/([a-zA-Z])/(.*)$') {
        return ($Matches[1].ToUpper() + ":\" + $Matches[2].Replace("/", "\"))
    }
    return $Path
}


$root = if ($RepoRoot) {
    (Resolve-Path $RepoRoot).Path
} else {
    (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}
if (-not $root) { Write-Error "Not inside a git repository."; exit 3 }
Set-Location $root

$hookDir = Join-Path $root ".githooks"
$hook = Join-Path $hookDir "pre-push"
if (-not (Test-Path $hook)) {
    Write-Error "Missing .githooks/pre-push"
    exit 3
}

$existing = (& $GitExe config --local --get core.hooksPath 2>$null)
if ($LASTEXITCODE -eq 0 -and $existing) {
    $existing = $existing.Trim()
    if ($existing -ne ".githooks") {
        Write-Output "core.hooksPath is already set to '$existing'."
        Write-Output "Refusing to overwrite another hook configuration. Nothing changed."
        exit 2
    }
}

$legacyHook = Join-Path $root ".git/hooks/pre-push"
if (Test-Path $legacyHook) {
    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $backup = "$legacyHook.backup-$stamp"
    Copy-Item $legacyHook $backup
    Write-Output "Existing .git/hooks/pre-push backed up to:"
    Write-Output "  $backup"
    Write-Output "It is bypassed because core.hooksPath points to .githooks."
}

& $GitExe config --local core.hooksPath .githooks
if ($LASTEXITCODE -ne 0) { Write-Error "Failed to set core.hooksPath"; exit 3 }

Write-Output "Installed local pre-push audit (core.hooksPath = .githooks)."
Write-Output ""
Write-Output "Manual audit:"
Write-Output "  powershell -ExecutionPolicy Bypass -File scripts/audit-git-content.ps1"
Write-Output ""
Write-Output "Disable:"
Write-Output "  git config --local --unset core.hooksPath"
