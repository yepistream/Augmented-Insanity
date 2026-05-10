<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Module System

How the runtime discovers, orders, loads, and lifecycles `.augins`
modules.

Code paths:

- `src/xrt/augins/augins_dispatch.{cpp,h}` -- loader, dispatch table.
- `src/xrt/augins/augins_lifecycle.{cpp,h}` -- lifecycle dispatcher.
- `src/xrt/augins/augins_module_abi.h` -- the public ABI module authors
  include.

## The `.augins` file format

A `.augins` is a zip archive with no enforced compression scheme.
Mandatory entries:

- `<module-id>.so` -- the module's native code, where `<module-id>` is
  the `ID` field from `metadata.json`. The `.so` filename matters
  because the loader does `dlopen("<module-id>.so", ...)`. No `lib`
  prefix.
- `metadata.json` -- the manifest. See
  [Manifest Schema](Manifest-Schema.md).

Optional entries:

- `settings.json` -- per-module settings; opaque to the runtime. The
  module reads it from its own data dir.
- Any other file the module wants to bundle: ONNX models, calibration
  JSON, license texts, additional `.so` siblings (e.g.
  `libarcore_sdk_c.so` for the ARCore module).

## Discovery

On service start, `augins::launch(modules_dir, cache_dir)` is called.
Where:

- `modules_dir` is `/data/data/com.augmented_insanity.runtime.out_of_process/files/modules/`,
  the directory the runtime watches for `.augins` files.
- `cache_dir` is `.../cache/opennedmodules/`, where each `.augins` is
  extracted (one subdirectory per module ID). Per-module data dirs
  exposed via the host API's `get_module_data_dir()` point here.

Every `.augins` file in `modules_dir` is enumerated and its
`metadata.json` parsed. Files that fail to parse are skipped with a
warning.

## Dependency resolution

After parsing, modules are sorted topologically by their `Dependencies`
field. Algorithm: Kahn's algorithm; modules with zero pending deps are
loaded first, dependents follow.

If module A's `Dependencies` lists module B and B is not present, A is
skipped with a clear log line. Cyclic dependencies cause every module
in the cycle to be skipped (the runtime stays healthy).

The resolved load order is logged at INFO so debugging is trivial:

```
Aug-Ins: Module load order: noop arcore_headpose mercury_handtracking_arcore
```

## Loading

For each module in load order:

1. Sibling `.so` files inside the `.augins` zip (anything *not* the
   main `<module-id>.so`) are pre-`dlopen`ed with
   `RTLD_NOW | RTLD_GLOBAL`. This lets the main module's `DT_NEEDED`
   entries resolve. Used for ARCore SDK and other vendored
   dependencies.
2. The main module is `dlopen`ed with `RTLD_NOW`.
3. Lifecycle symbols are resolved via `dlsym`:
   `aug_onModuleLoad`, `aug_runtimeInit`, `aug_onConnect`,
   `aug_runtimeFinished`, etc. Missing symbols are silently skipped
   (each callback is optional).
4. Hook symbols listed in `metadata.json::Implemented_Functions` are
   `dlsym`'d and registered in `g_dispatch_table[name]`.
5. Manifest-advertised features (`Advertised_OpenXR_Features.Extensions`,
   `SystemPropertyBits`) are unioned into the global aggregator.
6. The module is appended to `g_loaded_modules`.

After all modules are loaded, the runtime fans out the
`aug_onModuleLoad` callback to every module, passing the host API
table as `args`.

## Lifecycle

Callbacks fire in this order, fanned out across all loaded modules at
each step:

1. `aug_onModuleLoad(args)` -- once at startup. `args` is
   `const struct aug_host_api *`. Modules cache the pointer here.
2. `aug_runtimeInit(NULL)` -- once at startup, after dispatch table
   is fully built.
3. `aug_onConnect(NULL)` -- on each new client connection.
4. **Per-IPC-call hooks** -- `augins_fire_hooks("xrLocateSpace", ...)`
   etc. for every IPC call mapped in `proto.py`'s `aug_ipc_to_xr`.**[Planned changes to allow all IPC-calls, currently I'm doing it this way for ease of testing]**
5. `aug_runtimeFinished(NULL)` -- once at shutdown, after the IPC
   server has stopped accepting clients and the back-buffer worker
   has joined.

Optional back-buffer thread callbacks
(`aug_backBufferUpdateLoop`, `...Pause`, `...Resume`) run on a
dedicated thread the runtime spawns; intended for slow per-second
work that must not block the IPC dispatch.

## Three module roles

A module typically plays one or more of three roles:

- **Hook implementer.** Lists IPC hook names in
  `Implemented_Functions` and exports the corresponding `extern "C"`
  symbols. Reads or modifies IPC replies. Example: head-sway example.
- **Capability producer.** Calls
  `g_host->register_hand_tracker(cb)` and friends. The runtime stub
  xdevs route queries to the registered callback. Requires a
  matching `Advertised_OpenXR_Features.SystemPropertyBits` entry.
  Example: Mercury hand-tracking module.
- **Frame broker producer or subscriber.** Calls
  `g_host->publish_camera_frame_y8(...)` or
  `g_host->subscribe_camera_frame(...)` to share Y-plane luminance
  frames across modules. Producer thread drives subscribers
  synchronously. Example: arcore-headpose publishes; mercury-arcore
  subscribes.

A single module can play all three roles at once.

## Aborting modules

If a hook returns `AUG_FATAL_MODULE`, the runtime removes that
module from `g_dispatch_table` and from `g_loaded_modules` and
continues dispatching to the rest. The module's process state
remains; only its hooks stop firing.

`AUG_FATAL_RUNTIME` aborts the whole service process. Reserve for
unrecoverable errors that compromise other modules and *(even thous seperate by a whole layer, but just in case)* the runtime.

## See also

- [Manifest Schema](Manifest-Schema.md) for `metadata.json` fields.
- [Host API Reference](Host-API-Reference.md) for the function
  table modules receive.
- [IPC Hook Dispatch](IPC-Hook-Dispatch.md) for the dispatch
  details.
