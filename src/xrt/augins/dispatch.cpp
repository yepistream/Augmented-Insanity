// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Aug-Ins dispatch registry: name -> [(fn, module_id, data_dir,
// priority), ...].
//
// Populated by the loader at service startup. Read once per IPC
// dispatch (aug_has_modules_for) and per adapter call (aug_get_
// modules_for). All registration happens during single-threaded
// startup; all reads happen after on a different set of threads. No
// mutation post-startup.
//
// The two patterns therefore don't compete: we use a plain unordered
// map without any per-call locking. If we ever add hot-reload (a
// future v0.2.x or v0.3 feature) we'll need to add a reader/writer
// lock, but for now the cost would be all overhead and no benefit.

#include "dispatch.h"

#include <android/log.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#define TAG "Aug-Ins.Dispatch"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

namespace {

struct InternalEntry
{
	void       *fn;
	const char *module_id; // borrowed from loader's per-module struct
	const char *data_dir;  // borrowed from loader's per-module struct
	int         priority;
};

// Name strings stored as std::string keys (owned). Module-side
// borrowed pointers stored as-is; their lifetime is managed by the
// loader's loaded-modules list (which is the canonical owner).
std::unordered_map<std::string, std::vector<InternalEntry>> g_map;

} // namespace

bool
aug_has_modules_for(const char *xr_name)
{
	if (xr_name == nullptr) return false;
	auto it = g_map.find(xr_name);
	return it != g_map.end() && !it->second.empty();
}

size_t
aug_get_modules_for(const char *xr_name,
                    struct aug_module_entry *out_buf,
                    size_t out_capacity)
{
	if (xr_name == nullptr) return 0;
	auto it = g_map.find(xr_name);
	if (it == g_map.end()) return 0;

	const auto &v = it->second;
	if (out_buf == nullptr || out_capacity == 0) {
		// Caller is asking for the count.
		return v.size();
	}

	const size_t n = std::min(v.size(), out_capacity);
	for (size_t i = 0; i < n; ++i) {
		out_buf[i].fn        = v[i].fn;
		out_buf[i].module_id = v[i].module_id;
		out_buf[i].data_dir  = v[i].data_dir;
	}
	return n;
}

void
aug_dispatch_register(const char *xr_name,
                      void       *fn,
                      const char *module_id,
                      const char *data_dir,
                      int         priority)
{
	if (xr_name == nullptr || fn == nullptr) return;
	g_map[xr_name].push_back({fn, module_id, data_dir, priority});
}

void
aug_dispatch_sort_by_priority(void)
{
	for (auto &kv : g_map) {
		std::stable_sort(kv.second.begin(), kv.second.end(),
		                 [](const InternalEntry &a, const InternalEntry &b) {
			                 return a.priority < b.priority;
		                 });
	}
	LOGI("dispatch registry sorted: %zu function name(s) total", g_map.size());
}

void
aug_dispatch_clear(void)
{
	g_map.clear();
}
