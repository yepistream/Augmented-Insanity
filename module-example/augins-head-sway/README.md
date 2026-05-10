<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Aug-Ins Head-Sway Example

Smallest practical `.augins` module that does something visible.

The module hooks the IPC dispatch path Augmented Insanity uses for
per-xdev tracked-pose queries (`device_get_tracked_pose`, surfaced to
modules under the synthetic name `aug_deviceGetTrackedPose`), filters to
the head xdev's `XRT_INPUT_GENERIC_HEAD_POSE` input, and adds a sinusoidal
X-axis offset to the pose the runtime is about to send back to the
client. The visual result: the rendered scene sways left and right with
a 4 second period and 0.3 m amplitude.

## Why this exists

It is the first thing a new module author should read after
[docs/wiki/Architecture-Overview](../../docs/wiki/Architecture-Overview.md)
and [docs/wiki/Module-System](../../docs/wiki/Module-System.md). It
demonstrates, in one source file:

- The `.augins` lifecycle (`aug_onModuleLoad`, `aug_runtimeInit`,
  `aug_onConnect`, `aug_runtimeFinished`).
- Receiving the runtime's host API table at module load.
- Hooking an IPC dispatch call.
- Reading the IPC reply via a mirror struct.
- Modifying the reply via the host API
  (`set_locate_space_relation`).
- Filtering hooks so they do not perturb other inputs (controllers,
  hand trackers).

It deliberately does not use the camera-frame broker, the stub-xdev
factory, the host API's `register_hand_tracker`, or any external SDK.
For those, see `samples/augins-arcore-headpose/` and
`samples/augins-mercury-handtracking-arcore/`.

## Files

- `head_sway.cpp` -- the entire module, ~150 lines. Heavily commented.
- `metadata.json` -- declares the module ID, version, and the
  `aug_deviceGetTrackedPose` hook.
- `settings.json` -- empty placeholder.
- `CMakeLists.txt` -- standalone NDK CMake build. Includes `xrt/xrt_defines.h`
  and `augins_module_abi.h` from the runtime tree.
- `build.gradle` -- thin Gradle Zip task that produces `head-sway.augins`.
- `LICENSE.txt` -- GPL-3.0-or-later text for self-contained distribution.

## Build

From the repository root:

```
.\gradlew :module-example:augins-head-sway:packageHeadSwayAugins
```

Output: `module-example/augins-head-sway/build/head-sway.augins`. A
few KB.

## Install on device

The runtime APK must already be installed and debuggable. Then:

```
.\gradlew :module-example:augins-head-sway:installHeadSwayAugins
adb shell am force-stop com.augmented_insanity.runtime.out_of_process
```

The next time the runtime starts (re-launch the runtime activity, or any
OpenXR client that triggers it), the module will be loaded.

## Verify

Watch for the module's log lines:

```
adb logcat -s "AugInsHeadSway:*"
```

Expected sequence on first client connect:

```
AugInsHeadSway: aug_onModuleLoad: host API v2 accepted
AugInsHeadSway: aug_runtimeInit: dispatch table built; sway period=4.00s amplitude=0.30m
AugInsHeadSway: aug_onConnect: an OpenXR client just attached
```

Visual: launch any OpenXR app -- the rendered scene's head pose will
sway left-right at the configured period and amplitude. helloxr,
hello_xr, the Godot OpenXR samples all work.

## Uninstall

```
adb shell run-as com.augmented_insanity.runtime.out_of_process \
    rm files/modules/head-sway.augins
adb shell am force-stop com.augmented_insanity.runtime.out_of_process
```

## Walkthrough

A line-by-line tour of `head_sway.cpp` is in
[docs/wiki/Module-Example-Walkthrough.md](../../docs/wiki/Module-Example-Walkthrough.md).
