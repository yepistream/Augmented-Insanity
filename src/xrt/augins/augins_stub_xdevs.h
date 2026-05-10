// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * @file
 * @brief  Generic stub xdev factory for Aug-Ins module-advertised features.
 *
 * For each feature bit in the manifest aggregator (see augins_extensions.h),
 * augins_stub_xdevs_install() creates a placeholder xrt_device, sets the
 * appropriate `static_roles` slot in the system_devices, and wires the
 * device's vtable to forward queries through the host-API-registered
 * producer callback.
 *
 * For v2: only the hand-tracker stub is implemented (driven by
 * AUGINS_SYS_HAND_TRACKING). When future modules need eye / face / body
 * tracking, add another build_*_stub() helper here following the same
 * pattern.
 *
 * Call this exactly once during service startup, AFTER Aug-Ins modules have
 * been loaded (so the aggregator state is finalized) and BEFORE the IPC
 * server starts processing client connections.
 *
 * @ingroup augins
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct xrt_system_devices;
struct xrt_space_overseer;

// xsysd : the system devices container Ã¢â‚¬â€ receives the new xdev in static_xdevs
//         and the appropriate static_roles slots.
// xso   : the space overseer Ã¢â‚¬â€ must register the new xdev so locate_device IPC
//         calls (xrLocateHandJointsEXT, etc.) don't hit
//         find_xdev_space_read_locked's "ptr != NULL" assertion.
void
augins_stub_xdevs_install(struct xrt_system_devices *xsysd, struct xrt_space_overseer *xso);

#ifdef __cplusplus
}
#endif
