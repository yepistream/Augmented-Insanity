# Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Aug-Ins v0.2 service-side module subsystem.
#
# Phase 2a baseline: scaffolding only. Loader is a stub that logs and
# returns; dispatch registry is permanently empty; host API table is
# real (used by future modules but currently has no callers).
#
# Linked into:
#   - ipc_server (so the IPC dispatch can probe `aug_has_modules_for`
#     in the generated dispatch table once Phase 2d lands)
#   - augins-service shared library (so service_target.cpp can call
#     augins_loader_init / _shutdown at service start / stop)
#
# Both link sites are gated on XRT_FEATURE_AUG_INS; see src/xrt/ipc/
# CMakeLists.txt and src/xrt/targets/service-lib/CMakeLists.txt.

add_library(
	aux_augins STATIC
	module_abi.h
	host_api.cpp
	host_api.h
	dispatch.cpp
	dispatch.h
	extract.cpp
	extract.h
	manifest.cpp
	manifest.h
	loader.cpp
	loader.h
	lifecycle.cpp
	lifecycle.h
	adapters.cpp  # Phase 2d: hand-written per-OpenXR-function adapters
	adapters.h
	)

target_include_directories(
	aux_augins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
	)

# adapters.cpp uses XrSpaceLocation etc. (xrt-external-openxr) and calls
# ipc_handle_<call> from the IPC server (ipc_server's source headers).
# Both are headers-only deps for compilation; link is supplied by ipc_server
# at the final link of libaugins-service.so.
target_link_libraries(
	aux_augins
	PUBLIC
		xrt-external-openxr  # <openxr/openxr.h> for adapter signatures
	PRIVATE
		aux_util             # xrt_results.h, xrt_defines.h, U_LOG
		ipc_shared           # shared/ipc_protocol.h
		xrt-external-cjson   # metadata.json parsing
		xrt-external-miniz   # .augins zip extraction
		${CMAKE_DL_LIBS}
		${ANDROID_LOG_LIBRARY}
	)

# Note: adapters.cpp does NOT include the IPC server's generated header
# (it lives in the build dir and would require exposing that publicly).
# Each adapter hand-forward-declares the ipc_handle_* it delegates to;
# signature changes will produce a compile-time mismatch and we update.

set_property(TARGET aux_augins PROPERTY POSITION_INDEPENDENT_CODE ON)
