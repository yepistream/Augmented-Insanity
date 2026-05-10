// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Aug-Ins Mercury (ARCore camera) hand-tracking module.
//
// Lifecycle:
//   aug_onModuleLoad      -- validate host API >= v2, cache it
//   aug_onConnect         -- subscribe to camera-frame broker (publisher: the
//                            arcore-headpose module); register hand-tracker
//                            callback so the runtime stub xdev can route
//                            xrLocateHandJointsEXT queries here.
//   on_frame (callback)   -- on the first frame, use the intrinsics ARCore
//                            published to build a t_stereo_camera_calibration
//                            (mono Ã¢â€ â€™ duplicate view[0] into view[1]) and create
//                            Mercury via ht_device_create. Then on every frame
//                            wrap the Y plane in an xrt_frame and push to the
//                            left sink. Mercury runs its own worker thread.
//   joints_cb (callback)  -- forward to xrt_device_get_hand_tracking on
//                            Mercury's xdev.
//   aug_runtimeFinished   -- unsubscribe, unregister, destroy.

#include "augins_module_abi.h"

#include "ht/ht_interface.h"
#include "tracking/t_hand_tracking.h"
#include "tracking/t_tracking.h"
#include "util/u_frame.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_frame.h"
#include "xrt/xrt_results.h"

#include <android/log.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

#define TAG "AugIns.MercuryARCore"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// ---------------------------------------------------------------------------
// Module globals
// ---------------------------------------------------------------------------

static const struct aug_host_api *g_host = nullptr;

static std::mutex                          g_mu;
static struct t_stereo_camera_calibration *g_calib   = nullptr;
static struct xrt_frame_context            g_xfctx   = {};
static struct xrt_slam_sinks              *g_sinks   = nullptr;
static struct xrt_device                  *g_ht_xdev = nullptr;
static std::atomic<bool>                   g_mercury_ready{false};
static std::atomic<bool>                   g_mercury_create_failed{false};

// Lazy-subscribe gate. We do NOT subscribe to camera frames in aug_onConnect
// any more; subscription is deferred to the FIRST joints_cb invocation. That
// way Mercury never spins up if the client doesn't actually use hand tracking,
// and ARCore gets the entire init window CPU-uncontested when the client
// only cares about head pose. The first joint query returns is_active=false
// while Mercury bootstraps; subsequent queries get real data.
static std::atomic<bool>                   g_subscribed{false};

// Frame decimation: ARCore publishes camera frames at ~30 Hz; we forward only
// every Nth frame to Mercury so ONNX inference runs at ~10 Hz instead of 30 Hz.
// Cuts Mercury CPU usage by ~3x, which keeps ARCore VIO's feature-extraction
// thread (target ~33ms per frame at 30fps) from being preempted into 100ms+
// stalls that destabilize visual-inertial tracking.
//
// 10 Hz is plenty for hand-UI interaction; the Mercury Quest reference also
// runs in this regime when CPU-constrained.
static std::atomic<uint32_t> g_frame_counter{0};
static constexpr uint32_t kDecimateEveryN = 3;

// Captured during aug_onModuleLoad (when the runtime's per-module TLS context
// is valid). Used from on_camera_frame which fires on the arcore-headpose
// worker thread where get_module_data_dir() would return empty.
static std::string g_models_dir;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static struct t_stereo_camera_calibration *
build_calib_from_intrinsics(const struct aug_camera_intrinsics &intr, uint32_t width, uint32_t height)
{
    // ARCore reports an undistorted pinhole-equivalent model. We pick OPENCV
    // RADTAN_5 with all-zero distortion as the closest "neutral" representation
    // that Mercury accepts. (RADTAN_5 is the Mercury default for monocular use.)
    struct t_stereo_camera_calibration *calib = nullptr;
    t_stereo_camera_calibration_alloc(&calib, T_DISTORTION_OPENCV_RADTAN_5);

    auto fill_view = [&](struct t_camera_calibration &v) {
        v.image_size_pixels.w = (int)width;
        v.image_size_pixels.h = (int)height;

        std::memset(v.intrinsics, 0, sizeof(v.intrinsics));
        v.intrinsics[0][0] = intr.fx;
        v.intrinsics[1][1] = intr.fy;
        v.intrinsics[0][2] = intr.cx;
        v.intrinsics[1][2] = intr.cy;
        v.intrinsics[2][2] = 1.0;

        std::memset(v.distortion_parameters_as_array, 0, sizeof(v.distortion_parameters_as_array));
    };
    fill_view(calib->view[0]);
    fill_view(calib->view[1]);

    // Mono: identity stereo extrinsics. Mercury duplicates view[0] internally
    // when create_info.view_count == 1, but a sane identity is still needed.
    std::memset(calib->camera_translation, 0, sizeof(calib->camera_translation));
    std::memset(calib->camera_rotation, 0, sizeof(calib->camera_rotation));
    calib->camera_rotation[0][0] = 1.0;
    calib->camera_rotation[1][1] = 1.0;
    calib->camera_rotation[2][2] = 1.0;

    return calib;
}

static bool
ensure_mercury_created(const struct aug_camera_intrinsics &intr, uint32_t width, uint32_t height)
{
    if (g_mercury_ready.load(std::memory_order_acquire)) {
        return true;
    }
    if (g_mercury_create_failed.load(std::memory_order_acquire)) {
        return false; // Stop spamming logs if creation failed once.
    }
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_ht_xdev != nullptr) {
        return true;
    }

    g_calib = build_calib_from_intrinsics(intr, width, height);
    if (g_calib == nullptr) {
        LOGE("ensure_mercury_created: t_stereo_camera_calibration_alloc failed");
        return false;
    }

    struct t_hand_tracking_create_info ci = {};
    ci.view_count = 1; // mono Ã¢â‚¬â€ Mercury's mono path
    // Leave cams_info zeroed (no vignette boundary, default orientation 0).

    // Tell Mercury where to find the bundled ONNX models. The dir was captured
    // during aug_onModuleLoad when host API TLS was valid (we can't call
    // get_module_data_dir from this thread Ã¢â‚¬â€ it's the arcore-headpose worker).
    if (!g_models_dir.empty()) {
        ci.models_dir = g_models_dir.c_str();
    }

    struct xrt_slam_sinks *sinks = nullptr;
    struct xrt_device     *xdev  = nullptr;

    int rc = ht_device_create(&g_xfctx, g_calib, ci, &sinks, &xdev);
    if (rc != 0 || xdev == nullptr || sinks == nullptr) {
        LOGE("ht_device_create failed (rc=%d). models_dir='%s'. Will not retry.",
             rc, g_models_dir.empty() ? "<not captured>" : g_models_dir.c_str());
        g_mercury_create_failed.store(true, std::memory_order_release);
        return false;
    }

    g_sinks   = sinks;
    g_ht_xdev = xdev;
    g_mercury_ready.store(true, std::memory_order_release);

    LOGI("Mercury ready: xdev=%p sinks=%p (mono, %ux%u, fx=%.1f fy=%.1f cx=%.1f cy=%.1f)",
         (void *)xdev, (void *)sinks, width, height, intr.fx, intr.fy, intr.cx, intr.cy);
    return true;
}

// ---------------------------------------------------------------------------
// Camera-frame subscriber
// ---------------------------------------------------------------------------

static void
on_camera_frame(const uint8_t *y_data,
                uint32_t width,
                uint32_t height,
                uint32_t stride_bytes,
                int64_t timestamp_ns,
                const struct aug_camera_intrinsics *intr,
                void *userdata)
{
    (void)userdata;

    // Frame decimation: drop 2 of every 3 frames so Mercury inference runs at
    // ~10 Hz instead of ~30 Hz. See kDecimateEveryN comment near top.
    {
        uint32_t n = g_frame_counter.fetch_add(1, std::memory_order_relaxed);
        if (n % kDecimateEveryN != 0) {
            return;
        }
    }

    if (y_data == nullptr || width == 0 || height == 0) {
        return;
    }
    if (intr == nullptr) {
        // Without intrinsics we can't build calibration; drop the frame and
        // wait for the producer to start advertising them.
        return;
    }

    if (!ensure_mercury_created(*intr, width, height)) {
        return;
    }

    struct xrt_slam_sinks *sinks = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        sinks = g_sinks;
    }
    if (sinks == nullptr || sinks->cam_count < 1 || sinks->cams[0] == nullptr) {
        return;
    }

    // Allocate an L8 xrt_frame and copy the Y plane in. (The broker buffer is
    // borrowed and is only valid for the duration of this callback, so we must
    // copy.) u_frame_create_one_off allocates a contiguous buffer with stride
    // equal to width * bpp; since our source has its own stride we copy
    // row-by-row.
    struct xrt_frame *xf = nullptr;
    u_frame_create_one_off(XRT_FORMAT_L8, width, height, &xf);
    if (xf == nullptr) {
        return;
    }
    for (uint32_t row = 0; row < height; ++row) {
        std::memcpy(xf->data + row * xf->stride, y_data + row * stride_bytes, width);
    }
    xf->timestamp = timestamp_ns;
    xf->source_timestamp = timestamp_ns;

    // Push to the left sink. Mercury's async wrapper handles ownership; we
    // release our reference after pushing so the sink's retained ref is what
    // keeps it alive until consumed.
    xrt_sink_push_frame(sinks->cams[0], xf);
    xrt_frame_reference(&xf, nullptr);
}

// ---------------------------------------------------------------------------
// Hand-tracker producer Ã¢â‚¬â€ called by the runtime stub xdev.
// ---------------------------------------------------------------------------

static void
joints_cb(uint32_t handed,
          int64_t at_timestamp_ns,
          struct xrt_hand_joint_set *out,
          int64_t *out_timestamp_ns,
          void *userdata)
{
    (void)userdata;
    if (out == nullptr) {
        return;
    }
    std::memset(out, 0, sizeof(*out));
    if (out_timestamp_ns != nullptr) {
        *out_timestamp_ns = at_timestamp_ns;
    }

    // First-call lazy-subscribe to the camera-frame broker. This is what
    // actually starts Mercury inference flowing. We do this here (not in
    // aug_onConnect) so head-pose-only clients never trigger Mercury and
    // ARCore gets to init undisturbed.
    bool expected = false;
    if (g_subscribed.compare_exchange_strong(expected, true)) {
        if (g_host != nullptr && g_host->subscribe_camera_frame != nullptr) {
            g_host->subscribe_camera_frame(on_camera_frame, nullptr);
            LOGI("joints_cb: first call Ã¢â‚¬â€ subscribed to camera-frame broker; "
                 "Mercury will spin up on next ARCore frame");
        }
    }

    struct xrt_device *xdev = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        xdev = g_ht_xdev;
    }
    if (xdev == nullptr) {
        return; // Mercury not yet created Ã¢â‚¬â€ return inactive set.
    }

    enum xrt_input_name name = (handed == 0) ? XRT_INPUT_HT_UNOBSTRUCTED_LEFT
                                              : XRT_INPUT_HT_UNOBSTRUCTED_RIGHT;
    int64_t mercury_ts = at_timestamp_ns;
    xrt_device_get_hand_tracking(xdev, name, at_timestamp_ns, out, &mercury_ts);
    if (out_timestamp_ns != nullptr) {
        *out_timestamp_ns = mercury_ts;
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

extern "C" {

void
aug_onModuleLoad(void *args)
{
    const auto *api = static_cast<const struct aug_host_api *>(args);
    if (api == nullptr || api->version < 2u) {
        LOGE("aug_onModuleLoad: host API too old (got %u, need >= 2)",
             api ? api->version : 0u);
        return;
    }
    g_host = api;

    // Capture the per-module data dir NOW. The runtime's get_module_data_dir
    // is implemented via thread-local set during lifecycle dispatch; calling
    // it later (e.g. from the camera-frame subscriber callback, which runs on
    // the arcore-headpose worker thread) returns empty. Cache it here.
    if (g_host->get_module_data_dir != nullptr) {
        const char *base = g_host->get_module_data_dir();
        if (base != nullptr && base[0] != '\0') {
            g_models_dir = std::string(base) + "/models";
            LOGI("aug_onModuleLoad: cached models dir = %s", g_models_dir.c_str());
        } else {
            LOGW("aug_onModuleLoad: get_module_data_dir() returned empty (TLS not set?)");
        }
    }

    LOGI("aug_onModuleLoad: host API v%u accepted", api->version);
}

void
aug_onConnect(void *args)
{
    (void)args;
    if (g_host == nullptr) {
        LOGE("aug_onConnect: no host API");
        return;
    }
    if (g_host->register_hand_tracker == nullptr || g_host->subscribe_camera_frame == nullptr) {
        LOGE("aug_onConnect: host API missing v2 functions (runtime too old?)");
        return;
    }

    // Tell the runtime's stub hand-tracker xdev to route joints queries to us.
    // We register the callback now but defer the camera-frame subscription
    // until the first joints query Ã¢â‚¬â€ see g_subscribed and joints_cb. Rationale:
    // ARCore needs ~1-2 seconds of unobstructed CPU to bootstrap its VIO;
    // Mercury's ONNX inference would otherwise starve it (observed: 0/20 RANSAC
    // inliers, VIO stuck in NotTracking Ã¢â€ â€™ client falls back to gyro+accel
    // 3DoF). Lazy-subscribing means clients that only want head pose never
    // pay the Mercury cost, and clients that DO want hand tracking get it
    // shortly after their first xrLocateHandJointsEXT call.
    g_host->register_hand_tracker(joints_cb, nullptr);
    LOGI("aug_onConnect: hand-tracker callback registered (camera subscription deferred)");
}

void
aug_runtimeFinished(void *args)
{
    (void)args;
    if (g_host != nullptr) {
        if (g_host->register_hand_tracker != nullptr) {
            g_host->register_hand_tracker(nullptr, nullptr);
        }
        if (g_host->subscribe_camera_frame != nullptr) {
            g_host->subscribe_camera_frame(nullptr, nullptr);
        }
    }

    std::lock_guard<std::mutex> lk(g_mu);
    if (g_xfctx.nodes != nullptr || g_ht_xdev != nullptr) {
        // Tearing the frame context down also destroys Mercury's async wrapper,
        // sync object, and the ht_device that wraps them. After this g_ht_xdev
        // is dangling; clear it.
        xrt_frame_context_destroy_nodes(&g_xfctx);
        g_ht_xdev = nullptr;
        g_sinks   = nullptr;
    }
    if (g_calib != nullptr) {
        t_stereo_camera_calibration_reference(&g_calib, nullptr);
    }
    g_mercury_ready.store(false);
    g_mercury_create_failed.store(false);
    g_subscribed.store(false); // re-arm the lazy gate for the next session
    g_frame_counter.store(0);  // first frame of next session is always kept
    LOGI("aug_runtimeFinished: Mercury torn down");
}

} // extern "C"
