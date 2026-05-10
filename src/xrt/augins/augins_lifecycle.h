// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * @file
 * @brief  Aug-Ins runtime state machine and module lifecycle fan-out.
 *
 * Tracks the GRS process-level states from design doc Ã‚Â§8 and routes lifecycle
 * callbacks to all loaded modules in the right order.
 *
 * @ingroup augins
 */

#pragma once

#include <string>

#ifdef __cplusplus
extern "C" {
#endif

// Plain-C entry points used from service-lib (C-linkage IPC callbacks).
void
augins_on_client_connecting_c(void);

void
augins_on_client_disconnecting_c(void);

// State enum integer mirroring augins::state_t. Stable for use from JNI.
//   0 = Launching, 1 = Ready, 2 = Connecting,
//   3 = Running,   4 = Disconnecting, 5 = Critical
int
augins_status_state(void);

#ifdef __cplusplus
}

namespace augins {

enum class state_t
{
	Launching,
	Ready,
	Connecting,
	Running,
	Disconnecting,
	Critical,
};

// Top-level entry point. Loads all .augins from `modules_dir`, builds the
// dispatch table, fires aug_onModuleLoad / aug_runtimeInit, starts the
// background loop. Idempotent Ã¢â‚¬â€ calling twice is a no-op.
void
launch(const std::string &modules_dir, const std::string &cache_dir);

// Stop the background loop and dlclose every loaded module. Used at service
// shutdown.
void
shutdown(void);

// State transitions wired into the IPC server's client-connect/disconnect
// callbacks (see service_target.cpp).
void
on_client_connecting(void);

void
on_client_disconnecting(void);

// Critical-failure entry. Logs and sets state to Critical. v1 does not yet
// terminate the process Ã¢â‚¬â€ that's left to the caller via the XR_ERROR_RUNTIME_FAILURE
// return path so the IPC server can shut down cleanly.
void
critical(const std::string &reason);

state_t
current_state(void);

} // namespace augins

#endif
