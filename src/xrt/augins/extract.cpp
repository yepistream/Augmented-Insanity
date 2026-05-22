// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// .augins zip extraction. Vendored from the v0.2 client-side loader's
// extract code (which read from an fd); this version reads from a
// filesystem path because the service has direct access to its own
// modules dir.

#include "extract.h"

#include "miniz.h"

#include <android/log.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#define TAG "Aug-Ins.Extract"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace augins {

namespace {

// mkdir_p -- create a directory and all missing parents.
// EEXIST is fine; everything else is fatal.
bool
mkdir_p(const std::string &path)
{
	for (size_t i = 1; i <= path.size(); ++i) {
		if (i == path.size() || path[i] == '/') {
			std::string seg = path.substr(0, i);
			if (seg.empty()) continue;
			if (::mkdir(seg.c_str(), 0700) != 0 && errno != EEXIST) {
				LOGE("mkdir(%s): %s", seg.c_str(), std::strerror(errno));
				return false;
			}
		}
	}
	return true;
}

// Read a whole file into a byte vector.
bool
read_file_bytes(const std::string &path, std::vector<uint8_t> &out, size_t *out_byte_count)
{
	int fd = ::open(path.c_str(), O_RDONLY);
	if (fd < 0) {
		LOGE("open(%s): %s", path.c_str(), std::strerror(errno));
		return false;
	}
	struct stat st = {};
	if (::fstat(fd, &st) != 0) {
		LOGE("fstat(%s): %s", path.c_str(), std::strerror(errno));
		::close(fd);
		return false;
	}
	out.resize((size_t)st.st_size);
	if (out_byte_count) *out_byte_count = (size_t)st.st_size;

	size_t off = 0;
	while (off < out.size()) {
		ssize_t n = ::read(fd, out.data() + off, out.size() - off);
		if (n <= 0) {
			LOGE("read at %zu/%zu of %s: %s",
			     off, out.size(), path.c_str(), std::strerror(errno));
			::close(fd);
			return false;
		}
		off += (size_t)n;
	}
	::close(fd);
	return true;
}

// Write a buffer to a file (creating parent dirs if needed). Mode 0600
// is intentionally restrictive: extracted .so files do not need the
// execute bit on Android because dlopen mmaps with PROT_EXEC
// regardless of the filesystem mode.
bool
write_file(const std::string &path, const void *data, size_t size)
{
	size_t slash = path.find_last_of('/');
	if (slash != std::string::npos) {
		if (!mkdir_p(path.substr(0, slash))) return false;
	}
	int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) {
		LOGE("open(%s): %s", path.c_str(), std::strerror(errno));
		return false;
	}
	const uint8_t *p = static_cast<const uint8_t *>(data);
	size_t off = 0;
	while (off < size) {
		ssize_t n = ::write(fd, p + off, size - off);
		if (n < 0) {
			LOGE("write(%s) at %zu/%zu: %s",
			     path.c_str(), off, size, std::strerror(errno));
			::close(fd);
			return false;
		}
		off += (size_t)n;
	}
	::close(fd);
	return true;
}

// Size-stamp cache helpers. We compare the source .augins zip's byte
// count against a stamp file in the extraction directory; equal sizes
// mean the extracted tree is up to date.
bool
read_size_stamp(const std::string &dir, size_t &out_size)
{
	std::ifstream f(dir + "/.size");
	if (!f.is_open()) return false;
	size_t v = 0;
	f >> v;
	if (!f) return false;
	out_size = v;
	return true;
}

void
write_size_stamp(const std::string &dir, size_t size)
{
	std::ofstream f(dir + "/.size", std::ios::trunc);
	if (!f.is_open()) {
		LOGW("write .size stamp: open failed in %s", dir.c_str());
		return;
	}
	f << size;
}

} // namespace

bool
extract_augins_zip(const std::string &augins_zip_path,
                   const std::string &staging_subdir,
                   const std::string &cache_root,
                   std::string       &out_dir)
{
	const std::string dest = cache_root + "/" + staging_subdir;
	out_dir = dest;

	std::vector<uint8_t> blob;
	size_t src_size = 0;
	if (!read_file_bytes(augins_zip_path, blob, &src_size)) {
		return false;
	}

	// Cache hit: matching source size -> assume the extracted tree is
	// up to date. Cheap, correct for our use case (a rebuilt module
	// has different byte content, almost always different size).
	{
		size_t cached = 0;
		if (read_size_stamp(dest, cached) && cached == src_size) {
			LOGI("%s: cache hit at %s (%zu bytes)",
			     staging_subdir.c_str(), dest.c_str(), cached);
			return true;
		}
	}

	if (!mkdir_p(dest)) {
		return false;
	}

	mz_zip_archive zip = {};
	if (!mz_zip_reader_init_mem(&zip, blob.data(), blob.size(), 0)) {
		LOGE("mz_zip_reader_init_mem failed for %s (size=%zu)",
		     augins_zip_path.c_str(), blob.size());
		return false;
	}

	mz_uint count = mz_zip_reader_get_num_files(&zip);
	for (mz_uint i = 0; i < count; ++i) {
		mz_zip_archive_file_stat fs = {};
		if (!mz_zip_reader_file_stat(&zip, i, &fs)) {
			LOGW("entry %u: stat failed; skipping", i);
			continue;
		}
		if (mz_zip_reader_is_file_a_directory(&zip, i)) {
			mkdir_p(dest + "/" + fs.m_filename);
			continue;
		}

		// Path-traversal guard: refuse "..", refuse leading "/".
		std::string name = fs.m_filename;
		if (name.find("..") != std::string::npos || (!name.empty() && name[0] == '/')) {
			LOGW("entry %u: suspicious filename '%s' rejected", i, name.c_str());
			continue;
		}

		size_t out_size = 0;
		void *out = mz_zip_reader_extract_to_heap(&zip, i, &out_size, 0);
		if (out == nullptr) {
			LOGE("extract_to_heap failed on '%s' in %s",
			     name.c_str(), augins_zip_path.c_str());
			mz_zip_reader_end(&zip);
			return false;
		}

		bool ok = write_file(dest + "/" + name, out, out_size);
		mz_free(out);
		if (!ok) {
			mz_zip_reader_end(&zip);
			return false;
		}
	}

	mz_zip_reader_end(&zip);
	write_size_stamp(dest, src_size);
	LOGI("%s: extracted %u entries into %s",
	     staging_subdir.c_str(), count, dest.c_str());
	return true;
}

} // namespace augins
