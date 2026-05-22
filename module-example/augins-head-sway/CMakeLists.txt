# Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Aug-Ins v0.2 head-sway tutorial module.
# Standalone NDK build. No external SDK dependencies.

cmake_minimum_required(VERSION 3.22)
project(augins-head-sway LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(AUG_RUNTIME_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/xrt/augins")
set(OPENXR_INCLUDE      "${CMAKE_CURRENT_SOURCE_DIR}/../../src/external/openxr_includes")

add_library(head_sway_module SHARED head_sway.cpp)

target_include_directories(head_sway_module PRIVATE
    ${AUG_RUNTIME_INCLUDE}    # module_abi.h
    ${OPENXR_INCLUDE}         # <openxr/openxr.h>
    )

target_link_libraries(head_sway_module PRIVATE
    log
    )

# The loader does dlopen("<ID from metadata.json>.so", ...). Output
# filename must be exactly the manifest ID, no "lib" prefix.
set_target_properties(head_sway_module PROPERTIES
    PREFIX ""
    OUTPUT_NAME "com.augmented_insanity.examples.head_sway"
    SUFFIX ".so"
    )
