# Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Aug-Ins no-op sample .augins module.
#
# Standalone Ã¢â‚¬â€ does NOT participate in the main Monado/Aug-Ins CMake build.
# Configure and build this directory directly with the Android NDK toolchain;
# the wrapping samples/augins-noop/build.gradle does that for you and zips
# the output into noop.augins.

cmake_minimum_required(VERSION 3.22)
project(augins-noop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(noop_module SHARED noop.cpp)

target_link_libraries(noop_module PRIVATE android log)

# The .augins loader does dlopen("<module_id>.so", ...) Ã¢â‚¬â€ the file inside the
# zip must be named exactly <ID from metadata.json>.so, no "lib" prefix.
set_target_properties(noop_module PROPERTIES
	PREFIX ""
	OUTPUT_NAME "com.augmented_insanity.samples.noop"
	SUFFIX ".so"
	)
