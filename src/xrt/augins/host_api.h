// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * @file
 * @brief  Internal interface to the Aug-Ins host API singleton.
 *
 * Wraps `struct aug_host_api` initialisation. The runtime calls
 * `augins_host_api_set_jvm_ctx()` once at service start with the
 * JVM/Context pair the IPC server received from the runtime APK,
 * then hands the same `aug_host_api *` to every module via
 * `aug_on_module_load`.
 *
 * The "per-module data dir" entry is implemented via a thread-local
 * the dispatcher sets around every call into module code -- see
 * `augins_host_api_push_data_dir` / `..._pop_data_dir`.
 *
 * Internal header. Not part of the module ABI; module authors include
 * `module_abi.h` only.
 *
 * @ingroup aug_runtime
 */

#pragma once

#include "module_abi.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Returns the singleton host API table. Stable for the lifetime of
 * the service process. Modules receive this pointer via
 * `aug_on_module_load`.
 */
const struct aug_host_api *
augins_host_api_get(void);


/*!
 * Stash the JVM and Application Context the host API's `get_jvm()`
 * and `get_context()` will hand back to modules. Called once from
 * the service IPC bootstrap, with the values the runtime APK passed
 * across to the native side.
 *
 * Passing NULL is permitted (e.g. for non-Android dev runs); modules'
 * `get_jvm()` / `get_context()` will return NULL.
 */
void
augins_host_api_set_jvm_ctx(void *jvm, void *ctx);


/*!
 * Push the per-module data-dir path onto the calling thread's TLS
 * stack. Called by the dispatcher right before invoking any module-
 * exported function so the module's calls to
 * `host->get_module_data_dir()` return the right path even if
 * multiple modules are being walked in priority order.
 *
 * The path string is borrowed (not copied); caller keeps it alive
 * for the duration of the matching `_pop_data_dir`.
 */
void
augins_host_api_push_data_dir(const char *path);


/*!
 * Pop the per-module data-dir path. Must be paired with a prior
 * `_push_data_dir` on the same thread.
 */
void
augins_host_api_pop_data_dir(void);


#ifdef __cplusplus
} // extern "C"
#endif
