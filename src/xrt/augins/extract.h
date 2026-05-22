// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * @file
 * @brief  In-memory extraction of a .augins zip onto disk.
 *
 * Vendored pattern from the deleted v0.2 client-side loader, adapted
 * for service-side use (reads from a filesystem path rather than a
 * ParcelFileDescriptor handed across IPC).
 *
 * Size-stamp cache: a `.size` file in the destination directory holds
 * the byte count of the .augins zip that produced the extraction. If
 * the source size matches the stamp, extraction is skipped on re-load.
 * This makes service restart cheap when modules haven't changed.
 *
 * Internal header. Not part of the module ABI.
 *
 * @ingroup aug_runtime
 */

#pragma once

#include <string>

namespace augins {

/*!
 * Extract `augins_zip_path` into `<cache_root>/<staging_subdir>/`.
 * Writes a `.size` stamp on success so a future call with the same
 * source byte size skips re-extraction.
 *
 * @param augins_zip_path  Absolute path to a .augins file on disk.
 * @param staging_subdir   Subdirectory name under cache_root for the
 *                         extracted tree. The actual module ID lives
 *                         inside metadata.json (which is inside the
 *                         zip), so the loader passes a filename-based
 *                         staging key here; the resolved-module-ID
 *                         renaming step is the loader's concern, not
 *                         this function's.
 * @param cache_root       Absolute path to the per-runtime extraction
 *                         cache root (e.g.
 *                         /data/.../cache/opennedmodules/).
 * @param out_dir          On success, filled with the absolute path
 *                         to the extracted tree.
 *
 * @return true on success (fresh extraction or cache hit). false on
 *         any unrecoverable error (zip open failure, corrupt entries,
 *         disk-full, path-escape attempt, etc.). Failures are logged.
 */
bool
extract_augins_zip(const std::string &augins_zip_path,
                   const std::string &staging_subdir,
                   const std::string &cache_root,
                   std::string       &out_dir);

} // namespace augins
