// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Aug-Ins ARCore head-pose module.
//
// Intercepts three IPC calls:
//   aug_spaceCreateSemanticIds Ã¢â‚¬â€ caches the world-locked reference-space IDs
//                                (LOCAL / LOCAL_FLOOR / STAGE / UNBOUNDED) and
//                                the VIEW space ID.
//   xrLocateSpace              Ã¢â‚¬â€ if the query is view-in-{world-locked-space},
//                                overwrites the reply with the latest
//                                ARCore-tracked pose. Server-side locate_space
//                                is in-process so we can only override the
//                                final IPC reply here.
//   aug_deviceGetTrackedPose   Ã¢â‚¬â€ synthetic key for the per-xdev IPC call
//                                device_get_tracked_pose. We override only
//                                XRT_INPUT_GENERIC_HEAD_POSE, replacing the
//                                Android-Sensors gyro+accel fused pose with
//                                ARCore's. This is what xrLocateViews uses
//                                for T_xdev_head; without it, sensor rotation
//                                pollutes the final view orientation even when
//                                space_locate_device is overridden.
//
// We deliberately do NOT hook space_locate_device anymore: with the head
// xdev's get_tracked_pose returning ARCore, T_base_xdev gets to be Monado's
// natural value (identity for LOCAL, floor offset for LOCAL_FLOOR, etc.) and
// the relation chain composes correctly as ARCore Ã¢Ë†Ëœ floor_offset.
//
// Also publishes the YUV camera frame's Y plane via host API v2's
// publish_camera_frame_y8 every tick, so other modules (e.g. mercury hand
// tracking) can subscribe to grayscale frames without spinning up a second
// ArSession (which is impossible Ã¢â‚¬â€ only one ArSession per process).
//
// All other queries (hand poses, action poses, etc.) pass through unchanged
// because the space-id and input-name guards fail fast.

#include "augins_module_abi.h"
#include "arcore_instance.h"

#include <android/log.h>
#include <jni.h>

#include <atomic>
#include <chrono>
#include <climits>
#include <cstdint>
#include <mutex>
#include <thread>

#define TAG "AugIns.ARCore"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// ---------------------------------------------------------------------------
// Mirror structs Ã¢â‚¬â€ layout must match the generated ipc_protocol_generated.h
// structs for the two IPC calls we intercept.
//
// READING via a mirror: a layout mismatch produces a wrong space_id, so the
// xrLocateSpace guard fails and the hook no-ops. Safe.
// WRITING the reply: delegated entirely to g_host->set_locate_space_relation,
// which lives in the service and owns the generated cast.
// ---------------------------------------------------------------------------

// Mirrors ipc_space_create_semantic_ids_reply
struct aug_reply_semantic_ids
{
    int32_t  result;
    uint32_t root_id;
    uint32_t view_id;
    uint32_t local_id;
    uint32_t local_floor_id;
    uint32_t stage_id;
    uint32_t unbounded_id;
};

// Mirrors ipc_space_locate_space_msg
struct aug_msg_locate_space
{
    int32_t         cmd;            // enum ipc_command Ã¢â‚¬â€ same width as int32_t
    uint32_t        base_space_id;
    struct xrt_pose base_offset;
    int64_t         at_timestamp;
    uint32_t        space_id;
    struct xrt_pose offset;
};

// Mirrors ipc_device_get_tracked_pose_msg. enum xrt_input_name is an int32
// (Monado uses sized enums for IPC stability).
struct aug_msg_device_get_tracked_pose
{
    int32_t cmd;
    uint32_t id;            // xdev id
    int32_t name;           // enum xrt_input_name (e.g. XRT_INPUT_GENERIC_HEAD_POSE)
    int64_t at_timestamp;
};

// ---------------------------------------------------------------------------
// Module globals
// ---------------------------------------------------------------------------

static const struct aug_host_api *g_host    = nullptr;
static struct arcore_min          g_arcore  = {};

static std::thread           g_worker;
static std::atomic<bool>     g_stop{false};
static std::atomic<bool>     g_arcore_started{false};

static std::mutex                g_pose_mu;
static struct xrt_space_relation g_cached_pose = {};
static bool                      g_pose_valid  = false;

static std::atomic<uint32_t> g_view_id{UINT32_MAX};

// World-locked reference spaces. Apps choose one for their play space:
//   LOCAL        Ã¢â‚¬â€ gravity-aligned, origin at where the app started.
//   LOCAL_FLOOR  Ã¢â‚¬â€ same as LOCAL but origin at floor height.
//   STAGE        Ã¢â‚¬â€ predefined room boundaries (we don't have any; runtime
//                  fakes one).
//   UNBOUNDED    Ã¢â‚¬â€ SLAM-style with no fixed origin, used by AR apps.
//
// On a phone all four are effectively the same world frame (we have no real
// floor or stage detection). Godot defaults to LOCAL_FLOOR when available;
// helloxr uses LOCAL. We intercept view-in-* against any of these so the
// ARCore pose flows regardless of which the client picked.
//
// (Y-offset for floor-relative spaces is currently ignored Ã¢â‚¬â€ head pose ends up
// rendered at headset height not floor height. Acceptable for v1; tracked as a
// follow-up.)
static std::atomic<uint32_t> g_local_id{UINT32_MAX};
static std::atomic<uint32_t> g_local_floor_id{UINT32_MAX};
static std::atomic<uint32_t> g_stage_id{UINT32_MAX};
static std::atomic<uint32_t> g_unbounded_id{UINT32_MAX};

// True if the given space id is one of the world-locked reference spaces we
// can override head pose against.
static inline bool
is_world_locked_base(uint32_t space_id)
{
    if (space_id == UINT32_MAX) return false;
    return space_id == g_local_id.load(std::memory_order_relaxed) ||
           space_id == g_local_floor_id.load(std::memory_order_relaxed) ||
           space_id == g_stage_id.load(std::memory_order_relaxed) ||
           space_id == g_unbounded_id.load(std::memory_order_relaxed);
}

// Cached camera intrinsics (filled on first successful arcore_min_get_intrinsics).
static struct aug_camera_intrinsics g_cam_intr  = {};
static std::atomic<bool>            g_intr_valid{false};

// ---------------------------------------------------------------------------
// Worker thread Ã¢â‚¬â€ runs arcore_min_tick in a loop, caches the pose
// ---------------------------------------------------------------------------

static void
arcore_worker(JavaVM *vm, jobject ctx_global)
{
    JNIEnv *env = nullptr;
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
        LOGE("AttachCurrentThread failed Ã¢â‚¬â€ cannot start ARCore");
        // env is null, can't safely delete ctx_global; leak it (fatal situation)
        return;
    }

    struct arcore_min_config cfg;
    arcore_min_config_set_defaults(&cfg);
    cfg.focus_mode     = AUTO_FOCUS_ENABLED;
    cfg.camera_hz_mode = MAX_ARCAMERA_HZ;

    // arcore_min_start_ex calls get_env() which uses GetEnv() first; since we
    // are already attached, did_attach stays false so arcore_min cleanup will
    // NOT call DetachCurrentThread.  We own the attachment and must detach.
    if (!arcore_min_start_ex(&g_arcore, vm, ctx_global, &cfg)) {
        LOGE("arcore_min_start_ex failed Ã¢â‚¬â€ is Google Play Services for AR installed?");
        env->DeleteGlobalRef(ctx_global); // delete BEFORE detaching
        vm->DetachCurrentThread();
        return;
    }
    LOGI("ARCore session started");

    while (!g_stop.load(std::memory_order_relaxed)) {
        float   pos[3]    = {};
        float   rot[4]    = {};
        bool    tracking  = false;
        int64_t ts_ns     = 0;

        if (arcore_min_tick(&g_arcore, pos, rot, &tracking, &ts_ns)) {
            struct xrt_space_relation rel = {};
            if (tracking) {
                rel.pose.position.x    = pos[0];
                rel.pose.position.y    = pos[1];
                rel.pose.position.z    = pos[2];
                rel.pose.orientation.x = rot[0];
                rel.pose.orientation.y = rot[1];
                rel.pose.orientation.z = rot[2];
                rel.pose.orientation.w = rot[3];
                rel.relation_flags = (enum xrt_space_relation_flags)(
                    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
                    XRT_SPACE_RELATION_POSITION_VALID_BIT    |
                    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |
                    XRT_SPACE_RELATION_POSITION_TRACKED_BIT);
            }
            std::lock_guard<std::mutex> lk(g_pose_mu);
            g_cached_pose = rel;
            g_pose_valid  = tracking;
        }

        // Camera-frame broker producer side: harvest the latest YUV frame's
        // Y plane and fan it out to subscribers (e.g. mercury hand tracking).
        // Skip if the runtime is older than v2 (no broker available).
        if (g_host != nullptr && g_host->version >= 2 && g_host->publish_camera_frame_y8 != nullptr) {
            // Fetch intrinsics once. ARCore only returns valid intrinsics
            // after the first frame; retry until success, cache afterwards.
            if (!g_intr_valid.load(std::memory_order_acquire)) {
                struct arcore_min_intrinsics intr = {};
                if (arcore_min_get_intrinsics(&g_arcore, /*use_image_intrinsics*/ true, &intr)) {
                    g_cam_intr.fx = intr.fx;
                    g_cam_intr.fy = intr.fy;
                    g_cam_intr.cx = intr.cx;
                    g_cam_intr.cy = intr.cy;
                    g_cam_intr.distortion_model = AUG_CAMERA_DISTORTION_PINHOLE;
                    // ARCore reports an undistorted pinhole-equivalent model;
                    // distortion[] stays zero-filled.
                    g_intr_valid.store(true, std::memory_order_release);
                    LOGI("ARCore intrinsics: fx=%.1f fy=%.1f cx=%.1f cy=%.1f (%dx%d)",
                         intr.fx, intr.fy, intr.cx, intr.cy, intr.width, intr.height);
                }
            }

            struct arcore_min_image img = {};
            if (arcore_min_acquire_camera_image(&g_arcore, &img)) {
                // YUV_420_888: plane[0] is Y (8-bit luminance) Ã¢â‚¬â€ exactly L8.
                if (img.num_planes >= 1 && img.plane_data[0] != nullptr && img.width > 0 &&
                    img.height > 0) {
                    g_host->publish_camera_frame_y8(
                        img.plane_data[0], (uint32_t)img.width, (uint32_t)img.height,
                        (uint32_t)img.plane_row_stride[0], img.timestamp_ns,
                        g_intr_valid.load(std::memory_order_acquire) ? &g_cam_intr : nullptr);
                }
                arcore_min_release_image(&g_arcore, &img);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // arcore_min_stop will NOT detach (did_attach=false since we pre-attached).
    arcore_min_stop(&g_arcore);
    LOGI("ARCore session stopped");
    env->DeleteGlobalRef(ctx_global); // delete BEFORE detaching
    vm->DetachCurrentThread();
}

// ---------------------------------------------------------------------------
// Exported symbols Ã¢â‚¬â€ lifecycle hooks
// ---------------------------------------------------------------------------

extern "C" {

void
aug_onModuleLoad(void *args)
{
    const auto *api = static_cast<const struct aug_host_api *>(args);
    // This module needs v2 (publish_camera_frame_y8). Forward-compat: accept
    // any runtime version >= 2.
    if (api == nullptr || api->version < 2u) {
        LOGE("aug_onModuleLoad: host API too old (got %u, need >= 2)",
             api ? api->version : 0u);
        return;
    }
    g_host = api;
    LOGI("aug_onModuleLoad: host API v%u accepted", api->version);
}

void
aug_onConnect(void *args)
{
    (void)args;

    if (!g_host) {
        LOGE("aug_onConnect: no host API");
        return;
    }

    // Start the ARCore session only once across all client connections.
    if (g_arcore_started.exchange(true)) {
        LOGI("aug_onConnect: ARCore worker already running");
        return;
    }

    auto *vm  = static_cast<JavaVM *>(g_host->get_vm());
    auto *ctx = static_cast<jobject>(g_host->get_context());
    if (vm == nullptr || ctx == nullptr) {
        LOGE("aug_onConnect: VM or context not available");
        g_arcore_started.store(false);
        return;
    }

    // Create a global ref so the worker thread can hold it after this stack unwinds.
    JNIEnv *env = nullptr;
    vm->AttachCurrentThread(&env, nullptr);
    jobject ctx_global = env->NewGlobalRef(ctx);
    vm->DetachCurrentThread();

    g_stop.store(false);
    g_worker = std::thread(arcore_worker, vm, ctx_global);
    LOGI("aug_onConnect: ARCore worker started");
}

void
aug_runtimeFinished(void *args)
{
    (void)args;

    g_stop.store(true);
    if (g_worker.joinable()) {
        g_worker.join();
    }
    g_arcore_started.store(false);
    LOGI("aug_runtimeFinished: worker joined");
}

// ---------------------------------------------------------------------------
// Exported symbols Ã¢â‚¬â€ IPC dispatch hooks
// ---------------------------------------------------------------------------

int32_t
aug_spaceCreateSemanticIds(void *ics, void *msg, void *reply, void *unused)
{
    (void)ics;
    (void)msg;
    (void)unused;

    const auto *r = static_cast<const struct aug_reply_semantic_ids *>(reply);
    if (r == nullptr) {
        return AUG_OK;
    }

    g_view_id.store(r->view_id,            std::memory_order_relaxed);
    g_local_id.store(r->local_id,          std::memory_order_relaxed);
    g_local_floor_id.store(r->local_floor_id, std::memory_order_relaxed);
    g_stage_id.store(r->stage_id,          std::memory_order_relaxed);
    g_unbounded_id.store(r->unbounded_id,  std::memory_order_relaxed);
    LOGI("aug_spaceCreateSemanticIds: view=%u local=%u local_floor=%u stage=%u unbounded=%u",
         r->view_id, r->local_id, r->local_floor_id, r->stage_id, r->unbounded_id);
    return AUG_OK;
}

int32_t
xrLocateSpace(void *ics, void *msg, void *reply, void *unused)
{
    (void)ics;
    (void)unused;

    if (g_host == nullptr || msg == nullptr || reply == nullptr) {
        return AUG_OK;
    }

    const uint32_t view_id = g_view_id.load(std::memory_order_relaxed);

    // View ID not yet captured Ã¢â‚¬â€ pass through.
    if (view_id == UINT32_MAX) {
        return AUG_OK;
    }

    const auto *m = static_cast<const struct aug_msg_locate_space *>(msg);

    // Only intercept view-in-{any world-locked space} queries. Different
    // clients pick different play spaces (helloxr Ã¢â€ â€™ LOCAL, Godot Ã¢â€ â€™ LOCAL_FLOOR
    // typically). On a phone they're all the same world frame to us.
    if (m->space_id != view_id || !is_world_locked_base(m->base_space_id)) {
        return AUG_OK;
    }

    struct xrt_space_relation rel;
    {
        std::lock_guard<std::mutex> lk(g_pose_mu);
        if (!g_pose_valid) {
            return AUG_OK;
        }
        rel = g_cached_pose;
    }

    g_host->set_locate_space_relation(reply, &rel);
    return AUG_OK;
}

// device_get_tracked_pose IPC fires per-xdev. xrLocateViews calls this for the
// head xdev with name == XRT_INPUT_GENERIC_HEAD_POSE to populate T_xdev_head.
// We override the reply with our ARCore pose so the head's own-frame pose is
// ARCore-driven rather than gyro+accel-driven. We DO NOT touch any other
// input name (XRT_INPUT_*_AIM_POSE, XRT_INPUT_HT_*, etc.) Ã¢â‚¬â€ those go through
// to controllers / hand trackers untouched.
//
// Reply layout matches space_locate_space (struct xrt_space_relation), so we
// reuse set_locate_space_relation.
int32_t
aug_deviceGetTrackedPose(void *ics, void *msg, void *reply, void *unused)
{
    (void)ics;
    (void)unused;

    if (g_host == nullptr || msg == nullptr || reply == nullptr) {
        return AUG_OK;
    }

    const auto *m = static_cast<const struct aug_msg_device_get_tracked_pose *>(msg);

    // Filter: only the head xdev's GENERIC_HEAD_POSE input gets the override.
    // We don't need to know the xdev id explicitly Ã¢â‚¬â€ only the head device
    // exposes this input on a phone runtime.
    if (m->name != (int32_t)XRT_INPUT_GENERIC_HEAD_POSE) {
        return AUG_OK;
    }

    struct xrt_space_relation rel;
    {
        std::lock_guard<std::mutex> lk(g_pose_mu);
        if (!g_pose_valid) {
            return AUG_OK;
        }
        rel = g_cached_pose;
    }

    g_host->set_locate_space_relation(reply, &rel);
    return AUG_OK;
}

} // extern "C"
