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
