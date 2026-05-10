<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Host API Reference

The host API is the function-pointer table the runtime hands to every
module at `aug_onModuleLoad` time. Modules call back into the runtime
through this table; they do not link directly against runtime symbols.

Source-of-truth header:
`src/xrt/augins/augins_module_abi.h`. This page summarises that header;
when in doubt, read the header.

## Versioning

```c
#define AUG_HOST_API_VERSION 2u
```

Forward-compatible, additive only:

- The runtime advertises its host-API version in the table's `version`
  field.
- Each module declares its required minimum version and MUST check
  `api->version >= module_required_version` on entry to
  `aug_onModuleLoad`. A newer runtime accepts older modules (because
  fields are only ever added at the end). An older runtime cannot
  serve newer modules; the module aborts itself.
- New fields go AT THE END of `aug_host_api`. Never reorder, rename,
  or change the meaning of an existing field -- doing so breaks every
  installed `.augins` on disk.

## Receiving the table

```c
extern "C" void
aug_onModuleLoad(void *args)
{
    const auto *api = static_cast<const struct aug_host_api *>(args);
    if (api == nullptr || api->version < 1u) { /* abort module */ }
    g_host = api; // store for later use
}
```

Cache the pointer; the table itself is valid for the lifetime of the
runtime.

## 0.1.x (unchanged in later versions)

### `set_locate_space_relation(reply, relation)`

```c
void (*set_locate_space_relation)(void *reply,
                                  const struct xrt_space_relation *relation);
```

Writes `relation` into a reply struct of type
`struct ipc_space_locate_space_reply *` (or any IPC reply with the
same prefix layout, currently also
`ipc_device_get_tracked_pose_reply`). Sets `reply->result =
XRT_SUCCESS`.

The runtime owns the cast; modules pass the opaque `reply` pointer
they received as `a2` in their hook. This is how a hook overrides
head pose in `xrLocateSpace` or `aug_deviceGetTrackedPose`.

### `get_vm()` -> `JavaVM *`

```c
void *(*get_vm)(void);
```

Returns the service process's `JavaVM *`. Cast the return value to
`JavaVM *`. Lifetime: process. Use to attach a worker thread to the
JVM (`vm->AttachCurrentThread(...)`).

### `get_context()` -> `jobject` (Android Context)

```c
void *(*get_context)(void);
```

Returns the service process's application `Context` as a `jobject`.
Cast the return. Lifetime: process. Wrap in a global ref before
storing on a worker thread.

## 0.2.x (additive)

### `register_hand_tracker(cb, userdata)`

```c
typedef void (*aug_hand_get_joints_fn)(uint32_t handed,
                                       int64_t at_timestamp_ns,
                                       struct xrt_hand_joint_set *out,
                                       int64_t *out_timestamp_ns,
                                       void *userdata);

void (*register_hand_tracker)(aug_hand_get_joints_fn cb, void *userdata);
```

Installs the producer callback for the runtime's stub hand-tracker
xdevs. Pass `cb = NULL` to unregister; while no callback is
registered the stub xdevs return `is_active = false` to clients.

For the registration to have any effect, the calling module must
also advertise `"Advertised_OpenXR_Features.SystemPropertyBits":
["handTracking"]` in its manifest, so the stub xdevs are actually
fabricated.

The callback fires on the IPC server's per-client thread when a
client invokes `xrLocateHandJointsEXT`. Implementations should not
do heavy work inline; offload to a worker thread (see
`samples/augins-mercury-handtracking-arcore/mercury_arcore_module.cpp`
for the pattern).

### `publish_camera_frame_y8(...)`

```c
void (*publish_camera_frame_y8)(const uint8_t *y_data,
                                uint32_t width,
                                uint32_t height,
                                uint32_t stride_bytes,
                                int64_t timestamp_ns,
                                const struct aug_camera_intrinsics *intr);
```

Producer side of the camera-frame broker. The producer passes a
single Y-plane luminance frame (8 bits per pixel, `stride_bytes`
between row starts, `width * height` valid pixels). The runtime
fans out the call to every subscriber synchronously on the
producer's thread. Pointer is borrowed for the duration of the call;
subscribers must memcpy if they need to retain.

`intr` may be `NULL` if the producer does not yet know the camera
intrinsics. Subscribers that need intrinsics should drop frames
without intrinsics and wait.

See [Camera Frame Broker](Camera-Frame-Broker.md) for the design
contract.

### `subscribe_camera_frame(cb, userdata)`

```c
typedef void (*aug_camera_frame_cb)(const uint8_t *y_data,
                                    uint32_t width,
                                    uint32_t height,
                                    uint32_t stride_bytes,
                                    int64_t timestamp_ns,
                                    const struct aug_camera_intrinsics *intr,
                                    void *userdata);

void (*subscribe_camera_frame)(aug_camera_frame_cb cb, void *userdata);
```

Subscriber side of the camera-frame broker. Pass `cb = NULL` (with
the same `userdata`) to unsubscribe. Capped at 8 simultaneous
subscribers.

### `get_module_data_dir()` -> `const char *`

```c
const char *(*get_module_data_dir)(void);
```

Returns the per-module extraction directory of the *calling* module
-- the directory the `.augins` zip was unpacked into. Use it to
load assets bundled inside the zip (ONNX models, calibration
JSON, etc.).

Lifetime: process; do not free.

**Implementation note:** the runtime uses thread-local state set
during lifecycle dispatch, so this only returns the right value
when called *from* a module lifecycle callback
(`aug_onModuleLoad`, `aug_onConnect`, `aug_runtimeFinished`, ...).
If you need the dir on a worker thread, capture it inside
`aug_onModuleLoad` and store it in a module global. Calling from
threads outside lifecycle dispatch returns the empty string.

## Return-code contract for hooks

Every IPC hook returns `int32_t`:

| Value | Name | Meaning |
|------:|------|---------|
| `0` | `AUG_OK` | Success. Runtime continues to the next module. |
| `-1` | `AUG_FATAL_RUNTIME` | Aborts the whole runtime process. |
| `-2` | `AUG_FATAL_MODULE` | Removes this module from the dispatch table; other modules continue. |
| `> 0` | -- | Developer-defined warning. Logged, then continue. |
| `< -2` | -- | Developer-defined error. Logged, then continue. |

`AUG_OK` is the right answer 99% of the time. Reserve the fatals for
truly unrecoverable conditions.
