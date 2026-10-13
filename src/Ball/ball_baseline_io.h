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

// Header-only loader for the persisted live ball baseline blob (the row-major
// float32 empty-mat baseline B a studio session learned, written per swing by
// the exporter). Header-only + standalone so it is unit-testable without the
// SwingWindow / BallRunner machinery (project convention: header-only detector
// math + a tests/ gate, e.g. src/IMU/impact_detector.h). Shape mirrors the parity
// fixtures' loadF32 (src/Pose/tests/ball_temporal_parity_test.cpp).

#ifdef HAVE_OPENCV

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QString>

#include <cstring>
#include <opencv2/core.hpp>

// Load a raw row-major float32 blob into a freshly-allocated CV_32F Mat of shape
// (h, w). The file MUST be exactly w*h*sizeof(float) bytes — any short/long read
// is a hard reject (BallRunner then falls back to self-seeding). Returns false on
// degenerate dims, open failure, or a size mismatch; `out` is left untouched on
// failure.
inline bool loadBallBaselineBlob(const QString &path, int w, int h, cv::Mat &out)
{
    if (w <= 0 || h <= 0)
        return false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray raw = f.readAll();
    if (raw.size() != qint64(w) * qint64(h) * qint64(sizeof(float)))
        return false;
    cv::Mat m(h, w, CV_32F);                                  // contiguous by construction
    std::memcpy(m.data, raw.constData(), size_t(raw.size()));
    out = m;                                                  // refcounted handoff
    return true;
}

#endif // HAVE_OPENCV
