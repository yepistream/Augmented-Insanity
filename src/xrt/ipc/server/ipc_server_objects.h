// Copyright 2025-2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * @file
 * @brief Tracking objects to IDs.
 * @author Jakob Bornecrantz <tbornecrantz@nvidia.com>
 * @ingroup ipc_server
 */

#pragma once

#include "xrt/xrt_results.h"

#include <stdbool.h>
#include <stdint.h>

struct ipc_client_state;


/*!
 *
 * Device functions.
 *
 */

/*!
 * Get a device by ID, must only be called from the per client
 * thread as this function accesses the client state's memory.
 *
 * @param ics The client state instance.
 * @param id The device ID.
 * @param out_xdev Will be filled with the device object on success.
 * @return XRT_SUCCESS on success, some other result on failure.
 *
 * @ingroup ipc_server
 */
xrt_result_t
ipc_server_objects_get_xdev_and_validate(volatile struct ipc_client_state *ics,
                                         uint32_t id,
                                         struct xrt_device **out_xdev);

/*!
 * Get a device ID for a given device object, must only be called from the per
 * client thread as this function accesses the client state's memory.
 *
 * @param ics The client state instance.
 * @param xdev The device object.
 * @param out_id Will be filled with the device ID on success.
 * @return XRT_SUCCESS on success, some other result on failure.
 *
 * @ingroup ipc_server
 */
xrt_result_t
ipc_server_objects_get_xdev_id_or_add(volatile struct ipc_client_state *ics, struct xrt_device *xdev, uint32_t *out_id);

/*!
 * Aug-Ins helper. Test whether the xdev at the given ID is the same
 * pointer as the system's static head role. Used by the v0.2 service
 * adapter `aug_adapter_space_locate_device` to filter module dispatch
 * to head-pose queries only. Safe on bad inputs (returns false; never
 * fatal); intended for fast use on the hot IPC dispatch path.
 *
 * @ingroup ipc_server
 */
bool
ipc_server_xdev_is_head_role(volatile struct ipc_client_state *ics, uint32_t id);


/*!
 *
 * Tracking origin functions.
 *
 */

/*!
 * Get a tracking origin by ID, must only be called from the per client
 * thread as this function accesses the client state's memory.
 *
 * @param ics The client state instance.
 * @param id The tracking origin ID.
 * @param out_xtrack Will be filled with the tracking origin object on success.
 * @return XRT_SUCCESS on success, some other result on failure.
 *
 * @ingroup ipc_server
 */
xrt_result_t
ipc_server_objects_get_xtrack_and_validate(volatile struct ipc_client_state *ics,
                                           uint32_t id,
                                           struct xrt_tracking_origin **out_xtrack);

/*!
 * Get a tracking origin ID for a given tracking origin object, must only be
 * called from the per client thread as this function accesses the client
 * state's memory.
 *
 * @param ics The client state instance.
 * @param xtrack The tracking origin object.
 * @param out_id Will be filled with the tracking origin ID on success.
 * @return XRT_SUCCESS on success, some other result on failure.
 *
 * @ingroup ipc_server
 */
xrt_result_t
ipc_server_objects_get_xtrack_id_or_add(volatile struct ipc_client_state *ics,
                                        struct xrt_tracking_origin *xtrack,
                                        uint32_t *out_id);
