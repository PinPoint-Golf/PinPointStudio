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

#include "types.h"
#include "format_descriptor.h"
#include <chrono>
#include <optional>
#include <string>
#include <cstddef>
#include <cstdint>

namespace pinpoint {

enum class SyncSource {
    SoftwareTimestamp,  // v1 default
    HardwarePts,        // placeholder
    HardwareTrigger,    // placeholder
};

struct SourceDescriptor {
    SourceId id{kInvalidSourceId};
    std::string name;
    std::string identifier;  // device serial number or opaque device id
    FormatDescriptor format;
    std::chrono::milliseconds window_duration{5000};

    SyncSource sync_source{SyncSource::SoftwareTimestamp};
    std::optional<std::string> sync_group;

    std::chrono::microseconds expected_interarrival_us{0};

    // Bytes per slot: max_payload_bytes for cameras, packet_bytes aligned to 8 for IMUs.
    size_t computeSlotBytes() const;

    // Slot count: next power of 2 >= ceil(rate * window_seconds).
    size_t computeSlotCount() const;
};

} // namespace pinpoint
