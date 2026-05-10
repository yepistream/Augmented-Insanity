// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * @file
 * @brief  Generic stub xdev factory for Aug-Ins module-advertised features.
 * @ingroup augins
 */

#include "augins_stub_xdevs.h"

#include "augins_extensions.h"
#include "augins_hand_tracker_dispatch.h"

#include "b_space_overseer.h"

#include "util/u_device.h"

#include "xrt/xrt_device.h"
#include "xrt/xrt_results.h"
#include "xrt/xrt_space.h"
#include "xrt/xrt_system.h"

#include <android/log.h>

#include <cstdio>
#include <cstring>

#define LOG_TAG "Aug-Ins"
#define AUG_LOGI(fmt, ...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, fmt, ##__VA_ARGS__)
#define AUG_LOGW(fmt, ...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, fmt, ##__VA_ARGS__)

namespace {

// ---------------------------------------------------------------------------
// Stub hand tracker xdev Ã¢â‚¬â€ single device with two inputs (left + right). The
// same xdev pointer is plugged into both .hand_tracking.{unobstructed,
// conforming}.{left,right} slots; the input `name` argument disambiguates.
// ---------------------------------------------------------------------------

struct augins_hand_tracker_xdev
{
	struct xrt_device base;
};

xrt_result_t
ht_stub_get_tracked_pose(struct xrt_device *xdev,
                         enum xrt_input_name name,
                         int64_t at_timestamp_ns,
                         struct xrt_space_relation *out_relation)
{
	(void)xdev;
	(void)name;
	(void)at_timestamp_ns;
	if (out_relation != nullptr) {
		std::memset(out_relation, 0, sizeof(*out_relation));
	}
	return XRT_SUCCESS;
}

xrt_result_t
ht_stub_get_hand_tracking(struct xrt_device *xdev,
                          enum xrt_input_name name,
                          int64_t desired_timestamp_ns,
                          struct xrt_hand_joint_set *out_value,
                          int64_t *out_timestamp_ns)
{
	(void)xdev;
	if (out_value != nullptr) {
		std::memset(out_value, 0, sizeof(*out_value));
	}
	uint32_t handed = 0;
	switch (name) {
	case XRT_INPUT_HT_UNOBSTRUCTED_LEFT:
	case XRT_INPUT_HT_CONFORMING_LEFT:
		handed = 0;
		break;
	case XRT_INPUT_HT_UNOBSTRUCTED_RIGHT:
	case XRT_INPUT_HT_CONFORMING_RIGHT:
		handed = 1;
		break;
	default:
		// Unknown input Ã¢â‚¬â€ return an inactive set rather than dispatching.
		if (out_timestamp_ns != nullptr) {
			*out_timestamp_ns = desired_timestamp_ns;
		}
		return XRT_SUCCESS;
	}
	augins_hand_tracker_dispatch(handed, desired_timestamp_ns, out_value, out_timestamp_ns);
	return XRT_SUCCESS;
}

void
ht_stub_destroy(struct xrt_device *xdev)
{
	u_device_free(xdev);
}

struct xrt_device *
build_hand_tracker_stub(void)
{
	auto flags = static_cast<u_device_alloc_flags>(U_DEVICE_ALLOC_NO_FLAGS | U_DEVICE_ALLOC_TRACKING_NONE);
	// Four inputs: UNOBSTRUCTED {left,right} AND CONFORMING {left,right}.
	// `xrCreateHandTrackerEXT` with XR_HAND_TRACKING_DATA_SOURCE_*_EXT (the
	// hand_tracking_data_source extension) chooses which input it wants. Apps
	// targeting controller emulation (e.g. Godot's XR Hand Modifier) request
	// CONFORMING; gesture-only apps request UNOBSTRUCTED. We expose both so
	// every client gets a matching input Ã¢â‚¬â€ the joints we hand back are
	// identical (same Mercury source) regardless of which variant is asked.
	const int num_inputs = 4;

	auto *htd = U_DEVICE_ALLOCATE(struct augins_hand_tracker_xdev, flags, num_inputs, 0);
	if (htd == nullptr) {
		return nullptr;
	}

	htd->base.tracking_origin->type = XRT_TRACKING_TYPE_RGB;

	// Defaults for everything we don't override.
	u_device_populate_function_pointers(&htd->base, ht_stub_get_tracked_pose, ht_stub_destroy);
	htd->base.update_inputs = u_device_noop_update_inputs;
	htd->base.get_hand_tracking = ht_stub_get_hand_tracking;

	std::snprintf(htd->base.str, XRT_DEVICE_NAME_LEN, "Aug-Ins Hand Tracker");
	std::snprintf(htd->base.serial, XRT_DEVICE_NAME_LEN, "augins-hand-tracker");

	htd->base.inputs[0].name = XRT_INPUT_HT_UNOBSTRUCTED_LEFT;
	htd->base.inputs[1].name = XRT_INPUT_HT_UNOBSTRUCTED_RIGHT;
	htd->base.inputs[2].name = XRT_INPUT_HT_CONFORMING_LEFT;
	htd->base.inputs[3].name = XRT_INPUT_HT_CONFORMING_RIGHT;

	htd->base.name = XRT_DEVICE_HAND_TRACKER;
	htd->base.device_type = XRT_DEVICE_TYPE_HAND_TRACKER;
	htd->base.supported.orientation_tracking = true;
	htd->base.supported.position_tracking = true;
	htd->base.supported.hand_tracking = true;

	return &htd->base;
}

void
install_hand_tracker_stub(struct xrt_system_devices *xsysd, struct xrt_space_overseer *xso)
{
	if (xsysd == nullptr) {
		return;
	}

	struct xrt_device *xdev = build_hand_tracker_stub();
	if (xdev == nullptr) {
		AUG_LOGW("augins_stub_xdevs: hand-tracker allocation failed");
		return;
	}

	if (xsysd->static_xdev_count >= XRT_SYSTEM_MAX_DEVICES) {
		AUG_LOGW("augins_stub_xdevs: static_xdev_count at max Ã¢â‚¬â€ dropping hand tracker");
		u_device_free(xdev);
		return;
	}

	xsysd->static_xdevs[xsysd->static_xdev_count++] = xdev;

	// One xdev serves all four roles; the input `name` argument distinguishes
	// left vs right inside the get_hand_tracking callback.
	xsysd->static_roles.hand_tracking.unobstructed.left = xdev;
	xsysd->static_roles.hand_tracking.unobstructed.right = xdev;
	xsysd->static_roles.hand_tracking.conforming.left = xdev;
	xsysd->static_roles.hand_tracking.conforming.right = xdev;

	// Register the new xdev with the space overseer so locate_device IPC
	// calls (xrLocateHandJointsEXT internally calls oxr_space_locate_device)
	// don't trip the "ptr != NULL" assertion in find_xdev_space_read_locked.
	// b_space_overseer_legacy_setup() does this for the ORIGINAL xdev list at
	// system creation time; we missed the train, so we plug ours in here.
	if (xso != nullptr) {
		auto *uso = reinterpret_cast<struct b_space_overseer *>(xso);
		struct xrt_space *xs = nullptr;
		b_space_overseer_create_null_space(uso, xso->semantic.root, &xs);
		b_space_overseer_link_space_to_device(uso, xs, xdev);
		// link_space_to_device took its own reference; release ours so the
		// space's lifetime is owned by the overseer's xdev_map entry.
		xrt_space_reference(&xs, nullptr);
		AUG_LOGI("augins_stub_xdevs: hand tracker installed and registered with space overseer");
	} else {
		AUG_LOGW("augins_stub_xdevs: no space overseer Ã¢â‚¬â€ locate_device on the stub xdev will fault");
	}
}

} // namespace

extern "C" void
augins_stub_xdevs_install(struct xrt_system_devices *xsysd, struct xrt_space_overseer *xso)
{
	uint64_t bits = augins_get_system_bits();
	if (bits == 0) {
		AUG_LOGI("augins_stub_xdevs: no module advertises a system bit; skipping");
		return;
	}

	if (bits & AUGINS_SYS_HAND_TRACKING) {
		install_hand_tracker_stub(xsysd, xso);
	}
	// Future: install_eye_tracker_stub, install_body_tracker_stub, Ã¢â‚¬Â¦ driven
	// by their respective AUGINS_SYS_* bits.
}
