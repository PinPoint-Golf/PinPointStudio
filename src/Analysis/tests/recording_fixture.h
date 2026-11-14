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

// THE SYNTHETIC SWING the Recording tests share (recording_contract_test, recording_store_test):
// swing_window_parity_test's swing — one face-on camera, one IMU bound as lead forearm + lead hand,
// a Wrist session, impact 50 ms in — as the live capture holds it at the freeze (liveRecording),
// and the same swing written to a folder the way capture writes one (writeSyntheticSwing): the
// header through the ONE header writer (recording_header.h, with the analysisContext block), a
// video stream backed by a raw sidecar, the IMU samples, the recorded ball stream, and
// analysis.bindings. Loading that folder (recording_loader.h) is the disk half of the round trip.

#include "../../Recording/recording.h"
#include "../../Core/club_standards.h"
#include "../../Analysis/swing_analysis_json.h"   // bindingsToJson — the writer's own spelling
#include "../../Export/recording_header.h"
#include "../../Export/swing_doc.h"

#include <QByteArray>
#include <QDateTime>
#include <QTimeZone>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace recording_fixture {

using namespace pinpoint;
using namespace pinpoint::analysis;
namespace rec = pinpoint::recording;

// ── swing_window_parity_test's synthetic swing ────────────────────────────────────────────────
constexpr int64_t kT0       = 1'000'000;
constexpr int64_t kFrameDt  = 10'000;
constexpr int     kFrames   = 12;
constexpr int64_t kImuDt    = 5'000;
constexpr int     kImu      = 30;
constexpr int64_t kImpact   = kT0 + 50'000;
// captureSwingWindow(kT0 - 1, last + 1): the window starts one µs before the first sample.
constexpr int64_t kWindowStart = kT0 - 1;
constexpr int64_t kWindowEnd   = std::max(kT0 + (kImu - 1) * kImuDt, kT0 + (kFrames - 1) * kFrameDt) + 1;
constexpr SourceId kCam = 1;   // registration order in the fixture: the camera, then the IMU
constexpr SourceId kImuId = 2;
// The loader numbers a document's streams in their order, from 0: the camera, then the IMU.
constexpr SourceId kDiskCam = 0;
constexpr SourceId kDiskImu = 1;
// The raw sidecar's frame: 4 × 4 Mono8.
constexpr int kRawW = 4, kRawH = 4;

inline rec::ClubGeometry geometry(double lengthMm, std::vector<double> bands, double hosel = 0.0)
{
    rec::ClubGeometry g;
    g.lengthMm      = lengthMm;
    g.lengthKnown   = lengthMm > 0.0;
    g.shaftType     = QStringLiteral("steel");
    g.bandCentersMm = std::move(bands);
    g.hoselFromButtMm = hosel;
    g.shaftLengthMm = hosel > 0.0 ? hosel - 265.0 : 0.0;
    g.handsEndMm    = hosel > 0.0 ? 180.0 : 0.0;
    return g;
}

inline rec::AnalysisContext syntheticContext()
{
    rec::AnalysisContext c;
    c.handedness = rec::Handedness::Right;
    c.heightM    = 1.80;
    c.clubSet    = ClubSet::Men;
    c.club.name  = QStringLiteral("7 IRON");
    c.club.geometry = geometry(940.0, { 300.0, 500.0, 700.0 }, 880.0);
    c.club.lengthPrior.px = 310.0; c.club.lengthPrior.varPx = 25.0; c.club.lengthPrior.n = 6;
    c.club.standardLengthMm = standardClub(c.club.name, c.clubSet, c.heightM).lengthMm;
    c.bag.insert(QStringLiteral("7 IRON"), c.club.geometry);
    c.bag.insert(QStringLiteral("9 IRON"), geometry(914.0, { 290.0, 490.0 }, 855.0));
    c.bag.insert(QStringLiteral("DRIVER"), geometry(1156.0, {}, 1080.0));
    c.bag.insert(QStringLiteral("PITCHING WEDGE"), geometry(0.0, { 280.0 }));   // never measured
    return c;
}

// The synthetic swing as the live capture holds it at the freeze: every time window-relative,
// t0Us the window's start on the ring clock. `cam` / `imu` are the ring's source ids.
inline rec::Recording liveRecording(SourceId cam = kCam, SourceId imu = kImuId)
{
    rec::Recording r;
    r.t0Us = kWindowStart;
    r.sources.cameras.push_back({ cam, rec::Perspective::FaceOn, false, true, QString() });
    r.sources.imuLanes = { imu };
    r.capture.sessionType = 1;                       // Wrist
    r.capture.shotSource  = 4;
    r.capture.impactUs    = kImpact - kWindowStart;
    r.capture.motionCaptureQuality = QStringLiteral("High");

    // A face-on ball stream: found from frame 2, launched at frame 6.
    r.ball.track.camera = cam;
    for (int i = 0; i < kFrames; ++i) {
        BallSample2D s;
        s.t_us   = kT0 + i * kFrameDt - kWindowStart;
        s.found  = i >= 2 && i < 6;
        s.center = QPointF(0.5 + 0.001 * i, 0.8);
        s.radiusNorm = 0.01f;
        s.conf   = s.found ? 0.9f : 0.0f;
        r.ball.track.frames.push_back(s);
    }
    r.ball.track.launchTUs    = kT0 + 6 * kFrameDt - kWindowStart;
    r.ball.track.launchCenter = QPointF(0.505, 0.8);
    r.ball.searchRoi = QRectF(0.3, 0.6, 0.4, 0.3);

    ImuSegmentBinding fore, hand;
    fore.source = imu; fore.role = SegmentRole::LeadForearm; fore.calibrated = true;
    hand.source = imu; hand.role = SegmentRole::LeadHand;    hand.calibrated = true;
    fore.alignA = QQuaternion(0.9f, 0.1f, 0.2f, 0.3f).normalized();
    hand.mountM = QQuaternion(0.8f, -0.1f, 0.4f, 0.1f).normalized();
    fore.calibratedAtUtc = QStringLiteral("2026-11-14T09:00:00.000Z");
    fore.calibAgeSec = 600.0;
    r.imuBindings = { fore, hand };

    r.integrity.hostOrientationFilter = QStringLiteral("madgwick");
    r.integrity.windowStartUs = 0;
    r.integrity.windowEndUs   = kWindowEnd - kWindowStart;
    r.context = syntheticContext();
    return r;
}

inline QJsonArray relativeTimes(int64_t first, int64_t dt, int n)
{
    QJsonArray a;
    for (int i = 0; i < n; ++i) a.append(static_cast<qint64>(first + i * dt - kWindowStart));
    return a;
}

// The header capture writes for this Recording: the one header writer, with the context recorded.
inline RecordingHeader headerFor(const rec::Recording &r)
{
    RecordingHeader h;
    h.swing   = RecordingHeader::Identity{ QStringLiteral("swing_0001"), 1 };
    h.athlete = RecordingHeader::Athlete{ QStringLiteral("Test Golfer"), QStringLiteral("uuid-1"),
                                          QStringLiteral("Right"), r.context.heightM };
    h.sessionDir = QStringLiteral("2026-11-14_Test-Golfer_Wrist_01");
    h.clock   = RecordingHeader::Clock{ r.t0Us, QDateTime::fromMSecsSinceEpoch(1'763'100'000'000LL, QTimeZone::UTC) };
    h.window  = RecordingHeader::Window{ r.integrity.windowStartUs, r.integrity.windowEndUs };
    h.capture = QJsonObject{
        { QStringLiteral("sessionType"),          r.capture.sessionType },
        { QStringLiteral("shotSource"),           r.capture.shotSource },
        { QStringLiteral("impactUs"),             static_cast<qint64>(r.capture.impactUs) },
        { QStringLiteral("motionCaptureQuality"), r.capture.motionCaptureQuality },
        // micTravel present: a travel-corrected anchor, so the legacy de-bias does not apply.
        { QStringLiteral("latencyUs"), QJsonObject{ { QStringLiteral("micTravel"), 2915 } } },
    };
    h.analysisContext = r.context;
    return h;
}

// Write `r` (a liveRecording) to `dir` as capture would: header, streams, analysis.bindings.
// `extra` is merged into the root last (a review block, a newer schema, …). False on a write error.
inline bool writeSyntheticSwing(const QString &dir, const rec::Recording &r,
                                const QJsonObject &extra = {}, QString *error = nullptr)
{
    const rec::CameraStream &cam = r.sources.cameras.front();

    // The face-on camera: a raw sidecar of kFrames 4×4 Mono8 frames.
    const QString rawName = QStringLiteral("FaceOn.raw");
    QFile raw(dir + QLatin1Char('/') + rawName);
    if (!raw.open(QIODevice::WriteOnly)) { if (error) *error = QStringLiteral("raw sidecar"); return false; }
    for (int i = 0; i < kFrames; ++i)
        raw.write(QByteArray(kRawW * kRawH, char(i)));
    raw.close();

    QJsonArray roi{ r.ball.searchRoi.x(), r.ball.searchRoi.y(), r.ball.searchRoi.width(), r.ball.searchRoi.height() };
    const QJsonObject video{
        { QStringLiteral("kind"),    QStringLiteral("video") },
        { QStringLiteral("alias"),   QStringLiteral("FaceOn") },
        { QStringLiteral("file"),    QStringLiteral("FaceOn.mp4") },
        { QStringLiteral("source"),  QJsonObject{ { QStringLiteral("serial"), QStringLiteral("CAM-1") } } },
        { QStringLiteral("encoded"), QJsonObject{ { QStringLiteral("width"), kRawW }, { QStringLiteral("height"), kRawH } } },
        { QStringLiteral("capture"), QJsonObject{ { QStringLiteral("fps_num"), 100 }, { QStringLiteral("fps_den"), 1 } } },
        { QStringLiteral("frames"),  QJsonObject{ { QStringLiteral("t_us"), relativeTimes(kT0, kFrameDt, kFrames) } } },
        { QStringLiteral("raw"),     QJsonObject{
              { QStringLiteral("file"), rawName }, { QStringLiteral("width"), kRawW },
              { QStringLiteral("height"), kRawH }, { QStringLiteral("pixelFormat"), QStringLiteral("Mono8") },
              { QStringLiteral("stride"), kRawW }, { QStringLiteral("frameBytes"), kRawW * kRawH } } },
        { QStringLiteral("setup"),   QJsonObject{
              { QStringLiteral("perspective"),  rec::perspectiveCode(cam.perspective) },
              { QStringLiteral("mirrored"),     cam.mirrored },
              { QStringLiteral("fixedInPlace"), cam.fixedInPlace },
              { QStringLiteral("ballDetection"), QJsonObject{ { QStringLiteral("searchRoi"), roi } } } } },
    };

    QJsonArray imuData;
    for (int i = 0; i < kImu; ++i)
        imuData.append(QJsonArray{ 0.0, 0.0, 9.81, 0.1 * i, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0 });
    const QJsonObject imu{
        { QStringLiteral("kind"),    QStringLiteral("imu") },
        { QStringLiteral("alias"),   QStringLiteral("LeadArm") },
        { QStringLiteral("source"),  QJsonObject{ { QStringLiteral("serial"), QStringLiteral("IMU-1") } } },
        { QStringLiteral("device"),  QJsonObject{ { QStringLiteral("outputRateHz"), 200 },
                                                  { QStringLiteral("orientationFilter"), r.integrity.hostOrientationFilter } } },
        { QStringLiteral("samples"), QJsonObject{ { QStringLiteral("t_us"), relativeTimes(kT0, kImuDt, kImu) },
                                                  { QStringLiteral("data"), imuData } } },
    };

    QJsonArray ballT, ballData;
    for (const BallSample2D &s : r.ball.track.frames) {
        ballT.append(static_cast<qint64>(s.t_us));
        ballData.append(QJsonArray{ s.found ? 1.0 : 0.0, s.center.x(), s.center.y(), double(s.radiusNorm), double(s.conf) });
    }
    const QJsonObject ball{
        { QStringLiteral("kind"),   QStringLiteral("ball") },
        { QStringLiteral("alias"),  QStringLiteral("FaceOn") },
        { QStringLiteral("frames"), QJsonObject{ { QStringLiteral("t_us"), ballT }, { QStringLiteral("data"), ballData } } },
        { QStringLiteral("launch"), QJsonObject{ { QStringLiteral("t_us"), static_cast<qint64>(r.ball.track.launchTUs) },
                                                 { QStringLiteral("x"), r.ball.track.launchCenter.x() },
                                                 { QStringLiteral("y"), r.ball.track.launchCenter.y() } } },
    };

    QJsonObject root = recordingHeaderJson(headerFor(r), QJsonArray{ video, imu, ball });

    std::vector<BindingRecord> binds;
    for (const ImuSegmentBinding &b : r.imuBindings) {
        BindingRecord br;
        br.serial = QStringLiteral("IMU-1");
        br.role = b.role; br.alignA = b.alignA; br.mountM = b.mountM;
        br.anatCalibrated = b.anatCalibrated; br.calibrated = b.calibrated;
        br.mountDeviationDeg = b.mountDeviationDeg; br.mountGravityErrorDeg = b.mountGravityErrorDeg;
        br.calibratedAtUtc = b.calibratedAtUtc; br.calibAgeSec = b.calibAgeSec;
        br.trunkCalib = b.trunkCalib; br.hackMotion = b.hackMotion;
        binds.push_back(br);
    }
    root[QStringLiteral("analysis")] = QJsonObject{ { QStringLiteral("bindings"), bindingsToJson(binds) } };
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it)
        root[it.key()] = it.value();
    // SwingStore::save, not writeSwingJson: `extra` may carry a schema the writer would restamp.
    return SwingStore::save(dir, root, error);
}

} // namespace recording_fixture
