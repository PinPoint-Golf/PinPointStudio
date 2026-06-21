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

#include "platform_target.h"

namespace pp::update {

QString PlatformTarget::assetArchToken() const
{
    switch (arch) {
    case Arch::X86_64:
        // AppImage + DMG both use "x86_64"; Windows installers use "x64".
        return os == Os::Windows ? QStringLiteral("x64") : QStringLiteral("x86_64");
    case Arch::Arm64:
        // Linux AppImage convention is "aarch64"; macOS/Windows use "arm64".
        return os == Os::Linux ? QStringLiteral("aarch64") : QStringLiteral("arm64");
    case Arch::Unknown:
        break;
    }
    return QString();
}

PlatformTarget PlatformTarget::current()
{
    PlatformTarget t;

#if defined(Q_OS_LINUX)
    t.os = Os::Linux;
#elif defined(Q_OS_WIN)
    t.os = Os::Windows;
#elif defined(Q_OS_MACOS)
    t.os = Os::MacOS;
#else
    t.os = Os::Unknown;
#endif

#if defined(Q_PROCESSOR_X86_64)
    t.arch = Arch::X86_64;
#elif defined(Q_PROCESSOR_ARM_64)
    t.arch = Arch::Arm64;
#else
    t.arch = Arch::Unknown;
#endif

    return t;
}

} // namespace pp::update
