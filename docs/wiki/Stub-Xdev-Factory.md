<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# **IMPORTANT THIS WILL BE CHANGED AS SOON AS POSSIBLE**
# Stub Xdev Factory `[WIP]`



When a module advertises an OpenXR feature that the OpenXR state
tracker fulfils by querying an `xrt_device`, the runtime fabricates a
placeholder xdev so the state tracker has something to query. The xdev
forwards every query into the host-API-registered producer callback in
the relevant module.

**NOTICE:** This is only used for testing, and will be replaced, a module must hook into the query function on dispatch as that is the intended way to keep modularity and flexibility.

Code paths:

- `src/xrt/augins/augins_stub_xdevs.{cpp,h}` -- factory.
- `src/xrt/augins/augins_hand_tracker_dispatch.h` -- forward to the
  module-registered hand-tracker callback.
- `src/xrt/ipc/server/ipc_server_process.c` -- where the factory is
  invoked.

## What it currently does

For the hand-tracking system bit
(`Advertised_OpenXR_Features.SystemPropertyBits: ["handTracking"]`),
`augins_stub_xdevs_install`:

1. Allocates one `xrt_device` of `device_type =
   XRT_DEVICE_TYPE_HAND_TRACKER`.
2. Sets four `xrt_input` entries on the xdev:
   `XRT_INPUT_HT_UNOBSTRUCTED_LEFT`,
   `XRT_INPUT_HT_UNOBSTRUCTED_RIGHT`,
   `XRT_INPUT_HT_CONFORMING_LEFT`,
   `XRT_INPUT_HT_CONFORMING_RIGHT`. The four-input layout supports
   both `XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT` and
   `XR_HAND_TRACKING_DATA_SOURCE_CONTROLLER_EXT` clients.
3. Plugs the same xdev pointer into all four
   `xsysd->static_roles.hand_tracking.{unobstructed,conforming}.{left,right}`
   slots.
4. Adds the xdev to `xsysd->static_xdevs[]` (so the IPC layer can
   find it by index).
5. Registers the xdev with the space overseer
   (`b_space_overseer_link_space_to_device`) so
   `xrt_space_overseer_locate_device` does not assert when the
   client queries the hand pose.

The xdev's `get_hand_tracking` function pointer dispatches into
`augins_hand_tracker_dispatch`, which calls the module-registered
hand-tracker callback (or returns `is_active = false` if no module
has registered yet).

## Where it is invoked

```c
// src/xrt/ipc/server/ipc_server_process.c
xret = xrt_instance_create_system(s->xinst, &s->xsys, &s->xsysd, &s->xso, &s->xsysc);
// ... real Monado system-builder runs and logs "In roles:" ...

#ifdef XRT_FEATURE_AUG_INS
augins_stub_xdevs_install(s->xsysd, s->xso);
#endif
```

This is post-hoc. The system has already been built, the space
overseer already has the original xdev list, and `p_create_system`
has already logged its
`In roles: hand_tracking.unobstructed.left: <none>` line. The
factory then walks in and patches.

## Why post-hoc is currently the design

Hooking into the system-builder pipeline (`target_lists.c` and the
target-builder protocol) requires a more invasive change. I picked
the post-hoc approach to test the head-tracking + hand-tracking pair, the cleanup is on the [Roadmap](Roadmap.md).

## Why post-hoc is a problem

Three issues:

- **Misleading log.** The
  `In roles: hand_tracking.*: <none>` line fires before our patch
  and is wrong by the time clients see anything.
- **Custom space-overseer registration.** I had to expose
  `b_space_overseer_link_space_to_device` and
  `b_space_overseer_create_null_space` in the public header so the
  factory can register the new xdev with the space overseer. A
  cleaner design would not need this header surface.
- **Race window.** If a client connected and queried the hand
  tracker between `xrt_instance_create_system` returning and
  `augins_stub_xdevs_install` running, it would get an empty
  reply. In practice the IPC server has not started accepting
  clients at this point, but the order is brittle.

## How the refactor will work

Plan: The next patch is going to add an Aug-Ins target-builder hook that consults the
manifest aggregator and contributes its xdevs INTO the initial
xdev list passed to `b_space_overseer_legacy_setup`. The
post-hoc patch goes away; the misleading log goes away;
`b_space_overseer.h` shrinks. 

*NOTICE: Again this is not a method that will stay*

This is one of the items in [Roadmap](Roadmap.md) under
"short-term".

## Adding a new feature bit

To wire up a new system-property bit (e.g. eye tracking):

1. Add an enum entry in `src/xrt/augins/augins_extensions.h`
   (`AUGINS_SYS_EYE_TRACKING`).
2. Map a manifest token in
   `augins_extensions.cpp::add_advertised_system_bit`
   (`"eyeTracking" -> AUGINS_SYS_EYE_TRACKING`).
3. Add a stub-xdev builder in
   `augins_stub_xdevs.cpp::augins_stub_xdevs_install` that fires
   when the bit is set: allocate the xdev, set its inputs, plug
   into the appropriate `static_roles` slots, register with the
   space overseer.
4. Add the host-API producer-registration helper for that capability
   (e.g. `register_eye_tracker(cb)`); bump
   `AUG_HOST_API_VERSION`.
5. Document the new manifest token in
   [Manifest Schema](Manifest-Schema.md) and the new host API
   helper in [Host API Reference](Host-API-Reference.md).
