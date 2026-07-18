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

// Runtime discovery of the Teledyne FLIR Spinnaker SDK (Windows only).
//
// The Spinnaker SDK must NOT be redistributed with PinPoint (its EULA forbids
// bundling the SDK in an installer). Instead the app is built with the Spinnaker
// imports *delay-loaded* (/DELAYLOAD:Spinnaker_v140.dll — see CMakeLists.txt) and
// locates a user-installed SDK at runtime. runtimeAvailable() probes for the SDK,
// preloads its core DLL so the delay-loaded imports (and their dependency graph)
// resolve from the SDK folder, and caches the result.
//
// Every Spinnaker entry point MUST call runtimeAvailable() first and touch no
// Spinnaker symbol when it returns false — otherwise the first use would trigger a
// delay-load of a missing DLL and raise a fatal structured (SEH) exception that the
// surrounding C++ try/catch cannot intercept.

namespace pinpoint::spinnaker {

// True if the Spinnaker SDK is installed and its DLLs have been made loadable.
// The probe runs once; the result is cached. Safe to call from any thread.
bool runtimeAvailable();

}  // namespace pinpoint::spinnaker
