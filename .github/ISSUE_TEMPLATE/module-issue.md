<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->
---
name: Module issue
about: A specific shipped module is misbehaving (ARCore head-pose, Mercury hand-tracking, head-sway).
title: "[<module-id>] "
labels: module-bug
---

## Module

- **Module ID:** (e.g. `com.augmented_insanity.samples.arcore_headpose`)
- **Module version:** (from `metadata.json`)
- **Other modules installed:** (some bugs only appear with specific module
  combinations -- list everything in `files/modules/`)

## What it does wrong

<!-- One paragraph. -->

## Client app

- **App package:** (e.g. `com.example.openxrhandtrackingdemo`)
- **What the client requests:** (xrLocateSpace, xrLocateHandJointsEXT,
  xrLocateViews, etc., as far as you can tell from the symptom)
- **Visible symptom:** (3DoF instead of 6DoF, hands not rendering, scene
  swaying when it shouldn't, etc.)

## Module logcat

```
adb shell am force-stop com.augmented_insanity.runtime.out_of_process
adb logcat -c
adb shell am start -n com.augmented_insanity.runtime.out_of_process/com.augmented_insanity.runtime.AugInsTestActivity
adb shell am start -n <client-package>/<client-activity>
# wait ~15s, reproduce; then:
adb logcat -d -s "Aug-Ins:*" "AugIns.*:*" "native:*" *:F > logcat.txt
```

Attach `logcat.txt`. If you can also capture the client-side logcat
(filtered to its own PID), include both.

## Workarounds tried

<!-- e.g. "removing module X makes the issue go away" -->
