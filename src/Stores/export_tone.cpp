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

#include "export_tone.h"

#include "../Analysis/mp4_frame_reader.h"
#include "../Video/display_tone.h"
#include "../Video/display_tone_image.h"   // withToneTable: the one way a picture goes through the table

#include <QBuffer>
#include <QImage>
#include <QJsonArray>

#include <opencv2/core.hpp>

namespace pinpoint {

namespace {

constexpr int kPerspectiveFaceOn = 2;   // recording::Perspective::FaceOn (perspective.h)
constexpr int kPerspectiveImpact = 4;   // recording::Perspective::Impact
constexpr int kThumbJpegQuality  = 85;  // as swing_exporter.cpp writeThumbnail writes it

bool isRawBayerStream(const QJsonObject &s)
{
    const QString fmt = s.value(QStringLiteral("source")).toObject().value(QStringLiteral("pixelFormat")).toString();
    return displayToneFor(fmt.toStdString()) < 1.0;
}

} // namespace

ExportToneMembers exportToneMembers(const QJsonObject &doc)
{
    ExportToneMembers m;
    const QJsonArray streams = doc.value(QStringLiteral("streams")).toArray();
    int faceOn = -1, firstCamera = -1;
    for (int i = 0; i < streams.size(); ++i) {
        const QJsonObject s = streams.at(i).toObject();
        if (s.value(QStringLiteral("kind")).toString() != QLatin1String("video"))
            continue;
        const QString file = s.value(QStringLiteral("file")).toString();
        const int perspective = s.value(QStringLiteral("setup")).toObject().value(QStringLiteral("perspective")).toInt(-1);
        // A stream recorded from a camera carries its source format; a phone clip carries only a serial.
        if (s.value(QStringLiteral("source")).toObject().contains(QStringLiteral("pixelFormat"))) {
            if (firstCamera < 0) firstCamera = i;
            if (faceOn < 0 && perspective == kPerspectiveFaceOn) faceOn = i;
        }
        if (file.isEmpty() || !isRawBayerStream(s))
            continue;
        ExportToneMembers::Clip c;
        const double fps = s.value(QStringLiteral("playback")).toObject().value(QStringLiteral("fps")).toDouble(30.0);
        c.playbackFps = fps >= 1.0 ? int(fps + 0.5) : 30;
        c.impact      = perspective == kPerspectiveImpact;
        m.clips.insert(file, c);
    }
    const int thumbStream = faceOn >= 0 ? faceOn : firstCamera;
    m.thumbnail = thumbStream >= 0 && isRawBayerStream(streams.at(thumbStream).toObject());
    return m;
}

RetoneResult retoneClip(const std::string &srcMp4, IVideoEncoder &enc, VideoEncoderConfig cfg,
                        const std::array<std::uint8_t, 256> &lut)
{
    RetoneResult r;
    // The same reader the recording loader decodes with, at its defaults; it reads the stored file only.
    analysis::Mp4FrameDecoder dec(srcMp4, {});
    const cv::Mat table(1, 256, CV_8U, const_cast<std::uint8_t *>(lut.data()));
    cv::Mat frame, curved;
    for (int64_t i = 0;; ++i) {
        if (!dec.read(i, frame))
            break;
        if (i == 0) {
            cfg.width  = frame.cols;
            cfg.height = frame.rows;
            if (!enc.open(cfg)) {
                r.error = "could not open the encoder for " + cfg.path;
                return r;
            }
        }
        cv::LUT(frame, table, curved);   // every channel through the one table
        if (!enc.writeBgr(curved)) {
            r.error = "encode failed at frame " + std::to_string(i);
            return r;
        }
        ++r.frames;
    }
    if (r.frames == 0) {
        r.error = "no frames decoded from " + srcMp4;
        return r;
    }
    if (!enc.finish()) {
        r.error = "could not finish " + cfg.path;
        return r;
    }
    r.ok = true;
    return r;
}

QByteArray retoneJpeg(const QString &path, const std::array<std::uint8_t, 256> &lut)
{
    const QImage img = withToneTable(QImage(path), lut);
    if (img.isNull())
        return {};
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    if (!img.save(&buf, "JPG", kThumbJpegQuality))
        return {};
    return out;
}

} // namespace pinpoint
