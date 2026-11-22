param(
    [switch]$SkipBuild,
    [string]$OutputDir
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$TestDir = Join-Path $ProjectRoot "tests\mifareui"
$BuildDir = Join-Path $ProjectRoot "build\qt515-mingw64\tests\mifareui"
if (-not $OutputDir) {
    $OutputDir = Join-Path $ProjectRoot "artifacts\stage33-screenshots"
}

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
if (-not $qmake -or -not $make) {
    Write-Host "Qt 5.15 / MinGW tools not found." -ForegroundColor Red
    exit 2
}
$env:PATH = (Split-Path $qmake -Parent) + ";" + $env:PATH

if (-not $SkipBuild) {
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
    Push-Location $BuildDir
    try {
        $previous = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        & $qmake (Join-Path $TestDir "mifareui_tests.pro") -spec win32-g++ "CONFIG+=release" 2>&1 |
            Tee-Object "qmake.log" | Out-Null
        $qmakeCode = $LASTEXITCODE
        if ($qmakeCode -eq 0) {
            & $make -j $env:NUMBER_OF_PROCESSORS 2>&1 | Tee-Object "build.log" | Out-Null
            $makeCode = $LASTEXITCODE
        } else {
            $makeCode = 1
        }
        $ErrorActionPreference = $previous
    } finally {
        Pop-Location
    }
    if ($qmakeCode -ne 0 -or $makeCode -ne 0) {
        Write-Host "mifareui test build failed." -ForegroundColor Red
        exit 1
    }
}

$exe = Join-Path $BuildDir "release\tst_mifareui.exe"
if (-not (Test-Path $exe)) {
    Write-Host "Test executable not found: $exe" -ForegroundColor Red
    exit 2
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
# Remove only artifacts produced by this script so an old DPR does not mix
# with a new run.
Get-ChildItem $OutputDir -File -Filter "stage33-*.png" -ErrorAction SilentlyContinue | Remove-Item -Force
Get-ChildItem $OutputDir -File -Filter "geometry-dpr*.txt" -ErrorAction SilentlyContinue | Remove-Item -Force
Get-ChildItem $OutputDir -File -Filter "capture-dpr*.txt" -ErrorAction SilentlyContinue | Remove-Item -Force

$results = @()
foreach ($dpr in @("1", "1.25", "1.5", "2")) {
    $env:QT_SCALE_FACTOR = $dpr
    $env:QT_AUTO_SCREEN_SCALE_FACTOR = "0"
    $env:STAGE33_CAPTURE_DIR = $OutputDir
    $report = Join-Path $OutputDir ("capture-dpr{0}.txt" -f $dpr)
    & $exe "dpiScreenshotArtifacts" -o "$report,txt" 2>&1 | Out-Null
    $rc = $LASTEXITCODE
    $totals = (Get-Content $report -ErrorAction SilentlyContinue |
        Select-String "^Totals:" | Select-Object -First 1).Line
    if (-not $totals) { $totals = "no totals line" }
    $results += "dpr=$dpr rc=$rc $totals"
    if ($rc -ne 0) {
        Write-Host "DPI capture failed at $dpr (rc=$rc)." -ForegroundColor Red
        exit 1
    }
}

Remove-Item Env:\QT_SCALE_FACTOR -ErrorAction SilentlyContinue
Remove-Item Env:\QT_AUTO_SCREEN_SCALE_FACTOR -ErrorAction SilentlyContinue
Remove-Item Env:\STAGE33_CAPTURE_DIR -ErrorAction SilentlyContinue

Write-Output "Screenshots: $OutputDir"
$results | ForEach-Object { Write-Output $_ }
exit 0
