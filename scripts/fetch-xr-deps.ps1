# Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fetch-xr-deps.ps1 -- one-shot downloader for the external native
# dependencies the Mercury hand-tracking module needs (OpenCV Android SDK,
# ONNX Runtime Android AAR, hand-tracking ONNX models).
#
# Reads pinned URLs and target paths from xr-deps-versions.json at the
# repository root. Extracts each archive under xr-deps/. Idempotent: skips
# anything already present (checked via the per-dep `expected_marker` /
# `expected_files`).
#
# Usage:
#   pwsh scripts/fetch-xr-deps.ps1 [-RepoRoot <path>] [-Force]
#
# Notes:
# - The Mercury module is the ONLY consumer of these deps. If you do not
#   build the Mercury module, you do not need to run this script.
# - The hand-tracking models are pulled from Collabora's git-LFS repo. If
#   the LFS bandwidth quota is exhausted, mirror the two .onnx files
#   yourself and edit `xr-deps-versions.json` to point at HTTP URLs (delete
#   the `git_url` / `git_ref` keys, add `url` and `expected_marker`).

[CmdletBinding()]
param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$VersionsFile = Join-Path $RepoRoot "xr-deps-versions.json"
if (-not (Test-Path $VersionsFile)) {
    throw "xr-deps-versions.json not found at $VersionsFile"
}

$config = Get-Content -Raw -Path $VersionsFile | ConvertFrom-Json

function Test-AnyMissing {
    param($dep, [string]$RepoRoot)
    if ($dep.PSObject.Properties['expected_marker']) {
        return -not (Test-Path (Join-Path $RepoRoot $dep.expected_marker))
    }
    if ($dep.PSObject.Properties['expected_files']) {
        foreach ($f in $dep.expected_files) {
            if (-not (Test-Path (Join-Path $RepoRoot $f))) { return $true }
        }
        return $false
    }
    return $true
}

function Fetch-And-Extract-Zip {
    param([string]$Name, $Dep, [string]$RepoRoot)
    $extractTo = Join-Path $RepoRoot $Dep.extract_to
    Write-Host ""
    Write-Host "[$Name] version $($Dep.version)"

    if (-not $Force -and -not (Test-AnyMissing $Dep $RepoRoot)) {
        Write-Host "  already present (use -Force to refetch); skipping"
        return
    }

    $tmp = Join-Path $env:TEMP ("aug-ins-" + [guid]::NewGuid().ToString() + ".tmp")
    try {
        Write-Host "  downloading: $($Dep.url)"
        Invoke-WebRequest -Uri $Dep.url -OutFile $tmp -UseBasicParsing
        $size = (Get-Item $tmp).Length
        Write-Host ("  downloaded {0:F1} MB" -f ($size / 1MB))

        if ($Dep.PSObject.Properties['sha256'] -and $Dep.sha256 -ne '') {
            $actual = (Get-FileHash -Path $tmp -Algorithm SHA256).Hash.ToLower()
            $expected = $Dep.sha256.ToLower()
            if ($actual -ne $expected) {
                throw "  SHA-256 mismatch: expected $expected, got $actual"
            }
            Write-Host "  SHA-256 verified"
        }

        if (Test-Path $extractTo) { Remove-Item -Recurse -Force $extractTo }
        New-Item -ItemType Directory -Path $extractTo | Out-Null

        Write-Host "  extracting to: $extractTo"
        # ZIP and AAR (which is just a renamed ZIP) both expand the same way.
        Expand-Archive -Path $tmp -DestinationPath $extractTo -Force

        if (Test-AnyMissing $Dep $RepoRoot) {
            Write-Warning "  expected marker missing after extract; archive layout may have changed upstream"
        } else {
            Write-Host "  ok"
        }
    } finally {
        if (Test-Path $tmp) { Remove-Item -Force $tmp }
    }
}

function Fetch-Git-Lfs {
    param([string]$Name, $Dep, [string]$RepoRoot)
    $extractTo = Join-Path $RepoRoot $Dep.extract_to
    Write-Host ""
    Write-Host "[$Name] version $($Dep.version)  (git-LFS clone)"

    if (-not $Force -and -not (Test-AnyMissing $Dep $RepoRoot)) {
        Write-Host "  already present (use -Force to refetch); skipping"
        return
    }

    $hasGit = $null -ne (Get-Command git -ErrorAction SilentlyContinue)
    if (-not $hasGit) {
        throw "  git is not on PATH but is required to fetch $Name"
    }
    $hasLfs = $null -ne (Get-Command git-lfs -ErrorAction SilentlyContinue)
    if (-not $hasLfs) {
        Write-Warning "  git-lfs is not on PATH -- the .onnx files will be 1KB pointer files instead of real models. Install Git LFS (https://git-lfs.com) and re-run."
    }

    if (Test-Path $extractTo) { Remove-Item -Recurse -Force $extractTo }
    Write-Host "  cloning: $($Dep.git_url) (ref $($Dep.git_ref))"
    git clone --depth 1 --branch $Dep.git_ref $Dep.git_url $extractTo
    if ($LASTEXITCODE -ne 0) { throw "  git clone failed" }

    Push-Location $extractTo
    try {
        if ($hasLfs) {
            Write-Host "  git lfs pull"
            git lfs pull
            if ($LASTEXITCODE -ne 0) { Write-Warning "  git lfs pull failed; .onnx files may be pointers" }
        }
    } finally {
        Pop-Location
    }

    if (Test-AnyMissing $Dep $RepoRoot) {
        Write-Warning "  expected files missing after clone; check git-lfs status"
    } else {
        Write-Host "  ok"
    }
}

Write-Host "[fetch-xr-deps] Repository root: $RepoRoot"
Write-Host "[fetch-xr-deps] Versions file:   $VersionsFile"

if ($config.PSObject.Properties['opencv'])              { Fetch-And-Extract-Zip "opencv"     $config.opencv     $RepoRoot }
if ($config.PSObject.Properties['onnxruntime'])         { Fetch-And-Extract-Zip "onnxruntime" $config.onnxruntime $RepoRoot }
if ($config.PSObject.Properties['hand_tracking_models']){ Fetch-Git-Lfs        "hand_tracking_models" $config.hand_tracking_models $RepoRoot }

Write-Host ""
Write-Host "[fetch-xr-deps] Done. Layout summary:"
if (Test-Path (Join-Path $RepoRoot "xr-deps")) {
    Get-ChildItem (Join-Path $RepoRoot "xr-deps") -Directory | ForEach-Object {
        Write-Host ("  {0}" -f $_.FullName)
    }
}
Write-Host ""
Write-Host "Override paths in your build with -PopencvDir / -PonnxRoot / -PhandTrackingModelsDir"
Write-Host "if you keep the dependencies elsewhere on disk."
