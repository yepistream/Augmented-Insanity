<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Modifications relative to upstream Monado

Subsystem-grained summary of what Augmented Insanity adds or
changes relative to upstream Monado
(<https://gitlab.freedesktop.org/monado/monado>). For per-file
license attribution, the `SPDX-License-Identifier` header at the
top of each source file is authoritative.

The fork point was a snapshot taken from upstream Monado main in
2025. Merges back to upstream are not currently practiced.

## Module system (new subsystem)

Path: `src/xrt/augins/`

The largest addition. A general-purpose module loader that lets
third parties ship `.augins` packages providing OpenXR-shaped
capabilities (head pose, hand tracking, ...) without recompiling
the runtime. Components, all new:

- `module_abi.h` -- the public ABI header. Defines
  `AUG_MANIFEST_VERSION` and `struct aug_host_api`. The single
  header a module author needs to include.
- `host_api.{h,cpp}` -- singleton `aug_host_api` table. Holds the
  JVM and Application Context, exposes the per-module data-dir
  TLS push/pop used by the dispatcher, and routes the `log`
  entry through `__android_log_print`.
- `dispatch.{h,cpp}` -- name -> vector<entry> dispatch registry.
  Public probes (`aug_has_modules_for`, `aug_get_modules_for`)
  used by the generated IPC server and by adapters; internal
  register / sort-by-priority / clear used by the loader.
- `extract.{h,cpp}` -- vendored miniz-based `.augins` zip
  extraction with a size-stamp cache that lets a subsequent
  start skip extraction for byte-identical sources. Path
  traversal guards.
- `manifest.{h,cpp}` -- cJSON parser for `metadata.json`.
  Validates `Manifest_Version`, requires `ID` / `Version` /
  `Implemented_Functions`, reads optional `Name` / `Description`
  / `Priority`.
- `loader.{h,cpp}` -- scans the modules dir, extracts each zip,
  parses each manifest, preloads sibling `.so` files
  (`RTLD_NOW | RTLD_GLOBAL`), dlopens the main `<ID>.so`,
  dlsyms `Implemented_Functions`, registers dispatch entries,
  stores lifecycle hooks; finally sorts the registry by priority
  and fires `aug_on_module_load` for each module.
- `lifecycle.{h,cpp}` -- loaded-module list with the per-module
  `(handle, id, data_dir, on_load, on_unload)`; fans
  `aug_on_module_load` forward at start and
  `aug_on_module_unload` in reverse at shutdown; dlcloses after.
- `adapters.{h,cpp}` -- hand-written per-IPC-call adapters that
  translate IPC msg/reply structs into OpenXR-shaped arguments,
  iterate registered modules, and pack the result back.
  Currently: `aug_adapter_space_locate_space` (backs
  `xrLocateSpace`) and `aug_adapter_space_locate_device` (backs
  the T_base_xdev half of `xrLocateViews` via the synthetic name
  `aug_LocateDeviceInSpace`, filtered to head xdev only).

## IPC dispatch fork (generated)

Path: `src/xrt/ipc/shared/proto.py`

`proto.py` carries two new tables: `aug_ipc_to_xr` (maps IPC
call name -> module-facing OpenXR or synthetic name) and
`aug_implemented_adapters` (the subset that has a hand-written
adapter in `src/xrt/augins/adapters.cpp`). The codegen now emits
a dispatch fork in `ipc_server_generated.c` per matching IPC call:

```c
#ifdef XRT_FEATURE_AUG_INS
if (aug_has_modules_for("<openxr-or-synthetic-name>")) {
    reply.result = aug_adapter_<ipc-call>(...);
} else
#endif
{
    reply.result = ipc_handle_<ipc-call>(...);
}
```

Modules absent from the registry pay only a `unordered_map::count`
probe per IPC call. Modules registered for the name go through
the adapter, which calls `ipc_handle_<call>` first for the
baseline reply (Q2), then iterates modules in priority order.

The v0.1 hook system (`augins_fire_hooks`, mirror-struct calling
convention, post-reply patching) was removed in the Phase 1
demolition that preceded the v0.2 rewrite; nothing in `proto.py`
or `ipc_server_process.c` calls into hooks anymore.

## IPC server helper: head-xdev role probe

Path: `src/xrt/ipc/server/ipc_server_objects.{h,c}`

New helper `ipc_server_xdev_is_head_role(ics, id)` resolves an
IPC `xdev_id` against `ics->server->xsysd->static_roles.head`
and returns whether the two match. Used by
`aug_adapter_space_locate_device` to filter dispatch to head
queries only; non-head devices bypass module dispatch entirely.

Safe on bad inputs (returns false); intended for the hot IPC
dispatch path.

## Space overseer: public-header exposure

Path: `src/xrt/base/b_space_overseer.{h,c}`

No code change. The public header now exposes
`b_space_overseer_create_null_space` and
`b_space_overseer_link_space_to_device`. The v0.1 stub-xdev
factory used these to register module-advertised xdevs with the
space overseer after `b_space_overseer_legacy_setup` had already
run. The stub-xdev factory itself was deleted in Phase 1
demolition; the header exposure is preserved for the Mercury
v0.2 rewrite, which will reintroduce module-advertised
hand-tracking devices through a different mechanism (an
`xrCreateHandTrackerEXT` adapter instead of a stub xdev).

## Hand-tracking driver `models_dir` override

Paths:
- `src/xrt/include/tracking/t_hand_tracking.h`
- `src/xrt/drivers/ht/ht_driver.c`

`struct t_hand_tracking_create_info` gains a
`const char *models_dir` field. When non-NULL, `ht_device_create`
uses it in place of `u_file_get_hand_tracking_models_dir()`
(which on Android resolves to a Linux-only path that does not
exist). Lets `.augins` modules ship the Mercury ONNX models
inside their zip and pass the per-module extraction directory.

Preserved for the Mercury v0.2 rewrite; the field has no
in-tree consumer in v0.2 base.

## Hand-tracking async worker scheduling priority

Path: `src/xrt/tracking/hand/t_hand_tracking_async.c`

The Mercury inference worker thread now calls
`setpriority(PRIO_PROCESS, 0, 10)` very early on Linux/Android.
ARCore VIO threads (default nice 0) preempt it, preventing the
inference workload from stalling visual-inertial odometry
feature extraction during initialisation. Combined with
in-module frame decimation, this is what made Mercury and ARCore
coexist in the same process in v0.1; the same setup will apply
to the v0.2 Mercury rewrite.

## Service entry point

Path: `src/xrt/targets/service-lib/service_target.cpp`

`nativeStartServer` calls `augins_host_api_set_jvm_ctx(jvm, ctx)`
to bind the host API singleton; `IpcServerHelper::startServer`
calls `augins_loader_init(modules_dir, cache_dir)` before the
IPC mainloop starts; `nativeShutdownServer` calls
`augins_loader_shutdown()` after the mainloop exits. All three
are `#ifdef XRT_FEATURE_AUG_INS`.

The v0.1 `augins::launch(...)` single-entry helper was replaced
by the two-call (set_jvm_ctx + loader_init) sequence to match
where the JVM is available versus where the cache directory is
known.

## Android runtime manifest and Gradle setup

Paths:
- `src/xrt/targets/openxr_android/build.gradle`
- `src/xrt/targets/openxr_android/src/outOfProcess/AndroidManifest.xml`

The `outOfProcess` flavor's Gradle dependencies grew an optional
`com.google.ar:core:1.52.0` entry. The corresponding manifest
gained CAMERA permission, the `android.hardware.camera.ar`
feature, and a `com.google.ar.core` meta-data entry. Required
by the ARCore head-pose module when shipped; benign when the
module is not installed.

## AugInsTestActivity

Paths:
- `src/xrt/targets/openxr_android/src/outOfProcess/java/com/augmented_insanity/runtime/AugInsTestActivity.kt`
- `src/xrt/targets/openxr_android/src/outOfProcess/java/com/augmented_insanity/runtime/AugInsBridge.kt`
- `src/xrt/targets/openxr_android/src/outOfProcess/res/layout/activity_augins_test.xml`
- `src/xrt/targets/openxr_android/src/outOfProcess/res/values/strings.xml`

A minimal launcher Activity for poking at the runtime on-device
without an OpenXR client. Buttons: Start GRS
(`startForegroundService`), Stop GRS (`SHUTDOWN_ACTION`), Refresh.
The native bridge `AugInsBridge` reads the loaded-module list and
state from the loader for display.

## Sample modules

Paths: `samples/augins-arcore-headpose/`,
`samples/augins-test-noop/`, `samples/augins-test-locate-space/`

All new. See [docs/wiki/Implementation-Status.md](docs/wiki/Implementation-Status.md)
for status.

- `augins-arcore-headpose` -- production. Provides 6DoF head
  pose via Google ARCore. Exports `aug_LocateDeviceInSpace`;
  the adapter filters to head xdev only. Spawns an `ArSession`
  in a worker thread spawned from `aug_on_module_load`.
- `augins-test-noop` -- verification module. Lifecycle hooks
  only, no dispatched functions. Smallest valid `.augins` zip.
- `augins-test-locate-space` -- verification module. Overrides
  `xrLocateSpace` with a sentinel pose (`42.0, 42.0, 42.0`) for
  the dispatch chain trace.

## Sample-build infrastructure

Paths: `settings.gradle`, each sample's `build.gradle` and
`CMakeLists.txt`.

Each sample is a Gradle subproject. The build does not use the
Android Gradle Plugin for the `.so` step (the AGP forces `lib*`
naming on the output, which would break
`dlopen("<ID>.so", ...)`); instead each sample drives `cmake -G
Ninja` directly via `Exec` tasks, then a `Zip` task assembles
the resulting `.so` plus `metadata.json` plus any vendored
sibling `.so` files and bundled assets into `<short-name>.augins`.

## Repository scaffolding (new files)

- `README.md` (rewritten) -- user-facing entry with developer
  pointer.
- `LICENSE` (rewritten) -- multi-license overview.
- `LICENSES/GPL-3.0-or-later.txt` -- standard SPDX text.
- `MODIFICATIONS.md` (this file).
- `CONTRIBUTING.md` (rewritten) -- contribution rules.
- `CODE_OF_CONDUCT.md` -- standard.
- `.github/ISSUE_TEMPLATE/`, `.github/PULL_REQUEST_TEMPLATE.md`.
- `docs/wiki/` -- developer documentation, source-of-truth for
  the GitHub Wiki.
- `module-example/augins-head-sway/` -- tutorial module.
  Decorator pattern: adds a sinusoidal X-offset to the head
  pose returned by the runtime via `aug_LocateDeviceInSpace`.
  Standalone NDK build, no external SDK dependencies. See
  [docs/wiki/Module-Example-Walkthrough.md](docs/wiki/Module-Example-Walkthrough.md).
- `scripts/relicense.ps1` -- one-shot license header normaliser.
- `scripts/fetch-xr-deps.ps1`, `scripts/fetch-xr-deps.sh`,
  `xr-deps-versions.json` -- pinned external dependency
  downloads (Mercury ONNX models, OpenCV, ONNX Runtime).
  Preserved for the Mercury v0.2 rewrite; no in-tree consumer
  in v0.2 base.

## Out of scope here

Many upstream-Monado source files have only had their copyright
header bumped to add a Marko Kazimirovic line alongside the
original Collabora line; this is fork-mark housekeeping, not
substantive modification, and these files retain their original
BSL-1.0 license. They are not enumerated above.
