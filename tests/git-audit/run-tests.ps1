param(
    [switch]$KeepTemp
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

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$sourcePolicy = Join-Path $repo ".git-push-policy.json"
$sourceAudit = Join-Path $repo "scripts/git-push-audit.ps1"
$sourceSetup = Join-Path $repo "scripts/setup-git-hooks.ps1"
$sourceHook = Join-Path $repo ".githooks/pre-push"

$temp = Join-Path $env:TEMP ("proxmark3_git_audit_" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $temp | Out-Null

$failures = 0
$tests = 0

function Assert-Equal {
    param($Expected, $Actual, [string]$Label)
    $script:tests++
    if ($Expected -ne $Actual) {
        $script:failures++
        Write-Output ("FAIL {0}: expected {1}, got {2}" -f $Label, $Expected, $Actual)
    } else {
        Write-Output ("PASS {0}" -f $Label)
    }
}

function Invoke-Audit {
    param([string]$Range, [string]$PrePushLine)
    $arguments = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $temp "scripts/git-push-audit.ps1"), "-RepoRoot", $temp)
    if ($Range) { $arguments += @("-Range", $Range) }
    if ($PrePushLine) { $arguments += @("-PrePush", "-PrePushLine", $PrePushLine) }
    & powershell.exe @arguments | Out-Null
    return $LASTEXITCODE
}

function Write-TextFile {
    param([string]$Path, [string]$Content)
    $full = Join-Path $temp $Path
    $dir = Split-Path $full -Parent
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    Set-Content -Path $full -Value $Content -Encoding UTF8
}

function Commit-All {
    param([string]$Message)
    Push-Location $temp
    & $GitExe add -A | Out-Null
    & $GitExe -c user.name="Audit Test" -c user.email="audit@example.invalid" commit -m $Message --quiet | Out-Null
    $sha = (& $GitExe rev-parse HEAD).Trim()
    Pop-Location
    return $sha
}

try {
    New-Item -ItemType Directory -Force -Path (Join-Path $temp "scripts") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $temp ".githooks") | Out-Null
    Copy-Item $sourcePolicy (Join-Path $temp ".git-push-policy.json")
    Copy-Item $sourceAudit (Join-Path $temp "scripts/git-push-audit.ps1")
    Copy-Item $sourceSetup (Join-Path $temp "scripts/setup-git-hooks.ps1")
    Copy-Item $sourceHook (Join-Path $temp ".githooks/pre-push")

    Push-Location $temp
    & $GitExe init --quiet | Out-Null
    & $GitExe config user.name "Audit Test"
    & $GitExe config user.email "audit@example.invalid"
    Pop-Location

    Write-TextFile "src/main.cpp" "int main() { return 0; }"
    Write-TextFile "README.md" "# Test project"
    $base = Commit-All "base"

    Write-TextFile "src/safe.cpp" "int value = 1;"
    $safe = Commit-All "safe source"
    Assert-Equal 0 (Invoke-Audit -Range "$base..$safe") "safe source file is allowed"

    Write-TextFile "docs/notes.md" "# Notes`nNo secrets here."
    $doc = Commit-All "safe document"
    Assert-Equal 0 (Invoke-Audit -Range "$safe..$doc") "safe markdown is allowed"

    $bs = [char]92
    $localPath = "E:" + $bs + "Opencode_project" + $bs + "Proxmark3GUI-master"
    Write-TextFile "docs/path-note.md" ("Path: " + $localPath)
    $pathCommit = Commit-All "local path"
    Assert-Equal 1 (Invoke-Audit -Range "$doc..$pathCommit") "local project path is blocked"

    $token = "ghp_" + ("A" * 36)
    Write-TextFile "src/token.txt" ("token=" + $token)
    $tokenCommit = Commit-All "token"
    Assert-Equal 1 (Invoke-Audit -Range "$pathCommit..$tokenCommit") "github token is blocked"

    $keyBody = "-----BEGIN " + "RSA " + "PRIVATE" + " KEY-----"
    Write-TextFile "src/key.pem" $keyBody
    $keyCommit = Commit-All "private key"
    Assert-Equal 1 (Invoke-Audit -Range "$tokenCommit..$keyCommit") "private key is blocked"

    $idm = "01010212F622" + "131C"
    Write-TextFile "pasmo_dump.json" ("{`"idm`":`"" + $idm + "`"}")
    $dumpCommit = Commit-All "card dump"
    Assert-Equal 1 (Invoke-Audit -Range "$keyCommit..$dumpCommit") "real card dump path is blocked"

    Write-TextFile "pasmo_card.json" ("{`"card`":{`"balance`":157}}")
    $cardCommit = Commit-All "card data"
    Assert-Equal 1 (Invoke-Audit -Range "$dumpCommit..$cardCommit") "pasmo_card.json is blocked"

    $binary = [byte[]](1, 2, 3, 4, 5, 0, 255)
    $binPath = Join-Path $temp "assets/unknown.dat"
    New-Item -ItemType Directory -Force -Path (Split-Path $binPath -Parent) | Out-Null
    [System.IO.File]::WriteAllBytes($binPath, $binary)
    $binCommit = Commit-All "unknown binary"
    Assert-Equal 2 (Invoke-Audit -Range "$cardCommit..$binCommit") "unknown binary requires review"

    Write-TextFile "notes/new.md" "harmless"
    $unknownDoc = Commit-All "new document"
    Assert-Equal 0 (Invoke-Audit -Range "$binCommit..$unknownDoc") "ordinary document is allowed"

    Push-Location $temp
    & $GitExe rm -q pasmo_dump.json
    Pop-Location
    $deleted = Commit-All "remove dump"
    Assert-Equal 0 (Invoke-Audit -Range "$unknownDoc..$deleted") "deleting a blocked file passes"

    Assert-Equal 1 (Invoke-Audit -Range "$base..$deleted") "history with a blocked file is detected"
    Assert-Equal 0 (Invoke-Audit -Range "$deleted..$deleted") "empty range does not fail open"

    Assert-Equal 0 (Invoke-Audit -PrePushLine "$base|0000000000000000000000000000000000000000|refs/heads/main") "first push audits the tree and allows safe content"

    Push-Location $temp
    & $GitExe config --local core.hooksPath ".githooks"
    Pop-Location
    Assert-Equal 0 $LASTEXITCODE "custom hooksPath can be configured"

    $temp2 = Join-Path $env:TEMP ("proxmark3_hooktest_" + [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $temp2 | Out-Null
    Copy-Item $sourceSetup (Join-Path $temp2 "setup.ps1")
    New-Item -ItemType Directory -Force -Path (Join-Path $temp2 ".githooks") | Out-Null
    Copy-Item $sourceHook (Join-Path $temp2 ".githooks/pre-push")
    Push-Location $temp2
    & $GitExe init --quiet | Out-Null
    New-Item -ItemType Directory -Force -Path ".git/hooks" | Out-Null
    Set-Content ".git/hooks/pre-push" "#!/bin/sh`nexit 0" -Encoding ASCII
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $temp2 "setup.ps1") -RepoRoot $temp2 | Out-Null
    $hookExit = $LASTEXITCODE
    Pop-Location
    Assert-Equal 0 $hookExit "hook setup succeeds with an existing legacy hook"
    $backups = Get-ChildItem (Join-Path $temp2 ".git/hooks") -Filter "pre-push.backup-*" -ErrorAction SilentlyContinue
    Assert-Equal 1 @($backups).Count "existing legacy hook is backed up"

    Push-Location $temp2
    & $GitExe config --local core.hooksPath "custom-hooks"
    Pop-Location
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $temp2 "setup.ps1") -RepoRoot $temp2 | Out-Null
    Assert-Equal 2 $LASTEXITCODE "existing custom hooksPath is not overwritten"
} finally {
    if (-not $KeepTemp) {
        Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue
        if ($temp2) { Remove-Item $temp2 -Recurse -Force -ErrorAction SilentlyContinue }
    } else {
        Write-Output "temp: $temp"
        if ($temp2) { Write-Output "temp2: $temp2" }
    }
}

Write-Output ""
Write-Output ("Tests: {0}, failures: {1}" -f $tests, $failures)
if ($failures -gt 0) { exit 1 }
exit 0
