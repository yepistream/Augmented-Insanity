// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * @file
 * @brief  Aug-Ins v0.2 public module ABI.
 *
 * THIS is the header that .augins module authors include. Everything in
 * here is part of the public, version-stable ABI between modules and
 * the runtime. Internal runtime headers (loader.h, dispatch.h, ...) do
 * not appear here on purpose -- module authors should not see them.
 *
 * A module:
 *   1. Includes <augins/module_abi.h> and <openxr/openxr.h>.
 *   2. Optionally exports `aug_on_module_load(host)` and
 *      `aug_on_module_unload()`.
 *   3. Exports real OpenXR-signature functions for any name listed in
 *      its metadata.json `Implemented_Functions` array.
 *   4. Ships the .so alongside the metadata.json (and any bundled
 *      assets) in a `.augins` zip in the runtime's modules dir.
 *
 * Dispatch contract: the runtime calls its own default handler first,
 * then iterates registered modules in `Priority` order (lower number
 * runs earlier). Multiple modules may register for the same name; the
 * last one to write a given output field wins. There is no short-
 * circuit -- the chain always runs to completion unless a module
 * returns a non-success XrResult, in which case the chain aborts and
 * the client sees that result.
 *
 * Compatibility: see `AUG_MANIFEST_VERSION` and the
 * `struct aug_host_api::struct_version` field. Both bump independently
 * when the runtime adds new manifest fields or new host API entries.
 *
 * @ingroup aug_module_abi
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Manifest schema version. A module's `metadata.json` must contain
 * `"Manifest_Version": 1` to be accepted by a runtime built against
 * this header. Mismatch -> loader rejects the module with a clear log
 * line.
 */
#define AUG_MANIFEST_VERSION 1


/*!
 * Host API table handed to every module via `aug_on_module_load`.
 *
 * Forward-compatible by appending: `struct_version` bumps every time
 * a field is added. Modules MUST check `struct_version` before reading
 * any field added after baseline (v1) and gracefully fall back if the
 * runtime they are loaded into is older than required.
 *
 * Every function pointer is guaranteed non-null by the runtime; modules
 * do not need to null-check. Threading: see per-field doc.
 */
struct aug_host_api
{
	/*!
	 * Bumped when fields are added at the end of this struct. Modules
	 * check this before reading any field beyond the v1 baseline.
	 */
	uint32_t struct_version;

	/*!
	 * Reserved for alignment of subsequent 8-byte fields.
	 */
	uint32_t _reserved;

	/*
	 * v1 (initial v0.2 baseline) ----------------------------------
	 */

	/*!
	 * Returns the runtime service process's JavaVM as an opaque
	 * `void *`. Cast to `JavaVM *`. Returns NULL on non-Android
	 * builds.
	 *
	 * Lifetime: process. Do not free. Thread-safe.
	 */
	void *(*get_jvm)(void);

	/*!
	 * Returns the runtime service process's Application Context as a
	 * `jobject` cast to `void *`. Cast back to `jobject` before use.
	 * Returns NULL on non-Android.
	 *
	 * The reference is a global ref owned by the runtime; modules
	 * must NOT `DeleteGlobalRef` it. Modules that need to hold the
	 * reference on a worker thread that outlives `aug_on_module_load`
	 * should `NewGlobalRef` their own copy and manage that.
	 *
	 * Lifetime: process. Thread-safe.
	 */
	void *(*get_context)(void);

	/*!
	 * Returns the absolute path of the calling module's extracted
	 * assets directory (where the runtime unpacked the bundled files
	 * from the module's .augins zip). Use to load bundled ONNX
	 * models, calibration files, etc.
	 *
	 * Implemented via a thread-local set by the dispatcher around
	 * every call into the module. Calling from a worker thread the
	 * module spawned itself returns the empty string ""; modules
	 * should capture the path inside `aug_on_module_load` and store
	 * it in a module-private global.
	 *
	 * Lifetime: process. Do not free. Returned pointer is to a
	 * runtime-owned string.
	 */
	const char *(*get_module_data_dir)(void);

	/*!
	 * Route a log line through the runtime's logging system. Maps
	 * `level` to one of:
	 *
	 *   0 = trace, 1 = debug, 2 = info, 3 = warn, 4 = error
	 *
	 * Modules may also use `__android_log_print` directly; this is
	 * just a convenience for keeping log style consistent with the
	 * runtime's own output. Thread-safe.
	 */
	void (*log)(int level, const char *fmt, ...);
};


/*!
 * Current host API version. Modules built against this header expect
 * at least this much from the runtime. The runtime's hand-over value
 * may be higher (forward-compat); modules check for >= this constant.
 */
#define AUG_HOST_API_VERSION 1u


/*!
 * Optional lifecycle entry point. Called once per module immediately
 * after `dlopen` succeeds and the runtime has built its dispatch map
 * entries for this module. The module typically:
 *
 *   - Validates `host->struct_version >= AUG_HOST_API_VERSION`
 *     (or the lower minimum the module actually needs).
 *   - Caches `host` in a module-private global for use from its
 *     OpenXR-shaped exported functions.
 *   - Captures `host->get_module_data_dir()` if needed on worker
 *     threads.
 *   - Spawns any background threads (sensor pollers, ARCore session,
 *     ONNX inference worker, ...).
 *
 * Returning non-zero tells the loader to reject the module: dlclose
 * happens and none of its dispatched functions get called. Returning
 * zero accepts the module.
 *
 * If the module does not export this symbol the loader treats it as
 * if it returned zero -- the module loads with no state.
 */
typedef int (*aug_on_module_load_fn)(const struct aug_host_api *host);


/*!
 * Optional teardown entry point. Called once at service shutdown
 * before the module's `.so` is `dlclose`d. Typical work:
 *
 *   - Signal worker threads to exit, join them.
 *   - Release ARCore / Camera2 / ONNX resources.
 *   - Drop any JNI global refs the module created.
 *
 * If the module does not export this symbol the runtime just calls
 * `dlclose` directly. Modules that don't own anything beyond the .so
 * itself can safely skip exporting it.
 */
typedef void (*aug_on_module_unload_fn)(void);


#ifdef __cplusplus
} // extern "C"
#endif
