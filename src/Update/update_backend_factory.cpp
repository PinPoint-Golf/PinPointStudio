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

#include "update_backend_factory.h"

#include <QtGlobal>

#include "inert_update_backend.h"

// The ONLY platform #ifdef in the update subsystem (besides PlatformTarget::current()
// reading the compile-time arch). Every other file is platform-agnostic.
#if defined(Q_OS_LINUX)
#  include "linux_appimage_backend.h"
#elif defined(Q_OS_WIN) && defined(HAVE_WINSPARKLE)
#  include "win_sparkle_backend.h"
#elif defined(Q_OS_MACOS) && defined(HAVE_SPARKLE)
#  include "mac_sparkle_backend.h"
#endif

UpdateBackend *makeUpdateBackend(AppSettings *settings,
                                 SessionController *session,
                                 QObject *parent)
{
#if defined(Q_OS_LINUX)
    return new LinuxAppImageBackend(settings, session, parent);
#elif defined(Q_OS_WIN) && defined(HAVE_WINSPARKLE)
    return new WinSparkleBackend(settings, session, parent);
#elif defined(Q_OS_MACOS) && defined(HAVE_SPARKLE)
    return new MacSparkleBackend(settings, session, parent);
#else
    // macOS without Sparkle / Windows without WinSparkle → inert.
    Q_UNUSED(settings);
    Q_UNUSED(session);
    return new InertUpdateBackend(parent);
#endif
}
