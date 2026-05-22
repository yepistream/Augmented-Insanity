// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Aug-Ins v0.2 dispatch adapters (hand-written, first cut).
//
// Each adapter pairs ONE IPC handler signature with ONE OpenXR
// function signature. The pattern (followed for every adapter):
//
//   1. Call the underlying ipc_handle_<call> to fill the IPC reply
//      with the runtime's baseline output (Q2 runs default first).
//   2. Translate IPC msg/reply structs into OpenXR-shaped args.
//   3. Iterate aug_get_modules_for(name) in priority order, calling
//      each module's function pointer cast to the OpenXR signature.
//      Push/pop the per-module data_dir TLS around each call so the
//      module's host->get_module_data_dir() returns the right path.
//   4. On first non-success XrResult, abort and return (Q5).
//   5. Translate OpenXR output back into the IPC reply.
//
// Module function pointers are dlsym'd in loader.cpp and stored in
// dispatch.cpp's g_map. Adapters call them as raw void* casts to the
// PFN_xr* typedefs from <openxr/openxr.h>.
//
// XrSpace handles: the IPC layer uses uint32_t space_id values that
// the runtime maps internally to xrt_space objects. There is no
// pre-existing XrSpace handle on the service side -- we synthesise
// one by casting the space_id to XrSpace. Modules can compare these
// handles for equality (e.g. "is this the same space the
// xrCreateReferenceSpace adapter reported?") but cannot dereference
// them as OpenXR-loader handles. If a module needs to query a space's
// type or relation chain, a host-API entry will be added in v0.2.x.

#include "adapters.h"
#include "dispatch.h"
#include "host_api.h"

#include "shared/ipc_protocol.h"

#include <openxr/openxr.h>

#include <android/log.h>

#include <vector>

// Forward declarations for the IPC handlers we delegate to. They are
// auto-generated into ipc_server_generated.h (which lives in the build
// directory, not the source tree, so we can't include it from aux_augins
// without exposing the build dir publicly). Hand-declared here mirrors
// the runtime's actual signature in ipc_server_handler.c; if the
// signature ever changes the compile will fail loudly and we update.
//
// Also forward-declares the Aug-Ins helper `ipc_server_xdev_is_head_role`
// (declared in ipc_server_objects.h but pulling that public header in
// here would drag the full ipc_server.h chain transitively).
extern "C" {
xrt_result_t
ipc_handle_space_locate_space(volatile struct ipc_client_state *ics,
                              uint32_t base_space_id,
                              const struct xrt_pose *base_offset,
                              int64_t at_timestamp,
                              uint32_t space_id,
                              const struct xrt_pose *offset,
                              struct xrt_space_relation *out_relation);

xrt_result_t
ipc_handle_space_locate_device(volatile struct ipc_client_state *ics,
                               uint32_t base_space_id,
                               const struct xrt_pose *base_offset,
                               int64_t at_timestamp,
                               uint32_t xdev_id,
                               struct xrt_space_relation *out_relation);

bool
ipc_server_xdev_is_head_role(volatile struct ipc_client_state *ics, uint32_t id);
}

#define TAG "Aug-Ins.Adapters"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)

// ---------------------------------------------------------------------------
// Flag-set conversions between xrt_space_relation_flags and the
// XrSpaceLocationFlags surface a module sees. Bits are NOT the same
// numeric values across the two enums; we map explicitly.
// ---------------------------------------------------------------------------

namespace {

XrSpaceLocationFlags
xrt_relation_to_xr_loc_flags(enum xrt_space_relation_flags rf)
{
	XrSpaceLocationFlags out = 0;
	if (rf & XRT_SPACE_RELATION_ORIENTATION_VALID_BIT)   out |= XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
	if (rf & XRT_SPACE_RELATION_POSITION_VALID_BIT)      out |= XR_SPACE_LOCATION_POSITION_VALID_BIT;
	if (rf & XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT) out |= XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
	if (rf & XRT_SPACE_RELATION_POSITION_TRACKED_BIT)    out |= XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
	return out;
}

enum xrt_space_relation_flags
xr_loc_flags_to_xrt_relation(XrSpaceLocationFlags lf)
{
	int out = 0;
	if (lf & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)   out |= XRT_SPACE_RELATION_ORIENTATION_VALID_BIT;
	if (lf & XR_SPACE_LOCATION_POSITION_VALID_BIT)      out |= XRT_SPACE_RELATION_POSITION_VALID_BIT;
	if (lf & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) out |= XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT;
	if (lf & XR_SPACE_LOCATION_POSITION_TRACKED_BIT)    out |= XRT_SPACE_RELATION_POSITION_TRACKED_BIT;
	return static_cast<enum xrt_space_relation_flags>(out);
}

// Convert XR_SUCCESS / XR_ERROR_* into xrt_result_t for IPC return.
// XR errors don't all have direct xrt_result_t equivalents; for
// non-success we collapse to XRT_ERROR_IPC_FAILURE (the most generic
// non-success in xrt_results.h). The client-side OpenXR loader maps
// XRT_ERROR_IPC_FAILURE to XR_ERROR_RUNTIME_FAILURE, which is the
// right "something went wrong, can't be more specific" code.
xrt_result_t
xr_result_to_xrt(XrResult xr)
{
	if (xr == XR_SUCCESS) return XRT_SUCCESS;
	LOGW("module returned XrResult=%d; aborting dispatch chain", (int)xr);
	return XRT_ERROR_IPC_FAILURE;
}

} // namespace

// ---------------------------------------------------------------------------
// Adapter: aug_adapter_space_locate_space (OpenXR: xrLocateSpace)
// ---------------------------------------------------------------------------

xrt_result_t
aug_adapter_space_locate_space(volatile struct ipc_client_state *ics,
                               uint32_t base_space_id,
                               const struct xrt_pose *base_offset,
                               int64_t at_timestamp,
                               uint32_t space_id,
                               const struct xrt_pose *offset,
                               struct xrt_space_relation *out_relation)
{
	// Step 1: call runtime default to fill baseline reply (Q2).
	xrt_result_t r = ipc_handle_space_locate_space(ics,
	                                                base_space_id,
	                                                base_offset,
	                                                at_timestamp,
	                                                space_id,
	                                                offset,
	                                                out_relation);
	if (r != XRT_SUCCESS) {
		// Default failed; do not invoke modules with garbage input.
		return r;
	}

	// Step 2: unpack IPC reply into OpenXR-shaped output.
	XrSpaceLocation loc = {};
	loc.type          = XR_TYPE_SPACE_LOCATION;
	loc.next          = nullptr;
	loc.locationFlags = xrt_relation_to_xr_loc_flags(out_relation->relation_flags);
	loc.pose.position.x    = out_relation->pose.position.x;
	loc.pose.position.y    = out_relation->pose.position.y;
	loc.pose.position.z    = out_relation->pose.position.z;
	loc.pose.orientation.x = out_relation->pose.orientation.x;
	loc.pose.orientation.y = out_relation->pose.orientation.y;
	loc.pose.orientation.z = out_relation->pose.orientation.z;
	loc.pose.orientation.w = out_relation->pose.orientation.w;

	const XrSpace xr_space      = (XrSpace)(uintptr_t)space_id;
	const XrSpace xr_base_space = (XrSpace)(uintptr_t)base_space_id;
	const XrTime  xr_time       = (XrTime)at_timestamp;

	// Step 3: iterate registered modules. Stack-allocate small;
	// almost no realistic deployment has >16 modules overriding a
	// single OpenXR call.
	constexpr size_t kMaxChain = 16;
	struct aug_module_entry chain[kMaxChain];
	const size_t n = aug_get_modules_for("xrLocateSpace", chain, kMaxChain);

	for (size_t i = 0; i < n; ++i) {
		augins_host_api_push_data_dir(chain[i].data_dir);
		auto fn = reinterpret_cast<PFN_xrLocateSpace>(chain[i].fn);
		XrResult xr = fn(xr_space, xr_base_space, xr_time, &loc);
		augins_host_api_pop_data_dir();
		if (xr != XR_SUCCESS) {
			return xr_result_to_xrt(xr);
		}
	}

	// Step 4: pack OpenXR output back into IPC reply.
	out_relation->relation_flags = xr_loc_flags_to_xrt_relation(loc.locationFlags);
	out_relation->pose.position.x    = loc.pose.position.x;
	out_relation->pose.position.y    = loc.pose.position.y;
	out_relation->pose.position.z    = loc.pose.position.z;
	out_relation->pose.orientation.x = loc.pose.orientation.x;
	out_relation->pose.orientation.y = loc.pose.orientation.y;
	out_relation->pose.orientation.z = loc.pose.orientation.z;
	out_relation->pose.orientation.w = loc.pose.orientation.w;
	// Velocities aren't part of XrSpaceLocation (they're in a chained
	// XrSpaceVelocity struct we don't expose to modules yet); zero
	// them so a module that wants to override pose doesn't accidentally
	// leak stale velocities from the baseline. v0.2.x can add velocity
	// support via a host-API entry.
	out_relation->linear_velocity  = {0, 0, 0};
	out_relation->angular_velocity = {0, 0, 0};

	return XRT_SUCCESS;
}

// ---------------------------------------------------------------------------
// Adapter: aug_adapter_space_locate_device
//   (OpenXR-style module-facing name: aug_LocateDeviceInSpace)
//
// Backs xrLocateViews's T_base_xdev half. Filtered to head-device only
// in v0.2: if `xdev_id` is not the head, the runtime default handler
// runs and modules are never invoked. Modules can therefore export
// `aug_LocateDeviceInSpace` knowing that any invocation is for the
// head device.
//
// Non-head device roles (controllers, generic-trackers) are a
// v0.2.x parking-lot item -- see PROJECT_PLAN.md.
// ---------------------------------------------------------------------------

xrt_result_t
aug_adapter_space_locate_device(volatile struct ipc_client_state *ics,
                                uint32_t base_space_id,
                                const struct xrt_pose *base_offset,
                                int64_t at_timestamp,
                                uint32_t xdev_id,
                                struct xrt_space_relation *out_relation)
{
	// Step 1: call runtime default to fill baseline reply (Q2).
	xrt_result_t r = ipc_handle_space_locate_device(ics,
	                                                 base_space_id,
	                                                 base_offset,
	                                                 at_timestamp,
	                                                 xdev_id,
	                                                 out_relation);
	if (r != XRT_SUCCESS) {
		return r;
	}

	// Step 2: head-only filter. Non-head devices skip module dispatch
	// entirely and use the runtime default we already computed.
	if (!ipc_server_xdev_is_head_role(ics, xdev_id)) {
		return XRT_SUCCESS;
	}

	// Step 3: unpack IPC reply into OpenXR-shaped output.
	XrSpaceLocation loc = {};
	loc.type          = XR_TYPE_SPACE_LOCATION;
	loc.next          = nullptr;
	loc.locationFlags = xrt_relation_to_xr_loc_flags(out_relation->relation_flags);
	loc.pose.position.x    = out_relation->pose.position.x;
	loc.pose.position.y    = out_relation->pose.position.y;
	loc.pose.position.z    = out_relation->pose.position.z;
	loc.pose.orientation.x = out_relation->pose.orientation.x;
	loc.pose.orientation.y = out_relation->pose.orientation.y;
	loc.pose.orientation.z = out_relation->pose.orientation.z;
	loc.pose.orientation.w = out_relation->pose.orientation.w;

	const XrSpace xr_base_space = (XrSpace)(uintptr_t)base_space_id;
	const XrTime  xr_time       = (XrTime)at_timestamp;

	// Module-facing signature (synthetic, see proto.py):
	//   XrResult aug_LocateDeviceInSpace(XrSpace baseSpace,
	//                                    XrTime  time,
	//                                    XrSpaceLocation *location)
	using PFN_aug_LocateDeviceInSpace =
	    XrResult (XRAPI_PTR *)(XrSpace, XrTime, XrSpaceLocation *);

	// Step 4: iterate registered modules.
	constexpr size_t kMaxChain = 16;
	struct aug_module_entry chain[kMaxChain];
	const size_t n = aug_get_modules_for("aug_LocateDeviceInSpace", chain, kMaxChain);

	for (size_t i = 0; i < n; ++i) {
		augins_host_api_push_data_dir(chain[i].data_dir);
		auto fn = reinterpret_cast<PFN_aug_LocateDeviceInSpace>(chain[i].fn);
		XrResult xr = fn(xr_base_space, xr_time, &loc);
		augins_host_api_pop_data_dir();
		if (xr != XR_SUCCESS) {
			return xr_result_to_xrt(xr);
		}
	}

	// Step 5: pack OpenXR output back into IPC reply.
	out_relation->relation_flags = xr_loc_flags_to_xrt_relation(loc.locationFlags);
	out_relation->pose.position.x    = loc.pose.position.x;
	out_relation->pose.position.y    = loc.pose.position.y;
	out_relation->pose.position.z    = loc.pose.position.z;
	out_relation->pose.orientation.x = loc.pose.orientation.x;
	out_relation->pose.orientation.y = loc.pose.orientation.y;
	out_relation->pose.orientation.z = loc.pose.orientation.z;
	out_relation->pose.orientation.w = loc.pose.orientation.w;
	out_relation->linear_velocity  = {0, 0, 0};
	out_relation->angular_velocity = {0, 0, 0};

	return XRT_SUCCESS;
}
