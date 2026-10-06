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

// ShaftTracker v3.0-r1 — the SwingWindow layer. Derives per-frame grip anchors
// + lead-forearm φ + the 8-joint body skeleton from the offline pose (mirroring
// tools/shaftlab/prep_swing.py), builds a frame-decode callback over the face-on
// camera, and hands the lot to decideTrack() (shaft_track_assembly), the shared
// SwingWindow-free decide core. Vision-only (streams/segmentation unused).

#include "shaft_tracker.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <QElapsedTimer>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <variant>
#include <vector>

#include "ball_anchor.h"            // applyBallAnchor — the v3.4 post-hoc pass
#include "hand_axis.h"              // handAxisDirection — the WB4 shaft θ prior
#include "shaft_frame_io.h"         // lerpPoseFrame / decodeGray / buildFrameCache (shared with DTL)
#include "shaft_hand_clean.h"       // cleanHandTrack (pair consistency + glitch rejection)
#include "shot_analyzer.h"          // ShotAnalysisJob
#include "swing_window.h"
#include "format_descriptor.h"
#include "../Core/pp_debug.h"

namespace pinpoint::analysis {
namespace {

constexpr double kPi = 3.14159265358979323846;
// COCO joints for the body ROI: shoulders, hips, knees, ankles (prep_swing).
constexpr int kBodyJoints[8] = {5, 6, 11, 12, 13, 14, 15, 16};

// lerpPoseFrame / decodeGrayFromBytes / decodeGray / the frame cache moved to
// shaft_frame_io.h — same bodies, now shared with DtlShaftTracker.

} // namespace

ShaftTrack2D ShaftTracker::track(const pinpoint::SwingWindow& window, const PoseTrack2D& pose,
                                 const BallTrack2D& ball,
                                 const FusedStreams& /*streams*/, const Segmentation& /*segmentation*/,
                                 const ShotAnalysisJob& job, ShaftTrace* trace)
{
    QElapsedTimer wall;
    wall.start();
    // The time split (shaftshared::ShaftProf): one [ShaftTracker] split line at
    // the end, nothing persisted.
    shaftshared::ShaftProf prof;
    const shaftshared::ShaftProfInstall profInstall(&prof);
    ShaftTrack2D out;
    out.camera = pose.camera;
    if (pose.frames.size() < 2) { ppWarn() << "[ShaftTracker] no usable pose track — invalid"; return out; }

    const pinpoint::FormatDescriptor& fd = window.formatOf(pose.camera);
    const auto* cfmt = std::get_if<pinpoint::CameraFormat>(&fd.format);
    if (!cfmt || cfmt->width == 0 || cfmt->height == 0) {
        ppWarn() << "[ShaftTracker] source" << pose.camera << "has no camera format — invalid";
        return out;
    }
    const int w = int(cfmt->width), h = int(cfmt->height);

    ShaftV3Config cfg = ShaftV3Config::fromOverrides(job.tuningOverrides);
    if (job.fullWindow)
        cfg.spanBound = false;   // explicit re-analysis: evidence over the whole window, not just the swing span

    // camera frames inside pose coverage
    const std::vector<pinpoint::IndexEntry> all = window.entriesFor(pose.camera);
    const int64_t tLo = pose.frames.front().t_us, tHi = pose.frames.back().t_us;
    std::vector<pinpoint::IndexEntry> cov;
    for (const auto& e : all) if (e.timestamp_us >= tLo && e.timestamp_us <= tHi) cov.push_back(e);
    const int nf = int(cov.size());
    if (nf < 2) { ppWarn() << "[ShaftTracker] < 2 frames inside pose coverage — invalid"; return out; }

    // timebase: median inter-frame interval (NOT container fps)
    std::vector<int64_t> diffs;
    for (int i = 1; i < nf; ++i) diffs.push_back(cov[i].timestamp_us - cov[i - 1].timestamp_us);
    std::nth_element(diffs.begin(), diffs.begin() + diffs.size() / 2, diffs.end());
    const double dtUs = double(diffs[diffs.size() / 2]);
    const double fps = dtUs > 0 ? 1e6 / dtUs : 30.0;

    // derive grip / φ / joints per frame from the pose (mirrors prep_swing.py)
    const int leadElbow = (job.handedness == 2) ? 8 : 7;   // right-lead=7(L), left-lead=8(R)
    const int trailElbow = (leadElbow == 7) ? 8 : 7;       // the other arm (cfg.addr.trailArmVeto)
    std::vector<int64_t> tUs(nf);
    std::vector<double> gx(nf), gy(nf), phiRaw(nf), phiTrailRaw(nf);
    std::vector<std::vector<cv::Point2d>> rawJoints(nf, std::vector<cv::Point2d>(8));
    // WB4 hand-axis θ prior (dark unless shaft.handAxisPrior.enabled): per-frame
    // grip hand-axis direction (deg, image atan2) + confidence. Left empty when
    // the prior is off so decideTrack's cost tables stay bit-identical.
    const bool wantHandAxis = cfg.handAxisPrior.enabled;
    std::vector<double> handAxisDeg, handAxisConf;
    if (wantHandAxis) {
        handAxisDeg.assign(size_t(nf), std::numeric_limits<double>::quiet_NaN());
        handAxisConf.assign(size_t(nf), 0.0);
    }
    // Hand-track cleaning (shaft_hand_clean.h, cfg.hands): the grip below is the
    // mean of the two hand centroids, and on 16 Sept 2026 (W02 s2, ViTPose-B) the
    // lead centroid sat on the wrist while the trail one glitched 85 px every
    // 80 ms. Applied to a COPY of the pose, hands only; the persisted pose track
    // is untouched. Byte-identical when the pose shows neither fault.
    PoseTrack2D poseC = pose;
    {
        HandCleanConfig hc;
        hc.enabled = cfg.hands.enabled; hc.pairTolPx = cfg.hands.pairTolPx;
        hc.glitchPx = cfg.hands.glitchPx; hc.forearmGain = cfg.hands.forearmGain;
        hc.pairTolForearm = cfg.hands.pairTolForearm; hc.stillPx = cfg.hands.stillPx;
        hc.pairMinRun = cfg.hands.pairMinRun;
        const int leadWrist = (leadElbow == 7) ? 9 : 10, trailWrist = (leadElbow == 7) ? 10 : 9;
        std::vector<HandFrameIn> hf(poseC.frames.size());
        for (size_t k = 0; k < poseC.frames.size(); ++k) {
            const PoseFrame2D& f = poseC.frames[k];
            HandFrameIn& o = hf[k];
            o.lead  = QPointF(f.leadHand.x() * w,  f.leadHand.y() * h);
            o.trail = QPointF(f.trailHand.x() * w, f.trailHand.y() * h);
            o.leadWrist  = QPointF(f.kp[size_t(leadWrist)].x() * w,  f.kp[size_t(leadWrist)].y() * h);
            o.trailWrist = QPointF(f.kp[size_t(trailWrist)].x() * w, f.kp[size_t(trailWrist)].y() * h);
            o.leadElbow  = QPointF(f.kp[size_t(leadElbow)].x() * w,  f.kp[size_t(leadElbow)].y() * h);
            o.leadElbowConf  = f.conf[size_t(leadElbow)];
            o.leadWristConf  = f.conf[size_t(leadWrist)];
            o.trailWristConf = f.conf[size_t(trailWrist)];
        }
        // The glitch rule stops at impact: the finish hold's flaps are the phase
        // model's last motion run and stay as they were (see HandCleanConfig).
        size_t glitchLimit = size_t(-1);
        if (job.impactUs >= 0) {
            glitchLimit = 0;
            while (glitchLimit < poseC.frames.size() && poseC.frames[glitchLimit].t_us < job.impactUs) ++glitchLimit;
        }
        const HandCleanStats st = cleanHandTrack(hf, hc, glitchLimit);
        for (size_t k = 0; k < poseC.frames.size(); ++k) {
            poseC.frames[k].leadHand  = QPointF(hf[k].lead.x() / w,  hf[k].lead.y() / h);
            poseC.frames[k].trailHand = QPointF(hf[k].trail.x() / w, hf[k].trail.y() / h);
        }
        out.handPairFixed   = st.pairFixed;
        out.handGlitchFixed = st.glitchFixed;
        if (st.pairFixed || st.glitchFixed)
            ppInfo() << "[ShaftTracker] hands cleaned: pair" << st.pairFixed << "glitch" << st.glitchFixed
                     << "of" << int(poseC.frames.size()) << "pose frames";
    }
    const int handPairFixed = out.handPairFixed, handGlitchFixed = out.handGlitchFixed;

    int impf = -1;
    if (job.impactUs >= 0) {
        int64_t best = std::numeric_limits<int64_t>::max();
        for (int i = 0; i < nf; ++i) {
            const int64_t d = std::llabs(cov[i].timestamp_us - job.impactUs);
            if (d < best) { best = d; impf = i; }
        }
    }
    for (int i = 0; i < nf; ++i) tUs[i] = cov[i].timestamp_us;

    // Per-frame grip / φ / joints from a pose track (mirrors prep_swing.py). Called
    // once on the RAW pose and, only if that track fails, once on the cleaned one.
    const auto derive = [&](const PoseTrack2D& P) {
        size_t poseIdx = 0;
        for (int i = 0; i < nf; ++i) {
            while (poseIdx + 1 < P.frames.size() && P.frames[poseIdx + 1].t_us <= cov[i].timestamp_us) ++poseIdx;
            const PoseFrame2D pf = lerpPoseFrame(P, poseIdx, std::min(poseIdx + 1, P.frames.size() - 1),
                                                 cov[i].timestamp_us);
            const double grx = 0.5 * (pf.leadHand.x() + pf.trailHand.x()) * w;
            const double gry = 0.5 * (pf.leadHand.y() + pf.trailHand.y()) * h;
            gx[i] = grx; gy[i] = gry;
            const double ex = pf.kp[size_t(leadElbow)].x() * w, ey = pf.kp[size_t(leadElbow)].y() * h;
            const double plen = std::hypot(grx - ex, gry - ey);
            phiRaw[i] = (pf.conf[size_t(leadElbow)] > 0.30f && plen > 8.0)
                            ? std::atan2(gry - ey, grx - ex) * 180.0 / kPi
                            : std::numeric_limits<double>::quiet_NaN();
            const double tex = pf.kp[size_t(trailElbow)].x() * w, tey = pf.kp[size_t(trailElbow)].y() * h;
            const double tlen = std::hypot(grx - tex, gry - tey);
            phiTrailRaw[i] = (pf.conf[size_t(trailElbow)] > 0.30f && tlen > 8.0)
                                 ? std::atan2(gry - tey, grx - tex) * 180.0 / kPi
                                 : std::numeric_limits<double>::quiet_NaN();
            for (int j = 0; j < 8; ++j)
                rawJoints[i][j] = {pf.kp[size_t(kBodyJoints[j])].x() * w, pf.kp[size_t(kBodyJoints[j])].y() * h};
            if (wantHandAxis) {
                double aDeg = std::numeric_limits<double>::quiet_NaN();
                const float aConf = handAxisDirection(pf, w, h, cfg.handAxisPrior.confMin, aDeg);
                handAxisDeg[size_t(i)]  = (aConf > 0.f) ? aDeg : std::numeric_limits<double>::quiet_NaN();
                handAxisConf[size_t(i)] = aConf;
            }
        }
    };

    // ── decode-once span cache with parallel decode (shaft_frame_io.h) ───────
    // The cache vector stays OURS: buildFrameCache's callable closes over it by
    // reference, so it must outlive every frameAt() call below.
    // decode.frameStore: the analysis' one grey decode of this camera, shared (frame_store.h);
    // without a store the lease holds the camera's reader lock until this function returns,
    // since the executor no longer serialises this stage on the camera.
    const FrameLease frameLease(window, pose.camera, job.tuningOverrides, /*needBgr*/ false);
    std::vector<cv::Mat> frameCache;
    shaftshared::ShaftProfScope tCache(&prof, "cache");
    const FrameSource frameAt = buildFrameCache(window, cov, *cfmt, w, h, frameCache, frameLease.store());
    tCache.stop();
    // Persistent club-length prior (club_length_fusion.h) from the job — filled by
    // ShotProcessor from AppSettings (live) or SwingDiskLoader from swing.json
    // (re-analysis). Joined into the fusion only when matured (n ≥ 2); pass null
    // otherwise so the E-prior term contributes nothing. Read-only here — the
    // prior UPDATE happens in the caller after out.lengths is populated.
    LengthPriorState lengthPrior;
    lengthPrior.emaPx = job.priorClubLenPx;
    lengthPrior.varPx = (job.priorClubLenVarPx >= 0.0) ? job.priorClubLenVarPx : 0.0;
    lengthPrior.n     = job.priorClubLenN;
    const LengthPriorState* priorPtr = (job.priorClubLenN >= 2 && job.priorClubLenPx > 0.0)
                                           ? &lengthPrior : nullptr;
    // Pass the ball into decideTrack (A1) so out.measuredClubLenPx is measured
    // before head placement and can drive the length ladder; null when empty
    // (same emptiness notion as applyBallAnchor). θ is unaffected either way.
    // E4 club geometry (markerless_club_tracker_design.md §4.1): hosel from the
    // record, else the iron-family seed (length − 58 mm, the lab 7-iron measures
    // 940 − 882); grip end = hosel − shaftLengthMm, else a 265 mm grip. Only read
    // when shaft.seg.enabled.
    SegmentGeom segGeom;
    segGeom.clubLenMm = job.clubLengthM * 1000.0;
    segGeom.hoselMm   = job.hoselFromButtMm > 0.0 ? job.hoselFromButtMm : segGeom.clubLenMm - 58.0;
    segGeom.gripEndMm = job.shaftLengthMm > 0.0 ? segGeom.hoselMm - job.shaftLengthMm : 265.0;
    segGeom.bandsMm   = job.bandCentersMm;
    if (job.handsEndMm > 0.0) cfg.seg.handsEndMm = float(job.handsEndMm);   // measured once with a tape (club record)
    // The recorded exposure separates the blur's two edges (WedgeConfig::exposureUs); 0 ⇒ decideTrack
    // takes 99 % of the frame period.
    if (cfmt->exposure_us > 0.0) cfg.wedge.exposureUs = cfmt->exposure_us;

    // One tracking attempt on a pose track: decideTrack, the post-hoc ball anchor,
    // and the P1 re-sample from the anchored samples.
    const auto attempt = [&](const PoseTrack2D& P) -> ShaftTrack2D {
        shaftshared::ShaftProfScope tAttempt(&prof, "attempt");
        derive(P);
        ShaftTrack2D t = decideTrack(frameAt, tUs, gx, gy, phiRaw, rawJoints, w, h, fps,
                                     job.bandCentersMm, job.clubLengthM * 1000.0, impf, cfg, trace,
                                     ball.frames.empty() ? nullptr : &ball, priorPtr,
                                     handAxisDeg, handAxisConf, &segGeom, &phiTrailRaw);
        t.camera = pose.camera;
        // v3.4 (design §9): additive post-hoc ball anchor — reads the frozen DP
        // output above, never re-solves it. No-op when `ball` is empty.
        shaftshared::ShaftProfScope tAnchor(&prof, "ballAnchor");
        applyBallAnchor(t, ball, gx, gy, tUs, w, h, impf, job, trace);
        tAnchor.stop();
        // The anchor rewrites the address-hold SAMPLES after the P-positions were
        // sampled from them, so P1 could carry a θ its own samples no longer show
        // (16 Sept W02 s2 on B: samples 100°, P1 132°). A track-sampled P1 whose
        // nearest sample the anchor moved now follows that sample; a milestone-fit
        // P1 (measured from the pixels) is left alone.
        for (ShaftPosition& pos : t.positions) {
            if (pos.p != 1 || pos.source != uint8_t(PositionSource::TrackSample)) continue;
            const ShaftSample2D* ns = nullptr;
            int64_t bd = std::numeric_limits<int64_t>::max();
            for (const ShaftSample2D& sm : t.samples) {
                const int64_t d = std::llabs(sm.t_us - pos.t_us);
                if (d < bd) { bd = d; ns = &sm; }
            }
            if (!ns || !(ns->flags & ShaftBallAnchored)) continue;
            pos.thetaRad = ns->thetaRad;
            pos.gripPx   = ns->gripPx;
            pos.headPx   = ns->headPx;
            pos.lenPx    = ns->visibleLenPx;
            pos.conf     = std::max(pos.conf, ns->conf);
        }
        return t;
    };

    // ── The hands ladder (2026-10-01) ───────────────────────────────────────
    // Track on the RAW hands first: every swing the raw pose tracks stays exactly
    // as it was. Only a track that comes back invalid or refused is tried again on
    // the CLEANED hands (pair consistency + de-glitching), and that second track is
    // taken only if it is valid. The cleaning rescues 16 Sept W02 s2 on ViTPose-B
    // (a lead centroid on the wrist, an 85 px flicker every 80 ms), but applied to
    // every swing it also moved the onset and P1 of clean 09-09 swings whose poses
    // flicker just as much (the onset heuristics were tuned on that flicker).
    bool handsRetried = false;
    out = attempt(pose);
    const bool cleaned = handPairFixed > 0 || handGlitchFixed > 0;
    if (cfg.hands.enabled && cleaned && (!out.valid || out.refusedReason)) {
        ShaftTrack2D second = attempt(poseC);
        handsRetried = true;
        if (second.valid && second.refusedReason == 0) out = std::move(second);
        else if (trace) {
            // keep the first attempt's verdict; the trace now describes the second —
            // re-run the first so the trace matches what is published
            out = attempt(pose);
        }
        ppInfo() << "[ShaftTracker] raw hands gave" << (out.valid ? "a valid" : "an invalid")
                 << "track; cleaned hands" << (second.valid && second.refusedReason == 0 ? "ADOPTED" : "rejected");
    }
    out.handPairFixed   = handPairFixed;     // the stats are reported whether or not the cleaned hands were used
    out.handGlitchFixed = handGlitchFixed;
    out.handsRetried    = handsRetried;
    if (trace) { trace->handPairFixed = handPairFixed; trace->handGlitchFixed = handGlitchFixed; trace->handsRetried = handsRetried; }
    // Hands inconsistent on more than half the pose frames: the pose has no grip to
    // offer and every downstream witness (phase model, ray origin, ball geometry)
    // read from it is fiction. Refuse (reason 4) unless something stronger already did.
    if (cfg.addr.refuse && out.refusedReason == 0 && !poseC.frames.empty()
        && handPairFixed * 2 > int(poseC.frames.size())) {
        out.refusedReason = 4;
        out.valid = false;
    }

    // Why each attempt ran is part of the split: a 2nd is the cleaned-hands
    // retry of an invalid/refused raw track, a 3rd the trace-only re-run.
    ppInfo() << "[ShaftTracker] split ms:" << prof.line().c_str() << "| phase retries" << out.phaseRetries
             << (handsRetried ? "| attempt 2: raw hands invalid/refused, cleaned hands tried" : "")
             << (handsRetried && trace ? "(a 3rd is the trace re-run when they were rejected)" : "");
    ppInfo() << "[ShaftTracker] v3 frames" << nf << "coverage" << out.coverage
             << (out.valid ? "VALID" : "invalid") << "," << wall.elapsed() << "ms";
    if (out.refusedReason)
        ppWarn() << "[ShaftTracker] track REFUSED:" << shaftRefusedReasonName(out.refusedReason)
                 << "(P1-ball" << out.p1BallDeltaDeg << "deg, length ratio" << out.lenBallRatio
                 << ", phase retries" << out.phaseRetries << (out.phaseSuspect ? "suspect" : "ok") << ")";
    return out;
}

} // namespace pinpoint::analysis
