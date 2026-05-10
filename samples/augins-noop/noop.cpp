// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Aug-Ins no-op sample module.
//
// Smallest possible .augins module Ã¢â‚¬â€ exists to validate the GRS pipeline
// end-to-end (extract Ã¢â€ â€™ metadata parse Ã¢â€ â€™ dlopen Ã¢â€ â€™ dlsym Ã¢â€ â€™ dispatch) without
// pulling in any external SDKs or tracking libraries.
//
// Hook calling convention (set by the GRS, documented in augins_module_abi.h):
//   a0 = volatile struct ipc_client_state *  (opaque session context)
//   a1 = IPC message struct *                (deserialized input args, or NULL)
//   a2 = IPC reply struct *                  (writable output args, or NULL)
//   a3 = NULL (reserved)
//
// All hooks are called BEFORE the IPC reply is sent to the client, so a2
// can be modified to override output values (e.g. pose for xrLocateSpace).

#include <android/log.h>
#include <stdint.h>

#define LOG_TAG "AugInsNoop"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define AUG_OK 0

extern "C" {

// ---- GRS lifecycle hooks --------------------------------------------------

void
aug_onModuleLoad(void * /*args*/)
{
	LOGI("aug_onModuleLoad: noop module is alive");
}

void
aug_runtimeInit(void * /*args*/)
{
	LOGI("aug_runtimeInit: dispatch table built");
}

void
aug_onConnect(void * /*args*/)
{
	LOGI("aug_onConnect: an OpenXR client just attached");
}

// ---- OpenXR hooks ---------------------------------------------------------
// a0 = ics (opaque), a1 = ipc_session_create_msg*, a2 = ipc_result_reply*, a3 = NULL

int32_t
xrCreateSession(void *ics, void * /*msg*/, void * /*reply*/, void * /*unused*/)
{
	LOGI("xrCreateSession hook: ics=%p", ics);
	return AUG_OK;
}

} // extern "C"
