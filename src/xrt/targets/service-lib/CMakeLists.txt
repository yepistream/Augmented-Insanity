# Copyright 2020, Collabora, Ltd.
# Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
# SPDX-License-Identifier: BSL-1.0

# The native library that backs the Augmented Insanity Android service.
# Renamed from the upstream Monado "monado-service" so it shows up under the
# Aug-Ins brand in adb / installed-app listings; the Java-side default in
# src/xrt/ipc/android/build.gradle was updated to match.
add_library(augins-service MODULE service_target.cpp)

target_link_libraries(
	augins-service
	PRIVATE
		aux_util
		st_prober
		ipc_server
		comp_main
		target_lists
		target_instance
		xrt-external-jni-wrap
	)

# Aug-Ins v0.2 service-side module subsystem. service_target.cpp calls
# augins_loader_init / _shutdown at service start / stop.
if(XRT_FEATURE_AUG_INS)
	target_link_libraries(augins-service PRIVATE aux_augins)
endif()
