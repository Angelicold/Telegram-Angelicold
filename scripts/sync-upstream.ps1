#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Sync this Telegram fork with upstream telegramdesktop/tdesktop while keeping
    the local custom commits on top.

.DESCRIPTION
    Default run is a DRY RUN: it fetches upstream, determines the latest stable
    tag, and prints exactly what would change. Nothing is modified.

    Use -Apply to actually move your branch onto the new upstream release.
    By default your custom commits are REBASED on top of the new release
    (clean linear history, requires a force-push afterwards). Use -Merge to
    merge instead (no history rewrite, no force-push, keeps merge commits).

    Safety:
      * A backup tag 'backup/pre-sync-<timestamp>' is created at the old HEAD.
        Undo everything with:  git reset --hard <that tag>
      * Uncommitted local changes are auto-stashed and restored afterwards.
      * On conflict the script stops and prints how to continue or abort.

.PARAMETER Tag
    Upstream tag to sync to (e.g. v7.2.10). Default: latest stable v* tag.

.PARAMETER Apply
    Perform the sync. Without it the script only reports (dry run).

.PARAMETER Merge
    Use merge instead of rebase.

.PARAMETER Remote
    Upstream remote name. Default: upstream.

.EXAMPLE
    ./scripts/sync-upstream.ps1
    ./scripts/sync-upstream.ps1 -Apply
    ./scripts/sync-upstream.ps1 -Apply -Tag v7.2.10
    ./scripts/sync-upstream.ps1 -Apply -Merge
#>
[CmdletBinding()]
param(
    [string]$Tag,
    [switch]$Apply,
    [switch]$Merge,
    [string]$Remote = 'upstream'
)

# Note: we do NOT set $ErrorActionPreference='Stop' so that git writing to
# stderr does not get turned into a terminating error. We check $LASTEXITCODE.

function Invoke-Git([string[]]$GitArgs) {
    # Run git, return output; throw on non-zero exit.
    $out = & git @GitArgs 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host ($out -join "`n")
        throw "git $($GitArgs -join ' ') failed (exit $LASTEXITCODE)"
    }
    return $out
}

function Try-Git([string[]]$GitArgs) {
    $out = & git @GitArgs 2>&1
    return [pscustomobject]@{ Ok = ($LASTEXITCODE -eq 0); Out = ($out -join "`n") }
}

# --- Locate repo root ---
$repoRoot = (& git rev-parse --show-toplevel 2>$null)
if ($LASTEXITCODE -ne 0) { throw "Not inside a git repository." }
Set-Location $repoRoot

# --- Verify upstream remote ---
$remotes = & git remote
if ($remotes -notcontains $Remote) {
    throw "Remote '$Remote' not found. Add it with:`n  git remote add $Remote https://github.com/telegramdesktop/tdesktop.git"
}

Write-Host "==> Fetching '$Remote' ..." -ForegroundColor Cyan
Invoke-Git @('fetch', $Remote, '--tags', '--prune', '--no-recurse-submodules') | Out-Null

# --- Resolve target tag ---
if (-not $Tag) {
    $Tag = & git tag -l 'v*' --sort=-v:refname |
        Where-Object { $_ -notmatch 'beta|alpha|rc|dev' } |
        Select-Object -First 1
    if (-not $Tag) { throw "Could not determine latest stable tag." }
}
$null = Invoke-Git @('rev-parse', '--verify', "$Tag^{commit}")

$branch = (& git rev-parse --abbrev-ref HEAD).Trim()
$behind = (& git rev-list --count "HEAD..$Tag").Trim()
$custom = @(& git log --oneline "$Tag..HEAD")
$customCount = $custom.Count
$dirty = @(& git status --porcelain --untracked-files=no)
$isDirty = $dirty.Count -gt 0

Write-Host ""
Write-Host "Branch:          $branch"
Write-Host "Upstream target: $Tag"
Write-Host "Behind upstream: $behind commit(s)"
Write-Host "Your commits to keep on top ($customCount):"
if ($customCount -gt 0) { $custom | ForEach-Object { Write-Host "  $_" } }
else { Write-Host "  (none)" }

# --- Dry run ---
if (-not $Apply) {
    Write-Host ""
    Write-Host "DRY RUN - nothing changed." -ForegroundColor Yellow
    if ($isDirty) {
        Write-Host "Uncommitted local changes (will be auto-stashed & restored on -Apply):" -ForegroundColor Yellow
        $dirty | ForEach-Object { Write-Host "  $_" }
    }
    $mergeFlag = if ($Merge) { '-Merge ' } else { '' }
    Write-Host ""
    Write-Host "To apply: ./scripts/sync-upstream.ps1 -Apply $mergeFlag-Tag $Tag"
    return
}

if ($behind -eq '0' -and $customCount -ge 0) {
    Write-Host ""
    Write-Host "Already up to date with $Tag - nothing to do." -ForegroundColor Green
    return
}

# --- Apply ---
$stamp  = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = "backup/pre-sync-$stamp"
Write-Host ""
Write-Host "==> Backup tag '$backup' at current HEAD" -ForegroundColor Cyan
Invoke-Git @('tag', $backup, 'HEAD') | Out-Null

$stashed = $false
if ($isDirty) {
    Write-Host "==> Stashing local changes" -ForegroundColor Cyan
    Invoke-Git @('stash', 'push', '-m', "sync-upstream-$stamp") | Out-Null
    $stashed = $true
}

$strategy = if ($Merge) { 'merge' } else { 'rebase' }
Write-Host "==> Running $strategy onto $Tag" -ForegroundColor Cyan
$res = if ($Merge) { Try-Git @('merge', '--no-edit', $Tag) } else { Try-Git @('rebase', $Tag) }
Write-Host $res.Out

if (-not $res.Ok) {
    Write-Host ""
    Write-Host "!! $strategy stopped with conflicts." -ForegroundColor Red
    Write-Host "   Resolve the conflicted files, then:"
    if ($Merge) {
        Write-Host "     git add -A ; git commit --no-edit        # finish the merge"
    } else {
        Write-Host "     git add -A ; git rebase --continue"
        Write-Host "   or give up:  git rebase --abort"
    }
    if ($stashed) {
        Write-Host "   Your local changes are stashed - restore later with: git stash pop"
    }
    Write-Host "   Full undo: git reset --hard $backup"
    exit 1
}

# --- Keep 'dev' as a pristine upstream mirror when it is a clean ancestor ---
$devAnc = Try-Git @('merge-base', '--is-ancestor', 'dev', $Tag)
if ($devAnc.Ok) {
    Write-Host "==> Fast-forwarding 'dev' to $Tag (pristine mirror)" -ForegroundColor Cyan
    Invoke-Git @('branch', '-f', 'dev', $Tag) | Out-Null
} else {
    Write-Host "==> Skipping 'dev' update (it has commits not in $Tag)."
}

# --- Submodules (required for a successful build) ---
Write-Host "==> Updating submodules (can take a while)" -ForegroundColor Cyan
$sm = Try-Git @('submodule', 'update', '--init', '--recursive')
if (-not $sm.Ok) {
    Write-Host $sm.Out
    Write-Host "!! Some submodules failed to update - the build may fail until this is fixed." -ForegroundColor Yellow
}

# --- Re-apply local build tweaks that live inside submodules ---
# 'git submodule update' resets submodule working trees, so our low-memory
# MSVC tweaks in the 'cmake' submodule must be re-applied from the patch.
$cmakePatch = Join-Path $repoRoot 'patches/cmake-local-build.patch'
if (Test-Path $cmakePatch) {
    Write-Host "==> Re-applying local cmake build tweaks" -ForegroundColor Cyan
    $already = Try-Git @('-C', 'cmake', 'apply', '--reverse', '--check', $cmakePatch)
    if ($already.Ok) {
        Write-Host "   Already applied - skipping."
    } else {
        $ap = Try-Git @('-C', 'cmake', 'apply', $cmakePatch)
        if (-not $ap.Ok) {
            Write-Host $ap.Out
            Write-Host "!! Could not auto-apply patches/cmake-local-build.patch - apply it manually or regenerate it." -ForegroundColor Yellow
        }
    }
}

# --- Restore local changes ---
if ($stashed) {
    Write-Host "==> Restoring local changes (git stash pop)" -ForegroundColor Cyan
    $pop = Try-Git @('stash', 'pop')
    Write-Host $pop.Out
    if (-not $pop.Ok) {
        Write-Host "!! 'git stash pop' hit conflicts - resolve them; your changes are still in the stash." -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "Done. Branch '$branch' now sits on $Tag with your $customCount commit(s) on top." -ForegroundColor Green
Write-Host "Next steps:"
Write-Host "  1) Reconfigure + rebuild, then smoke-test the app."
if ($Merge) {
    Write-Host "  2) Push:  git push origin $branch"
} else {
    Write-Host "  2) Push:  git push --force-with-lease origin $branch"
}
Write-Host "  Undo everything:  git reset --hard $backup"
