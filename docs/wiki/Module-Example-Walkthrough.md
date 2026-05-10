<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Module Example Walkthrough

A line-by-line tour of `module-example/augins-head-sway/`, the
featured tutorial module. Build it once, install it, observe the
silly visual result, then come back here and read the source.

## What it does

The module hooks the IPC dispatch path the runtime uses for per-xdev
tracked-pose queries. When a query arrives for the head xdev's
`XRT_INPUT_GENERIC_HEAD_POSE` input, the module reads the
runtime's computed reply, adds a sinusoidal X-axis offset to the
position component, and writes the modified pose back. The visual
result: the rendered scene's head pose sways left-right at 4 second
period and 0.3 m amplitude.

## File layout

```
module-example/augins-head-sway/
    metadata.json        manifest
    settings.json        empty placeholder
    head_sway.cpp        the entire module, ~150 lines
    CMakeLists.txt       standalone NDK build
    build.gradle         Gradle Zip task
    LICENSE.txt          GPL-3.0-or-later text
    README.md            user-facing instructions
```

## metadata.json

```json
{
    "Name": "Aug-Ins Head-Sway Example",
    "ID": "com.augmented_insanity.examples.head_sway",
    "Version": "0.1.0",
    "Description": "Tutorial module. Adds a sinusoidal X-axis offset...",
    "Dependencies": [],
    "Advertised_OpenXR_Features": {
        "Extensions": [],
        "InteractionProfiles": [],
        "SystemPropertyBits": []
    },
    "Implemented_Functions": [
        "aug_deviceGetTrackedPose"
    ]
}
```

Notable choices:

- `ID` is reverse-DNS, distinct from the production samples. The
  module's `.so` will be named exactly
  `com.augmented_insanity.examples.head_sway.so`.
- `Dependencies` is empty -- the module depends on nothing else.
- `Advertised_OpenXR_Features` is empty -- the module does not
  bring any new OpenXR features. It only modifies what the runtime
  is already returning.
- `Implemented_Functions` lists exactly one hook,
  `aug_deviceGetTrackedPose`. That is the synthetic name for the
  IPC `device_get_tracked_pose` call (see
  [IPC Hook Dispatch](IPC-Hook-Dispatch.md)).

## head_sway.cpp -- the structure

The single C++ file has four sections:

1. Includes and constants.
2. Mirror structs that match the IPC msg/reply layout.
3. Module globals and helpers.
4. The `extern "C"` lifecycle and hook functions.

### Includes

```cpp
#include "augins_module_abi.h"   // aug_host_api, AUG_OK, etc.
#include "xrt/xrt_defines.h"     // xrt_pose, xrt_space_relation, XRT_INPUT_GENERIC_HEAD_POSE
```

`augins_module_abi.h` is the only header a module is required to
include. It transitively pulls in `xrt/xrt_defines.h`, but the
explicit second include makes the dependency obvious.

### Constants

```cpp
constexpr float kSwayPeriodSeconds   = 4.0f;
constexpr float kSwayAmplitudeMeters = 0.30f;
constexpr float kTwoPi               = 6.283185307179586f;
```

Tunables. Edit and rebuild to change the look. 4 s period and 30 cm
amplitude is large enough to be obvious, small enough not to
nauseate.

### Mirror structs

```cpp
struct head_sway_msg_dev_get_tracked_pose
{
    int32_t  cmd;
    uint32_t id;
    int32_t  name;
    int64_t  at_timestamp;
};

struct head_sway_reply_relation
{
    int32_t                   result;
    struct xrt_space_relation relation;
};
```

The runtime owns the layout of `ipc_device_get_tracked_pose_msg`
and `ipc_device_get_tracked_pose_reply`. A module cannot include
the generated header (`ipc_protocol_generated.h`) because that
header is built with the runtime's specific IPC protocol version
and bringing it into a module build is fragile.

The pragmatic alternative: redeclare just the fields you need in
layouts that match byte-for-byte. The first struct gets us at
`name` (so we can filter to only the head pose); the second gets us
at `relation` (so we can read what the runtime put there).

If the runtime's IPC schema ever changes incompatibly, this
module's filter will mismatch and the hook will silently no-op --
the runtime keeps working, the module just stops doing anything
visible. Safer than crashing.

### Module globals

```cpp
static const struct aug_host_api *g_host = nullptr;
static std::atomic<int64_t> g_t0_ns{0};

static int64_t monotonic_ns_now() {
    using clock = std::chrono::steady_clock;
    auto d = clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
}
```

`g_host` is the host API table; cached at `aug_onModuleLoad` time
and used later for `set_locate_space_relation`.

`g_t0_ns` is the steady-clock reference for the sway phase. By
capturing it at module load, the sway starts at zero offset and is
deterministic across launches.

### `aug_onModuleLoad`

```cpp
extern "C" void
aug_onModuleLoad(void *args)
{
    const auto *api = static_cast<const struct aug_host_api *>(args);
    if (api == nullptr || api->version < 1u) {
        LOGE("aug_onModuleLoad: host API too old");
        return;
    }
    g_host = api;
    g_t0_ns.store(monotonic_ns_now(), std::memory_order_release);
    LOGI("aug_onModuleLoad: host API v%u accepted", api->version);
}
```

Three things every module's `aug_onModuleLoad` should do:

1. Cast `args` to `const struct aug_host_api *`.
2. Verify the version is high enough. The module uses only
   `set_locate_space_relation` which is in 1.0; we require >= 1.0
3. Store the pointer in a module global.

If the version check fails, just `return` without storing -- the
runtime sees that `g_host` is still `nullptr` later and the hook
no-ops.

### The hook

```cpp
extern "C" int32_t
aug_deviceGetTrackedPose(void *ics, void *msg, void *reply, void *unused)
{
    (void)ics;
    (void)unused;
    if (g_host == nullptr || msg == nullptr || reply == nullptr) {
        return AUG_OK;
    }

    const auto *m = static_cast<const struct head_sway_msg_dev_get_tracked_pose *>(msg);
    if (m->name != static_cast<int32_t>(XRT_INPUT_GENERIC_HEAD_POSE)) {
        return AUG_OK;
    }

    const auto *r = static_cast<const struct head_sway_reply_relation *>(reply);
    struct xrt_space_relation rel = r->relation;

    int64_t t0 = g_t0_ns.load(std::memory_order_acquire);
    float t_seconds = (monotonic_ns_now() - t0) / 1e9f;
    float phase = (kTwoPi * t_seconds) / kSwayPeriodSeconds;
    rel.pose.position.x += kSwayAmplitudeMeters * std::sin(phase);

    g_host->set_locate_space_relation(reply, &rel);
    return AUG_OK;
}
```

Anatomy:

- **Cheap-out checks.** Always handle the case where pointers are
  unexpectedly null. Cheap. Keeps the runtime alive if something
  upstream goes wrong.
- **Filter on `name`.** The hook fires for every per-xdev
  tracked-pose query: head, hand-tracker xdevs, controllers (when
  they exist). We want to perturb only the head pose. The
  `XRT_INPUT_GENERIC_HEAD_POSE` enum value identifies it.
- **Read the reply.** The runtime has already filled in
  `reply->relation`. We read it through the mirror struct.
- **Compute and apply the offset.** Standard sinusoid.
- **Write back via the host API.** `set_locate_space_relation`
  writes our modified relation into the reply struct AND sets
  `result = XRT_SUCCESS`. The runtime then sends the modified
  reply to the client.

`return AUG_OK` tells the runtime "continue dispatching to the
next module's hook". If we returned `AUG_FATAL_MODULE`, our
module would be removed from the dispatch table.

## CMakeLists.txt

The interesting bits:

```cmake
set(AUG_RUNTIME_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/xrt/augins")
set(XRT_PUBLIC_INCLUDE  "${CMAKE_CURRENT_SOURCE_DIR}/../../src/xrt/include")

target_include_directories(head_sway_module PRIVATE
    ${AUG_RUNTIME_INCLUDE}    # augins_module_abi.h
    ${XRT_PUBLIC_INCLUDE}     # xrt/xrt_defines.h
    )

set_target_properties(head_sway_module PROPERTIES
    PREFIX ""
    OUTPUT_NAME "com.augmented_insanity.examples.head_sway"
    SUFFIX ".so"
    )
```

Reaches sideways into the runtime tree for the two headers we
need. The `PREFIX ""` and `OUTPUT_NAME` lines force the `.so` to
be named exactly `<module-id>.so`, which is what the loader
expects.

## build.gradle

A thin wrapper that calls `cmake` and `ninja` via Exec tasks, then
zips the result. See
`module-example/augins-head-sway/build.gradle` for the source. The
key tasks:

- `configureHeadSway` -> runs CMake.
- `buildHeadSway` -> runs Ninja.
- `packageHeadSwayAugins` -> Zip task; produces
  `build/head-sway.augins` containing the `.so`, `metadata.json`,
  `settings.json`, and `LICENSE.txt`.
- `installHeadSwayAugins` -> packages and adb-pushes onto a
  device.

## What you should change to write your own

Copy `module-example/augins-head-sway/` to a new directory under
`samples/` (or your own external repo), then:

1. Edit `metadata.json`: change `Name`, `ID`, `Description`, and
   the list of `Implemented_Functions` to match what your module
   does.
2. Edit `head_sway.cpp` -> rename to your module's primary
   source file, replace the sway logic with whatever your module
   actually does.
3. Edit `CMakeLists.txt`: change `OUTPUT_NAME` to match your
   `metadata.json::ID`.
4. Edit `build.gradle`: search-replace `HeadSway` ->
   `<YourModule>` and `head-sway.augins` -> `<your-module>.augins`.
5. Add a project entry in `settings.gradle`.

That is the complete recipe.
