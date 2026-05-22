# Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Minimal Aug-Ins v0.2 service-side test module: noop.
#
# Standalone NDK build, driven by the wrapping build.gradle's Exec
# tasks. Does NOT participate in the main runtime CMake graph -- it's
# packaged into noop.augins independently and installed via adb.

cmake_minimum_required(VERSION 3.22)
project(augins-test-noop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# In-tree sample: include path points at the runtime's augins headers.
# A third-party module author would vendor a copy of module_abi.h
# instead.
set(AUG_RUNTIME_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/xrt/augins")

add_library(noop_module SHARED noop_module.cpp)

target_include_directories(noop_module PRIVATE
    ${AUG_RUNTIME_INCLUDE}
    )

target_link_libraries(noop_module PRIVATE
    log
    )

# The loader does dlopen("<module_id>.so", ...) where <module_id> is
# the ID field from metadata.json. Output name must match exactly,
# no "lib" prefix.
set_target_properties(noop_module PROPERTIES
    PREFIX ""
    OUTPUT_NAME "com.augmented_insanity.samples.test_noop"
    SUFFIX ".so"
    )
