# Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
# SPDX-License-Identifier: BSL-1.0
#
# miniz: single-file public-domain ZIP library
# Used by the Augmented Insanity module loader to extract .augins archives.

add_library(xrt-external-miniz STATIC miniz.c)
target_include_directories(
	xrt-external-miniz SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
	)
set_property(TARGET xrt-external-miniz PROPERTY POSITION_INDEPENDENT_CODE ON)
