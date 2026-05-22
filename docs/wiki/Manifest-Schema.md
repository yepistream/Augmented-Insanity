<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Manifest Schema

Every `.augins` package contains `metadata.json` at the zip root.
Schema version: v1 (`Manifest_Version: 1`). The runtime rejects
modules with a mismatched or missing `Manifest_Version`.

Parser source of truth: `src/xrt/augins/manifest.cpp`.

## Minimal example

```json
{
    "Manifest_Version": 1,
    "ID": "com.example.modules.my_module",
    "Version": "0.1.0",
    "Implemented_Functions": [
        "xrLocateSpace"
    ]
}
```

## Full example

From `samples/augins-arcore-headpose/metadata.json`:

```json
{
    "Manifest_Version": 1,
    "ID": "com.augmented_insanity.samples.arcore_headpose",
    "Version": "0.2.0",
    "Name": "Aug-Ins ARCore Head Pose",
    "Description": "Provides 6DoF head pose via Google ARCore. ...",
    "Priority": 100,
    "Implemented_Functions": [
        "aug_LocateDeviceInSpace"
    ]
}
```

## Field reference

### Required

#### `Manifest_Version` (integer)

The schema version. Currently `1`. The runtime compares against
`AUG_MANIFEST_VERSION` (defined in `src/xrt/augins/module_abi.h`)
and rejects mismatches.

#### `ID` (string)

Globally unique reverse-DNS identifier for the module. Used to:

- Name the main `.so` inside the zip (`<ID>.so` exactly, no `lib`
  prefix).
- Construct the per-module data dir path.
- Disambiguate modules that override the same function name.

Recommended scheme: `<owner-domain-reversed>.modules.<short>` for
production modules, or `<owner-domain-reversed>.samples.<short>`
for examples in this repo. Restricted to characters valid in a
filename.

#### `Version` (string)

Module version. Any string format. SemVer recommended. The
runtime does not currently parse or compare the value; the field
is for human inspection and future update-detection logic.

#### `Implemented_Functions` (array of strings)

The list of OpenXR-named or Aug-Ins-synthetic functions this module
implements. The loader dlsyms each name from the module's `.so` and
registers it for dispatch. See
[Service-Side-Dispatch](Service-Side-Dispatch.md) for the current
set of dispatchable names (`aug_implemented_adapters` in
`src/xrt/ipc/shared/proto.py`).

A module may list a name with no registered adapter. The loader
warns and skips that entry; other entries from the same module
still register.

### Optional

#### `Name` (string)

Human-readable display name. Surfaced by `AugInsTestActivity` and
by any future settings UI. Free-form. Defaults to `ID` if absent.

#### `Description` (string)

Free-form text, same usage as `Name`.

#### `Priority` (integer)

Dispatch ordering. Default: `100`. Lower runs earlier; combined
with last-write-wins (Q1) this means higher `Priority`
overwrites the writes of lower-priority modules.

Suggested ranges:

- `0..49` -- pre-baseline decorators (runs before default-100)
- `50..99` -- secondary observers and filters
- `100` -- default for production modules
- `101..199` -- explicit overrides on top of the default
- `200+` -- last-word modules (debug overlays, user settings
  overrides)

The runtime sorts by the numeric value; the ranges above are
convention only.

`[Planned]` v0.2.x adds a central `module_order.json` override file
in the modules dir that can re-order modules independent of their
declared `Priority`.

## Fields planned but not in v1

The parser will accept and ignore unknown top-level fields, so the
following can be added in v0.2.x without breaking v1 modules:

- `Failure_Mode` -- per-module choice for Q5 ("abort",
  "recoverable", "critical"). v0.2 base behaviour is "abort".
- `Dependencies` -- array of module IDs that must be loaded first.
  Will trigger a topological sort across the module set.
- `Advertised_OpenXR_Features` -- system bits this module
  contributes to (`supportsHandTracking`, etc.).

See [Roadmap](Roadmap.md).

## Validation walk

The parser:

1. Loads the file as UTF-8 JSON via cJSON.
2. Reads `Manifest_Version`; rejects non-integer or mismatched.
3. Reads `ID`, `Version`, `Implemented_Functions`; rejects if any
   missing or wrong type.
4. Reads `Name`, `Description`, `Priority` if present; defaults
   are `""`, `""`, `100`.
5. Unknown top-level keys are ignored. (v1 forward-compat path for
   future fields.)
