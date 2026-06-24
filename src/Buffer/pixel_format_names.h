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
#include <string_view>

namespace pinpoint {

// Canonical PixelFormat ↔ name mapping — the single source of truth for the
// swing.json "pixelFormat" string. Writer: the swing exporter; reader: the swing
// reanalyzer / SwingDiskLoader. The inverse is DERIVED from the name table, so the
// two directions can never drift: adding a PixelFormat only requires extending the
// switch below. Qt-free so it stays in the standalone Buffer library.
constexpr const char* pixelFormatName(PixelFormat fmt) noexcept
{
    switch (fmt) {
    case PixelFormat::Mono8:        return "Mono8";
    case PixelFormat::Mono12:       return "Mono12";
    case PixelFormat::Mono12Packed: return "Mono12Packed";
    case PixelFormat::Mono16:       return "Mono16";
    case PixelFormat::BayerRG8:     return "BayerRG8";
    case PixelFormat::BayerRG12:    return "BayerRG12";
    case PixelFormat::BayerRG16:    return "BayerRG16";
    case PixelFormat::BayerBG8:     return "BayerBG8";
    case PixelFormat::BayerGR8:     return "BayerGR8";
    case PixelFormat::BayerGB8:     return "BayerGB8";
    case PixelFormat::BayerGB16:    return "BayerGB16";
    case PixelFormat::YUV422:       return "YUV422";
    case PixelFormat::YUYV:         return "YUYV";
    case PixelFormat::UYVY:         return "UYVY";
    case PixelFormat::NV12:         return "NV12";
    case PixelFormat::YUV420P:      return "YUV420P";
    case PixelFormat::BGR24:        return "BGR24";
    case PixelFormat::RGB24:        return "RGB24";
    case PixelFormat::BGRA32:       return "BGRA32";
    case PixelFormat::RGBA32:       return "RGBA32";
    case PixelFormat::MJPEG:        return "MJPEG";
    case PixelFormat::H264_NAL:     return "H264_NAL";
    case PixelFormat::Unknown:      return "Unknown";
    }
    return "Unknown";
}

// Inverse of pixelFormatName — returns Unknown for an unrecognised string.
inline PixelFormat pixelFormatFromName(std::string_view name) noexcept
{
    for (int i = 0; i <= static_cast<int>(PixelFormat::H264_NAL); ++i) {
        const auto fmt = static_cast<PixelFormat>(i);
        if (name == pixelFormatName(fmt))
            return fmt;
    }
    return PixelFormat::Unknown;
}

} // namespace pinpoint
