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

#include "swing_exporter.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <variant>

#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <opencv2/imgproc.hpp>

#include "format_descriptor.h"
#include "imu_sample.h"
#include "swing_window.h"
#include "video_encoder.h"
#include "../Core/pp_debug.h"

namespace pinpoint {

namespace {

// How a payload of a given PixelFormat becomes a BGR frame.
struct DemosaicPlan {
    bool supported   = false;
    int  matType     = 0;       // cv::Mat element type of the raw payload
    int  cvtCode     = -1;      // cv::cvtColor code; -1 = passthrough (already BGR)
    const char* tag  = "none";  // recorded in swing.json processing.demosaic
    // Raw Mat rows = height * rowsNum / rowsDen. Planar YUV 4:2:0 payloads
    // (NV12/I420 — Y plane followed by chroma, stored contiguously by
    // CameraInstance::publishFrameToBuffer) are 3/2 image-height rows tall.
    int  rowsNum     = 1;
    int  rowsDen     = 1;
};

// Mirrors the live-view mapping (camera_instance.cpp PixelFormat->BayerPattern,
// raw_video_frame.cpp pattern->COLOR_Bayer{RGGB,BGGR,GRBG,GBRG}2BGR) so export
// colour matches the on-screen image exactly.  The _EA (edge-aware) variants
// share the same pattern naming — better quality, irrelevant cost off the hot
// path.  v1 handles 8-bit formats only.
DemosaicPlan demosaicPlanFor(PixelFormat fmt)
{
    switch (fmt) {
    case PixelFormat::BayerRG8: return {true, CV_8UC1, cv::COLOR_BayerRGGB2BGR_EA, "EA"};
    case PixelFormat::BayerBG8: return {true, CV_8UC1, cv::COLOR_BayerBGGR2BGR_EA, "EA"};
    case PixelFormat::BayerGR8: return {true, CV_8UC1, cv::COLOR_BayerGRBG2BGR_EA, "EA"};
    case PixelFormat::BayerGB8: return {true, CV_8UC1, cv::COLOR_BayerGBRG2BGR_EA, "EA"};
    case PixelFormat::Mono8:    return {true, CV_8UC1, cv::COLOR_GRAY2BGR,         "none"};
    case PixelFormat::BGR24:    return {true, CV_8UC3, -1,                         "none"};
    case PixelFormat::YUV422:
    case PixelFormat::YUYV:     return {true, CV_8UC2, cv::COLOR_YUV2BGR_YUYV,     "none"};
    case PixelFormat::UYVY:     return {true, CV_8UC2, cv::COLOR_YUV2BGR_UYVY,     "none"};
    case PixelFormat::NV12:     return {true, CV_8UC1, cv::COLOR_YUV2BGR_NV12,     "none", 3, 2};
    case PixelFormat::YUV420P:  return {true, CV_8UC1, cv::COLOR_YUV2BGR_I420,     "none", 3, 2};
    default:                    return {};   // MJPEG, H264_NAL, 12/16-bit: unsupported in v1
    }
}

const char* pixelFormatName(PixelFormat fmt)
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
    default:                        return "Unknown";
    }
}

QJsonArray toJsonTimestamps(const std::vector<int64_t>& tUs)
{
    QJsonArray a;
    for (int64_t t : tUs)
        a.append(static_cast<qint64>(t));
    return a;
}

// Demosaics the frame nearest impactUs from one camera and writes it as
// thumb.jpg (≤480 px wide) in the swing dir.  One small frame off the hot
// path — the extra demosaic is irrelevant next to the encode loop.  Returns
// the absolute entry timestamp used, or -1 when nothing could be written
// (no frames, unsupported format, short payload, imwrite failure).
int64_t writeThumbnail(const SwingWindow& window, SourceId sid,
                       int64_t impactUs, const QString& thumbPath)
{
    const auto entries = window.entriesFor(sid);
    if (entries.empty())
        return -1;

    const FormatDescriptor& fd = window.formatOf(sid);
    const auto* cfmt = std::get_if<CameraFormat>(&fd.format);
    if (!cfmt)
        return -1;
    const DemosaicPlan plan = demosaicPlanFor(cfmt->pixel_format);
    if (!plan.supported)
        return -1;

    // Entry nearest impact — clamps naturally to the captured range when a
    // backdated impact timestamp falls outside it.
    const IndexEntry* best = &entries.front();
    for (const IndexEntry& e : entries) {
        if (std::llabs(e.timestamp_us - impactUs) < std::llabs(best->timestamp_us - impactUs))
            best = &e;
    }

    const int srcW    = static_cast<int>(cfmt->width);
    const int srcH    = static_cast<int>(cfmt->height);
    const int rawRows = srcH * plan.rowsNum / plan.rowsDen;
    const size_t bpp      = static_cast<size_t>(CV_ELEM_SIZE(plan.matType));
    const size_t stride   = cfmt->plane_strides[0] ? cfmt->plane_strides[0]
                                                   : static_cast<size_t>(srcW) * bpp;
    const size_t minBytes = stride * static_cast<size_t>(rawRows);

    const SourceRing::ReadHandle handle = window.payloadOf(*best);
    if (!handle.data || handle.bytes < minBytes)
        return -1;

    const cv::Mat raw(rawRows, srcW, plan.matType,
                      const_cast<std::byte*>(handle.data), stride);
    cv::Mat bgr;
    if (plan.cvtCode >= 0)
        cv::cvtColor(raw, bgr, plan.cvtCode);
    else
        bgr = raw;

    // Downscale for cheap carousel I/O; never upscale.
    constexpr int kMaxThumbWidth = 480;
    cv::Mat thumb;
    if (bgr.cols > kMaxThumbWidth) {
        const double scale = double(kMaxThumbWidth) / bgr.cols;
        cv::resize(bgr, thumb, cv::Size(), scale, scale, cv::INTER_AREA);
    } else {
        thumb = bgr;
    }

    // Save via Qt, NOT cv::imwrite: OpenCV's imgcodecs resolves libjpeg symbols
    // against whichever libjpeg generation loaded first in this multi-FFmpeg
    // process, and a mismatched jpeg_error_mgr layout turns its error-handler
    // longjmp into a crash. QImage uses Qt's own (self-consistent) JPEG handler.
    const QImage img(thumb.data, thumb.cols, thumb.rows,
                     static_cast<qsizetype>(thumb.step), QImage::Format_BGR888);
    if (!img.save(thumbPath, "JPG", 85)) {
        ppWarn() << "[SwingExport] failed to write thumbnail" << thumbPath;
        return -1;
    }
    return best->timestamp_us;
}

} // namespace

SwingExportResult SwingExporter::run(const SwingWindow& window, const SwingExportJob& job)
{
    SwingExportResult result;
    result.swingDir = job.swingDir;

    const int64_t t0 = window.startTimestampUs();

    struct VideoStreamRecord {
        const SwingExportCamera* cam = nullptr;
        const CameraFormat*      fmt = nullptr;
        QString serial;
        const char* demosaicTag = "none";
        std::vector<int64_t> tUs;
    };
    std::vector<VideoStreamRecord> videoStreams;

    // ── Per-camera encode (sequential — lowest peak RAM; per-camera
    //    parallelism is a possible future option) ──────────────────────────
    cv::Mat bgr;   // single reused BGR scratch across all cameras
    for (const SwingExportCamera& cam : job.cameras) {
        const auto entries = window.entriesFor(cam.sourceId);
        if (entries.empty()) {
            ppWarn() << "[SwingExport] no frames for camera" << cam.alias << "— skipping";
            continue;
        }

        const FormatDescriptor& fd = window.formatOf(cam.sourceId);
        const auto* cfmt = std::get_if<CameraFormat>(&fd.format);
        if (!cfmt) {
            ppWarn() << "[SwingExport] source" << cam.sourceId << "is not a camera — skipping";
            continue;
        }

        const DemosaicPlan plan = demosaicPlanFor(cfmt->pixel_format);
        if (!plan.supported) {
            Q_ASSERT(cfmt->pixel_format != PixelFormat::H264_NAL);   // not expected from these cameras
            ppWarn() << "[SwingExport] unsupported pixel format"
                     << pixelFormatName(cfmt->pixel_format)
                     << "for camera" << cam.alias << "— skipping";
            continue;
        }

        const int srcW = static_cast<int>(cfmt->width);
        const int srcH = static_cast<int>(cfmt->height);
        const int outW = srcW & ~1;   // libx264/yuv420p needs even dims: crop,
        const int outH = srcH & ~1;   // never pad (padding invents pixels)
        if (outW <= 0 || outH <= 0) {
            ppWarn() << "[SwingExport] degenerate dimensions for" << cam.alias << "— skipping";
            continue;
        }
        const cv::Rect cropRect(0, 0, outW, outH);

        auto encoder = makeVideoEncoder(job.codec.toStdString());
        if (!encoder) {
            result.error = QStringLiteral("no '%1' encoder available (built without FFmpeg?)")
                               .arg(job.codec);
            return result;
        }

        VideoEncoderConfig cfg;
        cfg.width   = outW;
        cfg.height  = outH;
        cfg.out_fps = 30;
        cfg.crf     = job.crf;
        cfg.preset  = "medium";
        cfg.path    = QString(job.swingDir + QLatin1Char('/') + cam.fileName).toStdString();
        if (!encoder->open(cfg)) {
            result.error = QStringLiteral("failed to open encoder for %1").arg(cam.fileName);
            return result;
        }

        VideoStreamRecord rec;
        rec.cam         = &cam;
        rec.fmt         = cfmt;
        rec.serial      = QString::fromStdString(fd.device_serial);
        rec.demosaicTag = plan.tag;
        rec.tUs.reserve(entries.size());

        const int    rawRows  = srcH * plan.rowsNum / plan.rowsDen;
        const size_t bpp      = static_cast<size_t>(CV_ELEM_SIZE(plan.matType));
        const size_t stride   = cfmt->plane_strides[0] ? cfmt->plane_strides[0]
                                                       : static_cast<size_t>(srcW) * bpp;
        const size_t minBytes = stride * static_cast<size_t>(rawRows);

        for (const IndexEntry& e : entries) {
            const SourceRing::ReadHandle handle = window.payloadOf(e);
            // Skip absent/short payloads entirely so MP4 frame i == t_us[i].
            if (!handle.data || handle.bytes < minBytes)
                continue;

            // Zero-copy wrap of the frozen ring payload (stable while Paused).
            const cv::Mat raw(rawRows, srcW, plan.matType,
                              const_cast<std::byte*>(handle.data), stride);
            if (plan.cvtCode >= 0)
                cv::cvtColor(raw, bgr, plan.cvtCode);
            else
                bgr = raw;   // BGR24 passthrough — still no copy

            // TODO(restorer): frame restoration hook

            if (!encoder->writeBgr(bgr(cropRect))) {
                result.error = QStringLiteral("encode failed for %1").arg(cam.fileName);
                return result;
            }
            rec.tUs.push_back(e.timestamp_us - t0);
        }

        // Every payload skipped (size/descriptor mismatch) would otherwise
        // "succeed" with an empty MP4 — fail loudly instead so the UI surfaces
        // it (the toast on swingSaveFailed) rather than a blank swing.
        if (rec.tUs.empty()) {
            result.error = QStringLiteral("0 frames written for %1 — all %2 buffered "
                                          "payloads skipped (payload/descriptor mismatch?)")
                               .arg(cam.fileName).arg(entries.size());
            return result;
        }

        if (!encoder->finish()) {
            result.error = QStringLiteral("failed to finalise %1").arg(cam.fileName);
            return result;
        }

        ppInfo() << "[SwingExport]" << cam.fileName << ":" << rec.tUs.size() << "frames";
        videoStreams.push_back(std::move(rec));
    }

    // ── Impact thumbnail ──────────────────────────────────────────────────
    // Prefer the designated camera (face-on); fall back to the other exported
    // streams in order. A missing thumbnail never fails the export.
    int64_t thumbTsUs = -1;
    if (job.thumbnailTimestampUs >= 0) {
        std::vector<SourceId> tryOrder;
        if (job.thumbnailSourceId != kInvalidSourceId)
            tryOrder.push_back(job.thumbnailSourceId);
        for (const VideoStreamRecord& rec : videoStreams)
            if (std::find(tryOrder.begin(), tryOrder.end(), rec.cam->sourceId) == tryOrder.end())
                tryOrder.push_back(rec.cam->sourceId);

        const QString thumbPath = job.swingDir + QStringLiteral("/thumb.jpg");
        for (SourceId sid : tryOrder) {
            thumbTsUs = writeThumbnail(window, sid, job.thumbnailTimestampUs, thumbPath);
            if (thumbTsUs >= 0) {
                result.thumbnailPath = thumbPath;
                break;
            }
        }
        if (result.thumbnailPath.isEmpty())
            ppWarn() << "[SwingExport] no thumbnail written for" << job.swingId;
    }

    // ── swing.json ────────────────────────────────────────────────────────
    QJsonArray streams;

    for (const VideoStreamRecord& rec : videoStreams) {
        QJsonObject s;
        s[QStringLiteral("kind")]  = QStringLiteral("video");
        s[QStringLiteral("alias")] = rec.cam->alias;
        s[QStringLiteral("file")]  = rec.cam->fileName;
        s[QStringLiteral("source")] = QJsonObject{
            {QStringLiteral("serial"),      rec.serial},
            {QStringLiteral("pixelFormat"), pixelFormatName(rec.fmt->pixel_format)},
            {QStringLiteral("width"),       static_cast<int>(rec.fmt->width)},
            {QStringLiteral("height"),      static_cast<int>(rec.fmt->height)},
        };
        s[QStringLiteral("capture")] = QJsonObject{
            {QStringLiteral("fps_num"), static_cast<int>(rec.fmt->fps_numerator)},
            {QStringLiteral("fps_den"), static_cast<int>(rec.fmt->fps_denominator)},
        };
        s[QStringLiteral("playback")] = QJsonObject{{QStringLiteral("fps"), 30}};
        s[QStringLiteral("processing")] = QJsonObject{
            {QStringLiteral("demosaic"), QString::fromLatin1(rec.demosaicTag)},
            {QStringLiteral("restorer"), QStringLiteral("none")},
        };
        s[QStringLiteral("frames")] = QJsonObject{
            {QStringLiteral("count"), static_cast<qint64>(rec.tUs.size())},
            {QStringLiteral("t_us"),  toJsonTimestamps(rec.tUs)},
        };
        streams.append(s);
    }

    if (job.saveImu) {
        // Discover IMU sources from the window itself (distinct source ids with
        // an ImuFormat descriptor) — the job only carries alias lookups.
        std::vector<SourceId> imuIds;
        for (const IndexEntry& e : window.entries()) {
            if (std::find(imuIds.begin(), imuIds.end(), e.source_id) != imuIds.end())
                continue;
            if (std::holds_alternative<ImuFormat>(window.formatOf(e.source_id).format))
                imuIds.push_back(e.source_id);
        }

        for (SourceId sid : imuIds) {
            const FormatDescriptor& fd = window.formatOf(sid);
            const QString serial = QString::fromStdString(fd.device_serial);
            const QString alias  = job.imuAliasBySerial.value(serial, serial);

            std::vector<int64_t> tUs;
            QJsonArray data;
            for (const IndexEntry& e : window.entriesFor(sid)) {
                const SourceRing::ReadHandle handle = window.payloadOf(e);
                if (!handle.data || handle.bytes < sizeof(ImuSample))
                    continue;
                ImuSample sample;                                    // alignment-safe
                std::memcpy(&sample, handle.data, sizeof(ImuSample));
                data.append(QJsonArray{sample.accel_x, sample.accel_y, sample.accel_z,
                                       sample.gyro_x,  sample.gyro_y,  sample.gyro_z,
                                       sample.quat_w,  sample.quat_x,
                                       sample.quat_y,  sample.quat_z});
                tUs.push_back(e.timestamp_us - t0);
            }
            if (tUs.empty())
                continue;

            QJsonObject s;
            s[QStringLiteral("kind")]   = QStringLiteral("imu");
            s[QStringLiteral("alias")]  = alias;
            s[QStringLiteral("schema")] = QStringLiteral("imu_sample_v1");
            s[QStringLiteral("source")] = QJsonObject{{QStringLiteral("serial"), serial}};
            s[QStringLiteral("units")]  = QJsonObject{
                {QStringLiteral("accel"), QStringLiteral("g")},
                {QStringLiteral("gyro"),  QStringLiteral("deg/s")},
                {QStringLiteral("quat"),  QStringLiteral("wxyz")},
            };
            s[QStringLiteral("samples")] = QJsonObject{
                {QStringLiteral("count"), static_cast<qint64>(tUs.size())},
                {QStringLiteral("t_us"),  toJsonTimestamps(tUs)},
                {QStringLiteral("data"),  data},
            };
            streams.append(s);
        }
    }

    // Wallclock: the anchor was snapshotted when the window was captured, i.e.
    // at monotonic ~endTimestampUs(); subtracting the window duration gives an
    // honest (ms-accurate) wallclock for t0.
    const int64_t durationUs = window.endTimestampUs() - t0;
    const QDateTime wallclock = job.wallclockAnchorUtc.addMSecs(-durationUs / 1000);

    QJsonObject root;
    root[QStringLiteral("schema")] = QStringLiteral("pinpoint.swing/1");
    root[QStringLiteral("swing")] = QJsonObject{
        {QStringLiteral("index"), job.swingIndex},
        {QStringLiteral("id"),    job.swingId},
    };
    root[QStringLiteral("athlete")] = QJsonObject{
        {QStringLiteral("name"),       job.athleteName},
        {QStringLiteral("uuid"),       job.athleteUuid},
        {QStringLiteral("handedness"), job.handedness},
    };
    root[QStringLiteral("session")] = QJsonObject{{QStringLiteral("dir"), job.sessionId}};
    root[QStringLiteral("clock")] = QJsonObject{
        {QStringLiteral("t0_us"),     static_cast<qint64>(t0)},
        {QStringLiteral("unit"),      QStringLiteral("us")},
        {QStringLiteral("wallclock"), wallclock.toString(Qt::ISODateWithMs)},
    };
    root[QStringLiteral("window")] = QJsonObject{
        {QStringLiteral("start_us"), 0},
        {QStringLiteral("end_us"),   static_cast<qint64>(durationUs)},
    };
    if (!result.thumbnailPath.isEmpty()) {
        // Additive — readers ignore unknown keys by contract.
        root[QStringLiteral("thumbnail")] = QJsonObject{
            {QStringLiteral("file"), QStringLiteral("thumb.jpg")},
            {QStringLiteral("t_us"), static_cast<qint64>(thumbTsUs - t0)},
        };
    }
    root[QStringLiteral("streams")] = streams;

    const QString jsonPath = job.swingDir + QStringLiteral("/swing.json");
    QSaveFile file(jsonPath);
    if (!file.open(QIODevice::WriteOnly)) {
        result.error = QStringLiteral("cannot write %1: %2").arg(jsonPath, file.errorString());
        return result;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        result.error = QStringLiteral("failed to commit %1: %2").arg(jsonPath, file.errorString());
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace pinpoint
