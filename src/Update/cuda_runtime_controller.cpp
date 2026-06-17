/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "cuda_runtime_controller.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>

#if defined(Q_OS_WIN)
#  include <QLibrary>
#endif

#include "pp_debug.h"

namespace {

// The cuDNN runtime DLL this build requires (CUDA 12 / cuDNN 9 — see CLAUDE.md). Its
// presence next to the executable is the "CUDA runtime installed AND at the right
// major" check: a future ORT/CUDA bump changes this constant in lockstep.
const char *const kRequiredCudnnDll = "cudnn64_9.dll";

// Where the user fetches the standalone `-cuda` runtime installer (its own AppId).
// GitHub's latest-release page over HTTPS — the trusted source for the v1 offer.
const char *const kReleasesUrl =
    "https://github.com/PinPoint-Golf/PinPointStudio/releases/latest";

// True iff installed by our Inno Setup installer (it drops unins000.exe next to the
// exe). Same probe as WinSparkleUpdater::isInstalledBuild(); duplicated here to keep
// this controller free of the Windows-only WinSparkle header on other platforms.
bool isInstalledBuild()
{
    return QFileInfo::exists(QCoreApplication::applicationDirPath()
                             + QStringLiteral("/unins000.exe"));
}

} // namespace

CudaRuntimeController::CudaRuntimeController(QObject *parent) : QObject(parent)
{
#if defined(Q_OS_WIN) && defined(WITH_CUDA)
    // Only meaningful for a CUDA-capable app actually installed on Windows. A CPU-only
    // build has no CUDA EP to accelerate, and a dev/build-tree run has no installer.
    m_supported = isInstalledBuild();
#else
    m_supported = false;
#endif
    refresh();
}

void CudaRuntimeController::refresh()
{
    bool gpu = false;
    bool runtime = false;

#if defined(Q_OS_WIN)
    // nvcuda.dll is installed by the NVIDIA driver; if it loads, an NVIDIA GPU + driver
    // are present. (Same probe KokoroTTSEngine/LocalLlmEngine use before the CUDA EP.)
    QLibrary nvcuda(QStringLiteral("nvcuda.dll"));
    gpu = nvcuda.load();
    if (gpu)
        nvcuda.unload();

    runtime = QFileInfo::exists(QCoreApplication::applicationDirPath()
                                + QStringLiteral("/") + QString::fromLatin1(kRequiredCudnnDll));
#endif

    if (gpu == m_gpuPresent && runtime == m_runtimeInstalled)
        return;
    m_gpuPresent       = gpu;
    m_runtimeInstalled = runtime;
    emit stateChanged();
    if (shouldOffer())
        ppInfo() << "[CUDA] NVIDIA GPU present, runtime not installed — offering GPU acceleration";
}

void CudaRuntimeController::openDownloadPage()
{
    if (!m_supported)
        return;
    QDesktopServices::openUrl(QUrl(QString::fromLatin1(kReleasesUrl)));
}
