<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Service-Side Dispatch

How an incoming IPC call on the service side reaches a module
function with OpenXR-shaped arguments, and how to add a new
dispatchable function.

## IPC call vs OpenXR call

Monado's out-of-process design decomposes each OpenXR call into one
or more IPC messages on the client side. By the time the service
sees the call, the original OpenXR signature is gone -- replaced
by narrower IPC msg/reply structs.

Two examples:

| OpenXR call | Service-side IPC call(s) |
|-------------|-------------------------|
| `xrLocateSpace` | `space_locate_space` (1:1) |
| `xrLocateViews` | `space_locate_device` + `device_get_tracked_pose` (per view) |

For `xrLocateSpace` the IPC call carries the same arguments as
the OpenXR function. A module exports `xrLocateSpace` with the
real OpenXR signature; the adapter is a thin translation.

For `xrLocateViews` there is no signature that matches "half of
xrLocateViews on a per-view IPC call". The convention is a
synthetic OpenXR-style name with an `aug_` prefix
(`aug_LocateDeviceInSpace`), whose signature matches what the
IPC layer actually carries.

## The dispatch fork

`src/xrt/ipc/shared/proto.py` is the codegen that emits
`ipc_server_generated.c`. Two data structures govern dispatch:

```python
# In proto.py:
aug_ipc_to_xr = {
    "space_locate_space":  "xrLocateSpace",
    "space_locate_device": "aug_LocateDeviceInSpace",
    # ...
}
aug_implemented_adapters = {
    "xrLocateSpace",
    "aug_LocateDeviceInSpace",
}
```

For every IPC call in `aug_ipc_to_xr` whose mapped name is in
`aug_implemented_adapters`, the codegen emits a dispatch fork:

```c
case IPC_SPACE_LOCATE_DEVICE: {
    // ...
#ifdef XRT_FEATURE_AUG_INS
    if (aug_has_modules_for("aug_LocateDeviceInSpace")) {
        reply.result = aug_adapter_space_locate_device(ics,
                                                       msg->base_space_id,
                                                       &msg->base_offset,
                                                       msg->at_timestamp,
                                                       msg->xdev_id,
                                                       &reply.relation);
    } else
#endif
    {
        reply.result = ipc_handle_space_locate_device(ics,
                                                     msg->base_space_id,
                                                     &msg->base_offset,
                                                     msg->at_timestamp,
                                                     msg->xdev_id,
                                                     &reply.relation);
    }
    // ...
}
```

If no module is registered for the mapped name,
`aug_has_modules_for` returns false and the runtime calls the
original `ipc_handle_*` directly. The probe is an
`unordered_map::count` lookup; sub-microsecond.

## The adapter

Adapters live in `src/xrt/augins/adapters.{h,cpp}`. One adapter
per IPC call that participates in dispatch. Each has the same C
signature as the underlying `ipc_handle_*`, so the codegen can
route to it as a drop-in replacement.

The body has five steps:

1. Call the runtime default (`ipc_handle_*`) to fill the baseline
   reply (Q2).
2. Unpack the IPC msg/reply structs into OpenXR-shaped args. For
   `xrLocateSpace` this is `xrt_space_relation` ->
   `XrSpaceLocation` plus a manual flag-bit translation
   (`xrt_space_relation_flags` and `XrSpaceLocationFlags` use
   different bit values).
3. Iterate modules registered for the mapped name in priority
   order via `aug_get_modules_for(name, chain, max)`. Push and
   pop the per-module data-dir TLS around each call.
4. Abort on first non-success `XrResult` from a module (Q5),
   map the result back to `xrt_result_t`, return.
5. Repack the (possibly modified) OpenXR struct back into the
   IPC reply.

The `xrLocateSpace` adapter is the template; copy from there when
adding a new one.

## Adding a new adapter

Steps to make a new IPC call routable through modules:

1. Pick the module-facing name. For an IPC call with a 1:1 OpenXR
   equivalent, use the OpenXR name (`"xrCreateHandTrackerEXT"`).
   Otherwise invent a synthetic `aug_<CamelCase>` name and
   document the choice in the `proto.py` comment.
2. Add the entry to `aug_ipc_to_xr` in
   `src/xrt/ipc/shared/proto.py`, mapping IPC call to module-facing
   name.
3. Add the module-facing name to `aug_implemented_adapters` in the
   same file.
4. Declare the adapter in `src/xrt/augins/adapters.h` with the same
   signature as `ipc_handle_<call>` (defined in
   `src/xrt/ipc/server/ipc_server_handler.c`).
5. Implement the adapter in `src/xrt/augins/adapters.cpp`.
   Forward-declare `ipc_handle_<call>` at the top of the file (the
   generated header lives in the build dir and is not on
   `aux_augins`'s include path). Follow the five-step template.
6. If the adapter needs a runtime-side filter (like the head-only
   check in `aug_adapter_space_locate_device`), add the predicate
   to `src/xrt/ipc/server/ipc_server_objects.{h,c}` and
   forward-declare it in `adapters.cpp` alongside the
   `ipc_handle_*` declaration.
7. Rebuild. The next runtime build regenerates
   `ipc_server_generated.c` with the new dispatch fork. Existing
   modules that listed the name in `Implemented_Functions` start
   being dispatched into without re-installation.

## Why hand-write adapters

`[Planned]` v0.3+: a `gen_thunks.py` step that reads the OpenXR
`xr.xml` registry and emits adapters automatically. Until then
the adapters are hand-written.

The set of IPC calls that actually want module overrides is small
-- head pose, view locations, hand tracking, action poses, on the
order of 20 calls. Generating all 250 OpenXR functions adds
compile and link time that no current module benefits from.

## The `aug_has_modules_for` probe

Implemented in `src/xrt/augins/dispatch.cpp`. The dispatch
registry is `std::unordered_map<std::string, std::vector<entry>>`;
the probe is one `count` lookup. Called once per IPC message.

If the registry is empty for a name, the IPC call goes to the
runtime default unchanged. Modules are opt-in per call; the
runtime does not route through chains that do not exist.

## Adapter list (v0.2 base)

| IPC call | Module-facing name | Adapter | Filter |
|----------|-------------------|---------|--------|
| `space_locate_space` | `xrLocateSpace` | `aug_adapter_space_locate_space` | None |
| `space_locate_device` | `aug_LocateDeviceInSpace` | `aug_adapter_space_locate_device` | Head xdev only |

Further entries land during v0.2.x. See [Roadmap](Roadmap.md).
