// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// metadata.json parser for v0.2 Aug-Ins modules.
//
// Uses xrt-external-cjson (same as v0.1 did). Tolerates extra fields
// silently so a manifest written for a future v0.2.x revision still
// loads under a v0.2 runtime that doesn't know about the new fields,
// as long as the Manifest_Version still matches.

#include "manifest.h"
#include "module_abi.h"

#include "cjson/cJSON.h"

#include <android/log.h>

#include <fstream>
#include <sstream>

#define TAG "Aug-Ins.Manifest"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace augins {

bool
manifest_parse_file(const std::string &path, Manifest &out)
{
	std::ifstream f(path);
	if (!f.is_open()) {
		LOGE("open failed: %s", path.c_str());
		return false;
	}
	std::stringstream ss;
	ss << f.rdbuf();
	const std::string blob = ss.str();

	cJSON *root = cJSON_Parse(blob.c_str());
	if (root == nullptr) {
		LOGE("parse failed: %s", path.c_str());
		return false;
	}

	// Required: Manifest_Version
	cJSON *mv = cJSON_GetObjectItemCaseSensitive(root, "Manifest_Version");
	if (!cJSON_IsNumber(mv)) {
		LOGE("%s: missing required integer 'Manifest_Version'", path.c_str());
		cJSON_Delete(root);
		return false;
	}
	out.manifest_version = mv->valueint;
	if (out.manifest_version != AUG_MANIFEST_VERSION) {
		LOGE("%s: Manifest_Version=%d but runtime expects %d -- module rejected",
		     path.c_str(), out.manifest_version, (int)AUG_MANIFEST_VERSION);
		cJSON_Delete(root);
		return false;
	}

	// Required: ID
	cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "ID");
	if (!cJSON_IsString(id) || id->valuestring == nullptr || id->valuestring[0] == '\0') {
		LOGE("%s: missing or empty required string 'ID'", path.c_str());
		cJSON_Delete(root);
		return false;
	}
	out.id = id->valuestring;

	// Required: Version
	cJSON *ver = cJSON_GetObjectItemCaseSensitive(root, "Version");
	if (!cJSON_IsString(ver) || ver->valuestring == nullptr) {
		LOGE("%s: missing required string 'Version'", path.c_str());
		cJSON_Delete(root);
		return false;
	}
	out.version = ver->valuestring;

	// Required: Implemented_Functions (may be empty array)
	cJSON *impl = cJSON_GetObjectItemCaseSensitive(root, "Implemented_Functions");
	if (!cJSON_IsArray(impl)) {
		LOGE("%s: missing required array 'Implemented_Functions'", path.c_str());
		cJSON_Delete(root);
		return false;
	}
	cJSON *e = nullptr;
	cJSON_ArrayForEach(e, impl)
	{
		if (cJSON_IsString(e) && e->valuestring != nullptr) {
			out.implemented_functions.emplace_back(e->valuestring);
		}
	}

	// Optional: Name
	cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "Name");
	out.name = (cJSON_IsString(name) && name->valuestring) ? name->valuestring : out.id;

	// Optional: Description
	cJSON *desc = cJSON_GetObjectItemCaseSensitive(root, "Description");
	out.description = (cJSON_IsString(desc) && desc->valuestring) ? desc->valuestring : "";

	// Optional: Priority (default 100). Lower numbers run earlier in
	// the dispatch chain; last write wins, so higher Priority modules
	// effectively win conflicts.
	cJSON *prio = cJSON_GetObjectItemCaseSensitive(root, "Priority");
	if (cJSON_IsNumber(prio)) {
		out.priority = prio->valueint;
	}

	cJSON_Delete(root);
	return true;
}

} // namespace augins
