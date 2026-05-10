// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * @file
 * @brief  Aggregated OpenXR features advertised by loaded Aug-Ins modules.
 * @ingroup augins
 */

#include "augins_extensions.h"

#include <algorithm>
#include <atomic>
#include <mutex>

namespace {

std::vector<std::string> g_extensions;
std::atomic<uint64_t> g_system_bits{0};
std::mutex g_mu;

} // namespace

namespace augins {

void
add_advertised_extension(const std::string &name)
{
	if (name.empty()) {
		return;
	}
	std::lock_guard<std::mutex> lock(g_mu);
	if (std::find(g_extensions.begin(), g_extensions.end(), name) == g_extensions.end()) {
		g_extensions.push_back(name);
	}
}

void
add_advertised_system_bit(uint64_t bit)
{
	g_system_bits.fetch_or(bit, std::memory_order_relaxed);
}

const std::vector<std::string> &
get_advertising_extensions(void)
{
	return g_extensions;
}

uint64_t
get_advertising_system_bits(void)
{
	return g_system_bits.load(std::memory_order_relaxed);
}

} // namespace augins

extern "C" {

size_t
augins_get_extension_count(void)
{
	// No lock: extensions are only ever added during launch (single-threaded
	// against the OXR enumeration call). If this assumption changes, switch
	// to a snapshot-on-call pattern.
	return ::g_extensions.size();
}

const char *
augins_get_extension_name(size_t index)
{
	if (index >= ::g_extensions.size()) {
		return nullptr;
	}
	return ::g_extensions[index].c_str();
}

uint64_t
augins_get_system_bits(void)
{
	return ::g_system_bits.load(std::memory_order_relaxed);
}

} // extern "C"
