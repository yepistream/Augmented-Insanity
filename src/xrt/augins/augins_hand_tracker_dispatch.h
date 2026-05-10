// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * @file
 * @brief  Internal bridge from the stub hand-tracker xdev to the host-API-
 *         registered hand-joints producer callback.
 *
 * The runtime registers a stub hand-tracker xdev (see augins_stub_xdevs.*).
 * Its xrt_device::get_hand_tracking implementation calls
 * augins_hand_tracker_dispatch() to forward the query to whichever module
 * is currently registered via aug_host_api::register_hand_tracker.
 *
 * Also exposes the per-module data-dir thread-local used by
 * aug_host_api::get_module_data_dir; the lifecycle dispatcher in
 * augins_lifecycle.cpp sets/clears these around each module callback.
 *
 * @ingroup augins
 */

#pragma once

#include "xrt/xrt_defines.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void
augins_hand_tracker_dispatch(uint32_t handed,
                             int64_t at_timestamp_ns,
                             struct xrt_hand_joint_set *out,
                             int64_t *out_timestamp_ns);

void
augins_set_calling_module_dir(const char *dir);

void
augins_clear_calling_module_dir(void);

#ifdef __cplusplus
}
#endif
