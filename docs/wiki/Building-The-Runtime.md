<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Building The Runtime

Builds the runtime APK
(`openxr_android-outOfProcess-debug.apk`) containing the
out-of-process service and the client-side `libopenxr_monado.so`
that XR apps load via the Khronos broker.

For building a `.augins` module instead, see
[Building-A-Module](Building-A-Module.md).

## Prerequisites

- Android Studio (2024+ release). The project ships a `gradlew`
  wrapper, so the IDE is only needed for the SDK Manager.
- Android SDK with:
  - Platform `android-31` (Android 12; matches `compileSdkVersion`).
  - NDK `26.3.11579264`.
  - CMake `3.22.1`.
  - Build-tools 33+.
- Java 17 on PATH.
- arm64-v8a target. The project does not build for armv7, x86, or
  x86_64.
- Eigen development headers on the host. On Windows with MSYS2:
  `mingw-w64-x86_64-eigen3` (Gradle finds it at
  `D:\msys64\mingw64\share\eigen3\cmake`). Linux: `libeigen3-dev`.

Versions are sourced from `build.gradle` (the root project's
`ext` block).

## `local.properties`

Gradle reads `local.properties` at the repo root for paths:

```
sdk.dir=C:\\Users\\<you>\\AppData\\Local\\Android\\Sdk
```

File is gitignored. If absent, Gradle falls back to the
`ANDROID_SDK_ROOT` or `ANDROID_HOME` environment variable.

## Build

From the repo root:

```
.\gradlew.bat :src:xrt:targets:openxr_android:assembleOutOfProcessDebug
```

(Linux / macOS: `./gradlew :src:xrt:targets:openxr_android:assembleOutOfProcessDebug`.)

First build takes 3-5 minutes (configures CMake, builds Monado +
ipc + augins-service + openxr_monado in Release). Subsequent
builds finish in seconds via Gradle's up-to-date checks.

Artifact path:

```
src/xrt/targets/openxr_android/build/outputs/apk/outOfProcess/debug/openxr_android-outOfProcess-debug.apk
```

Install to the device after every build:

```
adb install -r src\xrt\targets\openxr_android\build\outputs\apk\outOfProcess\debug\openxr_android-outOfProcess-debug.apk
```

The Khronos OpenXR broker picks the runtime up via the
`org.khronos.openxr.OpenXRRuntimeService` intent.

## Spurious "fatal: not a git repository" line

Gradle's configure-time scripts invoke `git rev-parse` for
version-stamping. On some clones the subprocess emits:

```
fatal: not a git repository (or any of the parent directories): .git
```

This is not a build failure. If the overall Gradle task ends
with `BUILD SUCCESSFUL`, the APK is good.

## Build flavors

- `outOfProcessDebug` -- the only flavor used in this fork.
  Builds the service plus the `libopenxr_monado.so` for clients.
- `inProcessDebug` -- legacy upstream Monado flavor; runtime
  loaded directly into the XR app process. Not exercised here.

## Starting the runtime

Two ways to start it after install:

1. Launch any OpenXR client app. The Khronos broker resolves
   `OpenXRRuntimeService` to the augmented-insanity runtime,
   binds, and the service starts.
2. Launch the bundled `AugInsTestActivity` and tap "Start GRS".
   This uses `startForegroundService`, which works even when the
   app is in background.

Direct command-line start that bypasses Android 12+ background
restrictions:

```
adb shell am start-foreground-service \
  -a org.freedesktop.monado.ipc.CONNECT \
  -n com.augmented_insanity.runtime.out_of_process/org.freedesktop.monado.ipc.MonadoService
```

## Observing module load on the device

```
adb logcat -s "Aug-Ins.Loader:V" "Aug-Ins.Lifecycle:V" \
                 "Aug-Ins.Dispatch:V" "Aug-Ins.HostApi:V" "Aug-Ins.Adapters:V"
```

With a module installed, the runtime start emits roughly:

```
Aug-Ins.HostApi:   host API JVM/Context bound
Aug-Ins.Loader:    init: scanning ...
Aug-Ins.Loader:    init: found N .augins file(s)
Aug-Ins.Loader:    preloaded sibling: libfoo.so   (if any)
Aug-Ins.Loader:    loaded module 'com.x.y.z' v0.2.0 (priority=100, K/K functions resolved)
Aug-Ins.Lifecycle: module 'com.x.y.z': aug_on_module_load OK
Aug-Ins.Dispatch:  dispatch registry sorted: N function name(s) total
```

## Clean build

```
.\gradlew.bat clean
```

The CMake build cache lives under
`src/xrt/targets/openxr_android/.cxx/Debug/`. `gradlew clean`
wipes it; the next build re-configures CMake from scratch (3-5
minutes again).

## Troubleshooting

- `Cannot locate Android SDK` -- set `sdk.dir` in
  `local.properties` or `ANDROID_SDK_ROOT` in the environment.
- `NDK 26.3.11579264 not found` -- install the exact version via
  Android Studio's SDK Manager (SDK Tools tab; enable "show
  package details").
- CMake error mentioning Eigen -- install Eigen3 development
  headers. The CMake check is in
  `src/xrt/auxiliary/CMakeLists.txt`.
- Build hangs on first invocation -- dependency download. Check
  the network. Subsequent builds are local.
- PowerShell `Tee-Object` writes UTF-16-LE logs that downstream
  greps miss. Use `... 2>&1 | Out-File -Encoding utf8 build.log`
  instead.
