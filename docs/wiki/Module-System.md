<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Module System

Loader behaviour at service start, per-module lifecycle hooks,
and the dispatch contract.

## `.augins` package

A `.augins` file is a zip with at minimum:

```
my-module.augins
  +-- metadata.json            (required, see Manifest-Schema)
  +-- <ID>.so                  (required, the module .so)
  +-- libfoo.so, libbar.so     (optional, dlopen siblings)
  +-- models/, calibration/    (optional, bundled assets)
```

`<ID>` is the `ID` field from the manifest, verbatim. No `lib` prefix.
The loader does `dlopen("<ID>.so", ...)` once it has extracted the
zip; if the filename does not match, the module will not load.

## Where modules go

The service scans
`/data/data/com.augmented_insanity.runtime.out_of_process/files/modules/`
for `*.augins` files. Push your module there with:

```
adb push my-module.augins /data/local/tmp/
adb shell run-as com.augmented_insanity.runtime.out_of_process \
  cp /data/local/tmp/my-module.augins files/modules/
```

The sample modules' `installFooAugins` Gradle tasks automate this.
See [Building-A-Module](Building-A-Module.md).

`[Planned]` v0.2.x: APK-bundled fallback under `assets/modules/`
for default modules that ship with the runtime APK; user-installed
modules with the same `ID` will override APK-bundled ones.

## What the loader does at start

When the service process starts (Java `MonadoService.onCreate`
calls into native via `nativeStartServer`), the loader runs in
this order:

1. Bind host API to the JVM and Application Context. After this,
   `host->get_jvm()` and `host->get_context()` return valid
   handles.
2. Scan the modules dir for `*.augins`. Each entry is processed
   independently; a failure on one entry does not affect others.
3. Extract the zip into a per-module cache subdirectory. A
   size-stamp file (`<entry>.size`) lets a subsequent start skip
   extraction when the source zip is byte-identical.
4. Parse `metadata.json` and validate `Manifest_Version`, `ID`,
   `Version`, `Implemented_Functions`. Bad manifests are logged
   and the module is skipped.
5. Preload sibling `.so` files in the extraction dir with
   `RTLD_NOW | RTLD_GLOBAL` so the main module `.so` resolves
   their symbols.
6. `dlopen <ID>.so` with `RTLD_NOW`. Each name from
   `Implemented_Functions` is `dlsym`'d. Missing symbols are
   warned but do not block the module.
7. Register dispatch entries for each resolved symbol in the
   dispatch map (see Dispatch model below).
8. `dlsym aug_on_module_load` and `aug_on_module_unload` (both
   optional) and store them in the lifecycle list.
9. After all modules are loaded: sort the dispatch map by
   priority, then fire `aug_on_module_load` for each module in
   load order. A non-zero return rejects the module (dispatch
   entries are not yet unwound -- see `[Planned]` note in
   [Roadmap](Roadmap.md)).

At service shutdown the loader fires `aug_on_module_unload` in
reverse order, then dlcloses each handle.

## Lifecycle hooks

Both hooks are optional. A module that does not need worker
threads, ArSessions, or any setup beyond what dlopen already does
can omit both and export only its dispatched functions.

```c
// Called once after dlopen + dispatch entries registered.
// Return non-zero to reject the module (dlclose follows).
int aug_on_module_load(const struct aug_host_api *host);

// Called once at service shutdown, before dlclose. Join workers
// and release resources here.
void aug_on_module_unload(void);
```

Inside `aug_on_module_load`, modules typically:

- Validate `host->struct_version >= AUG_HOST_API_VERSION` (or the
  lower minimum the module actually needs).
- Cache the `host` pointer in a module-private global.
- Capture `host->get_module_data_dir()` if needed on worker threads.
- Spawn background threads (sensor pollers, ArSession ticker, ONNX
  inference workers, ...).

The runtime calls `aug_on_module_load` from the service's main
thread, which is already attached to the JVM. Calling
`DetachCurrentThread` on this thread is a JNI-spec fatal
("attempting to detach while still running code"). Use
`vm->GetEnv(...)` to obtain the `JNIEnv` inside the load hook.

## Dispatch model

Five locked design decisions govern how modules participate in
dispatch. These are not configurable per-module in v0.2 base.

| ID | Rule | Note |
|----|------|------|
| Q1 | Last-write-wins on output conflicts | Modules share a single output struct; later writes survive. |
| Q2 | Runtime default runs before modules | Modules see the baseline output and decorate or overwrite. |
| Q3 | No short-circuit, no exclusivity | Any module can register for any function name. The chain runs to completion unless aborted by Q5. |
| Q4 | Priority field in manifest orders the chain | Lower `Priority` runs earlier. Combined with Q1, higher `Priority` overwrites earlier writers. |
| Q5 | Abort on first non-success XrResult | Subsequent modules in the chain are skipped; the client sees that result. |

`[Planned]` v0.2.x adds a per-module `Failure_Mode` field
(`"abort"` / `"recoverable"` / `"critical"`) for tuning Q5.

## How a name resolves

The `Implemented_Functions` array holds OpenXR function names or
Aug-Ins synthetic names. Examples:

- `"xrLocateSpace"` -- the real OpenXR function. Backed by the
  service-side adapter `aug_adapter_space_locate_space`.
- `"aug_LocateDeviceInSpace"` -- an Aug-Ins synthetic name for a
  function that has no direct OpenXR equivalent (it backs half of
  `xrLocateViews` on the service-side IPC layer). The `aug_` prefix
  marks it as Aug-Ins-specific.

The runtime adapter decides which IPC call routes to which name.
See [Service-Side-Dispatch](Service-Side-Dispatch.md) for the
current `aug_implemented_adapters` set.

A module that lists a name without a registered adapter is loaded
without that entry; the loader logs a warning. Adding the adapter
and rebuilding the runtime activates the existing module's entry
on the next start.

## Per-module data directory

Each module's asset directory:

```
/data/data/com.augmented_insanity.runtime.out_of_process/cache/opennedmodules/_staged/<id>.augins/
```

Available via `host->get_module_data_dir()` from any call the
runtime made into the module (including `aug_on_module_load` and
any dispatched function). The implementation is thread-local
storage pushed and popped by the dispatcher around every call.

A worker thread the module spawned itself does not have the TLS
populated, so `get_module_data_dir()` returns an empty string
there. Capture the path inside `aug_on_module_load` and stash it
in a module-private global before spawning workers.

## Error handling at load

| Failure | Response |
|---------|----------|
| `metadata.json` parse failure | Log, skip module, continue. |
| `Manifest_Version` mismatch | Log, skip module, continue. |
| Zip extraction failure | Log, skip module, continue. |
| dlopen failure | Log, skip module, continue. |
| Symbol in `Implemented_Functions` missing | Warn for that name, register the others, partial load OK. |
| `aug_on_module_load` returns non-zero | Log; dispatch entries currently stay registered. See `[Planned]` cleanup in [Roadmap](Roadmap.md). |

The loader logs and continues on every failure path; one broken
module does not block the rest.
