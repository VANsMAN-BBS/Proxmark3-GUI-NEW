param(
    [switch]$SkipBuild,
    [string]$OutputDir
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$TestDir = Join-Path $ProjectRoot "tests\mifareui"
$BuildDir = Join-Path $ProjectRoot "build\qt515-mingw64\tests\mifareui"
if (-not $OutputDir) {
    $OutputDir = Join-Path $ProjectRoot "artifacts\stage322-screenshots"
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
Get-ChildItem $OutputDir -File -Filter "stage322-*.png" -ErrorAction SilentlyContinue | Remove-Item -Force
Get-ChildItem $OutputDir -File -Filter "capture-*.txt" -ErrorAction SilentlyContinue | Remove-Item -Force

$themes = @(
    @{ Name = "none"; Qss = "" },
    @{ Name = "dark"; Qss = "src/qdarkstyle/dark/darkstyle.qss" },
    @{ Name = "light"; Qss = "src/qdarkstyle/light/lightstyle.qss" }
)
$results = @()
foreach ($dpr in @("1", "1.25", "1.5", "2")) {
    foreach ($theme in $themes) {
        $env:QT_SCALE_FACTOR = $dpr
        $env:QT_AUTO_SCREEN_SCALE_FACTOR = "0"
        $env:STAGE322_CAPTURE_DIR = $OutputDir
        $env:STAGE322_THEME = $theme.Name
        $env:STAGE322_QSS = $theme.Qss
        $report = Join-Path $OutputDir ("capture-{0}-dpr{1}.txt" -f $theme.Name, $dpr)
        & $exe "dpiScreenshotArtifacts" -o "$report,txt" 2>&1 | Out-Null
        $rc = $LASTEXITCODE
        $totals = (Get-Content $report -ErrorAction SilentlyContinue |
            Select-String "^Totals:" | Select-Object -First 1).Line
        if (-not $totals) { $totals = "no totals line" }
        $results += "theme=$($theme.Name) dpr=$dpr rc=$rc $totals"
        if ($rc -ne 0) {
            Write-Host "Capture failed: theme=$($theme.Name) dpr=$dpr (rc=$rc)." -ForegroundColor Red
            exit 1
        }
    }
}

foreach ($name in @("QT_SCALE_FACTOR", "QT_AUTO_SCREEN_SCALE_FACTOR", "STAGE322_CAPTURE_DIR",
                    "STAGE322_THEME", "STAGE322_QSS")) {
    Remove-Item ("Env:\" + $name) -ErrorAction SilentlyContinue
}

Write-Output "Screenshots: $OutputDir"
$results | ForEach-Object { Write-Output $_ }
exit 0
