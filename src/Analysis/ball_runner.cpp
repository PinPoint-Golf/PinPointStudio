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

#include "ball_runner.h"

using pinpoint::analysis::BallBaselineRef;
using pinpoint::analysis::BallSample2D;
using pinpoint::analysis::BallTrack2D;
using pinpoint::analysis::PoseTrack2D;

#include <algorithm>
#include <variant>
#include <vector>

#include <QElapsedTimer>

#include "format_descriptor.h"
#include "swing_window.h"
#include "../Core/pp_debug.h"
#include "../Core/pp_profiler.h"

#if defined(HAVE_OPENCV)
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include "../Export/frame_decode.h"
#include "../Pose/ball_temporal.h"
#include "ball_baseline_io.h"
#endif

#if defined(HAVE_OPENCV)

namespace {

using namespace pinpoint::balltemporal;

// Live-detector timing constants BallDetector::detectTemporal() uses for the
// same replay (ball_detector.cpp's anonymous namespace) — duplicated here
// since they're detector-orchestration constants, not the parity-locked
// algorithm constants ball_temporal.h::tuning owns.
constexpr int   kSeedFrames   = 24;     // empty-mat baseline seed window (frames)
constexpr float kPresenceFrac = 0.40f;  // present if at-spot R >= this * L0

// COCO ankle keypoints — ball_detection_v2.md §4.1a's stance corridor: the
// ball is always horizontally between the feet and below the ankle line.
constexpr int kLeftAnkle  = 15;
constexpr int kRightAnkle = 16;
constexpr float kMinAnkleConf = 0.3f;

// Derive a single, generous search corridor (px, full-frame) spanning every
// posed frame's stance — offline replay only needs one static ROI for the
// whole window (the ball never moves once placed), unlike the live per-frame
// corridor that has to track a moving golfer in real time.
cv::Rect stanceCorridor(const PoseTrack2D &pose, int fw, int fh)
{
    double minX = 1.0, maxX = 0.0, ankleY = 0.0;
    int n = 0;
    for (const auto &f : pose.frames) {
        if (f.conf[kLeftAnkle] < kMinAnkleConf || f.conf[kRightAnkle] < kMinAnkleConf)
            continue;
        const double lx = f.kp[kLeftAnkle].x(),  rx = f.kp[kRightAnkle].x();
        const double ly = f.kp[kLeftAnkle].y(),  ry = f.kp[kRightAnkle].y();
        minX = std::min({minX, lx, rx});
        maxX = std::max({maxX, lx, rx});
        ankleY += 0.5 * (ly + ry);
        ++n;
    }
    if (n == 0 || minX > maxX) {
        // Fallback: static bottom-of-frame band (no pose coverage in the
        // address zone — e.g. pose disabled entirely for this session).
        return cv::Rect(int(0.15 * fw), int(0.75 * fh), int(0.70 * fw), int(0.25 * fh));
    }
    ankleY /= n;
    const double stanceWidth = (maxX - minX) * fw;
    const double margin = 0.5 * std::max(stanceWidth, 40.0);   // px, floor for a near-zero stance read
    const int x0 = std::clamp(int(minX * fw - margin), 0, fw - 1);
    const int x1 = std::clamp(int(maxX * fw + margin), x0 + 1, fw);
    const int y0 = std::clamp(int(ankleY * fh), 0, fh - 1);
    return cv::Rect(x0, y0, x1 - x0, fh - y0);
}

double medianDeltaUs(const std::vector<pinpoint::IndexEntry> &entries, size_t i0, size_t i1)
{
    if (i1 <= i0 + 1) return 1.0e6 / 100.0;   // degenerate — assume 100 Hz
    std::vector<double> dts;
    dts.reserve(i1 - i0 - 1);
    for (size_t i = i0 + 1; i < i1; ++i)
        dts.push_back(double(entries[i].timestamp_us - entries[i - 1].timestamp_us));
    std::nth_element(dts.begin(), dts.begin() + dts.size() / 2, dts.end());
    return dts[dts.size() / 2];
}

} // namespace

BallTrack2D BallRunner::run(const pinpoint::SwingWindow &window,
                            pinpoint::SourceId faceOnSource,
                            const PoseTrack2D &pose,
                            const ShotAnalysisRunnerOptions &opt,
                            const QRectF &searchRoi,
                            const BallBaselineRef &baseline)
{
    BallTrack2D track;
    track.camera = faceOnSource;

    const auto entries = window.entriesFor(faceOnSource);
    if (entries.empty()) {
        ppWarn() << "[BallRunner] no frames for source" << faceOnSource << "— empty track";
        return track;
    }

    const pinpoint::FormatDescriptor &fd = window.formatOf(faceOnSource);
    const auto *cfmt = std::get_if<pinpoint::CameraFormat>(&fd.format);
    if (!cfmt) {
        ppWarn() << "[BallRunner] source" << faceOnSource << "is not a camera — empty track";
        return track;
    }
    if (!pinpoint::demosaicPlanFor(cfmt->pixel_format).supported) {
        ppWarn() << "[BallRunner] unsupported pixel format for source" << faceOnSource
                 << "— empty track";
        return track;
    }

    QElapsedTimer wall;
    wall.start();
    PP_PROFILE_SCOPE("Analysis.BallRunner.run");

    // Same effective range PoseRunner::run() covers (plan §2's address-hold
    // widening included), but DENSE — no stride tiers: exploit 3/4 need the
    // precise found/x/y trace to place tk0 and the launch/departure instants
    // accurately, and ball detection is cheap enough (sub-ms/frame) that
    // sparse sampling isn't needed here the way it is for ViTPose.
    size_t i0 = 0, i1 = entries.size();
    if (opt.scanEndUs > opt.scanStartUs) {
        const int64_t lo = opt.scanStartUs - std::max<int64_t>(0, opt.addressScanPadUs);
        while (i0 < entries.size() && entries[i0].timestamp_us < lo)
            ++i0;
        while (i1 > i0 && entries[i1 - 1].timestamp_us > opt.scanEndUs)
            --i1;
        if (i0 >= i1) { i0 = 0; i1 = entries.size(); }
    }
    const int fw = cfmt->width, fh = cfmt->height;
    const double rHat = radiusForWidth(fw);

    // ── Pre-seeded baseline (re-analysis only): reconstruct the tracker from the
    //    persisted live empty-mat baseline instead of self-seeding over the
    //    opening frames, where the ball is already placed (which would bake its
    //    own response into B — the bug this path fixes). B is only valid over its
    //    OWN rect, so the baseline's rect wins over searchRoi/corridor. A dims or
    //    blob mismatch warns and falls through to the self-seed path unchanged.
    cv::Rect roiRect;
    cv::Mat  B;
    double   seedNoise = 1.0;
    bool     haveB = false;
    if (baseline.isValid()) {
        // Denormalize with the EXACT int-truncation the live detector uses
        // (ball_detector.cpp — static_cast<int> + qBound clamps, NOT round), so
        // the offline rect matches the pixels B was learned over (plan risk R4).
        const int rx = qBound(0, static_cast<int>(baseline.roi.x()      * fw), fw - 1);
        const int ry = qBound(0, static_cast<int>(baseline.roi.y()      * fh), fh - 1);
        const int rw = qBound(1, static_cast<int>(baseline.roi.width()  * fw), fw - rx);
        const int rh = qBound(1, static_cast<int>(baseline.roi.height() * fh), fh - ry);
        cv::Mat loaded;
        if (rw != baseline.w || rh != baseline.h) {
            ppWarn() << "[BallRunner] baseline ROI dims" << rw << "x" << rh
                     << "!= persisted" << baseline.w << "x" << baseline.h
                     << "(frame" << fw << "x" << fh << ") — self-seeding instead";
        } else if (!loadBallBaselineBlob(baseline.path, baseline.w, baseline.h, loaded)) {
            ppWarn() << "[BallRunner] baseline blob missing/truncated at"
                     << baseline.path << "— self-seeding instead";
        } else {
            roiRect   = cv::Rect(rx, ry, rw, rh);
            B         = loaded;
            seedNoise = baseline.noise0;
            haveB     = true;
        }
    }

    if (!haveB) {
        // Prefer the persisted hitting-area box (the region the live detector used)
        // so re-analysis searches only the ball, never the feet/shoes; fall back to
        // the pose-derived stance corridor for archival swings without one.
        if (!searchRoi.isEmpty()) {
            const int rx = std::clamp(int(searchRoi.x()      * fw), 0, fw - 1);
            const int ry = std::clamp(int(searchRoi.y()      * fh), 0, fh - 1);
            const int rw = std::clamp(int(searchRoi.width()  * fw), 1, fw - rx);
            const int rh = std::clamp(int(searchRoi.height() * fh), 1, fh - ry);
            roiRect = cv::Rect(rx, ry, rw, rh);
        } else {
            roiRect = stanceCorridor(pose, fw, fh);
        }
    }

    // Minimum-frames guard: self-seeding needs the full seed window + a couple of
    // replay frames; a pre-seeded baseline needs only >=2 frames to have something
    // to replay.
    const size_t minFrames = haveB ? size_t(2) : size_t(kSeedFrames) + 2;
    if (i1 - i0 < minFrames) {
        ppWarn() << "[BallRunner] too few frames in range (" << (i1 - i0) << ") to "
                 << (haveB ? "replay" : "seed a baseline") << " — empty track";
        return track;
    }

    const double fps = 1.0e6 / medianDeltaUs(entries, i0, i1);

    cv::Mat bgr, gray8, gray32;   // reused decode scratch
    auto decodeGray32 = [&](const pinpoint::IndexEntry &e) -> bool {
        const pinpoint::SourceRing::ReadHandle handle = window.payloadOf(e);
        if (!pinpoint::decodeToBgr(*cfmt, handle.data, handle.bytes, bgr))
            return false;
        cv::cvtColor(bgr, gray8, cv::COLOR_BGR2GRAY);
        gray8.convertTo(gray32, CV_32F);
        return true;
    };

    // ── Seed the empty-mat baseline B (mirrors BallDetector::startSeed()) ──
    // Skipped entirely when a persisted live baseline was loaded above — the
    // tracker then also gains the ~24 address frames the live seed consumed, and
    // replay starts at i0 (no frames consumed).
    size_t i = i0;
    if (!haveB) {
        cv::Mat seedAccum;
        int seedHave = 0;
        for (; i < i1 && seedHave < kSeedFrames; ++i) {
            if (!decodeGray32(entries[i]))
                continue;
            const cv::Mat R = paddedResponse(gray32, roiRect, rHat);
            if (R.empty())
                continue;
            cv::Mat R64;
            R.convertTo(R64, CV_64F);
            if (seedAccum.empty()) seedAccum = R64;
            else                   seedAccum += R64;
            seedNoise = robustNoise(R);
            ++seedHave;
        }
        if (seedHave == 0) {
            ppWarn() << "[BallRunner] could not seed a baseline (no decodable frames) — empty track";
            return track;
        }
        cv::Mat meanR = seedAccum / seedHave;
        meanR.convertTo(B, CV_32F);
    }
    TemporalBallTracker tracker(rHat, fps, B, seedNoise);

    // ── Replay the remaining frames through the SAME production core ──────
    track.frames.reserve(i1 - i);
    for (; i < i1; ++i) {
        const pinpoint::IndexEntry &e = entries[i];
        BallSample2D s;
        s.t_us = e.timestamp_us;
        if (!decodeGray32(e)) {
            track.frames.push_back(s);   // found=false — an honest decode gap, not a detector miss
            continue;
        }
        const cv::Mat R = paddedResponse(gray32, roiRect, rHat);
        if (R.empty()) {
            track.frames.push_back(s);
            continue;
        }
        tracker.push(R);
        const auto &L  = tracker.locked();
        const auto &LA = tracker.launched();

        if (L.valid) {
            const float ats = atSpot(R, L.ix, L.iy);
            const bool present = ats >= kPresenceFrac * L.L0;
            s.found      = present;
            s.center     = QPointF((roiRect.x + L.x) / double(fw), (roiRect.y + L.y) / double(fh));
            s.radiusNorm = float(rHat / fw);
            s.conf       = L.L0 > 0.f ? std::min(1.0f, ats / L.L0) : 0.f;
        }
        track.frames.push_back(s);

        if (LA.valid && track.launchTUs < 0) {
            // First collapse this window — record it as the launch instant.
            // No rearm(): ball_temporal.h's own contract is that an offline,
            // single-window run does not re-acquire after VANISHED.
            track.launchTUs    = e.timestamp_us;
            track.launchCenter = QPointF((roiRect.x + LA.x) / double(fw),
                                         (roiRect.y + LA.y) / double(fh));
        }
    }

    ppInfo() << "[BallRunner] source" << faceOnSource << ":" << track.frames.size()
             << "frames replayed, launch" << (track.launchTUs >= 0 ? "found" : "none")
             << "," << wall.elapsed() << "ms";
    return track;
}

#else // !HAVE_OPENCV

BallTrack2D BallRunner::run(const pinpoint::SwingWindow &window,
                            pinpoint::SourceId faceOnSource,
                            const PoseTrack2D &pose,
                            const ShotAnalysisRunnerOptions &opt,
                            const QRectF &searchRoi,
                            const BallBaselineRef &baseline)
{
    Q_UNUSED(window)
    Q_UNUSED(pose)
    Q_UNUSED(opt)
    Q_UNUSED(searchRoi)
    Q_UNUSED(baseline)
    ppWarn() << "[BallRunner] built without OpenCV — empty track";
    BallTrack2D track;
    track.camera = faceOnSource;
    return track;
}

#endif // HAVE_OPENCV


#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

BallTrack2D BallRunner::loadFromJson(const QString &file, pinpoint::SourceId camera)
{
    BallTrack2D track;
    track.camera = camera;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return track;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonArray frames = root[QStringLiteral("frames")].toArray();
    track.frames.reserve(size_t(frames.size()));
    for (const QJsonValue &fv : frames) {
        const QJsonObject o = fv.toObject();
        BallSample2D s;
        s.t_us       = int64_t(o[QStringLiteral("t_us")].toDouble());
        s.found      = o[QStringLiteral("found")].toBool();
        s.center     = QPointF(o[QStringLiteral("x")].toDouble(), o[QStringLiteral("y")].toDouble());
        s.radiusNorm = float(o[QStringLiteral("r")].toDouble());
        s.conf       = float(o[QStringLiteral("conf")].toDouble());
        track.frames.push_back(s);
    }
    const QJsonObject launch = root[QStringLiteral("launch")].toObject();
    if (!launch.isEmpty()) {
        track.launchTUs    = int64_t(launch[QStringLiteral("t_us")].toDouble());
        track.launchCenter = QPointF(launch[QStringLiteral("x")].toDouble(),
                                     launch[QStringLiteral("y")].toDouble());
    }
    return track;
}
