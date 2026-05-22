<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Host API Reference

The host API table is the function-pointer struct the runtime
passes to `aug_on_module_load`. Modules cache the pointer and use
it for JVM access, the per-module data path, and logging.

Source of truth: `src/xrt/augins/module_abi.h`. The struct
definition lives there; this page documents semantics.

## Versioning

Forward-compatible by appending. The `struct_version` field bumps
every time a function pointer is added at the end of the struct.
v0.2 ships v1.

```c
#define AUG_HOST_API_VERSION 1u   // from module_abi.h
```

A module checks `host->struct_version >= <minimum>` before
reading any field that requires a higher version. v1-compatible
modules check `>= 1`.

A v2 runtime accepts a v1 module (older fields are unchanged). A
module that needs v2 fields and is handed a v1 runtime must
reject the load by returning non-zero from `aug_on_module_load`.

## v1 fields

Declarations are in `src/xrt/augins/module_abi.h`. Per-field
semantics:

### `void *(*get_jvm)(void)`

Returns the runtime service process's `JavaVM *` as a `void *`.
Cast at the call site. Returns NULL on non-Android builds (none
exist yet; the field is reserved).

Lifetime: process. Thread-safe. Do not free.

### `void *(*get_context)(void)`

Returns the runtime service process's Android Application Context
as a `jobject` (cast to `void *`). Cast back to `jobject` before
JNI use.

The reference is a global ref owned by the runtime. Modules must
not call `DeleteGlobalRef` on it. A worker thread that outlives
`aug_on_module_load` should `NewGlobalRef` its own copy and
manage that reference itself.

Lifetime: process. Thread-safe.

### `const char *(*get_module_data_dir)(void)`

Returns the absolute path of the calling module's extracted assets
directory (where the runtime unpacked the `.augins` contents).
Used to load bundled ONNX models, calibration JSON, and any other
files shipped inside the zip.

The path is set via thread-local storage by the dispatcher around
every call into a module. A worker thread the module spawned
itself does not have the TLS populated and gets `""` back. Capture
the path inside `aug_on_module_load` and stash it in a
module-private global before spawning workers.

The returned pointer is to a runtime-owned string. Do not free.

### `void (*log)(int level, const char *fmt, ...)`

Routes a log line through the runtime's logging system.

Level mapping:
- `0` -- trace
- `1` -- debug
- `2` -- info
- `3` -- warn
- `4` -- error

Modules may use `__android_log_print` directly under their own
tag instead; this entry exists for log style consistent with
runtime output.

Thread-safe.

## Not present in v1

The following exist in v0.1 but were removed and have not yet been
re-added to v0.2:

- `set_locate_space_relation` and other mirror-struct helpers.
  The v0.1 IPC-reply-patching contract is gone; v0.2 modules
  receive OpenXR-shaped arguments via the adapter layer.
- `register_hand_tracker`. v0.1 stub-xdev factory hook. The
  replacement is an `xrCreateHandTrackerEXT` adapter, scheduled
  with the Mercury v0.2 rewrite.
- Camera frame broker (`publish_camera_frame_y8`,
  `subscribe_camera_frame`). v0.1 cross-module frame stream.
  Returns when Mercury needs it (see [Roadmap](Roadmap.md)).
- `xdev` accessors. Modules cannot query Monado xdev state
  directly. Where "which device is this" matters, the adapter
  filters before dispatch (e.g. `aug_LocateDeviceInSpace` fires
  only for head queries).

Entries are added to the table when a concrete module requires
them. The table grows during v0.2.x.

## Pitfalls

### Detaching the main thread

`aug_on_module_load` runs on the service's main Android thread,
which the JVM has already attached. Calling
`vm->DetachCurrentThread()` here is a JNI-spec fatal
("attempting to detach while still running code"). Use `GetEnv`
to fetch the existing `JNIEnv`:

```c
JNIEnv *env = NULL;
jint rc = vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
if (rc != JNI_OK || env == NULL) {
    return 1; // unexpected on the main thread
}
// use env...
// do NOT call DetachCurrentThread here
```

In worker threads the module spawned itself, the
`AttachCurrentThread` / `DetachCurrentThread` pair is required.

### Asset path on worker threads

`get_module_data_dir()` from a module-spawned worker thread
returns `""`. Capture the path inside `aug_on_module_load`:

```c
static std::string g_data_dir;

int aug_on_module_load(const struct aug_host_api *host) {
    g_data_dir = host->get_module_data_dir(); // populated here
    g_worker = std::thread(worker_main);      // worker reads g_data_dir
    return 0;
}
```

### Host pointer lifetime

The host pointer is valid until `aug_on_module_unload` returns.
Workers must be joined inside the unload hook.
