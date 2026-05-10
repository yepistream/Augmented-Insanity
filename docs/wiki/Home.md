<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Augmented Insanity Developer Wiki


## Where to start

- **New to the project?** Read the
  [Architecture Overview](Architecture-Overview.md) first.
- **Want to write a module?** Skim
  [Module System](Module-System.md), then walk through
  [Module Example Walkthrough](Module-Example-Walkthrough.md) and start from
  there.
- **Want to contribute to the runtime/fork?** Read
  [Architecture Overview](Architecture-Overview.md) and
  [IPC Hook Dispatch](IPC-Hook-Dispatch.md), then
  [Building The Runtime](Building-The-Runtime.md).

## Architecture and core systems

| Page | What it covers |
|------|----------------|
| [Architecture Overview](Architecture-Overview.md) | Runtime/module split, dispatch flow, ASCII diagram. |
| [Module System](Module-System.md) | Module lifecycle, dependency resolution, three module roles. |
| [Manifest Schema](Manifest-Schema.md) | Reference for `metadata.json`. |
| [Host API Reference](Host-API-Reference.md) | The `aug_host_api` table. |
| [IPC Hook Dispatch](IPC-Hook-Dispatch.md) | How `augins_fire_hooks` works, how to add a new hook. |
| [Stub Xdev Factory](Stub-Xdev-Factory.md) `[WIP]` | How module-advertised features become `xrt_device` instances. |
| [Camera Frame Broker](Camera-Frame-Broker.md) | Single-producer multiple-subscriber Y-plane broker. |

## Build and ship

| Page | What it covers |
|------|----------------|
| [Building The Runtime](Building-The-Runtime.md) | Android Studio prereqs, NDK 26, CMake 3.22, `xr-deps`, Gradle build, install via adb. |
| [Building A Module](Building-A-Module.md) | Standalone NDK CMake vs in-tree builds. Manifest fields. Packaging into `.augins`. |
| [Module Example Walkthrough](Module-Example-Walkthrough.md) | Line-by-line tour of `module-example/augins-head-sway/`. |

## Module references

| Page | What it covers |
|------|----------------|
| [ARCore Module Reference](ARCore-Module-Reference.md) `[WIP]` | The `augins-arcore-headpose` module: hooks, broker producer, ARCore session ownership. |
| [Mercury Module Reference](Mercury-Module-Reference.md) `[WIP]` | The `augins-mercury-handtracking-arcore` module: ONNX inference, lazy subscribe, frame decimation. |
| [Calibration Activity](Calibration-Activity.md) `[NOT IMPLEMENTED]` | Planned per-module camera-intrinsics calibration UI. |

## Status, issues, roadmap

| Page | What it covers |
|------|----------------|
| [Implementation Status](Implementation-Status.md) | Single-table summary of what works. |
| [Known Issues](Known-Issues.md) | Bugs I'm are aware of. |
| [Roadmap](Roadmap.md) | What is planned, short to long term. |
