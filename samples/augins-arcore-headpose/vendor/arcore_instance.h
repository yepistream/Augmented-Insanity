// arcore_min.h
#pragma once

#include <jni.h>
#include <stdbool.h>
#include <stdint.h>

#include "xrt/xrt_handles.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Tiny ARCore + tiny EGL pbuffer context for pose tracking.
 * Call all functions from the same thread that calls arcore_min_start_ex().
 */

enum arcore_min_focus_mode
{
	AUTO_FOCUS_DISABLED = 0,
	AUTO_FOCUS_ENABLED = 1,
};

enum arcore_min_camera_hz_mode
{
	MIN_ARCAMERA_HZ = 0,
	MAX_ARCAMERA_HZ = 1,
};

enum arcore_min_texture_update_mode
{
	ARCORE_MIN_TEXTURE_UPDATE_MODE_EXTERNAL_OES = 0,
	ARCORE_MIN_TEXTURE_UPDATE_MODE_HARDWARE_BUFFER = 1,
};

struct arcore_min_config
{
	/*! Camera autofocus mode. */
	enum arcore_min_focus_mode focus_mode;
	/*! Preferred camera FPS policy. */
	enum arcore_min_camera_hz_mode camera_hz_mode;
	/*! Camera texture update mode used by ARCore. */
	enum arcore_min_texture_update_mode texture_update_mode;

	/*! Optional ARCore features toggled at session configure time. */
	bool enable_plane_detection;
	bool enable_light_estimation;
	bool enable_depth;
	bool enable_instant_placement;
	bool enable_augmented_faces;
	bool enable_image_stabilization;
};

struct arcore_min_image
{
	void *handle;
	const uint8_t *plane_data[3];
	int32_t plane_data_length[3];
	int32_t plane_pixel_stride[3];
	int32_t plane_row_stride[3];
	int32_t width;
	int32_t height;
	int32_t num_planes;
	int32_t format;
	int64_t timestamp_ns;
};

struct arcore_min_intrinsics
{
	float fx;
	float fy;
	float cx;
	float cy;
	int32_t width;
	int32_t height;
};

void
arcore_min_config_set_defaults(struct arcore_min_config *out_cfg);

struct arcore_min
{
	/*! JVM/thread ownership for this runtime thread. */
	JavaVM *vm;
	jobject app_ctx_global; // GlobalRef(Context)
	bool did_attach;

	/*! EGL/GLES state (stored as integers to keep EGL types out of this header). */
	uintptr_t egl_display;
	uintptr_t egl_context;
	uintptr_t egl_surface;

	/*! GL external camera texture used when in OES mode. */
	uint32_t camera_tex_id; // GL_TEXTURE_EXTERNAL_OES
	enum arcore_min_texture_update_mode texture_update_mode;

	/*! ARCore opaque handles. */
	void *session;
	void *config;
	void *frame;
	void *pose;

	/*! True while session is active and objects are valid. */
	bool running;
};

bool
arcore_min_start_ex(struct arcore_min *a, JavaVM *vm, jobject app_context, const struct arcore_min_config *cfg);

bool
arcore_min_start(struct arcore_min *a, JavaVM *vm, jobject app_context);

bool
arcore_min_tick(struct arcore_min *a,
                float out_pos_m[3],
                float out_rot_m[4],
                bool *out_tracking,
                int64_t *out_frame_timestamp_ns);

bool
arcore_min_get_latest_camera_frame(struct arcore_min *a,
                                   xrt_graphics_buffer_handle_t *out_handle,
                                   uint32_t *out_width,
                                   uint32_t *out_height,
                                   int64_t *out_frame_timestamp_ns);

bool
arcore_min_acquire_camera_image(struct arcore_min *a, struct arcore_min_image *out_image);

bool
arcore_min_acquire_depth_image_16bits(struct arcore_min *a, struct arcore_min_image *out_image);

bool
arcore_min_is_depth_mode_supported(struct arcore_min *a, bool *out_supported);

void
arcore_min_release_image(struct arcore_min *a, struct arcore_min_image *image);

bool
arcore_min_get_intrinsics(struct arcore_min *a, bool use_image_intrinsics, struct arcore_min_intrinsics *out_intrinsics);

void
arcore_min_stop(struct arcore_min *a);

static inline uint32_t
arcore_min_camera_texture_id(const struct arcore_min *a)
{
	return a ? a->camera_tex_id : 0;
}

#ifdef __cplusplus
}
#endif
