<!--
Copyright 2018-2021, Collabora, Ltd.
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Augmented Insanity

A modular, debuggable, out-of-process OpenXR runtime for Android. Forked from
[Monado](https://gitlab.freedesktop.org/monado/monado).

Capabilities are provided by `.augins` modules loaded at runtime: drop a module
into the runtime's modules directory and the runtime picks it up on its next
start without recompilation. Currently shipped modules cover ARCore-based 6DoF
head pose and ONNX-based hand tracking; the runtime remains generic and the
module system supports arbitrary future capabilities.

> Status: work in progress. Public modules and the runtime are usable on
> ARCore-supported Android phones, but a number of subsystems (calibration UI,
> USB-camera hand tracking, multi-process module isolation, etc.) are not yet
> implemented or are still being stabilised. See
> [docs/wiki/Implementation-Status.md](docs/wiki/Implementation-Status.md) for
> the current state.

---

## For users

### Supported devices

- Android 12 or newer.
- ARCore-supported phone (see
  [Google's compatibility list](https://developers.google.com/ar/devices)) if
  you intend to use the ARCore head-pose or ARCore-camera hand-tracking
  modules.
- arm64-v8a only (no 32-bit or x86 builds).

### Install the runtime

1. Download `augmented-insanity-runtime-<version>.apk` from the
   [Releases page](https://github.com/yepistream/Augmented-Insanity/releases)
   and install it via `adb install -r <apk>` or by opening the file on the
   device.
2. Launch the bundled "Augmented Insanity" activity once. It will request the
   relevant permissions (camera, etc.) and register itself as the active
   OpenXR runtime through Khronos's runtime broker.

### Install a module

Modules are distributed as `.augins` zip archives. To install:

```
adb push <module>.augins /sdcard/Download/
adb shell run-as com.augmented_insanity.runtime.out_of_process \
    cp /sdcard/Download/<module>.augins files/modules/<module>.augins
adb shell am force-stop com.augmented_insanity.runtime.out_of_process
```

Restart the runtime activity (or any OpenXR client app) and the module will
be picked up. Verify with:

```
adb logcat -s "Aug-Ins:*" "AugIns.*:*"
```

You should see `Loaded module: <module name>` and module-specific log lines.

The shipped modules are also available as Release artifacts:

- `noop.augins` -- minimal lifecycle demonstration.
- `arcore-headpose.augins` -- 6DoF head pose via ARCore.
- `mercury-handtracking-arcore.augins` -- ONNX hand tracking via ARCore camera
  (depends on `arcore-headpose`).

To uninstall a module:

```
adb shell run-as com.augmented_insanity.runtime.out_of_process \
    rm files/modules/<module>.augins
adb shell am force-stop com.augmented_insanity.runtime.out_of_process
```

---

## For developers

### Architecture in one paragraph

Augmented Insanity preserves Monado's IPC bridge between OpenXR client
applications and the runtime service. On the service side a generic dispatcher
(`augins_fire_hooks`) intercepts every IPC call enumerated in
`src/xrt/ipc/shared/proto.py`'s `aug_ipc_to_xr` dictionary and fans out to
every loaded module that implements that hook. Modules can read or overwrite
the IPC reply before it is returned to the client. Modules optionally expose
producer callbacks (`register_hand_tracker`, `publish_camera_frame_y8`, ...)
through a versioned host-API table; the runtime fabricates stub `xrt_device`
instances for advertised capabilities and routes their queries to those
producer callbacks. See
[docs/wiki/Architecture-Overview.md](docs/wiki/Architecture-Overview.md) for
the full diagram and dispatch trace.

### Where to start

- **Want to write a module:** start with the
  [Module-Example-Walkthrough](docs/wiki/Module-Example-Walkthrough.md), which
  takes you through `module-example/augins-head-sway/` line by line. That
  example sways the rendered head pose left and right using nothing but the
  IPC hook mechanism -- no external SDKs.
- **Want to build the runtime:** see
  [Building-The-Runtime](docs/wiki/Building-The-Runtime.md). Android Studio,
  NDK 26, CMake 3.22, and one external dependency tree fetched by
  `scripts/fetch-xr-deps.ps1` (or `.sh`).
- **Want the deep dive:** [Architecture-Overview](docs/wiki/Architecture-Overview.md)
  -> [Module-System](docs/wiki/Module-System.md) ->
  [Manifest-Schema](docs/wiki/Manifest-Schema.md) ->
  [Host-API-Reference](docs/wiki/Host-API-Reference.md) ->
  [IPC-Hook-Dispatch](docs/wiki/IPC-Hook-Dispatch.md).
- **Want to know what works today:**
  [Implementation-Status](docs/wiki/Implementation-Status.md) and
  [Known-Issues](docs/wiki/Known-Issues.md).

The same files in `docs/wiki/` are mirrored into the
[GitHub Wiki](https://github.com/yepistream/Augmented-Insanity/wiki) for
browsing.

---

## Repository layout

```
README.md                  this file
LICENSE                    multi-license overview
LICENSES/                  per-license full text bodies
MODIFICATIONS.md           summary of changes vs upstream Monado, by subsystem
CONTRIBUTING.md            contribution rules
CODE_OF_CONDUCT.md
.github/                   issue and PR templates
docs/wiki/                 developer documentation (also mirrored to GitHub Wiki)
module-example/            featured tutorial module (head-sway)
samples/                   the three production / reference modules
scripts/                   fetch-xr-deps, relicense
src/                       runtime source (the Monado tree)
cmake/, tests/, doc/       upstream-Monado build infrastructure, preserved
```

---

## Acknowledgements

Augmented Insanity exists because Monado exists. Enormous thanks to the
[Monado team at Collabora](https://collabora.com/) and to every upstream
contributor: Jakob Bornecrantz, Rylie Pavlik, Moshi Turner, Korcan Hussein,
Pete Black, and many others. The IPC bridge, the OpenXR state tracker, the
compositor, the hand-tracking pipeline -- all of it is theirs. This fork only
adds a module loader and a few sample modules on top.

The Monado source preserved here remains under its original Boost Software
License 1.0; see `LICENSE` and `MODIFICATIONS.md` for details on what is new
in Augmented Insanity vs upstream.

Upstream Monado: https://gitlab.freedesktop.org/monado/monado.

---

## License

Dual-licensed: original Monado source under BSL-1.0; new Augmented Insanity
code and substantive modifications under GPL-3.0-or-later. See `LICENSE` for
the full statement and `LICENSES/` for the actual license texts. Each source
file's `SPDX-License-Identifier` header is the authoritative statement of
that file's license.

---

## Contact

Marko Kazimirovic <kazimirovicmarko@photon.me>

Issues and feature requests: please use the GitHub Issues for this repository.
