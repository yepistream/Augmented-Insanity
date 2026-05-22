// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Aug-Ins v0.2 ARCore head-pose module (service-side).
//
// Exports a single OpenXR-shaped function for the synthetic
// runtime-side adapter name `aug_LocateDeviceInSpace`, which the
// adapter routes head-device locate calls through. (See
// src/xrt/augins/adapters.cpp and src/xrt/ipc/shared/proto.py.) When
// ARCore tracking is valid, the module overwrites the XrSpaceLocation
// with ARCore's 6DoF pose; otherwise it passes through the baseline
// the runtime already filled in.
//
// Lifecycle:
//   aug_on_module_load   -> validate host API >= v1, cache it, spawn
//                           the ARCore worker (with a NewGlobalRef'd
//                           Context handed off to the worker thread).
//   aug_on_module_unload -> signal worker to stop, join, drop the
//                           global ref.
//
// One ArSession per service process. All XR clients connecting to the
// runtime share this single pose stream -- modules live service-side
// because that is exactly the place where ARCore-style "one ArSession
// or none" globally-scoped resources belong.
//
// Compared to the v0.1 source archived alongside this file:
//   - No semantic-ID caching: head-vs-controller-vs-trackers
//     discrimination is now done at the runtime adapter layer
//     (xdev role lookup), not at the module by string-matching
//     space IDs to OpenXR semantic spaces.
//   - No xrLocateSpace hook. v0.2 modules that want to override
//     view-in-LOCAL ride on `aug_LocateDeviceInSpace`.
//   - No T_xdev_head override (`aug_deviceGetTrackedPose` in v0.1).
//     `space_locate_device` carries the dominant transform; v0.2
//     ARCore module overrides only T_base_xdev. See PROJECT_PLAN.md.
//   - No camera-frame publishing: host API v1 has no frame broker.
//     A v0.2.x Mercury rewrite will add a broker entry to the host
//     API; until then this module focuses on head pose only.

#include "module_abi.h"
#include "arcore_instance.h"

#include <openxr/openxr.h>

#include <android/log.h>
#include <jni.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>

#define TAG "Aug-Ins.ARCore"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)


// ---------------------------------------------------------------------------
// Module globals
// ---------------------------------------------------------------------------

namespace {

const struct aug_host_api *g_host = nullptr;

struct arcore_min g_arcore = {};

std::thread       g_worker;
std::atomic<bool> g_stop{false};
std::atomic<bool> g_worker_running{false};

std::mutex g_pose_mu;
struct
{
	float pos[3];
	float rot[4]; // quaternion x, y, z, w (ARCore order)
	bool  tracking;
} g_cached_pose = {{0, 0, 0}, {0, 0, 0, 1}, false};

// ---------------------------------------------------------------------------
// Worker thread: pulls poses out of ARCore at ~60 Hz, stashes the latest
// into g_cached_pose. The pose getter is lock-protected because the dispatch
// thread reads it on every locate call.
// ---------------------------------------------------------------------------

void
arcore_worker(JavaVM *vm, jobject ctx_global)
{
	JNIEnv *env = nullptr;
	if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
		LOGE("AttachCurrentThread failed -- cannot start ARCore");
		// env is null; we cannot safely DeleteGlobalRef. Leak the ref
		// (fatal-init situation, the runtime is shutting down anyway).
		return;
	}

	struct arcore_min_config cfg;
	arcore_min_config_set_defaults(&cfg);
	cfg.focus_mode     = AUTO_FOCUS_ENABLED;
	cfg.camera_hz_mode = MAX_ARCAMERA_HZ;

	// arcore_min_start_ex uses GetEnv() which finds the env we attached.
	// did_attach stays false, so arcore_min_stop will NOT detach for us
	// -- we own the attach and must detach ourselves at the end.
	if (!arcore_min_start_ex(&g_arcore, vm, ctx_global, &cfg)) {
		LOGE("arcore_min_start_ex failed -- is Google Play Services for AR installed?");
		env->DeleteGlobalRef(ctx_global); // delete BEFORE detaching
		vm->DetachCurrentThread();
		return;
	}
	LOGI("ARCore session started");

	while (!g_stop.load(std::memory_order_relaxed)) {
		float   pos[3]   = {};
		float   rot[4]   = {};
		bool    tracking = false;
		int64_t ts_ns    = 0;

		if (arcore_min_tick(&g_arcore, pos, rot, &tracking, &ts_ns)) {
			std::lock_guard<std::mutex> lk(g_pose_mu);
			g_cached_pose.pos[0] = pos[0];
			g_cached_pose.pos[1] = pos[1];
			g_cached_pose.pos[2] = pos[2];
			g_cached_pose.rot[0] = rot[0];
			g_cached_pose.rot[1] = rot[1];
			g_cached_pose.rot[2] = rot[2];
			g_cached_pose.rot[3] = rot[3];
			g_cached_pose.tracking = tracking;
		}

		// ~60 Hz target. ARCore itself caps the camera FPS; we don't
		// gain anything by polling faster.
		std::this_thread::sleep_for(std::chrono::milliseconds(16));
	}

	arcore_min_stop(&g_arcore);
	LOGI("ARCore session stopped");
	env->DeleteGlobalRef(ctx_global); // delete BEFORE detaching
	vm->DetachCurrentThread();
}

} // namespace


// ---------------------------------------------------------------------------
// Exported symbols
// ---------------------------------------------------------------------------

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
	g_host = host;
	LOGI("aug_on_module_load: host API v%u accepted", host->struct_version);

	auto *vm  = static_cast<JavaVM *>(host->get_jvm());
	auto *ctx = static_cast<jobject>(host->get_context());
	if (vm == nullptr || ctx == nullptr) {
		LOGE("aug_on_module_load: JVM or Context not available");
		return 1;
	}

	// Promote the runtime-owned Context ref to one the worker owns,
	// so the worker can keep it after this stack unwinds.
	//
	// aug_on_module_load runs on the runtime's Android main thread,
	// which is already attached to the JVM. Detaching the main thread
	// is a JNI-spec fatal ("attempting to detach while still running
	// code"). GetEnv when already attached; do NOT Attach/Detach.
	JNIEnv *env = nullptr;
	jint env_rc = vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
	if (env_rc != JNI_OK || env == nullptr) {
		LOGE("aug_on_module_load: GetEnv failed rc=%d (expected JNI_OK on main thread)", env_rc);
		return 1;
	}
	jobject ctx_global = env->NewGlobalRef(ctx);
	if (ctx_global == nullptr) {
		LOGE("aug_on_module_load: NewGlobalRef returned NULL");
		return 1;
	}

	g_stop.store(false, std::memory_order_relaxed);
	g_worker = std::thread(arcore_worker, vm, ctx_global);
	g_worker_running.store(true, std::memory_order_release);
	LOGI("aug_on_module_load: ARCore worker spawned");
	return 0;
}

void
aug_on_module_unload(void)
{
	if (!g_worker_running.load(std::memory_order_acquire)) {
		LOGI("aug_on_module_unload: worker was not running");
		return;
	}
	g_stop.store(true, std::memory_order_relaxed);
	if (g_worker.joinable()) {
		g_worker.join();
	}
	g_worker_running.store(false, std::memory_order_release);
	LOGI("aug_on_module_unload: worker joined");
}

// Module-facing v0.2 synthetic OpenXR name: aug_LocateDeviceInSpace.
// Runtime adapter only invokes this when the underlying IPC call is
// for the head xdev (see ipc_server_xdev_is_head_role); the module
// can assume "this call is the head pose."
XRAPI_ATTR XrResult XRAPI_CALL
aug_LocateDeviceInSpace(XrSpace          baseSpace,
                        XrTime           time,
                        XrSpaceLocation *location)
{
	(void)baseSpace; // T_base_xdev assumes world-locked base; phone
	                 //   runtimes treat LOCAL / LOCAL_FLOOR / STAGE /
	                 //   UNBOUNDED as the same world frame. Y-offset
	                 //   for LOCAL_FLOOR is a v0.2.x follow-up.
	(void)time;      // ARCore pose is the latest, not time-queried.

	if (location == nullptr) {
		return XR_ERROR_VALIDATION_FAILURE;
	}

	// Rate-limited dispatch trace. xrLocateViews fires twice per frame
	// (once per view) so logging every call swamps logcat; every 240th
	// is fine for observability and not noisy.
	static std::atomic<uint64_t> s_dispatch_count{0};
	const uint64_t n = s_dispatch_count.fetch_add(1, std::memory_order_relaxed) + 1;
	if (n == 1 || n % 240 == 0) {
		LOGI("aug_LocateDeviceInSpace call #%llu (rate-limited 1/240)",
		     (unsigned long long)n);
	}

	// Snapshot the cached pose under lock.
	float pos[3];
	float rot[4];
	bool  tracking;
	{
		std::lock_guard<std::mutex> lk(g_pose_mu);
		pos[0]    = g_cached_pose.pos[0];
		pos[1]    = g_cached_pose.pos[1];
		pos[2]    = g_cached_pose.pos[2];
		rot[0]    = g_cached_pose.rot[0];
		rot[1]    = g_cached_pose.rot[1];
		rot[2]    = g_cached_pose.rot[2];
		rot[3]    = g_cached_pose.rot[3];
		tracking  = g_cached_pose.tracking;
	}

	if (!tracking) {
		// ARCore has not converged yet (typical for the first few
		// hundred ms after start). Leave the baseline -- whatever
		// Monado's gyro+accel fusion produced -- in place, just
		// strip the position-tracked bits so the client knows the
		// position is not yet reliable.
		location->locationFlags &=
		    static_cast<XrSpaceLocationFlags>(~XR_SPACE_LOCATION_POSITION_TRACKED_BIT);
		return XR_SUCCESS;
	}

	location->pose.position.x    = pos[0];
	location->pose.position.y    = pos[1];
	location->pose.position.z    = pos[2];
	location->pose.orientation.x = rot[0];
	location->pose.orientation.y = rot[1];
	location->pose.orientation.z = rot[2];
	location->pose.orientation.w = rot[3];
	location->locationFlags =
	    XR_SPACE_LOCATION_ORIENTATION_VALID_BIT  |
	    XR_SPACE_LOCATION_POSITION_VALID_BIT     |
	    XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT|
	    XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
	return XR_SUCCESS;
}

} // extern "C"
