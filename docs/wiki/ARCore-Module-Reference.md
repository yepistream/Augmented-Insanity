<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# ARCore Head-Pose Module Reference

`samples/augins-arcore-headpose/` provides 6DoF head pose via
Google ARCore. The module spawns an `ArSession` in the runtime
service process and exports an OpenXR-shaped function the
runtime dispatches into.

## Behaviour

- `aug_on_module_load` captures JVM and Context from the host API,
  spawns a worker thread, and starts an `ArSession` against the
  device's camera.
- The worker calls `arcore_min_tick` in a ~60 Hz loop and stashes
  each new pose into a mutex-guarded global.
- `aug_LocateDeviceInSpace` (see
  [Service-Side-Dispatch](Service-Side-Dispatch.md)) overwrites
  the `XrSpaceLocation` with the latest ARCore pose. Non-head
  devices never reach this function; the adapter filters them
  out.
- `aug_on_module_unload` signals the worker to stop, joins it,
  releases the `ArSession`, and detaches the JNI thread.

OpenXR clients on this runtime receive ARCore-tracked 6DoF head
pose in `XrView.pose` returned by `xrLocateViews`.

## Not implemented yet

- `T_xdev_head` override. The module only adapts
  `space_locate_device` (the T_base_xdev half of
  `xrLocateViews`). The T_xdev_head half stays whatever Monado's
  gyro+accel fusion produces. The T_base_xdev transform dominates
  so the resulting view pose is correct; v0.2.x can add a
  `device_get_tracked_pose` adapter if a downstream consumer
  needs ARCore on that half too. See [Roadmap](Roadmap.md).
- `LOCAL_FLOOR` height offset. Reference-space-type-aware offsets
  are not applied; head pose renders at headset height regardless
  of whether the app requested `LOCAL`, `LOCAL_FLOOR`, `STAGE`,
  or `UNBOUNDED`.
- Camera frame publishing. v0.1 published the YUV camera frame's
  Y plane to a host-API broker for the Mercury hand-tracking
  module. Host API v1 has no broker entry; Mercury is also not
  yet rewritten on the v0.2 ABI. The broker entry returns with
  the Mercury rewrite.

## Files

- `arcore_module.cpp` -- module entry points and the worker
  thread. ~200 lines.
- `vendor/arcore_c_api.h`, `vendor/arcore_instance.{cpp,h}`,
  `vendor/xrt/xrt_handles.h` -- vendored ARCore SDK C API plus a
  thin wrapper (`arcore_min`) over `ArSession_*` calls, written
  for the earlier in-tree ARCore xdev experiments.
- `vendor/arm64-v8a/libarcore_sdk_c.so` -- prebuilt ARCore SDK,
  shipped under `LICENSE-arcore`.
- `metadata.json` -- manifest (Manifest_Version 1).
- `CMakeLists.txt` -- standalone NDK build.
- `build.gradle` -- packaging + adb push.

## Dependencies

### On the device

- Google Play Services for AR. The ARCore SDK is a wrapper over
  an out-of-process service Google ships; `ArSession_create`
  fails without it.
- ARCore-compatible phone. See
  [Google's compatibility list](https://developers.google.com/ar/devices).
- CAMERA permission, granted to the runtime APK (declared in
  `AndroidManifest.xml`). Prompted on first runtime activity
  launch.

### At build time

- `src/xrt/augins/module_abi.h`.
- OpenXR headers (`src/external/openxr_includes/`).
- Vendored ARCore SDK headers (`vendor/`).
- Android NDK 26.3.x and CMake 3.22.x.

## Lifecycle

```
aug_on_module_load(host)
  - validate host->struct_version >= 1
  - cache host
  - GetEnv on main thread (do NOT Attach/Detach here)
  - NewGlobalRef the Application Context
  - spawn worker(vm, ctx_global)
  - return 0

worker(vm, ctx_global)
  - AttachCurrentThread (worker is a new thread)
  - arcore_min_start_ex(...)
  - loop {
      arcore_min_tick(...)
      stash pose into g_cached_pose (mutex)
      sleep ~16 ms
    } while (!g_stop)
  - arcore_min_stop()
  - DeleteGlobalRef(ctx_global)
  - DetachCurrentThread (worker is responsible for its own detach)

aug_LocateDeviceInSpace(baseSpace, time, &location)
  - snapshot g_cached_pose under mutex
  - if not tracking: clear POSITION_TRACKED bit, return XR_SUCCESS
  - else: write pose into location, set tracked flags, return XR_SUCCESS

aug_on_module_unload()
  - signal g_stop
  - join worker
  - return
```

The pose snapshot uses `std::mutex`. The critical section is a
half-dozen float copies and a bool.

## Configuration

No per-module settings file in v0.2. ARCore configuration
(autofocus, camera HZ mode) is set in `arcore_min_config` at
session-start time. Current defaults:

- `focus_mode = AUTO_FOCUS_ENABLED`
- `camera_hz_mode = MAX_ARCAMERA_HZ`
- `texture_update_mode` -- default (OES)
- Plane detection, depth, augmented faces -- disabled

Change by editing `arcore_module.cpp` and rebuilding. A per-module
settings UI is `[Planned]` for v0.2.2.

## Observing dispatch on the device

The module emits a rate-limited info line every 240 calls:

```
adb logcat -s "Aug-Ins.ARCore:V"
```

Output during a running XR app:

```
I Aug-Ins.ARCore: aug_on_module_load: host API v1 accepted
I Aug-Ins.ARCore: aug_on_module_load: ARCore worker spawned
I Aug-Ins.ARCore: ARCore session started
I Aug-Ins.ARCore: aug_LocateDeviceInSpace call #240 (rate-limited 1/240)
I Aug-Ins.ARCore: aug_LocateDeviceInSpace call #480 (rate-limited 1/240)
...
```

The dispatch fires twice per frame (one per view); a 90 Hz client
produces ~180 calls/sec, roughly one log line per 1.3 seconds.

## Known limitations and bugs

| Item | Status | Note |
|------|--------|------|
| `T_xdev_head` is gyro+accel, not ARCore | `[Working]` | T_base_xdev dominates so the view pose is correct, but the device's own-frame pose inside the service is not ARCore-driven. v0.2.x can add a second adapter. |
| Initial ~1 second ARCore convergence | `[Working]` | During convergence the module returns `XR_SUCCESS` with the `POSITION_TRACKED` bit cleared. |
| One `ArSession` per service process | By design | XR clients share the same ARCore stream. Camera is not re-opened per-client. |
| No `LOCAL_FLOOR` Y offset | `[WIP]` | Reference-space-type-aware Y offsets are not applied. |
| Eager ARCore start at module load | `[WIP]` | The worker runs even when no XR client is connected. v0.2.x will lazy-start on first dispatch. |

## Credits

The ARCore SDK and Google Play Services for AR are Google
products. See [Acknowledgements](Acknowledgements.md). The
prebuilt `libarcore_sdk_c.so` in this module's zip is
redistributed under Google's ARCore SDK license (file
`LICENSE-arcore` in the sample directory).

The `arcore_min` C wrapper was written for the earlier in-tree
ARCore xdev experiments and preserved under `vendor/`.
