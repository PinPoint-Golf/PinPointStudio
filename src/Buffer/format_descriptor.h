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
#include <string_view>
#include <variant>
#include <cstddef>

namespace pinpoint {

// Provenance of a stream's exposure value. Lets a consumer weight it:
//   Measured      — read from the captured frame, ExposureAuto=Off (stable manual)
//   MeasuredAuto  — read from the captured frame, auto-exposure active (frame-varying)
//   Derived       — computed as 1/(2*fps) (no per-frame exposure available; webcams)
//   Unknown       — nothing known; exposure_us == 0
enum class ExposureSource : uint8_t { Unknown = 0, Measured = 1, MeasuredAuto = 2, Derived = 3 };

// swing.json "exposureSource" provenance string. Measured and MeasuredAuto both
// map to "measured" — the separate "exposureAuto" bool carries the auto/manual
// bit — while Derived maps to "derived". Writer: swing exporter; reader: reanalyzer.
constexpr const char* exposureSourceName(ExposureSource s) noexcept {
    switch (s) {
    case ExposureSource::Measured:
    case ExposureSource::MeasuredAuto: return "measured";
    case ExposureSource::Derived:      return "derived";
    case ExposureSource::Unknown:      return "unknown";
    }
    return "unknown";
}

// Reconstruct ExposureSource from the swing.json provenance string + auto flag.
inline ExposureSource exposureSourceFromName(std::string_view name, bool exposureAuto) noexcept {
    if (name == "measured")
        return exposureAuto ? ExposureSource::MeasuredAuto : ExposureSource::Measured;
    if (name == "derived")
        return ExposureSource::Derived;
    return ExposureSource::Unknown;
}

struct CameraFormat {
    PixelFormat pixel_format{PixelFormat::Unknown};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t fps_numerator{60};
    uint32_t fps_denominator{1};
    uint32_t max_payload_bytes{0};      // sizes ring slots
    uint32_t typical_payload_bytes{0};  // statistics only
    std::array<uint32_t, 4> plane_strides{};
    double         exposure_us{0.0};    // per-stream exposure, microseconds; 0 = unknown
    ExposureSource exposure_source{ExposureSource::Unknown};
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
