<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Building A Module

How to write, build, and package a `.augins` module from scratch.

For a worked example with full source code, read
[Module Example Walkthrough](Module-Example-Walkthrough.md) and look
at `module-example/augins-head-sway/`.

## Decide on the build mode

Two patterns, depending on what your module needs:

### Pattern A: standalone NDK CMake (preferred)

Use this when the module's only dependencies are:

- The Android NDK (libc, liblog, libandroid).
- The Aug-Ins module ABI header (`augins_module_abi.h`) and the
  Monado `xrt/xrt_defines.h`.
- Optionally one or two vendored third-party `.so` files (e.g.
  the ARCore SDK's `libarcore_sdk_c.so`).

The module builds **outside** the main Aug-Ins CMake invocation, in
its own subdirectory, with its own CMakeLists.txt. The wrapping
`build.gradle` invokes `cmake` and `ninja` directly via `Exec`
tasks.

Reference templates:
- `samples/augins-noop/` -- minimum (no Monado headers needed).
- `samples/augins-arcore-headpose/` -- adds vendored SDK and Monado
  headers.
- `module-example/augins-head-sway/` -- the smallest module that
  uses `xrt_defines.h` and `augins_module_abi.h`.

### Pattern B: in-tree native target

Use this when the module needs to link against runtime-side libraries
that are themselves part of the Monado tree. The classic case is
Mercury hand tracking, which links against `t_ht_sync_mercury` and
its OpenCV/ONNX support libraries.

The module's `.so` is added as an `add_subdirectory(...)` from the
runtime's top-level CMakeLists.txt and built alongside
`augins-service.so`. The module's `build.gradle` is a thin Zip task
that picks the stripped `.so` out of the runtime's
`intermediates/stripped_native_libs/` and packages it.

Reference template:
- `samples/augins-mercury-handtracking-arcore/` -- complete in-tree
  example with OpenCV+ONNX linkage.

Pattern B is more invasive (the runtime build must know about your
module) but avoids vendoring large libraries inside the `.augins`
zip.

## Manifest

Every module ships a `metadata.json` at the root of the `.augins`
zip. See [Manifest Schema](Manifest-Schema.md) for the full
reference. Minimum:

```json
{
    "Name": "Your Module",
    "ID": "com.example.your_module",
    "Version": "0.1.0",
    "Description": "What your module does, in one paragraph.",
    "Implemented_Functions": []
}
```

If your module hooks IPC dispatches, list their names in
`Implemented_Functions`. If your module advertises an OpenXR feature
that needs a stub xdev, add the relevant
`Advertised_OpenXR_Features.SystemPropertyBits` entry.

## Source

Module source is one or more C++ files. The minimum lifecycle is
just `aug_onModuleLoad`:

```cpp
// my_module.cpp
#include "augins_module_abi.h"
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "MyModule", __VA_ARGS__)

static const struct aug_host_api *g_host = nullptr;

extern "C" void
aug_onModuleLoad(void *args)
{
    const auto *api = static_cast<const struct aug_host_api *>(args);
    if (api == nullptr || api->version < 1u) return;
    g_host = api;
    LOGI("MyModule loaded; host API v%u", api->version);
}
```

Hooks are `extern "C"` symbols whose names match
`Implemented_Functions`. Each takes the four-pointer signature:

```cpp
extern "C" int32_t
xrLocateSpace(void *ics, void *msg, void *reply, void *unused)
{
    // read msg, write reply via g_host helpers, return AUG_OK
    return AUG_OK;
}
```

See [IPC Hook Dispatch](IPC-Hook-Dispatch.md) for the dispatch
calling convention.

## CMakeLists.txt (Pattern A)

The output `.so` MUST be named exactly `<your-module-id>.so`, with no
`lib` prefix, because the loader does `dlopen("<id>.so", ...)`. Use
`set_target_properties`:

```cmake
cmake_minimum_required(VERSION 3.22)
project(my_module LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# Reach into the runtime tree for the module ABI and Monado headers.
set(AUG_RUNTIME_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/xrt/augins")
set(XRT_PUBLIC_INCLUDE  "${CMAKE_CURRENT_SOURCE_DIR}/../../src/xrt/include")

add_library(my_module SHARED my_module.cpp)
target_include_directories(my_module PRIVATE
    ${AUG_RUNTIME_INCLUDE}
    ${XRT_PUBLIC_INCLUDE}
    )
target_link_libraries(my_module PRIVATE android log)

set_target_properties(my_module PROPERTIES
    PREFIX ""
    OUTPUT_NAME "com.example.my_module"
    SUFFIX ".so"
    )
```

## build.gradle (Pattern A)

A thin wrapper that runs `cmake` and `ninja` via Exec tasks, then
zips the result. Copy `module-example/augins-head-sway/build.gradle`
verbatim, search-and-replace the module name and ID.

The interesting tasks are:

- `configure<Name>` -- runs `cmake -G Ninja ...`.
- `build<Name>` -- runs `cmake --build ...`.
- `package<Name>Augins` -- Zip task; produces
  `build/<name>.augins`.
- `install<Name>Augins` -- builds + adb-pushes onto a device via
  `run-as`.

## settings.gradle

Add your module subproject to the root `settings.gradle`:

```groovy
include ':path:to:my-module'
project(':path:to:my-module').projectDir = new File(rootDir, 'path/to/my-module')
```

After this, your tasks are reachable from the repo root:

```
.\gradlew :path:to:my-module:packageMyModuleAugins
.\gradlew :path:to:my-module:installMyModuleAugins
```

## Build and verify

```
.\gradlew :path:to:my-module:packageMyModuleAugins
ls path/to/my-module/build/my-module.augins
```

Inspect the zip:

```powershell
Add-Type -AssemblyName System.IO.Compression.FileSystem
$arc = [System.IO.Compression.ZipFile]::OpenRead("path\to\my-module\build\my-module.augins")
$arc.Entries | Format-Table FullName, Length
$arc.Dispose()
```

Expected entries: `<module-id>.so`, `metadata.json`, optionally
`settings.json`, `LICENSE.txt`, vendored `.so` siblings, etc.

## Install on device

```
.\gradlew :path:to:my-module:installMyModuleAugins
adb shell am force-stop com.augmented_insanity.runtime.out_of_process
```

Watch logs:

```
adb logcat -s "Aug-Ins:*" "MyModule:*"
```

You should see:

```
Aug-Ins: Loaded module: <Your Module> (id=com.example.your_module, prio=50)
MyModule: aug_onModuleLoad called; host API v2
```

## Common pitfalls

- **`.so` filename has `lib` prefix.** The loader will `dlopen` the
  expected name and fail. Set `PREFIX ""` in CMakeLists.
- **`.so` filename does not match the manifest `ID`.** Same
  failure mode. The two must be string-equal.
- **Hook symbol not exported.** Wrap in `extern "C"` (a hook in
  C++-mangled form is invisible to `dlsym`).
- **Hook listed in manifest but not implemented in source.** The
  loader logs a warning and skips it.
- **Module advertises a system bit but never registers a producer
  callback.** Stub xdev appears, every query returns `is_active =
  false`. Either register the callback or drop the bit from the
  manifest.
- **Reading `get_module_data_dir()` from a worker thread.**
  Returns empty string. Capture in `aug_onModuleLoad` and store.
