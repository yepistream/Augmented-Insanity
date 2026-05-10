// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * @file
 * @brief  Public ABI between Augmented Insanity (Aug-Ins) GRS and a .augins module.
 *
 * Module authors include this header to write a module. Intentionally does NOT
 * include <openxr/openxr.h> Ã¢â‚¬â€ modules that export hook functions with OpenXR
 * names (e.g. xrLocateSpace) would get a conflicting declaration if openxr.h
 * were pulled in transitively. Include <openxr/openxr.h> yourself only if you
 * need to compare signatures.
 *
 * @author Augmented Insanity contributors
 * @ingroup augins
 */

#pragma once

#include "xrt/xrt_defines.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// Host API table Ã¢â‚¬â€ passed to aug_onModuleLoad as `args`.
//
// Modules cannot include the runtime's generated IPC headers directly. To let
// modules safely modify reply structs (whose layout the runtime owns), the GRS
// passes a function-pointer table to every module at load time. Modules call
// these helpers; the runtime does the actual generated-struct casts.
//
// Versioning policy (forward-compatible, additive only):
//   - The runtime advertises its host-API version in `version`. Each module
//     declares the minimum version it requires and MUST check
//     `api->version >= module_required_version` on entry. A newer runtime
//     accepts older modules; an older runtime cannot serve newer modules.
//   - Bump AUG_HOST_API_VERSION whenever you add a field at the END of the
//     struct (additive). Never reorder or change the meaning of an existing
//     field Ã¢â‚¬â€ that breaks every existing module .augins on disk.
// -----------------------------------------------------------------------------

#define AUG_HOST_API_VERSION 2u

// -----------------------------------------------------------------------------
// Camera-frame broker types (used in v2 publish/subscribe pointers below).
// Single-channel 8-bit luminance only Ã¢â‚¬â€ Mercury and any future hand/eye
// trackers consume the Y plane; YUV chroma is dropped at the producer side.
// -----------------------------------------------------------------------------

enum aug_camera_distortion_model
{
	AUG_CAMERA_DISTORTION_PINHOLE = 0,
	AUG_CAMERA_DISTORTION_RADTAN5 = 1,
	AUG_CAMERA_DISTORTION_RADTAN8 = 2,
	AUG_CAMERA_DISTORTION_FISHEYE4 = 3,
};

struct aug_camera_intrinsics
{
	float fx, fy;        // focal length in pixels
	float cx, cy;        // principal point in pixels
	float distortion[8]; // model-specific; zero-fill if pinhole
	uint32_t distortion_model; // enum aug_camera_distortion_model as uint32_t
};

// Hand-tracker producer callback signature. Module installs one of these via
// `register_hand_tracker`; the runtime invokes it from the stub hand-tracker
// xdev when a client queries XR_EXT_hand_tracking joints.
//
//   handed         : 0 = left, 1 = right
//   at_timestamp_ns: monotonic ns the client asked about
//   out            : zero-initialized xrt_hand_joint_set the callback fills
//   out_timestamp_ns: actual ns of the joint sample returned (Ã¢â€°Â¤ at_timestamp_ns)
//   userdata       : opaque pointer the module supplied at register time
typedef void (*aug_hand_get_joints_fn)(uint32_t handed,
                                       int64_t at_timestamp_ns,
                                       struct xrt_hand_joint_set *out,
                                       int64_t *out_timestamp_ns,
                                       void *userdata);

// Camera-frame subscriber callback signature. Pointer is borrowed for the
// duration of the call only Ã¢â‚¬â€ subscribers MUST memcpy if they need to retain
// the data past the callback. Called on the publisher's thread.
typedef void (*aug_camera_frame_cb)(const uint8_t *y_data,
                                    uint32_t width,
                                    uint32_t height,
                                    uint32_t stride_bytes,
                                    int64_t timestamp_ns,
                                    const struct aug_camera_intrinsics *intr,
                                    void *userdata);

struct aug_host_api
{
	uint32_t version; // AUG_HOST_API_VERSION

	// ------------------------------------------------------------------ v1
	// Writes `relation` into the `xrLocateSpace` reply (a generated
	// `struct ipc_space_locate_space_reply *`). Sets reply->result = XRT_SUCCESS.
	void (*set_locate_space_relation)(void *reply, const struct xrt_space_relation *relation);

	// Returns the JavaVM* and android.content.Context jobject for the service
	// process. Cast the return values to JavaVM* and jobject respectively.
	// Both are valid for the lifetime of the runtime.
	void *(*get_vm)(void);
	void *(*get_context)(void);

	// ------------------------------------------------------------------ v2
	// Module installs/clears the producer callback for the runtime's stub
	// hand-tracker xdevs. Pass cb = NULL to unregister; while no callback is
	// registered the stub xdevs return is_active = false to clients.
	// Calling this also requires the module to advertise either
	// "Advertised_OpenXR_Features.SystemPropertyBits": ["handTracking"] in
	// its manifest (so the runtime stub xdevs are actually instantiated).
	void (*register_hand_tracker)(aug_hand_get_joints_fn cb, void *userdata);

	// Camera-frame broker. Producer (e.g. arcore-headpose) calls
	// publish_camera_frame_y8 every time a new Y-plane luminance frame is
	// available; the runtime fans out to all subscribers synchronously on
	// the publisher's thread. Subscribers (e.g. mercury-handtracking-arcore)
	// install their callback via subscribe_camera_frame.
	//
	// Pointers passed to the callback are borrowed and only valid for the
	// duration of the call.
	void (*publish_camera_frame_y8)(const uint8_t *y_data,
	                                uint32_t width,
	                                uint32_t height,
	                                uint32_t stride_bytes,
	                                int64_t timestamp_ns,
	                                const struct aug_camera_intrinsics *intr);
	void (*subscribe_camera_frame)(aug_camera_frame_cb cb, void *userdata);

	// Returns the per-module extraction directory of the CALLING module Ã¢â‚¬â€
	// the directory the .augins zip was unpacked into. Use this for
	// loading bundled assets (ONNX models, calibration JSON, etc.).
	// Lifetime: process lifetime; do not free.
	// Implementation note: the runtime uses thread-local state set during
	// lifecycle dispatch, so this only returns the right value when called
	// FROM a module lifecycle callback (aug_onModuleLoad, aug_onConnect,
	// aug_runtimeFinished, Ã¢â‚¬Â¦) or from a thread spawned by the module that
	// was given the dir explicitly.
	const char *(*get_module_data_dir)(void);
};

// -----------------------------------------------------------------------------
// Return code contract (see design doc Ã‚Â§5.2)
// -----------------------------------------------------------------------------
//
// Every OpenXR-shaped hook returns an int32_t. Meaning:
//
//   AUG_OK              ( 0)  Success. GRS continues to the next module.
//   AUG_FATAL_RUNTIME   (-1)  Fatal Ã¢â‚¬â€ GRS aborts the whole runtime process.
//   AUG_FATAL_MODULE    (-2)  Fatal to this module Ã¢â‚¬â€ GRS removes it from the
//                             dispatch table; other modules continue.
//   value > 0                 Developer-defined warning. Logged, then continue.
//   value < -2                Developer-defined error. Logged, then continue.
//
#define AUG_OK 0
#define AUG_FATAL_RUNTIME (-1)
#define AUG_FATAL_MODULE (-2)

// -----------------------------------------------------------------------------
// GRS-side lifecycle callbacks.
//
// All optional. Module authors export any subset by name. The GRS resolves
// each via dlsym at module load time and silently skips missing ones.
//
// `args` is reserved for future per-callback structures; pass NULL today.
// -----------------------------------------------------------------------------

typedef void (*aug_lifecycle_fn)(void *args);

// Expected exported names (use exactly these symbol names if you implement them):
//
//   void aug_onModuleLoad(void *args);
//   void aug_runtimeInit(void *args);
//   void aug_runtimeFinished(void *args);
//   void aug_backBufferUpdateLoop(void *args);
//   void aug_backBufferUpdateLoopPause(void *args);
//   void aug_backBufferUpdateLoopResume(void *args);
//   void aug_onConnect(void *args);
//   void aug_frame_begin(void *args);
//   void aug_frame_end(void *args);

#define AUG_LIFECYCLE_ON_MODULE_LOAD "aug_onModuleLoad"
#define AUG_LIFECYCLE_RUNTIME_INIT "aug_runtimeInit"
#define AUG_LIFECYCLE_RUNTIME_FINISHED "aug_runtimeFinished"
#define AUG_LIFECYCLE_BACK_BUFFER_LOOP "aug_backBufferUpdateLoop"
#define AUG_LIFECYCLE_BACK_BUFFER_PAUSE "aug_backBufferUpdateLoopPause"
#define AUG_LIFECYCLE_BACK_BUFFER_RESUME "aug_backBufferUpdateLoopResume"
#define AUG_LIFECYCLE_ON_CONNECT "aug_onConnect"
#define AUG_LIFECYCLE_FRAME_BEGIN "aug_frame_begin"
#define AUG_LIFECYCLE_FRAME_END "aug_frame_end"

#ifdef __cplusplus
}
#endif
