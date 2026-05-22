# Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Aug-Ins v0.2 test module: xrLocateSpace.

cmake_minimum_required(VERSION 3.22)
project(augins-test-locate-space LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(AUG_RUNTIME_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/xrt/augins")
set(OPENXR_INCLUDE      "${CMAKE_CURRENT_SOURCE_DIR}/../../src/external/openxr_includes")

add_library(locate_space_module SHARED locate_space_module.cpp)

target_include_directories(locate_space_module PRIVATE
    ${AUG_RUNTIME_INCLUDE}    # module_abi.h
    ${OPENXR_INCLUDE}         # <openxr/openxr.h>
    )

target_link_libraries(locate_space_module PRIVATE
    log
    )

set_target_properties(locate_space_module PROPERTIES
    PREFIX ""
    OUTPUT_NAME "com.augmented_insanity.samples.test_locate_space"
    SUFFIX ".so"
    )
