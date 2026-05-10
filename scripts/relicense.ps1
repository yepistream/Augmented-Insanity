# Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# relicense.ps1 -- one-shot license normalisation for Augmented Insanity.
#
# Performs two operations:
#
#   1. Replaces every occurrence of the fork-mark placeholder
#         "Copyright YEAR[-YEAR], NVIDIA CORPORATION[.]"
#      with
#         "Copyright YEAR[-YEAR], Marko Kazimirovic <kazimirovicmarko@photon.me>"
#      across the entire repository. (YEAR-range example written with the
#      literal token YEAR so this very comment does not self-match the regex
#      below if you re-run the script.)
#
#   2. Flips the SPDX identifier from BSL-1.0 to GPL-3.0-or-later for the
#      explicitly-curated allow-list `$GplHeaderFiles` below. These are the
#      files that have been substantively modified beyond the fork-mark
#      copyright bump. BSL-1.0 permits relicensing, so this is legal; the
#      original Collabora copyright lines stay in place per BSL-1.0 terms.
#
# Idempotent. Safe to re-run. Use -DryRun to preview without mutating.
#
# Usage:
#   pwsh scripts/relicense.ps1 [-DryRun] [-RepoRoot <path>]

[CmdletBinding()]
param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

# Match either form of the fork-mark placeholder, year-range preserved:
#   "Copyright YYYY[-YYYY], NVIDIA CORPORATION[.]"
#   "SPDX-FileCopyrightText: YYYY[-YYYY], NVIDIA CORPORATION[.]"   (REUSE-style)
$PlaceholderRegex = '((?:Copyright |SPDX-FileCopyrightText: ))(\d{4}(?:-\d{4})?)(, NVIDIA CORPORATION\.?)'
$ReplacementBase = ', Marko Kazimirovic <kazimirovicmarko@photon.me>'
# Used for accurate hit-count reporting and idempotency check.
$NewMarker = 'Marko Kazimirovic <kazimirovicmarko@photon.me>'
$OldSpdx = "SPDX-License-Identifier: BSL-1.0"
$NewSpdx = "SPDX-License-Identifier: GPL-3.0-or-later"

# Files that have been substantively modified beyond a fork-mark copyright
# bump. Their SPDX must be flipped to GPL-3.0-or-later. Paths are repo-relative.
# Add new entries here when you make substantive changes to additional upstream
# files. (Files entirely authored by Marko -- under src/xrt/augins/, samples/,
# module-example/ -- are handled in bulk by $GplHeaderDirs below.)
$GplHeaderFiles = @(
    "src/xrt/ipc/shared/proto.py",
    "src/xrt/ipc/server/ipc_server_process.c",
    "src/xrt/include/tracking/t_hand_tracking.h",
    "src/xrt/drivers/ht/ht_driver.c",
    "src/xrt/tracking/hand/t_hand_tracking_async.c",
    "src/xrt/targets/openxr_android/build.gradle",
    "src/xrt/targets/openxr_android/src/outOfProcess/AndroidManifest.xml",
    "src/xrt/targets/service-lib/service_target.cpp"
)

# Directories whose contents are entirely authored by Marko. Every source file
# inside (recursive, except $SkipFiles entries) gets its SPDX flipped to
# GPL-3.0-or-later. Vendored third-party files inside these dirs must be
# explicitly listed in $SkipFiles to escape relicensing.
$GplHeaderDirs = @(
    "src/xrt/augins",
    "samples/augins-noop",
    "samples/augins-arcore-headpose",
    "samples/augins-mercury-handtracking-arcore",
    "module-example"
)

# Vendored files we must not touch. They retain their upstream copyright and
# license. Even if they happen to match the placeholder pattern (they probably
# don't, but defensive), the relicense skips them.
$SkipFiles = @(
    "samples/augins-arcore-headpose/vendor/arcore_instance.cpp",
    "samples/augins-arcore-headpose/vendor/arcore_instance.h",
    "samples/augins-arcore-headpose/vendor/arcore_c_api.h",
    "samples/augins-arcore-headpose/vendor/arm64-v8a/libarcore_sdk_c.so",
    "LICENSES/GPL-3.0-or-later.txt",
    "LICENSES/BSL-1.0.txt"
)

# Directories to skip wholesale. Build outputs, vendored binaries, IDE caches.
# Note: src/external/ is NOT skipped -- it contains Marko's CMakeLists.txt glue
# files for third-party libraries. Third-party source files inside src/external
# do not contain the NVIDIA placeholder, so the regex naturally leaves them
# alone.
$SkipDirs = @(
    ".git", ".gradle", ".idea", ".cxx", ".kotlin", ".cache",
    "build", "out", "node_modules", "__pycache__"
)

function Should-Skip-Path {
    param([string]$RelPath)
    foreach ($d in $SkipDirs) {
        if ($RelPath -like "$d/*" -or $RelPath -like "*/$d/*" -or $RelPath.StartsWith("$d/")) {
            return $true
        }
    }
    foreach ($f in $SkipFiles) {
        if ($RelPath -ieq $f) { return $true }
    }
    return $false
}

# Source-y file extensions we will scan/edit. Kept conservative to avoid
# touching binary blobs.
$ScanGlobs = @(
    "*.c","*.cc","*.cpp","*.h","*.hh","*.hpp","*.hxx",
    "*.cs","*.kt","*.java",
    "*.py",
    "*.gradle","*.gradle.kts",
    "*.cmake","CMakeLists.txt",
    "*.json","*.xml","*.md","*.txt","*.in",
    "*.glsl","*.frag","*.vert","*.comp",
    "*.sh","*.ps1",".cmake-format.py",
    # REUSE-style standalone license sidecars and code-generator templates.
    "*.license","*.template","*.inc.glsl"
)

Write-Host "[relicense] Repository root: $RepoRoot"
Write-Host "[relicense] DryRun: $DryRun"
Write-Host "[relicense] Step 1 -- replacing copyright placeholder"

$copyrightHits = 0
$copyrightFiles = 0

# Collect unique file paths first (multiple globs can match the same file --
# e.g. CMakeLists.txt matches both *.txt and the literal CMakeLists.txt
# pattern). Process each file exactly once.
$seen = New-Object System.Collections.Generic.HashSet[string]
$queue = New-Object System.Collections.Generic.List[System.IO.FileInfo]
$repoRootResolved = (Resolve-Path $RepoRoot).Path
foreach ($glob in $ScanGlobs) {
    Get-ChildItem -Path $RepoRoot -Recurse -File -Filter $glob -ErrorAction SilentlyContinue | ForEach-Object {
        if ($seen.Add($_.FullName.ToLowerInvariant())) {
            $queue.Add($_)
        }
    }
}

foreach ($f in $queue) {
    $_ = $f  # keep prior idiom below
    $rel = $f.FullName.Substring($repoRootResolved.Length).TrimStart("\","/").Replace("\","/")
    if (Should-Skip-Path $rel) { continue }

    # Read raw bytes to preserve newline conventions where possible.
    $content = Get-Content -Path $f.FullName -Raw -ErrorAction Stop
    if ($null -eq $content) { continue }

    $matches = [regex]::Matches($content, $PlaceholderRegex)
    if ($matches.Count -eq 0) { continue }

    $occurrences = $matches.Count
    $copyrightHits += $occurrences
    $copyrightFiles += 1
    Write-Host ("  {0,4}x {1}" -f $occurrences, $rel)

    if (-not $DryRun) {
        # $1 = "Copyright ", $2 = the year range, $3 = ", NVIDIA CORPORATION[.]"
        # Replacement preserves $1 + $2 + ", Marko Kazimirovic <...>"
        $newContent = [regex]::Replace($content, $PlaceholderRegex, "`${1}`${2}$ReplacementBase")
        # Set-Content normalises encoding; use [System.IO.File]::WriteAllText
        # to keep the original line endings + UTF-8 (no BOM).
        $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
        [System.IO.File]::WriteAllText($f.FullName, $newContent, $utf8NoBom)
    }
}

Write-Host ("[relicense] Step 1 complete: replaced {0} placeholder(s) across {1} file(s)" -f $copyrightHits, $copyrightFiles)
Write-Host ""
Write-Host "[relicense] Step 2 -- flipping SPDX to GPL-3.0-or-later for substantively-modified files"

$spdxFlips = 0
$spdxMisses = 0
foreach ($rel in $GplHeaderFiles) {
    $abs = Join-Path $RepoRoot $rel
    if (-not (Test-Path $abs)) {
        Write-Warning "  missing: $rel"
        $spdxMisses += 1
        continue
    }
    $content = Get-Content -Path $abs -Raw
    if (-not $content.Contains($OldSpdx)) {
        if ($content.Contains($NewSpdx)) {
            Write-Host "  already-flipped: $rel"
        } else {
            Write-Warning "  no BSL-1.0 SPDX found in: $rel (manual check needed)"
            $spdxMisses += 1
        }
        continue
    }
    Write-Host "  flipping: $rel"
    if (-not $DryRun) {
        $newContent = $content.Replace($OldSpdx, $NewSpdx)
        $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
        [System.IO.File]::WriteAllText($abs, $newContent, $utf8NoBom)
    }
    $spdxFlips += 1
}

Write-Host ("[relicense] Step 2 complete: flipped {0} file(s), {1} miss(es)" -f $spdxFlips, $spdxMisses)
Write-Host ""
Write-Host "[relicense] Step 3 -- flipping SPDX to GPL-3.0-or-later in Marko-authored dirs"

$dirFlips = 0
$dirSkipped = 0
foreach ($d in $GplHeaderDirs) {
    $abs = Join-Path $RepoRoot $d
    if (-not (Test-Path $abs)) { continue }
    Get-ChildItem -Path $abs -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object {
        $rel = $_.FullName.Substring($repoRootResolved.Length).TrimStart("\","/").Replace("\","/")
        if (Should-Skip-Path $rel) { return }
        # Don't try to read non-text files (binaries, archives).
        $ext = [System.IO.Path]::GetExtension($_.FullName).ToLowerInvariant()
        if ($ext -in ".so",".a",".dll",".o",".bin",".dex",".onnx",".png",".jpg",".jpeg",".gif",".zip",".augins",".apk") {
            return
        }
        $content = Get-Content -Path $_.FullName -Raw -ErrorAction SilentlyContinue
        if ($null -eq $content) { return }
        if (-not $content.Contains($OldSpdx)) {
            if ($content.Contains($NewSpdx)) {
                # Already flipped; silent skip.
            }
            return
        }
        Write-Host "  flipping: $rel"
        if (-not $DryRun) {
            $newContent = $content.Replace($OldSpdx, $NewSpdx)
            $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
            [System.IO.File]::WriteAllText($_.FullName, $newContent, $utf8NoBom)
        }
        $dirFlips += 1
    }
}
Write-Host ("[relicense] Step 3 complete: flipped {0} file(s)" -f $dirFlips)
Write-Host ""

if ($DryRun) {
    Write-Host "[relicense] DRY RUN -- no files were modified. Re-run without -DryRun to apply."
} else {
    Write-Host "[relicense] Done. Verify with:"
    Write-Host '    Get-ChildItem -Recurse | Select-String -Pattern "NVIDIA CORPORATION"'
    Write-Host "  (expected: zero hits)"
}
