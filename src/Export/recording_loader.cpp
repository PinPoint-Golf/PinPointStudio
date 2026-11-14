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

#include "recording_loader.h"

#include "recording_header.h"     // the analysisContext reader, the legacy context, the club review
#include "swing_doc.h"            // analysisReadOriginUs — the one time-base guess
#include "swing_store.h"          // the document, whichever format it is in

#include "../Analysis/analysis_tuning.h"
#include "../Analysis/mp4_frame_reader.h"
#include "../Analysis/swing_analysis.h"
#include "../Buffer/format_descriptor.h"
#include "../Buffer/imu_sample.h"
#include "../Buffer/pixel_format_names.h"
#include "../Buffer/swing_payload_source.h"
#include "../Buffer/ram_payload_source.h"   // after swing_payload_source.h: it does not include it
#include "../Buffer/source_ring.h"
#include "../Buffer/swing_window.h"
#include "../Buffer/types.h"
#include "../Core/pp_debug.h"
#include "../Core/pp_tuned_constants.h"
#include "../IMU/hm_binding_recon.h"   // rebuilding a wG3 binding from what the recording holds
#include "../IMU/hm_unit_id.h"         // "<deviceId>#lowerArm" / "#palm" -> which segment
#include "../IMU/trunk_calib_provenance.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QQuaternion>
#include <QRectF>
#include <QRegularExpression>

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

namespace pinpoint {

namespace {

using namespace pinpoint::analysis;

std::vector<int64_t> readTimestamps(const QJsonArray& a)
{
    std::vector<int64_t> out;
    out.reserve(size_t(a.size()));
    for (const QJsonValue& v : a)
        out.push_back(int64_t(v.toDouble()));
    return out;
}

// Parse one IMU→segment binding from a JSON object carrying role + the A/M
// quaternions ([scalar,x,y,z]) + calibration provenance. Shared by both binding
// sources: analysis.bindings[] (analysed swings) and the per-stream device.*
// fallback (analysis-skipped corpus swings) — keeping the field set in one place.
ImuSegmentBinding parseBinding(const QJsonObject& b, SourceId source)
{
    const QJsonArray A = b[QStringLiteral("alignA")].toArray();
    const QJsonArray M = b[QStringLiteral("mountM")].toArray();
    ImuSegmentBinding bind;
    bind.source = source;
    bind.role   = SegmentRole(b[QStringLiteral("role")].toInt());
    bind.alignA = QQuaternion(float(A[0].toDouble()), float(A[1].toDouble()),
                              float(A[2].toDouble()), float(A[3].toDouble()));
    bind.mountM = QQuaternion(float(M[0].toDouble()), float(M[1].toDouble()),
                              float(M[2].toDouble()), float(M[3].toDouble()));
    bind.calibrated = !b.contains(QStringLiteral("calibrated"))
                   || b[QStringLiteral("calibrated")].toBool();
    bind.anatCalibrated       = b[QStringLiteral("anatCalibrated")].toBool(bind.calibrated);
    bind.mountDeviationDeg     = b[QStringLiteral("mountDeviationDeg")].toDouble();
    bind.mountGravityErrorDeg  = b[QStringLiteral("mountGravityErrorDeg")].toDouble();
    bind.calibratedAtUtc       = b[QStringLiteral("calibratedAt")].toString();
    bind.calibAgeSec           = b[QStringLiteral("calibAgeSec")].toDouble(-1.0);
    // Optional (trunk design §6.8): absent on every arm binding and every swing before W2.
    bind.trunkCalib            = readTrunkCalibProvenance(b);
    // ⚠ WHICH INSTRUMENT — and the fallback is not belt-and-braces, it is the only
    // answer for swings analysed before this key was written. Getting it wrong is
    // silent: the conjugate is skipped and every wrist sign inverts while the curve
    // still tracks the wrist convincingly.
    //
    // A unit-suffixed serial can only be a wG3 — "<deviceId>#lowerArm" / "#palm" is
    // that device's own identifier format and nothing else in the tree registers a
    // source that way — so the serial carries the answer whenever the flag does not.
    bind.hackMotion = b[QStringLiteral("hackMotion")].toBool(
        pinpoint::hm_unit_id::parse(b[QStringLiteral("serial")].toString(), nullptr, nullptr));
    return bind;
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-camera lazy frame readers. read(seq) streams ONE frame from disk into a
// single reusable buffer and returns a handle to it — valid until the next read()
// on the same reader (the analysis stages read frames strictly one-at-a-time, so
// a single buffer per camera satisfies the SwingPayloadSource contract). Memory
// stays bounded to one decoded frame per camera, never the whole window.
// ─────────────────────────────────────────────────────────────────────────────
class CameraReader {
public:
    virtual ~CameraReader() = default;
    virtual SourceRing::ReadHandle read(uint64_t seq) const noexcept = 0;
    FormatDescriptor fd;
};

// Raw sidecar: fixed-stride payloads, frame i at byte offset i*frameBytes — the
// exact bytes the live analyzer saw (bit-faithful). Random access is a seek+read.
class RawFrameReader final : public CameraReader {
public:
    QString path;
    qint64  frameBytes = 0;

    SourceRing::ReadHandle read(uint64_t seq) const noexcept override
    {
        if (!opened_) {
            file_.setFileName(path);
            opened_ = file_.open(QIODevice::ReadOnly);
        }
        if (!opened_ || frameBytes <= 0)
            return {};
        if (!file_.seek(qint64(seq) * frameBytes))
            return {};
        buf_.resize(size_t(frameBytes));
        const qint64 n = file_.read(reinterpret_cast<char*>(buf_.data()), frameBytes);
        if (n != frameBytes)
            return {};
        SourceRing::ReadHandle h;
        h.data  = buf_.data();
        h.bytes = size_t(frameBytes);
        return h;
    }

private:
    mutable QFile                  file_;
    mutable std::vector<std::byte> buf_;
    mutable bool                   opened_ = false;
};

// The decode.* keys (pose_inference_performance_plan.md step 5), resolved once per
// load. OFF (the defaults) is the reader as it was: OpenCV's backend choice, its own
// thread count, software decode, rewind to frame 0 on a back-seek.
Mp4FrameDecoder::Config mp4DecodeConfig(const QVariantMap& ov)
{
    namespace td = pinpoint::tuned::decode;
    Mp4FrameDecoder::Config c;
    c.threads = td::kThreads;
    c.seek    = td::kSeek;
    c.hwAccel = td::kHwAccel;
    tuning::apply(ov, "decode.threads", c.threads);
    tuning::apply(ov, "decode.seek",    c.seek);
    c.ffmpeg = td::kBackendFfmpeg;
    if (const auto it = ov.constFind(QStringLiteral("decode.backend")); it != ov.cend()) {
        const QString v = it->toString().trimmed().toLower();
        if (v == QLatin1String("ffmpeg"))
            c.ffmpeg = true;
        else if (v == QLatin1String("auto"))
            c.ffmpeg = false;
        else
            ppWarn() << "[Mp4Reader] decode.backend" << v << "is not auto|ffmpeg — left"
                     << (c.ffmpeg ? "ffmpeg" : "auto");
    }
    if (const auto it = ov.constFind(QStringLiteral("decode.hwAccel")); it != ov.cend()) {
        const QString v = it->toString().trimmed().toLower();
        if (v == QLatin1String("any") || v == QLatin1String("true") || v == QLatin1String("1"))
            c.hwAccel = true;
        else if (v == QLatin1String("off") || v == QLatin1String("false") || v == QLatin1String("0"))
            c.hwAccel = false;
        else
            ppWarn() << "[Mp4Reader] decode.hwAccel" << v << "is not off|any — left"
                     << (c.hwAccel ? "any" : "off");
    }
    return c;
}

// MP4 fallback (no raw sidecar): decode to BGR24, frame-accurate whatever the
// container's keyframe layout — lossy, slower, used only when raw is absent. The
// decoding itself is Mp4FrameDecoder (mp4_frame_reader.h); this adapts it to the
// payload contract and logs its numbers once, when the swing is let go.
class Mp4FrameReader final : public CameraReader {
public:
    Mp4FrameReader(const QString& path, Mp4FrameDecoder::Config cfg)
        : name_(QFileInfo(path).fileName()), dec_(path.toStdString(), cfg)
    {
    }
    ~Mp4FrameReader() override
    {
        // One line per camera per swing: frames decoded, rewinds/seeks, ms, and what
        // FFmpeg reports (codec, pixel format, threads, hw) — the decode column of the
        // [PoseRunner] split, and the other stages' reads, explained.
        if (dec_.stats().reads > 0 || dec_.info().opened || dec_.config().any())
            ppInfo() << "[Mp4Reader]" << name_.toStdString().c_str() << dec_.summary().c_str();
    }

    SourceRing::ReadHandle read(uint64_t seq) const noexcept override
    {
        try {
            const bool wasOpen = dec_.info().opened;
            cv::Mat bgr;
            const bool ok = dec_.read(int64_t(seq), bgr);
            if (!wasOpen && dec_.info().opened && dec_.info().ffmpegUnavailable)
                ppWarn() << "[Mp4Reader]" << name_.toStdString().c_str()
                         << "decode.* asked for the FFmpeg backend, which this OpenCV cannot load"
                            " (no opencv_videoio_ffmpeg plugin beside the exe or on PATH) — opened with"
                         << dec_.info().backend.c_str() << "instead";
            else if (!wasOpen && dec_.info().opened && dec_.config().hwAccel
                     && (dec_.info().hwFellBack || dec_.info().hwAccel == cv::VIDEO_ACCELERATION_NONE))
                ppWarn() << "[Mp4Reader]" << name_.toStdString().c_str()
                         << "decode.hwAccel any: no hardware decoder — decoding in software";
            if (!ok || bgr.empty())
                return {};
            // The handle points into the decoder's own frame buffer, valid until the
            // next read() — the SwingPayloadSource contract, without the 2 MB copy a
            // frame the old reader made into a second buffer.
            SourceRing::ReadHandle h;
            h.data  = reinterpret_cast<const std::byte*>(bgr.data);
            h.bytes = size_t(bgr.total() * bgr.elemSize());
            return h;
        } catch (...) {
            return {};
        }
    }

private:
    QString                 name_;
    mutable Mp4FrameDecoder dec_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Disk-backed SwingPayloadSource: IMU samples fully in RAM (tiny), camera frames
// streamed via the readers above. The validate() seqlock check is moot (the bytes
// are stable on disk / in our owned vectors) so it always succeeds.
// ─────────────────────────────────────────────────────────────────────────────
class SwingDiskSource final : public SwingPayloadSource {
public:
    // ⚠ IMU LANES ARE SERVED BY THE SHARED RamPayloadSource, not by a private
    // copy of the same code. That class was lifted out of THIS one
    // (deferred_sources_design.md §3.2) so the live deferred path and this
    // offline path resolve an IMU payload through one implementation —
    // re-analysis determinism depends on the two agreeing, and two copies that
    // drift would break it silently.
    void addImu(SourceId id, FormatDescriptor fd, std::vector<ImuSample> samples)
    {
        ram_.addImu(id, std::move(fd), std::move(samples));
    }
    void addCamera(SourceId id, std::unique_ptr<CameraReader> reader)
    {
        cam_[id] = std::move(reader);
    }

    SourceRing::ReadHandle payloadOf(SourceId id, uint64_t sequence) const noexcept override
    {
        if (ram_.has(id))
            return ram_.payloadOf(id, sequence);
        if (auto it = cam_.find(id); it != cam_.end())
            return it->second->read(sequence);
        return {};
    }

    const FormatDescriptor& formatOf(SourceId id) const noexcept override
    {
        if (ram_.has(id))
            return ram_.formatOf(id);
        if (auto it = cam_.find(id); it != cam_.end())
            return it->second->fd;
        static const FormatDescriptor kEmpty{};
        return kEmpty;
    }

    bool validate(SourceId, const SourceRing::ReadHandle&) const noexcept override
    {
        return true;   // bytes are stable (disk / owned RAM) — no seqlock race
    }

private:
    RamPayloadSource                                            ram_;
    std::unordered_map<SourceId, std::unique_ptr<CameraReader>> cam_;
};

} // namespace

LoadedRecording loadRecording(const QString &swingDir, const RecordingLoadOptions &opts)
{
    using namespace pinpoint::analysis;
    namespace rec = pinpoint::recording;
    LoadedRecording out;

    QString docError;
    const QJsonObject root = SwingStore::load(swingDir, &docError);
    if (root.isEmpty()) {
        out.error = docError;
        return out;
    }
    const QJsonObject analysisIn = root[QStringLiteral("analysis")].toObject();
    const QJsonObject captureIn  = root[QStringLiteral("capture")].toObject();
    out.analysisIn       = analysisIn;
    out.analysisOriginUs = analysisReadOriginUs(root);

    rec::Recording &r = out.recording;
    auto src = std::make_unique<SwingDiskSource>();
    std::vector<IndexEntry> entries;
    QHash<QString, SourceId> imuSerialToId;
    // Bindings rebuilt from each IMU stream's device.* calibration — the fallback
    // used when analysis.bindings is absent (analysis-skipped corpus swings).
    std::vector<ImuSegmentBinding> deviceBindings;
    // The HackMotion lanes the fallback could not bind, and why (hm_binding_recon.h) — recorded
    // below only when the fallback is the binding that is used.
    std::vector<hm_binding::Refusal> hmRefusals;

    int64_t  tMin   = INT64_MAX, tMax = INT64_MIN;
    SourceId nextId = 0;
    bool     anyRaw = false;
    // Distinguishes "no host-fused lane has been seen yet" from "one was seen and
    // recorded an empty filter" — only the second is a refusal to be preserved.
    bool     sawHostFusedLane = false;

    // One decode configuration for every MP4 camera of this swing (decode.* keys).
    const Mp4FrameDecoder::Config mp4Cfg = mp4DecodeConfig(opts.tuningOverrides);

    for (const QJsonValue& sv : root[QStringLiteral("streams")].toArray()) {
        const QJsonObject s    = sv.toObject();
        const QString     kind = s[QStringLiteral("kind")].toString();

        if (kind == QLatin1String("video")) {
            std::vector<int64_t> tUs =
                readTimestamps(s[QStringLiteral("frames")].toObject()[QStringLiteral("t_us")].toArray());
            if (tUs.empty())
                continue;

            const QString alias  = s[QStringLiteral("alias")].toString();
            const QString serial = s[QStringLiteral("source")].toObject()[QStringLiteral("serial")].toString();
            const int encW = s[QStringLiteral("encoded")].toObject()[QStringLiteral("width")].toInt();
            const int encH = s[QStringLiteral("encoded")].toObject()[QStringLiteral("height")].toInt();
            int fpsNum = 150, fpsDen = 1;
            double exposureUs = 0.0;
            ExposureSource exposureSource = ExposureSource::Unknown;
            if (s.contains(QStringLiteral("capture"))) {
                const QJsonObject c = s[QStringLiteral("capture")].toObject();
                if (c[QStringLiteral("fps_num")].toInt() > 0 && c[QStringLiteral("fps_den")].toInt() > 0) {
                    fpsNum = c[QStringLiteral("fps_num")].toInt();
                    fpsDen = c[QStringLiteral("fps_den")].toInt();
                }
                exposureUs     = c[QStringLiteral("exposureUs")].toDouble(0.0);
                exposureSource = exposureSourceFromName(
                    c[QStringLiteral("exposureSource")].toString().toStdString(),
                    c[QStringLiteral("exposureAuto")].toBool(false));
            }

            // Prefer the raw sidecar (bit-faithful) when present on disk.
            bool    useRaw = false;
            QString rawPath, rawPf;
            int     rawW = 0, rawH = 0;
            qint64  rawStride = 0, rawFrameBytes = 0;
            if (s.contains(QStringLiteral("raw"))) {
                const QJsonObject rw = s[QStringLiteral("raw")].toObject();
                rawPath = swingDir + QLatin1Char('/') + rw[QStringLiteral("file")].toString();
                if (QFile::exists(rawPath)) {
                    useRaw        = true;
                    rawW          = rw[QStringLiteral("width")].toInt();
                    rawH          = rw[QStringLiteral("height")].toInt();
                    rawPf         = rw[QStringLiteral("pixelFormat")].toString();
                    rawStride     = qint64(rw[QStringLiteral("stride")].toDouble());
                    rawFrameBytes = qint64(rw[QStringLiteral("frameBytes")].toDouble());
                }
            }
            anyRaw |= useRaw;

            FormatDescriptor fd;
            fd.device        = DeviceKind::Camera_UVC;
            fd.device_serial = serial.toStdString();
            CameraFormat cf{};
            cf.fps_numerator   = uint32_t(fpsNum);
            cf.fps_denominator = uint32_t(fpsDen);
            cf.exposure_us     = exposureUs;      // 0.0 for legacy swings (no exposure key)
            cf.exposure_source = exposureSource;
            std::unique_ptr<CameraReader> reader;
            if (useRaw) {
                cf.pixel_format      = pixelFormatFromName(rawPf.toStdString());
                cf.width             = uint32_t(rawW);
                cf.height            = uint32_t(rawH);
                cf.max_payload_bytes = uint32_t(rawFrameBytes);
                cf.plane_strides[0]  = uint32_t(rawStride);
                auto rr      = std::make_unique<RawFrameReader>();
                rr->path     = rawPath;
                rr->frameBytes = rawFrameBytes;
                reader       = std::move(rr);
            } else {
                cf.pixel_format      = PixelFormat::BGR24;
                cf.width             = uint32_t(encW);
                cf.height            = uint32_t(encH);
                cf.max_payload_bytes = uint32_t(encW * encH * 3);
                cf.plane_strides[0]  = uint32_t(encW * 3);
                reader = std::make_unique<Mp4FrameReader>(
                    swingDir + QLatin1Char('/') + s[QStringLiteral("file")].toString(), mp4Cfg);
            }
            fd.format  = cf;
            reader->fd = fd;

            const QJsonObject setup = s[QStringLiteral("setup")].toObject();
            const int perspectiveCode = setup[QStringLiteral("perspective")].toInt();

            // Face-on: recorded perspective (FaceOn == 2) unless the tool forces a
            // substring match; legacy streams without "setup" fall back to it too.
            bool faceOn;
            if (s.contains(QStringLiteral("setup")) && !opts.faceOnExplicit)
                faceOn = rec::perspectiveFromCode(perspectiveCode) == rec::Perspective::FaceOn;
            else
                faceOn = alias.contains(opts.faceOnSubstring, Qt::CaseInsensitive)
                      || s[QStringLiteral("file")].toString().contains(opts.faceOnSubstring, Qt::CaseInsensitive);

            // Hitting-area search box the face-on stream recorded (captures since
            // ball-ROI persistence) — BallRunner uses it instead of the pose stance
            // corridor, so re-analysis skips feet/shoe distractors like the live path.
            if (faceOn) {
                const QJsonArray sr = setup[QStringLiteral("ballDetection")].toObject()
                    [QStringLiteral("searchRoi")].toArray();
                if (sr.size() == 4)
                    r.ball.searchRoi = QRectF(sr.at(0).toDouble(), sr.at(1).toDouble(),
                                              sr.at(2).toDouble(), sr.at(3).toDouble());

                // Persisted live empty-mat baseline (captures since baseline
                // persistence): BallRunner reconstructs the tracker from it instead
                // of self-seeding over the swing's opening frames, where the ball
                // already sits. Validate dims, roi rect, and the blob file before
                // trusting it — any failure warns and leaves the ref empty.
                const QJsonObject bl = setup[QStringLiteral("ballDetection")].toObject()
                    [QStringLiteral("baseline")].toObject();
                if (!bl.isEmpty()) {
                    BallBaselineRef ref;
                    ref.path = swingDir + QLatin1Char('/') + bl[QStringLiteral("file")].toString();
                    ref.w    = bl[QStringLiteral("w")].toInt();
                    ref.h    = bl[QStringLiteral("h")].toInt();
                    const QJsonArray br = bl[QStringLiteral("roi")].toArray();
                    if (br.size() == 4)
                        ref.roi = QRectF(br.at(0).toDouble(), br.at(1).toDouble(),
                                         br.at(2).toDouble(), br.at(3).toDouble());
                    ref.rHat   = bl[QStringLiteral("rHat")].toDouble();
                    ref.fps    = bl[QStringLiteral("fps")].toDouble();
                    ref.noise0 = bl[QStringLiteral("noise0")].toDouble(1.0);
                    if (ref.w <= 0 || ref.h <= 0)
                        ppWarn() << "[Reanalysis]" << swingDir
                                 << "ballDetection.baseline has bad dims" << ref.w << "x"
                                 << ref.h << "— ignoring";
                    else if (br.size() != 4 || ref.roi.isEmpty())
                        ppWarn() << "[Reanalysis]" << swingDir
                                 << "ballDetection.baseline roi missing/empty — ignoring";
                    else if (!QFileInfo::exists(ref.path))
                        ppWarn() << "[Reanalysis]" << swingDir
                                 << "ballDetection.baseline blob missing at" << ref.path
                                 << "— ignoring";
                    else
                        r.ball.baseline = std::move(ref);
                }
            }

            const SourceId id = nextId++;
            for (size_t i = 0; i < tUs.size(); ++i)
                entries.push_back(IndexEntry{ tUs[i], id, uint64_t(i) });
            tMin = std::min(tMin, tUs.front());
            tMax = std::max(tMax, tUs.back());

            src->addCamera(id, std::move(reader));
            // A phone clip is timed from its own first frame, not the window's t0
            // (SwingStore::streamOnWindowClock): the capture check may not hold it to the window.
            if (!SwingStore::streamOnWindowClock(s))
                r.integrity.offClockCameras.push_back(id);
            // The impact camera (setup.perspective 4): the ImpactRunner's source,
            // never a face-on candidate (impact_camera_design.md §10).
            const bool impact = s.contains(QStringLiteral("setup"))
                && rec::perspectiveFromCode(perspectiveCode) == rec::Perspective::Impact;
            // The down-the-line camera (setup.perspective 1): read the same way
            // face-on is, and with the same fall-back for the pre-versioned
            // 2026-06-11 session, whose streams carry no "setup" block at all —
            // there the alias/file name is the only evidence of where the camera
            // stood ("Down-the-Line"). A stream the tool has NAMED as face-on
            // (--face-on DTL) is the face-on one and cannot also be this.
            //
            // The name fall-back matches WHOLE TOKENS, not substrings. A bare
            // contains("Down") claims "Downswing-cam", "Slowdown" and a directory
            // called "Downstairs"; and it ran against the whole PATH, so any swing
            // living under a folder with those letters in it was a DTL swing. The
            // regex wants "dtl" or "down the line" (any of -, _ or a space, or
            // nothing, between the words) bounded by a non-letter on each side,
            // case-insensitively. The alias is the golfer's own label and is asked
            // first; only then the file's BASENAME.
            bool dtl = false;
            if (!faceOn && !impact) {
                if (s.contains(QStringLiteral("setup"))) {
                    dtl = rec::perspectiveFromCode(perspectiveCode) == rec::Perspective::DownTheLine;
                } else {
                    static const QRegularExpression kDtlName(
                        QStringLiteral("(^|[^a-z])(dtl|down[-_ ]?the[-_ ]?line)([^a-z]|$)"),
                        QRegularExpression::CaseInsensitiveOption);
                    const QString base = QFileInfo(s[QStringLiteral("file")].toString()).fileName();
                    dtl = kDtlName.match(alias).hasMatch()
                       || kDtlName.match(base).hasMatch();
                }
            }
            // The camera's ROLE in this Recording, through the one perspective mapping: the impact
            // camera is never face-on; a face-on camera (the recorded one, or the one the tool
            // named) is never down the line. A stream with none of the three roles keeps its
            // recorded placement, or Other when the tool's face-on override took the role away.
            rec::CameraStream cam;
            cam.source = id;
            if (impact)      cam.perspective = rec::Perspective::Impact;
            else if (faceOn) cam.perspective = rec::Perspective::FaceOn;
            else if (dtl)    cam.perspective = rec::Perspective::DownTheLine;
            else {
                const rec::Perspective p = rec::perspectiveFromCode(perspectiveCode);
                cam.perspective = (p == rec::Perspective::None || p == rec::Perspective::Other)
                                      ? p : rec::Perspective::Other;
            }
            cam.mirrored     = setup[QStringLiteral("mirrored")].toBool(false);
            cam.fixedInPlace = setup[QStringLiteral("fixedInPlace")].toBool(false);
            cam.frames       = useRaw ? QStringLiteral("raw") : QStringLiteral("mp4");
            r.sources.cameras.push_back(cam);

        } else if (kind == QLatin1String("imu")) {
            const QJsonObject samples = s[QStringLiteral("samples")].toObject();
            std::vector<int64_t> tUs = readTimestamps(samples[QStringLiteral("t_us")].toArray());

            std::vector<ImuSample> rows;
            rows.reserve(size_t(samples[QStringLiteral("data")].toArray().size()));
            for (const QJsonValue& dv : samples[QStringLiteral("data")].toArray()) {
                const QJsonArray d = dv.toArray();
                std::array<float, 10> a{};
                for (int i = 0; i < 10 && i < d.size(); ++i)
                    a[size_t(i)] = float(d.at(i).toDouble());
                rows.push_back(makeImuSample(a[0], a[1], a[2], a[3], a[4],
                                             a[5], a[6], a[7], a[8], a[9]));
            }
            if (rows.size() != tUs.size() || rows.empty())
                continue;

            const QString serial = s[QStringLiteral("source")].toObject()[QStringLiteral("serial")].toString();
            int rateHz = 200;
            if (s[QStringLiteral("device")].toObject()[QStringLiteral("outputRateHz")].toInt() > 0)
                rateHz = s[QStringLiteral("device")].toObject()[QStringLiteral("outputRateHz")].toInt();

            // ⚠ THE INSTRUMENT IS RECORDED, SO READ IT RATHER THAN ASSUMING WITMOTION.
            // The exporter tags a HackMotion lane `"instrument": "hackmotion"`
            // (swing_exporter.cpp) precisely because the two lanes share the
            // imu_sample_v2 byte layout while meaning different things by it — accel is
            // gravity-removed here and raw there, and the quaternion runs world->body
            // here and body->world there. Hardcoding WitMotion threw that away on every
            // replay, which is why a re-analysed wG3 capture could not have produced a
            // correct wrist metric even once the live binding was fixed.
            const bool isHackMotion =
                s[QStringLiteral("instrument")].toString() == QLatin1String("hackmotion");
            const DeviceKind devKind = isHackMotion ? DeviceKind::IMU_HackMotion
                                                    : DeviceKind::IMU_WitMotion;

            // Which host filter produced the stored quaternions — host-fused lanes
            // only (see IntegrityInputs::hostOrientationFilter). Lanes that disagree
            // collapse to empty: with no single answer there is no safe one, and
            // the only consumer refuses rather than guesses.
            if (!isHackMotion) {
                const QString filt = s[QStringLiteral("device")].toObject()
                                        [QStringLiteral("orientationFilter")].toString();
                if (!sawHostFusedLane)
                    r.integrity.hostOrientationFilter = filt;
                else if (r.integrity.hostOrientationFilter.compare(filt, Qt::CaseInsensitive) != 0)
                    r.integrity.hostOrientationFilter.clear();
                sawHostFusedLane = true;
            }

            FormatDescriptor fd;
            fd.device        = devKind;
            fd.device_serial = serial.toStdString();
            ImuFormat imf{};
            imf.device         = devKind;
            imf.sample_rate_hz = uint32_t(rateHz);
            imf.packet_bytes   = sizeof(ImuSample);
            imf.packet_schema  = "imu_sample_v2";
            fd.format          = imf;

            const SourceId id = nextId++;
            for (size_t i = 0; i < tUs.size(); ++i)
                entries.push_back(IndexEntry{ tUs[i], id, uint64_t(i) });
            tMin = std::min(tMin, tUs.front());
            tMax = std::max(tMax, tUs.back());

            r.sources.imuLanes.push_back(id);
            if (!serial.isEmpty())
                imuSerialToId.insert(serial, id);
            src->addImu(id, fd, std::move(rows));

            // Fallback binding from the stream's device.* calibration (present only
            // when the exporter baked it in — every capture since corpus support).
            const QJsonObject deviceObj = s[QStringLiteral("device")].toObject();
            if (isHackMotion) {
                // The reconstruction itself — why a wG3 binding is rebuilt rather than
                // read back, and why the role comes from the unit and not from
                // sessionType — lives in hm_binding_recon.h, which is where it is
                // tested. Everything it needs is a value out of swing.json, so all that
                // is left here is reading them and reporting a refusal.
                const auto hb = hm_binding::reconstruct(
                    id, serial,
                    deviceObj[QStringLiteral("calibrationStateAtStart")].toInt(-1));
                if (hb.refusal != hm_binding::Refusal::None)
                    hmRefusals.push_back(hb.refusal);
                switch (hb.refusal) {
                case hm_binding::Refusal::NoFrameCandidate:
                    ppWarn() << "[Reanalysis]" << swingDir
                             << "carries a HackMotion lane but no frame candidate is"
                                " selected — the lane is loaded but not bound, so no"
                                " wrist metric will come from it.";
                    break;
                case hm_binding::Refusal::SerialNamesNoUnit:
                    ppWarn() << "[Reanalysis]" << swingDir
                             << "HackMotion lane serial" << serial
                             << "does not name a unit — cannot tell which segment it"
                                " measured, so it is not bound.";
                    break;
                case hm_binding::Refusal::None:
                    deviceBindings.push_back(*hb.binding);
                    break;
                }
            } else if (deviceObj[QStringLiteral("alignA")].toArray().size() == 4
                && deviceObj[QStringLiteral("mountM")].toArray().size() == 4)
                deviceBindings.push_back(parseBinding(deviceObj, id));

        } else if (kind == QLatin1String("ball")) {
            // v3.4 (design §9.7): NOT an EventBuffer/ring source — the recorded face-on ball
            // stream, carried into the Recording's ball inputs. t_us is in the same
            // window-relative domain as the video/imu streams above.
            const QJsonObject frames = s[QStringLiteral("frames")].toObject();
            std::vector<int64_t> bTUs = readTimestamps(frames[QStringLiteral("t_us")].toArray());
            const QJsonArray data = frames[QStringLiteral("data")].toArray();
            if (bTUs.size() != size_t(data.size()) || bTUs.empty())
                continue;

            BallTrack2D bt;
            bt.frames.reserve(bTUs.size());
            for (size_t i = 0; i < bTUs.size(); ++i) {
                const QJsonArray fr = data.at(int(i)).toArray();
                BallSample2D bs;
                bs.t_us = bTUs[i];
                if (fr.size() >= 5) {
                    bs.found      = fr.at(0).toDouble() != 0.0;
                    bs.center     = QPointF(fr.at(1).toDouble(), fr.at(2).toDouble());
                    bs.radiusNorm = float(fr.at(3).toDouble());
                    bs.conf       = float(fr.at(4).toDouble());
                }
                bt.frames.push_back(bs);
            }
            if (s.contains(QStringLiteral("launch"))) {
                const QJsonObject launch = s[QStringLiteral("launch")].toObject();
                bt.launchTUs    = int64_t(launch[QStringLiteral("t_us")].toDouble());
                bt.launchCenter = QPointF(launch[QStringLiteral("x")].toDouble(),
                                          launch[QStringLiteral("y")].toDouble());
            }
            r.ball.track = std::move(bt);
        }
    }

    // Sanity gate: drop a recorded ball stream whose WHOLE span lies outside the
    // reconstructed window [0, tMax−tMin] — the wrong-time-base recording bug
    // (warn-and-drop; the bug itself is out of scope). Conservative: only when
    // every frame is out, keyed off the time-ordered ends. (Whether a valid baseline makes the
    // BallRunner authoritative is the job builder's rule, D5 — a baseline-backed swing never hands
    // the recorded stream to the analysis, so the warning is not raised for one.)
    if (!r.ball.track.frames.empty() && tMax > tMin) {
        const int64_t span = tMax - tMin;
        if (r.ball.track.frames.front().t_us > span || r.ball.track.frames.back().t_us < 0) {
            if (!r.ball.baseline.isValid())
                ppWarn() << "[Reanalysis]" << swingDir
                         << "recorded ball stream lies wholly outside the window [0," << span
                         << "] (t_us" << r.ball.track.frames.front().t_us << ".."
                         << r.ball.track.frames.back().t_us << ") — dropping";
            r.ball.track = {};
        }
    }
    if (r.sources.cameras.empty()) {
        out.error = QStringLiteral("no video streams in swing.json");
        return out;
    }

    // Match the live window's time-ordered index (stable: per-source order kept).
    std::stable_sort(entries.begin(), entries.end(),
                     [](const IndexEntry& a, const IndexEntry& b) {
                         return a.timestamp_us < b.timestamp_us;
                     });

    // Session type — recorded capture.sessionType (a run may override it).
    if (captureIn.contains(QStringLiteral("sessionType"))
        && captureIn[QStringLiteral("sessionType")].toInt() >= 0)
        r.capture.sessionType = captureIn[QStringLiteral("sessionType")].toInt();
    r.capture.shotSource = captureIn[QStringLiteral("shotSource")].toInt(0);

    // Offline pose-model tier — recorded capture.motionCaptureQuality, so
    // re-analysis reproduces the model the swing was captured with (High ->
    // ViTPose++-L when the model is present on THIS host, else it degrades to B),
    // deterministically and independent of the re-running machine's live setting.
    // Absent on legacy swings ⇒ empty ⇒ ViTPose-B (unchanged historical behaviour).
    r.capture.motionCaptureQuality = captureIn[QStringLiteral("motionCaptureQuality")].toString();

    // Impact, most-precise source first (all WINDOW-RELATIVE µs to match the
    // reconstructed timeline): capture.impactUs (exact back-dated estimate, already
    // relative — corpus swings incl. analysis-skipped) → the recorded Impact phase
    // (analysed swings), taken to window time through the ONE time rule (windowTimeUs against
    // analysisReadOriginUs: a stamped document is read as written, an old absolute one
    // against its clock.t0_us) → thumbnail's nearest-captured-frame instant (quantized,
    // last resort).
    qint64 impact = -1;
    if (captureIn.contains(QStringLiteral("impactUs")))
        impact = qint64(captureIn[QStringLiteral("impactUs")].toDouble(-1));

    // Legacy acoustic-anchor de-bias. Captures before the mic-distance split
    // back-dated the anchor by a flat latencyUs.audioDevice (20 ms), which the
    // 2026-08-10 truth corpus measured as a uniform 13-22 ms EARLY vs video
    // impact (mean -17.1 ms, sd 3.3 — the acoustic travel is real, the device
    // latency was ~0). New captures subtract residual + travel at the source
    // and record latencyUs.micTravel; its ABSENCE identifies a legacy anchor,
    // which is corrected here to what the new capture chain would have
    // produced: add the recorded over-subtraction back, remove the travel for
    // the DEFAULT 1 m mic distance. The constant (not AppSettings) keeps
    // re-analysis deterministic and cross-host, like motionCaptureQuality and
    // the club prior. Gated on shotSource 4 (ShotController Acoustic —
    // Gui enum, compared as the persisted int): IMU/launch-monitor anchors
    // never carried the audio back-date. Corpus effect measured offline:
    // truth-swing anchor error -17.1 ms mean -> -0.0 ms, max |err| 5.1 ms.
    const QJsonObject latencyIn = captureIn[QStringLiteral("latencyUs")].toObject();
    if (impact > 0 && captureIn[QStringLiteral("shotSource")].toInt(-1) == 4
        && !latencyIn.contains(QStringLiteral("micTravel"))) {
        constexpr qint64 kDefaultMicTravelUs = 2915;   // 1 m at 343 m/s
        impact += qint64(latencyIn[QStringLiteral("audioDevice")].toDouble(0))
                - kDefaultMicTravelUs;
    }
    if (impact <= 0) {
        for (const QJsonValue& pv : analysisIn[QStringLiteral("phases")].toArray())
            if (pv.toObject()[QStringLiteral("phase")].toInt() == int(Phase::Impact))
                impact = windowTimeUs(qint64(pv.toObject()[QStringLiteral("t_us")].toDouble()),
                                      out.analysisOriginUs);
    }
    if (impact <= 0 && root.contains(QStringLiteral("thumbnail")))
        impact = qint64(root[QStringLiteral("thumbnail")].toObject()[QStringLiteral("t_us")].toDouble(-1));
    r.capture.impactUs = impact;

    // The analysis context: the recorded block, or the legacy context for an older document; and
    // the golfer's club edits, for effectiveContext (the analysis never reads the review block).
    const AnalysisContextRead ctx = readAnalysisContext(root);
    r.context            = ctx.context;
    out.contextRecorded  = ctx.recorded;
    out.readOnly         = ctx.readOnly;
    out.readOnlyReason   = ctx.readOnlyReason;
    out.review           = clubReviewOf(root);

    // IMU → segment bindings: the persisted session A/M calibration, serial-keyed.
    // Identity A/M is NOT synthesised — re-fusing without the recorded calibration
    // would be fabrication (mirrors swinglab_run).
    for (const QJsonValue& bv : analysisIn[QStringLiteral("bindings")].toArray()) {
        const QJsonObject b  = bv.toObject();
        const auto        it = imuSerialToId.constFind(b[QStringLiteral("serial")].toString());
        if (it == imuSerialToId.constEnd())
            continue;
        r.imuBindings.push_back(parseBinding(b, it.value()));
    }

    // No analysis.bindings (analysis-skipped corpus swing) → use the per-stream
    // device.* calibration the exporter baked into each IMU stream.
    if (r.imuBindings.empty()) {
        r.imuBindings = std::move(deviceBindings);
        // A lane refused here is a wrist reading this re-analysis cannot produce: its record
        // (wrist.hmBinding) joins analysis.gates through the analysis' caller.
        out.loadGates = hm_binding::refusalRecords(hmRefusals);
    }

    // Re-fusing on an uncalibrated A/M snapshot yields a confident-looking but
    // unreliable result — flag it, the way swinglab_run used to (don't silently
    // trust it into a corpus).
    for (const ImuSegmentBinding& b : r.imuBindings)
        if (!b.calibrated)
            ppWarn() << "[Reanalysis]" << swingDir << "binding role" << int(b.role)
                     << "recorded UNCALIBRATED (mount dev"
                     << b.mountDeviationDeg << "deg, gravity err"
                     << b.mountGravityErrorDeg << "deg) — results unreliable";

    const QJsonObject windowIn = root[QStringLiteral("window")].toObject();
    if (windowIn.contains(QStringLiteral("start_us")) && windowIn.contains(QStringLiteral("end_us"))) {
        r.integrity.windowStartUs = qint64(windowIn[QStringLiteral("start_us")].toDouble());
        r.integrity.windowEndUs   = qint64(windowIn[QStringLiteral("end_us")].toDouble());
    }

    out.window   = std::make_unique<SwingWindow>(std::move(src), std::move(entries), tMin, tMax);
    r.window     = out.window.get();
    r.t0Us       = 0;   // a loaded window's entries are already window-relative
    out.usedRaw  = anyRaw;
    out.ok       = true;
    return out;
}

} // namespace pinpoint
