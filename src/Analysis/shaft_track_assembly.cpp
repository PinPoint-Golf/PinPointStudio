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

// Shaft v3.0-r1 deciding half — faithful C++ port of tools/shaftlab/
// club_track_v3.py (the non-evidence stages: phase model, φ smoothing, C2
// geometry, per-frame DP emission, banded Viterbi, ψ-isotonic reconcile).
// scipy.ndimage.median_filter / gaussian_filter1d are reproduced with their
// default mode='reflect' so the smoothed φ / grip-speed / joints match numpy.

#include "shaft_track_assembly.h"

#include "analysis_tuning.h"       // pinpoint::analysis::tuning::apply

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace pinpoint::analysis {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kInf = 1e9;

inline double circWrap(double a) { return std::fmod(std::fmod(a + 180.0, 360.0) + 360.0, 360.0) - 180.0; }

// scipy 'reflect' boundary (…d c b a | a b c d | d c b a…): edge sample dup'd.
inline int reflectIdx(int i, int n)
{
    if (n == 1) return 0;
    while (i < 0 || i >= n) {
        if (i < 0) i = -1 - i;
        if (i >= n) i = 2 * n - 1 - i;
    }
    return i;
}

// scipy.ndimage.median_filter(x, size) — odd size, mode='reflect', origin 0.
std::vector<double> medianFilter1d(const std::vector<double>& x, int size)
{
    const int n = int(x.size());
    const int r = size / 2;
    std::vector<double> out(n);
    std::vector<double> win(size);
    for (int i = 0; i < n; ++i) {
        for (int k = -r; k <= r; ++k) win[k + r] = x[reflectIdx(i + k, n)];
        std::nth_element(win.begin(), win.begin() + r, win.end());
        out[i] = win[r];   // odd size ⇒ middle element is the median
    }
    return out;
}

// scipy.ndimage.gaussian_filter1d(x, sigma, truncate=4.0), mode='reflect'.
std::vector<double> gaussianFilter1d(const std::vector<double>& x, double sigma)
{
    const int n = int(x.size());
    const int r = int(4.0 * sigma + 0.5);   // radius = int(truncate*sigma + 0.5)
    std::vector<double> w(2 * r + 1);
    double sum = 0;
    const double inv2s2 = -0.5 / (sigma * sigma);
    for (int k = -r; k <= r; ++k) { w[k + r] = std::exp(inv2s2 * k * k); sum += w[k + r]; }
    for (double& v : w) v /= sum;
    std::vector<double> out(n, 0.0);
    for (int i = 0; i < n; ++i) {
        double acc = 0;
        for (int k = -r; k <= r; ++k) acc += w[k + r] * x[reflectIdx(i + k, n)];
        out[i] = acc;
    }
    return out;
}

// np.unwrap over a radian sequence.
std::vector<double> unwrap(const std::vector<double>& p)
{
    std::vector<double> out = p;
    double corr = 0;
    for (size_t i = 1; i < out.size(); ++i) {
        double d = (p[i] - p[i - 1]);
        double dd = std::fmod(d + kPi, 2 * kPi);
        if (dd < 0) dd += 2 * kPi;
        dd -= kPi;
        if (dd == -kPi && d > 0) dd = kPi;
        corr += dd - d;
        out[i] = p[i] + corr;
    }
    return out;
}

inline int wmaxFor(SwingPhase p, const ShaftV3Config& c)
{
    double w = c.wmaxDownswing;
    switch (p) {
        case SwingPhase::Addr:      w = c.wmaxAddr; break;
        case SwingPhase::Backswing: w = c.wmaxBackswing; break;
        case SwingPhase::Top:       w = c.wmaxTop; break;
        case SwingPhase::Impact:    w = c.wmaxImpact; break;
        case SwingPhase::Downswing: w = c.wmaxDownswing; break;
        case SwingPhase::Thru:      w = c.wmaxThru; break;
        case SwingPhase::Finish:    w = c.wmaxFinish; break;
    }
    return int(std::ceil(w / c.grid));
}

inline int phaseSign(SwingPhase p)
{
    switch (p) {
        case SwingPhase::Backswing: return +1;
        case SwingPhase::Downswing:
        case SwingPhase::Impact:
        case SwingPhase::Thru:
        case SwingPhase::Finish:    return -1;
        default:                    return 0;   // addr, top
    }
}

inline bool isMidswing(SwingPhase p)
{
    return p == SwingPhase::Backswing || p == SwingPhase::Top || p == SwingPhase::Downswing
        || p == SwingPhase::Impact || p == SwingPhase::Thru;
}

} // namespace

// ── config ───────────────────────────────────────────────────────────────────
ShaftV3Config ShaftV3Config::fromOverrides(const QVariantMap& ov)
{
    using namespace tuning;
    ShaftV3Config c;
    apply(ov, "shaft.grid", c.grid);
    apply(ov, "shaft.wmaxBackswing", c.wmaxBackswing);
    apply(ov, "shaft.wmaxDownswing", c.wmaxDownswing);
    apply(ov, "shaft.wmaxImpact", c.wmaxImpact);
    apply(ov, "shaft.wmaxThru", c.wmaxThru);
    apply(ov, "shaft.wmaxFinish", c.wmaxFinish);
    apply(ov, "shaft.wE2", c.wE2);
    apply(ov, "shaft.wBand", c.wBand);
    apply(ov, "shaft.wArm", c.wArm);
    apply(ov, "shaft.wC1", c.wC1);
    apply(ov, "shaft.wC2", c.wC2);
    apply(ov, "shaft.wCone", c.wCone);
    apply(ov, "shaft.kSmooth", c.kSmooth);
    apply(ov, "shaft.coneHalf", c.coneHalf);
    apply(ov, "shaft.c1Tol", c.c1Tol);
    apply(ov, "shaft.rayEvMin", c.rayEvMin);
    apply(ov, "shaft.bandTol", c.bandTol);
    apply(ov, "shaft.armVetoDeg", c.armVetoDeg);
    apply(ov, "shaft.stillSpeed", c.stillSpeed);
    apply(ov, "shaft.stillMin", c.stillMin);
    apply(ov, "shaft.bandNear", c.bandNear);
    apply(ov, "shaft.spanCollarUs", c.spanCollarUs);
    apply(ov, "shaft.spanBound", c.spanBound);
    apply(ov, "shaft.bodyMargin", c.bodyMargin);
    apply(ov, "shaft.rasterC2", c.rasterC2);
    apply(ov, "shaft.psiRail", c.psiRail);
    apply(ov, "shaft.armOutlierDeg", c.armOutlierDeg);
    apply(ov, "shaft.wIsoBand", c.wIsoBand);
    apply(ov, "shaft.wIsoRay", c.wIsoRay);
    apply(ov, "shaft.wIsoPred", c.wIsoPred);
    apply(ov, "shaft.isoHuber", c.isoHuber);
    apply(ov, "shaft.isoIters", c.isoIters);
    apply(ov, "shaft.reconTol", c.reconTol);
    apply(ov, "shaft.psiWinBack", c.psiWinBack);
    apply(ov, "shaft.psiWinFwd", c.psiWinFwd);
    apply(ov, "shaft.swSpd", c.swSpd);
    apply(ov, "shaft.impHalf", c.impHalf);
    apply(ov, "shaft.coverageMin", c.coverageMin);
    // evidence engines
    apply(ov, "shaft.rStep", c.ridge.rStep);
    apply(ov, "shaft.rHi", c.ridge.rHi);
    apply(ov, "shaft.bgHi", c.ridge.bgHi);
    apply(ov, "shaft.minLenPx", c.ridge.minLenPx);
    apply(ov, "shaft.satT", c.band.satT);
    apply(ov, "shaft.gripGate", c.band.gripGate);
    return c;
}

// ── φ smoothing (club_track_v3.smooth_phi) ───────────────────────────────────
std::vector<double> smoothPhi(const std::vector<double>& phiDeg, const ShaftV3Config& cfg)
{
    const int n = int(phiDeg.size());
    constexpr int win = 9;
    std::vector<double> c(n), s(n);
    for (int i = 0; i < n; ++i) { const double r = phiDeg[i] * kPi / 180.0; c[i] = std::cos(r); s[i] = std::sin(r); }
    const std::vector<double> cm = medianFilter1d(c, win);
    const std::vector<double> sm = medianFilter1d(s, win);
    for (int i = 0; i < n; ++i) {
        const double dev = std::abs(std::atan2(s[i] * cm[i] - c[i] * sm[i], c[i] * cm[i] + s[i] * sm[i]) * 180.0 / kPi);
        if (dev > cfg.armOutlierDeg) { c[i] = cm[i]; s[i] = sm[i]; }
    }
    const std::vector<double> cx = gaussianFilter1d(medianFilter1d(c, win), 3.0);
    const std::vector<double> cy = gaussianFilter1d(medianFilter1d(s, win), 3.0);
    std::vector<double> out(n);
    for (int i = 0; i < n; ++i) out[i] = std::atan2(cy[i], cx[i]) * 180.0 / kPi;
    return out;
}

// ── hands-only phase model (club_track_v3.segment_phases) ────────────────────
PhaseModel segmentPhases(const std::vector<double>& gx, const std::vector<double>& gy,
                         int nf, double /*fps*/, int impactFrame, const ShaftV3Config& cfg)
{
    PhaseModel m;
    std::vector<double> spd(nf, 0.0);
    for (int f = 1; f < nf; ++f) spd[f] = std::hypot(gx[f] - gx[f - 1], gy[f] - gy[f - 1]);
    m.spdSmoothed = gaussianFilter1d(medianFilter1d(spd, 5), 2.0);
    const auto& spdS = m.spdSmoothed;

    std::vector<char> mo(nf);
    for (int f = 0; f < nf; ++f) mo[f] = spdS[f] > cfg.swSpd;

    std::vector<std::pair<int, int>> runs;
    for (int f = 0; f < nf;) {
        if (mo[f]) {
            int g = f;
            while (g + 1 < nf && mo[g + 1]) ++g;
            if (g - f >= 6) runs.emplace_back(f, g);
            f = g + 1;
        } else ++f;
    }
    if (runs.empty()) {
        m.phase.assign(nf, SwingPhase::Addr);
        m.bs0 = 0; m.top = nf / 2; m.impact = nf / 2; m.fin0 = nf - 1;
        return m;
    }
    std::stable_sort(runs.begin(), runs.end(),
                     [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                         return (a.second - a.first) > (b.second - b.first);
                     });
    std::vector<std::pair<int, int>> big(runs.begin(), runs.begin() + std::min<size_t>(2, runs.size()));
    std::stable_sort(big.begin(), big.end(),
                     [](const std::pair<int, int>& a, const std::pair<int, int>& b) { return a.first < b.first; });
    const int bs0 = big[0].first;
    int top, dsEnd;
    if (big.size() >= 2) {
        const int gapLo = big[0].second, gapHi = big[1].first;
        if (gapHi > gapLo) {
            int amin = gapLo; for (int f = gapLo; f <= gapHi; ++f) if (spdS[f] < spdS[amin]) amin = f;
            top = amin;
        } else top = big[0].second;
        dsEnd = big[1].second;
    } else {
        int amax = bs0; for (int f = bs0; f <= big[0].second; ++f) if (-gy[f] > -gy[amax]) amax = f;
        top = amax;
        dsEnd = big[0].second;
    }
    // address grip height = median(gy[:max(bs0,1)])
    std::vector<double> head(gy.begin(), gy.begin() + std::max(bs0, 1));
    std::nth_element(head.begin(), head.begin() + head.size() / 2, head.end());
    double addrGy = head[head.size() / 2];
    if (head.size() % 2 == 0) {   // even count ⇒ mean of the two middle (np.median)
        double hi = head[head.size() / 2];
        double lo = *std::max_element(head.begin(), head.begin() + head.size() / 2);
        addrGy = 0.5 * (lo + hi);
    }
    int impf = impactFrame;
    if (impf < 0) {
        impf = (top + dsEnd) / 2;
        for (int f = top; f <= dsEnd; ++f) if (gy[f] >= addrGy - 20.0) { impf = f; break; }
    }
    impf = std::clamp(impf, top + 1, nf - 1);
    const int fin0 = std::min(dsEnd, nf - 1);
    const int IMP = cfg.impHalf;

    m.phase.resize(nf);
    for (int f = 0; f < nf; ++f) {
        if (f < bs0) m.phase[f] = SwingPhase::Addr;
        else if (f < top - 2) m.phase[f] = SwingPhase::Backswing;
        else if (f <= top + 2) m.phase[f] = SwingPhase::Top;
        else if (std::abs(f - impf) <= IMP) m.phase[f] = SwingPhase::Impact;
        else if (f < impf) m.phase[f] = SwingPhase::Downswing;
        else if (f <= fin0) m.phase[f] = SwingPhase::Thru;
        else m.phase[f] = SwingPhase::Finish;
    }
    m.bs0 = bs0; m.top = top; m.impact = impf; m.fin0 = fin0;
    return m;
}

// ── joint smoothing + body geometry ─────────────────────────────────────────
std::vector<std::vector<cv::Point2d>>
smoothJoints(const std::vector<std::vector<cv::Point2d>>& raw)
{
    const int nf = int(raw.size());
    if (nf == 0) return {};
    const int nj = int(raw[0].size());
    std::vector<std::vector<cv::Point2d>> out(nf, std::vector<cv::Point2d>(nj));
    for (int j = 0; j < nj; ++j) {
        std::vector<double> xs(nf), ys(nf);
        for (int f = 0; f < nf; ++f) { xs[f] = raw[f][j].x; ys[f] = raw[f][j].y; }
        xs = gaussianFilter1d(medianFilter1d(xs, 5), 2.0);
        ys = gaussianFilter1d(medianFilter1d(ys, 5), 2.0);
        for (int f = 0; f < nf; ++f) out[f][j] = {xs[f], ys[f]};
    }
    return out;
}

std::vector<BodyPoly> bodyPolys(const std::vector<std::vector<cv::Point2d>>& joints)
{
    std::vector<BodyPoly> polys;
    if (joints.empty() || joints[0].empty()) return polys;
    polys.resize(joints.size());
    for (size_t f = 0; f < joints.size(); ++f) {
        std::vector<cv::Point2f> pts;
        pts.reserve(joints[f].size());
        for (const auto& p : joints[f]) pts.emplace_back(float(p.x), float(p.y));
        std::vector<cv::Point2f> hullf;
        cv::convexHull(pts, hullf);
        const int E = int(hullf.size());
        std::vector<cv::Point2d> hull(E);
        cv::Point2d c(0, 0);
        for (int i = 0; i < E; ++i) { hull[i] = {double(hullf[i].x), double(hullf[i].y)}; c += hull[i]; }
        c *= 1.0 / E;
        BodyPoly bp; bp.n.resize(E); bp.d.resize(E);
        for (int i = 0; i < E; ++i) {
            const cv::Point2d e = hull[(i + 1) % E] - hull[i];     // edge vector
            cv::Vec2d nrm(e.y, -e.x);
            nrm /= (std::hypot(nrm[0], nrm[1]) + 1e-9);
            const cv::Point2d mid = hull[i] + 0.5 * e - c;
            if (nrm[0] * mid.x + nrm[1] * mid.y < 0) nrm *= -1.0;  // orient outward
            bp.n[i] = nrm;
            bp.d[i] = nrm[0] * hull[i].x + nrm[1] * hull[i].y;
        }
        polys[f] = std::move(bp);
    }
    return polys;
}

std::vector<cv::Mat> bodyMasks(const std::vector<std::vector<cv::Point2d>>& joints,
                               int W, int H, const ShaftV3Config& cfg)
{
    std::vector<cv::Mat> masks;
    if (joints.empty() || joints[0].empty()) return masks;
    const int k = int(cfg.bodyMargin);
    const cv::Mat se = cv::getStructuringElement(cv::MORPH_ELLIPSE, {2 * k + 1, 2 * k + 1});
    masks.resize(joints.size());
    for (size_t f = 0; f < joints.size(); ++f) {
        std::vector<cv::Point2f> pts;
        for (const auto& p : joints[f]) pts.emplace_back(float(p.x), float(p.y));
        std::vector<cv::Point2f> hullf;
        cv::convexHull(pts, hullf);
        std::vector<cv::Point> hull;
        for (const auto& p : hullf) hull.emplace_back(int(p.x), int(p.y));
        cv::Mat m = cv::Mat::zeros(H, W, CV_8UC1);
        cv::fillConvexPoly(m, hull, cv::Scalar(255));
        cv::dilate(m, m, se);
        masks[f] = m;
    }
    return masks;
}

std::vector<char> staticRuns(const std::vector<double>& spdS, const ShaftV3Config& cfg)
{
    const int nf = int(spdS.size());
    std::vector<char> stat(nf, 0);
    for (int f = 0; f < nf;) {
        if (spdS[f] < cfg.stillSpeed) {
            int g = f;
            while (g + 1 < nf && spdS[g + 1] < cfg.stillSpeed) ++g;
            if (g - f + 1 >= cfg.stillMin) for (int k = f; k <= g; ++k) stat[k] = 1;
            f = g + 1;
        } else ++f;
    }
    return stat;
}

// ── per-frame DP emission (club_track_v3 main loop, one frame) ───────────────
void frameEmission(std::vector<float>& emOut, std::vector<float>& insideOut,
                   const std::vector<float>& evMax, const std::vector<float>& rawNorm,
                   const BandMatch& band, double phiSDeg, SwingPhase phase, int chir,
                   double gx, double gy, const BodyPoly* poly, const cv::Mat& mask,
                   const std::vector<float>& gridRad, const std::vector<float>& gridDeg,
                   const ShaftV3Config& cfg)
{
    const int NS = int(evMax.size());
    emOut.assign(NS, 0.f);
    insideOut.assign(NS, 0.f);

    // ev copy with the band bin raised (E1 reward) — EV[] stored elsewhere is
    // the pre-raise evMax, so this raise is local to the emission.
    std::vector<float> ev = evMax;
    int bi = -1;
    if (band.ok) {
        bi = int(std::lround(band.thetaDeg / cfg.grid)) % NS;
        if (bi < 0) bi += NS;
        ev[bi] = std::max(ev[bi], 1.0f);
    }
    for (int k = 0; k < NS; ++k) emOut[k] = float(cfg.wE2 * (1.0 - ev[k]));

    // C4 arm-veto: shaft never points INTO the lead forearm (φ+180)
    const double arm = std::fmod(phiSDeg + 180.0, 360.0);
    for (int k = 0; k < NS; ++k)
        if (std::abs(circWrap(gridDeg[k] - arm)) < cfg.armVetoDeg) emOut[k] += float(cfg.wArm);

    // C4 wide reachable cone (chirality-centred), off addr/finish/top
    if (phase != SwingPhase::Addr && phase != SwingPhase::Finish && phase != SwingPhase::Top) {
        const double cen = phiSDeg + chir * (cfg.coneHalf - 40.0);
        for (int k = 0; k < NS; ++k)
            if (std::abs(circWrap(gridDeg[k] - cen)) > cfg.coneHalf) emOut[k] += float(cfg.wCone);
    }

    // C1 (weak form): reverse ridge OFF the forearm = scene line
    const double phiMod = std::fmod(std::fmod(phiSDeg, 360.0) + 360.0, 360.0);
    for (int k = 0; k < NS; ++k) {
        const double rev = rawNorm[(k + NS / 2) % NS];
        const double revArm = std::abs(circWrap(gridDeg[k] - phiMod));
        if (rev > cfg.c1Tol && revArm > cfg.armVetoDeg) emOut[k] += float(cfg.wC1);
    }

    // C2 body-overlap veto (mid-swing only)
    const bool haveC2 = (poly != nullptr) || !mask.empty();
    if (isMidswing(phase) && haveC2) {
        std::vector<double> BR;
        for (double r = cfg.bodyRLo; r < cfg.bodyRHi; r += cfg.bodyRStep) BR.push_back(r);
        const int nR = int(BR.size());
        const int W = mask.empty() ? 0 : mask.cols;
        const int H = mask.empty() ? 0 : mask.rows;
        for (int k = 0; k < NS; ++k) {
            const double ux = std::cos(double(gridRad[k])), uy = std::sin(double(gridRad[k]));
            int insideCount = 0;
            for (int r = 0; r < nR; ++r) {
                const double px = gx + ux * BR[r], py = gy + uy * BR[r];
                bool in;
                if (poly) {
                    double sdmax = -1e30;
                    for (size_t e = 0; e < poly->n.size(); ++e) {
                        const double sd = poly->n[e][0] * px + poly->n[e][1] * py - poly->d[e];
                        if (sd > sdmax) sdmax = sd;
                    }
                    in = (sdmax <= cfg.bodyMargin);
                } else {
                    int xi = int(px); if (xi < 0) xi = 0; else if (xi >= W) xi = W - 1;
                    int yi = int(py); if (yi < 0) yi = 0; else if (yi >= H) yi = H - 1;
                    in = (mask.at<uchar>(yi, xi) > 0);
                }
                if (in) ++insideCount;
            }
            const double frac = double(insideCount) / nR;
            insideOut[k] = float(frac);
            if (frac > 0.5) emOut[k] += float(cfg.wC2);
        }
    }

    // band negative well LAST — dominates the gates, forces the global path
    if (band.ok) emOut[bi] = float(-cfg.wBand);
}

// ── global banded Viterbi DP (club_track_v3 C3) ──────────────────────────────
DPResult viterbiDP(const std::vector<std::vector<float>>& emis,
                   const std::vector<SwingPhase>& phase, const ShaftV3Config& cfg)
{
    const int nf = int(emis.size());
    DPResult out;
    if (nf == 0) return out;
    const int NS = int(emis[0].size());

    std::vector<double> cost(emis[0].begin(), emis[0].end());
    std::vector<std::vector<int>> back(nf, std::vector<int>(NS, 0));

    std::vector<double> best(NS), tmp(NS);
    std::vector<int> barg(NS);
    for (int f = 1; f < nf; ++f) {
        const int wmax = wmaxFor(phase[f], cfg);
        const int sgn = phaseSign(phase[f]);
        const int dLo = (sgn > 0) ? 0 : -wmax;
        const int dHi = (sgn < 0) ? 0 : wmax;
        std::fill(best.begin(), best.end(), kInf);
        std::fill(barg.begin(), barg.end(), 0);
        for (int d = dLo; d <= dHi; ++d) {
            const double t = cfg.kSmooth * (d * cfg.grid) * (d * cfg.grid);
            for (int k = 0; k < NS; ++k) {
                const int src = ((k - d) % NS + NS) % NS;
                const double cand = cost[src] + t;
                if (cand < best[k]) { best[k] = cand; barg[k] = src; }
            }
        }
        for (int k = 0; k < NS; ++k) { cost[k] = best[k] + emis[f][k]; back[f][k] = barg[k]; }
    }

    out.thstar.assign(nf, 0);
    int last = 0; for (int k = 1; k < NS; ++k) if (cost[k] < cost[last]) last = k;
    out.thstar[nf - 1] = last;
    for (int f = nf - 1; f > 0; --f) out.thstar[f - 1] = back[f][out.thstar[f]];
    out.thetaDeg.resize(nf);
    for (int f = 0; f < nf; ++f) out.thetaDeg[f] = out.thstar[f] * cfg.grid;
    return out;
}

// ── ψ-isotonic reconciliation ────────────────────────────────────────────────
std::vector<double> pava(const std::vector<double>& y, const std::vector<double>& w, bool increasing)
{
    const double sgn = increasing ? 1.0 : -1.0;
    const int n = int(y.size());
    std::vector<double> means(n), wts(w);
    std::vector<std::vector<int>> idx(n);
    for (int i = 0; i < n; ++i) { means[i] = sgn * y[i]; idx[i] = {i}; }
    int i = 0;
    while (i < int(means.size()) - 1) {
        if (means[i] > means[i + 1] + 1e-12) {
            const double nw = wts[i] + wts[i + 1];
            means[i] = (means[i] * wts[i] + means[i + 1] * wts[i + 1]) / nw;
            wts[i] = nw;
            idx[i].insert(idx[i].end(), idx[i + 1].begin(), idx[i + 1].end());
            means.erase(means.begin() + i + 1);
            wts.erase(wts.begin() + i + 1);
            idx.erase(idx.begin() + i + 1);
            if (i > 0) --i;
        } else ++i;
    }
    std::vector<double> out(n, 0.0);
    for (size_t b = 0; b < means.size(); ++b)
        for (int k : idx[b]) out[k] = sgn * means[b];
    return out;
}

std::vector<double> robustIsotonic(const std::vector<double>& y, const std::vector<double>& w,
                                   bool increasing, const ShaftV3Config& cfg)
{
    std::vector<double> x = pava(y, w, increasing);
    for (int it = 0; it < cfg.isoIters; ++it) {
        std::vector<double> hw(y.size());
        for (size_t i = 0; i < y.size(); ++i) {
            const double r = std::abs(y[i] - x[i]);
            hw[i] = w[i] * (r <= cfg.isoHuber ? 1.0 : cfg.isoHuber / (r + 1e-9));
        }
        x = pava(y, hw, increasing);
    }
    return x;
}

ReconResult reconcilePsi(const std::vector<double>& thetaDeg, const std::vector<double>& phiS,
                         const std::vector<SwingPhase>& phase, const std::vector<char>& bandOk,
                         const std::vector<double>& evAt, int top, int nf, const ShaftV3Config& cfg)
{
    ReconResult rr;
    rr.thetaOut = thetaDeg;
    rr.psiResid.assign(nf, std::numeric_limits<double>::quiet_NaN());
    rr.recon.assign(nf, 0);
    const int lo = top - cfg.psiWinBack, hi = top + cfg.psiWinFwd;

    struct Block { std::vector<SwingPhase> phs; bool inc; };
    const Block blocks[2] = {
        {{SwingPhase::Backswing}, true},
        {{SwingPhase::Downswing, SwingPhase::Impact, SwingPhase::Thru}, false},
    };
    auto inBlock = [](SwingPhase p, const std::vector<SwingPhase>& set) {
        for (SwingPhase q : set) if (q == p) return true;
        return false;
    };
    auto weight = [&](int f) {
        if (!bandOk[f] && phase[f] == SwingPhase::Impact) return cfg.wIsoPred;   // RECON_PHASES=(impact,)
        if (bandOk[f]) return cfg.wIsoBand;
        return evAt[f] >= cfg.rayEvMin ? cfg.wIsoRay : cfg.wIsoPred;
    };

    for (const Block& blk : blocks) {
        std::vector<int> fs;
        for (int f = 0; f < nf; ++f)
            if (inBlock(phase[f], blk.phs) && !(lo <= f && f <= hi) && !std::isnan(thetaDeg[f]))
                fs.push_back(f);
        if (int(fs.size()) < 4) continue;
        std::vector<double> psiRad(fs.size()), ph(fs.size()), w(fs.size());
        for (size_t i = 0; i < fs.size(); ++i) {
            ph[i] = phiS[fs[i]];
            psiRad[i] = (thetaDeg[fs[i]] - ph[i]) * kPi / 180.0;
            w[i] = weight(fs[i]);
        }
        const std::vector<double> psiU = unwrap(psiRad);
        std::vector<double> psi(fs.size());
        for (size_t i = 0; i < fs.size(); ++i) psi[i] = psiU[i] * 180.0 / kPi;   // deg, continuous
        const std::vector<double> iso = robustIsotonic(psi, w, blk.inc, cfg);
        for (size_t i = 0; i < fs.size(); ++i) {
            const int f = fs[i];
            rr.psiResid[f] = std::abs(psi[i] - iso[i]);
            if (phase[f] == SwingPhase::Impact && !bandOk[f]) {    // blur: arm is the witness
                rr.thetaOut[f] = std::fmod(std::fmod(iso[i] + ph[i], 360.0) + 360.0, 360.0);
                rr.recon[f] = 1;
            }
        }
    }
    return rr;
}

// ── SwingWindow-free decide core ─────────────────────────────────────────────
namespace {

// np.percentile with linear interpolation.
float percentile(std::vector<float> v, double p)
{
    std::sort(v.begin(), v.end());
    const double idx = p / 100.0 * (v.size() - 1);
    const int lo = int(std::floor(idx)), hi = int(std::ceil(idx));
    return float(v[lo] + (v[hi] - v[lo]) * (idx - lo));
}

// Python norm(): clip((s - p50)/(p97 - p50 + 1e-6), 0, 1).
std::vector<float> normScores(const std::vector<float>& s)
{
    const float lo = percentile(s, 50.0), hi = percentile(s, 97.0);
    std::vector<float> out(s.size());
    for (size_t i = 0; i < s.size(); ++i)
        out[i] = std::clamp((s[i] - lo) / (hi - lo + 1e-6f), 0.f, 1.f);
    return out;
}

// np.interp fill of NaN entries using the frame index as the abscissa.
void interpFillNan(std::vector<double>& v)
{
    const int n = int(v.size());
    std::vector<int> good;
    for (int i = 0; i < n; ++i) if (!std::isnan(v[i])) good.push_back(i);
    if (good.empty()) { std::fill(v.begin(), v.end(), 0.0); return; }
    for (int i = 0; i < n; ++i) {
        if (!std::isnan(v[i])) continue;
        if (i <= good.front()) { v[i] = v[good.front()]; continue; }
        if (i >= good.back())  { v[i] = v[good.back()];  continue; }
        int a = good.front(), b = good.back();
        for (size_t k = 1; k < good.size(); ++k) if (good[k] >= i) { b = good[k]; a = good[k - 1]; break; }
        v[i] = v[a] + (v[b] - v[a]) * (double(i - a) / double(b - a));
    }
}

} // namespace

Segmentation phasesToSegmentation(const PhaseModel& pm, const std::vector<int64_t>& tUs, float conf)
{
    Segmentation seg;
    const int nf = int(tUs.size());
    if (nf < 2) return seg;
    auto tAt = [&](int f) { return tUs[std::clamp(f, 0, nf - 1)]; };
    auto add = [&](Phase p, int f) { PhaseEvent e; e.phase = p; e.t_us = tAt(f); e.conf = conf; seg.events.push_back(e); };
    add(Phase::Address, pm.bs0);
    add(Phase::Top,     pm.top);
    add(Phase::Impact,  pm.impact);
    add(Phase::Finish,  pm.fin0);
    std::stable_sort(seg.events.begin(), seg.events.end(),
                     [](const PhaseEvent& a, const PhaseEvent& b) { return a.t_us < b.t_us; });
    const int64_t pad = 100000;   // 100 ms, clamped to coverage
    seg.swingStartUs = std::max(tUs.front(), tAt(pm.bs0) - pad);
    seg.swingEndUs   = std::min(tUs.back(),  tAt(pm.fin0) + pad);
    seg.conf = conf;
    return seg;
}

ShaftTrack2D decideTrack(const FrameSource& frameAt, const std::vector<int64_t>& tUs,
                         const std::vector<double>& gxIn, const std::vector<double>& gyIn,
                         const std::vector<double>& phiRawIn,
                         const std::vector<std::vector<cv::Point2d>>& rawJoints,
                         int frameW, int frameH, double fps,
                         const std::vector<double>& bandsMm, double clubLenMm,
                         int impactFrame, const ShaftV3Config& cfg, ShaftDecideTrace* trace)
{
    ShaftTrack2D out;
    out.frameWidth = frameW; out.frameHeight = frameH;
    const int nf = int(gxIn.size());
    if (nf < 2) return out;
    const std::vector<double>& gx = gxIn;
    const std::vector<double>& gy = gyIn;

    std::vector<double> phiRaw = phiRawIn;
    interpFillNan(phiRaw);
    const std::vector<double> phiS = smoothPhi(phiRaw, cfg);

    const PhaseModel pm = segmentPhases(gx, gy, nf, fps, impactFrame, cfg);

    // chirality from unwrapped φ over [bs0, top]
    int chir = 1;
    {
        std::vector<double> phu(nf);
        double corr = 0; phu[0] = phiS[0] * kPi / 180.0;
        for (int i = 1; i < nf; ++i) {
            const double pr = phiS[i] * kPi / 180.0, pp = phiS[i - 1] * kPi / 180.0;
            double d = pr - pp, dd = std::fmod(d + kPi, 2 * kPi); if (dd < 0) dd += 2 * kPi; dd -= kPi;
            corr += dd - d; phu[i] = pr + corr;
        }
        const int t = std::min(pm.top, nf - 1);
        chir = (phu[t] - phu[pm.bs0]) >= 0 ? 1 : -1;
    }

    const std::vector<std::vector<cv::Point2d>> smoothed = smoothJoints(rawJoints);
    const std::vector<BodyPoly> polys = cfg.rasterC2 ? std::vector<BodyPoly>{} : bodyPolys(smoothed);
    const std::vector<cv::Mat>  masks = cfg.rasterC2 ? bodyMasks(smoothed, frameW, frameH, cfg) : std::vector<cv::Mat>{};
    const std::vector<char> stat = staticRuns(pm.spdSmoothed, cfg);

    const int NS = int(std::lround(360.0 / cfg.grid));
    std::vector<float> gridRad(NS), gridDeg(NS);
    for (int k = 0; k < NS; ++k) { gridDeg[k] = float(k * cfg.grid); gridRad[k] = float(k * cfg.grid * kPi / 180.0); }

    // scene background: median of every-8th frame (float32)
    cv::Mat sceneMed;
    {
        std::vector<cv::Mat> bg;
        for (int i = 0; i < nf; i += 8) { cv::Mat g = frameAt(i); if (!g.empty()) bg.push_back(g); }
        if (!bg.empty()) {
            const int H = bg[0].rows, W = bg[0].cols; const size_t n = bg.size();
            sceneMed.create(H, W, CV_32F);
            std::vector<uchar> vals(n);
            for (int r = 0; r < H; ++r) {
                float* orow = sceneMed.ptr<float>(r);
                for (int c = 0; c < W; ++c) {
                    for (size_t k = 0; k < n; ++k) vals[k] = bg[k].ptr<uchar>(r)[c];
                    std::nth_element(vals.begin(), vals.begin() + n / 2, vals.end());
                    orow[c] = float(vals[n / 2]);
                }
            }
        }
    }

    const int collar = int(std::lround(double(cfg.spanCollarUs) * 1e-6 * fps));
    const int spanLo = cfg.spanBound ? std::max(0, pm.bs0 - collar) : 0;
    const int spanHi = cfg.spanBound ? std::min(nf - 1, pm.fin0 + collar) : nf - 1;
    const double rmax = 0.62 * frameH;

    std::vector<std::vector<float>> emis(nf, std::vector<float>(NS, float(cfg.wE2)));
    std::vector<std::vector<float>> EV(nf, std::vector<float>(NS, 0.f));
    std::vector<BandMatch> band(nf);
    std::vector<char> bandOk(nf, 0);
    int heavy = 0;
    for (int i = 0; i < nf; ++i) {
        if (std::isnan(gx[i])) continue;
        if (!(spanLo <= i && i <= spanHi)) continue;
        cv::Mat g8 = frameAt(i);
        if (g8.empty()) continue;
        ++heavy;
        cv::Mat g32; g8.convertTo(g32, CV_32F);
        const RidgeResult sRaw = ridgeSweep(g32, gx[i], gy[i], gridRad, cfg.ridge, false);
        std::vector<float> normRaw = normScores(sRaw.score);
        std::vector<float> evMax = normRaw;
        if (!sceneMed.empty() && sceneMed.size() == g32.size()) {
            cv::Mat diff; cv::absdiff(g32, sceneMed, diff);
            const RidgeResult sDif = ridgeSweep(diff, gx[i], gy[i], gridRad, cfg.ridge, true);
            const std::vector<float> normDif = normScores(sDif.score);
            for (int k = 0; k < NS; ++k) evMax[k] = std::max(normRaw[k], normDif[k]);
        }
        EV[i] = evMax;
        BandMatch bm = frameBandMatch(g8, gx[i], gy[i], rmax, bandsMm, cfg.band);
        if (bm.ok && bm.r0 > 0.0f && bm.r0 <= 260.0f) { band[i] = bm; bandOk[i] = 1; }
        std::vector<float> em, inside;
        frameEmission(em, inside, evMax, normRaw, band[i], phiS[i], pm.phase[i], chir,
                      gx[i], gy[i], polys.empty() ? nullptr : &polys[i],
                      masks.empty() ? cv::Mat() : masks[i], gridRad, gridDeg, cfg);
        emis[i] = std::move(em);
    }

    const DPResult dp = viterbiDP(emis, pm.phase, cfg);
    std::vector<double> evAt(nf, 0.0);
    for (int i = 0; i < nf; ++i) evAt[i] = EV[i][dp.thstar[i]];
    ReconResult rec;
    if (cfg.psiRail)
        rec = reconcilePsi(dp.thetaDeg, phiS, pm.phase, bandOk, evAt, pm.top, nf, cfg);
    else {
        rec.thetaOut = dp.thetaDeg;
        rec.psiResid.assign(nf, std::numeric_limits<double>::quiet_NaN());
        rec.recon.assign(nf, 0);
    }

    double sTypical = 0; { std::vector<double> ss; for (int i = 0; i < nf; ++i) if (bandOk[i]) ss.push_back(band[i].s);
        if (!ss.empty()) { std::nth_element(ss.begin(), ss.begin() + ss.size() / 2, ss.end()); sTypical = ss[ss.size() / 2]; } }
    const double projLenPx = sTypical > 0 ? sTypical * clubLenMm : 0.55 * frameH;

    enum Tier { PRED = 0, RAY = 1, BAND = 2, RECON = 3 };
    int spanFrames = 0, spanMeas = 0;
    for (int i = 0; i < nf; ++i) {
        if (std::isnan(gx[i])) continue;
        const int thi = dp.thstar[i];
        const double thDp = dp.thetaDeg[i];
        const double th = rec.thetaOut[i];
        int tier = PRED; float conf = 0.30f;
        double headX = 0, headY = 0; bool hasHead = false; double visLen = 0;
        if (bandOk[i] && std::abs(circWrap(thDp - band[i].thetaDeg)) <= cfg.bandTol) {
            tier = BAND;
            const double s = band[i].s, r0 = band[i].r0;
            const double ux = std::cos(th * kPi / 180.0), uy = std::sin(th * kPi / 180.0);
            const double bx = gx[i] - s * r0 * ux, by = gy[i] - s * r0 * uy;
            headX = bx + s * clubLenMm * ux; headY = by + s * clubLenMm * uy; hasHead = true;
            visLen = std::hypot(headX - gx[i], headY - gy[i]);
            conf = float(std::min(0.9, 0.75 + 0.05 * (band[i].n - 4)));
        } else if (pm.phase[i] != SwingPhase::Addr) {
            const double evs = EV[i][thi], evrev = EV[i][(thi + NS / 2) % NS];
            bool bandNear = false;
            for (int j = std::max(0, i - cfg.bandNear); j <= std::min(nf - 1, i + cfg.bandNear); ++j) if (bandOk[j]) { bandNear = true; break; }
            const bool verifiable = (pm.phase[i] == SwingPhase::Finish) ? bandNear : (!stat[i] || bandNear);
            if (evs >= cfg.rayEvMin && evs > 1.15 * evrev && verifiable) { tier = RAY; conf = 0.55f; }
        }
        if (rec.recon[i] && std::abs(circWrap(th - thDp)) > cfg.reconTol) { tier = RECON; conf = 0.40f; hasHead = false; visLen = 0; }

        ShaftSample2D s;
        s.t_us = tUs[i];
        s.gripPx = QPointF(gx[i], gy[i]);
        s.thetaRad = th * kPi / 180.0;
        s.conf = conf;
        if (hasHead) { s.headPx = QPointF(headX, headY); s.flags = ShaftMeasured; s.visibleLenPx = visLen; }
        else {
            const double ux = std::cos(th * kPi / 180.0), uy = std::sin(th * kPi / 180.0);
            s.headPx = QPointF(gx[i] + projLenPx * ux, gy[i] + projLenPx * uy);
            s.flags = uint8_t((tier == RAY ? ShaftMeasured : ShaftCoasted) | ShaftHeadProjected);
        }
        out.samples.push_back(s);
        if (trace) { trace->frameIdx.push_back(i); trace->tier.push_back(tier);
                     trace->thetaDeg.push_back(th); trace->conf.push_back(conf); }
        if (i >= pm.bs0 && i <= pm.fin0) { ++spanFrames; if (tier == BAND || tier == RAY) ++spanMeas; }
    }

    for (size_t i = 0; i < out.samples.size(); ++i) {
        if (out.samples.size() < 2) break;
        const size_t a = (i == 0) ? 0 : i - 1;
        const size_t b = (i + 1 < out.samples.size()) ? i + 1 : i;
        const double dth = std::remainder(out.samples[b].thetaRad - out.samples[a].thetaRad, 2 * kPi);
        const double dt = double(out.samples[b].t_us - out.samples[a].t_us) * 1e-6;
        out.samples[i].thetaDotRadS = (dt > 0) ? dth / dt : 0.0;
    }

    out.coverage = spanFrames > 0 ? float(spanMeas) / float(spanFrames) : 0.f;
    out.valid = out.coverage >= float(cfg.coverageMin);

    if (trace) {
        trace->phases = pm; trace->phiSmoothed = phiS; trace->chir = chir;
        trace->spanLo = spanLo; trace->spanHi = spanHi; trace->heavyFrames = heavy;
        trace->dp = dp; trace->recon = rec;
        // Vision-only phase landmarks: real swing ⇒ vision-grade conf, else 0.
        const bool swingDetected = std::any_of(pm.phase.begin(), pm.phase.end(),
                                               [](SwingPhase p) { return p != SwingPhase::Addr; });
        trace->segmentation = phasesToSegmentation(pm, tUs, swingDetected ? 0.5f : 0.0f);
    }
    return out;
}

} // namespace pinpoint::analysis
