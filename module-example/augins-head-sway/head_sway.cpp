// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Aug-Ins head-sway tutorial module.
//
// What it does
// ------------
// Adds a sinusoidal X-axis offset to the head pose returned to OpenXR clients,
// producing an obvious side-to-side "swaying" motion of the rendered scene.
// Useful as a self-test that any module can intercept the head-pose IPC path
// and as a minimal worked example for new module authors.
//
// What it teaches
// ---------------
// 1. The .augins lifecycle (aug_onModuleLoad, aug_runtimeInit, aug_onConnect).
// 2. Receiving the host API table at module load.
// 3. Hooking an IPC dispatch call (aug_deviceGetTrackedPose) and modifying
//    the reply before it is sent back to the client.
// 4. Filtering hooks so they only fire for the cases you care about
//    (here: the head xdev's GENERIC_HEAD_POSE input -- not controllers,
//    not hand trackers).
// 5. Mirror structs: how to read fields out of an IPC message without
//    pulling the runtime's generated headers into your module.
//
// What it deliberately does NOT teach
// -----------------------------------
// - The host API's frame broker (publish_camera_frame_y8 /
//   subscribe_camera_frame). See augins-arcore-headpose and
//   augins-mercury-handtracking-arcore.
// - Stub xdev / register_hand_tracker. See augins-mercury-handtracking-arcore.
// - Vendoring third-party SDKs (ARCore, OpenCV, ONNX). See the production
//   sample modules.
//
// How to verify it works
// ----------------------
// Install the runtime, push head-sway.augins into the modules directory,
// restart the runtime, then launch any OpenXR client. Look for the LOGI
// lines below in `adb logcat -s "AugInsHeadSway:*"`. Visually, the rendered
// scene's head pose will sway left-right with period kSwayPeriodSeconds
// and amplitude kSwayAmplitudeMeters.

#include "augins_module_abi.h"   // aug_host_api, AUG_OK, etc.
#include "xrt/xrt_defines.h"     // xrt_pose, xrt_space_relation, XRT_INPUT_GENERIC_HEAD_POSE

#include <android/log.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>

#define LOG_TAG "AugInsHeadSway"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Sway tuning. Edit and rebuild to change the look. 4 second period and 30 cm
// amplitude is large enough to be obvious without making the user motion-sick.
constexpr float kSwayPeriodSeconds   = 4.0f;
constexpr float kSwayAmplitudeMeters = 0.30f;
constexpr float kTwoPi               = 6.283185307179586f;

// ---------------------------------------------------------------------------
// Mirror structs.
//
// The .augins module cannot include the runtime's generated IPC headers
// (ipc_protocol_generated.h) because the loader builds modules outside the
// runtime tree. Instead we redeclare the minimum fields we need in layouts
// that match the generated structs byte-for-byte. If the runtime's IPC
// schema changes incompatibly, this module's filter will mismatch and the
// hook will silently no-op -- the runtime keeps working.
// ---------------------------------------------------------------------------

// Mirrors ipc_device_get_tracked_pose_msg from
// src/xrt/ipc/shared/proto/50-device.json.
//
// Field-by-field correspondence:
//   cmd          -> enum ipc_command (32-bit, signed in proto.py)
//   id           -> uint32_t          (xdev id)
//   name         -> enum xrt_input_name (32-bit signed enum in C)
//   at_timestamp -> int64_t           (monotonic nanoseconds)
struct head_sway_msg_dev_get_tracked_pose
{
    int32_t  cmd;
    uint32_t id;
    int32_t  name;
    int64_t  at_timestamp;
};

// Mirrors the prefix of ipc_device_get_tracked_pose_reply -- we only read the
// xrt_space_relation. The runtime fills it in BEFORE our hook runs, so we
// can read it, modify it, and write it back via the host API.
struct head_sway_reply_relation
{
    int32_t                   result;     // xrt_result_t, in practice XRT_SUCCESS
    struct xrt_space_relation relation;
};

// ---------------------------------------------------------------------------
// Module-globals.
// ---------------------------------------------------------------------------

static const struct aug_host_api *g_host = nullptr;

// Steady-clock reference used to compute the sway phase. Captured in
// aug_onModuleLoad so the sway starts at zero offset and is reproducible.
static std::atomic<int64_t> g_t0_ns{0};

static int64_t
monotonic_ns_now()
{
    using clock = std::chrono::steady_clock;
    auto d = clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
}

// ---------------------------------------------------------------------------
// Lifecycle hooks. None of these are required, but implementing them at
// minimum gives module developers a clear log trail of when the runtime is
// calling into the module.
// ---------------------------------------------------------------------------

extern "C" {

void
aug_onModuleLoad(void *args)
{
    const auto *api = static_cast<const struct aug_host_api *>(args);
    if (api == nullptr) {
        LOGE("aug_onModuleLoad: host API table is NULL");
        return;
    }
    // We use only set_locate_space_relation, which is in v1. Accept any
    // runtime version >= 1. Newer versions are forward-compatible (additive
    // ABI).
    if (api->version < 1u) {
        LOGE("aug_onModuleLoad: host API too old (got %u, need >= 1)", api->version);
        return;
    }
    g_host = api;
    g_t0_ns.store(monotonic_ns_now(), std::memory_order_release);
    LOGI("aug_onModuleLoad: host API v%u accepted", api->version);
}

void
aug_runtimeInit(void * /*args*/)
{
    LOGI("aug_runtimeInit: dispatch table built; sway period=%.2fs amplitude=%.2fm",
         kSwayPeriodSeconds, kSwayAmplitudeMeters);
}

void
aug_onConnect(void * /*args*/)
{
    LOGI("aug_onConnect: an OpenXR client just attached");
}

// ---------------------------------------------------------------------------
// IPC hook: aug_deviceGetTrackedPose.
//
// This synthetic xr-name is mapped from the IPC call device_get_tracked_pose
// in src/xrt/ipc/shared/proto.py's aug_ipc_to_xr dictionary. The runtime
// fires it for every per-xdev tracked-pose query that goes through IPC,
// most notably the head xdev's pose lookup that backs xrLocateViews.
//
// Hook signature (a0..a3) is documented in augins_module_abi.h:
//   a0 = volatile struct ipc_client_state *  (opaque session context)
//   a1 = ipc_device_get_tracked_pose_msg *   (input args, mirror struct)
//   a2 = ipc_device_get_tracked_pose_reply * (output args, set by runtime
//                                             before the hook fires)
//   a3 = NULL
//
// Filter: only modify GENERIC_HEAD_POSE replies. Controllers, hand trackers,
// eye trackers etc. all go through this same IPC and we must not perturb them.
// ---------------------------------------------------------------------------

int32_t
aug_deviceGetTrackedPose(void *ics, void *msg, void *reply, void *unused)
{
    (void)ics;
    (void)unused;
    if (g_host == nullptr || msg == nullptr || reply == nullptr) {
        return AUG_OK;
    }

    const auto *m = static_cast<const struct head_sway_msg_dev_get_tracked_pose *>(msg);
    if (m->name != static_cast<int32_t>(XRT_INPUT_GENERIC_HEAD_POSE)) {
        return AUG_OK; // Not the head pose -- pass through unchanged.
    }

    // Read the relation the runtime put into the reply.
    const auto *r = static_cast<const struct head_sway_reply_relation *>(reply);
    struct xrt_space_relation rel = r->relation;

    // Compute sway offset from elapsed time. sin(0) = 0 at startup so the
    // initial frame is unmodified.
    int64_t t0 = g_t0_ns.load(std::memory_order_acquire);
    float t_seconds = (monotonic_ns_now() - t0) / 1e9f;
    float phase = (kTwoPi * t_seconds) / kSwayPeriodSeconds;
    float dx = kSwayAmplitudeMeters * std::sin(phase);

    rel.pose.position.x += dx;

    // Write back. set_locate_space_relation overwrites reply->relation and
    // sets reply->result = XRT_SUCCESS. The next thing the runtime does is
    // ipc_send the reply to the client.
    g_host->set_locate_space_relation(reply, &rel);

    return AUG_OK;
}

void
aug_runtimeFinished(void * /*args*/)
{
    LOGI("aug_runtimeFinished: module shutting down");
}

} // extern "C"
