# Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Aug-Ins ARCore head-pose sample .augins module.
#
# Standalone -- does NOT participate in the main Aug-Ins CMake build. The
# wrapping samples/augins-arcore-headpose/build.gradle invokes the NDK
# toolchain directly and zips the result into arcore-headpose.augins.

cmake_minimum_required(VERSION 3.22)
project(augins-arcore-headpose LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Path to the v0.2 public module ABI header. Module authors include
# only "module_abi.h"; XrSpaceLocation etc. come from <openxr/openxr.h>.
# The xrt/xrt_handles.h path is only here for the vendored arcore_instance.h
# shim, not for the module .cpp itself.
set(AUG_RUNTIME_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/xrt/augins")
set(OPENXR_INCLUDE      "${CMAKE_CURRENT_SOURCE_DIR}/../../src/external/openxr_includes")

# Vendored ARCore SDK + the user's tiny ARCore wrapper.
set(VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor")

# Imported target for the prebuilt ARCore client .so. The actual binary lives
# under vendor/<abi>/libarcore_sdk_c.so and gets bundled into the .augins zip
# by build.gradle so the Aug-Ins module loader can resolve it via
# android_dlopen_ext + ANDROID_DLEXT_USE_LIBRARY_PATH.
add_library(arcore_sdk_c SHARED IMPORTED)
set_target_properties(arcore_sdk_c PROPERTIES
    IMPORTED_LOCATION "${VENDOR_DIR}/${ANDROID_ABI}/libarcore_sdk_c.so"
    )

add_library(arcore_headpose_module SHARED
    arcore_module.cpp
    vendor/arcore_instance.cpp
    )

target_include_directories(arcore_headpose_module PRIVATE
    ${VENDOR_DIR}            # arcore_c_api.h, arcore_instance.h
    ${VENDOR_DIR}/xrt        # xrt_handles.h shim used by arcore_instance.cpp
    ${AUG_RUNTIME_INCLUDE}   # module_abi.h
    ${OPENXR_INCLUDE}        # <openxr/openxr.h>
    )

target_link_libraries(arcore_headpose_module PRIVATE
    arcore_sdk_c
    android log EGL GLESv2
    )

# The .augins loader does dlopen("<module_id>.so", ...) -- output filename must
# be exactly <ID from metadata.json>.so, no "lib" prefix.
set_target_properties(arcore_headpose_module PROPERTIES
    PREFIX ""
    OUTPUT_NAME "com.augmented_insanity.samples.arcore_headpose"
    SUFFIX ".so"
    )
