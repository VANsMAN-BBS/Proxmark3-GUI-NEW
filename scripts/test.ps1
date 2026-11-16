param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildRoot = Join-Path $ProjectRoot "build\qt515-mingw64\tests"
$ArtifactRoot = Join-Path $ProjectRoot "artifacts\test"
$Stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$ReportDir = Join-Path $ArtifactRoot "reports\$Stamp"
$LogDir = Join-Path $ArtifactRoot "logs"
New-Item -ItemType Directory -Force -Path $ReportDir, $LogDir | Out-Null

function Resolve-Tool {
    param([string]$Name, [string[]]$Candidates)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($candidate in $Candidates) {
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

$qmake = Resolve-Tool "qmake-qt5" @("C:\msys64\ucrt64\bin\qmake-qt5.exe")
$make = Resolve-Tool "mingw32-make" @("C:\msys64\ucrt64\bin\mingw32-make.exe")
if (-not $qmake -or -not $make) { Write-Host "Qt 5.15 / MinGW tools not found." -ForegroundColor Red; exit 2 }
$env:PATH = (Split-Path $qmake -Parent) + ";" + $env:PATH

$qtTests = @("felica", "hidseos", "ntagmodel", "amiibodata", "lfparser", "lfparser_stage2")
$failures = 0
$summary = @()

foreach ($name in $qtTests) {
    $testDir = Join-Path $ProjectRoot "tests\$name"
    $pro = Get-ChildItem $testDir -Filter "*.pro" | Select-Object -First 1
    if (-not $pro) { $summary += "$name : NO PROJECT"; $failures++; continue }

    $outDir = Join-Path $BuildRoot $name
    if (-not $SkipBuild) {
        New-Item -ItemType Directory -Force -Path $outDir | Out-Null
        Push-Location $outDir
        try {
            $previous = $ErrorActionPreference
            $ErrorActionPreference = "Continue"
            & $qmake $pro.FullName -spec win32-g++ "CONFIG+=release" 2>&1 | Tee-Object "qmake.log" | Out-Null
            $qmakeCode = $LASTEXITCODE
            $ErrorActionPreference = $previous
            if ($qmakeCode -ne 0) { $summary += "$name : QMAKE FAILED"; $failures++; continue }
            $previous = $ErrorActionPreference
            $ErrorActionPreference = "Continue"
            & $make -j $env:NUMBER_OF_PROCESSORS 2>&1 | Tee-Object "build.log" | Out-Null
            $makeCode = $LASTEXITCODE
            $ErrorActionPreference = $previous
            if ($makeCode -ne 0) { $summary += "$name : BUILD FAILED"; $failures++; continue }
        } finally {
            Pop-Location
        }
    }

    $exe = Get-ChildItem $outDir -Recurse -Filter "*.exe" -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -notmatch "^Qt" } | Select-Object -First 1
    if (-not $exe) { $summary += "$name : NO EXECUTABLE"; $failures++; continue }

    $report = Join-Path $ReportDir "$name.txt"
    & $exe.FullName -o "$report,txt" 2>&1 | Out-Null
    $rc = $LASTEXITCODE
    $totals = (Get-Content $report -ErrorAction SilentlyContinue | Select-String "^Totals:" | Select-Object -First 1).Line
    if (-not $totals) { $totals = "no totals line" }
    $summary += "$name : rc=$rc $totals"
    if ($rc -ne 0) { $failures++ }
}

# eMRTD Python tests (prefer an interpreter that actually has pytest)
function Resolve-PythonWithPytest {
    $candidates = @()
    $fromPath = Get-Command python -ErrorAction SilentlyContinue
    if ($fromPath) { $candidates += $fromPath.Source }
    $candidates += "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe"
    foreach ($candidate in $candidates) {
        if (-not $candidate -or -not (Test-Path $candidate)) { continue }
        $previous = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        & $candidate -c "import pytest" 2>$null | Out-Null
        $hasPytest = ($LASTEXITCODE -eq 0)
        $ErrorActionPreference = $previous
        if ($hasPytest) { return $candidate }
    }
    return $null
}

$python = Resolve-PythonWithPytest
if ($python) {
    $log = Join-Path $LogDir "emrtd-$Stamp.log"
    Push-Location (Join-Path $ProjectRoot "emrtd_pm3")
    try {
        $previous = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        & $python -m pytest -q 2>&1 | Tee-Object $log | Out-Null
        $rc = $LASTEXITCODE
        $ErrorActionPreference = $previous
    } finally {
        Pop-Location
    }
    $tail = (Get-Content $log | Select-Object -Last 1)
    $summary += "emrtd : rc=$rc $tail"
    if ($rc -ne 0) { $failures++ }
} else {
    $summary += "emrtd : PYTHON NOT FOUND (skipped)"
}

# Git push audit self-tests
$auditTests = Join-Path $ProjectRoot "tests\git-audit\run-tests.ps1"
if (Test-Path $auditTests) {
    $log = Join-Path $LogDir "git-audit-$Stamp.log"
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $auditTests 2>&1 | Tee-Object $log | Out-Null
    $rc = $LASTEXITCODE
    $tail = (Get-Content $log | Select-Object -Last 1)
    $summary += "git-audit : rc=$rc $tail"
    if ($rc -ne 0) { $failures++ }
}

Write-Output "Test batch: $Stamp"
Write-Output "Reports  : $ReportDir"
Write-Output ""
$summary | ForEach-Object { Write-Output $_ }
Write-Output ""
if ($failures -gt 0) {
    Write-Output "FAILED: $failures suite(s)"
    exit 1
}
Write-Output "All test suites passed."
exit 0
