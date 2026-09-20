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

// DTL deciding half, part 2 — the post-solve pass (see dtl_shaft_post.h).
//
// Two jobs, in this order: re-register the line the DP solved (the snap), then
// decide what each frame is allowed to PUBLISH (the tier ladder). They are kept
// apart on purpose. The snap is a SPATIAL correction to a direction that is
// already roughly right; the tier is a claim about evidence. Face-on makes the
// same separation ("tiers/DP/ψ/coverage/length are all upstream and untouched"),
// and it is why the tier below is read off the DP's own evidence tables at the
// SOLVED θ rather than off a table re-sampled at a moved anchor — a second
// evaluation of the same geometry is a second chance to disagree with itself.

#include "dtl_shaft_post.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

#include "shaft_track_shared.h"   // circWrap / snapSearch

namespace pinpoint::analysis {
namespace {

constexpr double kPi = 3.14159265358979323846;

inline bool fin(double v) { return std::isfinite(v); }

// θ (deg) → grid bin, wrapped. The one conversion the whole file does, written
// once so the tier, the veto lookup and the snap guard cannot disagree about
// which bin a direction is.
inline int binOf(double thetaDeg, double grid, int NS)
{
    if (NS <= 0) return 0;
    int b = int(std::lround(thetaDeg / std::max(1e-9, grid))) % NS;
    if (b < 0) b += NS;
    return b;
}

// The polarity-free local-contrast image |frame − boxblur(k×k)| — the SAME image
// the snap searches on, and the same reason: the taped shaft alternates black and
// white over a mid-grey lit screen, so a signed response cancels along it.
cv::Mat contrastOf(const cv::Mat& g8, int ksz)
{
    cv::Mat g32, blur, out;
    g8.convertTo(g32, CV_32F);
    const int k = std::max(3, ksz | 1);
    cv::blur(g32, blur, cv::Size(k, k));
    cv::absdiff(g32, blur, out);
    return out;
}

// ── the evidenced run along ONE ray (Defect A) ───────────────────────────────
// How far the club is SEEN to run from (ox,oy) along θ. Not a score's argmax: the
// far end of the longest maximal run of EVIDENCED samples that starts near the
// origin, with evidence-free gaps up to holePx bridged.
//
// This is the measurement ridgeSweep's rEnd is not. rEnd is the radius at which
// the sqrt-normalised cumulative ridge score peaks, and that peak is searched only
// from j0 = minLenPx / rStep onward — so a ray that leaves the shaft early reports
// EXACTLY rLo + minLenPx (98 px on the shipped constants) whatever the club is
// doing, which is a floor wearing a length's clothes. MEASURED on the dev six:
// 51–101 refused frames per swing carried rEnd = 98 px to the digit.
//
// Two rules, both from the measured failure rather than from caution:
//  · the run must START near the origin. A bright thing 200 px down the ray is not
//    a club that terminates at the hands, and D1 already says so about directions.
//  · a gap is bridged if it is shorter than holePx. The taped groups are black tape
//    over a dark mat and the bare steel between the lit stretches drops out; a run
//    measured with no hole allowance stops at the first tape edge.
//
// e > 8 is ridgeSweep's own support rule, not a second threshold: the two must
// agree about what "a sample saw something" means or the tier ladder is grading a
// different run from the one the DP scored.
double evidencedRun(const cv::Mat& contrast32, double ox, double oy, double thetaRad,
                    const DtlShaftConfig& cfg, double startWithinPx)
{
    const RayProfile P = rayProfile(contrast32, ox, oy, thetaRad,
                                    double(cfg.ridge.rLo), double(cfg.ridge.rHi),
                                    double(cfg.ridge.rStep), cfg.ridge);
    const int n = int(P.r.size());
    const auto lit = [&P](int k) { return P.inb[size_t(k)] != 0 && P.e[size_t(k)] > 8.0f; };
    double best = 0.0;
    for (int i = 0; i < n; ) {
        if (!lit(i)) { ++i; continue; }
        if (double(P.r[size_t(i)]) > startWithinPx) break;   // no run starts near the grip any more
        int last = i;
        for (int j = i + 1; j < n; ) {
            if (lit(j)) { last = j; ++j; continue; }
            int k = j;
            while (k < n && !lit(k)) ++k;
            if (k >= n) break;
            if (double(P.r[size_t(k)]) - double(P.r[size_t(last)]) > cfg.len.holePx) break;
            j = k;
        }
        best = std::max(best, double(P.r[size_t(last)]));
        i = last + 1;
    }
    return best;
}

} // namespace

DtlShaftTrack2D dtlPostSolve(const FrameSource& frameAt,
                             const std::vector<int64_t>& tUs,
                             const DtlAnchors& anchors,
                             const FaceOnWitness* witness,
                             DtlSolveState& state,
                             int frameW, int frameH,
                             const SegmentGeom& geom,
                             const DtlShaftConfig& cfg,
                             DtlDecideTrace* trace)
{
    DtlShaftTrack2D out;
    out.frameWidth  = frameW;
    out.frameHeight = frameH;
    out.bands       = state.bands;
    out.ball        = state.ball;
    out.lFullPx     = state.lFullPx;
    out.lFullSource = state.lFullSource;
    out.rowFitA     = state.rowFitA;
    out.rowFitB     = state.rowFitB;
    // Carried, never re-derived: the window was placed in the solve and the two
    // things built from it were built there too (dtl_shaft_decide,
    // clubAwayWindowOf). This half only has to be able to SAY which rule ran.
    out.clubAwayWindow = state.clubAwayWindow;

    const int nf = int(tUs.size());
    const int NS = state.NS;
    const double nan = dtl::kNan;
    // A solve that produced no tables is an INVALID track, not an empty valid one:
    // "we ran and found nothing" and "we never ran" are different answers and the
    // ladder exists so they stay different.
    if (nf <= 0 || NS < 8
        || int(state.solved.size())   != nf || int(state.thetaDeg.size()) != nf
        || int(state.EV.size())       != nf || int(state.SUP.size())      != nf
        || int(state.REND.size())     != nf || int(state.ARMVETO.size())  != nf
        || int(anchors.gx.size())     != nf || int(anchors.gy.size())     != nf)
        return out;

    const std::vector<double>& gx = anchors.gx;
    const std::vector<double>& gy = anchors.gy;

    // ── per frame: the DP's own verdict, read once ──────────────────────────
    std::vector<int>    solvedBin(size_t(nf), 0);
    std::vector<char>   bandLock(size_t(nf), 0);
    std::vector<double> thetaOutDeg(size_t(nf), nan);
    std::vector<double> gxOut = gx, gyOut = gy;
    std::vector<double> snapOff(size_t(nf), nan), snapDth(size_t(nf), nan);
    std::vector<char>   snapAcc(size_t(nf), 0);
    // The snap's own support numbers, kept per frame because the tier ladder now
    // asks about the FIRST of them: where the snap was accepted, the support under
    // the re-registered line is a better statement about this frame than the
    // evidence along a ray from an anchor that is tens of px off the shaft axis.
    std::vector<double> snapBest(size_t(nf), nan), snapOrig(size_t(nf), nan);
    // … and the extent the snap's objective was averaged over, because the address
    // direction error WAS this number (see below).
    std::vector<double> snapDrawn(size_t(nf), nan);
    // The evidenced run the tier ladder will be asked about, and where it came
    // from. Seeded with the DP's own rEnd so a frame we cannot open still has a
    // number and says which kind of number it is.
    std::vector<double>    lenOut(size_t(nf), nan);
    std::vector<DtlLenSrc> lenSrc(size_t(nf), DtlLenSrc::Rend);
    std::vector<double>    lenLat(size_t(nf), nan);
    // The two columns the ladder's new gates are decided on, kept per frame so the
    // trace carries the verdict AND the number behind it.
    std::vector<DtlEvSrc>     evSrcOut(size_t(nf), DtlEvSrc::Ev);
    std::vector<DtlRevWaiver> revWaived(size_t(nf), DtlRevWaiver::None);
    // The run the LADDER was asked about — after the max() rule, not before it —
    // on every solved frame, published or not. A refused frame's length is the
    // whole subject of the minimum-length rule.
    std::vector<double>       runOut(size_t(nf), nan);

    for (int i = 0; i < nf; ++i) {
        if (!state.solved[size_t(i)] || !fin(state.thetaDeg[size_t(i)])) continue;
        const int bi = binOf(state.thetaDeg[size_t(i)], cfg.grid, NS);
        solvedBin[size_t(i)] = bi;
        thetaOutDeg[size_t(i)] = state.thetaDeg[size_t(i)];
        if (i < int(state.bandOk.size()) && state.bandOk[size_t(i)]
            && std::abs(shaftshared::circWrap(state.thetaDeg[size_t(i)]
                                              - double(state.band[size_t(i)].thetaDeg))) <= cfg.bandTol)
            bandLock[size_t(i)] = 1;
    }

    // ── (1) line re-registration (§5.9 "re-register the line") ──────────────
    // On the POLARITY-FREE contrast image |frame − boxblur(k)|, never the raw
    // frame. The taped shaft alternates black and white, so a SIGNED ridge
    // integral cancels along it and a wide bright limb scores higher on a frame
    // where the shaft is plainly visible — measured, and it is the same trap the
    // evidence channels exist for. The snap would otherwise re-register the club
    // onto a leg, which is exactly the failure the face-on record names on the
    // lead arm.
    // ── (1b) … and then MEASURE the run along the line that came out of it ──
    // One loop, because both halves want the same contrast image of the same
    // frame and building it twice is the only thing that would differ.
    //
    // DEFECT A, measured: the length the tier ladder was refusing frames on came
    // from REND — a ray cast from the POSE grip, which sits tens of px off the
    // shaft axis, so the ray leaves the thin shaft after ~100 px however long the
    // club really is. The fix is face-on's: measure off the RE-REGISTERED line,
    // never off the pose anchor's ray. Where the snap moved the line we measure
    // along the snapped line from the snapped grip; where it did not run or was
    // refused we measure along the solved θ and try a few lateral origin offsets,
    // which is the same correction done cheaply for a frame the snap declined.
    for (int i = 0; i < nf; ++i) {
        if (!state.solved[size_t(i)]) continue;
        if (!fin(gx[size_t(i)]) || !fin(gy[size_t(i)])) continue;
        const double rho = (i < int(state.rhoPred.size())) ? state.rhoPred[size_t(i)] : nan;
        const cv::Mat g8 = frameAt(i);
        if (g8.empty() || g8.type() != CV_8UC1) continue;
        const cv::Mat contrast = contrastOf(g8, cfg.contrastKsz);

        // ── the snap ────────────────────────────────────────────────────────
        // A band lock is a direct measurement of the line; the snap's ridge search
        // can only move it. Face-on's rule, unchanged.
        if (cfg.snap.enabled && !bandLock[size_t(i)] && fin(rho) && rho >= cfg.snapRhoMin) {
            const double theta0 = state.thetaDeg[size_t(i)] * kPi / 180.0;
            // ── the extent the objective is averaged over ────────────────────
            // snapSearch's objective is the MEAN per-sample evidence over r ∈
            // [rLo, drawnLen), so drawnLen decides WHICH PART OF THE CLUB the
            // search is scored on. The DP's rEnd is not that length: it is the
            // ridge score's argmax along a ray from the POSE grip, which leaves
            // the thin shaft early — DtlLenSrc is the record of it — and at
            // address it reads 98–216 px against a 316 px projected club.
            //
            // MEASURED, swing 0004, thirteen address frames: with drawnLen = rEnd
            // the snap lands at 55.0–58.0° where the band truth is 50.75°; with
            // drawnLen = ρ̂_D·L̂_D — the visibility law's own predicted projected
            // length, which is D3's ceiling and costs nothing new to know — it
            // lands at 54.0–54.5° on EVERY frame. The near half of the club at
            // address contains a brighter ridge than the club; a mean over the
            // near half prefers it, and a mean over the whole club does not. Over
            // all six swings' truth frames the same swap takes p50 from 2.75–4.25°
            // to 0.25–2.00° and p90 from 4.00–7.00° to 0.50–3.75°, with not one
            // frame more than 2° worse.
            //
            // max(), not replace: where the DP's terminus really is longer than
            // the law predicts the club is longer than the law predicts, and
            // scoring a line over less than its own drawn length would credit a
            // ray for the part of it nobody looked at.
            const double predLen = (fin(state.lFullPx) && fin(rho)) ? rho * state.lFullPx : nan;
            double drawnLen = double(state.REND[size_t(i)][size_t(solvedBin[size_t(i)])]);
            if (fin(predLen) && predLen > 0.0) drawnLen = std::max(drawnLen, predLen);
            if (!(drawnLen > 0.0)) drawnLen = double(cfg.ridge.minLenPx);
            snapDrawn[size_t(i)] = drawnLen;

            const shaftshared::SnapResult sr =
                shaftshared::snapSearch(contrast, gx[size_t(i)], gy[size_t(i)], theta0,
                                        drawnLen, cfg.snap, cfg.ridge);
            snapOff[size_t(i)]  = sr.offsetPx;
            snapDth[size_t(i)]  = sr.dThetaDeg;
            snapBest[size_t(i)] = double(sr.bestLineConf);
            snapOrig[size_t(i)] = double(sr.originLineConf);
            const bool moved = std::abs(sr.offsetPx) > 1e-9 || std::abs(sr.dThetaDeg) > 1e-9;

            // Three guards, and each is a measured failure rather than caution:
            //  · it must BEAT the line it came from — a snap that finds less than
            //    the DP's own direction is a worse registration wearing a number;
            //  · it must not land under the limb veto — the address snap's one
            //    known tail is re-registration onto the leg;
            //  · at a still-club frame it must stay inside the ball gate, because
            //    that is the frame's one direct DTL witness.
            const double thNewDeg = state.thetaDeg[size_t(i)] + sr.dThetaDeg;
            const bool beats  = sr.bestLineConf > sr.originLineConf;
            const bool vetoed = state.ARMVETO[size_t(i)][size_t(binOf(thNewDeg, cfg.grid, NS))] != 0;
            const bool offGate = i < int(state.ballGate.size()) && state.ballGate[size_t(i)]
                                 && i < int(state.thetaBallDeg.size())
                                 && fin(state.thetaBallDeg[size_t(i)])
                                 && std::abs(shaftshared::circWrap(thNewDeg
                                                                   - state.thetaBallDeg[size_t(i)]))
                                        > cfg.ball.gateDeg;
            if (moved && beats && !vetoed && !offGate) {
                // The published grip moves to the foot of the perpendicular from
                // the POSE grip onto the snapped line, as face-on does: the snap
                // registers the line, and the anchor follows it rather than the
                // other way round.
                const double thNew = thNewDeg * kPi / 180.0;
                const double nx = -std::sin(theta0), ny = std::cos(theta0);
                const double ax = gx[size_t(i)] + sr.offsetPx * nx;
                const double ay = gy[size_t(i)] + sr.offsetPx * ny;
                const double ux = std::cos(thNew), uy = std::sin(thNew);
                const double t  = (gx[size_t(i)] - ax) * ux + (gy[size_t(i)] - ay) * uy;
                gxOut[size_t(i)] = ax + t * ux;
                gyOut[size_t(i)] = ay + t * uy;
                thetaOutDeg[size_t(i)] = thNewDeg;
                snapAcc[size_t(i)] = 1;
            }
        }

        // ── the run ─────────────────────────────────────────────────────────
        // Within 40 px of the origin, because a run that starts farther out is not
        // a club that terminates at the hands.
        //
        // … and a run shorter than the ridge engine's own shortest credible
        // visible shaft is NOT a length, it is this channel saying it saw nothing.
        // MEASURED at P5 on swings 0004/0005/0006: the club is at its fastest and
        // motion blur wipes its LOCAL contrast, so the contrast run breaks after
        // 10–48 px while the channel that actually won the frame terminates at
        // 232–390 px. Believing the 10 px would refuse a frame the montage review
        // has already adjudicated as right — the markerless length-gate lesson,
        // which is also why D3 is one-sided. Below the threshold we did not measure
        // a length: fall back to the DP's own rEnd and SAY so in lenSrc, rather
        // than publish a channel's blindness as a club.
        const double startWithin = 40.0;
        const double credible    = double(cfg.ridge.minLenPx);
        if (snapAcc[size_t(i)]) {
            const double th = thetaOutDeg[size_t(i)] * kPi / 180.0;
            const double run = evidencedRun(contrast, gxOut[size_t(i)], gyOut[size_t(i)],
                                            th, cfg, startWithin);
            if (run >= credible) {
                lenOut[size_t(i)] = run;
                lenSrc[size_t(i)] = DtlLenSrc::SnapLine;
                lenLat[size_t(i)] = 0.0;
            }
        } else {
            // No snapped line to measure off. Sweep a handful of lateral origins
            // instead: the pose grip's error is almost entirely ACROSS the shaft
            // (it is the midpoint of two hand keypoints, and the shaft passes
            // between them), so a ray from an origin nudged along the normal
            // follows the club far longer than one from the anchor itself. Cheap —
            // seven ray profiles — and the offset taken is recorded, because "we
            // had to move 30 px to see it" is a statement about the pose.
            const double th = state.thetaDeg[size_t(i)] * kPi / 180.0;
            const double nx = -std::sin(th), ny = std::cos(th);
            const double offsets[7] = { 0.0, 10.0, -10.0, 20.0, -20.0, 30.0, -30.0 };
            double bestRun = 0.0, bestOff = 0.0;
            for (const double o : offsets) {
                const double run = evidencedRun(contrast, gx[size_t(i)] + o * nx,
                                                gy[size_t(i)] + o * ny, th, cfg, startWithin);
                if (run > bestRun) { bestRun = run; bestOff = o; }
            }
            if (bestRun >= credible) {
                lenOut[size_t(i)] = bestRun;
                lenSrc[size_t(i)] = DtlLenSrc::LatBand;
                lenLat[size_t(i)] = bestOff;
            }
        }
    }

    // ── (2) tiering, final (§5.9) ───────────────────────────────────────────
    // The ladder's whole job is that its three ABSENCES stay different answers:
    // END_ON means the geometry says nothing could be seen, OCCLUDED that the
    // anchor was not trustworthy, UNSEEN that we looked and found nothing.
    // Publishing them as one "no sample" is how a 0.95 coverage hides a 40 %
    // end-on swing.
    //
    // SEG is NOT produced here. The segment probe along the solved direction is
    // the one deferred item of this package; a half-built SEG that published a
    // terminus it had not earned would be worse than no SEG at all, so the tier
    // stays reachable only through dtlTierName and nothing emits it.
    out.samples.reserve(size_t(nf));
    int bandIdx = 0;
    for (int i = 0; i < nf; ++i) {
        DtlSample s;
        s.t_us    = tUs[size_t(i)];
        s.gripPx  = QPointF(gxOut[size_t(i)], gyOut[size_t(i)]);
        s.headPx  = QPointF(nan, nan);
        s.rhoPred = (i < int(state.rhoPred.size())) ? state.rhoPred[size_t(i)] : nan;
        s.rhoSrc  = (i < int(state.rhoSrc.size()))  ? state.rhoSrc[size_t(i)]  : DtlRhoSrc::None;
        if (i < int(state.corridorOn.size()) && state.corridorOn[size_t(i)]) {
            s.corrCentreDeg[0] = state.corrCentreADeg[size_t(i)];
            s.corrCentreDeg[1] = state.corrCentreBDeg[size_t(i)];
            s.corrHalfDeg      = state.corrHalfDeg[size_t(i)];
            s.corridorOn       = true;
            s.corridorEscape   = state.corridorEscape[size_t(i)] != 0;
        }
        while (bandIdx < int(out.bands.size()) && out.bands[size_t(bandIdx)].hi < i) ++bandIdx;
        const bool inBand = bandIdx < int(out.bands.size())
                            && out.bands[size_t(bandIdx)].lo <= i && i <= out.bands[size_t(bandIdx)].hi;
        s.band = inBand ? bandIdx : -1;

        if (!state.solved[size_t(i)]) {
            // Order matters: a quarantined frame is OCCLUDED even where the
            // schedule would also have called it end-on, because the anchor is the
            // thing that failed and the reason has to say so.
            const bool inSpan = i < int(state.inSpan.size()) && state.inSpan[size_t(i)];
            if (i < int(state.quarantined.size()) && state.quarantined[size_t(i)])
                s.tier = DtlTier::Occluded;
            // END-ON is a claim about the GEOMETRY, so only a frame we actually
            // looked at can make it. Outside the inherited span we did not look.
            else if (!inSpan) s.tier = DtlTier::Unseen;
            else if (!fin(s.rhoPred) || s.rhoPred < cfg.rhoSolveMin) s.tier = DtlTier::EndOn;
            else s.tier = DtlTier::Unseen;
            s.reason = (i < int(state.reason.size())) ? state.reason[size_t(i)] : QString();
            if (s.reason.isEmpty()) s.reason = QStringLiteral("not solved");
            out.samples.push_back(s);
            continue;
        }

        const int bi = solvedBin[size_t(i)];
        const double ev  = double(state.EV[size_t(i)][size_t(bi)]);
        const double sup = double(state.SUP[size_t(i)][size_t(bi)]);
        const double rev = double(state.EV[size_t(i)][size_t((bi + NS / 2) % NS)]);
        const bool vetoed = state.ARMVETO[size_t(i)][size_t(bi)] != 0;
        s.thetaRad = thetaOutDeg[size_t(i)] * kPi / 180.0;

        // ── the published run (§5.9, and the bloomed-ribbon defect) ──────────
        // The evidenced run measured off the re-registered line is the better
        // number on a frame where the contrast profile is a thin line. It is NOT
        // the better number on a bright BLURRED frame: at this exposure a moving
        // shaft is a bloomed ribbon 10–20 px wide — wider than the lateral
        // background offsets the thin-line profile credits against — so the run
        // reads evidence-FREE and breaks early. MEASURED: 0006 P3 published 326 px
        // from rEnd in c2 and 130 px from the snapped line in c3, with the shaft
        // visible to the top-left corner in both.
        //
        // So take whichever of the two is LONGER, and only where rEnd is a length
        // at all: below minLenPx + 10 it is ridgeSweep's own floor (rLo 8 +
        // minLenPx 90 = 98 px to the digit on 51–101 refused frames a swing)
        // wearing a length's clothes, and above D3's ceiling it is a ray that ran
        // past the club. lenSrc says which won, because a ladder that cannot name
        // the number it refused on is the defect the last iteration closed.
        const double rendPx = double(state.REND[size_t(i)][size_t(bi)]);
        const double lenCeil = (fin(state.lFullPx) && fin(s.rhoPred))
                                   ? cfg.len.slack * s.rhoPred * state.lFullPx
                                   : std::numeric_limits<double>::infinity();
        double    runPx = rendPx;
        DtlLenSrc lsrc  = DtlLenSrc::Rend;
        if (fin(lenOut[size_t(i)]) && lenOut[size_t(i)] > 0.0) {
            runPx = lenOut[size_t(i)];
            lsrc  = lenSrc[size_t(i)];
            if (fin(rendPx) && rendPx > runPx
                && rendPx >= double(cfg.ridge.minLenPx) + 10.0
                && rendPx <= lenCeil) {
                runPx = rendPx;
                lsrc  = DtlLenSrc::Rend;
            }
        }
        s.lenSrc = lsrc;
        runOut[size_t(i)] = runPx;

        // ── a length equal to the sweep's own floor is not a length ──────────
        // ridgeSweep searches its sqrt-normalised cumulative score's argmax only
        // from j0 = minLenPx / rStep onward, so its shortest possible terminus is
        // rLo + minLenPx — 98 px on the shipped constants — and a ray that leaves
        // the club early reports that number TO THE DIGIT. A frame published on
        // it has had NO length measured: the argmax is parked on its own lower
        // bound, and the number is an artefact of where the search starts rather
        // than of anything in the image.
        //
        // MEASURED, 06-11 (§8): the two P4 tiles on swings 0001 and 0007 that
        // publish at θ 200° and 292° on "98–100 px" — the forearm-lock class this
        // whole design exists to prevent — plus the stray mid-swing frames that
        // appeared when L̂_D moved and the short impact-band runs. They clear the
        // MINIMUM-LENGTH rule only because ρ̂_D is small where they happen, so
        // that rule cannot catch them: it asks whether the run is long enough for
        // the schedule, and this asks whether a run was measured at all.
        //
        // Two exemptions, and both are the same sentence the rest of the file
        // makes. Only where the number came from REND: a run measured off the
        // re-registered line that lands near 98 px is a measurement of a short
        // run, which D3 allows one-sided and the minimum-length rule judges on
        // its merits. And never against a BAND lock, which is a direct
        // measurement of the line and owes the ridge sweep nothing.
        const double sweepFloorPx = double(cfg.ridge.rLo) + double(cfg.ridge.minLenPx);
        const bool lenIsFloor = lsrc == DtlLenSrc::Rend && fin(runPx)
                                && runPx <= sweepFloorPx + cfg.len.floorSlackPx
                                && !(fin(lenOut[size_t(i)])
                                     && lenOut[size_t(i)] > sweepFloorPx + cfg.len.floorSlackPx);

        // ── D1's publication test, and this view's standing excuse ───────────
        // Face-on's attachment test assumes FREE SPACE behind the butt. Down the
        // line there is none: the lead arm is near-collinear with the shaft at
        // address and impact and the forearms are at P3/P5, on the opposite side
        // of the grip, so the reverse ray of a CORRECT direction runs up the arms
        // and "the reverse ray is as strong" is the NORMAL condition of a right
        // frame here. MEASURED: 126 in-span frames refused on it across the dev
        // six, four of them ladder tiles the montage review adjudicated right.
        // Waived where the reverse lies within cfg.rev.armDeg of an arm direction,
        // and at a ball-gated frame where DTL's own ball already decided the
        // direction. Where neither applies the test stands as it was written.
        double armDeg[4] = { 0, 0, 0, 0 };
        const int nArm = dtlArmDirsDeg(anchors, i, gx[size_t(i)], gy[size_t(i)],
                                       cfg.rev.armMinPx, armDeg);
        const double revDirDeg = thetaOutDeg[size_t(i)] + 180.0;
        bool revArm = false;
        for (int a = 0; a < nArm && !revArm; ++a)
            revArm = std::abs(shaftshared::circWrap(revDirDeg - armDeg[a])) <= cfg.rev.armDeg;
        const bool revBall = i < int(state.ballGate.size()) && state.ballGate[size_t(i)];
        const DtlRevWaiver waiver = revArm    ? DtlRevWaiver::Arm
                                  : revBall   ? DtlRevWaiver::BallGate
                                              : DtlRevWaiver::None;
        revWaived[size_t(i)] = waiver;

        const bool beatsReverse = ev > cfg.revRatio * rev || waiver != DtlRevWaiver::None;
        // The minimum-length rule: the visibility law says how long the club ought
        // to look on this frame, and a 40 px stub at a moment the schedule calls
        // sighted is a piece of something else. Applied only where L̂_D is known —
        // with no length prior a short run is not evidence of a fault.
        const double lenFloor = (fin(state.lFullPx) && fin(s.rhoPred))
                                    ? cfg.minLenFrac * s.rhoPred * state.lFullPx : 0.0;
        const bool longEnough = !(lenFloor > 0.0) || (fin(runPx) && runPx >= lenFloor);
        // ── the evidence gate, and the measurement that may stand in for it ──
        // EV is read along a ray from the POSE grip — the same off-axis ray
        // DtlLenSrc is a record of — so on a frame where the shaft is a plainly
        // visible bright streak on a black background it reads 0.37–0.40 against
        // the 0.45 gate (0007 P5, 0008 P3, 0008 P5, all adjudicated right by eye).
        // Where the SNAP WAS ACCEPTED there is a better statement about this frame
        // to hand: the support under the line the frame would publish, which the
        // snap already had to earn against its own origin line, the limb veto and
        // the ball gate. cfg.lineConfRay is the p10 of that number over the frames
        // that already published on EV, so this admits frames the published set
        // itself says are ordinary — it does not invent a new standard.
        const bool evOk   = ev >= cfg.evRay;
        const bool lcOk   = snapAcc[size_t(i)] && fin(snapBest[size_t(i)])
                            && snapBest[size_t(i)] >= cfg.lineConfRay;
        const bool rayOk = (evOk || lcOk) && sup >= cfg.supRay && beatsReverse && !vetoed
                           && longEnough && !lenIsFloor;
        // Ev unless the LINE is what let this frame through: an absence satisfied
        // neither gate and must not read as though it satisfied the second.
        s.evSrc = (rayOk && !evOk && lcOk) ? DtlEvSrc::LineConf : DtlEvSrc::Ev;
        evSrcOut[size_t(i)] = s.evSrc;

        if (bandLock[size_t(i)]) {
            s.tier   = DtlTier::Band;
            s.bandS  = double(state.band[size_t(i)].s);
            s.bandR0 = double(state.band[size_t(i)].r0);
            // BAND is the strongest thing there is and still never certain: 0.75
            // floor, evidence lifts it, 0.9 ceiling. Nothing in this view earns 1.
            s.conf   = float(std::clamp(0.75 + 0.15 * std::clamp(ev, 0.0, 1.0), 0.0, 0.90));
        } else if (rayOk) {
            s.tier = DtlTier::Ray;
            s.conf = float(std::clamp(0.55 * std::clamp(ev, 0.0, 1.0), 0.0, 0.90));
        } else {
            // A solved θ that earned no tier publishes NOTHING — the DP's path is
            // not a measurement, and §2's impact frames are what happens when the
            // tier follows the solve instead of the pixels.
            s.tier     = DtlTier::Unseen;
            s.thetaRad = dtl::kNan;
            s.gripPx   = QPointF(gx[size_t(i)], gy[size_t(i)]);   // unpublished ⇒ the pose anchor, unmoved
            s.reason   = vetoed        ? QStringLiteral("solved direction runs into a %1")
                                             .arg(QLatin1String(
                                                 dtlLimbName(int(state.ARMJOINT[size_t(i)][size_t(bi)]))))
                       : !beatsReverse ? QStringLiteral("reverse ray as strong (ev %1 vs %2)")
                                             .arg(ev, 0, 'f', 2).arg(rev, 0, 'f', 2)
                       : !(evOk || lcOk)
                                        ? (snapAcc[size_t(i)] && fin(snapBest[size_t(i)])
                                             ? QStringLiteral("no evidence (ev %1 < %2, lineConf %3 < %4)")
                                                   .arg(ev, 0, 'f', 2).arg(cfg.evRay, 0, 'f', 2)
                                                   .arg(snapBest[size_t(i)], 0, 'f', 2)
                                                   .arg(cfg.lineConfRay, 0, 'f', 2)
                                             : QStringLiteral("no evidence (ev %1 < %2, no accepted snap)")
                                                   .arg(ev, 0, 'f', 2).arg(cfg.evRay, 0, 'f', 2))
                       : sup < cfg.supRay ? QStringLiteral("no support (sup %1 < %2)")
                                             .arg(sup, 0, 'f', 2).arg(cfg.supRay, 0, 'f', 2)
                       : !longEnough  ? QStringLiteral("run %1 px (%2) is under %3 px of the %4 px "
                                                       "the schedule allows")
                                             .arg(runPx, 0, 'f', 0)
                                             .arg(QLatin1String(dtlLenSrcName(s.lenSrc)))
                                             .arg(lenFloor, 0, 'f', 0)
                                             .arg(s.rhoPred * state.lFullPx, 0, 'f', 0)
                                        // … and the length that is not a length at
                                        // all. Last, so a frame that is ALSO short
                                        // for the schedule is still refused on the
                                        // measurement it made.
                                        : QStringLiteral("length is the sweep's floor (%1 px), "
                                                         "not a measurement")
                                             .arg(runPx, 0, 'f', 0);
        }
        if (s.tier >= DtlTier::Ray && fin(runPx) && runPx > 0.0) {
            s.lenPx  = runPx;
            s.headPx = QPointF(s.gripPx.x() + runPx * std::cos(s.thetaRad),
                               s.gripPx.y() + runPx * std::sin(s.thetaRad));
        }
        out.samples.push_back(s);
    }

    // The two coverage numbers §5.9 insists on, and the pin it insists on
    // counting: how many samples were published in a frame the schedule put
    // END-ON. Should be 0 BY CONSTRUCTION (an end-on frame is never in a band);
    // counted anyway, because a construction nobody measures is a belief.
    int published = 0, inBandFrames = 0;
    for (const DtlSample& s : out.samples) {
        if (s.band >= 0) ++inBandFrames;
        if (s.tier >= DtlTier::Ray) {
            ++published;
            if (!fin(s.rhoPred) || s.rhoPred < cfg.rhoSolveMin) ++out.publishedInEndOn;
        }
    }
    const int denom = state.spanFrames > 0 ? state.spanFrames : nf;
    out.sightedFrac = double(inBandFrames) / double(denom);
    out.valid = !out.bands.empty() && published > 0;

    if (trace) {
        trace->thetaOutDeg = thetaOutDeg;
        trace->snapOffsetPx  = snapOff;
        trace->snapDThetaDeg = snapDth;
        trace->snapAccepted  = snapAcc;
        trace->snapBestConf   = snapBest;
        trace->snapOriginConf = snapOrig;
        trace->snapDrawnPx    = snapDrawn;
        trace->revWaived      = revWaived;
        trace->evSrc          = evSrcOut;
        trace->lenSrc.assign(size_t(nf), DtlLenSrc::Rend);
        trace->lenPx.assign(size_t(nf), nan);
        trace->lenLatOffsetPx = lenLat;
        trace->tier.assign(size_t(nf), int(DtlTier::Unseen));
        trace->band.assign(size_t(nf), -1);
        for (int i = 0; i < nf && i < int(out.samples.size()); ++i) {
            trace->tier[size_t(i)] = int(out.samples[size_t(i)].tier);
            trace->band[size_t(i)] = out.samples[size_t(i)].band;
            // The run the ladder was ASKED about, on every solved frame — not only
            // on the ones that published. A refused frame's length is the whole
            // subject of the minimum-length rule, so a trace that carried it only
            // on the survivors could not be used to argue about the rule.
            trace->lenSrc[size_t(i)] = out.samples[size_t(i)].lenSrc;
            if (state.solved[size_t(i)]) trace->lenPx[size_t(i)] = runOut[size_t(i)];
        }
    }

    // The witness is not read here: every inheritance it carries was consumed by
    // the solve, and re-reading it after the fact is how a tier ends up decided on
    // face-on's word (§5.9). `geom` is the segment probe's club geometry and waits
    // with the SEG tier above.
    (void)witness; (void)geom;
    return out;
}

} // namespace pinpoint::analysis
