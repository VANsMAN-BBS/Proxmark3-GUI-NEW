param(
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BasePackage = Join-Path $ProjectRoot "artifacts\publish\win-x64\v0.4.1\Proxmark3-GUI-v0.4.1-win-x64"
$LatestRoot = Join-Path $ProjectRoot "artifacts\latest\win-x64"
$LatestDir = Join-Path $LatestRoot "Proxmark3-GUI-latest-win-x64"
$ReleaseExe = Join-Path $ProjectRoot "build\qt515-mingw64\release\release\Proxmark3GUI.exe"

if (-not (Test-Path $BasePackage)) {
    Write-Host "Base package not found: $BasePackage" -ForegroundColor Red
    exit 2
}

if (-not $NoBuild) {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build.ps1") -Configuration Release
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Release build failed; latest package was not updated." -ForegroundColor Red
        exit 1
    }
}

if (-not (Test-Path $ReleaseExe)) {
    Write-Host "Release executable not found: $ReleaseExe" -ForegroundColor Red
    exit 2
}

# Safety: only mirror inside artifacts/latest.
$resolvedLatest = Join-Path $ProjectRoot "artifacts\latest"
if (-not $LatestDir.StartsWith($resolvedLatest, [System.StringComparison]::OrdinalIgnoreCase)) {
    Write-Host "Refusing to update outside artifacts/latest." -ForegroundColor Red
    exit 3
}

New-Item -ItemType Directory -Force -Path $LatestRoot | Out-Null
if (-not (Test-Path $LatestDir)) {
    New-Item -ItemType Directory -Force -Path $LatestDir | Out-Null
}

# Mirror the verified v0.4.1 portable tree, then replace only the application exe.
& robocopy $BasePackage $LatestDir /MIR /NFL /NDL /NJH /R:1 /W:1 /MT:8 | Out-Null
if ($LASTEXITCODE -ge 8) {
    Write-Host "robocopy failed: $LASTEXITCODE" -ForegroundColor Red
    exit 1
}

Copy-Item $ReleaseExe (Join-Path $LatestDir "Proxmark3GUI.exe") -Force

# Never ship local settings or logs.
foreach ($name in @("GUIsettings.ini", "logs")) {
    $path = Join-Path $LatestDir $name
    if (Test-Path $path) { Remove-Item $path -Recurse -Force }
}
Get-ChildItem $LatestDir -Recurse -File -Include "*.log" -ErrorAction SilentlyContinue | Remove-Item -Force

$exeHash = (Get-FileHash (Join-Path $LatestDir "Proxmark3GUI.exe") -Algorithm SHA256).Hash
$gitExe = (Get-Command git -ErrorAction SilentlyContinue).Source
if (-not $gitExe) {
    foreach ($candidate in @("C:\msys64\usr\bin\git.exe", "C:\Program Files\Git\cmd\git.exe")) {
        if (Test-Path $candidate) { $gitExe = $candidate; break }
    }
}
$commit = "unknown"
if ($gitExe) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $detected = & $gitExe -C $ProjectRoot rev-parse --short HEAD 2>$null
    if ($LASTEXITCODE -eq 0 -and $detected) { $commit = $detected.Trim() }
    $ErrorActionPreference = $previous
}
$info = @(
    "build_type=local latest test package (not a release)"
    "commit=$commit"
    "built_utc=$((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))"
    "exe_sha256=$exeHash"
    "base_package=v0.4.1 portable tree"
    "note=overwritten by scripts/update-latest-build.ps1 after every change"
)
Set-Content -Path (Join-Path $LatestDir "BUILD_INFO.txt") -Value $info -Encoding UTF8

$required = @("Proxmark3GUI.exe", "platforms\qwindows.dll", "emrtd\python.exe", "LICENSE", "README.txt")
foreach ($item in $required) {
    if (-not (Test-Path (Join-Path $LatestDir $item))) {
        Write-Host "Incomplete package, missing: $item" -ForegroundColor Red
        exit 1
    }
}

Write-Output "Latest package: $LatestDir"
Write-Output "Commit        : $commit"
Write-Output "EXE SHA256    : $exeHash"
exit 0
