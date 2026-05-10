<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Building The Runtime

Build the Augmented Insanity runtime APK from source. This page covers
the toolchain, dependencies, and the Gradle invocation.

## Prerequisites

Tested on Windows 11 with PowerShell, Linux, and macOS. Versions are
tested-known-good; newer should work but is not guaranteed.

| Tool | Version |
|------|---------|
| Android Studio | Hedgehog (2023.1) or newer |
| Android SDK Platform | 35 |
| Android Build Tools | 34.0.0 |
| Android NDK | 26.3.11579264 |
| CMake (via Android Studio's SDK Manager) | 3.22.1 |
| Java JDK | 17 (Android Studio bundles Temurin 17) |
| Git | 2.30+ (the build calls `git describe` for the version code) |
| Eigen3 | 3.4 (installed manually OR auto-downloaded by Gradle) |
| Python | 3.8+ (PowerShell users on Windows: avoid the WindowsApps shim) |

For the Mercury hand-tracking module specifically, you also need the
external native dependencies in `xr-deps/`. See "External dependencies"
below.

## Clone

```
git clone https://github.com/yepistream/Augmented-Insanity.git
cd Augmented-Insanity
```

## External dependencies (Mercury only)

The Mercury hand-tracking module pulls in OpenCV (Android SDK), ONNX
Runtime (Android AAR), and two ONNX model files. These are not
committed to the repo. Fetch them once with:

```
# Windows (PowerShell)
.\scripts\fetch-xr-deps.ps1

# Linux/macOS
bash ./scripts/fetch-xr-deps.sh
```

The script reads pinned versions from `xr-deps-versions.json` at the
repo root and extracts everything under `xr-deps/`. Approximate
download sizes:

- OpenCV Android SDK 4.13.0: ~250 MB
- ONNX Runtime Android AAR 1.17.0: ~30 MB
- Hand-tracking ONNX models: ~12 MB

If you keep these dependencies elsewhere on disk (e.g. a shared
location across multiple checkouts), pass the paths to Gradle:

```
.\gradlew :src:xrt:targets:openxr_android:assembleOutOfProcessDebug `
    -PopencvDir=C:\path\to\OpenCV-android-sdk\sdk\native\jni\abi-arm64-v8a `
    -PonnxRoot=C:\path\to\onnxruntime-root `
    -PhandTrackingModelsDir=C:\path\to\hand-tracking-models
```

If you do NOT need Mercury, you can skip this step entirely. The
runtime APK and the other sample modules build without these deps;
the Mercury module just will not be packaged.

## Eigen3 (runtime always needs this)

Two options:

1. **Manual install.** Install Eigen3 (any 3.4.x), then point Gradle
   at it via `local.properties` at the repo root:

   ```
   eigenCMakeDir=C:\\path\\to\\eigen3\\cmake
   ```

2. **Auto-download.** Leave `eigenCMakeDir` unset. Gradle's
   `downloadEigen` task fetches Eigen on first build. Slower, but
   zero configuration.

## Build

The runtime APK ships in two product flavors. The interesting one for
Aug-Ins is `outOfProcess` (out-of-process service architecture; the
Aug-Ins module loader is enabled).

```
.\gradlew :src:xrt:targets:openxr_android:assembleOutOfProcessDebug
```

Output:
`src\xrt\targets\openxr_android\build\outputs\apk\outOfProcess\debug\openxr_android-outOfProcess-debug.apk`.

For a release build:

```
.\gradlew :src:xrt:targets:openxr_android:assembleOutOfProcessRelease
```

Release builds require a configured signing key. See the
"Signing release artifacts" comment in
`src\xrt\targets\openxr_android\build.gradle` for the
`MONADO_KEYSTORE_PROPERTIES` env-var protocol (carried over from
upstream Monado).

## Install on device

```
adb install -r src\xrt\targets\openxr_android\build\outputs\apk\outOfProcess\debug\openxr_android-outOfProcess-debug.apk
```

The first time, also launch the runtime activity once so it
self-registers as the active OpenXR runtime through Khronos's
runtime broker:

```
adb shell am start -n com.augmented_insanity.runtime.out_of_process/com.augmented_insanity.runtime.AugInsTestActivity
```

## Building a sample module

Sample modules are independent Gradle subprojects. Build any one
with:

```
.\gradlew :samples:augins-noop:packageNoopAugins
.\gradlew :samples:augins-arcore-headpose:packageArcoreHeadposeAugins
.\gradlew :samples:augins-mercury-handtracking-arcore:packageMercuryArcoreAugins
.\gradlew :module-example:augins-head-sway:packageHeadSwayAugins
```

Outputs: `samples/<name>/build/<name>.augins` (or
`module-example/<name>/build/<name>.augins`).

## Push a module to a device

Each sample's `build.gradle` defines a corresponding `install*Augins`
task that runs the build and `adb push`'s the result. Equivalent to
the manual sequence in [README.md](../../README.md), automated:

```
.\gradlew :samples:augins-arcore-headpose:installArcoreHeadposeAugins
adb shell am force-stop com.augmented_insanity.runtime.out_of_process
```

## Build flags reference

| Flag | Default | Purpose |
|------|---------|---------|
| `XRT_FEATURE_AUG_INS=ON` | auto-on for `outOfProcess` | Enables the Aug-Ins module loader. |
| `XRT_MODULE_MERCURY_HANDTRACKING=ON` | depends on `xr-deps` | Builds the Mercury hand-tracking native libs into the runtime so the Mercury `.augins` module's CMake target can link them. |
| `-PopencvDir=<path>` | `xr-deps/OpenCV-android-sdk/...` | OpenCV CMake config dir. |
| `-PonnxRoot=<path>` | `xr-deps/onnxruntime-root` | ONNX Runtime root (with `include/` and `lib/`). |
| `-PhandTrackingModelsDir=<path>` | `xr-deps/hand-tracking-models` | Where the .onnx files live. |

## Troubleshooting

- **"Cannot locate Android SDK"** -- set `sdk.dir` in
  `local.properties` at the repo root, or set the `ANDROID_SDK_ROOT`
  environment variable.
- **"Cannot find ndk-build"** -- the NDK version is wrong. Check
  `rootProject.ext.ndk_version` and install that exact NDK via SDK
  Manager.
- **"OpenCV_DIR is not set or is invalid"** -- run
  `scripts/fetch-xr-deps.ps1` (or `.sh`), or pass `-PopencvDir`.
- **Build hangs at first run** -- Gradle is downloading dependencies
  (Eigen, AGP). Subsequent builds are faster.
- **Path mangling on Windows Git Bash** -- if `adb push
  /data/local/tmp/...` rewrites the path to a Windows path, use
  PowerShell instead. The `samples/*/build.gradle` install tasks
  invoke adb correctly under PowerShell.
