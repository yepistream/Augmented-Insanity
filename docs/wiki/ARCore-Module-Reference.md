<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# ARCore Module Reference `[WIP]`

`samples/augins-arcore-headpose/` -- 6DoF head-pose tracking module
backed by Google ARCore. Also acts as the camera-frame producer for
downstream modules (currently only the Mercury hand-tracking module
consumes from it).

## What it does

- On `aug_onConnect`, spawns a worker thread that creates and runs
  an `ArSession` for the lifetime of the runtime.
- Hooks two IPC dispatch calls:
  - `xrLocateSpace` -- when a client locates the VIEW reference
    space against any world-locked reference space (LOCAL,
    LOCAL_FLOOR, STAGE, UNBOUNDED), the hook overrides the reply
    with ARCore's tracked pose.
  - `aug_deviceGetTrackedPose` -- when the head xdev's
    `XRT_INPUT_GENERIC_HEAD_POSE` input is queried, same override.
    This path backs the `T_xdev_head` half of `xrLocateViews`.
  - `aug_spaceCreateSemanticIds` -- caches the
    LOCAL/LOCAL_FLOOR/STAGE/UNBOUNDED/VIEW reference-space IDs as
    soon as the client requests them, so the other two hooks can
    filter accurately.
- Publishes the YUV camera Y plane through the host-API frame
  broker every tick. Subscribers (Mercury) consume it.

## Why this module exists

There can only be one `ArSession` per process. If we let multiple
modules each try to start ARCore, the second `ArSession_create()`
fails. So one module owns ARCore (this one), and any other module
that wants ARCore camera frames goes through the broker.

The pose interception is a separate concern: even with ARCore
running, the OpenXR state tracker would otherwise return Monado's
default Android-Sensors gyro+accel fused pose to clients. The
module's hooks are what make ARCore's VIO actually visible to
client apps.

## File layout

```
samples/augins-arcore-headpose/
    metadata.json
    settings.json
    LICENSE-arcore                 third-party license attribution
    arcore_module.cpp              the Aug-Ins glue
    CMakeLists.txt
    build.gradle
    vendor/
        arcore_c_api.h             ARCore C SDK header
        arcore_instance.cpp        thin wrapper around ARCore C API
        arcore_instance.h
        xrt/xrt_handles.h          minimal Monado shim
        arm64-v8a/
            libarcore_sdk_c.so     prebuilt ARCore SDK (~6 MB)
```

## Manifest

```json
{
    "Name": "Aug-Ins ARCore Head Pose",
    "ID": "com.augmented_insanity.samples.arcore_headpose",
    "Implemented_Functions": [
        "aug_spaceCreateSemanticIds",
        "xrLocateSpace",
        "aug_deviceGetTrackedPose"
    ]
}
```

No `Advertised_OpenXR_Features` -- ARCore head pose is
already the natural answer to `xrLocateViews`; the module just
substitutes a better source for the same answer. No system bits, no
new extensions.

## Dispatch flow

For a client calling `xrLocateViews`:

1. Client-side OXR layer calls
   `xrt_device_get_view_poses(head_xdev, ..., &T_xdev_head, fovs, poses)`,
   which internally fires the IPC `device_get_tracked_pose` for the
   head xdev's GENERIC_HEAD_POSE input.
2. Server-side dispatch: real Monado handler computes the gyro+accel
   pose into `reply.relation`.
3. Aug-Ins fires `aug_deviceGetTrackedPose` hooks. The
   arcore-headpose hook reads `m->name`, sees
   `XRT_INPUT_GENERIC_HEAD_POSE`, and overwrites
   `reply.relation` with ARCore's pose.
4. Client-side OXR layer separately calls
   `oxr_space_locate_device(head_xdev, base_space, ...)`, which
   fires the IPC `space_locate_device` (mapped to xr name
   `xrLocateViews` in Aug-Ins). The runtime returns the natural
   `T_base_xdev` (identity for LOCAL, floor offset for
   LOCAL_FLOOR, etc.).
5. Client composes `T_base_head = T_xdev_head * T_base_xdev`, gets
   ARCore's pose adjusted for the play space.

For a client calling `xrLocateSpace(view, base, ...)`:

1. The IPC `space_locate_space` fires; the server-side handler
   walks the space tree in-process (not via IPC) and computes the
   final relation.
2. Aug-Ins fires `xrLocateSpace`. The arcore-headpose hook
   verifies `m->space_id` is the cached VIEW id and
   `m->base_space_id` is one of the cached world-locked IDs, and
   overwrites with ARCore's pose.

## Camera-frame publish

After every successful ArCore frame tick, the worker calls:

```cpp
g_host->publish_camera_frame_y8(
    img.plane_data[0],
    img.width, img.height, img.plane_row_stride[0],
    img.timestamp_ns,
    g_intr_valid ? &g_cam_intr : nullptr);
```

The Y plane of the YUV camera image is the input format Mercury
expects. `g_cam_intr` is populated once on the first successful
`arcore_min_get_intrinsics` call.

## Threading

Three threads in this module:

- The IPC server's per-client thread fires the dispatch hooks
  synchronously. They are short -- read pose from a cached
  `xrt_space_relation`, call `set_locate_space_relation`, return.
- The arcore-headpose worker thread owns the `ArSession`, ticks
  it on every loop iteration, updates the cached pose under a
  mutex, and publishes the camera frame to the broker.
- The lifecycle/back-buffer thread (runtime-side) is irrelevant
  here.

## Known caveats

- **First-frame init is slow.** ARCore VIO needs ~1 second of
  motion to initialize. Until then, the cached pose is
  zero/identity. Clients see no head movement during this
  warm-up.
- **Translation drift after long sessions.** ARCore's VIO drifts
  a few cm/min in featureless environments. Not a module bug;
  intrinsic to monocular VIO.
- **Display-geometry warning spam.** ARCore logs
  `view_manager_utils.cc:133] Display geometry has an invalid
  width: 0` repeatedly. Not a problem; we do not give ARCore a
  render surface.
- **JNI-attach warnings on internal threads.** See
  [Known Issues](Known-Issues.md). Benign.

## Building

```
.\gradlew :samples:augins-arcore-headpose:packageArcoreHeadposeAugins
.\gradlew :samples:augins-arcore-headpose:installArcoreHeadposeAugins
```

## Verification

```
adb logcat -s "AugIns.ARCore:*"
```

Expected sequence on first client connect:

```
AugIns.ARCore: aug_onModuleLoad: host API v2 accepted
AugIns.ARCore: aug_onConnect: ARCore worker started
AugIns.ARCore: aug_spaceCreateSemanticIds: view=1 local=3 local_floor=4 stage=5 unbounded=6
AugIns.ARCore: ARCore session started
AugIns.ARCore: ARCore intrinsics: fx=... fy=... cx=... cy=... (640x480)
```

Visual: any OpenXR app's rendered scene tracks 6DoF as you move
the phone (translation + rotation), not just rotation.
