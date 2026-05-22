<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Building A Module

From-scratch guide to authoring a `.augins` module. Reference
samples in this repo:

- `samples/augins-test-noop/` -- smallest possible module,
  lifecycle hooks only, no dispatch.
- `samples/augins-test-locate-space/` -- one dispatched function.
- `samples/augins-arcore-headpose/` -- production module with a
  worker thread and a vendored SDK.

The module build is separate from the runtime build. Modules use
Gradle for orchestration (zip packaging, adb push, dependency
tracking) but invoke `cmake` and `ninja` directly via `Exec`
tasks. The Android Gradle Plugin's `lib*` `.so` naming would
break the loader's `dlopen("<ID>.so", ...)`.

## Prerequisites

- Android NDK `26.3.11579264` and CMake `3.22.1` (same as the
  runtime build).
- The runtime's `src/xrt/augins/module_abi.h` header. Module
  CMake adds `src/xrt/augins/` as an include directory.

## Directory layout

Convention used by the bundled samples:

```
samples/my-module/
  +-- build.gradle             # zip packaging + adb push
  +-- CMakeLists.txt           # cmake build of the module .so
  +-- metadata.json            # the manifest
  +-- settings.json            # (optional, reserved for future use)
  +-- my_module.cpp            # the module source
  +-- vendor/                  # any third-party SDKs / .so files
```

Plus a one-line entry in the root `settings.gradle`:

```groovy
include ':samples:my-module'
project(':samples:my-module').projectDir = new File(rootDir, 'samples/my-module')
```

## `metadata.json`

Required fields: `Manifest_Version`, `ID`, `Version`,
`Implemented_Functions`.

```json
{
    "Manifest_Version": 1,
    "ID": "com.example.modules.my_module",
    "Version": "0.1.0",
    "Implemented_Functions": []
}
```

An empty `Implemented_Functions` array is legal. The module
loads, its lifecycle hooks fire, no IPC dispatch routes through
it. This is the smallest configuration that exercises the loader.

Add OpenXR or `aug_*` names to `Implemented_Functions` as
functions are implemented. See [Manifest-Schema](Manifest-Schema.md)
for the full field reference.

## `CMakeLists.txt`

The minimum to produce a correctly-named `.so`:

```cmake
cmake_minimum_required(VERSION 3.22)
project(my-module LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Path to the v0.2 public module ABI header.
set(AUG_RUNTIME_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/xrt/augins")
set(OPENXR_INCLUDE      "${CMAKE_CURRENT_SOURCE_DIR}/../../src/external/openxr_includes")

add_library(my_module SHARED my_module.cpp)

target_include_directories(my_module PRIVATE
    ${AUG_RUNTIME_INCLUDE}     # module_abi.h
    ${OPENXR_INCLUDE}          # <openxr/openxr.h>
    )

target_link_libraries(my_module PRIVATE
    log     # __android_log_print
    )

# Critical: the loader does dlopen("<ID from metadata.json>.so", ...).
# Output filename must be exactly the manifest ID, with no "lib" prefix.
set_target_properties(my_module PROPERTIES
    PREFIX ""
    OUTPUT_NAME "com.example.modules.my_module"
    SUFFIX ".so"
    )
```

For vendored `.so` dependencies (e.g. the ARCore SDK's
`libarcore_sdk_c.so`), import them as `SHARED IMPORTED` targets
and add them to `target_link_libraries`. The runtime loader
preloads sibling `.so` files in the zip with
`RTLD_NOW | RTLD_GLOBAL` before `dlopen`ing the main module.

## `build.gradle`

The Gradle file:

1. Invokes `cmake -G Ninja` to configure the NDK build.
2. Invokes `cmake --build` to compile.
3. Zips the resulting `.so` plus `metadata.json` and any sibling
   files into `<short-name>.augins`.
4. Optionally pushes to the device via `adb`.

The sample `samples/augins-test-noop/build.gradle` is the
template. Copy and rename the filenames.

## The module source

Minimal module that logs from the lifecycle hooks:

```c
#include "module_abi.h"
#include <android/log.h>

#define TAG "MyModule"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

static const struct aug_host_api *g_host = NULL;

extern "C" {

int aug_on_module_load(const struct aug_host_api *host)
{
    if (host == NULL || host->struct_version < AUG_HOST_API_VERSION) {
        return 1; // reject
    }
    g_host = host;
    LOGI("loaded: host API v%u, data_dir=%s",
         host->struct_version, host->get_module_data_dir());
    return 0;
}

void aug_on_module_unload(void)
{
    LOGI("unloaded");
}

} // extern "C"
```

Pushed to the device, this emits two log lines per service start
and shutdown.

## Adding a dispatchable function

Example: override `xrLocateSpace` to return a sentinel pose.

```c
#include <openxr/openxr.h>

extern "C" XRAPI_ATTR XrResult XRAPI_CALL
xrLocateSpace(XrSpace space, XrSpace baseSpace, XrTime time,
              XrSpaceLocation *location)
{
    if (location == NULL) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    location->pose.position.x = 42.0f;
    location->pose.position.y = 42.0f;
    location->pose.position.z = 42.0f;
    // leave orientation as-is
    return XR_SUCCESS;
}
```

Add the name to the manifest:

```json
"Implemented_Functions": ["xrLocateSpace"]
```

On next start the loader dlsyms `xrLocateSpace` from the module
and the dispatcher routes `space_locate_space` IPC calls through
it.

Overriding a name that has no registered adapter is a runtime
change, not a module change. The current
`aug_implemented_adapters` set and the procedure for adding an
entry are in [Service-Side-Dispatch](Service-Side-Dispatch.md).

## Build, push, observe

```
.\gradlew.bat :samples:my-module:packageMyModuleAugins
.\gradlew.bat :samples:my-module:installMyModuleAugins
```

The first builds and zips. The second pushes and copies the file
into `files/modules/` via `run-as`. Restart the runtime:

```
adb shell am force-stop com.augmented_insanity.runtime.out_of_process
adb shell am start-foreground-service \
  -a org.freedesktop.monado.ipc.CONNECT \
  -n com.augmented_insanity.runtime.out_of_process/org.freedesktop.monado.ipc.MonadoService
```

Tail the logs:

```
adb logcat -s "Aug-Ins.Loader:V" "Aug-Ins.Lifecycle:V" \
                 "Aug-Ins.Dispatch:V" "MyModule:V"
```

The module shows as loaded and registered; when an OpenXR app
calls the dispatched function the sentinel value appears.

## Bundled assets

For runtime files (ONNX models, calibration JSON, shaders), drop
them into the project directory and include them in the `Zip`
task's `from()` blocks. The loader extracts the zip into a
per-module directory; modules read the path via
`host->get_module_data_dir()` and append relative paths.

Capture the path in `aug_on_module_load` for use from worker
threads. See [Host-API-Reference](Host-API-Reference.md).

## Worker threads

Spawn in `aug_on_module_load`, join in `aug_on_module_unload`.
The runtime calls the load hook on the service's main thread
(already JVM-attached); workers that need JNI call
`AttachCurrentThread` themselves and pair it with
`DetachCurrentThread` at exit. Calling `DetachCurrentThread`
from the load hook itself is a JNI fatal.

## Zip layout

```
my-module.augins
  +-- com.example.modules.my_module.so   # main module .so
  +-- libfoo.so                          # any vendored .so
  +-- metadata.json
  +-- assets/                            # optional
  +-- settings.json                      # optional
```

Filename rules:

- The main `.so` must be exactly `<ID>.so` with no `lib` prefix.
- `metadata.json` must be at the zip root.
- Sibling `.so` files may keep the `lib` prefix; the loader
  preloads them by whatever name they have in the extraction dir.
