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

#include "frame_crop.h"

#include <QVarLengthArray>
#include <QVideoFrameFormat>
#include <atomic>
#include <cstring>

#include "pp_debug.h"

namespace pp_crop {

bool cropIsActive(const QRectF &norm)
{
    if (norm.width() <= 0.0 || norm.height() <= 0.0)
        return false;
    // The unit rect (or anything covering it) selects the whole frame.
    if (norm.x() <= 0.0 && norm.y() <= 0.0
        && norm.width() >= 1.0 && norm.height() >= 1.0)
        return false;
    return true;
}

QRect snapCropRect(const QRectF &norm, int w, int h, int xAlign, int yAlign)
{
    if (w <= 0 || h <= 0)
        return {};
    xAlign = qMax(1, xAlign);
    yAlign = qMax(1, yAlign);

    auto snapDown = [](int v, int align) { return (v / align) * align; };

    int rw = snapDown(qBound(xAlign, qRound(norm.width()  * w), w), xAlign);
    int rh = snapDown(qBound(yAlign, qRound(norm.height() * h), h), yAlign);
    int rx = snapDown(qBound(0, qRound(norm.x() * w), w - rw), xAlign);
    int ry = snapDown(qBound(0, qRound(norm.y() * h), h - rh), yAlign);
    return QRect(rx, ry, rw, rh);
}

QVideoFrame cropVideoFrame(const QVideoFrame &src, const QRectF &norm)
{
    if (!src.isValid() || !cropIsActive(norm))
        return src;

    QVideoFrame in(src); // ref-counted handle; map() needs a mutable object
    if (!in.map(QVideoFrame::ReadOnly))
        return src;

    // Per-plane copy spec: bytes per sample group in x, and subsampling
    // shifts. For NV12/NV21 plane 1 holds interleaved UV at half vertical
    // resolution — a horizontal offset of x pixels is x BYTES into the UV row
    // (one UV pair per 2 pixels x 2 bytes), encoded as bytesPerX=1, xShift=0.
    struct Plane { int bytesPerX; int xShift; int yShift; };
    QVarLengthArray<Plane, 3> planes;
    int xAlign = 2, yAlign = 2;

    const QVideoFrameFormat::PixelFormat fmt = in.surfaceFormat().pixelFormat();
    switch (fmt) {
    case QVideoFrameFormat::Format_Y8:
    case QVideoFrameFormat::Format_Y16:
        planes = {{fmt == QVideoFrameFormat::Format_Y16 ? 2 : 1, 0, 0}};
        xAlign = yAlign = 1;
        break;
    case QVideoFrameFormat::Format_NV12:
    case QVideoFrameFormat::Format_NV21:
        planes = {{1, 0, 0}, {1, 0, 1}};
        break;
    case QVideoFrameFormat::Format_YUV420P:
        planes = {{1, 0, 0}, {1, 1, 1}, {1, 1, 1}};
        break;
    case QVideoFrameFormat::Format_YUYV:
    case QVideoFrameFormat::Format_UYVY:
        planes = {{2, 0, 0}}; // 2 bytes/pixel packed; x must stay even
        yAlign = 1;
        break;
    case QVideoFrameFormat::Format_ARGB8888:
    case QVideoFrameFormat::Format_ARGB8888_Premultiplied:
    case QVideoFrameFormat::Format_XRGB8888:
    case QVideoFrameFormat::Format_BGRA8888:
    case QVideoFrameFormat::Format_BGRA8888_Premultiplied:
    case QVideoFrameFormat::Format_BGRX8888:
    case QVideoFrameFormat::Format_ABGR8888:
    case QVideoFrameFormat::Format_XBGR8888:
    case QVideoFrameFormat::Format_RGBA8888:
    case QVideoFrameFormat::Format_RGBX8888:
        planes = {{4, 0, 0}};
        xAlign = yAlign = 1;
        break;
    default: {
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true))
            ppWarn() << "[frame_crop] unsupported pixel format" << int(fmt)
                     << "- frames pass through uncropped";
        in.unmap();
        return src;
    }
    }

    const QRect r = snapCropRect(norm, in.width(), in.height(), xAlign, yAlign);
    if (r.width() <= 0 || r.height() <= 0
        || r.size() == QSize(in.width(), in.height())) {
        in.unmap();
        return src;
    }

    QVideoFrameFormat outFmt(r.size(), fmt);
    outFmt.setColorSpace(in.surfaceFormat().colorSpace());
    outFmt.setColorRange(in.surfaceFormat().colorRange());
    outFmt.setStreamFrameRate(in.surfaceFormat().streamFrameRate());
    QVideoFrame out(outFmt);
    if (!out.map(QVideoFrame::WriteOnly)) {
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true))
            ppWarn() << "[frame_crop] failed to map output frame"
                     << r.size() << "- frames pass through uncropped";
        in.unmap();
        return src;
    }

    for (int p = 0; p < in.planeCount() && p < planes.size(); ++p) {
        const Plane &pl = planes[p];
        const int rows     = r.height() >> pl.yShift;
        const int rowBytes = (r.width() >> pl.xShift) * pl.bytesPerX;
        const int srcXOff  = (r.x() >> pl.xShift) * pl.bytesPerX;
        const int srcYOff  = r.y() >> pl.yShift;
        const uchar *s = in.bits(p) + srcYOff * in.bytesPerLine(p) + srcXOff;
        uchar       *d = out.bits(p);
        for (int y = 0; y < rows; ++y)
            std::memcpy(d + y * out.bytesPerLine(p),
                        s + y * in.bytesPerLine(p),
                        rowBytes);
    }

    out.unmap();
    in.unmap();
    out.setStartTime(src.startTime());
    out.setEndTime(src.endTime());
    return out;
}

RawVideoFrame cropRawFrame(const RawVideoFrame &src, const QRectF &norm)
{
    if (src.isNull() || !cropIsActive(norm))
        return src;

    // Even x/y offsets preserve the Bayer CFA phase, so src.pattern carries
    // over unchanged. Rows are packed (stride == width per the contract).
    const QRect r = snapCropRect(norm, src.width, src.height, 2, 2);
    if (r.width() <= 0 || r.height() <= 0
        || r.size() == QSize(src.width, src.height))
        return src;

    RawVideoFrame out;
    out.width   = r.width();
    out.height  = r.height();
    out.pattern = src.pattern;
    out.data.resize(r.width() * r.height());

    const char *s = src.data.constData() + r.y() * src.width + r.x();
    char       *d = out.data.data();
    for (int y = 0; y < r.height(); ++y)
        std::memcpy(d + y * r.width(), s + y * src.width, r.width());
    return out;
}

} // namespace pp_crop
