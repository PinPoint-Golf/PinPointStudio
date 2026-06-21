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

#pragma once

#include <QString>

// PlatformTarget — the (OS, CPU) pair an update backend builds release-asset and
// feed selection from. CPU architecture is **data**, not a subclass: every OS
// backend is identical across arches except for one filename token (and, on
// macOS/Windows, the feed URL), so subtyping per arch would be a combinatorial
// near-duplicate explosion. The backend reads its own compile-time target via
// current() and asks assetArchToken() for the string the release artefacts use.
//
// This kills the hardcoded "x86_64" that used to live in the Linux feed parsing
// (UpdateController::versionFromAssetName), letting an aarch64 AppImage be offered
// by the same code path once one is published. The asset never crosses arches:
// the running binary only ever offers a same-arch build (no cross-arch deltas —
// docs/design/linux_update.md).
namespace pp::update {

enum class Os   { Linux, Windows, MacOS, Unknown };
enum class Arch { X86_64, Arm64,   Unknown };

struct PlatformTarget
{
    Os   os   = Os::Unknown;
    Arch arch = Arch::Unknown;

    // The arch token used in release-asset filenames / feeds for this target.
    //   Linux:   x86_64 | aarch64        (AppImage convention)
    //   macOS:   x86_64 | arm64          (DMG convention, docs/design/macos_update.md)
    //   Windows: x64    | arm64
    // Empty for an unknown arch — the backend treats "no token" as "offer nothing"
    // rather than guessing a wrong-arch binary.
    QString assetArchToken() const;

    // This binary's own target, resolved at COMPILE time from Q_OS_* / Q_PROCESSOR_*.
    static PlatformTarget current();
};

} // namespace pp::update
