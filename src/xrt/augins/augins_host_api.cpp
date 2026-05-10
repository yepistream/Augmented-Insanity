// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * @file
 * @brief  Aug-Ins host-API table implementation.
 *
 * The single translation unit allowed to include the generated IPC reply
 * structs (`ipc_protocol_generated.h`). Modules call into here through the
 * `struct aug_host_api` function pointer table they receive from
 * aug_onModuleLoad Ã¢â‚¬â€ they never see IPC layouts directly.
 *
 * Also owns:
 *   - the camera-frame broker (single producer, fan-out to N subscribers)
 *   - the hand-tracker callback registry (consumed by the stub xdev factory)
 *   - the per-module thread-local data-dir context used by get_module_data_dir
 *
 * @ingroup augins
 */

#include "augins_host_api.h"
#include "augins_hand_tracker_dispatch.h"

// ipc_protocol_generated.h requires these prereqs to be included first.
#include "shared/ipc_protocol.h"
#include "ipc_protocol_generated.h"

#include "xrt/xrt_results.h"
#include "android/android_globals.h"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

// --------------------------------------------------------------- thread-local
// "which module is currently making host-API calls" Ã¢â‚¬â€ set by the lifecycle
// dispatcher just before invoking a module callback, cleared just after.
// Used by host_get_module_data_dir to return the CALLER's extraction dir.
thread_local std::string g_tls_calling_module_dir;

// --------------------------------------------------------------- frame broker
struct camera_subscriber
{
	aug_camera_frame_cb cb;
	void *userdata;
};

constexpr size_t kMaxSubscribers = 8;

std::mutex g_broker_mu;
std::vector<camera_subscriber> g_subscribers;

// --------------------------------------------------------------- hand tracker
std::mutex g_ht_mu;
aug_hand_get_joints_fn g_ht_cb = nullptr;
void *g_ht_cb_userdata = nullptr;

} // namespace

extern "C" {

// ============================================================================ v1 API
static void
host_set_locate_space_relation(void *reply_v, const struct xrt_space_relation *relation)
{
	if (reply_v == nullptr || relation == nullptr) {
		return;
	}
	auto *reply = static_cast<struct ipc_space_locate_space_reply *>(reply_v);
	reply->relation = *relation;
	reply->result = XRT_SUCCESS;
}

static void *
host_get_vm(void)
{
	return android_globals_get_vm();
}

static void *
host_get_context(void)
{
	return android_globals_get_context();
}

// ============================================================================ v2 API

static void
host_register_hand_tracker(aug_hand_get_joints_fn cb, void *userdata)
{
	std::lock_guard<std::mutex> lk(g_ht_mu);
	g_ht_cb = cb;
	g_ht_cb_userdata = userdata;
}

static void
host_publish_camera_frame_y8(const uint8_t *y_data,
                             uint32_t width,
                             uint32_t height,
                             uint32_t stride_bytes,
                             int64_t timestamp_ns,
                             const struct aug_camera_intrinsics *intr)
{
	if (y_data == nullptr || width == 0 || height == 0) {
		return;
	}
	// Snapshot subscribers under the lock so the lock isn't held while
	// invoking arbitrary module code (which could deadlock if a callback
	// re-enters subscribe/unsubscribe).
	std::vector<camera_subscriber> subs;
	{
		std::lock_guard<std::mutex> lk(g_broker_mu);
		subs = g_subscribers;
	}
	for (const auto &s : subs) {
		if (s.cb != nullptr) {
			s.cb(y_data, width, height, stride_bytes, timestamp_ns, intr, s.userdata);
		}
	}
}

static void
host_subscribe_camera_frame(aug_camera_frame_cb cb, void *userdata)
{
	if (cb == nullptr) {
		// Treat NULL cb as "remove every subscription with this userdata".
		std::lock_guard<std::mutex> lk(g_broker_mu);
		g_subscribers.erase(std::remove_if(g_subscribers.begin(), g_subscribers.end(),
		                                   [&](const camera_subscriber &s) {
			                                   return s.userdata == userdata;
		                                   }),
		                    g_subscribers.end());
		return;
	}
	std::lock_guard<std::mutex> lk(g_broker_mu);
	if (g_subscribers.size() >= kMaxSubscribers) {
		return; // silently drop; caller can re-attempt later if it wants
	}
	g_subscribers.push_back({cb, userdata});
}

static const char *
host_get_module_data_dir(void)
{
	if (g_tls_calling_module_dir.empty()) {
		return nullptr;
	}
	return g_tls_calling_module_dir.c_str();
}

// ============================================================================ table

static const struct aug_host_api g_host_api = {
    /* .version                    = */ AUG_HOST_API_VERSION,
    /* .set_locate_space_relation  = */ host_set_locate_space_relation,
    /* .get_vm                     = */ host_get_vm,
    /* .get_context                = */ host_get_context,
    /* .register_hand_tracker      = */ host_register_hand_tracker,
    /* .publish_camera_frame_y8    = */ host_publish_camera_frame_y8,
    /* .subscribe_camera_frame     = */ host_subscribe_camera_frame,
    /* .get_module_data_dir        = */ host_get_module_data_dir,
};

const struct aug_host_api *
augins_host_api(void)
{
	return &g_host_api;
}

// ============================================================================ internal: hand-tracker dispatch
// Called from the stub hand-tracker xdev's get_hand_tracking implementation.
void
augins_hand_tracker_dispatch(uint32_t handed,
                             int64_t at_timestamp_ns,
                             struct xrt_hand_joint_set *out,
                             int64_t *out_timestamp_ns)
{
	aug_hand_get_joints_fn cb;
	void *ud;
	{
		std::lock_guard<std::mutex> lk(g_ht_mu);
		cb = g_ht_cb;
		ud = g_ht_cb_userdata;
	}
	if (cb == nullptr) {
		std::memset(out, 0, sizeof(*out));
		if (out_timestamp_ns != nullptr) {
			*out_timestamp_ns = at_timestamp_ns;
		}
		return;
	}
	cb(handed, at_timestamp_ns, out, out_timestamp_ns, ud);
}

// ============================================================================ internal: per-module TLS scope
// RAII-friendly (in C-callable form) accessor for the lifecycle dispatcher.
// Set before calling a module entry point; clear after the call returns.
void
augins_set_calling_module_dir(const char *dir)
{
	g_tls_calling_module_dir = (dir != nullptr) ? dir : "";
}

void
augins_clear_calling_module_dir(void)
{
	g_tls_calling_module_dir.clear();
}

} // extern "C"

// Pull in <algorithm> for std::remove_if (used above). Placed at the bottom so
// the extern "C" block stays single-source-of-truth for the API surface.
#include <algorithm>
