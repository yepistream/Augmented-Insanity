<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# IPC Hook Dispatch

How `augins_fire_hooks` works, and how to add a new hook.

## Why hooks fire on the service side

Monado's OpenXR loader runs in the client process and forwards calls
across an IPC bridge to the runtime service. Aug-Ins fires hooks on
the **service** side, not the client side, for two reasons:

- The service has the authoritative reply state from the real
  Monado dispatch. Hooks see the relation, the views, the joint
  set, etc., and can override what they want.
- The service is the only place where modules can coexist. Client
  processes do not see modules at all -- they only see the runtime
  through the standard OpenXR ABI.

## The dispatch table

```cpp
// src/xrt/augins/augins_dispatch.cpp
std::unordered_map<std::string, std::vector<aug_module_func_entry>>
g_dispatch_table;

struct aug_module_func_entry {
    void *symbol;             // dlsym result
    std::string module_id;
    std::string module_name;
    int priority;             // from manifest, default 50
};
```

When a module loads, its `Implemented_Functions` are walked; for each
name, `dlsym(handle, name)` is called and the resulting pointer is
appended to `g_dispatch_table[name]` along with module identity and
priority.

## `augins_fire_hooks`

```cpp
// src/xrt/augins/augins_dispatch.cpp
XrResult
augins_fire_hooks(const std::string &xr_fn_name,
                  void *a0, void *a1, void *a2, void *a3)
{
    std::vector<aug_module_func_entry> entries;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        auto it = g_dispatch_table.find(xr_fn_name);
        if (it == g_dispatch_table.end()) return XR_SUCCESS;
        entries = it->second; // snapshot under lock
    }

    using generic_fn = int32_t (*)(void *, void *, void *, void *);
    for (const auto &entry : entries) {
        auto fn = reinterpret_cast<generic_fn>(entry.symbol);
        int32_t aug_ret = fn(a0, a1, a2, a3);
        // ...handle AUG_OK / AUG_FATAL_MODULE / AUG_FATAL_RUNTIME...
    }
    return XR_SUCCESS;
}
```

The snapshot-then-iterate pattern keeps the lock held only briefly,
so a module returning `AUG_FATAL_MODULE` can mutate
`g_dispatch_table` without a deadlock.

The four `void *` arguments are the calling convention every hook
must accept:

- `a0` = `volatile struct ipc_client_state *` (opaque session
  context).
- `a1` = IPC msg struct pointer (deserialised input args), or
  `NULL` if the call has none.
- `a2` = IPC reply struct pointer (writable output args), or
  `NULL` if the call has none.
- `a3` = reserved, currently `NULL`.

## Where the call originates

The generated `ipc_server_generated.c` (output of
`src/xrt/ipc/shared/proto.py`) contains the per-IPC-call dispatch
switch. Each case looks like:

```c
case IPC_SPACE_LOCATE_DEVICE: {
    struct ipc_space_locate_device_msg *msg =
        (struct ipc_space_locate_device_msg *)ipc_command;
    struct ipc_space_locate_device_reply reply = {0};

    reply.result = ipc_handle_space_locate_device(
        ics, msg->base_space_id, &msg->base_offset,
        msg->at_timestamp, msg->xdev_id, &reply.relation);

#ifdef XRT_FEATURE_AUG_INS
    augins_fire_hooks("xrLocateViews", (void *)ics,
                      (void *)msg, (void *)&reply, NULL);
#endif

    xrt_result_t xret = ipc_send(...);
    return xret;
}
```

Hooks fire AFTER the real Monado handler computes the reply but
BEFORE the reply is sent back to the client.

## The `aug_ipc_to_xr` mapping

`src/xrt/ipc/shared/proto.py` declares:

```python
aug_ipc_to_xr = {
    "session_create":            "xrCreateSession",
    "session_begin":             "xrBeginSession",
    "session_end":               "xrEndSession",
    "session_destroy":           "xrDestroySession",
    "session_request_exit":      "xrRequestExitSession",
    "session_poll_events":       "xrPollEvent",
    "compositor_predict_frame":  "xrWaitFrame",
    "compositor_begin_frame":    "xrBeginFrame",
    "compositor_end_frame":      "xrEndFrame",
    "swapchain_create":          "xrCreateSwapchain",
    "swapchain_destroy":         "xrDestroySwapchain",
    "swapchain_acquire_image":   "xrAcquireSwapchainImage",
    "swapchain_wait_image":      "xrWaitSwapchainImage",
    "swapchain_release_image":   "xrReleaseSwapchainImage",
    "space_locate_space":        "xrLocateSpace",
    "space_locate_spaces":       "xrLocateSpaces",
    "space_locate_device":       "xrLocateViews",
    "space_create_semantic_ids": "aug_spaceCreateSemanticIds",
    "device_get_tracked_pose":   "aug_deviceGetTrackedPose",
}
```

Keys: IPC call names (snake_case, matching the JSON files in
`src/xrt/ipc/shared/proto/`).

Values: the names modules must use in their
`metadata.json::Implemented_Functions` and as their `extern "C"`
symbol names. Most are real OpenXR function names; some are
synthetic `aug_*` names for IPC calls that have no direct OpenXR
analogue.

## Adding a new hook

If a module needs to intercept an IPC call that is not currently in
the table:

1. Add the IPC name + xr name to `aug_ipc_to_xr` in
   `src/xrt/ipc/shared/proto.py`. Use a synthetic
   `aug_<descriptive>` name if there is no OpenXR analogue.
2. Rebuild the runtime; `proto.py` regenerates
   `ipc_server_generated.c` with the new
   `augins_fire_hooks(...)` call.
3. If the IPC reply requires a host-API helper to safely modify
   (because its struct layout is generated and modules cannot
   include the generated header), add a setter to
   `augins_host_api.cpp`. Bump `AUG_HOST_API_VERSION` if doing so
   adds a new field to the table.
4. Update [Host API Reference](Host-API-Reference.md) and this
   page.

## Example: the `aug_deviceGetTrackedPose` hook

This is the synthetic hook name for the `device_get_tracked_pose`
IPC call. It backs the half of `xrLocateViews` that asks the head
xdev for its pose in its own tracking-origin frame
(`T_xdev_head`). The arcore-headpose module hooks it to overwrite
the gyro+accel-fused pose with ARCore's VIO-tracked pose:

```cpp
int32_t
aug_deviceGetTrackedPose(void *ics, void *msg, void *reply, void *unused)
{
    const auto *m = static_cast<const struct aug_msg_device_get_tracked_pose *>(msg);
    if (m->name != XRT_INPUT_GENERIC_HEAD_POSE) return AUG_OK;

    struct xrt_space_relation rel = /* ARCore's pose */;
    g_host->set_locate_space_relation(reply, &rel);
    return AUG_OK;
}
```

The mirror struct technique avoids pulling generated IPC headers
into a `.augins` build.

## Diagnostic: which hooks fired?

Modules typically log on entry to make this visible. Filter logcat
to the relevant module tag:

```
adb logcat -s "AugIns.ARCore:*" "AugIns.MercuryARCore:*" "AugInsHeadSway:*"
```

A successful hook fire produces a single log line per IPC call (or
should -- see [Known Issues](Known-Issues.md) for the 11x
duplication bug).
