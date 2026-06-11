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

// swinglab_run — the SwingLab offline runner (SWINGLAB_IMPL.md, stage L1).
//
// Rebuilds an EventBuffer + SwingWindow from a recorded PinPointStudio swing
// dir (video frames + IMU samples at their ORIGINAL timestamps) and executes
// the unmodified production analysis pipeline (makeShotAnalyzer). Tuning
// params are injectable from JSON; --trace re-runs the shaft stages with the
// trace sinks and dumps per-frame internals.
//
//   swinglab_run <swing_dir> --out <run_dir> [--params p.json] [--trace]
//                [--session-type 1] [--face-on Face] [--impact-us N]
//
// Outputs in <run_dir>:
//   result.json    swing.json-shaped document with the re-run analysis block
//   runmeta.json   provenance: source kind, wall times, params echo
//   trace.jsonl    (--trace) one line per frame: anchor + every candidate +
//                  the association choice; final line: the s_hand fit record
//
// Frames come from the MP4s (decoded to BGR24) or, when present, the raw
// sidecars (bit-faithful — the exact bytes the live analyzer saw).

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuaternion>
#include <QSysInfo>
#include <QVariantMap>

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

#include <cstdio>
#include <cstring>
#include <vector>

#include "event_buffer.h"
#include "imu_sample.h"
#include "source_descriptor.h"
#include "swing_window.h"

#include "../../../src/Analysis/shot_analyzer.h"
#include "../../../src/Analysis/imu_vision_fuser.h"
#include "../../../src/Analysis/phase_segmenter.h"
#include "../../../src/Analysis/pose_runner.h"
#include "../../../src/Analysis/shaft_tracker.h"
#include "../../../src/Export/swing_doc.h"

using namespace pinpoint;
using namespace pinpoint::analysis;

namespace {

struct VideoStreamIn {
    QString  file;          // mp4 path
    QString  rawFile;       // raw sidecar path ("" when absent)
    QString  alias;
    int      width = 0, height = 0;          // encoded dims (mp4 replay)
    int      rawWidth = 0, rawHeight = 0;    // source dims (raw replay)
    QString  rawPixelFormat;
    qint64   rawStride = 0, rawFrameBytes = 0;
    std::vector<int64_t> tUs;
    bool     faceOn = false;
    int      fpsNum = 150, fpsDen = 1;       // stream "capture" fps; legacy default
    bool     hasSetup = false;               // stream carries a "setup" object
    int      perspective = 0;                // setup.perspective (FaceOn = 2)
    SourceId sourceId = kInvalidSourceId;
};

struct ImuStreamIn {
    QString  alias, serial;
    std::vector<int64_t> tUs;
    std::vector<std::array<float, 10>> samples;   // ax..az gx..gz qw qx qy qz
    int      rateHz = 200;                   // stream "device" rate; legacy default
    SourceId sourceId = kInvalidSourceId;
};

PixelFormat pixelFormatFromName(const QString &n)
{
    static const QHash<QString, PixelFormat> map = {
        { "Mono8", PixelFormat::Mono8 },       { "BayerRG8", PixelFormat::BayerRG8 },
        { "BayerBG8", PixelFormat::BayerBG8 }, { "BayerGR8", PixelFormat::BayerGR8 },
        { "BayerGB8", PixelFormat::BayerGB8 }, { "YUYV", PixelFormat::YUYV },
        { "UYVY", PixelFormat::UYVY },         { "NV12", PixelFormat::NV12 },
        { "YUV420P", PixelFormat::YUV420P },   { "BGR24", PixelFormat::BGR24 },
        { "RGB24", PixelFormat::RGB24 },       { "BGRA32", PixelFormat::BGRA32 },
        { "RGBA32", PixelFormat::RGBA32 },
    };
    return map.value(n, PixelFormat::Unknown);
}

std::vector<int64_t> readTimestamps(const QJsonArray &a)
{
    std::vector<int64_t> out;
    out.reserve(size_t(a.size()));
    for (const QJsonValue &v : a)
        out.push_back(int64_t(v.toDouble()));
    return out;
}

// Flatten {"shaft": {"ridgeKernelPx": 11}} and/or {"shaft.ridgeKernelPx": 11}
// into the dotted-key map the tuning hooks consume.
QVariantMap flattenParams(const QJsonObject &o, const QString &prefix = {})
{
    QVariantMap out;
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QString key = prefix.isEmpty() ? it.key() : prefix + "." + it.key();
        if (it->isObject())
            out.insert(flattenParams(it->toObject(), key));
        else
            out.insert(key, it->toVariant());
    }
    return out;
}

QJsonArray quatJson(const QQuaternion &q)
{
    return QJsonArray{ q.scalar(), q.x(), q.y(), q.z() };
}

int fail(const char *msg)
{
    std::fprintf(stderr, "swinglab_run: %s\n", msg);
    return 1;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QCommandLineParser cli;
    cli.addPositionalArgument("swing_dir", "Recorded swing directory");
    QCommandLineOption optOut({ "o", "out" }, "Output run directory", "dir");
    QCommandLineOption optParams("params", "Tuning-override JSON", "file");
    QCommandLineOption optTrace("trace", "Dump per-frame shaft internals");
    QCommandLineOption optSession("session-type", "SessionController::Type", "int", "1");
    QCommandLineOption optFaceOn("face-on", "Face-on stream alias substring", "str", "Face");
    QCommandLineOption optImpact("impact-us", "Impact instant override", "us");
    QCommandLineOption optPose("pose", "Inject PoseTrack2D JSON (skip ViTPose)", "file");
    cli.addOptions({ optOut, optParams, optTrace, optSession, optFaceOn, optImpact, optPose });
    cli.process(app);

    if (cli.positionalArguments().isEmpty() || !cli.isSet(optOut))
        return fail("usage: swinglab_run <swing_dir> --out <run_dir> [--params f] [--trace]");
    const QString swingDir = cli.positionalArguments().first();
    const QString outDir   = cli.value(optOut);
    QDir().mkpath(outDir);

    QElapsedTimer wall;
    wall.start();

    // ── Load swing.json ──────────────────────────────────────────────────────
    QFile f(swingDir + "/swing.json");
    if (!f.open(QIODevice::ReadOnly))
        return fail("cannot open swing.json");
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root.isEmpty())
        return fail("swing.json parse failed");
    const QJsonObject analysisIn = root["analysis"].toObject();

    QVariantMap tuning;
    if (cli.isSet(optParams)) {
        QFile pf(cli.value(optParams));
        if (!pf.open(QIODevice::ReadOnly))
            return fail("cannot open params file");
        tuning = flattenParams(QJsonDocument::fromJson(pf.readAll()).object());
    }

    // ── Parse streams ────────────────────────────────────────────────────────
    std::vector<VideoStreamIn> videos;
    std::vector<ImuStreamIn>   imus;
    for (const QJsonValue &sv : root["streams"].toArray()) {
        const QJsonObject s = sv.toObject();
        const QString kind = s["kind"].toString();
        if (kind == "video") {
            VideoStreamIn v;
            v.alias  = s["alias"].toString();
            v.file   = swingDir + "/" + s["file"].toString();
            v.width  = s["encoded"].toObject()["width"].toInt();
            v.height = s["encoded"].toObject()["height"].toInt();
            v.tUs    = readTimestamps(s["frames"].toObject()["t_us"].toArray());
            if (s.contains("raw")) {
                const QJsonObject r = s["raw"].toObject();
                v.rawFile        = swingDir + "/" + r["file"].toString();
                v.rawWidth       = r["width"].toInt();
                v.rawHeight      = r["height"].toInt();
                v.rawPixelFormat = r["pixelFormat"].toString();
                v.rawStride      = qint64(r["stride"].toDouble());
                v.rawFrameBytes  = qint64(r["frameBytes"].toDouble());
            }
            if (s.contains("capture")) {
                const QJsonObject c = s["capture"].toObject();
                if (c["fps_num"].toInt() > 0 && c["fps_den"].toInt() > 0) {
                    v.fpsNum = c["fps_num"].toInt();
                    v.fpsDen = c["fps_den"].toInt();
                }
            }
            if (s.contains("setup")) {
                v.hasSetup    = true;
                v.perspective = s["setup"].toObject()["perspective"].toInt();
            }
            // Face-on: recorded perspective when the stream carries "setup";
            // legacy alias substring otherwise. An explicit --face-on always
            // wins (escape hatch for mislabelled recordings).
            if (v.hasSetup && !cli.isSet(optFaceOn))
                v.faceOn = (v.perspective == 2);
            else
                v.faceOn = v.alias.contains(cli.value(optFaceOn), Qt::CaseInsensitive)
                        || s["file"].toString().contains(cli.value(optFaceOn), Qt::CaseInsensitive);
            videos.push_back(std::move(v));
        } else if (kind == "imu") {
            ImuStreamIn m;
            m.alias  = s["alias"].toString();
            m.serial = s["source"].toObject()["serial"].toString();
            if (s["device"].toObject()["outputRateHz"].toInt() > 0)
                m.rateHz = s["device"].toObject()["outputRateHz"].toInt();
            const QJsonObject samples = s["samples"].toObject();
            m.tUs = readTimestamps(samples["t_us"].toArray());
            for (const QJsonValue &dv : samples["data"].toArray()) {
                const QJsonArray d = dv.toArray();
                std::array<float, 10> a{};
                for (int i = 0; i < 10 && i < d.size(); ++i)
                    a[size_t(i)] = float(d.at(i).toDouble());
                m.samples.push_back(a);
            }
            if (m.samples.size() == m.tUs.size() && !m.samples.empty())
                imus.push_back(std::move(m));
        }
    }
    if (videos.empty())
        return fail("no video streams in swing.json");

    // ── Rebuild the EventBuffer ──────────────────────────────────────────────
    EventBuffer buffer;
    int64_t tMin = INT64_MAX, tMax = INT64_MIN;

    bool anyRaw = false;
    for (VideoStreamIn &v : videos) {
        const bool useRaw = !v.rawFile.isEmpty() && QFile::exists(v.rawFile);
        anyRaw |= useRaw;
        SourceDescriptor desc;
        desc.name       = v.alias.toStdString();
        desc.identifier = v.alias.toStdString();
        CameraFormat fmt{};
        if (useRaw) {
            fmt.pixel_format = pixelFormatFromName(v.rawPixelFormat);
            fmt.width  = uint32_t(v.rawWidth);
            fmt.height = uint32_t(v.rawHeight);
            fmt.max_payload_bytes = uint32_t(v.rawFrameBytes);
            fmt.plane_strides[0]  = uint32_t(v.rawStride);
        } else {
            fmt.pixel_format = PixelFormat::BGR24;
            fmt.width  = uint32_t(v.width);
            fmt.height = uint32_t(v.height);
            fmt.max_payload_bytes = uint32_t(v.width * v.height * 3);
            fmt.plane_strides[0]  = uint32_t(v.width * 3);
        }
        fmt.fps_numerator   = uint32_t(v.fpsNum);
        fmt.fps_denominator = uint32_t(v.fpsDen);
        desc.format.device = DeviceKind::Camera_UVC;
        desc.format.format = fmt;
        desc.window_duration = std::chrono::milliseconds(8000);
        desc.expected_interarrival_us =
            std::chrono::microseconds(int64_t(1000000) * v.fpsDen / v.fpsNum);
        v.sourceId = buffer.registerSource(desc);
        if (!v.tUs.empty()) {
            tMin = std::min(tMin, v.tUs.front());
            tMax = std::max(tMax, v.tUs.back());
        }
    }
    for (ImuStreamIn &m : imus) {
        SourceDescriptor desc;
        desc.name       = m.alias.toStdString();
        desc.identifier = m.serial.isEmpty() ? m.alias.toStdString()
                                             : m.serial.toStdString();
        ImuFormat fmt{};
        fmt.device         = DeviceKind::IMU_WitMotion;
        fmt.sample_rate_hz = uint32_t(m.rateHz);
        fmt.packet_bytes   = sizeof(ImuSample);
        fmt.packet_schema  = "imu_sample_v2";
        desc.format.device = DeviceKind::IMU_WitMotion;
        desc.format.format = fmt;
        desc.window_duration = std::chrono::milliseconds(8000);
        desc.expected_interarrival_us = std::chrono::microseconds(1000000 / m.rateHz);
        m.sourceId = buffer.registerSource(desc);
        if (!m.tUs.empty()) {
            tMin = std::min(tMin, m.tUs.front());
            tMax = std::max(tMax, m.tUs.back());
        }
    }
    buffer.start();

    // Write IMU samples (cheap) then frames at their original timestamps.
    for (const ImuStreamIn &m : imus) {
        for (size_t i = 0; i < m.samples.size(); ++i) {
            auto slot = buffer.acquireWriteSlot(m.sourceId);
            if (!slot.valid || slot.capacity < sizeof(ImuSample)) continue;
            const auto &a = m.samples[i];
            const ImuSample smp = makeImuSample(a[0], a[1], a[2], a[3], a[4], a[5],
                                                a[6], a[7], a[8], a[9]);
            std::memcpy(slot.data, &smp, sizeof smp);
            *slot.bytes_written = uint32_t(sizeof smp);
            *slot.timestamp_us  = m.tUs[i];
            buffer.publish(m.sourceId, slot.sequence);
        }
    }
    for (const VideoStreamIn &v : videos) {
        const bool useRaw = !v.rawFile.isEmpty() && QFile::exists(v.rawFile);
        if (useRaw) {
            QFile rf(v.rawFile);
            if (!rf.open(QIODevice::ReadOnly))
                return fail("cannot open raw sidecar");
            QByteArray frame;
            for (size_t i = 0; i < v.tUs.size(); ++i) {
                frame = rf.read(v.rawFrameBytes);
                if (frame.size() < v.rawFrameBytes) break;
                auto slot = buffer.acquireWriteSlot(v.sourceId);
                if (!slot.valid || qint64(slot.capacity) < v.rawFrameBytes) continue;
                std::memcpy(slot.data, frame.constData(), size_t(v.rawFrameBytes));
                *slot.bytes_written = uint32_t(v.rawFrameBytes);
                *slot.timestamp_us  = v.tUs[i];
                buffer.publish(v.sourceId, slot.sequence);
            }
        } else {
            cv::VideoCapture cap(v.file.toStdString());
            if (!cap.isOpened())
                return fail("cannot open mp4");
            cv::Mat bgr;
            for (size_t i = 0; i < v.tUs.size(); ++i) {
                if (!cap.read(bgr)) break;
                if (!bgr.isContinuous()) bgr = bgr.clone();
                const size_t bytes = size_t(bgr.total() * bgr.elemSize());
                auto slot = buffer.acquireWriteSlot(v.sourceId);
                if (!slot.valid || slot.capacity < bytes) continue;
                std::memcpy(slot.data, bgr.data, bytes);
                *slot.bytes_written = uint32_t(bytes);
                *slot.timestamp_us  = v.tUs[i];
                buffer.publish(v.sourceId, slot.sequence);
            }
        }
    }

    buffer.pause();
    SwingWindow window = buffer.captureSwingWindow(tMin, tMax);
    std::fprintf(stderr, "[swinglab] window rebuilt: %zu entries (%s)\n",
                 window.entries().size(), anyRaw ? "raw" : "mp4");

    // ── Resolve the job ──────────────────────────────────────────────────────
    ShotAnalysisJob job;
    // Session type: recorded capture.sessionType when present; the CLI option
    // (default 1) covers legacy swings and explicit overrides.
    const QJsonObject captureIn = root["capture"].toObject();
    if (!cli.isSet(optSession) && captureIn.contains("sessionType")
        && captureIn["sessionType"].toInt() >= 0)
        job.sessionType = captureIn["sessionType"].toInt();
    else
        job.sessionType = cli.value(optSession).toInt();
    job.tuningOverrides = tuning;
    if (cli.isSet(optPose))
        job.poseTrackPath = cli.value(optPose);

    // Impact: CLI override, else the recorded Impact phase event.
    if (cli.isSet(optImpact)) {
        job.impactUs = cli.value(optImpact).toLongLong();
    } else {
        for (const QJsonValue &pv : analysisIn["phases"].toArray())
            if (pv.toObject()["phase"].toInt() == int(Phase::Impact))
                job.impactUs = qint64(pv.toObject()["t_us"].toDouble());
    }
    if (job.impactUs <= 0)
        return fail("no impact instant (no recorded Impact phase; pass --impact-us)");

    const QString hand = root["athlete"].toObject()["handedness"].toString();
    job.handedness = hand.compare("Left", Qt::CaseInsensitive) == 0 ? 2
                   : hand.compare("Right", Qt::CaseInsensitive) == 0 ? 1 : 0;

    for (const VideoStreamIn &v : videos) {
        if (v.faceOn) {
            job.cameraSources.insert(job.cameraSources.begin(), v.sourceId);
            ++job.faceOnCameraCount;
        } else {
            job.cameraSources.push_back(v.sourceId);
        }
    }
    for (const ImuStreamIn &m : imus)
        job.imuSources.push_back(m.sourceId);

    // Bindings from the persisted analysis.bindings (serial-keyed); identity
    // A/M (role only) is NOT synthesized — re-fusing without the session
    // calibration would be fabrication.
    int calibKnown = 0, calibTrue = 0;   // bindings carrying "calibrated" / of those, true
    for (const QJsonValue &bv : analysisIn["bindings"].toArray()) {
        const QJsonObject b = bv.toObject();
        const QString serial = b["serial"].toString();
        for (const ImuStreamIn &m : imus) {
            if (m.serial != serial) continue;
            ImuSegmentBinding bind;
            bind.source = m.sourceId;
            bind.role   = SegmentRole(b["role"].toInt());
            const QJsonArray A = b["alignA"].toArray(), M = b["mountM"].toArray();
            bind.alignA = QQuaternion(float(A[0].toDouble()), float(A[1].toDouble()),
                                      float(A[2].toDouble()), float(A[3].toDouble()));
            bind.mountM = QQuaternion(float(M[0].toDouble()), float(M[1].toDouble()),
                                      float(M[2].toDouble()), float(M[3].toDouble()));
            // Calibration provenance (legacy swings lack the field → assume
            // calibrated, matching the old behaviour).
            bind.calibrated = !b.contains("calibrated") || b["calibrated"].toBool();
            if (b.contains("calibrated")) {
                ++calibKnown;
                if (b["calibrated"].toBool()) ++calibTrue;
            }
            bind.anatCalibrated       = b["anatCalibrated"].toBool(bind.calibrated);
            bind.mountDeviationDeg    = b["mountDeviationDeg"].toDouble();
            bind.mountGravityErrorDeg = b["mountGravityErrorDeg"].toDouble();
            bind.calibratedAtUtc      = b["calibratedAt"].toString();
            bind.calibAgeSec          = b["calibAgeSec"].toDouble(-1.0);
            if (!bind.calibrated)
                std::fprintf(stderr,
                             "[swinglab] WARNING: binding %s recorded UNCALIBRATED "
                             "(mount dev %.1f deg, gravity err %.1f deg) — results unreliable\n",
                             serial.toUtf8().constData(),
                             bind.mountDeviationDeg, bind.mountGravityErrorDeg);
            job.imuBindings.push_back(bind);
        }
    }

    // ── Run the production pipeline ──────────────────────────────────────────
    const qint64 buildMs = wall.elapsed();
    auto analyzer = makeShotAnalyzer(job.sessionType);
    const ShotAnalysisResult result = analyzer->analyze(window, job);
    const qint64 analyzeMs = wall.elapsed() - buildMs;
    std::fprintf(stderr, "[swinglab] analysis %s in %lld ms (score %d)%s%s\n",
                 result.ok ? "ok" : "FAILED", (long long)analyzeMs, result.score,
                 result.ok ? "" : ": ", result.ok ? "" : result.error.toUtf8().constData());

    // ── Outputs ──────────────────────────────────────────────────────────────
    QJsonObject manifest;
    manifest["schema"]  = "pinpoint.swinglab/1";
    manifest["source"]  = QJsonObject{ { "swingDir", swingDir },
                                       { "frames", anyRaw ? "raw" : "mp4" } };
    QString werr;
    if (result.detail) {
        if (!SwingDocWriter::writeSwingJson(outDir, manifest, result.detail.get(), &werr))
            std::fprintf(stderr, "[swinglab] result write failed: %s\n",
                         werr.toUtf8().constData());
        QFile::rename(outDir + "/swing.json", outDir + "/result.json");
    }

    QJsonObject meta;
    meta["swingDir"]   = swingDir;
    meta["frames"]     = anyRaw ? "raw" : "mp4";
    meta["ok"]         = result.ok;
    meta["error"]      = result.error;
    meta["score"]      = result.score;
    meta["buildMs"]    = buildMs;
    meta["analyzeMs"]  = analyzeMs;
    meta["params"]     = QJsonObject::fromVariantMap(tuning);
    meta["impactUs"]   = job.impactUs;
    meta["bindings"]   = int(job.imuBindings.size());
    meta["host"]       = QSysInfo::machineHostName();
    meta["platform"]   = QSysInfo::prettyProductName();
    meta["sessionType"] = job.sessionType;
    // Verbatim capture echo (empty object for legacy swings) + the corpus
    // calibration verdict: true/false when recorded, null when unknown.
    meta["capture"]    = captureIn;
    meta["calibrated"] = calibKnown == 0 ? QJsonValue(QJsonValue::Null)
                                         : QJsonValue(calibTrue == calibKnown);
    {
        QFile mf(outDir + "/runmeta.json");
        if (mf.open(QIODevice::WriteOnly))
            mf.write(QJsonDocument(meta).toJson());
        else
            std::fprintf(stderr, "[swinglab] cannot write runmeta.json: %s\n",
                         mf.errorString().toUtf8().constData());
    }

    // ── Trace (re-runs the shaft stages with the sinks) ──────────────────────
    if (cli.isSet(optTrace) && job.faceOnCameraCount > 0) {
        const FusedStreams streams = ImuVisionFuser::fuse(window, job.imuBindings);
        const Segmentation seg = PhaseSegmenter::segment(streams, job.impactUs);
        ShotAnalysisRunnerOptions opt;
        opt.impactUs   = job.impactUs;
        opt.handedness = job.handedness;
        const PoseTrack2D pose = job.poseTrackPath.isEmpty()
            ? PoseRunner::run(window, job.cameraSources.front(), opt)
            : PoseRunner::loadFromJson(job.poseTrackPath, job.cameraSources.front());
        ShaftTracker::ShaftTrace trace;
        ShaftTracker::track(window, pose, streams, seg, job, &trace);

        QFile tf(outDir + "/trace.jsonl");
        if (!tf.open(QIODevice::WriteOnly)) {
            std::fprintf(stderr, "[swinglab] cannot write trace.jsonl: %s\n",
                         tf.errorString().toUtf8().constData());
            return result.ok ? 0 : 2;
        }
        for (size_t i = 0; i < trace.obs.size(); ++i) {
            const ShaftFrameObs &o = trace.obs[i];
            QJsonArray cands;
            for (const ShaftCandidate &c : o.candidates)
                cands.append(QJsonObject{
                    { "theta", c.thetaRad }, { "sigma", c.sigmaThetaRad },
                    { "len", c.visibleLenPx }, { "score", c.score },
                    { "wedge", c.wedge },
                    { "head", QJsonArray{ c.headPx.x, c.headPx.y } } });
            QJsonObject line{
                { "t_us", qint64(o.t_us) },
                { "grip", QJsonArray{ o.gripPx.x, o.gripPx.y } },
                { "qHandValid", o.qHandValid },
                { "candidates", cands },
                { "selected", i < trace.assembly.selected.size()
                                  ? trace.assembly.selected[i] : -1 } };
            tf.write(QJsonDocument(line).toJson(QJsonDocument::Compact) + "\n");
        }
        const auto &fit = trace.assembly.fit;
        QJsonObject fitLine{
            { "fit", QJsonObject{
                { "ok", fit.ok }, { "sign", fit.sign },
                { "offsetRad", fit.offsetRad }, { "residualRad", fit.residualRad },
                { "framesUsed", fit.framesUsed },
                { "sHand", QJsonArray{ fit.sHand[0], fit.sHand[1], fit.sHand[2] } } } },
            { "poseFrames", int(pose.frames.size()) },
            { "segConf", seg.conf } };
        tf.write(QJsonDocument(fitLine).toJson(QJsonDocument::Compact) + "\n");
        std::fprintf(stderr, "[swinglab] trace: %zu frames, fit.ok=%d residual=%.4f\n",
                     trace.obs.size(), fit.ok, fit.residualRad);
    }

    return result.ok ? 0 : 2;
}
