# Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Aug-Ins head-sway tutorial module.
#
# Standalone -- does NOT participate in the main Aug-Ins CMake build. The
# wrapping module-example/augins-head-sway/build.gradle invokes the NDK
# toolchain directly and zips the result into head-sway.augins.

cmake_minimum_required(VERSION 3.22)
project(augins-head-sway LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# We need exactly two header trees from the runtime tree:
#   1. src/xrt/augins/      provides augins_module_abi.h (the host API table)
#   2. src/xrt/include/     provides xrt/xrt_defines.h (xrt_pose, xrt_quat,
#                                                       xrt_space_relation,
#                                                       XRT_INPUT_*)
# augins_module_abi.h includes xrt/xrt_defines.h transitively, so both must be
# on the include path.
set(AUG_RUNTIME_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/xrt/augins")
set(XRT_PUBLIC_INCLUDE  "${CMAKE_CURRENT_SOURCE_DIR}/../../src/xrt/include")

add_library(head_sway_module SHARED head_sway.cpp)

target_include_directories(head_sway_module PRIVATE
    ${AUG_RUNTIME_INCLUDE}
    ${XRT_PUBLIC_INCLUDE}
    )

target_link_libraries(head_sway_module PRIVATE android log)

# The .augins loader does dlopen("<module_id>.so", ...) -- the file inside the
# zip must be named exactly <ID from metadata.json>.so, no "lib" prefix.
set_target_properties(head_sway_module PROPERTIES
    PREFIX ""
    OUTPUT_NAME "com.augmented_insanity.examples.head_sway"
    SUFFIX ".so"
    )
