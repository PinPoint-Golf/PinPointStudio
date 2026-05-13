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
#include <QDebug>
#include <QLoggingCategory>

// PINPOINT_DEBUG_LEVEL controls in-app output verbosity.
//   0 = silent (no PinPoint output at all)
//   1 = warnings + errors only          (default — clean release output)
//   2 = startup info + warnings/errors  (development default)
//   3 = verbose debug + all of the above
//
// Set at CMake configure time: -DPINPOINT_DEBUG_LEVEL=N
// Default is 1 for Release builds and 2 for Debug builds (see CMakeLists.txt).
#ifndef PINPOINT_DEBUG_LEVEL
#  define PINPOINT_DEBUG_LEVEL 1
#endif

Q_DECLARE_LOGGING_CATEGORY(lcPP)

// ppDebug — per-operation trace, compiled away below level 3
#if PINPOINT_DEBUG_LEVEL >= 3
#  define ppDebug() qCDebug(lcPP)
#else
#  define ppDebug() QT_NO_QDEBUG_MACRO()
#endif

// ppInfo — startup / configuration messages, compiled away below level 2
#if PINPOINT_DEBUG_LEVEL >= 2
#  define ppInfo()  qCInfo(lcPP)
#else
#  define ppInfo()  QT_NO_QDEBUG_MACRO()
#endif

// ppWarn / ppError — always emitted regardless of level
#define ppWarn()    qCWarning(lcPP)
#define ppError()   qCCritical(lcPP)

namespace PinPointDebug {
    // Call once at the very start of main(), before creating any Qt objects.
    // Installs the Qt message handler, silences whisper/ggml/FFmpeg dependency noise.
    void install();
}
