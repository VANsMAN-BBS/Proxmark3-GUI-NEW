# Git push audit policy

This repository contains a local pre-push audit that classifies every file in a
push range before Git contacts GitHub. It never edits, deletes or rewrites
anything: it only blocks the push and prints what must be removed, sanitized or
reviewed.

> This is a guard for **future** pushes. It cannot remove content that is
> already present in published commits. Rewriting history is out of scope.

## Files

| File | Purpose |
|---|---|
| `.git-push-policy.json` | Path/extension rules and content patterns |
| `scripts/git-push-audit.ps1` | Audit engine (also used by the hook) |
| `scripts/audit-git-content.ps1` | Manual entry point |
| `scripts/setup-git-hooks.ps1` | Installs `core.hooksPath` safely |
| `.githooks/pre-push` | Git hook wrapper |
| `tests/git-audit/run-tests.ps1` | Synthetic tests for the audit rules |

## Classification

- **ALLOW** — source code, ordinary documents, project resources and
  allow-listed data (station code tables, icons, screenshots referenced by the
  README).
- **BLOCK** — card dumps and card data, local settings/logs, backups, build
  output, credentials, private keys, Windows user paths, and local project
  absolute paths.
- **REVIEW** — unknown binaries, new datasets, unknown file types, device
  port numbers, long hex blobs. Anything the policy does not recognise is
  REVIEW by default; unknown files are never allowed automatically.

Decision precedence: **BLOCK > REVIEW > ALLOW**.

## Install

```powershell
powershell -ExecutionPolicy Bypass -File scripts/setup-git-hooks.ps1
```

The installer:

1. refuses to run if `core.hooksPath` is already set to something other than
   `.githooks`;
2. backs up an existing `.git/hooks/pre-push` instead of overwriting it;
3. sets the repository-local `core.hooksPath=.githooks` (no global config).

Disable:

```powershell
git config --local --unset core.hooksPath
```

## Manual audit

Audit the current branch against its remote-tracking base:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/audit-git-content.ps1
```

Audit an explicit range:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/audit-git-content.ps1 -Range origin/upgrade/v0.4.1-polish..HEAD
```

Machine-readable result:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/audit-git-content.ps1 -Json
```

Exit codes: `0` allow, `1` block, `2` review, `3` configuration error.

## Pre-push behaviour

`git push` invokes the hook for each ref. The hook audits `remote..local` and
exits with the worst decision:

- `0` — push continues;
- `1` — push rejected (BLOCK);
- `2` — push rejected (REVIEW).

A first push (remote SHA all zeros) audits the entire pushed tree. An empty
range is treated as ALLOW.

## Tests

```powershell
powershell -ExecutionPolicy Bypass -File tests/git-audit/run-tests.ps1
```

The suite creates temporary repositories and verifies: safe source and
documentation pass; local paths, tokens, private keys, card dumps and card data
are blocked; unknown binaries are REVIEW; deletions pass; historical secrets in
the range are found; empty ranges and first pushes behave correctly; existing
hooks are backed up and custom `core.hooksPath` values are not overwritten.

All test secrets are synthetic strings. No real credentials or card data are
used.
