// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Aug-Ins v0.2 service-side test module: noop.
//
// Smallest possible module that proves the v0.2 loader works end-to-
// end. No OpenXR functions implemented (Implemented_Functions in the
// manifest is empty). Only exports the two lifecycle hooks and logs
// every call so a logcat grep shows the loader doing the right thing.
//
// Standalone NDK build (see CMakeLists.txt). The header copy of the
// v0.2 module ABI is local to this directory so the module is build-
// portable -- a third-party module author can copy this whole
// directory anywhere with just the NDK and have it compile.

#include "module_abi.h"

#include <android/log.h>

#define TAG "Aug-Ins.TestNoop"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

extern "C" {

int
aug_on_module_load(const struct aug_host_api *host)
{
	if (host == nullptr) {
		LOGI("aug_on_module_load: host is NULL (bare-bones runtime, ok)");
		return 0;
	}
	LOGI("aug_on_module_load: struct_version=%u", host->struct_version);

	// Exercise each host API entry so the verification log proves
	// the table is actually wired.
	void       *vm  = host->get_jvm     != nullptr ? host->get_jvm()     : nullptr;
	void       *ctx = host->get_context != nullptr ? host->get_context() : nullptr;
	const char *dir = host->get_module_data_dir != nullptr ? host->get_module_data_dir() : "";

	LOGI("aug_on_module_load: jvm=%p ctx=%p data_dir='%s'", vm, ctx, dir);

	if (host->log != nullptr) {
		host->log(2, "noop test module loaded successfully");
	}

	return 0;
}

void
aug_on_module_unload(void)
{
	LOGI("aug_on_module_unload: fired");
}

} // extern "C"
