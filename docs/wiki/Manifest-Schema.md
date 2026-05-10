<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Manifest Schema

`metadata.json` declares everything the runtime needs to know about a
module. It must sit at the root of the `.augins` zip alongside the
module's `.so`.

## Reference

```json
{
    "Name": "Human-readable name",
    "ID": "reverse.dotted.identifier",
    "Version": "0.1.0",
    "Description": "One-paragraph description of what the module does.",
    "Dependencies": ["other.module.id"],
    "Advertised_OpenXR_Features": {
        "Extensions": ["XR_EXT_..."],
        "InteractionProfiles": [],
        "SystemPropertyBits": ["handTracking"]
    },
    "Implemented_Functions": [
        "xrLocateSpace",
        "aug_deviceGetTrackedPose"
    ]
}
```

## Field-by-field

### `Name` (required, string)

Display name used in logs and (eventually) in any UI listing
installed modules. Free-form. Example: `"Mercury Hand Tracking
(ARCore camera)"`.

### `ID` (required, string)

The module's unique identifier across the entire module ecosystem.
Convention: reverse-DNS. Example:
`"com.augmented_insanity.samples.mercury_handtracking_arcore"`.

The runtime uses `ID` to:

- Name the per-module extraction directory (`cache/opennedmodules/<id>/`).
- `dlopen` the module's `.so` (the `.so` filename inside the zip
  must be exactly `<ID>.so`).
- Match against other modules' `Dependencies`.

Two `.augins` files with the same `ID` are an error; the second-loaded
one wins with a warning.

### `Version` (required, string)

Semver-shaped human-readable version. Currently informational only;
the runtime does not enforce semver semantics.

### `Description` (required, string)

One paragraph. Free-form.

### `Dependencies` (optional, array of `ID` strings)

Other modules this module requires loaded before it. The loader does
topological sort; if a dep is missing the dependent module is skipped
with a clear log line.

Example: `augins-mercury-handtracking-arcore` lists
`["com.augmented_insanity.samples.arcore_headpose"]` because Mercury
consumes camera frames produced by the ARCore module.

### `Advertised_OpenXR_Features` (optional, object)

Tells the runtime to surface OpenXR features on behalf of this module.
The runtime aggregates these across all loaded modules and answers
client queries truthfully.

#### `Extensions` (optional, array of string)

OpenXR extension names this module brings. Currently aggregated in
the runtime's manifest aggregator
(`src/xrt/augins/augins_extensions.cpp`); the OXR-side surfacing
through `xrEnumerateInstanceExtensionProperties` is not yet wired.
[WIP].

#### `InteractionProfiles` (optional, array of string)

Reserved. Schema field exists; aggregator does not yet consume.

#### `SystemPropertyBits` (optional, array of string)

Used by the stub-xdev factory to decide which placeholder
`xrt_device` instances to fabricate. Currently recognised tokens:

- `"handTracking"` -- creates a hand-tracker xdev with both
  `unobstructed` and `conforming` left/right inputs. The module
  must call `g_host->register_hand_tracker(cb)` to actually
  produce joints when queried.

Future tokens (eye tracking, body tracking, etc.) will be added
here as the matching xdev factory paths land.

### `Implemented_Functions` (optional, array of string)

Names of IPC dispatch hooks the module implements. Each name must:

- Be a key in the `aug_ipc_to_xr` value-set in
  `src/xrt/ipc/shared/proto.py` (i.e. either an `xr*` function name
  or a synthetic `aug_*` name for IPC calls without an OpenXR
  analogue).
- Be exported from the module's `.so` as a C symbol with the same
  name (`extern "C"`).
- Have the signature
  `int32_t (*)(void *ics, void *msg, void *reply, void *unused)`.

Hooks not listed here are not registered, even if the symbol is
exported.

Currently recognised names (see
[IPC Hook Dispatch](IPC-Hook-Dispatch.md) for the full list and
how to add new ones):

- OpenXR-derived: `xrCreateSession`, `xrBeginSession`,
  `xrEndSession`, `xrPollEvent`, `xrWaitFrame`, `xrBeginFrame`,
  `xrEndFrame`, `xrCreateSwapchain`, `xrAcquireSwapchainImage`,
  `xrLocateSpace`, `xrLocateSpaces`, `xrLocateViews`, ...
- Synthetic: `aug_spaceCreateSemanticIds`,
  `aug_deviceGetTrackedPose`.

### **Notice:** *I have a plan to allow any and all hooks to be dispatched.*

## Examples

### Minimal (head-sway tutorial)

```json
{
    "Name": "Aug-Ins Head-Sway Example",
    "ID": "com.augmented_insanity.examples.head_sway",
    "Version": "0.1.0",
    "Description": "Tutorial module.",
    "Dependencies": [],
    "Advertised_OpenXR_Features": {},
    "Implemented_Functions": [
        "aug_deviceGetTrackedPose"
    ]
}
```

### Capability producer with deps (Mercury ARCore-camera)

```json
{
    "Name": "Aug-Ins Mercury Hand Tracking (ARCore camera)",
    "ID": "com.augmented_insanity.samples.mercury_handtracking_arcore",
    "Version": "0.1.0",
    "Description": "Mercury+ONNX hand tracking, camera frames sourced from arcore-headpose.",
    "Dependencies": [
        "com.augmented_insanity.samples.arcore_headpose"
    ],
    "Advertised_OpenXR_Features": {
        "Extensions": ["XR_EXT_hand_tracking"],
        "SystemPropertyBits": ["handTracking"]
    },
    "Implemented_Functions": []
}
```

**Note:** a producer-only module like Mercury declares no
`Implemented_Functions` because it does not intercept IPC dispatches
itself. It uses the host API (`register_hand_tracker`,
`subscribe_camera_frame`) instead.

## Validation

`metadata.json` is parsed at module load time. Common errors:

- Missing `ID`, `Name`, or `Version`: module is skipped with
  `Manifest is missing required field: <field>`.
- Invalid JSON: module is skipped with parse error.
- `Dependencies` referencing a non-existent module ID: dependent
  module is skipped with `Dependency '<id>' not found`.
- `SystemPropertyBits` containing unrecognised tokens: tokens are
  warned and ignored.

There is no formal JSON schema validator yet; structural checks
happen in `augins_dispatch.cpp::parse_module`. A schema-driven
validator is a candidate for [Roadmap](Roadmap.md).
