<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Implementation Status

Per-subsystem status of the v0.2 codebase. Other wiki pages link
here rather than restating status.

Labels:

- `[Working]` -- implemented, on-device verified.
- `[WIP]` -- partial; some paths work, edges do not.
- `[Planned]` -- design exists, code does not.
- `[Broken]` -- regressed or misbehaves; tracked in
  [Known-Issues](Known-Issues.md).

Last verified on-device: 2026-05-21.

## Runtime infrastructure

| Item | Status | Note |
|------|--------|------|
| Out-of-process runtime build (APK) | `[Working]` | `gradlew assembleOutOfProcessDebug` |
| Khronos broker registration | `[Working]` | XR apps resolve us via `OpenXRRuntimeService` |
| `MonadoService.startService` background-restricted | `[WIP]` | `startForegroundService` works; bare `startService` fails on Android 12+. See [Known-Issues](Known-Issues.md). |
| Cardboard stereo distortion compositor | `[Working]` | Inherited from upstream Monado. |
| IPC dispatch (proto.py codegen) | `[Working]` | |

## Aug-Ins module system

| Item | Status | Note |
|------|--------|------|
| `.augins` zip extraction with size-stamp cache | `[Working]` | |
| Manifest parser (Manifest_Version 1) | `[Working]` | |
| dlopen + sibling-`.so` preload | `[Working]` | Verified with ARCore module's `libarcore_sdk_c.so` |
| Dispatch registry (`g_map`) | `[Working]` | |
| Priority sort | `[Working]` | Single-module case verified; multi-module priority interaction not yet stress-tested |
| Lifecycle hooks (`aug_on_module_load` / `aug_on_module_unload`) | `[Working]` | |
| Per-module data dir via TLS | `[Working]` | |
| `aug_on_module_load` reject unwind | `[WIP]` | Rejected modules log but dispatch entries are not pulled back |
| APK-bundled fallback (`assets/modules/`) | `[Planned]` | v0.2.x |
| Dependencies (manifest `Dependencies` field + Kahn sort) | `[Planned]` | v0.2.x or v0.3, only if a real case lands |
| `Failure_Mode` field | `[Planned]` | v0.2.x |
| `module_order.json` priority override file | `[Planned]` | v0.2.x |
| Hot reload | `[Planned]` | v0.3+ |

## Host API

| Item | Status | Note |
|------|--------|------|
| v1 baseline (`get_jvm`, `get_context`, `get_module_data_dir`, `log`) | `[Working]` | |
| Camera frame broker (`publish_camera_frame_y8`, `subscribe_camera_frame`) | `[Planned]` | Returns when Mercury is rewritten on v0.2 |
| Settings API (per-module persistent prefs) | `[Planned]` | v0.2.2 |
| Versioning via `struct_version` | `[Working]` | |

## Service-side adapters

| OpenXR / synthetic name | Adapter | Status |
|--------------------------|---------|--------|
| `xrLocateSpace` | `aug_adapter_space_locate_space` | `[Working]` |
| `aug_LocateDeviceInSpace` (backs T_base_xdev half of `xrLocateViews`) | `aug_adapter_space_locate_device` | `[Working]`, head-only filter |
| `aug_DeviceGetTrackedPose` (backs T_xdev_head half of `xrLocateViews`) | -- | `[Planned]` v0.2.x |
| `xrCreateHandTrackerEXT` family | -- | `[Planned]`, blocked on Mercury v0.2 rewrite |
| `xrEnumerateInstanceExtensionProperties` augmentation | -- | `[Planned]`, depends on manifest `Advertised_OpenXR_Features` |
| Codegen-from-`xr.xml` for adapters | -- | `[Planned]` v0.3+ |

## Shipped modules

| Module | Status | Note |
|--------|--------|------|
| `samples/augins-test-noop` | `[Working]` | Phase 2b verification module. No dispatch; lifecycle only. |
| `samples/augins-test-locate-space` | `[Working]` | Phase 2d verification module. Exports `xrLocateSpace`, writes sentinel pose. |
| `samples/augins-arcore-headpose` | `[Working]` | First production v0.2 module. See [ARCore-Module-Reference](ARCore-Module-Reference.md). |
| `module-example/augins-head-sway` | `[Working]` | Tutorial module; adds a sinusoidal X-offset to the head pose. Standalone NDK build, no external SDK. See [Module-Example-Walkthrough](Module-Example-Walkthrough.md). |
| Mercury hand-tracking (ARCore camera) | `[Planned]` | v0.1 source archived; not yet rewritten on v0.2 ABI |
| Mercury hand-tracking (USB camera) | `[Planned]` | Future work |

## Documentation

| Item | Status | Note |
|------|--------|------|
| Wiki landing + index | `[Working]` | Phase 2f |
| Architecture overview | `[Working]` | Phase 2f |
| Module system | `[Working]` | Phase 2f |
| Manifest schema | `[Working]` | Phase 2f |
| Host API reference | `[Working]` | Phase 2f, points at `module_abi.h` as source of truth |
| Service-side dispatch | `[Working]` | Phase 2f |
| Building the runtime | `[Working]` | Phase 2f |
| Building a module | `[Working]` | Phase 2f |
| ARCore module reference | `[Working]` | Phase 2f |
| Implementation status (this page) | `[Working]` | Phase 2f |
| Known issues | `[Working]` | Phase 2f |
| Roadmap | `[Working]` | Phase 2f |
| Acknowledgements | `[Working]` | Phase 2f |
| Head-sway example walkthrough | `[Working]` | See [Module-Example-Walkthrough](Module-Example-Walkthrough.md) |
| README (root) rewrite for v0.2 | `[Working]` | |
| `MODIFICATIONS.md` (subsystem-grouped changes vs upstream Monado) | `[Working]` | |
| Per-file SPDX header sweep | `[Working]` | `scripts/relicense.ps1` allow-list updated for v0.2; 5 files flipped; 0 `NVIDIA CORPORATION` placeholders remain in source. |
| Mercury module reference | `[Planned]` | After v0.2 Mercury rewrite |
| Calibration UI walkthrough | `[Planned]` | v0.2.2 |
