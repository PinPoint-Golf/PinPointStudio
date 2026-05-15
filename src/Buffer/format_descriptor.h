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
#include <array>
#include <string>
#include <variant>
#include <cstddef>

namespace pinpoint {

struct CameraFormat {
    PixelFormat pixel_format{PixelFormat::Unknown};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t fps_numerator{60};
    uint32_t fps_denominator{1};
    uint32_t max_payload_bytes{0};      // sizes ring slots
    uint32_t typical_payload_bytes{0};  // statistics only
    std::array<uint32_t, 4> plane_strides{};
};

struct ImuFormat {
    DeviceKind device{DeviceKind::IMU_Custom};
    uint32_t sample_rate_hz{0};
    uint32_t packet_bytes{0};       // fixed size per IMU packet
    std::string packet_schema;      // opaque parser identifier e.g. "wt9011_v2"
};

struct FormatDescriptor {
    DeviceKind device{DeviceKind::Camera_UVC};
    std::string device_serial;
    std::variant<CameraFormat, ImuFormat> format;
};

inline size_t payloadBytes(const FormatDescriptor& fd) {
    return std::visit([](const auto& f) -> size_t {
        using T = std::decay_t<decltype(f)>;
        if constexpr (std::is_same_v<T, CameraFormat>) {
            return f.max_payload_bytes;
        } else {
            return f.packet_bytes;
        }
    }, fd.format);
}

} // namespace pinpoint
