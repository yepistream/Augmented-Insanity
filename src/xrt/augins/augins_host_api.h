// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * @file
 * @brief  Internal accessor for the singleton aug_host_api table.
 *
 * Modules receive a `const struct aug_host_api *` via aug_onModuleLoad's `args`.
 * The runtime side builds and exposes that singleton here. Module authors do
 * not include this header Ã¢â‚¬â€ they only see `struct aug_host_api` in
 * augins_module_abi.h.
 *
 * @ingroup augins
 */

#pragma once

#include "augins_module_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

const struct aug_host_api *
augins_host_api(void);

#ifdef __cplusplus
}
#endif
