// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Aug-Ins v0.2 module loader, service-side.
//
// Lifecycle on service startup:
//   1. Scan modules_dir for *.augins files (filesystem-listing).
//   2. For each .augins:
//      a. Extract zip into cache_dir/_staged/<filename>/.
//      b. Parse metadata.json (validate Manifest_Version, ID, etc.).
//      c. Pre-load sibling .so files (RTLD_GLOBAL) so the main
//         module .so's DT_NEEDED entries can resolve against them.
//         Needed for modules that vendor SDKs like libarcore_sdk_c.so.
//      d. dlopen the main <ID>.so (RTLD_NOW).
//      e. dlsym every name in Implemented_Functions; for each one
//         that resolves, aug_dispatch_register the symbol with the
//         module's Priority. Names that don't resolve get a warning
//         and are dropped silently (per Q2e error policy).
//      f. dlsym aug_on_module_load / aug_on_module_unload and pass
//         to augins_lifecycle_register_module.
//   3. aug_dispatch_sort_by_priority() once all modules are registered.
//   4. Read module_order.json override file (TODO Phase 2b+).
//   5. augins_lifecycle_fire_load() to call aug_on_module_load on each.
//      Modules that return non-zero are downgraded: their dispatch
//      entries get pulled and their handle gets dlclose'd. (Downgrade
//      not implemented in this pass; TODO comment in fire_load.)
//
// Lifecycle on service shutdown:
//   1. augins_lifecycle_fire_unload()
//   2. aug_dispatch_clear() (drops borrowed pointers into lifecycle storage)
//   3. augins_lifecycle_dlclose_all() (dlclose handles, free storage)
//
// APK-bundled fallback (per Q2a): TODO Phase 2b+. The runtime APK
// can ship its own .augins files under assets/modules/. When we want
// that, this loader gets a second scan pass using AAssetManager over
// JNI; user-installed modules in modules_dir take precedence over
// APK-bundled ones with the same ID.

#include "loader.h"

#include "dispatch.h"
#include "extract.h"
#include "host_api.h"
#include "lifecycle.h"
#include "manifest.h"
#include "module_abi.h"

#include <android/log.h>
#include <dirent.h>
#include <dlfcn.h>
#include <sys/types.h>

#include <algorithm>
#include <atomic>
#include <list>
#include <string>
#include <vector>

#define TAG "Aug-Ins.Loader"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace {

std::atomic<bool> g_initialised{false};

// Scan dir_path for entries ending in ".augins". Returns sorted
// absolute paths (sort is just for deterministic load order in logs).
std::vector<std::string>
list_augins_files(const std::string &dir_path)
{
	std::vector<std::string> out;
	DIR *dir = ::opendir(dir_path.c_str());
	if (dir == nullptr) {
		// Not an error: the modules dir may simply not exist yet on a
		// fresh install. Log debug and return empty.
		LOGI("modules dir not present at %s (no modules to load)", dir_path.c_str());
		return out;
	}
	struct dirent *de;
	while ((de = ::readdir(dir)) != nullptr) {
		std::string name = de->d_name;
		const std::string suffix = ".augins";
		if (name.size() <= suffix.size()) continue;
		if (name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
		out.push_back(dir_path + "/" + name);
	}
	::closedir(dir);
	std::sort(out.begin(), out.end());
	return out;
}

// Pre-load every sibling .so in the extracted dir (anything except the
// main <module_id>.so) with RTLD_GLOBAL so the main module's DT_NEEDED
// resolves against them. Mirrors the v0.1 server-side loader; needed
// for any module that vendors a 3rd-party SDK (e.g. ARCore).
void
preload_siblings(const std::string &extract_dir, const std::string &main_so_basename)
{
	DIR *dir = ::opendir(extract_dir.c_str());
	if (dir == nullptr) return;
	struct dirent *de;
	while ((de = ::readdir(dir)) != nullptr) {
		std::string fname = de->d_name;
		if (fname.size() < 3 || fname.compare(fname.size() - 3, 3, ".so") != 0) continue;
		if (fname == main_so_basename) continue;
		std::string dep_path = extract_dir + "/" + fname;
		void *dh = ::dlopen(dep_path.c_str(), RTLD_NOW | RTLD_GLOBAL);
		if (dh == nullptr) {
			LOGW("preload(%s): %s", fname.c_str(), ::dlerror());
		} else {
			LOGI("preloaded sibling: %s", fname.c_str());
		}
		// Intentionally NOT dlclose'd. The sibling stays loaded for the
		// lifetime of the process so the main module can keep resolving
		// against it.
	}
	::closedir(dir);
}

// Load one .augins file. Returns true on full success (manifest parsed,
// dlopen succeeded, lifecycle registered). On any failure logs and
// returns false; the caller skips and continues with the next file.
bool
load_one(const std::string &augins_path, const std::string &cache_root)
{
	// Derive a filename-based staging key (the actual module ID is
	// inside the zip; we discover it after extracting).
	const std::string filename = augins_path.substr(augins_path.find_last_of('/') + 1);
	const std::string staging_subdir = std::string("_staged/") + filename;

	std::string extract_dir;
	if (!augins::extract_augins_zip(augins_path, staging_subdir, cache_root, extract_dir)) {
		LOGE("load_one[%s]: extract failed", filename.c_str());
		return false;
	}

	augins::Manifest m;
	if (!augins::manifest_parse_file(extract_dir + "/metadata.json", m)) {
		LOGE("load_one[%s]: manifest parse/validate failed", filename.c_str());
		return false;
	}

	const std::string main_so_basename = m.id + ".so";
	preload_siblings(extract_dir, main_so_basename);

	const std::string so_path = extract_dir + "/" + main_so_basename;
	void *handle = ::dlopen(so_path.c_str(), RTLD_NOW);
	if (handle == nullptr) {
		LOGE("load_one[%s]: dlopen(%s) failed: %s",
		     filename.c_str(), so_path.c_str(), ::dlerror());
		return false;
	}

	// Resolve lifecycle hooks (both optional).
	auto on_load   = reinterpret_cast<int  (*)(const void *)>(::dlsym(handle, "aug_on_module_load"));
	auto on_unload = reinterpret_cast<void (*)(void)>        (::dlsym(handle, "aug_on_module_unload"));

	augins_lifecycle_register_module(handle, m.id.c_str(), extract_dir.c_str(),
	                                 on_load, on_unload);

	// Resolve every Implemented_Function. Missing symbols are logged
	// (Q2e: "Log warning for that name, register the module for symbols
	// that did resolve, drop the missing ones") and the rest still
	// register cleanly.
	//
	// String storage: dispatch's aug_module_entry holds BORROWED const
	// char* for module_id and data_dir. The pointers must outlive the
	// dispatch registry (which lives for the process). std::list never
	// invalidates pointers on push_back, unlike std::vector -- so we
	// store the per-module strings in a static list and hand out the
	// addresses of the std::string objects' c_str()s. The list itself
	// is never cleared; it leaks at process exit (intentional -- 50
	// bytes per loaded module, no churn after startup).
	//
	// Function name strings (the dispatch map's keys) are owned by
	// the dispatch unordered_map internally (it copies into its own
	// std::string keys), so we don't need to keep our copy alive for
	// those.
	static std::list<std::string> g_perm_strings;
	g_perm_strings.push_back(m.id);
	const char *id_perm = g_perm_strings.back().c_str();
	g_perm_strings.push_back(extract_dir);
	const char *dir_perm = g_perm_strings.back().c_str();

	size_t resolved = 0;
	for (const auto &name : m.implemented_functions) {
		void *sym = ::dlsym(handle, name.c_str());
		if (sym == nullptr) {
			LOGW("module '%s': symbol '%s' listed in Implemented_Functions "
			     "but not exported by the .so -- dropping",
			     m.id.c_str(), name.c_str());
			continue;
		}
		aug_dispatch_register(name.c_str(), sym, id_perm, dir_perm, m.priority);
		++resolved;
	}

	LOGI("loaded module '%s' v%s (priority=%d, %zu/%zu functions resolved)",
	     m.id.c_str(), m.version.c_str(), m.priority,
	     resolved, m.implemented_functions.size());
	return true;
}

} // namespace

void
augins_loader_init(const char *modules_dir, const char *cache_dir)
{
	bool expected = false;
	if (!g_initialised.compare_exchange_strong(expected, true,
	                                           std::memory_order_acq_rel)) {
		LOGI("init: already initialised; ignoring duplicate call");
		return;
	}
	if (modules_dir == nullptr || modules_dir[0] == '\0' ||
	    cache_dir   == nullptr || cache_dir[0]   == '\0') {
		LOGW("init: NULL/empty paths (modules_dir=%s cache_dir=%s); "
		     "no modules will be loaded",
		     modules_dir != nullptr ? modules_dir : "(null)",
		     cache_dir   != nullptr ? cache_dir   : "(null)");
		return;
	}

	LOGI("init: scanning %s", modules_dir);
	const std::vector<std::string> files = list_augins_files(modules_dir);
	LOGI("init: found %zu .augins file(s)", files.size());

	size_t loaded = 0;
	for (const std::string &p : files) {
		if (load_one(p, cache_dir)) ++loaded;
	}

	aug_dispatch_sort_by_priority();

	// TODO Phase 2b+: read module_order.json override file from
	// <parent_of_modules_dir>/module_order.json. For each module ID
	// listed, replace its registered Priority with the override value
	// and re-sort. Skip silently if the file is absent or malformed.

	const int rejected = augins_lifecycle_fire_load();
	if (rejected > 0) {
		LOGW("init: %d module(s) returned non-zero from aug_on_module_load "
		     "(downgrade NOT yet implemented; their dispatch entries are "
		     "still live -- TODO Phase 2b+)",
		     rejected);
	}

	LOGI("init: %zu .augins file(s) processed, %zu module(s) loaded successfully",
	     files.size(), loaded);
}

void
augins_loader_shutdown(void)
{
	bool expected = true;
	if (!g_initialised.compare_exchange_strong(expected, false,
	                                           std::memory_order_acq_rel)) {
		return;
	}
	LOGI("shutdown: tearing down %d loaded module(s)", augins_lifecycle_module_count());
	augins_lifecycle_fire_unload();
	aug_dispatch_clear();
	augins_lifecycle_dlclose_all();
}
