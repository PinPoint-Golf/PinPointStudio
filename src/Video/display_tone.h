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

// THE DISPLAY TONE CURVE (#183): what a golfer sees of a studio camera's video.
//
// A raw-Bayer camera's frames are linear sensor values, and the recorder writes them to the MP4 with no tone
// curve, so on screen and in any other player they look dark. The curve is out = in^kDisplayToneExponent on
// each of R, G and B (in and out from 0 to 1): black and white stay put, shadows and mid-tones rise.
// displayToneLut() is the curve as a 256-entry table on 8-bit values: lut[v] == round(255 * (v / 255)^exponent).
//
// It is applied only where a golfer looks, never where the analysis reads:
//   - on screen, by the GPU: viewgain.frag in PpCameraFrame's pass, with the exponent handed to QML as
//     CameraInstance::displayTone (live) and ReplayStreamInfo::displayTone (Replay, Analyse);
//   - in a clip or thumbnail the golfer takes away in an export zip (Stores/export_tone.h), through
//     displayToneLut(). The table IS the curve the shader draws, from the same constant (display_tone_test).
// The stored MP4s, the raw sidecars, thumb.jpg and everything the analysis decodes stay linear; display_tone_test
// scans which files include this one.
//
// ONE TEST-ONLY EXCEPTION: the recording loader (Stores/recording_loader.cpp) builds its table from
// displayToneLutFor() when a tool's run sets the decode.toneExponent tuning key (swinglab_run --tone-curve,
// #183's analysis A/B: does the analysis do better or worse on curved frames?). No app code sets that key; a
// write-back with it is refused; without it the loader reads the frames exactly as before. Nothing else in
// Analysis/, Pose/, Club/, Ball/ or the loader may include this file.
//
// WHICH STREAMS: those whose pixel format is raw Bayer (displayToneFor), as streams[].source.pixelFormat
// records it (live: the format the camera instance stamped into the buffer, from which the recorder writes it).
// A phone's or a webcam's video (NV12, YUYV, BGRA, a phone clip with no source format) is already
// tone-mapped by the device and would wash out, so it gets 1.0. Mono8 gets 1.0 too: no rig has a mono
// camera, and the curve was fitted on colour cameras only.
//
// ⚠ THE DOUBLE-CURVE TRAP: "raw Bayer source" means "linear file" only while the recorder bakes no curve in.
// A change that curves the recorded file must write a marker in the stream's `processing` block and make
// displayToneFor read it, or the picture will be curved twice.
//
// Not a setting and not a Theme token: it must not vary by theme, the C++ that decides per stream must own
// it, and the refactor freeze bars a new setting. Rolling it back is this one constant set to 1.0.

#include "../Buffer/pixel_format_names.h"
#include "../Buffer/types.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace pinpoint {

// The exponent the Design Lead ruled on the step-0 sheets (25 Nov 2026): most of Swing Catalyst's lift
// (whose measured fit was 0.77), with less noise lifted from the dark wall and mat.
inline constexpr double kDisplayToneExponent = 0.80;

// True for every raw-Bayer format: the camera's linear values, demosaiced by us and never tone-mapped.
constexpr bool isRawBayerFormat(PixelFormat f) noexcept
{
    switch (f) {
    case PixelFormat::BayerRG8:  case PixelFormat::BayerRG12: case PixelFormat::BayerRG16:
    case PixelFormat::BayerBG8:  case PixelFormat::BayerGR8:  case PixelFormat::BayerGB8:
    case PixelFormat::BayerGB16:
        return true;
    default:
        return false;
    }
}

// The exponent a stream of this format is shown with: kDisplayToneExponent for raw Bayer, 1.0 (no curve)
// for everything else.
constexpr double displayToneFor(PixelFormat f) noexcept
{
    return isRawBayerFormat(f) ? kDisplayToneExponent : 1.0;
}

// The same, from the swing document's spelling (streams[].source.pixelFormat, pixel_format_names.h). An
// empty or unknown name is not Bayer: 1.0.
inline double displayToneFor(std::string_view pixelFormatName) noexcept
{
    return displayToneFor(pixelFormatFromName(pixelFormatName));
}

// The curve as a 256-entry table on 8-bit values: lut[v] = round(255 * (v / 255)^exponent).
inline std::array<std::uint8_t, 256> displayToneLutFor(double exponent)
{
    std::array<std::uint8_t, 256> t{};
    for (int v = 0; v < 256; ++v) {
        const double out = 255.0 * std::pow(v / 255.0, exponent);
        t[static_cast<std::size_t>(v)] = static_cast<std::uint8_t>(std::lround(std::fmin(std::fmax(out, 0.0), 255.0)));
    }
    return t;
}

// The table of the one constant, built once.
inline const std::array<std::uint8_t, 256> &displayToneLut()
{
    static const std::array<std::uint8_t, 256> t = displayToneLutFor(kDisplayToneExponent);
    return t;
}

} // namespace pinpoint
