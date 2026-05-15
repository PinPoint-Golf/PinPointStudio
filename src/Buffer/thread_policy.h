/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once
#include "platform.h"

namespace pinpoint {

enum class ThreadRole {
    Capture,    // highest priority — real-time capture callbacks
    Merger,     // above normal — timeline merger loop
    Consumer,   // normal — analysis, recording, UI
};

class ThreadPolicy {
public:
    // Apply the appropriate priority for this role on the calling thread.
    // Best-effort: never throws, never blocks.
    // Returns true if priority was successfully elevated above default.
    // Returns false if running at default (e.g. missing CAP_SYS_NICE on Linux).
    static bool apply(ThreadRole role) noexcept;

    // Pin the calling thread to a specific CPU core.
    // No-op on Apple and Android (returns false).
    // Returns true if pinning succeeded.
    static bool pinToCore(int core_id) noexcept;

    // Human-readable description of the result of the last apply() call,
    // suitable for a log line. Thread-safe (returns a string literal —
    // no allocation).
    static const char* lastApplyDescription() noexcept;
};

} // namespace pinpoint
