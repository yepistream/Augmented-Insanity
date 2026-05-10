<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->
---
name: Bug report
about: Runtime crash, IPC failure, build failure, or any unexpected behaviour.
title: "[bug] "
labels: bug
---

## What happened

<!-- One paragraph. Be specific. -->

## Expected behaviour

<!-- What did you think would happen instead? -->

## Reproduction steps

1.
2.
3.

## Environment

- **Device:** (manufacturer, model, Android version, ARCore-supported yes/no)
- **Augmented Insanity runtime:** (release tag or commit SHA)
- **Modules installed:** (list of `.augins` files in
  `/data/data/com.augmented_insanity.runtime.out_of_process/files/modules/`)
- **OpenXR client app:** (name, version, GLES/Vulkan)
- **Host OS:** (only relevant for build issues -- Windows/macOS/Linux + version)

## Logcat

Service-side and crash buffers are usually the most useful slices. Run
**before** reproducing the issue:

```
adb shell am force-stop com.augmented_insanity.runtime.out_of_process
adb shell am force-stop <your-client-package>
adb logcat -c
adb shell am start -n com.augmented_insanity.runtime.out_of_process/com.augmented_insanity.runtime.AugInsTestActivity
# launch the client now, reproduce the bug, then:
adb logcat -d -b main -b crash *:F > logcat.txt
```

Attach `logcat.txt`. If too long, narrow with `-s "Aug-Ins:*" "AugIns.*:*"
"libc:F" "DEBUG:*"`.

## Build output (only for build issues)

Run with `--info` and attach the relevant slice:

```
.\gradlew :src:xrt:targets:openxr_android:assembleOutOfProcessDebug --info > build.log 2>&1
```

## Anything else
