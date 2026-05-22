<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Architecture Overview

Augmented Insanity runs as an **out-of-process** OpenXR runtime on
Android. There are two processes involved at runtime:

- The XR app process (any app that uses OpenXR -- hello_xr, a
  Godot game, an in-house demo). Loads the Khronos OpenXR loader,
  which loads `libopenxr_monado.so` as the active runtime. Most
  OpenXR calls turn into IPC messages to the service.
- The runtime service process
  (`com.augmented_insanity.runtime.out_of_process`). Contains the
  compositor, device drivers, tracking system, and the Aug-Ins
  modules. Holds the CAMERA permission, the ARCore Java AAR, the
  display surface, and shared state across XR clients.

The split is inherited from upstream Monado. The v0.2 module
system runs in the service process: modules need camera access,
may open an `ArSession`, and outlive individual XR clients.

```
+---------- XR app process (untrusted) ----------+
|                                                |
|  Khronos OpenXR loader                         |
|       |                                        |
|       v                                        |
|  libopenxr_monado.so (IPC client side)         |
|       |                                        |
|       v IPC                                    |
+-----------------|------------------------------+
                  |
                  v
+---------- runtime service process (privileged) ----------+
|                                                          |
|  ipc_server_generated.c   (IPC dispatch table)           |
|    case IPC_SPACE_LOCATE_DEVICE:                         |
|      #ifdef XRT_FEATURE_AUG_INS                          |
|      if (aug_has_modules_for("aug_LocateDeviceInSpace")) |
|        aug_adapter_space_locate_device(...)              |
|      else                                                |
|      #endif                                              |
|        ipc_handle_space_locate_device(...)               |
|                                                          |
|  aux_augins (loader + dispatch + adapters + host API)    |
|       |                                                  |
|       v dlopen + dlsym                                   |
|  com.x.y.z.so   <-- the module from <id>.augins          |
|                                                          |
|  Monado: compositor, devices, drivers, state tracker     |
+----------------------------------------------------------+
```

## What a module actually is

A module is a `.augins` zip containing:

- `<ID>.so` -- the module's native code. Filename is `<manifest ID>.so`
  with no `lib` prefix (the loader dlopens by that exact name).
- `metadata.json` -- manifest (see [Manifest-Schema](Manifest-Schema.md)).
- Any sibling `.so` files the module needs (e.g.
  `libarcore_sdk_c.so` for the ARCore module). The loader preloads
  these with `RTLD_NOW | RTLD_GLOBAL` before dlopen'ing the main `.so`.
- Bundled assets (calibration files, ONNX models, etc.). The module
  reads them via `host->get_module_data_dir()`.

A module exports:

- (Optional) `aug_on_module_load(host)` -- called once after dlopen.
  Cache the host API table, spawn workers, open an ArSession.
- (Optional) `aug_on_module_unload()` -- called at service shutdown.
  Join workers, release resources.
- One or more OpenXR-shaped functions named in the manifest's
  `Implemented_Functions` array. The service-side adapters
  dispatch into these when the corresponding IPC call arrives.

## How a call flows

Trace of an `xrLocateViews` call with the ARCore module providing
head pose:

1. XR app calls `xrLocateViews(session, ..., views[2])`.
2. The OpenXR state tracker (`oxr_session_views.c`) decomposes the
   call per view. For the head's pose in world space it calls
   `oxr_space_locate_device(head_xdev, base_space, time, &T)`.
3. The IPC client space overseer translates this into
   `ipc_call_space_locate_device(...)` and sends an
   `IPC_SPACE_LOCATE_DEVICE` message to the service.
4. The service's generated dispatcher
   (`ipc_server_generated.c`, case `IPC_SPACE_LOCATE_DEVICE`) checks
   `aug_has_modules_for("aug_LocateDeviceInSpace")`. With the ARCore
   module registered, this returns true.
5. The dispatcher calls `aug_adapter_space_locate_device(...)`. The
   adapter calls `ipc_handle_space_locate_device(...)` first (Q2:
   runtime default runs before modules), then unpacks the
   `xrt_space_relation` reply into an `XrSpaceLocation` and iterates
   modules registered for `aug_LocateDeviceInSpace` in priority
   order (Q1: last-write-wins).
6. The ARCore module's `aug_LocateDeviceInSpace` function snapshots
   its worker-cached ARCore pose and writes it into the
   `XrSpaceLocation`.
7. The adapter packs the `XrSpaceLocation` back into the IPC reply
   and returns. The runtime sends the reply over IPC.
8. The XR app sees the ARCore-tracked pose in `views[i].pose`.

For why the call is split into `space_locate_device` plus
`device_get_tracked_pose` on the service side, see
[Service-Side-Dispatch](Service-Side-Dispatch.md).

## Why service-side and not client-side

Modules run in the service process, not in client XR-app
processes. Three reasons:

- Privileged resources (camera, microphone, ARCore session) cannot
  be requested per-XR-app without forcing every game to declare
  unrelated permissions.
- Some resources are singleton on Android. There is exactly one
  `ArSession` per process; on a phone this is effectively one per
  device, not one per app.
- Modules that override device state must coordinate across XR
  clients. Service-side dispatch puts them where that coordination
  already happens.


## Where the code lives

- `src/xrt/augins/` -- the runtime-side module subsystem (loader,
  dispatch registry, host API, manifest parser, zip extractor,
  adapters).
- `src/xrt/ipc/shared/proto.py` -- the codegen that emits the
  dispatch fork (`aug_has_modules_for` probe) in
  `ipc_server_generated.c`.
- `src/xrt/ipc/server/ipc_server_objects.{h,c}` -- xdev role
  helpers used by the adapters (e.g. `ipc_server_xdev_is_head_role`).
- `samples/augins-arcore-headpose/` -- production module.
- `samples/augins-test-noop/`, `samples/augins-test-locate-space/` --
  smaller test modules I used while verifying Phase 2b / Phase 2d.
