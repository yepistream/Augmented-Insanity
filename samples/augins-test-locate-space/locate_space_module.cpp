// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Aug-Ins v0.2 service-side test module: xrLocateSpace.
//
// Smallest possible module that exercises the v0.2 dispatch path:
// proto.py codegen routes space_locate_space IPC calls through
// aug_adapter_space_locate_space when modules are registered for
// "xrLocateSpace", which iterates this module's function.
//
// Function body: log a rate-limited count of calls and overwrite the
// output XrSpaceLocation's pose.position with a recognisable sentinel
// (42.0, 42.0, 42.0). The adapter then packs that back into the IPC
// reply and the client sees the sentinel in its xrLocateSpace result.

#include "module_abi.h"

#include <openxr/openxr.h>

#include <android/log.h>

#include <atomic>

#define TAG "Aug-Ins.TestLocateSpace"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

static const aug_host_api *g_host = nullptr;

extern "C" {

int
aug_on_module_load(const struct aug_host_api *host)
{
	g_host = host;
	LOGI("aug_on_module_load: host=%p struct_version=%u",
	     (void *)host, host != nullptr ? host->struct_version : 0u);
	return 0;
}

void
aug_on_module_unload(void)
{
	LOGI("aug_on_module_unload");
}

// Aug-Ins v0.2 dispatched function. Signature matches the real
// xrLocateSpace from <openxr/openxr.h>. Returning XR_SUCCESS keeps the
// chain alive (only this one module is registered for the name so the
// chain is length 1, but that doesn't matter for the signature).
XRAPI_ATTR XrResult XRAPI_CALL
xrLocateSpace(XrSpace          space,
              XrSpace          baseSpace,
              XrTime           time,
              XrSpaceLocation *location)
{
	if (location == nullptr) {
		return XR_ERROR_VALIDATION_FAILURE;
	}

	// Rate-limited logging. xrLocateSpace can fire many times per
	// frame for controller / action poses; logging every call swamps
	// logcat. Print every 120th call to make the dispatch observable
	// without spam.
	static std::atomic<uint64_t> s_count{0};
	uint64_t n = s_count.fetch_add(1, std::memory_order_relaxed) + 1;
	if (n == 1 || n % 120 == 0) {
		LOGI("xrLocateSpace call #%llu: space=%p base=%p time=%lld "
		     "in_loc.locationFlags=0x%x in_loc.pose=(%.2f, %.2f, %.2f)",
		     (unsigned long long)n,
		     (void *)space, (void *)baseSpace, (long long)time,
		     (unsigned)location->locationFlags,
		     location->pose.position.x,
		     location->pose.position.y,
		     location->pose.position.z);
	}

	// Sentinel: overwrite the position with (42, 42, 42). Adapter packs
	// this back into the IPC reply; we can grep for "pose=(42.00,
	// 42.00, 42.00)" in subsequent logs (or in the client app's own
	// logging if it logs locations) to confirm round-trip.
	location->pose.position.x = 42.0f;
	location->pose.position.y = 42.0f;
	location->pose.position.z = 42.0f;
	// Keep the orientation Monado provided, just to keep the test
	// surgical -- if we zero orientation too we can't distinguish
	// "module ran but didn't write" from "module wrote but result
	// dropped".

	return XR_SUCCESS;
}

} // extern "C"
