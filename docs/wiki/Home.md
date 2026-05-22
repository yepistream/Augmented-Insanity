<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Augmented Insanity Wiki

Augmented Insanity is a fork of [Monado](https://gitlab.freedesktop.org/monado/monado)
that adds a service-side module system. Modules are `.augins` zip
packages dropped into the runtime APK's modules dir; the runtime
discovers them at start, validates them, calls their lifecycle hooks,
and routes selected OpenXR IPC calls through them via hand-written
adapters.

This wiki is the developer-facing documentation. For a user-facing
"install the APK and run an OpenXR app" walkthrough, see the repo
[README](../../README.md).

Status: v0.2 `[WIP]`. The runtime, module loader, dispatch chain,
and the ARCore head-pose module are working. Pages marked `[WIP]`
or `[Planned]` document subsystems that do not yet exist or are
incomplete.

---

## Index

### Concepts

- [Architecture-Overview](Architecture-Overview.md) -- client process,
  service process, where modules live, how a call flows from
  `xrLocateViews` to a module function.
- [Module-System](Module-System.md) -- lifecycle hooks, dispatch
  ordering (Q1 through Q5), `.augins` package format, what the
  loader does at start.
- [Service-Side-Dispatch](Service-Side-Dispatch.md) -- the
  `proto.py` codegen and hand-written adapter pattern that route
  IPC calls into modules.

### Reference

- [Manifest-Schema](Manifest-Schema.md) -- `metadata.json` field by
  field, with examples from the bundled ARCore module.
- [Host-API-Reference](Host-API-Reference.md) -- the `aug_host_api`
  v1 table modules receive in `aug_on_module_load`. Source of truth
  is `src/xrt/augins/module_abi.h`.

### Building

- [Building-The-Runtime](Building-The-Runtime.md) -- prerequisites,
  Android Studio config, `gradlew assembleOutOfProcessDebug`, install
  to device via adb.
- [Building-A-Module](Building-A-Module.md) -- standalone NDK CMake
  build of a `.augins` module, packaging task, push to device.
- [Module-Example-Walkthrough](Module-Example-Walkthrough.md) --
  annotated read-through of `module-example/augins-head-sway/`,
  the head-sway tutorial module.

### Modules

- [ARCore-Module-Reference](ARCore-Module-Reference.md) -- the
  bundled ARCore head-pose module. First production v0.2 module.

### Project status

- [Implementation-Status](Implementation-Status.md) -- single source
  of truth: what works, what is `[WIP]`, what is `[Planned]`, what is
  `[Broken]`.
- [Known-Issues](Known-Issues.md) -- bugs and limitations I have
  observed but not yet fixed.
- [Roadmap](Roadmap.md) -- planned v0.2.x and v0.3+ work.

### Credits

- [Acknowledgements](Acknowledgements.md) -- upstream projects,
  vendored libraries, and authors this fork depends on.

---

Wiki source is checked into the repo under `docs/wiki/`. The GitHub
Wiki mirrors those files.
