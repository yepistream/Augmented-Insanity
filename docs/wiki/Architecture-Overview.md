<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Architecture Overview

Augmented Insanity is a fork of Monado that turns the OpenXR runtime into
a *modular* runtime: capabilities like head pose and hand tracking are
shipped as separate `.augins` packages and loaded at runtime, without
recompiling the runtime APK.

## Two artefacts

```
+-----------------------------+         +-----------------------------+
|        OpenXR client        |  IPC    |  Augmented Insanity service |
|  (helloxr, Godot, Unity,    | <-----> |  (com.augmented_insanity.   |
|   any XR app)               |         |   runtime.out_of_process)   |
+-----------------------------+         +-----------------------------+
                                                |
                                                | dlopen
                                                v
                                        +-----------------------------+
                                        |  .augins modules            |
                                        |    augins-arcore-headpose   |
                                        |    augins-mercury-...       |
                                        |    augins-noop              |
                                        |    augins-head-sway         |
                                        +-----------------------------+
```

The IPC bridge between client and service is unchanged from upstream
Monado. The service is the only thing that touches devices, sensors, and
modules.

## Runtime vs module

The **runtime** is infrastructure:

- Module loader (dependency resolution + `dlopen`).
- IPC server, including the generated dispatch table.
- Lifecycle manager (`aug_onModuleLoad` -> `aug_runtimeInit` -> `aug_onConnect` -> ...).
- Versioned host API table (function pointers exposed to modules).
- Camera-frame broker.
- Stub-xdev factory.
- Manifest aggregator (extension list, system-property bits).



A **module** is a capability:

- Ships as a `.augins` zip (a renamed zip with a `.so`, a `metadata.json`,
  any vendored dependencies, optional ONNX models, optional license
  texts and/or any other external resources needed).
- Declares hooks in its manifest (`Implemented_Functions`).
- Implements those hooks as `extern "C"` symbols in the `.so`.
- Optionally consumes the host API to register producer callbacks
  (hand-tracker callback, frame-broker subscription, etc).
- Is independently installable, removable, and version-managed.

## Dispatch flow

Every IPC call from the client to the service that is enumerated in
`src/xrt/ipc/shared/proto.py`'s `aug_ipc_to_xr` dictionary follows this
sequence on the service side:

1. Client invokes an OpenXR function (e.g. `xrLocateViews`).
2. Client-side OpenXR loader translates to an IPC message
   (`device_get_tracked_pose`) and sends it.
3. Service-side `ipc_dispatch` (generated from `proto.py`) receives the
   message.
4. The handler computes a real reply by calling into Monado
   (`ipc_handle_device_get_tracked_pose`).
5. **Aug-Ins dispatch fires before the reply is sent**:
   `augins_fire_hooks("aug_deviceGetTrackedPose", ics, msg, &reply, NULL)`.
6. The dispatch looks up `aug_deviceGetTrackedPose` in
   `g_dispatch_table` (a `unordered_map<string, vector<entry>>`), finds
   every module that registered a hook for that name, and calls each
   one in priority order.
7. Each hook receives the message and reply pointers and may read or
   overwrite the reply.
8. After all hooks return, the (possibly modified) reply is sent back
   to the client.

See [IPC Hook Dispatch](IPC-Hook-Dispatch.md) for the data structures
and code paths in detail.

## Side channels

A few cross-module concerns do not fit the pure IPC-hook model:

- **Camera frames.** Example: Only one ARCore session can exist per process, so
  the head-pose module owns it. Other modules subscribe to the
  resulting Y-plane via the host-API frame broker. See
  [Camera Frame Broker](Camera-Frame-Broker.md). [ ***WILL BE CHANGED IN FUTURE UPDATES*** ]
- **Hand-tracker xdevs.** The runtime fabricates stub `xrt_device`
  instances when modules advertise hand tracking; queries from the
  client's xrLocateHandJointsEXT bottom out in the module-registered
  callback via the host API's `register_hand_tracker`. See
  [Stub Xdev Factory](Stub-Xdev-Factory.md).
- **Per-module data dir.** Each `.augins` zip is extracted into its
  own directory; the host API exposes that path to the module via
  `get_module_data_dir()`. See
  [Host API Reference](Host-API-Reference.md).

## What is NOT modular

Some things stay in the runtime APK and cannot be replaced by a module:

- The OpenXR state tracker (`src/xrt/state_trackers/oxr/`).
- The compositor (`src/xrt/compositor/`).
- The IPC layer itself.
- The Vulkan / GLES interop with the Android surface.

I'm planning to implement modularity into this ones too, but as they are very *fragile* this will be tested at a later date. (Other then the IPC Layer)

## See next

- [Module System](Module-System.md) for the lifecycle and loader.
- [Host API Reference](Host-API-Reference.md) for what modules can
  call on the runtime side.
- [Module Example Walkthrough](Module-Example-Walkthrough.md) for a
  worked example.
