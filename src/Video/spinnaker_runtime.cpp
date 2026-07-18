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

#include "spinnaker_runtime.h"

#if defined(HAVE_SPINNAKER) && defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "pp_debug.h"

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <string>

namespace pinpoint::spinnaker {
namespace {

// The native C++ runtime DLL our import library (Spinnaker_v140.lib) binds to, and
// the toolset subdirectory it lives in under an SDK root.
constexpr const char *kBinSubdir = "bin64/vs2015";
constexpr const char *kCoreDll   = "Spinnaker_v140.dll";

// Candidate SDK install roots, in priority order (first existing wins): an explicit
// override, then the vendor's historical default install locations across the
// Teledyne / FLIR / Point Grey renames.
QStringList candidateRoots()
{
    QStringList roots;
    for (const char *var : { "PINPOINT_SPINNAKER_ROOT", "SPINNAKER_ROOT" }) {
        const QString v = qEnvironmentVariable(var);
        if (!v.isEmpty())
            roots << v;
    }
    const QString programFiles = qEnvironmentVariable("ProgramFiles", "C:/Program Files");
    roots << programFiles + "/Teledyne/Spinnaker"
          << programFiles + "/FLIR Systems/Spinnaker"
          << programFiles + "/Point Grey Research/Spinnaker";
    return roots;
}

bool probeAndLoad()
{
    for (const QString &root : candidateRoots()) {
        const QDir     binDir(QDir(root).filePath(kBinSubdir));
        const QString  dllPath = binDir.filePath(kCoreDll);
        if (!QFileInfo::exists(dllPath))
            continue;

        // Preload the core DLL by absolute path with LOAD_WITH_ALTERED_SEARCH_PATH so
        // its sibling dependency graph (SpinnakerC_v140, GenApi/GCBase/Log, libiomp5md,
        // …) resolves from the SDK folder rather than the app directory. Once the
        // module is in the process, the delay-load thunk for Spinnaker_v140.dll binds
        // to it by name — no further search or DLL-directory changes are needed. We
        // deliberately avoid SetDefaultDllDirectories(): it is process-wide and would
        // drop PATH from the default search, which could disturb how other native
        // dependencies (ONNX Runtime, CUDA) are resolved.
        const std::wstring dllW =
            QDir::toNativeSeparators(QFileInfo(dllPath).absoluteFilePath()).toStdWString();
        const HMODULE h = ::LoadLibraryExW(dllW.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (h) {
            ppInfo() << "[Spinnaker] SDK found at" << QDir::toNativeSeparators(root)
                     << "— high-speed camera support enabled.";
            return true;
        }
        ppWarn() << "[Spinnaker] found" << QDir::toNativeSeparators(dllPath)
                 << "but LoadLibraryEx failed (error" << ::GetLastError()
                 << ") — high-speed camera support disabled.";
        return false;
    }
    ppInfo() << "[Spinnaker] SDK not found — high-speed camera support disabled. "
                "Install the Teledyne Spinnaker SDK to enable it.";
    return false;
}

}  // namespace

bool runtimeAvailable()
{
    // Thread-safe, run-once initialisation (C++11 magic statics).
    static const bool available = probeAndLoad();
    return available;
}

}  // namespace pinpoint::spinnaker

#else  // !(HAVE_SPINNAKER && _WIN32)

namespace pinpoint::spinnaker {
bool runtimeAvailable() { return false; }
}  // namespace pinpoint::spinnaker

#endif
