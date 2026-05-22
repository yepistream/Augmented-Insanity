// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal vendored shim of Monado's xrt/xrt_handles.h — only the Android
// graphics-buffer typedef the vendored arcore_instance.cpp references in
// arcore_min_get_latest_camera_frame. The head-pose .augins module never
// calls that function but the type still needs to exist for the file to
// compile. We intentionally omit everything else (POSIX fd handles, IPC
// handles, etc.) — they're not used.

#pragma once

#include <android/hardware_buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef AHardwareBuffer *xrt_graphics_buffer_handle_t;

#define XRT_GRAPHICS_BUFFER_HANDLE_INVALID ((xrt_graphics_buffer_handle_t)NULL)

#ifdef __cplusplus
}
#endif
