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

// The one mapping from a GenICam pixel-format name (the SFNC symbolic name a
// machine-vision camera reports, e.g. "BayerRG8") to the capability's
// PixelFormat. Used by the device enumeration (video_input_factory.cpp) and by
// each GenICam backend's queryCapabilities (VideoInputAravis.cpp,
// VideoInputSpinnaker.cpp). Not the swing.json name: that is
// pinpoint::PixelFormat's, in src/Buffer/pixel_format_names.h.
//
// The two backends recognise different sets, and keep doing so: the Aravis
// side has never known the 16-bit formats (BayerRG16, BayerGB16, Mono16) or
// the "RGB8" alias, so they are Unknown there. A name no row matches is
// Unknown with 0 bits per pixel. The name is kept as the native key either way.
// Pinned per site by genicam_pixel_format_test (src/Gui/tests).

#include "camera_capabilities.h"

#include <QLatin1String>
#include <QString>

// Which backend's set of names applies.
enum class GenicamDialect { Aravis, Spinnaker };

struct GenicamPixelFormatRow {
    const char   *name;
    PixelEncoding encoding;
    int           bitsPerPixel;
    bool          knownToAravis;   // false: Spinnaker only
};

inline constexpr GenicamPixelFormatRow kGenicamPixelFormats[] = {
    { "BayerRG8",   PixelEncoding::BayerRG8,  8,  true  },
    { "BayerGB8",   PixelEncoding::BayerGB8,  8,  true  },
    { "BayerGR8",   PixelEncoding::BayerGR8,  8,  true  },
    { "BayerBG8",   PixelEncoding::BayerBG8,  8,  true  },
    { "BayerRG16",  PixelEncoding::BayerRG16, 16, false },
    { "BayerGB16",  PixelEncoding::BayerGB16, 16, false },
    { "Mono8",      PixelEncoding::Mono8,     8,  true  },
    { "Mono10",     PixelEncoding::Mono10,    10, true  },
    { "Mono12",     PixelEncoding::Mono12,    12, true  },
    { "Mono16",     PixelEncoding::Mono16,    16, false },
    { "RGB8Packed", PixelEncoding::RGB8,      24, true  },
    { "RGB8",       PixelEncoding::RGB8,      24, false },
    { "BGR8",       PixelEncoding::BGR8,      24, true  },
};

inline ::PixelFormat genicamPixelFormat(const QString &name, GenicamDialect dialect)
{
    ::PixelFormat pf;
    pf.nativeKey = name;
    for (const GenicamPixelFormatRow &row : kGenicamPixelFormats) {
        if (dialect == GenicamDialect::Aravis && !row.knownToAravis)
            continue;
        if (name == QLatin1String(row.name)) {
            pf.encoding     = row.encoding;
            pf.bitsPerPixel = row.bitsPerPixel;
            return pf;
        }
    }
    pf.encoding = PixelEncoding::Unknown;
    return pf;
}
