// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * @file
 * @brief  Parse + validate a module's metadata.json.
 *
 * Schema follows the locked-in v0.2 design:
 *   Required: Manifest_Version (int = 1), ID (string), Version (string),
 *             Implemented_Functions (array of strings)
 *   Optional: Name (string), Description (string), Priority (int, default 100)
 *
 * Reserved-for-later (loader will silently accept but ignore in v0.2):
 *   Failure_Mode, Dependencies
 *
 * Internal header. Not part of the module ABI.
 *
 * @ingroup aug_runtime
 */

#pragma once

#include <string>
#include <vector>

namespace augins {

struct Manifest
{
	int                       manifest_version = 0;
	std::string               id;
	std::string               version;
	std::vector<std::string>  implemented_functions;
	std::string               name;        // optional; defaults to id
	std::string               description; // optional; "" if absent
	int                       priority = 100;
};

/*!
 * Parse the metadata.json at `path` into `out`. Returns true on success.
 * On failure logs the error and leaves `out` in an unspecified state.
 *
 * Required-field checks:
 *   - manifest_version must be present and equal AUG_MANIFEST_VERSION
 *     (returns false otherwise with a clear log line about version mismatch)
 *   - id must be a non-empty string
 *   - version must be present
 *   - implemented_functions must be present and an array of strings
 *     (an empty array is OK -- modules can exist purely for lifecycle effects)
 */
bool
manifest_parse_file(const std::string &path, Manifest &out);

} // namespace augins
