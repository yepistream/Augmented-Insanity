<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Mercury Module Reference `[WIP]`

`samples/augins-mercury-handtracking-arcore/` -- monocular hand
tracking via Collabora's Mercury inference pipeline (two ONNX
models running on the CPU). Consumes camera frames from the
ARCore head-pose module's broker; produces hand joints into the
runtime's stub hand-tracker xdev.

## What it does

- On `aug_onModuleLoad`, captures the per-module data dir (where
  the .augins zip extracted its ONNX models) for use later.
- On `aug_onConnect`, registers a hand-tracker producer callback
  with the host API. **Does not** subscribe to camera frames yet.
  Lazy: the first time a client actually queries hand joints
  (`xrLocateHandJointsEXT`), the producer callback runs and that
  is when the module subscribes to the camera-frame broker. This
  prevents Mercury from spinning up on every client connect even
  when the client does not use hand tracking.
- On the first received camera frame, builds a
  `t_stereo_camera_calibration` from the broker-published
  intrinsics (or from a calibration JSON in the data dir if
  present), then calls `ht_device_create` to instantiate the
  Mercury inference pipeline.
- Decimates incoming camera frames to ~10 Hz
  (`kDecimateEveryN = 3` against ARCore's ~30 Hz). Mercury at
  10 Hz is plenty for UI interaction; running at 30 Hz starves
  ARCore VIO.
- For each delivered frame, wraps the Y plane in an `xrt_frame`
  and pushes it into Mercury's async sync sink. Mercury's
  background worker thread (running at `nice +10`) does the
  ONNX inference.
- When the runtime's stub hand-tracker xdev is queried for
  joints, the registered producer callback dispatches into
  `xrt_device_get_hand_tracking` on the Mercury xdev, which
  returns the latest cached joint set.

## Why this module exists

Hand tracking is a high-value capability that does not require
dedicated hardware on a phone. The Mercury inference pipeline is
upstream Monado code; this module is the glue that lets it run
inside an Aug-Ins runtime, consuming ARCore camera frames, and
exposes the results through the standard `XR_EXT_hand_tracking`
OpenXR extension.

## File layout

```
samples/augins-mercury-handtracking-arcore/
    metadata.json
    settings.json
    LICENSE-mercury, LICENSE-onnx, LICENSE-opencv
    mercury_arcore_module.cpp                the Aug-Ins glue
    CMakeLists.txt                           in-tree CMake (Pattern B)
    build.gradle                             thin Gradle Zip task
```

The actual Mercury+OpenCV+ONNX native code lives in the runtime's
build tree (`src/xrt/tracking/hand/mercury/` and friends), built
into the runtime APK behind `XRT_MODULE_MERCURY_HANDTRACKING=ON`.
The module's `.so` links against those runtime-side libraries.

The ONNX models (`grayscale_detection_160x160.onnx`,
`grayscale_keypoint_jan18.onnx`, ~12 MB combined) are bundled
inside the `.augins` zip, NOT inside the runtime APK.

## Manifest

```json
{
    "Name": "Aug-Ins Mercury Hand Tracking (ARCore camera)",
    "ID": "com.augmented_insanity.samples.mercury_handtracking_arcore",
    "Dependencies": [
        "com.augmented_insanity.samples.arcore_headpose"
    ],
    "Advertised_OpenXR_Features": {
        "Extensions": ["XR_EXT_hand_tracking"],
        "SystemPropertyBits": ["handTracking"]
    },
    "Implemented_Functions": []
}
```

Notable:

- `Dependencies` requires `arcore_headpose` -- without it, no
  camera-frame producer exists.
- `SystemPropertyBits: ["handTracking"]` triggers the runtime
  stub-xdev factory to create the hand-tracker xdev.
- `Implemented_Functions: []` -- this module does not intercept
  any IPC dispatches. It is purely a producer.

## Interaction with the runtime

Three integration points:

- **Camera-frame broker subscriber.**
  `g_host->subscribe_camera_frame(on_camera_frame, nullptr)` --
  registered lazily on first hand-joints query.
- **Hand-tracker producer callback.**
  `g_host->register_hand_tracker(joints_cb, nullptr)` --
  registered eagerly in `aug_onConnect`.
- **Per-module data dir.**
  `g_host->get_module_data_dir()` -- captured in
  `aug_onModuleLoad`, used to find bundled ONNX models. Saved to
  a module global so subsequent worker-thread reads do not need
  to call the host API (which only works during lifecycle
  dispatch).

## Threading

- Camera-frame callback runs on the producer's thread (the
  arcore-headpose worker). Must be fast: copy Y plane into a new
  `xrt_frame`, push to Mercury async sink, return.
- Mercury async worker thread runs ONNX inference. Niced to +10
  via `setpriority(PRIO_PROCESS, 0, 10)` early in its main loop
  (patched in `src/xrt/tracking/hand/t_hand_tracking_async.c`, still has problems when tested on Android, as Android dosen't give permissions to allow it, or I'm not cleaver enough).
  At nice +10 the kernel scheduler treats Mercury as background
  work; ARCore's VIO threads (default nice 0) preempt it.
- Joints producer callback runs on the IPC server's per-client
  thread. Reads the latest cached joint set; returns immediately.

## Known caveats

- **First-init briefly fights ARCore.** First Mercury frame
  triggers ONNX model loading + TensorFlow Lite XNNPack
  delegate spin-up. CPU-heavy. ARCore VIO may emit one or two
  "FeatureExtraction taking too long" warnings while this
  settles.
- **Calibration via JSON only.** No UI yet. The module reads
  `hand_camera.json` from its data dir if present; otherwise
  falls back to ARCore's published intrinsics. See
  [Calibration Activity](Calibration-Activity.md).
- **Mono only.** Mercury's stereo path exists upstream but the
  module is wired for the mono case. Adding stereo would require
  a second camera source (which Android phones do not generally
  have) or a stereo USB rig (the Mercury USB module on the
  roadmap will cover that).
- **Not real-time.** 10 Hz inference. Adequate for hand-UI
  interaction; not suitable for finger-precise applications.

## Building

Requires the external dependencies in `xr-deps/`. Run
`scripts/fetch-xr-deps.ps1` (or `.sh`) once. Then:

```
.\gradlew :samples:augins-mercury-handtracking-arcore:packageMercuryArcoreAugins
.\gradlew :samples:augins-mercury-handtracking-arcore:installMercuryArcoreAugins
```

## Verification

```
adb logcat -s "AugIns.MercuryARCore:*" "AugIns.ARCore:*"
```

Expected sequence on first hand-joints query:

```
AugIns.MercuryARCore: aug_onModuleLoad: cached models dir = /data/.../mercury-handtracking-arcore/models
AugIns.MercuryARCore: aug_onModuleLoad: host API v2 accepted
AugIns.MercuryARCore: aug_onConnect: hand-tracker callback registered (camera subscription deferred)
AugIns.MercuryARCore: joints_cb: first call -- subscribed to camera-frame broker; Mercury will spin up on next ARCore frame
AugIns.MercuryARCore: Mercury ready: xdev=... (mono, 640x480, fx=... fy=... cx=... cy=...)
```

Visual: launch a hand-tracking-aware OpenXR app (e.g. the Godot
OpenXR Hand Tracking demo). Hands should render and follow your
hands in front of the camera within ~1 second of the app
starting to query joints.
