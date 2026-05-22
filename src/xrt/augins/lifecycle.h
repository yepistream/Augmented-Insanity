// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * @file
 * @brief  Aug-Ins module lifecycle hook fan-out.
 *
 * The loader calls these to invoke `aug_on_module_load` /
 * `aug_on_module_unload` on the set of loaded modules. Split out from
 * loader.cpp so the loader can stay focused on filesystem + dlopen,
 * and the lifecycle code can stay focused on argument marshalling
 * and per-module error policy.
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
 * Invoke `aug_on_module_load` on every loaded module. Called by the
 * loader after all modules have been dlopen'd and their dispatch
 * entries registered, so a module's lifecycle hook can rely on every
 * other module already being mapped.
 *
 * Returns the count of modules whose hook returned non-zero. Those
 * modules are downgraded by the loader (dispatch entries dropped,
 * handle dlclose'd).
 */
int
augins_lifecycle_fire_load(void);


/*!
 * Invoke `aug_on_module_unload` on every loaded module in reverse
 * load order. Called by the loader at service teardown before the
 * .so handles are `dlclose`d.
 */
void
augins_lifecycle_fire_unload(void);


/*
 * ---------------------------------------------------------------------
 * Internal: only the loader calls these.
 * ---------------------------------------------------------------------
 */

/*!
 * Register a freshly-loaded module with the lifecycle bookkeeping.
 * The lifecycle subsystem takes a reference to the strings; they
 * must outlive the registration (loader owns the storage). dlsym'd
 * function pointers may be NULL -- means the module did not export
 * that optional hook.
 */
void
augins_lifecycle_register_module(void       *handle,
                                 const char *module_id,
                                 const char *data_dir,
                                 int (*on_load)(const void *host),
                                 void (*on_unload)(void));


/*!
 * Unregister and dlclose every loaded module. Called from the loader's
 * shutdown path AFTER `augins_lifecycle_fire_unload`. Clears the
 * internal list.
 */
void
augins_lifecycle_dlclose_all(void);


/*!
 * Read-only count, mostly for diagnostics / status JNI.
 */
int
augins_lifecycle_module_count(void);


#ifdef __cplusplus
} // extern "C"
#endif
