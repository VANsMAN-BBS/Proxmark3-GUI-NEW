param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildRoot = Join-Path $ProjectRoot "build\qt515-mingw64"
$ConfigurationDir = Join-Path $BuildRoot $Configuration.ToLowerInvariant()
$ProjectFile = Join-Path $ProjectRoot "src\Proxmark3GUI.pro"

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
if (-not $qmake) { Write-Host "qmake-qt5 not found. Install Qt 5.15 / MSYS2 UCRT64 and add it to PATH." -ForegroundColor Red
    exit 2 }
if (-not $make) { Write-Host "mingw32-make not found. Install MSYS2 UCRT64 and add it to PATH." -ForegroundColor Red
    exit 2 }

$qtVersion = (& $qmake -query QT_VERSION).Trim()
$qtBin = Split-Path $qmake -Parent
$env:PATH = $qtBin + ";" + $env:PATH

if ($Clean -and (Test-Path $ConfigurationDir)) {
    Remove-Item $ConfigurationDir -Recurse -Force
    Write-Output "cleaned $ConfigurationDir"
}
New-Item -ItemType Directory -Force -Path $ConfigurationDir | Out-Null

Push-Location $ConfigurationDir
try {
    $configArg = if ($Configuration -eq "Debug") { "CONFIG+=debug" } else { "CONFIG+=release" }
    Write-Output "qmake : $qmake ($qtVersion)"
    Write-Output "make  : $make"
    Write-Output "config: $Configuration"
    Write-Output "dir   : $ConfigurationDir"

    $previous = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $qmake $ProjectFile -spec win32-g++ $configArg 2>&1 | Tee-Object "qmake.log" | Out-Null
    $qmakeCode = $LASTEXITCODE
    $ErrorActionPreference = $previous
    if ($qmakeCode -ne 0) { Write-Error "qmake failed (exit $qmakeCode). See qmake.log"; exit 1 }

    $previous = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $make -j $env:NUMBER_OF_PROCESSORS 2>&1 | Tee-Object "build.log" | Out-Null
    $makeCode = $LASTEXITCODE
    $ErrorActionPreference = $previous
    if ($makeCode -ne 0) { Write-Error "build failed (exit $makeCode). See build.log"; exit 1 }

    $exe = Get-ChildItem $ConfigurationDir -Recurse -Filter "Proxmark3GUI.exe" |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $exe) { Write-Error "Proxmark3GUI.exe was not produced."; exit 1 }

    $warnings = (Select-String -Path "build.log" -Pattern "warning:" -ErrorAction SilentlyContinue | Measure-Object).Count
    $errors = (Select-String -Path "build.log" -Pattern "error:" -ErrorAction SilentlyContinue | Measure-Object).Count
    Write-Output ""
    Write-Output "Build OK: $($exe.FullName)"
    Write-Output "Size    : $($exe.Length) bytes"
    Write-Output "Warnings: $warnings  Errors: $errors"
    exit 0
} finally {
    Pop-Location
}
