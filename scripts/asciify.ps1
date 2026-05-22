# Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# One-shot ASCII normaliser. Walks the curated set of authored /
# substantively-modified files and replaces every non-ASCII
# character with a plain-keyboard equivalent.
#
# Patterns covered:
#   Q-alpha .. Q-epsilon       Greek letter labels       -> Q1..Q5
#   em-dash --                                            -> two ASCII hyphens
#   right-arrow ->                                        -> ASCII "->"
#   right-single-quote                                    -> ASCII apostrophe
#   surviving double-mojibake of degree-sign              -> " deg"
#   surviving double-mojibake of plus-minus               -> "+/-"
#   surviving double-mojibake of ellipsis                 -> "..."
#   surviving double-mojibake of bullet                   -> "* "
#
# Vendored upstream code (samples/augins-arcore-headpose/vendor/,
# src/external/, anything under build/ or .cxx/) is skipped.

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

# Construct mojibake patterns from explicit codepoints so this script
# source itself stays pure ASCII.
$pat_Aabp_plusminus = [char]0x00C3 + [char]0x201A + [char]0x00C2 + [char]0x00B1                                   # Aabp+/-
$pat_ellipsis       = [char]0x00C3 + [char]0x00A2 + [char]0x00E2 + [char]0x201A + [char]0x00AC + [char]0x00C2 + [char]0x00A6
$pat_bullet         = [char]0x00C3 + [char]0x00A2 + [char]0x00E2 + [char]0x201A + [char]0x00AC + [char]0x00C2 + [char]0x00A2
$pat_degree         = [char]0x00C2 + [char]0x00B0                                                                  # Aa-degree

# Parallel arrays: $patterns[i] gets replaced with $replacementsTo[i].
$patterns = @(
    $pat_Aabp_plusminus,
    $pat_ellipsis,
    $pat_bullet,
    $pat_degree,
    ('Q-' + [char]0x03B1),
    ('Q-' + [char]0x03B2),
    ('Q-' + [char]0x03B3),
    ('Q-' + [char]0x03B4),
    ('Q-' + [char]0x03B5),
    [string][char]0x2014,
    [string][char]0x2192,
    [string][char]0x2019
)
$replacementsTo = @(
    '+/-',
    '...',
    '* ',
    ' deg',
    'Q1',
    'Q2',
    'Q3',
    'Q4',
    'Q5',
    '--',
    '->',
    "'"
)

$paths = @(
    "README.md", "MODIFICATIONS.md", "CONTRIBUTING.md",
    "docs\wiki",
    "src\xrt\augins",
    "samples\augins-arcore-headpose",
    "samples\augins-test-noop",
    "samples\augins-test-locate-space",
    "module-example",
    "src\xrt\ipc\shared\proto.py",
    "src\xrt\ipc\server\ipc_server_process.c",
    "src\xrt\ipc\server\ipc_server_objects.h",
    "src\xrt\ipc\server\ipc_server_objects.c",
    "src\xrt\include\tracking\t_hand_tracking.h",
    "src\xrt\drivers\ht\ht_driver.c",
    "src\xrt\tracking\hand\t_hand_tracking_async.c",
    "src\xrt\base\b_space_overseer.h",
    "src\xrt\targets\openxr_android\build.gradle",
    "src\xrt\targets\openxr_android\src\outOfProcess\AndroidManifest.xml",
    "src\xrt\targets\openxr_android\src\outOfProcess\java\com\augmented_insanity\runtime\AugInsBridge.kt",
    "src\xrt\targets\openxr_android\src\outOfProcess\java\com\augmented_insanity\runtime\AugInsTestActivity.kt",
    "src\xrt\targets\openxr_android\src\outOfProcess\res\values\strings.xml",
    "src\xrt\targets\service-lib\service_target.cpp",
    "scripts\relicense.ps1"
)

$textExts = ".c", ".cc", ".cpp", ".h", ".hpp", ".kt", ".java", ".py", ".gradle", ".cmake", ".md", ".xml", ".json", ".txt", ".in", ".sh", ".ps1"

$root = (Get-Location).Path
$totalFiles = 0; $totalSubs = 0

foreach ($p in $paths) {
    if (-not (Test-Path $p)) { continue }
    $items = if ((Get-Item $p).PSIsContainer) {
        Get-ChildItem -Path $p -Recurse -File
    } else {
        @(Get-Item $p)
    }
    foreach ($f in $items) {
        if ($f.FullName -match '\\(build|\.cxx|\.gradle|\.idea|vendor)\\') { continue }
        if ($f.Name -ne "CMakeLists.txt" -and $textExts -notcontains $f.Extension.ToLower()) { continue }

        $content = [System.IO.File]::ReadAllText($f.FullName, [System.Text.Encoding]::UTF8)
        if ($null -eq $content) { continue }

        $fileSubs = 0
        for ($k = 0; $k -lt $patterns.Length; $k++) {
            $pat = $patterns[$k]; $rep = $replacementsTo[$k]
            if ($pat.Length -eq 0) { continue }
            $count = ($content.Length - $content.Replace($pat, '').Length) / $pat.Length
            if ($count -gt 0) {
                $content = $content.Replace($pat, $rep)
                $fileSubs += [int]$count
            }
        }
        if ($fileSubs -gt 0) {
            $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
            [System.IO.File]::WriteAllText($f.FullName, $content, $utf8NoBom)
            $rel = $f.FullName.Replace($root + "\", "")
            Write-Output ("  {0,3}x {1}" -f $fileSubs, $rel)
            $totalFiles++; $totalSubs += $fileSubs
        }
    }
}

Write-Output "---"
Write-Output ("Total: {0} substitution(s) across {1} file(s)" -f $totalSubs, $totalFiles)
