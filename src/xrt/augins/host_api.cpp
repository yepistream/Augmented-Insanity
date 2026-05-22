// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Aug-Ins v0.2 host API implementation.
//
// State held in this TU:
//
//   - g_jvm / g_ctx: set once at service start by
//     augins_host_api_set_jvm_ctx; read lock-free by get_jvm/get_context.
//
//   - tls_data_dir_stack: per-thread stack of data-dir paths the
//     dispatcher pushes/pops around every module call. get_module_data_dir
//     returns the top of the calling thread's stack, or "" if empty.
//
// The host API table itself (g_host_api) is a file-scope const; the
// runtime hands the same pointer to every module via aug_on_module_load.

#include "host_api.h"

#include <android/log.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <vector>
#include <string>

#define TAG "Aug-Ins.HostApi"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

namespace {

std::atomic<void *> g_jvm{nullptr};
std::atomic<void *> g_ctx{nullptr};

// Per-thread stack of data-dir strings. The dispatcher pushes before
// calling a module's function and pops after. We use a stack rather
// than a single slot so a module that (re-)enters the dispatcher
// during a call -- e.g. by invoking another runtime function that
// itself triggers module dispatch -- gets the right path each frame.
thread_local std::vector<const char *> tls_data_dir_stack;

} // namespace

// ---------------------------------------------------------------------------
// API table implementations
// ---------------------------------------------------------------------------

static void *
get_jvm_impl(void)
{
	return g_jvm.load(std::memory_order_acquire);
}

static void *
get_context_impl(void)
{
	return g_ctx.load(std::memory_order_acquire);
}

static const char *
get_module_data_dir_impl(void)
{
	if (tls_data_dir_stack.empty()) {
		return "";
	}
	return tls_data_dir_stack.back();
}

// Maps Aug-Ins log levels (0-4) to Android log priorities. Anything
// out of range gets clamped to INFO.
static int
to_android_priority(int aug_level)
{
	switch (aug_level) {
	case 0: return ANDROID_LOG_VERBOSE; // trace
	case 1: return ANDROID_LOG_DEBUG;
	case 2: return ANDROID_LOG_INFO;
	case 3: return ANDROID_LOG_WARN;
	case 4: return ANDROID_LOG_ERROR;
	default: return ANDROID_LOG_INFO;
	}
}

static void
log_impl(int level, const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	(void)vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	__android_log_print(to_android_priority(level), "Aug-Ins.Module", "%s", buf);
}

// ---------------------------------------------------------------------------
// The host API table modules see.
//
// File-scope const so a single instance is reused for every module
// load -- the runtime never mutates this struct after startup, and
// modules never need to compare pointers across loads.
// ---------------------------------------------------------------------------

static const struct aug_host_api g_host_api = {
    /* .struct_version       = */ AUG_HOST_API_VERSION,
    /* ._reserved            = */ 0u,
    /* .get_jvm              = */ &get_jvm_impl,
    /* .get_context          = */ &get_context_impl,
    /* .get_module_data_dir  = */ &get_module_data_dir_impl,
    /* .log                  = */ &log_impl,
};

// ---------------------------------------------------------------------------
// Internal getters / setters
// ---------------------------------------------------------------------------

const struct aug_host_api *
augins_host_api_get(void)
{
	return &g_host_api;
}

void
augins_host_api_set_jvm_ctx(void *jvm, void *ctx)
{
	g_jvm.store(jvm, std::memory_order_release);
	g_ctx.store(ctx, std::memory_order_release);
	LOGI("host API JVM/Context bound (jvm=%p ctx=%p)", jvm, ctx);
}

void
augins_host_api_push_data_dir(const char *path)
{
	tls_data_dir_stack.push_back(path != nullptr ? path : "");
}

void
augins_host_api_pop_data_dir(void)
{
	if (!tls_data_dir_stack.empty()) {
		tls_data_dir_stack.pop_back();
	}
}
