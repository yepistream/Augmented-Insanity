<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Acknowledgements

Augmented Insanity is built on other people's work. This page lists
the load-bearing upstream projects and vendored libraries.

## Upstream runtime: Monado

Most of the C/C++ in this repository is Monado, unchanged or
lightly modified. The compositor, the IPC infrastructure, the
OpenXR state tracker, every driver that did not need v0.2-specific
edits, the build system, the CMake macros, the original
`AndroidManifest.xml` layout for the out-of-process runtime -- all
Monado.

- Project: <https://gitlab.freedesktop.org/monado/monado>
- Website: <https://monado.freedesktop.org/>
- Primary maintainer: [Collabora](https://www.collabora.com/)
- License: BSL-1.0 (preserved on every upstream file in this fork)

Monado contributors whose names recur in the files modified for
this fork include Jakob Bornecrantz, Ryan Pavlik, Christoph Haag,
Moshi Turner, Daniel Willmott, Jarvis Huang, Lubosz Sarnecki, and
Rylie Pavlik. The complete contributor list is in Monado's git
history.

## OpenXR specification and loader: Khronos

The OpenXR API, the headers under `src/external/openxr_includes`,
and the loader an XR app talks to are all defined and shipped by
the Khronos Group's OpenXR Working Group.

- Specification: <https://www.khronos.org/openxr/>
- SDK and headers: <https://github.com/KhronosGroup/OpenXR-SDK>
- Loader: <https://github.com/KhronosGroup/OpenXR-SDK-Source>

## Head pose: Google ARCore

The bundled ARCore head-pose module
(`samples/augins-arcore-headpose/`) wraps the ARCore C SDK.
ARCore provides the 6DoF tracking that Monado's default sensor
fusion (gyro + accel only) cannot.

- Documentation: <https://developers.google.com/ar>
- SDK: <https://github.com/google-ar/arcore-android-sdk>

The prebuilt `libarcore_sdk_c.so` shipped in
`samples/augins-arcore-headpose/vendor/arm64-v8a/` is redistributed
under the ARCore SDK's terms (see `LICENSE-arcore` in the same
directory).

## Vendored single-file libraries

- **miniz** by Rich Geldreich. Used by the loader to extract
  `.augins` zips at runtime.
  <https://github.com/richgel999/miniz>
- **cJSON** by Dave Gamble. Used by the manifest parser to read
  `metadata.json`.
  <https://github.com/DaveGamble/cJSON>

Both are vendored under `src/external/`; their licenses are
preserved.

## Other Monado-side dependencies (build-time)

Pulled in by upstream Monado's CMake; this fork inherits them
unchanged.

- **Eigen** -- linear algebra:
  <https://eigen.tuxfamily.org/>
- **Google Cardboard SDK** -- stereo distortion shaders used by the
  on-phone compositor:
  <https://github.com/googlevr/cardboard>
- **OpenXR loader** -- bundled by the Khronos OpenXR-SDK
  distribution (see above).

For the complete list of Monado's build-time third-party libraries
(Vulkan SDK, libusb, hidapi, etc.) see Monado's own README and the
`src/external/` directory.

## Standards and references

- **Android NDK and Android Open Source Project** --
  <https://developer.android.com/ndk> and
  <https://source.android.com/>
- **OpenGL ES, EGL** -- Khronos
- **Vulkan** -- Khronos

## Attribution

Every source file carries an SPDX license header. Monado-derived
files preserve the original Collabora (or other upstream)
copyright line alongside any Aug-Ins additions, per the BSL-1.0
attribution requirement. New Aug-Ins files are GPL-3.0-or-later.
The combined work is distributable under GPL-3.0-or-later; the
BSL-1.0 portions remain individually distributable under BSL-1.0.

`MODIFICATIONS.md` at the repo root lists per-subsystem changes
versus upstream Monado.
