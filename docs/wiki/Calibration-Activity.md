<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Calibration Activity `[NOT IMPLEMENTED]`

A planned per-module camera-intrinsics calibration UI shipped inside
the runtime APK. Not yet implemented; this page documents the
intended design so contributors do not have to reverse-engineer it
from the Roadmap.

## Why

Today, the Mercury hand-tracking module reads camera intrinsics from
ARCore at first frame. ARCore's intrinsics are reasonable for a phone
camera at the resolution ARCore renders at, but they are not as good
as a proper chessboard calibration with the actual capture pipeline.
Quality of the hand-tracking ONNX inference is sensitive to
intrinsics accuracy; better calibration produces noticeably better
joints.

For the future Mercury USB-camera module, no calibration source
exists at all -- the user must produce their own. A calibration UI is
the standard answer.

## Planned design

A single Android Activity in the runtime APK,
`com.augmented_insanity.runtime.HandCameraCalibrationActivity`,
launched from the main `AugInsTestActivity` via a "Calibrate hand
tracking camera" button.

Auto-detects which Mercury module is installed
(`com.augmented_insanity.samples.mercury_handtracking_arcore` or, when
it lands, the USB variant) and routes to the appropriate flow:

### ARCore variant

- Stop the runtime service (or warn the user to do so).
- Open an `ArSession` from the Activity itself.
- Capture intrinsics directly via `ArCamera_getImageIntrinsics`,
  augmented with chessboard refinement frames if the user wants
  better-than-stock intrinsics.
- Write `hand_camera.json` into the runtime's private data dir.
- Restart the runtime service.

### USB variant

- Open the USB camera via the runtime's USB device helper
  (planned as a host API addition, see [Roadmap](Roadmap.md)).
- Run a chessboard capture flow (10-20 frames, OpenCV pose
  estimation, intrinsics + distortion).
- Write `hand_camera.json` into the runtime's private data dir.

### Output format

```json
{
    "version": 1,
    "module_id": "com.augmented_insanity.samples.mercury_handtracking_arcore",
    "captured_at": "2026-...",
    "image_size": { "width": 640, "height": 480 },
    "intrinsics": {
        "fx": 461.7, "fy": 459.6,
        "cx": 325.7, "cy": 239.3
    },
    "distortion_model": "OPENCV_RADTAN_5",
    "distortion": [0.0, 0.0, 0.0, 0.0, 0.0]
}
```

The Mercury module reads this JSON at startup if present and falls
back to the broker-provided ARCore intrinsics otherwise.

## Why not a module-bundled UI

The user's preferred long-term answer is "modules ship their own UIs
via DEX-loading" (see [Roadmap](Roadmap.md)). When that lands, the
Mercury module would ship its own calibration Fragment and the
runtime APK's `ModuleHostActivity` would render it. At that point,
this Activity in the runtime APK becomes redundant and gets removed.

Until DEX-loading is in, the calibration UI lives in the runtime
APK as a one-off built-in.

## Reference: `HandCameraIntrinsicsNative.kt`

The user's other Monado fork
(`D:\Repos\Private\FullAndroidOpen-XR\HT_Mercury\monadophonecam_non\monado`)
contains a working version of this UI under
`HandCameraIntrinsicsNative.kt`. When this Activity is implemented,
that file is the starting point.
