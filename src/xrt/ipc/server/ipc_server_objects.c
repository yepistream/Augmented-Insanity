// Copyright 2025-2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * @file
 * @brief Tracking objects to IDs.
 * @author Jakob Bornecrantz <tbornecrantz@nvidia.com>
 * @ingroup ipc_server
 */

#include "xrt/xrt_device.h"
#include "xrt/xrt_system.h"
#include "xrt/xrt_tracking.h"

#include "shared/ipc_protocol.h"
#include "server/ipc_server.h"
#include "server/ipc_server_objects.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>


/*
 *
 * Device functions.
 *
 */

xrt_result_t
ipc_server_objects_get_xdev_and_validate(volatile struct ipc_client_state *ics,
                                         uint32_t id,
                                         struct xrt_device **out_xdev)
{
	if (id >= XRT_SYSTEM_MAX_DEVICES) {
		IPC_ERROR(ics->server, "Invalid device ID %u (>= XRT_SYSTEM_MAX_DEVICES)", id);
		return XRT_ERROR_IPC_FAILURE;
	}

	struct xrt_device *xdev = ics->objects.xdevs[id];
	if (xdev == NULL) {
		IPC_ERROR(ics->server, "Device ID %u not found (NULL)", id);
		return XRT_ERROR_IPC_FAILURE;
	}

	*out_xdev = xdev;

	return XRT_SUCCESS;
}

// Aug-Ins helper: identify whether the xdev at `id` fills the system's
// head role. Used by aug_adapter_space_locate_device to filter module
// dispatch to head-pose queries only (v0.2 base; non-head device roles
// are a v0.2.x parking-lot item).
//
// Returns true only on a clean head-role match. False on: invalid id,
// NULL xdev slot, NULL static_roles.head, mismatch. NEVER fatal -- the
// caller falls back to runtime default on false.
bool
ipc_server_xdev_is_head_role(volatile struct ipc_client_state *ics, uint32_t id)
{
	if (ics == NULL || ics->server == NULL || ics->server->xsysd == NULL) {
		return false;
	}
	if (id >= XRT_SYSTEM_MAX_DEVICES) {
		return false;
	}
	struct xrt_device *xdev = ics->objects.xdevs[id];
	if (xdev == NULL) {
		return false;
	}
	return xdev == ics->server->xsysd->static_roles.head;
}

xrt_result_t
ipc_server_objects_get_xdev_id_or_add(volatile struct ipc_client_state *ics, struct xrt_device *xdev, uint32_t *out_id)
{
	assert(out_id != NULL);
	assert(xdev != NULL);

	// Check if device is already tracked and return its ID.
	for (uint32_t index = 0; index < XRT_SYSTEM_MAX_DEVICES; index++) {
		if (ics->objects.xdevs[index] == xdev) {
			*out_id = index;
			return XRT_SUCCESS;
		}
	}

	// If not, find a free slot for it, filled below.
	uint32_t index = 0;
	for (; index < XRT_SYSTEM_MAX_DEVICES; index++) {
		// Found a free slot.
		if (ics->objects.xdevs[index] == NULL) {
			break;
		}
	}

	if (index >= XRT_SYSTEM_MAX_DEVICES) {
		IPC_ERROR(ics->server, "Failed to find available slot for device: '%s'", xdev->str);
		return XRT_ERROR_IPC_FAILURE;
	}

	// Check that we can also get the tracking origin allocated.
	uint32_t tracking_origin_id = UINT32_MAX;
	xrt_result_t xret = ipc_server_objects_get_xtrack_id_or_add(ics, xdev->tracking_origin, &tracking_origin_id);
	IPC_CHK_AND_RET(ics->server, xret, "ipc_server_objects_get_xtrack_id_or_add");

	ics->objects.xdevs[index] = xdev;

	*out_id = index;

	return XRT_SUCCESS;
}


/*
 *
 * Tracking origin functions.
 *
 */

xrt_result_t
ipc_server_objects_get_xtrack_and_validate(volatile struct ipc_client_state *ics,
                                           uint32_t id,
                                           struct xrt_tracking_origin **out_xtrack)
{
	if (id >= XRT_SYSTEM_MAX_DEVICES) {
		IPC_ERROR(ics->server, "Invalid tracking origin ID %u (>= XRT_SYSTEM_MAX_DEVICES)", id);
		return XRT_ERROR_IPC_FAILURE;
	}

	struct xrt_tracking_origin *xtrack = ics->objects.xtracks[id];
	if (xtrack == NULL) {
		IPC_ERROR(ics->server, "Tracking origin ID %u not found (NULL)", id);
		return XRT_ERROR_IPC_FAILURE;
	}

	*out_xtrack = xtrack;

	return XRT_SUCCESS;
}

xrt_result_t
ipc_server_objects_get_xtrack_id_or_add(volatile struct ipc_client_state *ics,
                                        struct xrt_tracking_origin *xtrack,
                                        uint32_t *out_id)
{
	assert(out_id != NULL);

	// Check if tracking origin is already tracked and return its ID.
	for (uint32_t index = 0; index < XRT_SYSTEM_MAX_DEVICES; index++) {
		if (ics->objects.xtracks[index] == xtrack) {
			*out_id = index;
			return XRT_SUCCESS;
		}
	}

	// If not, find a free slot for it, filled below.
	for (uint32_t index = 0; index < XRT_SYSTEM_MAX_DEVICES; index++) {
		if (ics->objects.xtracks[index] == NULL) {
			ics->objects.xtracks[index] = xtrack;
			*out_id = index;
			return XRT_SUCCESS;
		}
	}

	IPC_ERROR(ics->server, "Failed to find available slot for tracking origin: '%s'", xtrack->name);

	return XRT_ERROR_IPC_FAILURE;
}
