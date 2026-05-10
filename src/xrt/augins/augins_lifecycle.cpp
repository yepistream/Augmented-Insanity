// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * @file
 * @brief  Aug-Ins runtime state machine and lifecycle fan-out.
 *
 * Implements design doc Ã‚Â§7 (lifecycle callbacks) and Ã‚Â§8 (process states).
 *
 * @ingroup augins
 */

#include "augins_lifecycle.h"

#include "augins_dispatch.h"
#include "augins_hand_tracker_dispatch.h"
#include "augins_host_api.h"
#include "augins_module_abi.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <dlfcn.h>
#include <mutex>
#include <thread>

#include <android/log.h>

#define LOG_TAG "Aug-Ins"
#define AUG_LOGI(fmt, ...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, fmt, ##__VA_ARGS__)
#define AUG_LOGW(fmt, ...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, fmt, ##__VA_ARGS__)
#define AUG_LOGE(fmt, ...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, fmt, ##__VA_ARGS__)

namespace augins {

namespace {

std::atomic<state_t> g_state{state_t::Launching};
std::atomic<bool> g_launched{false};

// Background loop thread state.
std::thread g_bg_thread;
std::atomic<bool> g_bg_running{false};
std::atomic<bool> g_bg_paused{false};
std::mutex g_bg_mu;
std::condition_variable g_bg_cv;

// Frame interval for the back-buffer loop. Modules that need more than this
// can implement frame-coherent work in aug_frame_begin / aug_frame_end.
constexpr auto kBackBufferTickInterval = std::chrono::milliseconds(16);

// Pointer-to-data-member: each callback slot in aug_module is a void* dlsym
// result. Using a member-pointer keeps fan_out callers as single-line.
using cb_member_t = void *aug_module::*;

void
fan_out(const char *label, cb_member_t member, void *args = nullptr)
{
	(void)label;
	for (const auto &mod : get_loaded_modules()) {
		void *sym = mod.*member;
		if (!sym) {
			continue;
		}
		// Set the per-module TLS so host_get_module_data_dir() returns the
		// caller's extraction dir for the duration of this callback. Cleared
		// before we return to caller code, even on exception.
		augins_set_calling_module_dir(mod.cache_dir.c_str());
		auto fn = reinterpret_cast<aug_lifecycle_fn>(sym);
		fn(args);
		augins_clear_calling_module_dir();
	}
}

void
back_buffer_loop_body(void)
{
	while (g_bg_running.load(std::memory_order_acquire)) {
		// Honour pauses cleanly without busy-spin.
		{
			std::unique_lock<std::mutex> lock(g_bg_mu);
			g_bg_cv.wait(lock, [] {
				return !g_bg_paused.load(std::memory_order_acquire) ||
				       !g_bg_running.load(std::memory_order_acquire);
			});
			if (!g_bg_running.load(std::memory_order_acquire)) {
				break;
			}
		}

		// Snapshot module list each tick so abort_module is safe.
		const auto &mods = get_loaded_modules();
		for (const auto &mod : mods) {
			if (!mod.cb_back_buffer_loop) {
				continue;
			}
			augins_set_calling_module_dir(mod.cache_dir.c_str());
			auto fn = reinterpret_cast<aug_lifecycle_fn>(mod.cb_back_buffer_loop);
			fn(nullptr);
			augins_clear_calling_module_dir();
		}

		std::this_thread::sleep_for(kBackBufferTickInterval);
	}
}

} // namespace

void
launch(const std::string &modules_dir, const std::string &cache_dir)
{
	bool expected = false;
	if (!g_launched.compare_exchange_strong(expected, true)) {
		AUG_LOGW("augins::launch called twice; ignoring");
		return;
	}

	AUG_LOGI("Aug-Ins launching (modules=%s, cache=%s)", modules_dir.c_str(), cache_dir.c_str());
	g_state.store(state_t::Launching);

	augins_load_all_from(modules_dir, cache_dir);

	fan_out("aug_onModuleLoad", &aug_module::cb_on_module_load,
	        const_cast<struct aug_host_api *>(augins_host_api()));

	augins_sort_dispatch_table();

	fan_out("aug_runtimeInit", &aug_module::cb_runtime_init);

	g_bg_running.store(true);
	g_bg_paused.store(false);
	g_bg_thread = std::thread(back_buffer_loop_body);

	g_state.store(state_t::Ready);
	AUG_LOGI("Aug-Ins state -> Ready_STATE");
}

void
shutdown(void)
{
	if (!g_launched.exchange(false)) {
		return;
	}

	g_bg_running.store(false);
	{
		std::lock_guard<std::mutex> lock(g_bg_mu);
		g_bg_paused.store(false);
	}
	g_bg_cv.notify_all();
	if (g_bg_thread.joinable()) {
		g_bg_thread.join();
	}

	// Now that the bg thread is gone and no module callbacks are racing, fire
	// the actual end-of-life hook. Modules use this to release resources they
	// allocated during aug_onModuleLoad / aug_onConnect.
	fan_out("aug_runtimeFinished", &aug_module::cb_runtime_finished);
}

void
on_client_connecting(void)
{
	g_state.store(state_t::Connecting);
	AUG_LOGI("Aug-Ins state -> Connecting_STATE");

	{
		std::lock_guard<std::mutex> lock(g_bg_mu);
		g_bg_paused.store(true);
	}
	g_bg_cv.notify_all();

	fan_out("aug_backBufferUpdateLoopPause", &aug_module::cb_back_buffer_pause);
	fan_out("aug_onConnect", &aug_module::cb_on_connect);

	g_state.store(state_t::Running);
	AUG_LOGI("Aug-Ins state -> Running_STATE");
}

void
on_client_disconnecting(void)
{
	g_state.store(state_t::Disconnecting);
	AUG_LOGI("Aug-Ins state -> Disconnecting_STATE");

	fan_out("aug_backBufferUpdateLoopResume", &aug_module::cb_back_buffer_resume);

	{
		std::lock_guard<std::mutex> lock(g_bg_mu);
		g_bg_paused.store(false);
	}
	g_bg_cv.notify_all();

	g_state.store(state_t::Ready);
	AUG_LOGI("Aug-Ins state -> Ready_STATE");
}

void
critical(const std::string &reason)
{
	g_state.store(state_t::Critical);
	AUG_LOGE("Aug-Ins state -> Critical_STATE (%s)", reason.c_str());
}

state_t
current_state(void)
{
	return g_state.load();
}

} // namespace augins

extern "C" {

void
augins_on_client_connecting_c(void)
{
	augins::on_client_connecting();
}

void
augins_on_client_disconnecting_c(void)
{
	augins::on_client_disconnecting();
}

int
augins_status_state(void)
{
	return static_cast<int>(augins::current_state());
}

} // extern "C"
