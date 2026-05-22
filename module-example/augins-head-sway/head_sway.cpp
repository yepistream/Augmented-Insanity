// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Aug-Ins v0.2 head-sway tutorial module.
//
// Smallest visible-effect module. Adds a sinusoidal X-offset to the
// pose returned by the runtime adapter for `aug_LocateDeviceInSpace`,
// so any XR app rendering on this runtime sees the head sway left and
// right with a 0.3 m amplitude every 4 seconds.
//
// What this teaches:
//   1. The v0.2 module lifecycle (aug_on_module_load /
//      aug_on_module_unload).
//   2. Exporting a real OpenXR-shaped function and letting the
//      runtime dispatch into it via a hand-written adapter.
//   3. The "decorator" pattern (Q2): the runtime fills the baseline
//      pose before this function runs, so the module only needs to
//      add to what is already there.
//
// No external SDK dependencies. Builds with just the Android NDK and
// the v0.2 module ABI header `module_abi.h` from
// `src/xrt/augins/`.

#include "module_abi.h"

#include <openxr/openxr.h>

#include <android/log.h>

#include <chrono>
#include <cmath>

#define TAG "Aug-Ins.HeadSway"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace {

constexpr float kSwayAmplitudeM = 0.3f; // metres
constexpr float kSwayPeriodS    = 4.0f; // seconds

// Monotonic start time captured at module load. The displacement is a
// function of (now - start), so the sway phase starts at zero on every
// service start.
std::chrono::steady_clock::time_point g_start_time;

} // namespace

extern "C" {

int
aug_on_module_load(const struct aug_host_api *host)
{
	if (host == nullptr || host->struct_version < AUG_HOST_API_VERSION) {
		LOGE("aug_on_module_load: host API too old (got %u, need >= %u)",
		     host != nullptr ? host->struct_version : 0u,
		     AUG_HOST_API_VERSION);
		return 1;
	}
	g_start_time = std::chrono::steady_clock::now();
	LOGI("aug_on_module_load: head-sway armed (amplitude=%.2f m, period=%.2f s)",
	     kSwayAmplitudeM, kSwayPeriodS);
	return 0;
}

void
aug_on_module_unload(void)
{
	LOGI("aug_on_module_unload: head-sway disarmed");
}

// Module-facing v0.2 synthetic OpenXR name: aug_LocateDeviceInSpace.
// The runtime adapter invokes this only when the underlying IPC call
// is for the head xdev (see src/xrt/ipc/server/ipc_server_objects.c
// :: ipc_server_xdev_is_head_role and src/xrt/augins/adapters.cpp).
// The pose passed in has already been filled by the runtime default
// handler (Q2); this function decorates the existing values rather
// than producing them from scratch.
XRAPI_ATTR XrResult XRAPI_CALL
aug_LocateDeviceInSpace(XrSpace          baseSpace,
                        XrTime           time,
                        XrSpaceLocation *location)
{
	(void)baseSpace; // World-locked reference; phone-VR clients treat
	                 //   LOCAL / LOCAL_FLOOR / STAGE / UNBOUNDED as the
	                 //   same frame, so the choice of base does not
	                 //   change the displacement applied here.
	(void)time;      // The sway phase is driven by host monotonic time,
	                 //   not the XR-app-requested predicted display time.

	if (location == nullptr) {
		return XR_ERROR_VALIDATION_FAILURE;
	}

	const auto now = std::chrono::steady_clock::now();
	const float t = std::chrono::duration<float>(now - g_start_time).count();
	const float omega = 2.0f * static_cast<float>(M_PI) / kSwayPeriodS;
	const float dx = kSwayAmplitudeM * std::sin(omega * t);

	location->pose.position.x += dx;
	return XR_SUCCESS;
}

} // extern "C"
