# Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Aug-Ins Mercury (ARCore camera) hand-tracking module.
#
# Built in-tree as a sibling target to augins-service / openxr_monado so it can
# directly link against Mercury's static libs (t_ht_mercury_*, drv_ht,
# hand_async). Top-level CMakeLists.txt add_subdirectory()'s here only when
# XRT_MODULE_MERCURY_HANDTRACKING is ON (no Mercury Ã¢â€ â€™ no module).
#
# Output: com.augmented_insanity.samples.mercury_handtracking_arcore.so
# Packaged into mercury-handtracking-arcore.augins by a Gradle Zip task.

if(NOT XRT_MODULE_MERCURY_HANDTRACKING)
	message(STATUS "Skipping samples/augins-mercury-handtracking-arcore: Mercury disabled")
	return()
endif()

set(_module_id "com.augmented_insanity.samples.mercury_handtracking_arcore")

add_library(${_module_id} SHARED mercury_arcore_module.cpp)

target_include_directories(
	${_module_id} PRIVATE
		${CMAKE_SOURCE_DIR}/src/xrt/augins
		# Drivers tree provides "ht/ht_interface.h"
		${CMAKE_SOURCE_DIR}/src/xrt/drivers
)

target_link_libraries(
	${_module_id} PRIVATE
		# Mercury & friends Ã¢â‚¬â€ static libs from the in-tree build.
		drv_ht
		t_ht_mercury
		t_ht_mercury_kine_lm
		t_ht_mercury_model
		t_ht_mercury_distorter
		hand_async
		# Monado interfaces / helpers needed by the glue code.
		xrt-interfaces
		aux_util
		aux_tracking
		${ANDROID_LOG_LIBRARY}
)

set_target_properties(${_module_id} PROPERTIES
	CXX_STANDARD 20
	POSITION_INDEPENDENT_CODE ON
	# Match the .augins zip layout: bare module-id .so name, no "lib" prefix.
	PREFIX ""
	OUTPUT_NAME "${_module_id}"
)
