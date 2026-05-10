// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * @file
 * @brief  Augmented Insanity (Aug-Ins) module loader and central hook dispatcher.
 *
 * Module loading (design doc Ã‚Â§5.1) and central hook dispatch (Ã‚Â§5.2).
 *
 * augins_fire_hooks() is called directly from ipc_server_generated.c (the
 * code emitted by proto.py). The call site lives INSIDE each generated IPC
 * dispatch case, after the handler returns but BEFORE ipc_send Ã¢â‚¬â€ so modules
 * can read input args (msg struct) and modify output args (reply struct).
 *
 * All modules run inside augins-service.so (the GRS service process). The
 * dispatch table is populated by augins_load_all_from() at service startup.
 *
 * @ingroup augins
 */

#include "augins_dispatch.h"
#include "augins_extensions.h"
#include "augins_lifecycle.h"
#include "augins_module_abi.h"

#include "miniz.h"

#include <cjson/cJSON.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <fstream>
#include <mutex>
#include <sstream>
#include <sys/stat.h>
#include <unordered_map>

#include <android/log.h>

#define LOG_TAG "Aug-Ins"
#define AUG_LOGI(fmt, ...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, fmt, ##__VA_ARGS__)
#define AUG_LOGW(fmt, ...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, fmt, ##__VA_ARGS__)
#define AUG_LOGE(fmt, ...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, fmt, ##__VA_ARGS__)

namespace augins {

namespace {

// File-scope GRS state (design doc Appendix A).
std::vector<aug_module> g_loaded_modules;
std::unordered_map<std::string, std::vector<aug_module_func_entry>> g_dispatch_table;
std::mutex g_mu;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool
mkdir_p(const std::string &path)
{
	if (path.empty())
		return false;
	std::string acc;
	acc.reserve(path.size());
	for (size_t i = 0; i < path.size(); ++i) {
		acc.push_back(path[i]);
		if (path[i] == '/' || i + 1 == path.size()) {
			if (acc == "/" || (acc.size() == 1 && acc[0] == '.'))
				continue;
			if (mkdir(acc.c_str(), 0755) != 0 && errno != EEXIST) {
				AUG_LOGE("mkdir(%s) failed: %s", acc.c_str(), strerror(errno));
				return false;
			}
		}
	}
	return true;
}

std::string
derive_module_dir_name(const std::string &augins_path)
{
	auto slash = augins_path.find_last_of('/');
	std::string base = (slash == std::string::npos) ? augins_path : augins_path.substr(slash + 1);
	const std::string suffix = ".augins";
	if (base.size() >= suffix.size() &&
	    base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0)
		base.erase(base.size() - suffix.size());
	return base;
}

bool
is_cached_and_valid(const std::string &augins_path, const std::string &cache_dir)
{
	struct stat src{};
	if (stat(augins_path.c_str(), &src) != 0)
		return false;
	std::string stamp_path = cache_dir + "/.augins.stamp";
	std::ifstream stamp(stamp_path);
	if (!stamp)
		return false;
	long stamped = 0;
	stamp >> stamped;
	return stamped == static_cast<long>(src.st_mtime);
}

void
write_cache_stamp(const std::string &augins_path, const std::string &cache_dir)
{
	struct stat src{};
	if (stat(augins_path.c_str(), &src) != 0)
		return;
	std::ofstream stamp(cache_dir + "/.augins.stamp");
	if (stamp)
		stamp << static_cast<long>(src.st_mtime);
}

bool
unzip_to(const std::string &augins_path, const std::string &cache_dir)
{
	mz_zip_archive zip{};
	if (!mz_zip_reader_init_file(&zip, augins_path.c_str(), 0)) {
		AUG_LOGE("miniz: cannot open %s", augins_path.c_str());
		return false;
	}

	bool ok = true;
	mz_uint count = mz_zip_reader_get_num_files(&zip);
	for (mz_uint i = 0; i < count; ++i) {
		mz_zip_archive_file_stat fs{};
		if (!mz_zip_reader_file_stat(&zip, i, &fs)) {
			ok = false;
			break;
		}
		std::string out = cache_dir + "/" + fs.m_filename;
		if (mz_zip_reader_is_file_a_directory(&zip, i)) {
			mkdir_p(out);
			continue;
		}
		auto last_slash = out.find_last_of('/');
		if (last_slash != std::string::npos)
			mkdir_p(out.substr(0, last_slash));
		if (!mz_zip_reader_extract_to_file(&zip, i, out.c_str(), 0)) {
			AUG_LOGE("miniz: failed to extract %s -> %s", fs.m_filename, out.c_str());
			ok = false;
			break;
		}
	}

	mz_zip_reader_end(&zip);
	return ok;
}

std::string
read_file(const std::string &path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return {};
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

std::string
json_string_or(cJSON *obj, const char *key, const char *fallback)
{
	cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
	if (cJSON_IsString(v) && v->valuestring)
		return v->valuestring;
	return fallback ? std::string(fallback) : std::string();
}

void
resolve_lifecycle_symbols(aug_module &mod)
{
	mod.cb_on_module_load = dlsym(mod.handle, AUG_LIFECYCLE_ON_MODULE_LOAD);
	mod.cb_runtime_init = dlsym(mod.handle, AUG_LIFECYCLE_RUNTIME_INIT);
	mod.cb_runtime_finished = dlsym(mod.handle, AUG_LIFECYCLE_RUNTIME_FINISHED);
	mod.cb_back_buffer_loop = dlsym(mod.handle, AUG_LIFECYCLE_BACK_BUFFER_LOOP);
	mod.cb_back_buffer_pause = dlsym(mod.handle, AUG_LIFECYCLE_BACK_BUFFER_PAUSE);
	mod.cb_back_buffer_resume = dlsym(mod.handle, AUG_LIFECYCLE_BACK_BUFFER_RESUME);
	mod.cb_on_connect = dlsym(mod.handle, AUG_LIFECYCLE_ON_CONNECT);
	mod.cb_frame_begin = dlsym(mod.handle, AUG_LIFECYCLE_FRAME_BEGIN);
	mod.cb_frame_end = dlsym(mod.handle, AUG_LIFECYCLE_FRAME_END);
}

} // namespace

// ---------------------------------------------------------------------------
// Module loading (design doc Ã‚Â§5.1)
//
// Loading happens in three phases so module dependencies can be honoured:
//   1. PARSE  Ã¢â‚¬â€ walk modules dir, extract each .augins, parse metadata.json,
//               build a parsed_module record (no dlopen yet).
//   2. ORDER  Ã¢â‚¬â€ topological sort by Dependencies. Modules whose deps are
//               missing are skipped with a clear log line. Cycles drop every
//               module in the cycle.
//   3. LOAD   Ã¢â‚¬â€ for each parsed module in load order, pre-load sibling .so
//               siblings, dlopen the module, register hooks + features.
//
// PARSE+ORDER are bundled in augins_load_all_from(); LOAD is the per-module
// dlopen_and_register() helper called from inside that function.
// ---------------------------------------------------------------------------

namespace {

// Keep PARSE state separate from the runtime aug_module so the dependency
// resolver can reason about modules before they're dlopen'd.
struct parsed_module
{
	std::string augins_path;
	std::string cache_dir;
	std::string module_name;
	std::string module_id;
	std::string version;
	std::string description;
	int priority = 50;
	std::vector<std::string> dependencies;
	std::vector<std::string> implemented_functions;
	std::vector<std::string> advertised_extensions;
	uint64_t advertised_system_bits = 0;
};

uint64_t
parse_system_property_bit_name(const std::string &name)
{
	if (name == "handTracking") {
		return AUGINS_SYS_HAND_TRACKING;
	}
	if (name == "eyeTracking") {
		return AUGINS_SYS_EYE_TRACKING;
	}
	AUG_LOGW("Unknown SystemPropertyBits entry '%s' Ã¢â‚¬â€ ignored", name.c_str());
	return 0;
}

void
parse_advertised_features(parsed_module &p, cJSON *meta)
{
	// Legacy top-level "Advertised_Extensions": [...] Ã¢â‚¬â€ still accepted.
	cJSON *legacy = cJSON_GetObjectItemCaseSensitive(meta, "Advertised_Extensions");
	if (cJSON_IsArray(legacy)) {
		cJSON *ext = nullptr;
		cJSON_ArrayForEach(ext, legacy)
		{
			if (!cJSON_IsString(ext) || !ext->valuestring)
				continue;
			p.advertised_extensions.emplace_back(ext->valuestring);
		}
	}

	// Preferred: nested "Advertised_OpenXR_Features": { Extensions: [...],
	// SystemPropertyBits: [...], InteractionProfiles: [...] }.
	cJSON *features = cJSON_GetObjectItemCaseSensitive(meta, "Advertised_OpenXR_Features");
	if (!cJSON_IsObject(features)) {
		return;
	}
	cJSON *exts = cJSON_GetObjectItemCaseSensitive(features, "Extensions");
	if (cJSON_IsArray(exts)) {
		cJSON *ext = nullptr;
		cJSON_ArrayForEach(ext, exts)
		{
			if (!cJSON_IsString(ext) || !ext->valuestring)
				continue;
			p.advertised_extensions.emplace_back(ext->valuestring);
		}
	}
	cJSON *bits = cJSON_GetObjectItemCaseSensitive(features, "SystemPropertyBits");
	if (cJSON_IsArray(bits)) {
		cJSON *b = nullptr;
		cJSON_ArrayForEach(b, bits)
		{
			if (!cJSON_IsString(b) || !b->valuestring)
				continue;
			p.advertised_system_bits |= parse_system_property_bit_name(b->valuestring);
		}
	}
	// InteractionProfiles parsed but not yet aggregated Ã¢â‚¬â€ see plan, out of
	// scope for v2.
}

bool
parse_module(const std::string &augins_path, const std::string &cache_root, parsed_module &out)
{
	out.augins_path = augins_path;
	out.cache_dir = cache_root + "/" + derive_module_dir_name(augins_path);
	mkdir_p(out.cache_dir);

	if (!is_cached_and_valid(augins_path, out.cache_dir)) {
		AUG_LOGI("Extracting %s -> %s", augins_path.c_str(), out.cache_dir.c_str());
		if (!unzip_to(augins_path, out.cache_dir)) {
			AUG_LOGE("Skipping module due to extraction failure: %s", augins_path.c_str());
			return false;
		}
		write_cache_stamp(augins_path, out.cache_dir);
	}

	std::string metadata_path = out.cache_dir + "/metadata.json";
	std::string meta_text = read_file(metadata_path);
	if (meta_text.empty()) {
		AUG_LOGE("Could not read metadata: %s", metadata_path.c_str());
		return false;
	}
	cJSON *meta = cJSON_Parse(meta_text.c_str());
	if (!meta) {
		AUG_LOGE("metadata.json parse error: %s", metadata_path.c_str());
		return false;
	}

	out.module_name = json_string_or(meta, "Name", "");
	out.module_id = json_string_or(meta, "ID", "");
	out.version = json_string_or(meta, "Version", "");
	out.description = json_string_or(meta, "Description", "");

	if (out.module_id.empty()) {
		AUG_LOGE("Module at %s has no ID; skipping", augins_path.c_str());
		cJSON_Delete(meta);
		return false;
	}

	cJSON *deps = cJSON_GetObjectItemCaseSensitive(meta, "Dependencies");
	if (cJSON_IsArray(deps)) {
		cJSON *d = nullptr;
		cJSON_ArrayForEach(d, deps)
		{
			if (!cJSON_IsString(d) || !d->valuestring)
				continue;
			out.dependencies.emplace_back(d->valuestring);
		}
	}

	cJSON *impl = cJSON_GetObjectItemCaseSensitive(meta, "Implemented_Functions");
	if (cJSON_IsArray(impl)) {
		cJSON *fn = nullptr;
		cJSON_ArrayForEach(fn, impl)
		{
			if (!cJSON_IsString(fn) || !fn->valuestring)
				continue;
			out.implemented_functions.emplace_back(fn->valuestring);
		}
	}

	parse_advertised_features(out, meta);

	cJSON_Delete(meta);

	bool enabled = true;
	std::string settings_text = read_file(out.cache_dir + "/settings.json");
	if (!settings_text.empty()) {
		cJSON *settings = cJSON_Parse(settings_text.c_str());
		if (settings) {
			cJSON *p = cJSON_GetObjectItemCaseSensitive(settings, "Priority");
			if (cJSON_IsNumber(p))
				out.priority = p->valueint;
			cJSON *e = cJSON_GetObjectItemCaseSensitive(settings, "Enabled");
			if (cJSON_IsBool(e))
				enabled = cJSON_IsTrue(e);
			cJSON_Delete(settings);
		}
	}

	if (!enabled) {
		AUG_LOGI("Module '%s' disabled in settings.json; skipping", out.module_name.c_str());
		return false;
	}

	return true;
}

// Kahn's-algorithm topological sort. Returns the load order (parents before
// children). Modules whose deps are missing or are involved in a cycle are
// dropped with a clear log line.
std::vector<parsed_module>
resolve_load_order(std::vector<parsed_module> parsed)
{
	std::unordered_map<std::string, size_t> id_to_index;
	for (size_t i = 0; i < parsed.size(); ++i) {
		id_to_index[parsed[i].module_id] = i;
	}

	// First, drop modules whose declared deps don't exist as installed modules.
	std::vector<bool> alive(parsed.size(), true);
	bool changed = true;
	while (changed) {
		changed = false;
		for (size_t i = 0; i < parsed.size(); ++i) {
			if (!alive[i])
				continue;
			for (const auto &dep : parsed[i].dependencies) {
				auto it = id_to_index.find(dep);
				if (it == id_to_index.end() || !alive[it->second]) {
					AUG_LOGW("Skipping '%s': dependency '%s' is not installed/loadable",
					         parsed[i].module_id.c_str(), dep.c_str());
					alive[i] = false;
					changed = true;
					break;
				}
			}
		}
	}

	// In-degree count over surviving modules.
	std::vector<size_t> in_degree(parsed.size(), 0);
	for (size_t i = 0; i < parsed.size(); ++i) {
		if (!alive[i])
			continue;
		for (const auto &dep : parsed[i].dependencies) {
			auto it = id_to_index.find(dep);
			if (it != id_to_index.end() && alive[it->second]) {
				in_degree[i]++;
			}
		}
	}

	std::vector<parsed_module> order;
	order.reserve(parsed.size());
	std::vector<size_t> ready;
	for (size_t i = 0; i < parsed.size(); ++i) {
		if (alive[i] && in_degree[i] == 0) {
			ready.push_back(i);
		}
	}
	// Stable order within a topological "rank": sort by priority ascending so
	// behaviour is predictable.
	std::sort(ready.begin(), ready.end(),
	          [&](size_t a, size_t b) { return parsed[a].priority < parsed[b].priority; });

	while (!ready.empty()) {
		size_t i = ready.front();
		ready.erase(ready.begin());

		order.push_back(parsed[i]);

		// Any module that depended on i: decrement its in-degree.
		for (size_t j = 0; j < parsed.size(); ++j) {
			if (!alive[j] || j == i)
				continue;
			for (const auto &dep : parsed[j].dependencies) {
				if (dep == parsed[i].module_id) {
					if (--in_degree[j] == 0) {
						ready.push_back(j);
					}
					break;
				}
			}
		}
		std::sort(ready.begin(), ready.end(),
		          [&](size_t a, size_t b) { return parsed[a].priority < parsed[b].priority; });
	}

	if (order.size() < std::count(alive.begin(), alive.end(), true)) {
		AUG_LOGE("Dependency cycle detected; modules in the cycle were dropped");
		// (We could log which modules specifically Ã¢â‚¬â€ anything alive[] but not
		// in `order`. Keep terse for now.)
	}

	std::string trace = "Module load order:";
	for (const auto &p : order) {
		trace += " ";
		trace += p.module_id;
	}
	AUG_LOGI("%s", trace.c_str());
	return order;
}

void
dlopen_and_register(const parsed_module &p)
{
	std::string so_path = p.cache_dir + "/" + p.module_id + ".so";

	// Pre-load any sibling .so files in the extraction directory with RTLD_GLOBAL
	// so the dynamic linker can satisfy DT_NEEDED entries in the module .so
	// (e.g. libarcore_sdk_c.so bundled alongside the module in the .augins zip).
	{
		DIR *d = opendir(p.cache_dir.c_str());
		struct dirent *ent;
		while (d != nullptr && (ent = readdir(d)) != nullptr) {
			std::string name = ent->d_name;
			if (name.size() < 3 || name.substr(name.size() - 3) != ".so")
				continue;
			if (name == p.module_id + ".so")
				continue;
			std::string sibling = p.cache_dir + "/" + name;
			void *sh = dlopen(sibling.c_str(), RTLD_NOW | RTLD_GLOBAL);
			if (sh) {
				AUG_LOGI("Pre-loaded sibling: %s", name.c_str());
			} else {
				AUG_LOGW("Could not pre-load sibling %s: %s", name.c_str(), dlerror());
			}
		}
		if (d != nullptr)
			closedir(d);
	}

	aug_module mod;
	mod.module_name = p.module_name;
	mod.module_id = p.module_id;
	mod.version = p.version;
	mod.description = p.description;
	mod.cache_dir = p.cache_dir;
	mod.dependencies = p.dependencies;
	mod.advertised_system_bits = p.advertised_system_bits;

	mod.handle = dlopen(so_path.c_str(), RTLD_NOW);
	if (!mod.handle) {
		AUG_LOGE("dlopen(%s) failed: %s", so_path.c_str(), dlerror());
		return;
	}

	resolve_lifecycle_symbols(mod);

	std::lock_guard<std::mutex> lock(g_mu);

	for (const auto &name : p.implemented_functions) {
		void *sym = dlsym(mod.handle, name.c_str());
		if (!sym) {
			AUG_LOGW("Module '%s' advertises '%s' but dlsym failed",
			         mod.module_id.c_str(), name.c_str());
			continue;
		}
		mod.advertised_functions.push_back(name);
		g_dispatch_table[name].push_back(
		    aug_module_func_entry{sym, mod.module_id, mod.module_name, p.priority});
	}

	for (const auto &ext_name : p.advertised_extensions) {
		mod.advertised_extensions.push_back(ext_name);
		augins::add_advertised_extension(ext_name);
	}
	if (p.advertised_system_bits != 0) {
		augins::add_advertised_system_bit(p.advertised_system_bits);
	}

	g_loaded_modules.push_back(std::move(mod));

	AUG_LOGI("Loaded module: %s (id=%s, prio=%d)", g_loaded_modules.back().module_name.c_str(),
	         g_loaded_modules.back().module_id.c_str(), p.priority);
}

} // namespace

void
augins_load_all_from(const std::string &modules_dir, const std::string &cache_root)
{
	mkdir_p(modules_dir);
	mkdir_p(cache_root);

	DIR *dir = opendir(modules_dir.c_str());
	if (!dir) {
		AUG_LOGW("Modules dir does not exist or is unreadable: %s", modules_dir.c_str());
		return;
	}

	// Phase 1: parse every .augins.
	std::vector<parsed_module> parsed;
	const std::string suffix = ".augins";
	while (struct dirent *ent = readdir(dir)) {
		std::string name = ent->d_name;
		if (name.size() <= suffix.size() ||
		    name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
			continue;
		parsed_module p;
		if (parse_module(modules_dir + "/" + name, cache_root, p)) {
			parsed.push_back(std::move(p));
		}
	}
	closedir(dir);

	// Phase 2: resolve load order honouring Dependencies.
	auto order = resolve_load_order(std::move(parsed));

	// Phase 3: dlopen + register in load order.
	for (const auto &p : order) {
		dlopen_and_register(p);
	}
}

void
augins_sort_dispatch_table(void)
{
	std::lock_guard<std::mutex> lock(g_mu);
	for (auto &kv : g_dispatch_table) {
		std::sort(kv.second.begin(), kv.second.end(),
		          [](const aug_module_func_entry &a, const aug_module_func_entry &b) {
			          return a.priority < b.priority;
		          });
	}
}

const std::vector<aug_module> &
get_loaded_modules(void)
{
	return g_loaded_modules;
}

void
abort_module(const std::string &module_id)
{
	std::lock_guard<std::mutex> lock(g_mu);
	for (auto &kv : g_dispatch_table) {
		auto &v = kv.second;
		v.erase(std::remove_if(v.begin(), v.end(),
		                       [&](const aug_module_func_entry &e) {
			                       return e.module_id == module_id;
		                       }),
		        v.end());
	}
	auto it = std::find_if(g_loaded_modules.begin(), g_loaded_modules.end(),
	                       [&](const aug_module &m) { return m.module_id == module_id; });
	if (it != g_loaded_modules.end()) {
		if (it->handle)
			dlclose(it->handle);
		g_loaded_modules.erase(it);
	}
	AUG_LOGW("ABORT_MODULE: removed '%s' from dispatch table", module_id.c_str());
}

// ---------------------------------------------------------------------------
// Central dispatcher (design doc Ã‚Â§5.2)
// ---------------------------------------------------------------------------

XrResult
augins_fire_hooks(const std::string &xr_fn_name, void *a0, void *a1, void *a2, void *a3)
{
	// Snapshot under the lock so the lock is not held while calling modules
	// (a module returning AUG_FATAL_MODULE would deadlock on abort_module).
	std::vector<aug_module_func_entry> entries;
	{
		std::lock_guard<std::mutex> lock(g_mu);
		auto it = g_dispatch_table.find(xr_fn_name);
		if (it == g_dispatch_table.end())
			return XR_SUCCESS;
		entries = it->second;
	}

	// Generic 4-pointer function type. On ARM64/x86-64 all OpenXR handles and
	// pointers are pointer-sized so the callee reads the correct values from
	// the argument registers regardless of its declared signature.
	using generic_fn = int32_t (*)(void *, void *, void *, void *);

	for (const auto &entry : entries) {
		auto fn = reinterpret_cast<generic_fn>(entry.symbol);
		int32_t aug_ret = fn(a0, a1, a2, a3);

		switch (aug_ret) {
		case AUG_OK:
			continue;

		case AUG_FATAL_RUNTIME:
			AUG_LOGE("[%s] AUG_FATAL_RUNTIME in %s Ã¢â‚¬â€ aborting GRS",
			         entry.module_name.c_str(), xr_fn_name.c_str());
			critical("module " + entry.module_id + " requested ABORT_WHOLE_RUNTIME");
			return XR_ERROR_RUNTIME_FAILURE;

		case AUG_FATAL_MODULE:
			AUG_LOGE("[%s] AUG_FATAL_MODULE in %s Ã¢â‚¬â€ removing from dispatch",
			         entry.module_name.c_str(), xr_fn_name.c_str());
			abort_module(entry.module_id);
			continue;

		default:
			if (aug_ret > 0)
				AUG_LOGW("[%s] warning %d in %s", entry.module_name.c_str(), aug_ret,
				         xr_fn_name.c_str());
			else
				AUG_LOGE("[%s] error %d in %s", entry.module_name.c_str(), aug_ret,
				         xr_fn_name.c_str());
			continue;
		}
	}

	return XR_SUCCESS;
}

} // namespace augins

// ---------------------------------------------------------------------------
// C-linkage surface
// ---------------------------------------------------------------------------

extern "C" {

XrResult
augins_fire_hooks(const char *xr_fn_name, void *a0, void *a1, void *a2, void *a3)
{
	return augins::augins_fire_hooks(xr_fn_name, a0, a1, a2, a3);
}

size_t
augins_status_module_count(void)
{
	std::lock_guard<std::mutex> lock(augins::g_mu);
	return augins::g_loaded_modules.size();
}

const char *
augins_status_module_name(size_t index)
{
	std::lock_guard<std::mutex> lock(augins::g_mu);
	return index < augins::g_loaded_modules.size()
	           ? augins::g_loaded_modules[index].module_name.c_str()
	           : nullptr;
}

const char *
augins_status_module_id(size_t index)
{
	std::lock_guard<std::mutex> lock(augins::g_mu);
	return index < augins::g_loaded_modules.size()
	           ? augins::g_loaded_modules[index].module_id.c_str()
	           : nullptr;
}

const char *
augins_status_module_version(size_t index)
{
	std::lock_guard<std::mutex> lock(augins::g_mu);
	return index < augins::g_loaded_modules.size()
	           ? augins::g_loaded_modules[index].version.c_str()
	           : nullptr;
}

size_t
augins_status_module_function_count(size_t index)
{
	std::lock_guard<std::mutex> lock(augins::g_mu);
	return index < augins::g_loaded_modules.size()
	           ? augins::g_loaded_modules[index].advertised_functions.size()
	           : 0;
}

const char *
augins_status_module_function_name(size_t module_index, size_t function_index)
{
	std::lock_guard<std::mutex> lock(augins::g_mu);
	if (module_index >= augins::g_loaded_modules.size())
		return nullptr;
	const auto &fns = augins::g_loaded_modules[module_index].advertised_functions;
	return function_index < fns.size() ? fns[function_index].c_str() : nullptr;
}

} // extern "C"
