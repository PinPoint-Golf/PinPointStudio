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

#include <QByteArray>
#include <QMetaType>

// Lightweight container for a single raw (un-demosaiced) video frame.
// Used to pass Bayer data from the capture loop to the GPU display item
// and the CPU preprocessor without an intermediate demosaic step.
struct RawVideoFrame {
    enum class BayerPattern { RG = 0, BG = 1, GR = 2, GB = 3 };

    QByteArray   data;              // packed row-major bytes (stride == width)
    int          width   = 0;
    int          height  = 0;
    BayerPattern pattern = BayerPattern::RG;

    bool isNull() const { return data.isEmpty() || width <= 0 || height <= 0; }

    // OpenCV cvtColor code for Bayer→BGR conversion.
    int opencvBayerToBgrCode() const;
};
Q_DECLARE_METATYPE(RawVideoFrame)
