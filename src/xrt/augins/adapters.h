// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * @file
 * @brief  Aug-Ins v0.2 service-side dispatch adapters.
 *
 * One adapter per OpenXR function name that modules can override.
 * Each adapter has the same signature as the corresponding
 * `ipc_handle_<call>` so the generated IPC dispatcher can route to
 * the adapter as a drop-in replacement. Internally, the adapter:
 *
 *   1. Calls `ipc_handle_<call>` to fill the baseline output
 *      (Q2: runtime default runs before modules).
 *   2. Unpacks the IPC msg/reply into OpenXR-shaped args.
 *   3. Iterates modules registered for the OpenXR name in priority
 *      order, calling each with the unpacked args (Q1: last-write-
 *      wins on the shared output struct; Q3: no short-circuit).
 *   4. Packs the OpenXR output back into the IPC reply.
 *   5. Aborts on first non-success XrResult (Q5: abort-on-error in
 *      the v0.2 baseline).
 *
 * Hand-written for v0.2. Codegen from the IPC schema + OpenXR
 * signatures is a v0.2.x / v0.3 future task; the proto.py dispatch
 * fork emission stays the same when that lands.
 *
 * Internal header. Not part of the module ABI.
 *
 * @ingroup aug_runtime
 */

#pragma once

#include "xrt/xrt_results.h"
#include "xrt/xrt_defines.h"

struct ipc_client_state;

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Adapter for `space_locate_space` (OpenXR: `xrLocateSpace`).
 *
 * Same signature as `ipc_handle_space_locate_space` in
 * `src/xrt/ipc/server/ipc_server_handler.c`. The generated IPC
 * dispatcher routes to this when `aug_has_modules_for("xrLocateSpace")`
 * returns true.
 */
xrt_result_t
aug_adapter_space_locate_space(volatile struct ipc_client_state *ics,
                               uint32_t base_space_id,
                               const struct xrt_pose *base_offset,
                               int64_t at_timestamp,
                               uint32_t space_id,
                               const struct xrt_pose *offset,
                               struct xrt_space_relation *out_relation);


/*!
 * Adapter for `space_locate_device` (module-facing synthetic OpenXR
 * name: `aug_LocateDeviceInSpace`).
 *
 * Same signature as `ipc_handle_space_locate_device`. The generated
 * IPC dispatcher routes to this when
 * `aug_has_modules_for("aug_LocateDeviceInSpace")` returns true.
 *
 * V0.2 BEHAVIOUR: the adapter filters head-device-only. If `xdev_id`
 * does not resolve to the system's head xdev, the adapter delegates
 * straight to `ipc_handle_space_locate_device` and never invokes
 * modules. Non-head device roles (controllers, generic-trackers) are
 * a v0.2.x parking-lot item -- see PROJECT_PLAN.md.
 *
 * The module-facing signature is:
 *   XrResult aug_LocateDeviceInSpace(XrSpace baseSpace,
 *                                    XrTime  time,
 *                                    XrSpaceLocation *location);
 * No xdev parameter is exposed -- "if the module's function got
 * called, the locate is for the head device."
 */
xrt_result_t
aug_adapter_space_locate_device(volatile struct ipc_client_state *ics,
                                uint32_t base_space_id,
                                const struct xrt_pose *base_offset,
                                int64_t at_timestamp,
                                uint32_t xdev_id,
                                struct xrt_space_relation *out_relation);


#ifdef __cplusplus
} // extern "C"
#endif
