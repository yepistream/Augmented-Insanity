<!--
Copyright 2018-2021, Collabora, Ltd.
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Augmented Insanity

A modular, debuggable, out-of-process OpenXR runtime for Android.
Forked from [Monado](https://gitlab.freedesktop.org/monado/monado).

Capabilities are provided by `.augins` modules loaded at service
start. A module is a zip of native code plus a manifest; the
runtime discovers it, validates it, calls its lifecycle hooks,
and routes selected OpenXR IPC calls through it via hand-written
adapters. Modules run service-side, in the privileged runtime
process, alongside the compositor and Monado's tracking system.

> Status: `[WIP]` v0.2. The runtime, module loader, dispatch
> chain, and the ARCore head-pose module are working on
> ARCore-supported Android phones. A number of subsystems
> (calibration UI, USB-camera hand tracking, Mercury v0.2 rewrite,
> multi-process module isolation, etc.) are not yet implemented.
> See [docs/wiki/Implementation-Status.md](docs/wiki/Implementation-Status.md)
> for the current state.

---

## For users

### Supported devices

- Android 12 or newer.
- ARCore-supported phone (see [Google's compatibility list](https://developers.google.com/ar/devices))
  if intending to use the ARCore head-pose module.
- arm64-v8a only. No 32-bit or x86 builds.

### Install the runtime

1. Download `openxr_android-outOfProcess-debug.apk` from the
   [Releases page](https://github.com/yepistream/Augmented-Insanity/releases)
   and install via `adb install -r <apk>` or by opening the file
   on the device.
2. Open the `Augmented Insanity` activity once. It requests the
   camera permission and registers with Khronos's OpenXR runtime
   broker as the active runtime.

### Install a module

Modules are distributed as `.augins` zip archives. Push the file
into the runtime APK's private modules directory:

```
adb push <module>.augins /sdcard/Download/
adb shell run-as com.augmented_insanity.runtime.out_of_process \
    cp /sdcard/Download/<module>.augins files/modules/<module>.augins
```

Restart the runtime. The simplest way to do that:

```
adb shell am force-stop com.augmented_insanity.runtime.out_of_process
adb shell am start-foreground-service \
    -a org.freedesktop.monado.ipc.CONNECT \
    -n com.augmented_insanity.runtime.out_of_process/org.freedesktop.monado.ipc.MonadoService
```

Verify with logcat:

```
adb logcat -s "Aug-Ins.Loader:V" "Aug-Ins.Lifecycle:V"
```

The loader logs `loaded module '<id>' v<x.y.z>` for each module
it accepted.

### Available modules

| Module | Status | Description |
|--------|--------|-------------|
| `arcore-headpose.augins` | `[Working]` | 6DoF head pose via Google ARCore. |
| `head-sway.augins` | `[Working]` | Tutorial module. Sways head pose left and right by 0.3 m every 4 s. See [docs/wiki/Module-Example-Walkthrough.md](docs/wiki/Module-Example-Walkthrough.md). |
| `test-noop.augins` | `[Working]` | Verification module. Lifecycle hooks only, no dispatch. |
| `test-locate-space.augins` | `[Working]` | Verification module. Overrides `xrLocateSpace` with a sentinel pose. |
| `mercury-handtracking.augins` | `[Planned]` | ONNX hand tracking. v0.1 source archived; v0.2 rewrite scheduled for v0.2.1. |

To uninstall a module:

```
adb shell run-as com.augmented_insanity.runtime.out_of_process \
    rm files/modules/<module>.augins
```

---

## For developers

### Architecture

An XR client app talks to `libopenxr_monado.so` (loaded via the
Khronos OpenXR loader), which IPC-marshals OpenXR calls to the
runtime service process. On the service side, `proto.py`'s
codegen emits a dispatch fork for every IPC call listed in
`aug_implemented_adapters`: if any module is registered for the
mapped name, the call goes through `aug_adapter_<call>` (which
unpacks the IPC payload into OpenXR-shaped arguments, iterates
the registered modules in priority order, then repacks); else
it goes straight to the upstream `ipc_handle_<call>`.

Modules export real OpenXR functions (e.g. `xrLocateSpace` with
the exact `<openxr/openxr.h>` signature), or Aug-Ins synthetic
names (e.g. `aug_LocateDeviceInSpace` for the head-device-locate
half of `xrLocateViews`).

Full diagram and dispatch trace:
[docs/wiki/Architecture-Overview.md](docs/wiki/Architecture-Overview.md).

### Where to start

- To write a module: [Building-A-Module](docs/wiki/Building-A-Module.md).
  Smallest reference sample is `samples/augins-test-noop/`.
- To build the runtime: [Building-The-Runtime](docs/wiki/Building-The-Runtime.md).
  Android Studio with NDK `26.3.11579264`, CMake `3.22.1`,
  platform `android-31`.
- To add a new dispatchable function: [Service-Side-Dispatch](docs/wiki/Service-Side-Dispatch.md).
- To understand the host API a module receives: [Host-API-Reference](docs/wiki/Host-API-Reference.md).
- For the manifest schema: [Manifest-Schema](docs/wiki/Manifest-Schema.md).
- For current state: [Implementation-Status](docs/wiki/Implementation-Status.md)
  and [Known-Issues](docs/wiki/Known-Issues.md).

The files in `docs/wiki/` are mirrored to the
[GitHub Wiki](https://github.com/yepistream/Augmented-Insanity/wiki).

---

## Repository layout

```
README.md                  this file
LICENSE                    multi-license overview
LICENSES/                  per-license full text
MODIFICATIONS.md           changes vs upstream Monado, by subsystem
CONTRIBUTING.md            contribution rules
CODE_OF_CONDUCT.md
.github/                   issue + PR templates
docs/wiki/                 developer documentation (mirrored to GitHub Wiki)
samples/                   reference and production modules
  augins-arcore-headpose/    production: 6DoF head pose via ARCore
  augins-test-noop/          smallest module, lifecycle only
  augins-test-locate-space/  test override of xrLocateSpace
module-example/            tutorial modules
  augins-head-sway/          decorator-pattern walkthrough
scripts/                   relicense, fetch-xr-deps, upstream Monado utilities
src/                       runtime source (mostly upstream Monado)
  xrt/augins/                Aug-Ins module subsystem
  xrt/ipc/                   IPC client/server + proto.py codegen
cmake/, tests/, doc/       upstream Monado infrastructure, preserved
```

---

## Acknowledgements

Augmented Insanity exists because Monado exists. The IPC bridge,
the OpenXR state tracker, the compositor, the build system, the
Android-side runtime broker plumbing -- all upstream Monado, from
the team at [Collabora](https://www.collabora.com/) and the wider
contributor base at <https://gitlab.freedesktop.org/monado/monado>.
This fork adds a module loader, a small set of adapters, and the
sample modules on top.

Full list of upstream projects and vendored libraries:
[docs/wiki/Acknowledgements.md](docs/wiki/Acknowledgements.md).

---

## License

Dual-licensed. Original Monado source is preserved under BSL-1.0.
New Augmented Insanity code and substantive modifications are
GPL-3.0-or-later. Each source file's `SPDX-License-Identifier`
header is the authoritative statement for that file.

See `LICENSE` for the project-level overview, `LICENSES/` for the
full license texts, and `MODIFICATIONS.md` for a per-subsystem
summary of what changed versus upstream Monado.

---

## Contact

Marko Kazimirovic <kazimirovicmarko@photon.me>

Issues and feature requests: GitHub Issues on this repository.
