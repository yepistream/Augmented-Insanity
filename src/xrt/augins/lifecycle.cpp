// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Aug-Ins module lifecycle bookkeeping + hook fan-out.
//
// Owns the canonical list of loaded modules. The loader registers a
// module here once dlopen + dlsym succeed; this code keeps the
// (handle, id, data_dir, on_load, on_unload) tuple and drives the
// lifecycle hook fan-out on the loader's behalf.
//
// On unload: fire on_unload in REVERSE registration order, then
// dlclose. Reverse order matches typical destruction semantics
// (later-loaded modules may depend on earlier-loaded modules' state).

#include "lifecycle.h"
#include "host_api.h"

#include <android/log.h>
#include <dlfcn.h>

#include <string>
#include <vector>

#define TAG "Aug-Ins.Lifecycle"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace {

struct LoadedModule
{
	void       *handle;
	std::string module_id;
	std::string data_dir;
	int (*on_load)(const void *host);
	void (*on_unload)(void);
};

std::vector<LoadedModule> g_loaded;

} // namespace

void
augins_lifecycle_register_module(void       *handle,
                                 const char *module_id,
                                 const char *data_dir,
                                 int (*on_load)(const void *host),
                                 void (*on_unload)(void))
{
	g_loaded.push_back(LoadedModule{
	    handle,
	    module_id != nullptr ? module_id : "",
	    data_dir  != nullptr ? data_dir  : "",
	    on_load,
	    on_unload,
	});
}

int
augins_lifecycle_fire_load(void)
{
	int rejected = 0;
	for (const auto &m : g_loaded) {
		if (m.on_load == nullptr) {
			// Module did not export the hook. That's fine -- a module
			// with no init state can skip it.
			continue;
		}
		// Set the per-module data dir on the calling thread's TLS so
		// the module's get_module_data_dir() during on_load returns
		// the right path.
		augins_host_api_push_data_dir(m.data_dir.c_str());
		int rc = m.on_load(augins_host_api_get());
		augins_host_api_pop_data_dir();
		if (rc != 0) {
			LOGW("module '%s': aug_on_module_load returned %d -- "
			     "module will be rejected by the loader",
			     m.module_id.c_str(), rc);
			++rejected;
		} else {
			LOGI("module '%s': aug_on_module_load OK", m.module_id.c_str());
		}
	}
	return rejected;
}

void
augins_lifecycle_fire_unload(void)
{
	// Reverse order, mirroring destruction.
	for (auto it = g_loaded.rbegin(); it != g_loaded.rend(); ++it) {
		if (it->on_unload == nullptr) continue;
		augins_host_api_push_data_dir(it->data_dir.c_str());
		it->on_unload();
		augins_host_api_pop_data_dir();
		LOGI("module '%s': aug_on_module_unload fired", it->module_id.c_str());
	}
}

void
augins_lifecycle_dlclose_all(void)
{
	// Same reverse order as fire_unload, so a module that pulled in
	// sibling libraries via DT_NEEDED gets unmapped before its deps.
	for (auto it = g_loaded.rbegin(); it != g_loaded.rend(); ++it) {
		if (it->handle != nullptr) {
			(void)::dlclose(it->handle);
		}
	}
	g_loaded.clear();
}

int
augins_lifecycle_module_count(void)
{
	return static_cast<int>(g_loaded.size());
}
