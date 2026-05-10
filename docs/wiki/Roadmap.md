<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Roadmap

What is committed to ship next, ordered by priority within each tier. Items
that are stuck on a question of design, not implementation, are flagged.

## Short term (next release)

### Stub-xdev installation refactor

Today: `augins_stub_xdevs_install` runs AFTER `xrt_instance_create_system`,
requiring a post-hoc patch to the space overseer
(`b_space_overseer_link_space_to_device`) and producing a misleading
`In roles: <none>` log line. See
[Stub Xdev Factory](Stub-Xdev-Factory.md) and
[Known Issues](Known-Issues.md).

Plan: hook into the system-builder pipeline so module-advertised xdevs
are present in the initial xdev list passed to
`b_space_overseer_legacy_setup`. The post-hoc patch and the misleading
log both go away.

### Fix the 11x lifecycle hook duplication

See [Known Issues](Known-Issues.md). Bisection in progress; root cause
not yet identified.

### Per-module calibration activity

Today: the Mercury hand-tracking module reads camera intrinsics from
ARCore at first frame. No mechanism to override with a calibrated
chessboard intrinsics file. See
[Calibration Activity](Calibration-Activity.md).

Plan: ship `HandCameraCalibrationActivity` as part of the runtime APK.
Stops the runtime service, runs the chessboard capture flow, writes
`hand_camera.json` into the runtime's private data dir, restarts the
service. Mercury reads the JSON if present and falls back to ARCore
intrinsics otherwise.

## Medium term

### Module-provided Activities (DEX loading)

Today: modules cannot ship Java code, so any UI a module wants must live
in the runtime APK or in a separate companion APK.

Plan: runtime APK ships a generic `ModuleHostActivity` that DEX-loads a
`module.dex` bundled inside the `.augins` zip via `PathClassLoader`.
Module's manifest declares `Advertised_Activities` (settings,
diagnostics, calibration); the main runtime activity routes to the
module-provided Fragment via Intent. Trust model is unchanged from
loading native code.

This unblocks the Mercury USB module's USB-camera-bridge Java helper,
the per-module settings story, and any future module that wants its own
configuration UI.

### Mercury USB camera module

Today: only the ARCore-camera Mercury module is shipped. USB UVC support
from another one of my modified (private) Monado repos exists but is not wrapped as a `.augins`.

Plan: `samples/augins-mercury-handtracking-usb/` consuming a
`UsbHandCameraBridge.java` shipped from the runtime APK (depends on the
DEX-loading work above) plus libuvc vendored in the module zip.

### `ht_ctrl_emu` integration

Today: `src/xrt/drivers/ht_ctrl_emu/` is upstream code that emulates
controllers from hand-tracking input. Not currently exposed.

Plan: small built-in feature flag (`XRT_MODULE_HT_CTRL_EMU`) that, when
combined with any module advertising hand tracking, plugs cemu xdevs
into the controller role slots. Modules opt in via a manifest field.

## Longer term

### Multi-process module isolation

Today: every module loads into the runtime service process via `dlopen`.
A module crash kills the runtime; a module memory leak grows the
runtime's RSS.

Plan: optional per-module process. Would require an additional IPC layer
between the runtime and the module process. Major architectural change;
only worth doing once enough modules exist to justify the cost.

### CI

Today: no automated build verification.

Plan: GitHub Actions workflow that builds the runtime APK + all
samples + the head-sway example on every PR. SHA-256 verification of
the `.augins` artifacts on tag pushes.

### Settings UI for modules

Today: module settings live in `settings.json` inside the `.augins` zip
and are read at module load. No way to change them from the device.

Plan: depends on module-provided Activities. Each module ships a
settings Fragment; runtime APK's "Modules" screen lists installed
modules and routes to their settings UI on tap.


