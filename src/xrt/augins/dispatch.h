// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * @file
 * @brief  Aug-Ins dispatch registry: name -> registered modules.
 *
 * The runtime IPC dispatcher consults `aug_has_modules_for(name)` per
 * call to decide between calling its own default handler or the
 * generated per-function adapter. The adapter then walks the modules
 * registered for that name via `aug_get_modules_for()` in priority
 * order and calls each one with the OpenXR-shaped args.
 *
 * Population: the loader (loader.cpp) fills the registry at service
 * startup via the internal `aug_dispatch_register_*` helpers, then
 * sorts each name's vector by Priority. After that the registry is
 * read-only for the lifetime of the service.
 *
 * Internal header. Not part of the module ABI.
 *
 * @ingroup aug_runtime
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * One entry in the dispatch map for a given OpenXR function name.
 * The adapter walks an array of these in priority order.
 */
struct aug_module_entry
{
	void       *fn;        //!< Raw symbol from dlsym, cast by adapter to PFN_xrFoo.
	const char *module_id; //!< Borrowed pointer to the module's manifest ID. NUL-terminated.
	const char *data_dir;  //!< Borrowed pointer to the module's extracted data dir. NUL-terminated.
};


/*!
 * Fast yes/no probe: does any module have a function registered for
 * this OpenXR name? Called once per IPC dispatch. Returns false if
 * the loader has not populated the registry yet (no modules loaded
 * means no overrides means runtime default always runs).
 */
bool
aug_has_modules_for(const char *xr_name);


/*!
 * Fills `out_buf` (capacity `out_capacity`) with the module entries
 * registered for `xr_name`, in dispatch (priority) order. Returns the
 * number of entries written -- which may be less than the total if
 * `out_capacity` was undersized. Pass 0/NULL to query the count.
 */
size_t
aug_get_modules_for(const char *xr_name,
                    struct aug_module_entry *out_buf,
                    size_t out_capacity);


/*
 * ---------------------------------------------------------------------
 * Internal: only the loader and lifecycle code call these. Module
 * authors never see them.
 * ---------------------------------------------------------------------
 */

/*!
 * Register one (function name -> module symbol) pairing. Called by
 * the loader for each name in a module's Implemented_Functions array
 * that resolves to a real exported symbol. `module_id` and `data_dir`
 * pointers must remain valid for the lifetime of the registration
 * (loader keeps them in its loaded-modules list, which outlives the
 * dispatch registry).
 */
void
aug_dispatch_register(const char *xr_name,
                      void       *fn,
                      const char *module_id,
                      const char *data_dir,
                      int         priority);

/*!
 * Sort each name's vector by Priority (ascending: lower number runs
 * earlier in the chain, which combined with last-write-wins means
 * higher Priority effectively wins conflicts). Called once by the
 * loader after all modules have been registered.
 */
void
aug_dispatch_sort_by_priority(void);

/*!
 * Drop every registered entry. Called from the loader on shutdown
 * before any .so handle gets dlclose'd.
 */
void
aug_dispatch_clear(void);


#ifdef __cplusplus
} // extern "C"
#endif
