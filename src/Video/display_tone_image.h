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

// THE DISPLAY TONE CURVE ON A PICTURE (#183): the one way a still picture is put through the curve's table
// (display_tone.h), on the CPU. Used where a golfer sees a picture the GPU pass doesn't draw: the report's
// stills and the Charts pictures (Gui/report/report_stills.cpp), the Markup Lab's frames
// (Gui/markup/markup_controller.cpp), and thumb.jpg in an export zip (Stores/export_tone.cpp).
//
// Every channel goes through the one table, as the shader does it; the picture comes back as RGB888 (no alpha:
// a video frame or a JPEG is opaque). It is a display step: never call it on frames the analysis reads.
// display_tone_test scans which files include this header, as it does display_tone.h.

#include "display_tone.h"

#include <QImage>

#include <array>
#include <cstdint>

namespace pinpoint {

// img through lut, every channel: RGB888 out. A null image stays null.
inline QImage withToneTable(const QImage &img, const std::array<std::uint8_t, 256> &lut)
{
    if (img.isNull())
        return img;
    QImage out = img.convertToFormat(QImage::Format_RGB888);
    for (int y = 0; y < out.height(); ++y) {
        uchar *row = out.scanLine(y);
        for (int x = 0; x < out.width() * 3; ++x)
            row[x] = lut[row[x]];
    }
    return out;
}

// img as a golfer is shown it, for a stream whose exponent is `exponent` (displayToneFor). At 1.0 (a phone's or a
// webcam's picture, already tone-mapped) it comes back as it is, in its own format.
inline QImage withDisplayTone(const QImage &img, double exponent)
{
    if (exponent == 1.0)
        return img;
    return withToneTable(img, exponent == kDisplayToneExponent ? displayToneLut() : displayToneLutFor(exponent));
}

} // namespace pinpoint
