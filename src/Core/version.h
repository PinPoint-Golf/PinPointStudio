/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once

// ── Release version ───────────────────────────────────────────────────────────
// Bump MAJOR/MINOR for releases; set POSTFIX to "" for clean releases,
// e.g. "-beta1", "-alpha1", "-rc2" for pre-releases.
#define PINPOINT_VERSION_MAJOR   0
#define PINPOINT_VERSION_MINOR   1
#define PINPOINT_VERSION_POSTFIX "-alpha1"

// ── Assembled string ─────────────────────────────────────────────────────────
#define PP_STR_IMPL(x) #x
#define PP_STR(x)      PP_STR_IMPL(x)

#define PINPOINT_VERSION_STRING \
    "v" PP_STR(PINPOINT_VERSION_MAJOR) "." PP_STR(PINPOINT_VERSION_MINOR) PINPOINT_VERSION_POSTFIX
