#!/usr/bin/env bash
# Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fetch-xr-deps.sh -- POSIX equivalent of scripts/fetch-xr-deps.ps1.
# Reads xr-deps-versions.json at the repo root and downloads OpenCV Android
# SDK, ONNX Runtime Android AAR, and the hand-tracking ONNX models into
# xr-deps/.
#
# Usage:
#   bash scripts/fetch-xr-deps.sh [--repo-root <path>] [--force]
#
# Requires: bash, jq, curl, unzip, git, git-lfs.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FORCE=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --repo-root) REPO_ROOT="$2"; shift 2 ;;
        --force)     FORCE=1; shift ;;
        -h|--help)
            sed -n '4,15p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "unknown arg: $1" >&2; exit 1 ;;
    esac
done

VERSIONS="$REPO_ROOT/xr-deps-versions.json"
[[ -f "$VERSIONS" ]] || { echo "xr-deps-versions.json not found at $VERSIONS" >&2; exit 1; }

for tool in jq curl unzip; do
    command -v "$tool" >/dev/null || { echo "$tool not on PATH" >&2; exit 1; }
done

# Returns 0 (true) if any expected file/marker for the dep is missing.
any_missing() {
    local dep_path="$1"
    local marker; marker=$(jq -r --arg k "$dep_path" '.[$k].expected_marker // empty' "$VERSIONS")
    if [[ -n "$marker" ]]; then
        [[ ! -e "$REPO_ROOT/$marker" ]]
        return $?
    fi
    local files; files=$(jq -r --arg k "$dep_path" '.[$k].expected_files[]? // empty' "$VERSIONS")
    if [[ -n "$files" ]]; then
        while IFS= read -r f; do
            [[ -e "$REPO_ROOT/$f" ]] || return 0
        done <<<"$files"
        return 1
    fi
    return 0
}

fetch_zip() {
    local name="$1"
    local url version extract_to sha256 marker
    version=$(jq -r --arg k "$name" '.[$k].version' "$VERSIONS")
    url=$(jq -r --arg k "$name" '.[$k].url' "$VERSIONS")
    extract_to=$(jq -r --arg k "$name" '.[$k].extract_to' "$VERSIONS")
    sha256=$(jq -r --arg k "$name" '.[$k].sha256 // ""' "$VERSIONS")

    echo
    echo "[$name] version $version"

    if [[ $FORCE -eq 0 ]] && ! any_missing "$name"; then
        echo "  already present (use --force to refetch); skipping"
        return
    fi

    local tmp; tmp=$(mktemp --tmpdir="${TMPDIR:-/tmp}" "aug-ins-XXXXXX.tmp")
    trap 'rm -f "$tmp"' RETURN

    echo "  downloading: $url"
    curl -fL --progress-bar -o "$tmp" "$url"
    local size; size=$(stat -c%s "$tmp" 2>/dev/null || stat -f%z "$tmp")
    printf '  downloaded %.1f MB\n' "$(echo "scale=1; $size/1048576" | bc)"

    if [[ -n "$sha256" ]]; then
        local actual; actual=$(sha256sum "$tmp" | awk '{print $1}')
        if [[ "$actual" != "${sha256,,}" ]]; then
            echo "  SHA-256 mismatch: expected $sha256, got $actual" >&2
            return 1
        fi
        echo "  SHA-256 verified"
    fi

    rm -rf "$REPO_ROOT/$extract_to"
    mkdir -p "$REPO_ROOT/$extract_to"
    echo "  extracting to: $REPO_ROOT/$extract_to"
    unzip -q "$tmp" -d "$REPO_ROOT/$extract_to"

    if any_missing "$name"; then
        echo "  WARNING: expected marker missing after extract; archive layout may have changed upstream" >&2
    else
        echo "  ok"
    fi
}

fetch_git_lfs() {
    local name="$1"
    local version git_url git_ref extract_to
    version=$(jq -r --arg k "$name" '.[$k].version' "$VERSIONS")
    git_url=$(jq -r --arg k "$name" '.[$k].git_url' "$VERSIONS")
    git_ref=$(jq -r --arg k "$name" '.[$k].git_ref' "$VERSIONS")
    extract_to=$(jq -r --arg k "$name" '.[$k].extract_to' "$VERSIONS")

    echo
    echo "[$name] version $version  (git-LFS clone)"

    if [[ $FORCE -eq 0 ]] && ! any_missing "$name"; then
        echo "  already present (use --force to refetch); skipping"
        return
    fi

    command -v git >/dev/null || { echo "  git not on PATH" >&2; return 1; }
    if ! command -v git-lfs >/dev/null; then
        echo "  WARNING: git-lfs not on PATH; .onnx files will be ~1KB pointer files. Install Git LFS (https://git-lfs.com) and re-run." >&2
    fi

    rm -rf "$REPO_ROOT/$extract_to"
    echo "  cloning: $git_url (ref $git_ref)"
    git clone --depth 1 --branch "$git_ref" "$git_url" "$REPO_ROOT/$extract_to"

    if command -v git-lfs >/dev/null; then
        ( cd "$REPO_ROOT/$extract_to" && echo "  git lfs pull" && git lfs pull )
    fi

    if any_missing "$name"; then
        echo "  WARNING: expected files missing after clone; check git-lfs status" >&2
    else
        echo "  ok"
    fi
}

echo "[fetch-xr-deps] Repository root: $REPO_ROOT"
echo "[fetch-xr-deps] Versions file:   $VERSIONS"

if jq -e '.opencv'              "$VERSIONS" >/dev/null 2>&1; then fetch_zip     opencv;              fi
if jq -e '.onnxruntime'         "$VERSIONS" >/dev/null 2>&1; then fetch_zip     onnxruntime;         fi
if jq -e '.hand_tracking_models' "$VERSIONS" >/dev/null 2>&1; then fetch_git_lfs hand_tracking_models; fi

echo
echo "[fetch-xr-deps] Done. Layout summary:"
if [[ -d "$REPO_ROOT/xr-deps" ]]; then
    find "$REPO_ROOT/xr-deps" -mindepth 1 -maxdepth 1 -type d -printf '  %p\n'
fi
echo
echo "Override paths in your build with -PopencvDir / -PonnxRoot / -PhandTrackingModelsDir"
echo "if you keep the dependencies elsewhere on disk."
