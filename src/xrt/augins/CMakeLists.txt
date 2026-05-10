# Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Augmented Insanity (Aug-Ins) module loader.
#
# Static library linked into the IPC service binary. Hosts the .augins
# unpacking logic, the priority-sorted dispatch table, the GRS state machine,
# and the aggregated extension list used by xrEnumerateInstanceExtensionProperties.

add_library(
	aux_augins STATIC
	augins_dispatch.cpp
	augins_dispatch.h
	augins_extensions.cpp
	augins_extensions.h
	augins_hand_tracker_dispatch.h
	augins_host_api.cpp
	augins_host_api.h
	augins_lifecycle.cpp
	augins_lifecycle.h
	augins_module_abi.h
	augins_stub_xdevs.cpp
	augins_stub_xdevs.h
	)

target_include_directories(
	aux_augins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
	)

target_link_libraries(
	aux_augins
	PUBLIC xrt-interfaces xrt-external-openxr
	PRIVATE
		aux_android
		aux_util
		base
		ipc_shared
		xrt-external-cjson
		xrt-external-miniz
		${CMAKE_DL_LIBS}
		${ANDROID_LOG_LIBRARY}
	)

set_property(TARGET aux_augins PROPERTY POSITION_INDEPENDENT_CODE ON)
