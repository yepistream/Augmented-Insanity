<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

## Summary

<!-- One paragraph. What does this PR change? -->

## Why

<!-- Link related issue: Fixes #N / Refs #N. Why is this change desired? -->

## Checklist

Before requesting review, confirm each item by ticking the box:

- [ ] Every commit carries `Signed-off-by:` (DCO).
- [ ] New files have an `SPDX-License-Identifier` header in the file's
      natural comment style.
- [ ] Modified files preserve their original copyright lines; my copyright
      line is added below them.
- [ ] If a substantive change to a previously BSL-1.0 file: SPDX flipped to
      `GPL-3.0-or-later`, and the file path is added to the
      `$GplHeaderFiles` allow-list in `scripts/relicense.ps1`.
- [ ] `MODIFICATIONS.md` updated if a runtime subsystem was changed.
- [ ] `docs/wiki/` page updated or added if the change affects a
      documented surface.
- [ ] `clang-format` / `cmake-format` clean on touched files.
- [ ] Build succeeds:
      `.\gradlew :src:xrt:targets:openxr_android:assembleOutOfProcessDebug`.
- [ ] On-device verified (describe the test scenario below).

## Test plan

<!-- Concrete steps that exercise the change end-to-end on a real device. -->

1.
2.
3.

## Risk / regression notes

<!-- What could break? Did you regression-test the existing samples? -->
