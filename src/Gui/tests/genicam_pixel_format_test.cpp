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

// The GenICam pixel-format names a camera reports, mapped to the capability's PixelFormat
// (src/Video/genicam_pixel_format.h, platform refactor SD-5). Four sites used to carry their own
// copy of this mapping: the device enumeration's Aravis and Spinnaker branches
// (video_input_factory.cpp) and each backend's queryCapabilities (VideoInputAravis.cpp,
// VideoInputSpinnaker.cpp). This test pins, for every site, what that site's copy produced, so
// the one mapping changes nothing:
//
//   G1 every name any copy knew, at every site: its encoding and bits per pixel
//   G2 the Aravis sites never knew the 16-bit formats or the "RGB8" alias: Unknown, 0 bits
//   G3 an unknown, empty, wrong-case or padded name is Unknown with 0 bits, at every site
//   G4 the name is kept as the native key, byte for byte, known or not

#include "camera_capabilities.h"
#include "genicam_pixel_format.h"

#include <QString>

#include <cstdio>

static int g_fail = 0;
static int g_run  = 0;

static void check(const QString &label, bool ok)
{
    ++g_run;
    if (!ok) {
        std::printf("  [FAIL] %s\n", qPrintable(label));
        ++g_fail;
    }
}

// The four sites the copies lived in.
enum class Site { FactoryAravis, FactorySpinnaker, VideoInputAravis, VideoInputSpinnaker };

static const char *siteName(Site s)
{
    switch (s) {
    case Site::FactoryAravis:       return "video_input_factory (Aravis)";
    case Site::FactorySpinnaker:    return "video_input_factory (Spinnaker)";
    case Site::VideoInputAravis:    return "VideoInputAravis";
    case Site::VideoInputSpinnaker: return "VideoInputSpinnaker";
    }
    return "?";
}

static bool isAravis(Site s) { return s == Site::FactoryAravis || s == Site::VideoInputAravis; }

// What each site maps a name to.
static PixelFormat mapAt(Site s, const QString &name)
{
    return genicamPixelFormat(name, isAravis(s) ? GenicamDialect::Aravis : GenicamDialect::Spinnaker);
}

// The pin: what each copy produced on origin/main e1aeee20, read off its ladder.
struct Expect {
    const char   *name;
    PixelEncoding aravis;     int aravisBits;
    PixelEncoding spinnaker;  int spinnakerBits;
};

using E = PixelEncoding;
static const Expect kExpect[] = {
    // G1: known to both
    { "BayerRG8",     E::BayerRG8,  8,  E::BayerRG8,  8  },
    { "BayerGB8",     E::BayerGB8,  8,  E::BayerGB8,  8  },
    { "BayerGR8",     E::BayerGR8,  8,  E::BayerGR8,  8  },
    { "BayerBG8",     E::BayerBG8,  8,  E::BayerBG8,  8  },
    { "Mono8",        E::Mono8,     8,  E::Mono8,     8  },
    { "Mono10",       E::Mono10,    10, E::Mono10,    10 },
    { "Mono12",       E::Mono12,    12, E::Mono12,    12 },
    { "RGB8Packed",   E::RGB8,      24, E::RGB8,      24 },
    { "BGR8",         E::BGR8,      24, E::BGR8,      24 },
    // G2: known to the Spinnaker copies only
    { "BayerRG16",    E::Unknown,   0,  E::BayerRG16, 16 },
    { "BayerGB16",    E::Unknown,   0,  E::BayerGB16, 16 },
    { "Mono16",       E::Unknown,   0,  E::Mono16,    16 },
    { "RGB8",         E::Unknown,   0,  E::RGB8,      24 },
    // G3: real GenICam names no copy knew
    { "BayerGR16",    E::Unknown,   0,  E::Unknown,   0  },
    { "BayerBG16",    E::Unknown,   0,  E::Unknown,   0  },
    { "BayerRG12",    E::Unknown,   0,  E::Unknown,   0  },
    { "BayerRG10",    E::Unknown,   0,  E::Unknown,   0  },
    { "Mono12Packed", E::Unknown,   0,  E::Unknown,   0  },
    { "Mono14",       E::Unknown,   0,  E::Unknown,   0  },
    { "BGR8Packed",   E::Unknown,   0,  E::Unknown,   0  },
    { "BGRa8",        E::Unknown,   0,  E::Unknown,   0  },
    { "RGBa8",        E::Unknown,   0,  E::Unknown,   0  },
    { "YUV422Packed", E::Unknown,   0,  E::Unknown,   0  },
    { "YCbCr422_8",   E::Unknown,   0,  E::Unknown,   0  },
    // G3: names a table lookup could get wrong
    { "",             E::Unknown,   0,  E::Unknown,   0  },
    { "Unknown",      E::Unknown,   0,  E::Unknown,   0  },
    { "bayerrg8",     E::Unknown,   0,  E::Unknown,   0  },
    { "BAYERRG8",     E::Unknown,   0,  E::Unknown,   0  },
    { "mono8",        E::Unknown,   0,  E::Unknown,   0  },
    { "rgb8",         E::Unknown,   0,  E::Unknown,   0  },
    { " BayerRG8",    E::Unknown,   0,  E::Unknown,   0  },
    { "BayerRG8 ",    E::Unknown,   0,  E::Unknown,   0  },
    { "BayerRG",      E::Unknown,   0,  E::Unknown,   0  },
    { "BayerRG80",    E::Unknown,   0,  E::Unknown,   0  },
    { "RGB8Packed2",  E::Unknown,   0,  E::Unknown,   0  },
    { "Mono1",        E::Unknown,   0,  E::Unknown,   0  },
};

int main()
{
    std::printf("genicam_pixel_format_test — the one GenICam pixel-format mapping (genicam_pixel_format.h)\n");
    const Site sites[] = { Site::FactoryAravis, Site::FactorySpinnaker,
                           Site::VideoInputAravis, Site::VideoInputSpinnaker };
    for (Site s : sites) {
        for (const Expect &x : kExpect) {
            const QString name = QString::fromLatin1(x.name);
            const PixelFormat pf = mapAt(s, name);
            const PixelEncoding enc = isAravis(s) ? x.aravis : x.spinnaker;
            const int bits          = isAravis(s) ? x.aravisBits : x.spinnakerBits;
            const QString where = QStringLiteral("%1 \"%2\"").arg(QLatin1String(siteName(s)), name);
            check(where + QStringLiteral(": encoding"), pf.encoding == enc);
            check(where + QStringLiteral(": bits per pixel"), pf.bitsPerPixel == bits);
            check(where + QStringLiteral(": native key kept (G4)"), pf.nativeKey == name);
        }
        // G4: a null name stays a null-or-empty key and is Unknown.
        const PixelFormat nul = mapAt(s, QString());
        check(QStringLiteral("%1 null name").arg(QLatin1String(siteName(s))),
              nul.encoding == E::Unknown && nul.bitsPerPixel == 0 && nul.nativeKey.isEmpty());
    }
    std::printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
