# Copyright 2019-2023, Collabora, Ltd.
# Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
# SPDX-License-Identifier: BSL-1.0

# Order matters.
add_subdirectory(include)
add_subdirectory(auxiliary)
add_subdirectory(tracking)
add_subdirectory(base)
add_subdirectory(drivers)
if(XRT_MODULE_COMPOSITOR)
	add_subdirectory(compositor)
endif()
add_subdirectory(state_trackers)
if(XRT_MODULE_IPC)
	add_subdirectory(ipc)
endif()
if(XRT_FEATURE_AUG_INS)
	add_subdirectory(augins)
endif()
add_subdirectory(targets)
