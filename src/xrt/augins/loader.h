// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * @file
 * @brief  Aug-Ins module loader entry points.
 *
 * The service calls `augins_loader_init()` once at startup, with the
 * paths the runtime APK Java side passed across to the native side
 * (modules_dir holds the installed .augins files; cache_dir holds
 * per-module extracted asset trees). Loader scans, parses, dlopens,
 * dlsyms, populates the dispatch registry, and fans out
 * `aug_on_module_load` to every accepted module.
 *
 * `augins_loader_shutdown()` runs at service teardown: fans out
 * `aug_on_module_unload`, then `dlclose`s every handle and clears the
 * dispatch registry.
 *
 * Internal header. Not part of the module ABI.
 *
 * @ingroup aug_runtime
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Scan, parse, dlopen, dlsym, register, and fire `aug_on_module_load`
 * for every .augins discovered under `modules_dir` (and any APK-
 * bundled fallback, when that lands in Phase 2a's follow-up). Builds
 * the dispatch registry. Idempotent: calling more than once is safe
 * but does nothing past the first call.
 *
 * Errors are non-fatal: a broken module is logged and skipped; the
 * service keeps running with whatever else loaded successfully. See
 * the Q2e load-time error policy in the v0.2 design notes.
 *
 * Either path may be NULL or empty -- the loader treats that as "no
 * modules to load from that source" and continues with the others.
 */
void
augins_loader_init(const char *modules_dir, const char *cache_dir);


/*!
 * Fire `aug_on_module_unload` on every loaded module (in reverse load
 * order), then dlclose every handle and clear the dispatch registry.
 * Safe to call before `augins_loader_init` (no-op) or twice in a row
 * (second call is no-op).
 */
void
augins_loader_shutdown(void);


#ifdef __cplusplus
} // extern "C"
#endif
