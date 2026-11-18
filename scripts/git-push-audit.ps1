param(
    [switch]$PrePush,
    [string]$PrePushLine,
    [string]$Range,
    [switch]$Json,
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


function Invoke-Git {
    param([string[]]$Arguments)
    $output = & $GitExe @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw ("git " + ($Arguments -join " ") + " failed: " + ($output -join " "))
    }
    return $output
}

$root = if ($RepoRoot) { $RepoRoot } else { Convert-ToWindowsPath ((& $GitExe rev-parse --show-toplevel).Trim()) }
if (-not $root) { Write-Error "Not inside a git repository."; exit 3 }
Set-Location $root

if (-not (Test-Path ".git-push-policy.json")) {
    Write-Error "Missing .git-push-policy.json"
    exit 3
}
$policy = Get-Content ".git-push-policy.json" -Raw | ConvertFrom-Json

function Get-EmptyTree {
    # Git SHA-1 empty tree. This repository uses the default SHA-1 object format.
    return "4b825dc642cb6eb9a060e54bf8d69288fbee4904"
}

function Resolve-AuditTarget {
    if ($PrePushLine) {
        $parts = $PrePushLine.Split("|")
        if ($parts.Count -lt 3) { throw "Invalid pre-push line" }
        $localSha = $parts[0]
        $remoteSha = $parts[1]
        $ref = $parts[2]
        if ($localSha -match "^0+$") { return $null }
        $base = if ($remoteSha -match "^0+$") { Get-EmptyTree } else { $remoteSha }
        return [pscustomobject]@{ Base = $base; Head = $localSha; Label = "$base..$localSha ($ref)" }
    }

    if ($Range) {
        if ($Range -match "\.\.") {
            $sides = $Range -split "\.\.", 2
            $base = if ($sides[0]) { $sides[0] } else { Get-EmptyTree }
            $head = if ($sides[1]) { $sides[1] } else { "HEAD" }
            return [pscustomobject]@{ Base = $base; Head = $head; Label = "$base..$head" }
        }
        return [pscustomobject]@{ Base = $Range; Head = "HEAD"; Label = "$Range..HEAD" }
    }

    $branch = (& $GitExe rev-parse --abbrev-ref HEAD).Trim()
    $upstream = $null
    try { $upstream = (& $GitExe rev-parse --verify "origin/$branch" 2>$null).Trim() } catch { }
    if ($upstream) {
        return [pscustomobject]@{ Base = $upstream; Head = "HEAD"; Label = "origin/$branch..HEAD" }
    }
    $base = Get-EmptyTree
    return [pscustomobject]@{ Base = $base; Head = "HEAD"; Label = "(first push) $base..HEAD" }
}

function Test-PathPattern {
    param([string]$Path, [string]$Pattern)
    $p = $Path.Replace("\", "/")
    $pat = $Pattern.Replace("\", "/")
    if ($pat.EndsWith("/")) { return $p.StartsWith($pat, [System.StringComparison]::OrdinalIgnoreCase) }
    if ($pat.StartsWith("*.")) { return $p.EndsWith($pat.Substring(1), [System.StringComparison]::OrdinalIgnoreCase) }
    return $p.Equals($pat, [System.StringComparison]::OrdinalIgnoreCase)
}

function Get-DecisionName {
    param([int]$Decision)
    switch ($Decision) {
        1 { return "BLOCK" }
        2 { return "REVIEW" }
        default { return "ALLOW" }
    }
}

function Get-PathDecision {
    param([string]$Path)

    if ($policy.allow_exact_paths) {
        foreach ($allowed in $policy.allow_exact_paths) {
            if ($Path.Equals($allowed, [System.StringComparison]::OrdinalIgnoreCase)) {
                return @{ Decision = 0; Reason = "explicitly allow-listed documentation" }
            }
        }
    }

    if ($policy.block_prefixes) {
        foreach ($prefix in $policy.block_prefixes) {
            if ($Path.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
                return @{ Decision = 1; Reason = "development report path is blocked" }
            }
        }
    }
    if ($policy.block_suffixes) {
        foreach ($suffix in $policy.block_suffixes) {
            if ($Path.EndsWith($suffix, [System.StringComparison]::OrdinalIgnoreCase)) {
                return @{ Decision = 1; Reason = "development report file is blocked" }
            }
        }
    }

    foreach ($pattern in $policy.block_paths) {
        if (Test-PathPattern $Path $pattern) { return @{ Decision = 1; Reason = "path matches block rule: $pattern" } }
    }
    foreach ($pattern in $policy.review_paths) {
        if (Test-PathPattern $Path $pattern) { return @{ Decision = 2; Reason = "path needs review: $pattern" } }
    }

    $ext = [System.IO.Path]::GetExtension($Path).ToLowerInvariant()

    $allowPath = $false
    foreach ($pattern in $policy.allow_paths) {
        if (Test-PathPattern $Path $pattern) { $allowPath = $true; break }
    }

    if ($allowPath) { return @{ Decision = 0; Reason = "allow-listed path" } }

    foreach ($pattern in $policy.review_extensions) {
        if ($ext -eq $pattern) { return @{ Decision = 2; Reason = "binary/unknown resource type $ext" } }
    }

    if ($policy.allow_extensions -contains $ext) { return @{ Decision = 0; Reason = "source/document type $ext" } }

    return @{ Decision = 2; Reason = "unknown file type (default REVIEW)" }
}

function Get-ChangedFiles {
    param([string]$Base, [string]$Head)

    $raw = & $GitExe diff --name-status -z --no-renames $Base $Head 2>$null
    if ($LASTEXITCODE -ne 0) { throw "git diff failed for $Base..$Head" }
    $tokens = @()
    if ($raw -is [array]) { $tokens = $raw } else { $tokens = @($raw) }
    $joined = ($tokens -join "`n")
    $parts = $joined.Split([char]0)
    $result = @()
    for ($i = 0; $i + 1 -lt $parts.Count; $i += 2) {
        $status = $parts[$i].Trim()
        $path = $parts[$i + 1]
        if (-not $status -or -not $path) { continue }
        $result += [pscustomobject]@{ Status = $status.Substring(0, 1); Path = $path.Replace("\", "/") }
    }
    return $result
}

function Get-ContentHits {
    param([string]$Head, [string[]]$Paths)

    $hits = @()
    $patterns = @()
    foreach ($pattern in $policy.content_block_patterns) { $patterns += @{ Pattern = $pattern; Decision = 1 } }
    foreach ($pattern in $policy.content_review_patterns) { $patterns += @{ Pattern = $pattern; Decision = 2 } }
    if ($patterns.Count -eq 0 -or $Paths.Count -eq 0) { return $hits }

    $patternFile = Join-Path ([System.IO.Path]::GetTempPath()) ("git-audit-" + [Guid]::NewGuid().ToString("N") + ".txt")
    $patternLines = @()
    foreach ($entry in $patterns) { $patternLines += $entry.Pattern }
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($patternFile, ($patternLines -join "`n"), $utf8NoBom)

    for ($offset = 0; $offset -lt $Paths.Count; $offset += 40) {
        $chunk = $Paths[$offset..([Math]::Min($offset + 39, $Paths.Count - 1))]
        $arguments = @("grep", "-n", "-I", "-P", "--no-color", "-f", $patternFile, $Head, "--")
        $arguments += $chunk
        $output = & $GitExe @arguments 2>$null
        if ($LASTEXITCODE -ne 0) { continue }
        foreach ($line in @($output)) {
            if ($line -notmatch "^[0-9a-fA-F]+:(.+?):(\d+):(.*)$") { continue }
            $path = $Matches[1]
            $lineNo = $Matches[2]
            $text = $Matches[3]
            $decision = 1
            $matched = ""
            foreach ($entry in $patterns) {
                if ($text -match $entry.Pattern) { $decision = $entry.Decision; $matched = $entry.Pattern; break }
            }
            $hits += [pscustomobject]@{ Path = $path; Line = $lineNo; Decision = $decision; Pattern = $matched; Text = $text.Trim() }
        }
    }
    Remove-Item $patternFile -Force -ErrorAction SilentlyContinue
    return $hits
}

$target = Resolve-AuditTarget
if (-not $target) {
    Write-Output "Git Push Audit: nothing to push."
    exit 0
}

$changed = Get-ChangedFiles -Base $target.Base -Head $target.Head
$files = @()
$blocked = @()
$review = @()
$allowed = @()

foreach ($entry in $changed) {
    $pathDecision = Get-PathDecision -Path $entry.Path
    $decision = $pathDecision.Decision
    $reason = $pathDecision.Reason
    if ($entry.Status -eq "D") {
        $decision = 0
        $reason = "deleted in this range"
    }
    $files += [pscustomobject]@{ Path = $entry.Path; Status = $entry.Status; Decision = $decision; Reason = $reason }
}

$contentCandidates = @()
foreach ($file in $files) {
    if ($file.Status -ne "D" -and $file.Decision -ne 1) { $contentCandidates += $file.Path }
}
$contentHits = Get-ContentHits -Head $target.Head -Paths $contentCandidates

foreach ($hit in $contentHits) {
    $file = $files | Where-Object { $_.Path -eq $hit.Path } | Select-Object -First 1
    if (-not $file) { continue }
    if ($hit.Decision -eq 1) {
        $file.Decision = 1
        $file.Reason = "content block rule triggered"
    } elseif ($file.Decision -eq 0) {
        $file.Decision = 2
        $file.Reason = "content review rule triggered"
    }
}

foreach ($file in $files) {
    switch ($file.Decision) {
        1 { $blocked += $file }
        2 { $review += $file }
        default { $allowed += $file }
    }
}

$final = 0
if ($blocked.Count -gt 0) { $final = 1 } elseif ($review.Count -gt 0) { $final = 2 }

if ($Json) {
    $summary = [pscustomobject]@{
        range = $target.Label
        final = (Get-DecisionName $final)
        block = @($blocked | ForEach-Object { $_.Path })
        review = @($review | ForEach-Object { $_.Path })
        allow = @($allowed | ForEach-Object { $_.Path })
        content = @($contentHits | ForEach-Object { "$($_.Path):$($_.Line)" })
    }
    $summary | ConvertTo-Json -Depth 4
} else {
    Write-Output "Git Push Audit"
    Write-Output "=============="
    Write-Output ""
    Write-Output ("Commit Range: " + $target.Label)
    Write-Output ("Files: " + $files.Count)
    Write-Output ""
    Write-Output ("ALLOW: " + $allowed.Count)
    foreach ($file in $allowed) { Write-Output ("  " + $file.Path) }
    Write-Output ""
    Write-Output ("BLOCK: " + $blocked.Count)
    foreach ($file in $blocked) { Write-Output ("  " + $file.Path + "  [" + $file.Reason + "]") }
    Write-Output ""
    Write-Output ("REVIEW: " + $review.Count)
    foreach ($file in $review) { Write-Output ("  " + $file.Path + "  [" + $file.Reason + "]") }
    Write-Output ""
    Write-Output ("Sensitive content: " + $contentHits.Count)
    foreach ($hit in $contentHits) {
        if ($hit.Text.Length -gt 120) { $hit.Text = $hit.Text.Substring(0, 120) }
        Write-Output ("  " + $hit.Path + ":" + $hit.Line + "  -> " + $hit.Text)
    }
    Write-Output ""
    Write-Output ("Final Decision: " + (Get-DecisionName $final))
}

exit $final
