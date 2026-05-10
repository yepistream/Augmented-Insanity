// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * @file
 * @brief  Aggregated OpenXR features advertised by loaded Aug-Ins modules.
 *
 * Two parallel aggregations live here:
 *   1. Extension names Ã¢â‚¬â€ used by the OpenXR state tracker to merge module-
 *      advertised extensions into xrEnumerateInstanceExtensionProperties.
 *   2. System-property bits Ã¢â‚¬â€ used by the OpenXR state tracker when filling
 *      e.g. XrSystemHandTrackingPropertiesEXT.supportsHandTracking.
 *
 * Modules declare both via their metadata.json:
 *   "Advertised_OpenXR_Features": {
 *     "Extensions": ["XR_EXT_hand_tracking"],
 *     "SystemPropertyBits": ["handTracking"]
 *   }
 *
 * The legacy top-level "Advertised_Extensions": [...] is also still accepted.
 *
 * @ingroup augins
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

// System-property bit enum Ã¢â‚¬â€ keep these in sync with the manifest string keys
// in augins_dispatch.cpp::parse_system_property_bit_name().
//
// Includable from C, so plain enum / preprocessor Ã¢â‚¬â€ no `enum class`.
#define AUGINS_SYS_HAND_TRACKING (1ULL << 0)
#define AUGINS_SYS_EYE_TRACKING  (1ULL << 1)
// Add more here as future modules need them. The aggregator just OR's.

#ifdef __cplusplus
#include <string>
#include <vector>

namespace augins {

// Add a name to the global advertised set; deduplicated.
void
add_advertised_extension(const std::string &name);

// OR the bit into the aggregated mask.
void
add_advertised_system_bit(uint64_t bit);

// Read-only snapshot of all module-advertised extension names.
const std::vector<std::string> &
get_advertising_extensions(void);

// Aggregated system-property bit mask (OR of all modules).
uint64_t
get_advertising_system_bits(void);

} // namespace augins

extern "C" {
#endif

// Plain-C accessors for use from oxr_api_instance.c (which is C, not C++).
// The C side calls augins_get_extension_count() then iterates 0..count-1
// calling augins_get_extension_name(i).
size_t
augins_get_extension_count(void);

const char *
augins_get_extension_name(size_t index);

// Aggregated system-property bit mask (uses AUGINS_SYS_* macros above).
uint64_t
augins_get_system_bits(void);

#ifdef __cplusplus
}
#endif
