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

#include "pose_backend.h"

#include <QtGlobal>
#include <QFile>

#if defined(Q_OS_WIN)
#  include <QLibrary>
#endif

namespace pinpoint::pose {

QString bestAvailableAcceleratedBackend()
{
    // Same priority order the estimators try: CoreML → CUDA → DirectML → CPU.
    // Each branch is present only when its EP was compiled in (the WITH_* defines
    // are set by CMake when the matching GPU ORT binary is downloaded).

#ifdef WITH_COREML
    // WITH_COREML is defined only on Apple-Silicon macOS builds, where the CoreML
    // EP is always available — presence is guaranteed by the platform guard.
    return QStringLiteral("CoreML");
#endif

#ifdef WITH_CUDA
    {
#  if defined(Q_OS_LINUX)
        // ORT's only GPU EP on Linux is CUDA; the NVIDIA driver publishes this
        // procfs node, so its presence means an NVIDIA GPU + driver are installed.
        const bool hasNv = QFile::exists(QStringLiteral("/proc/driver/nvidia/version"));
#  elif defined(Q_OS_WIN)
        // nvcuda.dll is installed by the NVIDIA driver; if it loads, a GPU is present
        // (same probe CudaRuntimeController / the estimator cascade use).
        QLibrary nvcuda(QStringLiteral("nvcuda.dll"));
        const bool hasNv = nvcuda.load();
        if (hasNv)
            nvcuda.unload();
#  else
        const bool hasNv = false;
#  endif
        if (hasNv)
            return QStringLiteral("CUDA");
    }
#endif

#ifdef WITH_DIRECTML
    // DirectML runs on any Direct3D 12 adapter; when compiled in, treat it as
    // present (the EP itself falls back to CPU if no adapter is usable).
    return QStringLiteral("DirectML");
#endif

    return QString();   // no accelerated EP available — CPU only
}

} // namespace pinpoint::pose
