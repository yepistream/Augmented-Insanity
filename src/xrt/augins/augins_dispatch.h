// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * @file
 * @brief  Augmented Insanity (Aug-Ins) central hook dispatcher.
 *
 * Internal header Ã¢â‚¬â€ not intended for module authors. Module authors see
 * augins_module_abi.h instead.
 *
 * Central dispatch: augins_fire_hooks() is called by the generated IPC server
 * dispatch code (ipc_server_generated.c, produced by proto.py) for every
 * mapped OpenXR function, BEFORE the reply is sent to the client. Modules
 * receive the deserialized IPC message struct (input args) and the reply struct
 * (output args they may modify) as void* a1/a2 respectively.
 *
 * @ingroup augins
 */

#pragma once

#include <openxr/openxr.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Central dispatcher Ã¢â‚¬â€ called from oxr_api_*.c for functions with real args.
// All args are type-erased; on ARM64/x86-64 the callee reads back the correct
// types because all OpenXR handles and pointers are pointer-sized.
// ---------------------------------------------------------------------------

// Looks up xr_fn_name in the dispatch table and calls every registered module
// hook in priority order. Returns XR_ERROR_RUNTIME_FAILURE if any hook
// triggers AUG_FATAL_RUNTIME, XR_SUCCESS otherwise.
XrResult
augins_fire_hooks(const char *xr_fn_name,
                  void *a0,
                  void *a1,
                  void *a2,
                  void *a3);

// ---------------------------------------------------------------------------
// Status accessors used by the test/diagnostic UI.
// All return zero/NULL before the GRS has run.
// ---------------------------------------------------------------------------

size_t
augins_status_module_count(void);

const char *
augins_status_module_name(size_t index);

const char *
augins_status_module_id(size_t index);

const char *
augins_status_module_version(size_t index);

size_t
augins_status_module_function_count(size_t index);

const char *
augins_status_module_function_name(size_t module_index, size_t function_index);

#ifdef __cplusplus
}

#include <string>
#include <vector>

namespace augins {

// Per-function-per-module entry in the dispatch table (design doc Appendix A).
struct aug_module_func_entry
{
	void *symbol;            // Resolved function pointer from dlsym()
	std::string module_id;   // For error reporting and ABORT_MODULE
	std::string module_name; // For human-readable logging
	int priority;            // Loaded from settings.json; sorted ascending
};

// In-memory representation of a loaded module (design doc Appendix A).
struct aug_module
{
	std::string module_name;
	std::string module_id;
	std::string version;
	std::string description;
	void *handle = nullptr; // dlopen() handle
	std::string cache_dir; // per-module extraction directory (used by host API
	                       // get_module_data_dir helper)
	std::vector<std::string> advertised_functions;
	std::vector<std::string> advertised_extensions;
	std::vector<std::string> dependencies; // module IDs this module requires
	uint64_t advertised_system_bits = 0;   // bitmask of AUGINS_SYS_*

	// Cached lifecycle callback symbols, resolved at load time.
	void *cb_on_module_load = nullptr;
	void *cb_runtime_init = nullptr;
	void *cb_runtime_finished = nullptr;
	void *cb_back_buffer_loop = nullptr;
	void *cb_back_buffer_pause = nullptr;
	void *cb_back_buffer_resume = nullptr;
	void *cb_on_connect = nullptr;
	void *cb_frame_begin = nullptr;
	void *cb_frame_end = nullptr;
};

void
augins_load_all_from(const std::string &modules_dir, const std::string &cache_root);

void
augins_sort_dispatch_table(void);

const std::vector<aug_module> &
get_loaded_modules(void);

void
abort_module(const std::string &module_id);

// C++-internal central dispatcher (calls all hooks in the table for xr_name).
XrResult
augins_fire_hooks(const std::string &xr_fn_name,
                  void *a0,
                  void *a1,
                  void *a2,
                  void *a3);

} // namespace augins

#endif // __cplusplus
