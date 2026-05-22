<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Known Issues

Observed bugs and limitations in v0.2 that are not yet fixed.
Fix targets are in [Roadmap](Roadmap.md).

## Runtime startup

### `BackgroundServiceStartNotAllowedException` from a cold launch

When an XR app binds to the runtime via the Khronos broker while
the runtime APK is in the background, Android 12+ rejects the
bare `startService` call inside `MonadoService.onCreate`. The
runtime process crashes with
`BackgroundServiceStartNotAllowedException` before
`nativeStartServer` runs; modules never load; the XR client sees
no available runtime.

Workaround: start the runtime via `am start-foreground-service`
(or open `AugInsTestActivity` and tap "Start GRS") before
launching the XR app. With the runtime already a foreground
service, subsequent XR-app binds work.

Fix: `MonadoService.onCreate` should `startForegroundService`
instead of `startService` for the self-rebind. Inherited upstream
issue; tracked for v0.2.x.

### `adb am force-stop` bypasses graceful shutdown

`force-stop` sends SIGKILL. `Service.onDestroy` does not run,
`nativeShutdownServer` does not run, `augins_loader_shutdown` does
not run, `aug_on_module_unload` does not fire. Module workers do
not get joined.

This is standard Android force-stop behaviour. To verify
graceful-shutdown paths, use the test activity's "Stop GRS"
button (which sends `SHUTDOWN_ACTION`) or let the service idle
out via its `exit_when_idle` logic.

## Tracking quality

### `T_xdev_head` is gyro+accel, not ARCore

The v0.2 ARCore module only adapts `space_locate_device` (the
T_base_xdev half of `xrLocateViews`). The T_xdev_head half stays
whatever Monado's default gyro+accel fusion produces. The
T_base_xdev transform dominates and the resulting view pose is
correct, but the head xdev's own-frame pose inside the service
is not ARCore-driven.

Symptom: a downstream consumer that reads the head xdev's raw
tracked pose (rather than going through `xrLocateViews`) sees
gyro+accel output instead of ARCore. No current consumer does
this.

Fix: add `aug_adapter_device_get_tracked_pose` with the same
head-only filter pattern; have the ARCore module export both
`aug_LocateDeviceInSpace` and `aug_DeviceGetTrackedPose`. v0.2.x.

### `LOCAL_FLOOR` height offset not applied

Reference-space-type-aware Y offsets are not applied. Apps
requesting `LOCAL_FLOOR` get the head pose at headset height
instead of floor height. Phone-VR clients rarely notice (no
real floor detection on a phone) but the behaviour is wrong
relative to the OpenXR spec.

### ARCore first-frame latency

`ArSession_create` takes ~1 second on cold start (the ARCore
service and camera pipeline are initialising). During this
window the module returns `XR_SUCCESS` with `POSITION_TRACKED`
cleared; the client treats position as not yet reliable. Once
converged the module reports tracked.

## Loader edge cases

### `aug_on_module_load` rejection does not unwind dispatch entries

A module's `aug_on_module_load` returning non-zero is logged but
the dispatch entries are not pulled out of the registry. The
module's `.so` stays loaded and its exported symbols stay
dispatchable; subsequent calls into those symbols run the
module's code despite the rejection.

Harmless for modules that return non-zero before allocating
state. A problem for modules that do partial init and then
reject on a late check; their half-initialised state stays
callable.

Fix: stage dispatch entries and only commit after
`aug_on_module_load` returns zero. v0.2.x.

### Sibling `.so` preload uses zip extraction order

The loader preloads sibling `.so` files in the order `opendir`
returns them, which is filesystem-dependent. Two sibling `.so`
files with a load-order dependency between them produce
non-deterministic results.

Workaround: do not ship two `.so` files in the same `.augins`
with a load-order dependency between them.

Fix: a `Preload_Order` manifest field listing sibling `.so`
names in dependency order. v0.2.x, only if a real case needs it.

## Documentation and code-base hygiene

### PowerShell `Tee-Object` writes UTF-16-LE with BOM

Logging a build run via `Tee-Object -FilePath` writes UTF-16;
downstream grep tools silently miss lines. Use:

```
... 2>&1 | Out-File -Encoding utf8 build.log
```

Tooling pitfall, not a code bug.

### Spurious `fatal: not a git repository` from gradle

Gradle invokes a `git rev-parse` for version-stamping that fails
with this line on some setups. Not a build failure; ignore
unless the overall Gradle task exit code is non-zero.

## Other minor issues

### Single ArSession per service process

The ARCore module opens one `ArSession` in `aug_on_module_load`,
holds it for the service's lifetime, and joins on
`aug_on_module_unload`. This is correct for v0.2 (one `ArSession`
shared across all XR clients) but the service consumes camera
and CPU even when no XR client is connected.

Lazy-start (open on first dispatch, idle-out after N seconds of
no calls) is a v0.2.x optimisation.

### One service process per package

The runtime APK declares a single service process. Concurrent XR
clients share it; per-client process isolation is `[Planned]`
for v0.3+.
