[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern("^v[0-9A-Za-z._-]+$")]
    [string]$Version,

    [Parameter(Mandatory = $true)]
    [string]$StagingDir,

    [string]$OutputRoot,

    [switch]$Force
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $ProjectRoot "artifacts\publish\win-x64"
}

if (-not (Test-Path $StagingDir)) {
    Write-Host "Staging directory not found: $StagingDir" -ForegroundColor Red
    exit 2
}
$StagingDir = (Resolve-Path $StagingDir).Path
$versionDir = Join-Path $OutputRoot $Version
$archiveName = Split-Path $StagingDir -Leaf
$archivePath = Join-Path $versionDir "$archiveName.zip"

if (Test-Path $versionDir) {
    if (-not $Force) {
        Write-Host "Release directory already exists: $versionDir (use -Force only for an intentionally new build)" -ForegroundColor Red
        exit 3
    }
}

if (-not $PSCmdlet.ShouldProcess($versionDir, "Create release package $Version")) {
    Write-Output "WhatIf: would package $StagingDir -> $versionDir"
    exit 0
}

New-Item -ItemType Directory -Force -Path $versionDir | Out-Null

Add-Type -AssemblyName System.IO.Compression.FileSystem
if (Test-Path $archivePath) { Remove-Item $archivePath -Force }
[System.IO.Compression.ZipFile]::CreateFromDirectory(
    $StagingDir, $archivePath, [System.IO.Compression.CompressionLevel]::Optimal, $true)

$files = Get-ChildItem $StagingDir -Recurse -File | Sort-Object FullName
$sums = foreach ($file in $files) {
    $hash = (Get-FileHash $file.FullName -Algorithm SHA256).Hash
    $relative = $file.FullName.Substring($StagingDir.Length).TrimStart("\").Replace("\", "/")
    "$hash  $relative"
}
$zipHash = (Get-FileHash $archivePath -Algorithm SHA256).Hash
$sums += "$zipHash  $archiveName.zip"
$sumsFile = Join-Path $versionDir "SHA256SUMS.txt"
Set-Content -Path $sumsFile -Value $sums -Encoding ASCII

$manifest = @(
    "version=$Version"
    "created_utc=$((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))"
    "source_dir=$StagingDir"
    "archive=$archiveName.zip"
    "archive_bytes=$((Get-Item $archivePath).Length)"
    "archive_sha256=$zipHash"
)
Set-Content -Path (Join-Path $versionDir "MANIFEST.txt") -Value $manifest -Encoding UTF8

Write-Output "Package: $archivePath"
Write-Output "SHA256 : $zipHash"
Write-Output "Sums   : $sumsFile"
exit 0
