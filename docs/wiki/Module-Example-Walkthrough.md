<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Module Example Walkthrough

`module-example/augins-head-sway/` is the tutorial module. It does
one thing: add a sinusoidal X-offset to the head pose returned by
the runtime, so any XR app rendering on this runtime sees the
view sway left and right by 0.3 m every 4 seconds.

It is the simplest possible v0.2 module that produces a visible
effect. Standalone NDK build, no external SDK dependencies, no
worker threads, ~50 lines of C++ in the dispatched function plus
two short lifecycle hooks.

## Files

```
module-example/augins-head-sway/
  +-- metadata.json
  +-- settings.json
  +-- head_sway.cpp
  +-- CMakeLists.txt
  +-- build.gradle
```

The build packages these into `head-sway.augins` along with the
GPL-3.0-or-later license body copied from `LICENSES/`.

## `metadata.json`

```json
{
    "Manifest_Version": 1,
    "ID": "com.augmented_insanity.examples.head_sway",
    "Version": "0.2.0",
    "Name": "Aug-Ins Head Sway Example",
    "Description": "Tutorial module. Adds a sinusoidal X-offset ...",
    "Priority": 200,
    "Implemented_Functions": [
        "aug_LocateDeviceInSpace"
    ]
}
```

Field-by-field reference: [Manifest-Schema](Manifest-Schema.md).

The `Priority` is `200`, deliberately higher than the ARCore
module's `100`. The dispatch chain runs lower-priority modules
first (Q4) and the last write wins (Q1), so when both modules
are installed the order is ARCore (writes 6DoF pose) -> head-sway
(adds X-offset to whatever was written). Reverse the priorities
and the sway would be replaced by ARCore's pose instead of being
added to it.

## `settings.json`

```json
{
}
```

Empty. The field exists in the schema for forward compatibility
with the planned v0.2.2 settings UI; v0.2 base has no consumer.

## `head_sway.cpp`

The module exports three symbols.

### `aug_on_module_load`

```c
int aug_on_module_load(const struct aug_host_api *host)
{
    if (host == nullptr || host->struct_version < AUG_HOST_API_VERSION) {
        return 1;
    }
    g_start_time = std::chrono::steady_clock::now();
    LOGI("aug_on_module_load: head-sway armed (amplitude=%.2f m, period=%.2f s)",
         kSwayAmplitudeM, kSwayPeriodS);
    return 0;
}
```

Validates the runtime's host API is at least v1 (the minimum
required). Captures the monotonic start time so the sinusoidal
phase starts at zero on every service start. Returns 0 to accept
the module; non-zero would reject it.

No need to cache the `host` pointer here -- the dispatched
function uses none of the host API entries. A more typical
module would store `g_host = host` for later use.

### `aug_on_module_unload`

```c
void aug_on_module_unload(void)
{
    LOGI("aug_on_module_unload: head-sway disarmed");
}
```

Logs and returns. The module spawned no worker threads, holds no
OS resources, owns no JNI global refs; there is nothing to clean
up.

### `aug_LocateDeviceInSpace`

```c
XRAPI_ATTR XrResult XRAPI_CALL
aug_LocateDeviceInSpace(XrSpace          baseSpace,
                        XrTime           time,
                        XrSpaceLocation *location)
{
    if (location == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    const auto now = std::chrono::steady_clock::now();
    const float t = std::chrono::duration<float>(now - g_start_time).count();
    const float omega = 2.0f * static_cast<float>(M_PI) / kSwayPeriodS;
    const float dx = kSwayAmplitudeM * std::sin(omega * t);
    location->pose.position.x += dx;
    return XR_SUCCESS;
}
```

The runtime adapter passed `location` with the baseline pose
already filled in (Q2: runtime default runs before modules).
This function decorates that pose with a sinusoidal X-offset
rather than producing a pose from scratch -- canonical decorator
pattern.

`baseSpace` and `time` are unused. A more sophisticated module
would inspect them: `baseSpace` to apply different transforms in
different reference-space frames, `time` to drive an animation
phase off the XR-app-requested predicted display time instead of
host monotonic time. For a tutorial demonstration, ignoring them
keeps the code short.

Why the function name is `aug_LocateDeviceInSpace` and not
`xrLocateViews`: see [Service-Side-Dispatch](Service-Side-Dispatch.md).
The IPC layer carries one head-locate-in-space call per view, so
the function signature matches that narrower shape, not the
top-level multi-view `xrLocateViews` shape.

## `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.22)
project(augins-head-sway LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(AUG_RUNTIME_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/xrt/augins")
set(OPENXR_INCLUDE      "${CMAKE_CURRENT_SOURCE_DIR}/../../src/external/openxr_includes")

add_library(head_sway_module SHARED head_sway.cpp)

target_include_directories(head_sway_module PRIVATE
    ${AUG_RUNTIME_INCLUDE}    # module_abi.h
    ${OPENXR_INCLUDE}         # <openxr/openxr.h>
    )

target_link_libraries(head_sway_module PRIVATE log)

set_target_properties(head_sway_module PROPERTIES
    PREFIX ""
    OUTPUT_NAME "com.augmented_insanity.examples.head_sway"
    SUFFIX ".so"
    )
```

Three things matter here:

- The include paths pull `module_abi.h` (the v0.2 module ABI
  header) and `<openxr/openxr.h>` from inside the repo. A
  third-party module shipping outside the Aug-Ins repo would
  copy `module_abi.h` into its own tree and use whatever OpenXR
  headers it has.
- `PREFIX ""` plus an explicit `OUTPUT_NAME` and `SUFFIX ".so"`
  produces the file as exactly `<ID from metadata.json>.so`,
  not `lib<ID>.so`. The loader does
  `dlopen("<ID>.so", ...)`; the `lib` prefix would break the
  match.
- No `target_link_libraries` beyond `log`. The module needs
  nothing from `aug_host_api` and never touches the host API
  beyond reading `struct_version`.

## `build.gradle`

The Gradle file invokes `cmake` and `ninja` directly via `Exec`
tasks (the Android Gradle Plugin would re-prefix the `.so` with
`lib`, breaking the loader's `dlopen`). Its `Zip` task assembles
the resulting `.so` plus `metadata.json` plus `settings.json`
plus a copy of `LICENSES/GPL-3.0-or-later.txt` (renamed to
`LICENSE.txt`) into `build/head-sway.augins`.

Three tasks worth knowing:

- `packageAugins` -- builds the `.so` and produces the zip.
- `installAugins` -- packages then pushes the zip to the
  device's `files/modules/` via `adb` + `run-as`.
- `clean` -- wipes the per-module `build/` tree.

See [Building-A-Module](Building-A-Module.md) for the same
build pattern explained in template form.

## Build, install, observe

```
.\gradlew.bat :module-example:augins-head-sway:installAugins
adb shell am force-stop com.augmented_insanity.runtime.out_of_process
adb shell am start-foreground-service \
    -a org.freedesktop.monado.ipc.CONNECT \
    -n com.augmented_insanity.runtime.out_of_process/org.freedesktop.monado.ipc.MonadoService
adb logcat -s "Aug-Ins.Loader:V" "Aug-Ins.Lifecycle:V" "Aug-Ins.HeadSway:V"
```

The expected loader output:

```
I Aug-Ins.Loader:    loaded module 'com.augmented_insanity.examples.head_sway' v0.2.0 (priority=200, 1/1 functions resolved)
I Aug-Ins.HeadSway:  aug_on_module_load: head-sway armed (amplitude=0.30 m, period=4.00 s)
I Aug-Ins.Lifecycle: module 'com.augmented_insanity.examples.head_sway': aug_on_module_load OK
```

Launching any OpenXR app on the device produces a visible
left-right sway in the rendered scene with a 4-second period.

## Combining with other modules

With both `arcore-headpose.augins` (Priority 100) and
`head-sway.augins` (Priority 200) installed, the dispatch chain
for `aug_LocateDeviceInSpace` becomes:

```
runtime default head pose -> ARCore overrides with 6DoF tracking -> head-sway adds X-offset
```

The visible result is 6DoF head tracking plus a constant
sinusoidal sway. Swap the priorities and head-sway runs first;
ARCore's write then overwrites the sway and the sway disappears.
This is the dispatch contract documented in
[Module-System](Module-System.md) -- last write wins (Q1),
order set by `Priority` (Q4).

## Adapting this for a real module

The head-sway pattern is what a "pose decorator" module looks
like. Real modules typically:

- Replace the trigonometry with state pulled from a sensor or
  cached worker thread. See `samples/augins-arcore-headpose/`
  for the ARCore worker thread pattern.
- Add more exported functions (e.g. `xrLocateSpace` for
  semantic-space queries that need the same override).
- Use the host API entries (`get_jvm`, `get_context`,
  `get_module_data_dir`) for asset loading and JNI access.
- Spawn workers in `aug_on_module_load` and join them in
  `aug_on_module_unload`.

See [Building-A-Module](Building-A-Module.md) for the general
authoring template, and [Host-API-Reference](Host-API-Reference.md)
for what the runtime exposes to modules.
