<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Roadmap

Planned work, grouped by target release.

## v0.2.x

Patch and minor releases that fill gaps in the v0.2 architecture
without changing the module ABI.

- `aug_DeviceGetTrackedPose` adapter. Adds the T_xdev_head half
  of `xrLocateViews` to the dispatchable set, letting the ARCore
  module own head pose end-to-end. Same head-only filter as
  `aug_LocateDeviceInSpace`.
- `Failure_Mode` per-module manifest field. Three tiers:
  `"abort"` (current Q5 behaviour), `"recoverable"` (drop the
  module from the chain on error, continue), `"critical"` (taint
  the runtime, kill the service). Default stays `"abort"`.
- `Dependencies` manifest field. Array of module IDs that must
  load before this one; loader runs a Kahn topological sort
  before firing `aug_on_module_load`. Added only when a concrete
  case lands; Priority covers most ordering needs.
- `module_order.json` central override file in the modules dir.
  Overrides Priority values without re-zipping each module.
- APK-bundled fallback for modules. Default modules ship under
  `assets/modules/` in the runtime APK; the loader unpacks on
  first start. User-installed `.augins` in `files/modules/` with
  the same `ID` override the bundled copy.
- `aug_on_module_load` rejection unwind. Stage dispatch entries
  and commit only on success. See [Known-Issues](Known-Issues.md).
- `MonadoService.startForegroundService` for self-rebind. Fixes
  the `BackgroundServiceStartNotAllowedException` on cold XR-app
  launch. Possibly upstreamed to Monado first.
- Lazy `ArSession` start in the ARCore module. Open on first
  dispatch call instead of in `aug_on_module_load`; idle out
  after N seconds of no calls. Cuts idle CPU and camera draw.
- Non-head device adapters for `space_locate_device`. v0.2 base
  filters to head-only. When controllers or generic-trackers
  need module overrides, add per-role synthetic names
  (`aug_LocateControllerInSpace`, ...) or extend
  `aug_LocateDeviceInSpace` with an `aug_device_role` parameter.
- `LOCAL_FLOOR` Y offset applied in the head-pose path.
- `Preload_Order` manifest field for sibling `.so` preload
  ordering. Optional, added only when a real case needs it.

### v0.2.1 (Mercury rewrite)

- Mercury hand-tracking module on the v0.2 ABI. Rewrites
  `samples/augins-mercury-handtracking-arcore` on the v0.2
  service-side ABI. Implements `xrCreateHandTrackerEXT`,
  `xrLocateHandJointsEXT`, `xrDestroyHandTrackerEXT` via
  adapters that need writing.
- Camera frame broker in the host API. Returns when Mercury
  needs it. Same shape as v0.1: a producer module
  (`arcore-headpose`) publishes Y8 frames and intrinsics, a
  subscriber module (`mercury-handtracking-arcore`) consumes
  them. In-process function-pointer broker, no IPC.
- `xrEnumerateInstanceExtensionProperties` augmentation. A
  manifest `Advertised_OpenXR_Features` field plus a service-side
  aggregator. A hand-tracking module's manifest causes the
  runtime to advertise `XR_EXT_hand_tracking` and set
  `XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT` bits.

### v0.2.2 (developer settings)

- Per-module settings UI. A list activity in the runtime APK
  reads each module's optional `settings.json` and renders
  sliders, toggles, and text inputs. Values land in per-module
  SharedPreferences read by modules via a new
  `host->get_setting(name)` host API entry.
- Calibration activity. UI for hand-tracking intrinsic
  calibration. Replaces the current fallback-to-ARCore-intrinsics
  path with explicit user calibration.

## v0.3+

Items that change the module ABI or service architecture.

- Codegen-from-`xr.xml` for adapters. A `gen_thunks.py` step
  reads the OpenXR registry and emits adapters automatically.
  Hand-written adapters retire to a small fallback set for IPC
  calls without a 1:1 OpenXR mapping.
- Hot reload modules without service restart. Push a new
  `.augins`; the loader detects the change (size-stamp), drains
  in-flight calls, dlcloses the old `.so`, dlopens the new one,
  re-runs lifecycle hooks. Requires refcounting around in-flight
  dispatch.
- DEX loading for modules with Java helpers. Modules ship a
  `classes.dex` inside their `.augins`; the loader uses
  `DexClassLoader`. Enables modules that need their own Activity
  (USB-camera calibration, per-module settings).
- Multi-process module isolation. Each module runs in its own
  sandboxed sub-process; the runtime communicates with it via a
  smaller IPC. One bad module cannot crash the service.
  Significant complexity; not done unless modules become a
  security or stability target.
- Module signing and trust. Modules carry a signature; the
  runtime can be configured to refuse unsigned modules.
- USB-camera hand tracking. A Mercury variant using a UVC USB
  camera instead of the ARCore camera stream. Enables hand
  tracking on devices without ARCore support.
- `ht_ctrl_emu` integration. Map hand poses to virtual controller
  inputs so apps that only support controllers can use hand
  tracking transparently. Upstream Monado has the building
  blocks.

## Out of scope

Ruled out in earlier design discussions. Reasoning recorded so
the same proposals do not get re-relitigated.

- v0.1 module ABI back-compat. v0.1 modules must be rewritten
  on the v0.2 ABI. The mirror-struct
  `void *(*)(void *, void *, void *, void *)` calling convention
  is not coming back.
- Client-side module loading. Modules run in the service
  process, not in untrusted XR-app processes. Service-side is
  the locked architecture; see [Architecture-Overview](Architecture-Overview.md).
- Module-supplied compositor replacements. The "Tier 4" idea
  from early sketches. Outside v0.2 / v0.3 scope.
- Pruning Monado code not used by Aug-Ins (Linux HMD drivers,
  SteamVR bridge, etc.). Aggressive pruning loses the ability
  to merge upstream Monado changes; the repo size is acceptable.

## Process

Items in the v0.2.x / v0.2.1 / v0.2.2 buckets become real when:

- A concrete use case lands that needs the feature.
- A pre-existing module is blocked on a missing piece (e.g.
  Mercury on the camera broker).
- A user-visible bug forces the issue.

Order within a bucket is not strict; the item that unblocks the
most other work goes first.
