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

// DTL deciding half, part 1 — the band solve (see dtl_shaft_decide.h).
//
// Mirrors decideTrack's SHAPE (decode-once cache, parallel per-frame evidence
// with per-index writes, emission → banded Viterbi) and none of its DECISIONS:
// no phase model, no C2 schedule, no ψ reconcile, no bridging. §2 records what
// those do when pointed at this view.

#include "dtl_shaft_decide.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "shaft_track_shared.h"   // circWrap / normScores / percentile / viterbiBanded

namespace pinpoint::analysis {
namespace {

using shaftshared::circWrap;

constexpr double kPi = 3.14159265358979323846;

inline bool fin(double v)            { return std::isfinite(v); }
inline bool finPt(const cv::Point2d& p) { return std::isfinite(p.x) && std::isfinite(p.y); }

// Up to `cap` evenly spaced entries of `src`, endpoints included. A median is a
// rank statistic: 25 well-spread frames say what 250 say, at a tenth of the cost.
std::vector<int> thinTo(const std::vector<int>& src, int cap)
{
    if (cap <= 0 || int(src.size()) <= cap) return src;
    std::vector<int> out;
    out.reserve(size_t(cap));
    for (int k = 0; k < cap; ++k)
        out.push_back(src[size_t(std::llround(double(k) * double(src.size() - 1) / double(cap - 1)))]);
    return out;
}

// Per-pixel median of a set of CV_8UC1 frames, as CV_32F. Per-row nth_element
// exactly as decideTrack's scene median: the k-th order statistic is unique, so
// the parallel result is byte-identical to the serial scan.
cv::Mat medianImage(const FrameSource& frameAt, const std::vector<int>& idx, bool parallel)
{
    std::vector<cv::Mat> bg;
    bg.reserve(idx.size());
    for (const int i : idx) { const cv::Mat g = frameAt(i); if (!g.empty()) bg.push_back(g); }
    if (bg.empty()) return {};
    const int H = bg[0].rows, W = bg[0].cols;
    const size_t n = bg.size();
    for (const cv::Mat& m : bg)
        if (m.rows != H || m.cols != W || m.type() != CV_8UC1) return {};
    cv::Mat med(H, W, CV_32F);
    const auto rows = [&](const cv::Range& rng) {
        std::vector<uchar> vals(n);
        for (int r = rng.start; r < rng.end; ++r) {
            float* orow = med.ptr<float>(r);
            for (int c = 0; c < W; ++c) {
                for (size_t k = 0; k < n; ++k) vals[k] = bg[k].ptr<uchar>(r)[c];
                std::nth_element(vals.begin(), vals.begin() + n / 2, vals.end());
                orow[c] = float(vals[n / 2]);
            }
        }
    };
    if (parallel) cv::parallel_for_(cv::Range(0, H), rows);
    else          rows(cv::Range(0, H));
    return med;
}

// Mean of a small disc, on a CV_32F image. The permanence probe: the ball is a
// bright thing that STAYS bright through the address hold and is GONE after
// impact; a mat marking is a bright thing that is still there.
double discMean(const cv::Mat& img32, double cx, double cy, double r)
{
    const int x0 = std::max(0, int(std::floor(cx - r))), x1 = std::min(img32.cols - 1, int(std::ceil(cx + r)));
    const int y0 = std::max(0, int(std::floor(cy - r))), y1 = std::min(img32.rows - 1, int(std::ceil(cy + r)));
    double sum = 0.0; int n = 0;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const double dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy <= r * r) { sum += double(img32.at<float>(y, x)); ++n; }
        }
    return n ? sum / n : std::numeric_limits<double>::quiet_NaN();
}

// Linear-interpolation percentile of a copy (type 7), as the assembly's.
double pctOf(std::vector<double> v, double p)
{
    if (v.empty()) return std::numeric_limits<double>::quiet_NaN();
    std::sort(v.begin(), v.end());
    const double pos = (p / 100.0) * double(v.size() - 1);
    const size_t lo = size_t(std::floor(pos));
    const size_t hi = std::min(lo + 1, v.size() - 1);
    return v[lo] + (pos - double(lo)) * (v[hi] - v[lo]);
}

// One evidence channel: the ridge sweep plus the S1 absolute floor. A channel
// whose RAW p97 misses the floor is DROWNED — zero scores and zero support — so
// it can neither win the DP nor bless a tier. normScores would otherwise rescale
// a frame of pure noise into a full-strength winner.
struct Channel {
    std::vector<float> norm, sup, rend;
    bool alive = false;
};
Channel sweepChannel(const cv::Mat& img32, double gx, double gy,
                     const std::vector<float>& gridRad, const RidgeConfig& rc,
                     bool brightOnly, double absFloor, int NS)
{
    Channel c;
    const RidgeResult r = ridgeSweep(img32, gx, gy, gridRad, rc, brightOnly);
    const float p97 = shaftshared::percentile(r.score, 97.0);
    if (absFloor > 0.0 && p97 < float(absFloor)) {
        c.norm.assign(size_t(NS), 0.f);
        c.sup.assign(size_t(NS), 0.f);
        c.rend.assign(size_t(NS), 0.f);
        return c;
    }
    c.norm = shaftshared::normScores(r.score);
    c.sup  = r.support;
    c.rend = r.rEnd;
    c.alive = true;
    return c;
}

} // namespace

// ── the DTL ball (§4.3, §5.5) ────────────────────────────────────────────────
DtlBall dtlFindBall(const FrameSource& frameAt,
                    const std::vector<int>& addrFramesIn,
                    const std::vector<int>& postFramesIn,
                    const DtlAnchors& anchors,
                    int frameW, int frameH)
{
    DtlBall out;
    const std::vector<int> addr = thinTo(addrFramesIn, 25);
    if (addr.size() < 3) { out.reason = QStringLiteral("no address hold to search"); return out; }

    const cv::Mat med32 = medianImage(frameAt, addr, false);
    if (med32.empty()) { out.reason = QStringLiteral("address-hold median unavailable"); return out; }
    cv::Mat med8; med32.convertTo(med8, CV_8U);

    // Threshold: bright in ABSOLUTE terms (230), and the 99.5th percentile is a
    // way DOWN from that on a dim clip — never a way up.
    //
    // MEASURED, 07-04, all six dev swings: the DTL frame carries a lit simulator
    // screen and a lit mat, so p99.5 of the address median is 254. max(230, p99.5)
    // therefore thresholds at 254 and throws away the ball, which images at 220–236
    // — the detector then reports "no bright compact blob" about the brightest
    // compact thing in the frame. The percentile is a FLOOR-lowering escape for a
    // clip where nothing reaches 230, which is the only job it can honestly do:
    // a relative-only threshold on a frame with no ball promotes the brightest
    // noise to a candidate.
    std::vector<double> all;
    all.reserve(size_t(med8.total()));
    for (int r = 0; r < med8.rows; ++r)
        for (int c = 0; c < med8.cols; ++c) all.push_back(double(med8.at<uchar>(r, c)));
    const double thr = std::min(230.0, pctOf(all, 99.5));

    // thr − 0.5 so a pixel AT the threshold passes: with a strict `>`, a clip
    // whose 99.5th percentile IS the ball's own level thresholds the ball away
    // and the detector reports "no bright blob" about the brightest thing in it.
    cv::Mat bin;
    cv::threshold(med8, bin, thr - 0.5, 255, cv::THRESH_BINARY);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);

    // The golfer's own geometry, in this view's sentence (§4.3): the ball is
    // BELOW the ankle line and FARTHER from the ankles, horizontally, than the
    // grip is, in the direction hips→grip. "Between the feet" is the face-on
    // sentence and §2 finding 5 is what porting it costs.
    double ankleY = 0, ankleX = 0, hipX = 0, gripX = 0;
    int nA = 0, nH = 0, nG = 0;
    for (const int i : addr) {
        if (i < int(anchors.joints.size()) && anchors.joints[size_t(i)].size() >= 8) {
            const cv::Point2d& la = anchors.joints[size_t(i)][6];
            const cv::Point2d& ra = anchors.joints[size_t(i)][7];
            if (finPt(la) && finPt(ra)) { ankleY += 0.5 * (la.y + ra.y); ankleX += 0.5 * (la.x + ra.x); ++nA; }
            const cv::Point2d& lh = anchors.joints[size_t(i)][2];
            const cv::Point2d& rh = anchors.joints[size_t(i)][3];
            if (finPt(lh) && finPt(rh)) { hipX += 0.5 * (lh.x + rh.x); ++nH; }
        }
        if (i < int(anchors.gx.size()) && fin(anchors.gx[size_t(i)])) { gripX += anchors.gx[size_t(i)]; ++nG; }
    }
    if (!nA || !nH || !nG) { out.reason = QStringLiteral("no address pose to anchor the ball prior"); return out; }
    ankleY /= nA; ankleX /= nA; hipX /= nH; gripX /= nG;
    const double dir = (gripX >= hipX) ? 1.0 : -1.0;          // hips → grip, horizontally
    const double gripOut = (gripX - ankleX) * dir;            // how far out the grip sits

    const double aMin = kPi * 4.0 * 4.0, aMax = kPi * 16.0 * 16.0;
    struct Cand { double x, y; };
    std::vector<Cand> cands;
    int nShape = 0;
    for (const auto& ct : contours) {
        const double A = cv::contourArea(ct);
        if (A < aMin || A > aMax) continue;
        const double P = cv::arcLength(ct, true);
        if (P <= 0.0) continue;
        if (4.0 * kPi * A / (P * P) < 0.6) continue;
        const cv::RotatedRect rr = cv::minAreaRect(ct);
        const double lo = std::min(rr.size.width, rr.size.height), hi = std::max(rr.size.width, rr.size.height);
        if (lo <= 0.0 || hi / lo > 2.0) continue;             // the alignment stick dies here
        ++nShape;
        const cv::Moments m = cv::moments(ct);
        if (m.m00 <= 0.0) continue;
        const double bx = m.m10 / m.m00, by = m.m01 / m.m00;
        if (by <= ankleY - 0.02 * frameH) continue;           // above the ankle line
        if ((bx - ankleX) * dir <= gripOut) continue;         // not beyond the grip, on the golfer's side
        cands.push_back({ bx, by });
    }
    if (cands.empty()) {
        out.reason = nShape ? QStringLiteral("%1 bright blob(s), none past the grip below the ankle line").arg(nShape)
                            : QStringLiteral("no bright compact blob in the address prior");
        return out;
    }

    // Permanence WITH LAUNCH. Both halves are load-bearing: a ball that flickers
    // through the hold was never locked, and a bright thing still there 120 ms
    // after impact is the mat, not the ball.
    // No post-impact frames ⇒ the launch half of the test cannot be run, and the
    // permanence half alone would accept any static white thing. Abstain, and say
    // that is what happened rather than reporting a shape verdict.
    const std::vector<int> post = thinTo(postFramesIn, 9);
    if (post.empty()) {
        out.reason = QStringLiteral("%1 candidate(s), but no post-impact frames to test launch against")
                         .arg(int(cands.size()));
        return out;
    }
    std::vector<Cand> kept;
    for (const Cand& c : cands) {
        std::vector<double> hold;
        for (const int i : addr) {
            const cv::Mat g = frameAt(i);
            if (g.empty()) continue;
            cv::Mat g32; g.convertTo(g32, CV_32F);
            const double v = discMean(g32, c.x, c.y, 6.0);
            if (fin(v)) hold.push_back(v);
        }
        if (hold.size() < 3) continue;
        const double holdMed = pctOf(hold, 50.0);
        if (holdMed <= 0.0) continue;
        // The PERMANENCE half, on the 25th percentile rather than the minimum.
        // The clubhead is BEHIND THE BALL at address and the golfer waggles, so
        // on the dev six the single darkest hold frame reads 88–173 against a
        // median of 219–236 — a min test refuses the real ball on every swing.
        // A quarter of the hold may have the head or its shadow over the ball;
        // three quarters may not (measured p25: 212–228 vs the 186–201 gate).
        if (pctOf(hold, 25.0) < 0.85 * holdMed) continue;
        std::vector<double> after;
        for (const int i : post) {
            const cv::Mat g = frameAt(i);
            if (g.empty()) continue;
            cv::Mat g32; g.convertTo(g32, CV_32F);
            const double v = discMean(g32, c.x, c.y, 6.0);
            if (fin(v)) after.push_back(v);
        }
        if (after.empty()) continue;
        if (pctOf(after, 50.0) >= 0.50 * holdMed) continue;    // it never left
        kept.push_back(c);
    }
    if (kept.size() == 1) {
        out.found = true; out.x = kept[0].x; out.y = kept[0].y;
        out.reason = QStringLiteral("address-stable, gone after impact");
        return out;
    }
    out.reason = kept.empty()
                     ? QStringLiteral("%1 candidate(s), none passed permanence-with-launch").arg(int(cands.size()))
                     : QStringLiteral("%1 candidates passed — ambiguous, no ball").arg(int(kept.size()));
    return out;
}

// ── the band solve ───────────────────────────────────────────────────────────
DtlSolveState dtlSolve(const FrameSource& frameAt,
                       const std::vector<int64_t>& tUs,
                       const DtlAnchors& anchors,
                       const FaceOnWitness* witness,
                       int frameW, int frameH, double fps,
                       const std::vector<double>& bandsMm, double clubLenMm,
                       const DtlShaftConfig& cfg,
                       DtlDecideTrace* trace)
{
    DtlSolveState st;
    const int nf = int(tUs.size());
    st.nf = nf;
    st.NS = (cfg.grid > 0.0) ? int(std::lround(360.0 / cfg.grid)) : 0;
    const int NS = st.NS;
    if (nf < 2 || NS < 8) return st;
    if (int(anchors.gx.size()) != nf || int(anchors.gy.size()) != nf) return st;

    const double nan = dtl::kNan;
    st.rhoPred.assign(size_t(nf), nan);
    st.thetaDeg.assign(size_t(nf), nan);
    st.solved.assign(size_t(nf), 0);
    st.quarantined.assign(size_t(nf), 0);
    st.sighted.assign(size_t(nf), 0);
    st.inSpan.assign(size_t(nf), 0);
    st.rowResid.assign(size_t(nf), nan);
    st.corrCentreADeg.assign(size_t(nf), nan);
    st.corrCentreBDeg.assign(size_t(nf), nan);
    st.corrHalfDeg.assign(size_t(nf), nan);
    st.corridorOn.assign(size_t(nf), 0);
    st.corridorEscape.assign(size_t(nf), 0);
    st.corrSignTaken.assign(size_t(nf), 0);
    st.reason.assign(size_t(nf), QString());
    st.band.assign(size_t(nf), BandMatch{});
    st.bandOk.assign(size_t(nf), 0);
    st.rhoSrc.assign(size_t(nf), DtlRhoSrc::None);
    st.ballGate.assign(size_t(nf), 0);
    st.thetaBallDeg.assign(size_t(nf), nan);
    st.EV.assign(size_t(nf), std::vector<float>(size_t(NS), 0.f));
    st.SUP.assign(size_t(nf), std::vector<float>(size_t(NS), 0.f));
    st.REND.assign(size_t(nf), std::vector<float>(size_t(NS), 0.f));
    st.ARMVETO.assign(size_t(nf), std::vector<char>(size_t(NS), 0));
    st.ARMJOINT.assign(size_t(nf), std::vector<signed char>(size_t(NS), -1));
    st.gridDeg.assign(size_t(NS), 0.f);
    std::vector<float> gridRad(size_t(NS), 0.f);
    for (int k = 0; k < NS; ++k) {
        st.gridDeg[size_t(k)] = float(k * cfg.grid);
        gridRad[size_t(k)]    = float(k * cfg.grid * kPi / 180.0);
    }

    const std::vector<double>& gx = anchors.gx;
    const std::vector<double>& gy = anchors.gy;

    // ── decode once ─────────────────────────────────────────────────────────
    // Same cap and the same rule as decideTrack / buildFrameCache: over it, every
    // pass falls back to the serial frameAt. Owned Mats are what make the evidence
    // loop below safe to run under cv::parallel_for_.
    const size_t cacheBytes = size_t(nf) * size_t(std::max(0, frameW)) * size_t(std::max(0, frameH));
    std::vector<cv::Mat> frameCache;
    if (cacheBytes > 0 && cacheBytes <= shaftshared::kFrameCacheCapBytes) {
        frameCache.assign(size_t(nf), cv::Mat());
        for (int i = 0; i < nf; ++i) frameCache[size_t(i)] = frameAt(i);
    }
    const bool parFrames = !frameCache.empty();
    const FrameSource frameSrc = [&](int i) -> cv::Mat {
        if (parFrames && i >= 0 && i < nf) return frameCache[size_t(i)];
        return frameAt(i);
    };
    std::vector<char> decodable(size_t(nf), 1);
    if (parFrames)
        for (int i = 0; i < nf; ++i) decodable[size_t(i)] = frameCache[size_t(i)].empty() ? 0 : 1;

    // ── (a) the witness, per DTL frame ──────────────────────────────────────
    // ρ̂_D = √(max(0, 1 − u_x²)) with u_x = ρ_F·cos θ_F. The clamp is not
    // defensive: a ρ_F that rounds above 1 makes the radicand negative and √ of it
    // is a NaN that LOOKS like the honest "face-on measured nothing" NaN and is
    // not. Both are treated as not-sighted, and isfinite is tested first.
    std::vector<char>   wok(size_t(nf), 0);
    std::vector<double> thetaF(size_t(nf), nan), rhoF(size_t(nf), nan), gripYF(size_t(nf), nan);
    std::vector<char>   wMeasured(size_t(nf), 0);
    for (int i = 0; i < nf; ++i) {
        if (!witness) continue;
        const FaceOnWitness::At a = witness->at(tUs[size_t(i)]);
        if (!a.ok) continue;
        wok[size_t(i)]   = 1;
        thetaF[size_t(i)] = a.thetaRad;
        rhoF[size_t(i)]   = a.rhoF;
        gripYF[size_t(i)] = a.gripYPx;
        // "Measured" for D4/D5 means a face-on sample that earned an angle from
        // face-on pixels: never a predicted or reconstructed one (§5.7 gate).
        wMeasured[size_t(i)] = (a.tier == FoTier::Ray || a.tier == FoTier::Band
                                || a.tier == FoTier::Wedge || a.tier == FoTier::Seg) ? 1 : 0;
        // ρ_F measured ⇒ the schedule as designed. ρ_F NaN with a finite θ_F ⇒ the
        // ρ_F := 1 BOUND (see DtlRhoSrc). Face-on does not MEASURE a club length at
        // address (it coasts) or at impact (it reconstructs), and those are the two
        // best-seen DTL moments of the swing; refusing them for want of a face-on
        // length threw away ~96 frames per swing — the whole address hold and the
        // impact zone — with "ρ̂ unknown" over frames where the club is sharp and in
        // plain view. The bound is CONSERVATIVE, not optimistic: ρ_F = 1 maximises
        // |u_x| = |cos θ_F| and so MINIMISES ρ̂_D = |sin θ_F|, which leaves a
        // near-horizontal face-on shaft (P2, the top, P6) reading END-ON exactly as
        // before and only admits the near-vertical ones. It moves the SCHEDULE and
        // nothing else: D4 and D5 still require a measured face-on tier below.
        if (fin(a.rhoF)) {
            const double ux = a.rhoF * std::cos(a.thetaRad);
            st.rhoPred[size_t(i)] = std::sqrt(std::max(0.0, 1.0 - ux * ux));
            st.rhoSrc[size_t(i)]  = DtlRhoSrc::Measured;
        } else if (fin(a.thetaRad)) {
            const double ux = std::cos(a.thetaRad);
            st.rhoPred[size_t(i)] = std::sqrt(std::max(0.0, 1.0 - ux * ux));
            st.rhoSrc[size_t(i)]  = DtlRhoSrc::Bound;
        }
    }

    // Inherited time (§5.3): the ladder, the impact instant. Never re-derived.
    int64_t tP1 = -1, tLast = -1, tP2 = -1, tP4 = -1, tP5 = -1;
    if (witness) {
        for (const auto& e : witness->ladder) {
            if (e.first == 1 && tP1 < 0) tP1 = e.second;
            if (e.first == 2 && tP2 < 0) tP2 = e.second;
            if (e.first == 4 && tP4 < 0) tP4 = e.second;
            if (e.first == 5 && tP5 < 0) tP5 = e.second;
            tLast = std::max(tLast, e.second);
        }
        if (tP1 < 0 && !witness->tUs.empty()) tP1 = witness->tUs.front();
        if (tLast < 0 && !witness->tUs.empty()) tLast = witness->tUs.back();
    }
    const int64_t impactUs = witness ? witness->impactUs : -1;
    int impf = -1;
    if (impactUs >= 0) {
        int64_t best = std::numeric_limits<int64_t>::max();
        for (int i = 0; i < nf; ++i) {
            const int64_t d = std::llabs(tUs[size_t(i)] - impactUs);
            if (d < best) { best = d; impf = i; }
        }
    }

    // ── (b) the cross-view anchor quarantine (§5.2) ─────────────────────────
    // Both cameras see vertical, so y_D ≈ a·y_F + b. Fitted over P1→impact, where
    // the hands are genuinely visible in both views; the residual afterwards is
    // what catches the post-impact invented hands, which hand confidence will not
    // (the wholebody model's hand confidence is known not to be trustworthy, and
    // it will not say so).
    {
        double sx = 0, sy = 0, sxx = 0, sxy = 0; int n = 0;
        for (int i = 0; i < nf; ++i) {
            const int64_t t = tUs[size_t(i)];
            if (tP1 >= 0 && t < tP1) continue;
            if (impactUs >= 0 && t > impactUs) continue;
            if (!fin(gripYF[size_t(i)]) || !fin(gy[size_t(i)])) continue;
            const double x = gripYF[size_t(i)], y = gy[size_t(i)];
            sx += x; sy += y; sxx += x * x; sxy += x * y; ++n;
        }
        if (n >= 4) {
            const double den = double(n) * sxx - sx * sx;
            if (std::abs(den) > 1e-9) {
                st.rowFitA = (double(n) * sxy - sx * sy) / den;
                st.rowFitB = (sy - st.rowFitA * sx) / double(n);
            }
        }
    }
    for (int i = 0; i < nf; ++i) {
        if (fin(st.rowFitA) && fin(gripYF[size_t(i)]) && fin(gy[size_t(i)]))
            st.rowResid[size_t(i)] = gy[size_t(i)] - (st.rowFitA * gripYF[size_t(i)] + st.rowFitB);
        const double r = st.rowResid[size_t(i)];
        const bool noGrip = !fin(gx[size_t(i)]) || !fin(gy[size_t(i)]);
        // An EMPTY wrist column means the caller supplied none (a pose-free
        // harness); a SHORT or NaN one means the pose had no confident wrist on
        // this frame, which is the condition the quarantine is for.
        const bool haveWrists = !anchors.leadWrist.empty() || !anchors.trailWrist.empty();
        const bool noWrist = haveWrists
                             && (i >= int(anchors.leadWrist.size())  || !finPt(anchors.leadWrist[size_t(i)])
                              || i >= int(anchors.trailWrist.size()) || !finPt(anchors.trailWrist[size_t(i)]));
        const bool wide = fin(r) && std::abs(r) > cfg.quarantine.absPx;
        const bool late = impactUs >= 0 && tUs[size_t(i)] > impactUs + cfg.quarantine.postImpactUs
                          && fin(r) && std::abs(r) > cfg.quarantine.postAbsPx;
        const bool pre = i < int(anchors.quarantined.size()) && anchors.quarantined[size_t(i)];
        if (noGrip || noWrist || wide || late || pre) {
            st.quarantined[size_t(i)] = 1;
            st.reason[size_t(i)] =
                noGrip  ? QStringLiteral("anchor quarantined: no grip")
              : noWrist ? QStringLiteral("anchor quarantined: wrist keypoint unconfident")
              : (wide || late)
                        ? QStringLiteral("anchor quarantined: row residual %1 px").arg(std::abs(r), 0, 'f', 0)
                        : QStringLiteral("anchor quarantined");
        }
    }

    // ── (c0) the frame sets the plate and the ball are built from (§5.4) ────
    // Derived BEFORE the sighted bands because the ball is now an input to them:
    // the still-club frames are gated toward grip→ball, and where there is no ball
    // they are not solved at all. The club is above the waist through P2½→P5, so
    // the low rows are clean there; the upper frame is clean during the address
    // hold. It is only a choice of WHICH FRAMES enter a median.
    std::vector<int> addrFrames, lowFrames, allFrames, postFrames;
    for (int i = 0; i < nf; ++i) {
        if (!decodable[size_t(i)]) continue;
        allFrames.push_back(i);
        const int64_t t = tUs[size_t(i)];
        if (tP1 >= 0 && t < tP1 - 100000) addrFrames.push_back(i);
        if (impactUs >= 0 && t > impactUs + 120000) postFrames.push_back(i);
        if (tP2 >= 0 && tP4 > tP2 && tP5 > 0) {
            const int64_t lo = tP2 + int64_t(0.4 * double(tP4 - tP2));
            if (t >= lo && t <= tP5) lowFrames.push_back(i);
        }
    }

    // ── (c1) the DTL ball, then L̂_D ────────────────────────────────────────
    if (witness) {
        st.ball = dtlFindBall(frameSrc, addrFrames, postFrames, anchors, frameW, frameH);
    } else {
        st.ball.reason = QStringLiteral("truth-only run: no face-on ladder, no ball search");
    }
    st.lFullSource = QStringLiteral("none");
    if (st.ball.found) {
        // |grip − ball| at address IS the projected club, so dividing by the
        // address ρ̂_D recovers the full DTL length. Both are medians over the
        // hold — one frame of either is a measurement of the pose jitter.
        std::vector<double> dl, dr;
        for (const int i : addrFrames) {
            if (fin(gx[size_t(i)]) && fin(gy[size_t(i)]))
                dl.push_back(std::hypot(st.ball.x - gx[size_t(i)], st.ball.y - gy[size_t(i)]));
            if (fin(st.rhoPred[size_t(i)])) dr.push_back(st.rhoPred[size_t(i)]);
        }
        if (!dl.empty()) {
            const double lenAddr = pctOf(dl, 50.0);
            const double rhoAddr = dr.empty() ? 1.0 : std::max(pctOf(dr, 50.0), cfg.rhoSolveMin);
            st.lFullPx = lenAddr / rhoAddr;
            st.lFullSource = QStringLiteral("ball");
        }
    }
    if (!fin(st.lFullPx) && witness && witness->fullLenPx > 0.0 && fin(st.rowFitA) && st.rowFitA > 0.0) {
        // Fallback: the face-on full length carried across by the fitted
        // cross-view ROW scale. Both cameras see vertical, so `a` is the only
        // scale the two views share without a calibration.
        st.lFullPx = witness->fullLenPx * st.rowFitA;
        st.lFullSource = QStringLiteral("faceOnRowScale");
    }

    // ── (c2) the still-club frames (§4.3, and face-on's as-built address rule)
    // The club is AT THE BALL in the address hold and at impact, and in this view
    // grip→ball is a direct DTL measurement — the one prior that owes face-on
    // nothing. Only the TIMING is inherited (before P1 + 30 ms, ±20 ms of the
    // face-on impact instant); the angle is DTL's own. MEASURED on the dev six:
    // without this the tracker publishes θ ≈ 113–132° at address — down-LEFT,
    // along the trail leg to the feet — where the truth is 58–62°, because the
    // leg is a longer, higher-contrast ray out of the hands than the shaft and
    // the shared percentile normalisation ties them at EV ≈ 1.
    //
    // … and the address hold ENDS when the club leaves the ball, not 30 ms after
    // P1. MEASURED on swings 0005 and 0007: the frame after the 30 ms window closes
    // the solve jumps to the trouser/shin edge at 103–132° while the truth has the
    // club still within 5° of the ball line for ~190 ms — a slow one-piece
    // takeaway. P1 + 30 ms is face-on's instant for "the takeaway has begun", which
    // is a claim about the HANDS; the gate wants "the head has left the ball". So
    // the hold runs forward from P1 and releases at the first frame the face-on
    // witness says has moved — |θ_F(t) − θ_F(P1)| above stillDeg — or at
    // stillMaxUs, whichever comes first. θ_F is inherited state, exactly as the
    // ladder instants are; the gate's DIRECTION is still DTL's own grip→ball.
    // A frame with no usable witness ends the hold rather than extending it on
    // nothing, which leaves it under the P1 + 30 ms rule it had before.
    int64_t stillEndUs = (tP1 >= 0) ? tP1 + 30000 : std::numeric_limits<int64_t>::min();
    if (witness && tP1 >= 0) {
        const FaceOnWitness::At aP1 = witness->at(tP1);
        if (aP1.ok && fin(aP1.thetaRad)) {
            const double thP1Deg = aP1.thetaRad * 180.0 / kPi;
            for (int i = 0; i < nf; ++i) {
                const int64_t t = tUs[size_t(i)];
                if (t < tP1) continue;
                if (t > tP1 + cfg.ball.stillMaxUs) break;
                if (!wok[size_t(i)] || !fin(thetaF[size_t(i)])) break;
                if (std::abs(circWrap(thetaF[size_t(i)] * 180.0 / kPi - thP1Deg)) > cfg.ball.stillDeg)
                    break;
                stillEndUs = std::max(stillEndUs, t + 1);
            }
        }
    }
    for (int i = 0; i < nf; ++i) {
        if (!witness) continue;
        const int64_t t = tUs[size_t(i)];
        const bool atAddr = tP1 >= 0 && t < stillEndUs;
        const bool atImp  = impactUs >= 0 && std::llabs(t - impactUs) <= 20000;
        if (!atAddr && !atImp) continue;
        st.ballGate[size_t(i)] = 1;
        if (st.ball.found && fin(gx[size_t(i)]) && fin(gy[size_t(i)]))
            st.thetaBallDeg[size_t(i)] =
                std::atan2(st.ball.y - gy[size_t(i)], st.ball.x - gx[size_t(i)]) * 180.0 / kPi;
    }

    // ── (c) sighted bands (§5.8) ────────────────────────────────────────────
    const int64_t spanLo = (witness && tP1 >= 0) ? tP1 - 600000 : std::numeric_limits<int64_t>::min();
    const int64_t spanHi = (witness && tLast >= 0) ? tLast + 150000 : std::numeric_limits<int64_t>::max();
    st.spanFrames = 0;
    for (int i = 0; i < nf; ++i) {
        const int64_t t = tUs[size_t(i)];
        const bool inSpan = t >= spanLo && t <= spanHi;
        st.inSpan[size_t(i)] = inSpan ? 1 : 0;
        if (inSpan) ++st.spanFrames;
        if (st.quarantined[size_t(i)]) continue;
        if (!inSpan) { st.reason[size_t(i)] = QStringLiteral("outside the inherited swing span"); continue; }
        if (!decodable[size_t(i)]) { st.reason[size_t(i)] = QStringLiteral("frame undecodable"); continue; }
        const double rho = st.rhoPred[size_t(i)];
        if (cfg.schedule.enabled && witness) {
            if (!fin(rho)) {
                st.reason[size_t(i)] = QStringLiteral("no face-on club length here (ρ̂ unknown)");
                continue;
            }
            if (rho < cfg.rhoSolveMin) {
                st.reason[size_t(i)] = QStringLiteral("end-on ρ̂=%1").arg(rho, 0, 'f', 2);
                continue;
            }
        }
        // The still club with no ball to point at is NOT solved. Face-on's rule is
        // "probe address toward the ball, not along a clamp; no ball ⇒ not probed",
        // and this is the same sentence in this view: the alternative is a solve
        // whose only strong candidate is the leg line, published blind.
        if (st.ballGate[size_t(i)] && !st.ball.found) {
            st.reason[size_t(i)] = QStringLiteral("no ball witness at address");
            continue;
        }
        st.sighted[size_t(i)] = 1;
    }
    // Maximal runs, minBandFrames or longer. A shorter run is a flicker at an
    // end-on edge, and a Viterbi over it is a per-frame pick in disguise.
    for (int i = 0; i < nf; ) {
        if (!st.sighted[size_t(i)]) { ++i; continue; }
        int j = i;
        while (j + 1 < nf && st.sighted[size_t(j + 1)]) ++j;
        if (j - i + 1 < cfg.minBandFrames) {
            for (int k = i; k <= j; ++k) {
                st.sighted[size_t(k)] = 0;
                st.reason[size_t(k)] = QStringLiteral("sighted run of %1 frames is shorter than %2")
                                           .arg(j - i + 1).arg(cfg.minBandFrames);
            }
        } else {
            DtlBand b;
            b.lo = i; b.hi = j; b.loUs = tUs[size_t(i)]; b.hiUs = tUs[size_t(j)];
            st.bands.push_back(b);
        }
        i = j + 1;
    }
    // Names: the nearest ladder P at each end, interpolated between entries.
    // Never gated on — they exist so a report row can be read without a clock.
    {
        std::vector<std::pair<int, int64_t>> lad;
        if (witness) lad = witness->ladder;
        std::sort(lad.begin(), lad.end(),
                  [](const std::pair<int, int64_t>& a, const std::pair<int, int64_t>& b) {
                      return a.second < b.second;
                  });
        const auto pName = [&](int64_t t) -> QString {
            if (lad.empty()) return QStringLiteral("t%1").arg(qlonglong(t / 1000));
            if (t <= lad.front().second) return QStringLiteral("addr");
            if (t >= lad.back().second)  return QStringLiteral("fin");
            for (size_t k = 1; k < lad.size(); ++k) {
                if (t <= lad[k].second) {
                    const double span = double(lad[k].second - lad[k - 1].second);
                    const double f = span > 0.0 ? double(t - lad[k - 1].second) / span : 0.0;
                    return QStringLiteral("P%1").arg(double(lad[k - 1].first)
                                                     + f * double(lad[k].first - lad[k - 1].first), 0, 'f', 1);
                }
            }
            return QStringLiteral("fin");
        };
        for (DtlBand& b : st.bands) b.name = pName(b.loUs) + QStringLiteral("→") + pName(b.hiUs);
    }

    // ── (d) the phase-aware clean plate (§5.4) ──────────────────────────────
    // The one place the face-on coupling touches EVIDENCE rather than the
    // decision, and the place the research record's permanence-snapshot error
    // (§17.2: "contained the address club and therefore silently vetoed the whole
    // downswing") gets made again. Its frame sets are (c0)'s.
    cv::Mat plate;
    double rowSplit = 0.0;
    if (!witness) {
        // No witness ⇒ no schedule to build a phase-aware plate from. A whole-clip
        // median CONTAINS THE ADDRESS CLUB (the golfer stands at address for half
        // the clip, and the club returns to within 3° of that line at impact), so
        // the motion channel is blind exactly where it would matter. Said out loud
        // rather than hidden, because this is the truth-only mode's known cost.
        plate = medianImage(frameSrc, thinTo(allFrames, cfg.plateMaxFrames), parFrames);
    } else {
        std::vector<double> gyA;
        for (const int i : addrFrames) if (fin(gy[size_t(i)])) gyA.push_back(gy[size_t(i)]);
        rowSplit = gyA.empty() ? 0.5 * frameH : pctOf(gyA, 50.0) - 40.0;
        const cv::Mat hi = medianImage(frameSrc, thinTo(addrFrames, cfg.plateMaxFrames), parFrames);
        const cv::Mat lo = medianImage(frameSrc, thinTo(lowFrames, cfg.plateMaxFrames), parFrames);
        if (hi.empty() && lo.empty()) {
            // no plate: neither phase window had frames — the motion channel is
            // simply absent and the other two speak for every frame
        } else if (lo.empty()) {
            plate = hi;
        } else if (hi.empty()) {
            plate = lo;
        } else {
            plate = hi.clone();
            const int r0 = std::clamp(int(std::lround(rowSplit)), 0, plate.rows);
            if (r0 < plate.rows) lo.rowRange(r0, plate.rows).copyTo(plate.rowRange(r0, plate.rows));
        }
    }
    // After impact + 100 ms the simulator screen is redrawing a ball-flight
    // animation. The screen ROWS are not knowable a priori, so the WHOLE plate is
    // invalid there and those frames run on the raw-derived channels alone.
    const int64_t plateValidUntil = (impactUs >= 0) ? impactUs + 100000
                                                    : std::numeric_limits<int64_t>::max();
    st.platesNote = plate;

    // ── (e)(f) evidence + emission, per sighted frame ───────────────────────
    std::vector<std::vector<float>> emis(size_t(nf), std::vector<float>(size_t(NS), float(cfg.wE2)));
    const double rmax = 0.62 * frameH;
    const double difFloor = cfg.evAbsFloorDif >= 0.0 ? cfg.evAbsFloorDif : cfg.evAbsFloor;
    std::vector<char> armVetoAny(size_t(nf), 0);
    std::vector<double> d3Cost(size_t(nf), 0.0);

    const auto body = [&](int i) {
        if (!st.sighted[size_t(i)]) return;
        const cv::Mat g8 = frameSrc(i);
        if (g8.empty()) return;
        cv::Mat g32; g8.convertTo(g32, CV_32F);
        const double GX = gx[size_t(i)], GY = gy[size_t(i)];

        // Channel 1 — MOTION: |frame − clean plate|, the instrument over the lit
        // screen where bare steel has no raw contrast at all.
        Channel mo;
        const bool plateOk = !plate.empty() && plate.size() == g32.size()
                             && tUs[size_t(i)] <= plateValidUntil;
        if (plateOk) {
            cv::Mat d; cv::absdiff(g32, plate, d);
            mo = sweepChannel(d, GX, GY, gridRad, cfg.ridge, true, difFloor, NS);
        }
        // Channel 2 — CONTRAST: |frame − boxblur(frame)|, POLARITY-FREE. This is
        // the channel that defeats the black/white band cancellation: a signed
        // bright-ridge response sums to nothing along an alternating shaft, and
        // the wide bright forearm wins on a frame where the shaft is plainly
        // visible. Measured: 37–48 grey levels along the true shaft over the
        // screen and the mat, 1–8 on a control line.
        Channel co;
        {
            const int ksz = std::max(3, cfg.contrastKsz | 1);
            cv::Mat blur, d;
            cv::blur(g32, blur, cv::Size(ksz, ksz));
            cv::absdiff(g32, blur, d);
            co = sweepChannel(d, GX, GY, gridRad, cfg.ridge, true, difFloor, NS);
        }
        // Channel 3 — RAW, signed, exactly as face-on runs it.
        const Channel rw = sweepChannel(g32, GX, GY, gridRad, cfg.ridge, false, cfg.evAbsFloor, NS);

        std::vector<float>& EV = st.EV[size_t(i)];
        std::vector<float>& SUP = st.SUP[size_t(i)];
        std::vector<float>& REND = st.REND[size_t(i)];
        const Channel* chans[3] = { &mo, &co, &rw };
        for (int k = 0; k < NS; ++k) {
            float bestEv = 0.f; const Channel* win = nullptr; float sup = 0.f;
            for (const Channel* c : chans) {
                if (c->norm.empty()) continue;
                if (c->norm[size_t(k)] > bestEv || !win) { bestEv = c->norm[size_t(k)]; win = c; }
                sup = std::max(sup, c->sup[size_t(k)]);
            }
            EV[size_t(k)]   = bestEv;
            SUP[size_t(k)]  = sup;
            REND[size_t(k)] = win ? win->rend[size_t(k)] : 0.f;
        }

        // E1. Not expected to lock often in this view — the DTL camera has no ring
        // light, so the bands are ordinary white paint between black tape rather
        // than saturated retro-blobs — but a lock is the strongest thing there is,
        // so it is always asked. Same r0 gate as face-on.
        const BandMatch bm = frameBandMatch(g8, GX, GY, rmax, bandsMm, cfg.band);
        if (bm.ok && bm.r0 > 0.0f && bm.r0 <= 260.0f) { st.band[size_t(i)] = bm; st.bandOk[size_t(i)] = 1; }

        std::vector<float>& em = emis[size_t(i)];
        for (int k = 0; k < NS; ++k) em[size_t(k)] = float(cfg.wE2 * (1.0 - double(EV[size_t(k)])));

        // D1 reverse ray — strong evidence out of the grip the OTHER way is a
        // scene line, not a club that terminates at the hands.
        //
        // … EXCEPT where this view has no free space behind the butt. Face-on's
        // C1 assumes there is some; down the line the lead arm is near-collinear
        // with the shaft at address and at impact and the forearms are at P3/P5,
        // on the OPPOSITE side of the grip, so the reverse ray of a CORRECT
        // direction runs up the golfer's own arm. The test is waived where the
        // reverse direction lies within cfg.rev.armDeg of grip→elbow or
        // grip→shoulder of either arm, and at a ball-gated frame, where the
        // direction has already been decided by DTL's own ball. Everywhere else
        // it stands exactly as it did.
        double armDeg[4] = { 0, 0, 0, 0 };
        const int nArm = dtlArmDirsDeg(anchors, i, GX, GY, cfg.rev.armMinPx, armDeg);
        const bool revWaiveBall = st.ballGate[size_t(i)] != 0;
        for (int k = 0; k < NS; ++k) {
            if (double(EV[size_t((k + NS / 2) % NS)]) <= cfg.rev.tol) continue;
            if (revWaiveBall) continue;
            const double revDeg = double(st.gridDeg[size_t(k)]) + 180.0;
            bool waived = false;
            for (int a = 0; a < nArm && !waived; ++a)
                waived = std::abs(circWrap(revDeg - armDeg[a])) <= cfg.rev.armDeg;
            if (waived) continue;
            em[size_t(k)] += float(cfg.rev.wRev);
        }

        // D2 limb veto — BOTH arms AND every limb joint below the shoulders, ALL
        // phases. The lateral test is the discriminator, not the angle: the
        // counterfeit runs along the limb AND close to its joint; the shaft only
        // ever does the first. Generalised past the elbows because the measured
        // address failure is the TROUSER LINE from the hands down the trail leg
        // to the feet — a ray D2-on-elbows says nothing about. Shoulders and head
        // are deliberately absent: at P3 the true shaft passes near them.
        std::vector<char>& veto = st.ARMVETO[size_t(i)];
        std::vector<signed char>& vjoint = st.ARMJOINT[size_t(i)];
        const auto jointAt = [&](int slot) -> cv::Point2d {
            if (slot == 0) return (i < int(anchors.leadElbow.size()))
                                      ? anchors.leadElbow[size_t(i)] : cv::Point2d(dtl::kNan, dtl::kNan);
            if (slot == 1) return (i < int(anchors.trailElbow.size()))
                                      ? anchors.trailElbow[size_t(i)] : cv::Point2d(dtl::kNan, dtl::kNan);
            // 2..7 are hips, knees, ankles in the shared 8-joint skeleton, whose
            // first two entries are the shoulders — skipped, see above.
            if (i < int(anchors.joints.size()) && anchors.joints[size_t(i)].size() >= 8)
                return anchors.joints[size_t(i)][size_t(slot)];
            return { dtl::kNan, dtl::kNan };
        };
        for (int a = 0; a < 8; ++a) {
            const cv::Point2d J = jointAt(a);
            if (!finPt(J)) continue;
            const double ex = J.x - GX, ey = J.y - GY;
            // A joint nearer than this is not a limb the ray runs ALONG: grip→J is
            // pose jitter at that range and the veto would refuse real directions.
            if (std::hypot(ex, ey) <= cfg.arm.minJointPx) continue;
            const double aDeg = std::atan2(ey, ex) * 180.0 / kPi;
            for (int k = 0; k < NS; ++k) {
                if (std::abs(circWrap(double(st.gridDeg[size_t(k)]) - aDeg)) >= cfg.arm.vetoDeg) continue;
                const double ux = std::cos(double(gridRad[size_t(k)])), uy = std::sin(double(gridRad[size_t(k)]));
                const double along = ex * ux + ey * uy;
                if (along <= 0.0) continue;                       // the joint is behind the ray
                const double lat = std::abs(ex * uy - ey * ux);   // ⊥ distance joint→ray
                if (lat > cfg.arm.latPx) continue;
                // The cost is added ONCE per θ however many joints of one limb line
                // up behind it (hip, knee and ankle all do at address): a triple
                // charge would make the leg a hard gate and hide what one veto is
                // worth. The FIRST joint to fire is the one named — slot order runs
                // arms, hips, knees, ankles, i.e. nearest-limb first.
                if (!veto[size_t(k)]) {
                    em[size_t(k)] += float(cfg.arm.wArm);
                    veto[size_t(k)] = 1;
                    vjoint[size_t(k)] = static_cast<signed char>(a);
                    armVetoAny[size_t(i)] = 1;
                }
            }
        }

        // D3 over-length, ONE-SIDED. A run LONGER than the visibility law allows
        // is a forearm or a scene line; a SHORT run is always allowed (occlusion,
        // dim steel — the markerless length-gate lesson).
        const double rho = st.rhoPred[size_t(i)];
        if (fin(st.lFullPx) && fin(rho)) {
            const double cap = cfg.len.slack * rho * st.lFullPx;
            for (int k = 0; k < NS; ++k)
                if (double(REND[size_t(k)]) > cap) { em[size_t(k)] += float(cfg.len.wLen); d3Cost[size_t(i)] = cfg.len.wLen; }
        }

        // D4 half-plane — (b) of §4.1: up in face-on is up down-the-line, because
        // both images share the vertical axis and y is down in both. Structurally
        // halves the circle wherever face-on is measured.
        if (wok[size_t(i)] && wMeasured[size_t(i)] && fin(thetaF[size_t(i)])) {
            const double sF = std::sin(thetaF[size_t(i)]);
            if (std::abs(sF) > 0.25) {
                const double want = (sF > 0.0) ? 1.0 : -1.0;
                for (int k = 0; k < NS; ++k) {
                    const double sk = std::sin(double(gridRad[size_t(k)]));
                    if (sk * want < 0.0) em[size_t(k)] += float(cfg.half.wHalf);
                }
            }
        }

        // D5 the face-on corridor. Two centres, because the depth SIGN is not
        // established (§4.2); the cost is the min over the two, and the ceiling is
        // deliberately below wE2 so clean evidence outside can still win — and be
        // logged, because an escape is either an off-plane swing or a face-on
        // error, and both are worth a frame number.
        const bool corrGate = cfg.corridor.enabled && wok[size_t(i)] && wMeasured[size_t(i)]
                              && fin(rhoF[size_t(i)]) && rhoF[size_t(i)] <= cfg.corridor.rhoFMax
                              && (tLast < 0 || tUs[size_t(i)] <= tLast);
        if (corrGate) {
            const double rF = rhoF[size_t(i)];
            const double yC = rF * std::sin(thetaF[size_t(i)]);
            const double xC = std::sqrt(std::max(0.0, 1.0 - rF * rF));
            const double cA = std::atan2(yC, -xC) * 180.0 / kPi;
            const double cB = std::atan2(yC,  xC) * 180.0 / kPi;
            st.corrCentreADeg[size_t(i)] = cA;
            st.corrCentreBDeg[size_t(i)] = cB;
            st.corrHalfDeg[size_t(i)]    = cfg.corridor.w0Deg;
            st.corridorOn[size_t(i)]     = 1;
            const double w0 = std::max(1.0, cfg.corridor.w0Deg);
            for (int k = 0; k < NS; ++k) {
                const double d = std::min(std::abs(circWrap(double(st.gridDeg[size_t(k)]) - cA)),
                                          std::abs(circWrap(double(st.gridDeg[size_t(k)]) - cB)));
                if (d <= w0) continue;
                const double u = (d - w0) / w0;
                em[size_t(k)] += float(std::min(cfg.corridor.wCorr, cfg.corridor.wCorr * u * u));
            }
        }

        // D6 the DTL ball — a soft well at grip→ball where the head IS at the
        // ball. A prior, never a pin: shallow, Gaussian, additive, and absent when
        // there is no ball.
        const bool atAddr = tP1 >= 0 && tUs[size_t(i)] < tP1;
        const bool atImp  = impf >= 0 && std::abs(i - impf) <= 2;
        if (st.ball.found && (atAddr || atImp)) {
            const double tb = std::atan2(st.ball.y - GY, st.ball.x - GX) * 180.0 / kPi;
            const double sig = std::max(1.0, cfg.ball.sigmaDeg);
            for (int k = 0; k < NS; ++k) {
                const double d = circWrap(double(st.gridDeg[size_t(k)]) - tb) / sig;
                em[size_t(k)] -= float(cfg.ball.wBall * std::exp(-0.5 * d * d));
            }
        }

        // D6b the BALL GATE at the still club. D6 above is a 4-deep well against a
        // tie; this is the gate that actually decides. It applies only where the
        // club is KNOWN to be at the ball — the address hold and ±20 ms of impact
        // (timing inherited, angle DTL's own) — and it is expressed as a COST so
        // the trace can show what it refused. Frames gated with no ball never get
        // here: they were refused in (c) and say so.
        if (st.ballGate[size_t(i)] && fin(st.thetaBallDeg[size_t(i)])) {
            const double tb = st.thetaBallDeg[size_t(i)];
            for (int k = 0; k < NS; ++k)
                if (std::abs(circWrap(double(st.gridDeg[size_t(k)]) - tb)) > cfg.ball.gateDeg)
                    em[size_t(k)] += float(cfg.ball.wGate);
        }

        // The band well LAST, as face-on: it must dominate every gate above.
        if (st.bandOk[size_t(i)]) {
            int bi = int(std::lround(double(st.band[size_t(i)].thetaDeg) / cfg.grid)) % NS;
            if (bi < 0) bi += NS;
            em[size_t(bi)] = float(-cfg.wBand);
        }
    };
    if (parFrames)
        cv::parallel_for_(cv::Range(0, nf), [&](const cv::Range& rng) {
            for (int i = rng.start; i < rng.end; ++i) body(i);
        });
    else
        for (int i = 0; i < nf; ++i) body(i);

    // ── (h) one Viterbi per band, independently ─────────────────────────────
    // Nothing connects the bands and no sample is emitted in a gap: in an end-on
    // gap θ_D is UNDEFINED, so there is nothing to bridge, and a global path
    // across one is the "flatter wrong branch across an evidence-free gap" failure
    // by construction. The rate bound is ω_base / ρ̂_D — θ_D legitimately moves
    // fast as the projection shortens toward a gap — with ρ̂_D FLOORED, because
    // inside a stub it is a difference of near-1 numbers and dividing by it is how
    // a band edge gets an unbounded transition.
    for (const DtlBand& b : st.bands) {
        const int n = b.hi - b.lo + 1;
        std::vector<std::vector<float>> sub;
        sub.reserve(size_t(n));
        for (int i = b.lo; i <= b.hi; ++i) sub.push_back(emis[size_t(i)]);
        std::vector<int> wmax(size_t(n), 1), sgn(size_t(n), 0);
        for (int k = 0; k < n; ++k) {
            const double rho = st.rhoPred[size_t(b.lo + k)];
            const double den = fin(rho) ? std::max(rho, cfg.rhoSolveMin) : 1.0;
            const int w = int(std::ceil((cfg.omegaBaseDegPerFrame / den) / std::max(1e-6, cfg.grid)));
            wmax[size_t(k)] = std::clamp(w, 1, NS / 2);
        }
        const DPResult dp = shaftshared::viterbiBanded(sub, wmax, sgn, cfg.kSmooth, cfg.grid);
        if (int(dp.thetaDeg.size()) != n) continue;
        for (int k = 0; k < n; ++k) {
            st.thetaDeg[size_t(b.lo + k)] = dp.thetaDeg[size_t(k)];
            st.solved[size_t(b.lo + k)]   = 1;
        }
    }

    // Which centre the evidence took, and whether it escaped the corridor. §4.2
    // does not claim the mid-band sign; this column is how it gets settled, or
    // does not, over a dev set.
    for (int i = 0; i < nf; ++i) {
        if (!st.solved[size_t(i)] || !st.corridorOn[size_t(i)]) continue;
        const double th = st.thetaDeg[size_t(i)];
        const double dA = std::abs(circWrap(th - st.corrCentreADeg[size_t(i)]));
        const double dB = std::abs(circWrap(th - st.corrCentreBDeg[size_t(i)]));
        st.corrSignTaken[size_t(i)] = (dA < dB) ? 1 : (dB < dA ? -1 : 0);
        st.corridorEscape[size_t(i)] = std::min(dA, dB) > st.corrHalfDeg[size_t(i)] ? 1 : 0;
    }

    // ── (i) the trace ───────────────────────────────────────────────────────
    if (trace) {
        trace->rhoPred        = st.rhoPred;
        trace->corrCentreADeg = st.corrCentreADeg;
        trace->corrCentreBDeg = st.corrCentreBDeg;
        trace->corrHalfDeg    = st.corrHalfDeg;
        trace->corridorOn     = st.corridorOn;
        trace->corridorEscape = st.corridorEscape;
        trace->corrSignTaken  = st.corrSignTaken;
        trace->rowResid       = st.rowResid;
        trace->quarantine     = st.quarantined;
        trace->reason         = st.reason;
        trace->d2ArmVeto      = armVetoAny;
        trace->d3LenCost      = d3Cost;
        trace->rhoSrc         = st.rhoSrc;
        trace->ballGate       = st.ballGate;
        trace->thetaBall      = st.thetaBallDeg;
        trace->thetaSolvedDeg = st.thetaDeg;
        trace->rowFitA        = st.rowFitA;
        trace->rowFitB        = st.rowFitB;
        trace->evAtTheta.assign(size_t(nf), nan);
        trace->supAtTheta.assign(size_t(nf), nan);
        trace->rEnd.assign(size_t(nf), nan);
        trace->limbVetoJoint.assign(size_t(nf), -1);
        trace->bandN.assign(size_t(nf), 0);
        trace->bandTheta.assign(size_t(nf), nan);
        trace->bandS.assign(size_t(nf), nan);
        trace->bandR0.assign(size_t(nf), nan);
        for (int i = 0; i < nf; ++i) {
            if (st.solved[size_t(i)]) {
                int bi = int(std::lround(st.thetaDeg[size_t(i)] / cfg.grid)) % NS;
                if (bi < 0) bi += NS;
                trace->evAtTheta[size_t(i)]  = double(st.EV[size_t(i)][size_t(bi)]);
                trace->supAtTheta[size_t(i)] = double(st.SUP[size_t(i)][size_t(bi)]);
                trace->rEnd[size_t(i)]       = double(st.REND[size_t(i)][size_t(bi)]);
                // The joint at the SOLVED θ, not "some joint fired somewhere":
                // which is the column a montage review can act on.
                trace->limbVetoJoint[size_t(i)] = int(st.ARMJOINT[size_t(i)][size_t(bi)]);
            }
            if (st.bandOk[size_t(i)]) {
                trace->bandN[size_t(i)]     = st.band[size_t(i)].n;
                trace->bandTheta[size_t(i)] = double(st.band[size_t(i)].thetaDeg);
                trace->bandS[size_t(i)]     = double(st.band[size_t(i)].s);
                trace->bandR0[size_t(i)]    = double(st.band[size_t(i)].r0);
            }
        }
    }
    // fps and clubLenMm are part of the shared decide signature; this view derives
    // its timebase from tUs and its length in PIXELS (L̂_D), so neither is read.
    (void)fps; (void)clubLenMm;
    return st;
}

} // namespace pinpoint::analysis
