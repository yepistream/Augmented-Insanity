<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Modifications relative to upstream Monado

This document summarises what Augmented Insanity adds or changes relative to
its parent project, Monado (https://gitlab.freedesktop.org/monado/monado), at
the granularity of subsystems rather than individual files. For per-file
license attribution, consult the `SPDX-License-Identifier` header at the top
of each source file.

The fork point was a snapshot taken from upstream Monado main in 2025; merges
back to upstream are not currently practiced.

## Module system (new subsystem)

Path: `src/xrt/augins/`

The largest addition. A general-purpose module loader that lets third parties
ship `.augins` packages providing capabilities (head pose, hand tracking, etc.)
without recompiling the runtime. Components:

- **Loader** (`augins_dispatch.cpp`, `augins_dispatch.h`) -- enumerates
  `.augins` files in the runtime's modules directory, parses each manifest,
  resolves dependencies via Kahn's topological sort, `dlopen`s the modules in
  order, and registers their exported hook functions in a name-keyed dispatch
  table.
- **Lifecycle** (`augins_lifecycle.cpp`, `augins_lifecycle.h`) -- drives
  `aug_onModuleLoad`, `aug_runtimeInit`, `aug_onConnect`, `aug_runtimeFinished`
  callbacks across all loaded modules; manages the optional background-buffer
  worker thread.
- **Host API table** (`augins_module_abi.h`, `augins_host_api.cpp`,
  `augins_host_api.h`) -- the function pointer table the runtime hands to each
  module so it can call back into the runtime without depending on internal
  headers. Versioned (currently v2). Includes `set_locate_space_relation`,
  `register_hand_tracker`, `publish_camera_frame_y8`, `subscribe_camera_frame`,
  `get_module_data_dir`, `get_vm`, `get_context`.
- **Stub xdev factory** (`augins_stub_xdevs.cpp`, `augins_stub_xdevs.h`,
  `augins_hand_tracker_dispatch.h`) -- when modules advertise OpenXR features
  that need a corresponding `xrt_device` (currently: hand tracker), creates
  the placeholder xdev, slots it into the right `static_roles` entries, and
  registers it with the space overseer.
- **Manifest aggregator** (`augins_extensions.cpp`, `augins_extensions.h`) --
  unions `Advertised_OpenXR_Features.Extensions` and `SystemPropertyBits`
  from every loaded module so OpenXR queries can answer truthfully.

## IPC dispatch hook firing

Path: `src/xrt/ipc/shared/proto.py`, `src/xrt/ipc/server/ipc_server_process.c`

The proto.py code generator now emits a call to `augins_fire_hooks(name, ics,
msg, &reply, NULL)` immediately before each `ipc_send` for every IPC call
listed in the `aug_ipc_to_xr` dictionary. This lets module hooks read or
overwrite the reply before the client sees it. The full mapping currently
covers `xrCreateSession`, frame and swapchain calls, `xrLocateSpace`,
`xrLocateSpaces`, `aug_deviceGetTrackedPose` (synthetic name backing
`device_get_tracked_pose`), and `aug_spaceCreateSemanticIds` (synthetic name
backing `space_create_semantic_ids`).

`ipc_server_process.c::ipc_server_init_system_if_available_locked` was
modified to call `augins_stub_xdevs_install(xsysd, xso)` after
`xrt_instance_create_system`, so module-advertised xdevs are present in
`static_xdevs[]` and the space overseer before client shm state is published.

## Space overseer registration helper

Path: `src/xrt/base/b_space_overseer.{c,h}`

No code change; only the public header now exposes
`b_space_overseer_link_space_to_device` and `b_space_overseer_create_null_space`
to permit the stub xdev factory to register newly-added xdevs with the space
overseer after `b_space_overseer_legacy_setup` has already run. This is a
pragmatic post-hoc registration; a future refactor will hook into the system
builder pipeline so stub xdevs are present in the initial xdev list passed to
`legacy_setup` and this back-door becomes unnecessary.

## Hand-tracking driver `models_dir` override

Paths:
- `src/xrt/include/tracking/t_hand_tracking.h`
- `src/xrt/drivers/ht/ht_driver.c`

`struct t_hand_tracking_create_info` gains a `const char *models_dir` field.
When non-NULL, `ht_device_create` uses it in place of
`u_file_get_hand_tracking_models_dir()` (which on Android resolves to a
nonexistent Linux-only path). Lets `.augins` modules ship the ONNX models
inside their zip and pass the per-module extraction directory.

## Hand-tracking async worker scheduling priority

Path: `src/xrt/tracking/hand/t_hand_tracking_async.c`

The Mercury inference worker thread now calls
`setpriority(PRIO_PROCESS, 0, 10)` very early on Linux/Android. ARCore VIO
threads (default nice 0) preempt it, preventing the inference workload from
stalling visual-inertial odometry feature extraction during initialization.
Combined with frame decimation in the Mercury module itself, this is what
makes Mercury and ARCore coexist in the same process.

## Service entry point

Path: `src/xrt/targets/service-lib/service_target.cpp`

The `nativeStartServer` JNI handler now calls `augins::launch(modules_dir,
cache_dir)` before spawning the IPC server thread, so the dispatch table and
module lifecycle callbacks are live by the time any client can connect.

## Android runtime manifest and Gradle setup

Paths:
- `src/xrt/targets/openxr_android/build.gradle`
- `src/xrt/targets/openxr_android/src/outOfProcess/AndroidManifest.xml`

The `outOfProcess` flavor's Gradle dependencies grew an optional
`com.google.ar:core:1.52.0` entry; the corresponding manifest gained CAMERA
permission, the `android.hardware.camera.ar` feature, and a
`com.google.ar.core` meta-data entry. Required by the ARCore head-pose module
when shipped; benign when the module is not installed.

## Sample modules

Paths: `samples/augins-noop/`, `samples/augins-arcore-headpose/`,
`samples/augins-mercury-handtracking-arcore/`

Three reference module implementations, all new:

- **augins-noop** -- minimal lifecycle-hook demonstration; logs every callback
  it receives. No host API consumption.
- **augins-arcore-headpose** -- wraps Google ARCore for 6DoF head-pose
  tracking. Hooks `xrLocateSpace` (view-in-world-locked) and
  `aug_deviceGetTrackedPose` (head xdev's `XRT_INPUT_GENERIC_HEAD_POSE`).
  Publishes the YUV camera Y plane through the host-API broker so downstream
  modules can consume frames without spinning up a second `ArSession` (which
  is impossible -- only one `ArSession` per process).
- **augins-mercury-handtracking-arcore** -- in-tree Mercury+ONNX hand
  tracking. Subscribes to the camera-frame broker, registers a hand-tracker
  callback via the host API, decimates frames to 10 Hz to keep ONNX
  inference from starving ARCore VIO. Bundles its own ONNX model files
  inside the `.augins` zip.

## Sample-build infrastructure

Paths: `settings.gradle`, sample modules' `build.gradle` and `CMakeLists.txt`

Each sample is an additional Gradle subproject. `augins-noop` builds via a
standalone NDK CMake invocation (vendored Monado headers only). The other two
build as native targets inside the runtime APK's CMake invocation so they can
reuse runtime-side libraries (Mercury hand-tracking lib, ARCore SDK), with a
thin Gradle Zip task on top that packages the resulting `.so` plus the
manifest, models, and licenses into a `.augins` archive.

## Repository scaffolding (new files)

- `LICENSE` (rewritten) -- multi-license overview.
- `LICENSES/GPL-3.0-or-later.txt` -- standard SPDX text.
- `MODIFICATIONS.md` (this file).
- `README.md` (rewritten) -- user-facing entry with developer pointer.
- `CONTRIBUTING.md` (rewritten) -- contribution rules.
- `CODE_OF_CONDUCT.md` -- standard.
- `.github/ISSUE_TEMPLATE/`, `.github/PULL_REQUEST_TEMPLATE.md` -- GitHub
  conventions.
- `docs/wiki/` -- developer documentation, source-of-truth for the GitHub
  wiki.
- `module-example/augins-head-sway/` -- featured tutorial module, separate
  from `samples/`.
- `scripts/relicense.ps1` -- one-shot license normaliser.
- `scripts/fetch-xr-deps.ps1`, `scripts/fetch-xr-deps.sh`,
  `xr-deps-versions.json` -- pinned external dependency downloads.

## Out of scope here

Many upstream-Monado source files have only had their copyright line bumped
to add a Marko Kazimirovic claim alongside the original Collabora line; this
is fork-mark housekeeping, not substantive modification, and these files
retain their original BSL-1.0 license. They are not enumerated above.
