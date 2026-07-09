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

#include "clubhead_track.h"

#include "analysis_tuning.h"    // pinpoint::analysis::tuning::apply

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

namespace pinpoint::analysis {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// H1 scoring blend weights (clubhead_measure §candidate scoring). Provenance-
// fixed, not sweepable: they set the shape of the tail-quality/support tradeoff,
// not a physical threshold.
constexpr double kScoreTailW    = 0.6;   // score = kScoreTailW·tail_q + (1−)·min(support/kSupportNorm,1)·prior
constexpr double kSupportNorm   = 0.6;   // support normalisation ("40% visible in chunks still wins")
constexpr double kTailWinPx     = 12.0;  // tail-quality window (px) back from the terminus
constexpr double kConfSupportW  = 0.5;   // conf = kConfSupportW·min(support/kSupportNorm,1) + (1−)·tail_q

inline double clip01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

// Bilinear sample of `img` (CV_32F, single-channel) along a 1×N ray, base coords
// (X,Y) shifted by (sx,sy); BORDER_CONSTANT=0 so off-frame reads are 0 — matches
// cv2.remap(INTER_LINEAR, BORDER_CONSTANT, 0). `out` receives N values.
void sampleShifted(const cv::Mat &img, const std::vector<float> &X, const std::vector<float> &Y,
                   float sx, float sy, std::vector<float> &out)
{
    const int nr = int(X.size());
    cv::Mat mapx(1, nr, CV_32F), mapy(1, nr, CV_32F);
    float *mx = mapx.ptr<float>(0), *my = mapy.ptr<float>(0);
    for (int k = 0; k < nr; ++k) { mx[k] = X[k] + sx; my[k] = Y[k] + sy; }
    cv::Mat dst;
    cv::remap(img, dst, mapx, mapy, cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));
    out.resize(nr);
    const float *d = dst.ptr<float>(0);
    for (int k = 0; k < nr; ++k) out[k] = d[k];
}

// Edge-pair ridge response at width `w` along the ray (normal n = (nx,ny)): a
// bright/dark thin line makes the two flanking gradients point OPPOSITE, so
// −dm·dp > 0. clip(sqrt(max(0,−dm·dp))/tauP, 0, 1). `gxImg`/`gyImg` are Sobel
// x/y of the source image (gray for the club ridge, sceneMed for the veto).
void edgePair(const cv::Mat &gxImg, const cv::Mat &gyImg,
              const std::vector<float> &X, const std::vector<float> &Y,
              double nx, double ny, double w, double tauP, std::vector<float> &e,
              std::vector<float> &bufAx, std::vector<float> &bufAy,
              std::vector<float> &bufBx, std::vector<float> &bufBy)
{
    const int nr = int(X.size());
    const float dxw = float(nx * w * 0.5), dyw = float(ny * w * 0.5);
    sampleShifted(gxImg, X, Y, -dxw, -dyw, bufAx);
    sampleShifted(gyImg, X, Y, -dxw, -dyw, bufAy);
    sampleShifted(gxImg, X, Y, +dxw, +dyw, bufBx);
    sampleShifted(gyImg, X, Y, +dxw, +dyw, bufBy);
    e.resize(nr);
    for (int k = 0; k < nr; ++k) {
        const double dm = bufAx[k] * nx + bufAy[k] * ny;
        const double dp = bufBx[k] * nx + bufBy[k] * ny;
        const double v  = -dm * dp;
        e[k] = float(clip01((v > 0.0 ? std::sqrt(v) : 0.0) / tauP));
    }
}

// 2×2 helpers for the KF (F = [[1,dt],[0,1]]).
struct Mat2 { double a, b, c, d; };
inline Mat2 inv2(const Mat2 &m)
{
    const double det = m.a * m.d - m.b * m.c;
    const double id = (std::abs(det) > 1e-30) ? 1.0 / det : 0.0;
    return { m.d * id, -m.b * id, -m.c * id, m.a * id };
}
inline Mat2 mul2(const Mat2 &x, const Mat2 &y)
{
    return { x.a * y.a + x.b * y.c, x.a * y.b + x.b * y.d,
             x.c * y.a + x.d * y.c, x.c * y.b + x.d * y.d };
}

} // namespace

// ── config ───────────────────────────────────────────────────────────────────
ClubheadConfig ClubheadConfig::fromOverrides(const QVariantMap &ov)
{
    using namespace pinpoint::analysis::tuning;
    ClubheadConfig c;
    apply(ov, "shaft.head.enabled",          c.enabled);
    apply(ov, "shaft.head.hitThr",           c.hitThr);
    apply(ov, "shaft.head.permThr",          c.permThr);
    apply(ov, "shaft.head.localWin",         c.localWin);
    apply(ov, "shaft.head.localFrac",        c.localFrac);
    apply(ov, "shaft.head.supportMin",       c.supportMin);
    apply(ov, "shaft.head.startFrac",        c.startFrac);
    apply(ov, "shaft.head.tauP",             c.tauP);
    apply(ov, "shaft.head.tauM",             c.tauM);
    apply(ov, "shaft.head.tauChg",           c.tauChg);
    apply(ov, "shaft.head.rMinFrac",         c.rMinFrac);
    apply(ov, "shaft.head.edgeCensorPx",     c.edgeCensorPx);
    apply(ov, "shaft.head.edgeWidthThin",    c.edgeWidthThin);
    apply(ov, "shaft.head.edgeWidthBloom",   c.edgeWidthBloom);
    apply(ov, "shaft.head.edgeWidthBlade",   c.edgeWidthBlade);
    apply(ov, "shaft.head.ambigFloor",       c.ambigFloor);
    apply(ov, "shaft.head.latMaxPx",         c.latMaxPx);
    apply(ov, "shaft.head.bgAlpha",          c.bgAlpha);
    apply(ov, "shaft.head.annulusCeilFactor", c.annulusCeilFactor);
    apply(ov, "shaft.head.hardFloorFactor",  c.hardFloorFactor);
    apply(ov, "shaft.head.priorSigmaFrac",   c.priorSigmaFrac);
    apply(ov, "shaft.head.priorSigmaFloorPx", c.priorSigmaFloorPx);
    apply(ov, "shaft.head.measFloorFrac",    c.measFloorFrac);
    apply(ov, "shaft.head.floorRampHi",      c.floorRampHi);
    apply(ov, "shaft.head.streakConfCap",    c.streakConfCap);
    apply(ov, "shaft.head.offFactor",        c.offFactor);
    apply(ov, "shaft.head.armFactor",        c.armFactor);
    apply(ov, "shaft.head.armConfMin",       c.armConfMin);
    apply(ov, "shaft.head.segThetaJump",     c.segThetaJump);
    apply(ov, "shaft.head.sigmaAcc",         c.sigmaAcc);
    apply(ov, "shaft.head.gateSig",          c.gateSig);
    apply(ov, "shaft.head.coastSlow",        c.coastSlow);
    apply(ov, "shaft.head.coastFast",        c.coastFast);
    apply(ov, "shaft.head.fastRdot",         c.fastRdot);
    apply(ov, "shaft.head.confMeasMin",      c.confMeasMin);
    apply(ov, "shaft.head.runMin",           c.runMin);
    apply(ov, "shaft.head.reinitCap",        c.reinitCap);
    apply(ov, "shaft.head.sigMeasMax",       c.sigMeasMax);
    apply(ov, "shaft.head.initSigR",         c.initSigR);
    apply(ov, "shaft.head.initSigRdot",      c.initSigRdot);
    apply(ov, "shaft.head.measSigBase",      c.measSigBase);
    apply(ov, "shaft.head.measSigSlope",     c.measSigSlope);
    apply(ov, "shaft.head.flipConfRatio",    c.flipConfRatio);
    apply(ov, "shaft.head.flipConfAbs",      c.flipConfAbs);
    return c;
}

HeadSceneCtx makeHeadSceneCtx(const cv::Mat &sceneMed32, int W, int H, const ClubheadConfig &cfg)
{
    HeadSceneCtx ctx;
    ctx.W = W; ctx.H = H;
    ctx.cfg = cfg;
    ctx.rMin = std::max(20.0, cfg.rMinFrac * double(H));
    if (!sceneMed32.empty()) {
        sceneMed32.convertTo(ctx.sceneMed, CV_32F);   // no-op copy if already CV_32F
        cv::Sobel(ctx.sceneMed, ctx.sceneMedGx, CV_32F, 1, 0, 3);
        cv::Sobel(ctx.sceneMed, ctx.sceneMedGy, CV_32F, 0, 1, 3);
    }
    // Lateral offsets across the ray normal (clubhead_scan.lat_offsets): 0 = the
    // centre ray (real blurred footage rides the blur), a ±band for sharp/noisy.
    if (cfg.latMaxPx <= 0.0) {
        ctx.offsets = { 0.0 };
    } else {
        const int lm = int(cfg.latMaxPx);
        for (int u = -lm; u <= lm; u += 4) ctx.offsets.push_back(double(u));
    }
    return ctx;
}

// ── geometry / floors ────────────────────────────────────────────────────────
double rayEdgeRadius(double gx, double gy, double thetaDeg, int W, int H)
{
    const double th = thetaDeg * kPi / 180.0;
    const double c = std::cos(th), s = std::sin(th);
    double best = 0.0; bool have = false;
    auto consider = [&](double t) { if (t > 0.0 && (!have || t < best)) { best = t; have = true; } };
    if (std::abs(c) > 1e-9) {
        for (double x : { 0.0, double(W - 1) }) {
            const double t = (x - gx) / c;
            if (t > 0.0) { const double y = gy + t * s; if (y >= -1.0 && y <= H) consider(t); }
        }
    }
    if (std::abs(s) > 1e-9) {
        for (double y : { 0.0, double(H - 1) }) {
            const double t = (y - gy) / s;
            if (t > 0.0) { const double x = gx + t * c; if (x >= -1.0 && x <= W) consider(t); }
        }
    }
    return have ? best : 0.0;
}

double armFloorPx(const cv::Point2d &leadSho, const cv::Point2d &trailSho,
                  double gx, double gy, bool quasiStill, const ClubheadConfig &cfg)
{
    if (!quasiStill) return -1.0;
    const double dL = std::hypot(leadSho.x - gx, leadSho.y - gy);
    const double dT = std::hypot(trailSho.x - gx, trailSho.y - gy);
    return cfg.armFactor * std::max(dL, dT);
}

double hardFloorPx(double armFloor, double lPx, bool applies, const ClubheadConfig &cfg)
{
    if (!applies) return -1.0;
    double fl = (armFloor > 0.0) ? armFloor : -1.0;
    if (lPx > 0.0) fl = std::max(fl, cfg.hardFloorFactor * lPx);
    return fl;
}

HeadBounds headBounds(double rayEdge, double lPx, double fallbackCeilPx,
                      double armFloor, bool floorApplies, const HeadSceneCtx &ctx,
                      double floorFracOverride)
{
    HeadBounds b;
    b.rLo = ctx.rMin;
    if (lPx > 0.0)                     b.rHi = std::min(rayEdge, ctx.cfg.annulusCeilFactor * lPx);
    else if (fallbackCeilPx > 0.0)     b.rHi = std::min(rayEdge, fallbackCeilPx);
    else                               b.rHi = rayEdge;
    b.rFloor = hardFloorPx(armFloor, lPx, floorApplies, ctx.cfg);
    // Universal measurement-acceptance floor (design §4: r_meas ≥ floorFrac·L̂,
    // ALL frames — floorApplies gates only the strong still/impact floor above).
    // L̂ falls back to the ladder length when the ball abstained: the gateB-0705
    // counterfeit (a retro-band edge at ~⅓ club length on a MOVING no-ball frame)
    // lived exactly in the "no floor at all" hole this closes. floorFracOverride
    // (≥ 0) is the wiring's phase-ramp hook — this module stays phase-blind; −1
    // keeps the constant cfg fraction. The floor filters candidates inside
    // measureHeadRadius, never the scan range — a true terminus beyond a
    // sub-floor counterfeit still wins; nothing past it ⇒ no measurement
    // (honest pred), never a blessed counterfeit.
    const double frac = (floorFracOverride >= 0.0) ? floorFracOverride : ctx.cfg.measFloorFrac;
    const double lHat = (lPx > 0.0) ? lPx : fallbackCeilPx;
    if (lHat > 0.0) b.rFloor = std::max(b.rFloor, frac * lHat);
    return b;
}

double headPrior(double lPx, bool quasiStill)
{
    return (quasiStill && lPx > 0.0) ? lPx : kNaN;
}

// ── H1 per-frame terminus ────────────────────────────────────────────────────
HeadMeasurement measureHeadRadius(const cv::Mat &gray32, const cv::Mat &prev32,
                                  const cv::Mat &bg32, const cv::Mat &gxs, const cv::Mat &gys,
                                  const HeadSceneCtx &ctx, double gx, double gy, double thetaDeg,
                                  double rLo, double rHi, double rFloor, double lPrior)
{
    const ClubheadConfig &cfg = ctx.cfg;
    HeadMeasurement none;   // rPx NaN, conf 0

    const int nr = int(rHi - rLo);
    if (nr < cfg.localWin + 4 || gray32.empty() || gxs.empty() || gys.empty()) return none;

    const double th = thetaDeg * kPi / 180.0;
    const double c = std::cos(th), s = std::sin(th);
    const double nx = -s, ny = c;
    const bool haveScene = !ctx.sceneMed.empty();
    const bool havePerm  = !ctx.sceneMedGx.empty() && !ctx.sceneMedGy.empty();
    const double widths[3] = { cfg.edgeWidthThin, cfg.edgeWidthBloom, cfg.edgeWidthBlade };

    // Accumulators (max over lateral offsets).
    std::vector<char>  hit(nr, 0);
    std::vector<float> ep(nr, 0.f), eChg(nr, 0.f), eMot(nr, 0.f), epMed(nr, 0.f);

    // Reusable per-offset / per-width scratch.
    std::vector<float> X(nr), Y(nr), Iv, sceneV, bgV, prevV, ew, tmpAx, tmpAy, tmpBx, tmpBy;
    std::vector<float> epU(nr), pmU(nr), chU(nr), moU(nr);

    for (double u : ctx.offsets) {
        for (int k = 0; k < nr; ++k) {
            const double R = rLo + double(k);
            X[k] = float(gx + R * c + u * nx);
            Y[k] = float(gy + R * s + u * ny);
        }
        // multi-width ridge (club) + permanence veto (sceneMed's own edge-pair)
        std::fill(epU.begin(), epU.end(), 0.f);
        std::fill(pmU.begin(), pmU.end(), 0.f);
        for (double w : widths) {
            edgePair(gxs, gys, X, Y, nx, ny, w, cfg.tauP, ew, tmpAx, tmpAy, tmpBx, tmpBy);
            for (int k = 0; k < nr; ++k) epU[k] = std::max(epU[k], ew[k]);
            if (havePerm) {
                edgePair(ctx.sceneMedGx, ctx.sceneMedGy, X, Y, nx, ny, w, cfg.tauP, ew,
                         tmpAx, tmpAy, tmpBx, tmpBy);
                for (int k = 0; k < nr; ++k) pmU[k] = std::max(pmU[k], ew[k]);
            }
        }
        sampleShifted(gray32, X, Y, 0.f, 0.f, Iv);
        if (haveScene) sampleShifted(ctx.sceneMed, X, Y, 0.f, 0.f, sceneV);
        sampleShifted(bg32,   X, Y, 0.f, 0.f, bgV);
        sampleShifted(prev32, X, Y, 0.f, 0.f, prevV);
        for (int k = 0; k < nr; ++k) {
            chU[k] = haveScene ? float(clip01(std::abs(Iv[k] - sceneV[k]) / cfg.tauChg)) : 1.f;
            const double mo = std::min(std::abs(Iv[k] - bgV[k]), std::abs(Iv[k] - prevV[k]));
            moU[k] = float(clip01(mo / cfg.tauM));
            const bool h = (std::max(epU[k], moU[k]) > cfg.hitThr)
                        && (std::max(chU[k], moU[k]) > cfg.hitThr)
                        && (pmU[k] < cfg.permThr);
            if (h) hit[k] = 1;
            ep[k]    = std::max(ep[k],    epU[k]);
            eChg[k]  = std::max(eChg[k],  chU[k]);
            eMot[k]  = std::max(eMot[k],  moU[k]);
            epMed[k] = std::max(epMed[k], pmU[k]);
        }
    }

    // local sustainment = boxcar(hit, localWin) >= localFrac. Replicates
    // np.convolve(hit, ones(L)/L, 'same') >= localFrac exactly (zero-padded).
    const int L = cfg.localWin;
    const int startOff = (L - 1) / 2;
    const double need = cfg.localFrac * double(L);
    std::vector<char> local(nr, 0);
    {
        // prefix sums of hit for O(nr) window counts
        std::vector<int> ps(nr + 1, 0);
        for (int k = 0; k < nr; ++k) ps[k + 1] = ps[k] + hit[k];
        for (int i = 0; i < nr; ++i) {
            const int m = i + startOff;
            const int lo = std::max(0, m - L + 1);
            const int hi = std::min(nr - 1, m);
            const int cnt = (hi >= lo) ? ps[hi + 1] - ps[lo] : 0;
            local[i] = (double(cnt) >= need) ? 1 : 0;
        }
    }
    int first = -1;
    for (int k = 0; k < nr; ++k) if (local[k]) { first = k; break; }
    if (first < 0) return none;

    // tail-quality channel q_ec = 0.5·ep + 0.5·min(max(e_chg,e_mot),1)
    std::vector<double> qec(nr);
    for (int k = 0; k < nr; ++k)
        qec[k] = 0.5 * ep[k] + 0.5 * std::min(1.0, double(std::max(eChg[k], eMot[k])));

    // candidate termini = ends of contiguous locally-sustained segments; score
    // each by tail evidence quality × length prior. The gap-tolerant "always the
    // LAST sample" over-ran into moving-shadow junk — score-and-pick instead.
    const int tailWin = int(kTailWinPx);
    double bestScore = -1.0, bestSupport = 0.0, bestTail = 0.0; int bestEi = -1;
    std::vector<double> scores;
    int k = 0;
    while (k < nr) {
        if (!local[k]) { ++k; continue; }
        const int s0 = k;
        while (k < nr && local[k]) ++k;
        const int ei = k - 1;   // last locally-sustained index of this segment

        if (rFloor >= 0.0 && (rLo + double(ei)) < rFloor) continue;      // club-longer-than-arm
        if (double(first) > cfg.startFrac * double(ei)) continue;        // grip connection
        int cnt = 0; for (int q = first; q <= ei; ++q) cnt += hit[q];
        const double support = (ei >= first) ? double(cnt) / double(ei - first + 1) : 0.0;
        if (support < cfg.supportMin) continue;
        const int t0 = std::max(s0, ei - tailWin);
        double tsum = 0.0; for (int q = t0; q <= ei; ++q) tsum += qec[q];
        const double tailQ = tsum / double(ei - t0 + 1);
        double prior = 1.0;
        if (std::isfinite(lPrior)) {
            const double sig = std::max(cfg.priorSigmaFrac * lPrior, cfg.priorSigmaFloorPx);
            const double d = (rLo + double(ei) - lPrior) / sig;
            prior = std::exp(-0.5 * d * d);
        }
        const double score = (kScoreTailW * tailQ + (1.0 - kScoreTailW) * std::min(support / kSupportNorm, 1.0)) * prior;
        scores.push_back(score);
        if (score > bestScore) { bestScore = score; bestEi = ei; bestSupport = support; bestTail = tailQ; }
    }
    if (bestEi < 0) return none;

    double conf = clip01(kConfSupportW * std::min(bestSupport / kSupportNorm, 1.0) + (1.0 - kConfSupportW) * bestTail);
    // ambiguity shaping: a near-tied runner-up means the pick is a coin flip —
    // confidence must say so (honesty). sqrt(1 − s2/s1), floored.
    if (scores.size() > 1 && bestScore > 1e-9) {
        std::sort(scores.begin(), scores.end(), std::greater<double>());
        const double s2s1 = scores[1] / scores[0];
        conf *= std::sqrt(std::max(cfg.ambigFloor, std::min(1.0, 1.0 - s2s1)));
    }

    HeadMeasurement out;
    out.rPx  = rLo + double(bestEi);
    out.conf = clip01(conf);
    return out;
}

bool isFlipSuspect(double confFwd, double confOpp, const ClubheadConfig &cfg)
{
    return confOpp > std::max(cfg.flipConfRatio * confFwd, cfg.flipConfAbs);
}

// ── H2 Kalman ────────────────────────────────────────────────────────────────
HeadKf1D::HeadKf1D(double dt, const ClubheadConfig &cfg)
    : m_dt(dt)
{
    const double q = cfg.sigmaAcc * cfg.sigmaAcc;
    const double dt2 = dt * dt, dt3 = dt2 * dt, dt4 = dt2 * dt2;
    m_Q00 = q * dt4 / 4.0; m_Q01 = q * dt3 / 2.0;
    m_Q10 = q * dt3 / 2.0; m_Q11 = q * dt2;
    m_gate2 = cfg.gateSig * cfg.gateSig;
    m_initP00 = cfg.initSigR * cfg.initSigR;
    m_initP11 = cfg.initSigRdot * cfg.initSigRdot;
}

void HeadKf1D::init(double r)
{
    m_x0 = r; m_x1 = 0.0;
    m_P00 = m_initP00; m_P11 = m_initP11;   // diag([initSigR², initSigRdot²])
    m_P01 = m_P10 = 0.0;
    m_hist.clear();
    m_hist.push_back({ m_x0, m_x1, m_P00, m_P01, m_P10, m_P11,
                       m_x0, m_x1, m_P00, m_P01, m_P10, m_P11 });
}

bool HeadKf1D::step(bool hasZ, double z, double sigR)
{
    // predict: xp = F x ; Pp = F P F^T + Q  (F = [[1,dt],[0,1]])
    const double dt = m_dt;
    const double xp0 = m_x0 + dt * m_x1;
    const double xp1 = m_x1;
    const double Pp00 = m_P00 + dt * m_P10 + dt * m_P01 + dt * dt * m_P11 + m_Q00;
    const double Pp01 = m_P01 + dt * m_P11 + m_Q01;
    const double Pp10 = m_P10 + dt * m_P11 + m_Q10;
    const double Pp11 = m_P11 + m_Q11;

    bool accepted = false;
    if (hasZ) {
        const double S = Pp00 + sigR * sigR;
        const double innov = z - xp0;
        if (innov * innov <= m_gate2 * S) {
            const double K0 = Pp00 / S, K1 = Pp10 / S;   // K = Pp[:,0]/S
            m_x0 = xp0 + K0 * innov;
            m_x1 = xp1 + K1 * innov;
            // P = Pp − K · Pp[0,:]   (Pp[0,:] = [Pp00, Pp01])
            m_P00 = Pp00 - K0 * Pp00; m_P01 = Pp01 - K0 * Pp01;
            m_P10 = Pp10 - K1 * Pp00; m_P11 = Pp11 - K1 * Pp01;
            accepted = true;
        }
    }
    if (!accepted) {
        m_x0 = xp0; m_x1 = xp1;
        m_P00 = Pp00; m_P01 = Pp01; m_P10 = Pp10; m_P11 = Pp11;
    }
    m_hist.push_back({ m_x0, m_x1, m_P00, m_P01, m_P10, m_P11,
                       xp0, xp1, Pp00, Pp01, Pp10, Pp11 });
    return accepted;
}

void HeadKf1D::trimTail(int n)
{
    const int keep = std::max(0, int(m_hist.size()) - std::max(0, n));
    m_hist.resize(size_t(keep));
}

void HeadKf1D::rts(std::vector<double> &rOut, std::vector<double> &varOut) const
{
    const int n = int(m_hist.size());
    rOut.assign(n, 0.0); varOut.assign(n, 0.0);
    if (n == 0) return;
    // local copies of the filtered posteriors (overwritten backward to smoothed)
    std::vector<double> x0(n), x1(n);
    std::vector<Mat2>   P(n);
    for (int i = 0; i < n; ++i) {
        x0[i] = m_hist[i].x0; x1[i] = m_hist[i].x1;
        P[i]  = { m_hist[i].P00, m_hist[i].P01, m_hist[i].P10, m_hist[i].P11 };
    }
    const double dt = m_dt;
    const Mat2 Ft{ 1.0, 0.0, dt, 1.0 };   // F^T
    for (int kk = n - 2; kk >= 0; --kk) {
        // predicted INTO k+1 from the filtered posterior at k == stored hist[k+1]
        const Hist &h1 = m_hist[kk + 1];
        const double xp0 = h1.xp0, xp1 = h1.xp1;
        const Mat2 Pp{ h1.Pp00, h1.Pp01, h1.Pp10, h1.Pp11 };
        // C = P[k] · F^T · Pp^{-1}
        const Mat2 C = mul2(mul2(P[kk], Ft), inv2(Pp));
        const double d0 = x0[kk + 1] - xp0, d1 = x1[kk + 1] - xp1;
        x0[kk] += C.a * d0 + C.b * d1;
        x1[kk] += C.c * d0 + C.d * d1;
        // P[k] += C · (P[k+1] − Pp) · C^T
        const Mat2 D{ P[kk + 1].a - Pp.a, P[kk + 1].b - Pp.b, P[kk + 1].c - Pp.c, P[kk + 1].d - Pp.d };
        const Mat2 Ct{ C.a, C.c, C.b, C.d };
        const Mat2 upd = mul2(mul2(C, D), Ct);
        P[kk].a += upd.a; P[kk].b += upd.b; P[kk].c += upd.c; P[kk].d += upd.d;
    }
    for (int i = 0; i < n; ++i) { rOut[i] = x0[i]; varOut[i] = P[i].a; }
}

// ── H2 temporal model ────────────────────────────────────────────────────────
std::vector<HeadFrameResult> runHeadTemporal(const HeadTemporalInput &in)
{
    const ClubheadConfig &cfg = in.cfg;
    const int nf = int(in.thetaDeg.size());
    std::vector<HeadFrameResult> out(size_t(std::max(0, nf)));
    if (nf == 0) return out;

    // participating frames (valid θ), in decode order — the Python `frames` list.
    std::vector<int> F;
    F.reserve(nf);
    for (int i = 0; i < nf; ++i) if (!std::isnan(in.thetaDeg[i])) F.push_back(i);
    if (F.empty()) return out;

    auto zv     = [&](int i) { return (i < int(in.z.size()))     ? in.z[i]     : kNaN; };
    auto zc     = [&](int i) { return (i < int(in.zconf.size())) ? in.zconf[i] : 0.0; };
    auto s1meas = [&](int i) { return (i < int(in.s1IsMeas.size())    && in.s1IsMeas[i]); };
    auto flip   = [&](int i) { return (i < int(in.flipSuspect.size()) && in.flipSuspect[i]); };
    auto redge  = [&](int i) { return (i < int(in.rEdge.size())) ? in.rEdge[i] : 0.0; };

    // segment breaks where stage-1 θ jumps > segThetaJump (a re-init moved the
    // whole ray — radial continuity across it is meaningless). Circular diff so a
    // ±180° wrap in the decided θ isn't a false break (deviation from the Python,
    // which pre-unwraps theta_u; safer here).
    std::vector<char> segBreak(size_t(nf), 0);
    for (size_t j = 1; j < F.size(); ++j) {
        double d = std::fmod(in.thetaDeg[F[j]] - in.thetaDeg[F[j - 1]] + 180.0, 360.0);
        if (d < 0) d += 360.0; d -= 180.0;
        if (std::abs(d) > cfg.segThetaJump) segBreak[size_t(F[j])] = 1;
    }

    // KF pass — segmented; coasted tails trimmed at budget overrun.
    struct Segment { std::vector<int> frames; HeadKf1D kf; };
    std::vector<Segment> segments;
    std::vector<char> accepted(size_t(nf), 0);
    std::unique_ptr<HeadKf1D> kf;
    std::vector<int> segFrames;
    int coast = 0;
    for (int f : F) {
        const bool hasZ = std::isfinite(zv(f)) && zc(f) >= cfg.confMeasMin;
        if (kf && segBreak[size_t(f)]) {
            segments.push_back({ std::move(segFrames), *kf });
            kf.reset(); segFrames.clear(); coast = 0;
        }
        if (!kf) {
            if (hasZ) {
                kf = std::make_unique<HeadKf1D>(in.dt, cfg);
                kf->init(zv(f));
                segFrames = { f };
                accepted[size_t(f)] = 1;
                coast = 0;
            }
            continue;
        }
        const double sigR = cfg.measSigBase + (1.0 - zc(f)) * cfg.measSigSlope;
        const bool ok = kf->step(hasZ, hasZ ? zv(f) : 0.0, sigR);
        accepted[size_t(f)] = (ok && hasZ) ? 1 : 0;
        segFrames.push_back(f);
        coast = accepted[size_t(f)] ? 0 : coast + 1;
        const int budget = (std::abs(kf->rdot()) > cfg.fastRdot) ? cfg.coastFast : cfg.coastSlow;
        if (coast > budget) {
            const int trim = std::min(coast, int(segFrames.size()) - 1);
            for (int t = int(segFrames.size()) - trim; t < int(segFrames.size()); ++t)
                accepted[size_t(segFrames[t])] = 0;               // trimmed coast carries no info
            kf->trimTail(trim);
            segFrames.resize(size_t(int(segFrames.size()) - trim));
            segments.push_back({ std::move(segFrames), *kf });
            kf.reset(); segFrames.clear(); coast = 0;
        }
    }
    if (kf && !segFrames.empty()) segments.push_back({ std::move(segFrames), *kf });

    // per-segment RTS (never across a break); segments < 2 contribute no smoothed r.
    std::vector<double> rSm(size_t(nf), kNaN), sigSm(size_t(nf), kNaN);
    for (const Segment &seg : segments) {
        if (seg.frames.size() < 2) continue;
        std::vector<double> rs, vs;
        seg.kf.rts(rs, vs);
        const int m = std::min(int(seg.frames.size()), int(rs.size()));
        for (int i = 0; i < m; ++i) {
            rSm[size_t(seg.frames[i])]   = rs[i];
            sigSm[size_t(seg.frames[i])] = std::sqrt(std::max(vs[i], 1.0));
        }
    }

    // confirmed runs (F7/F9): accepted measurements in runs >= runMin escape the
    // re-init cap; runs tolerate a single-frame dropout hole. Python's `k<2`
    // branch is a documented no-op for confirmed runs, folded away here.
    std::vector<double> confOut(size_t(nf), kNaN);
    std::vector<int> run;
    auto flush = [&](std::vector<int> &r) {
        const bool shortRun = int(r.size()) < cfg.runMin;
        for (int f : r) confOut[size_t(f)] = shortRun ? std::min(zc(f), cfg.reinitCap) : zc(f);
        r.clear();
    };
    int miss = 0;
    for (int f : F) {
        if (accepted[size_t(f)]) { run.push_back(f); miss = 0; }
        else if (++miss > 1) { flush(run); miss = 0; }
    }
    flush(run);

    // smoothed-r interpolation support (frame → r) for pred-tier bridging.
    std::vector<int> smF; std::vector<double> smV;
    for (int f : F) if (std::isfinite(rSm[size_t(f)])) { smF.push_back(f); smV.push_back(rSm[size_t(f)]); }
    auto interpR = [&](int f) -> double {
        if (smF.empty()) return kNaN;
        if (f <= smF.front()) return smV.front();
        if (f >= smF.back())  return smV.back();
        // smF ascending — linear interp (np.interp semantics)
        int lo = 0, hi = int(smF.size()) - 1;
        while (hi - lo > 1) { const int mid = (lo + hi) / 2; (smF[mid] <= f ? lo : hi) = mid; }
        const double t = double(f - smF[lo]) / double(smF[hi] - smF[lo]);
        return smV[lo] + t * (smV[hi] - smV[lo]);
    };

    const double lp = (in.lPx > 0.0) ? in.lPx : kNaN;   // ball length replaces the self-fit L_pred

    // assemble tiers
    for (int f : F) {
        const double re = redge(f);
        double lhat = rSm[size_t(f)];
        if (std::isnan(lhat)) lhat = interpR(f);
        if (std::isnan(lhat) && std::isfinite(lp)) lhat = lp;
        const bool s1pred = !s1meas(f) || flip(f);
        const double sig = sigSm[size_t(f)];
        const bool inConf = std::isfinite(confOut[size_t(f)]);
        const bool acc = accepted[size_t(f)] != 0;

        HeadTier tier = HeadTier::Pred;
        double rOut = lhat;          // may be NaN
        double cOut = 0.2;

        if (std::isfinite(lhat) && re < cfg.offFactor * lhat) {
            tier = HeadTier::Off; rOut = kNaN;
        } else if (inConf && acc && zc(f) >= 0.5 && confOut[size_t(f)] > cfg.reinitCap && !s1pred) {
            tier = HeadTier::Meas;
            rOut = std::isfinite(rSm[size_t(f)]) ? rSm[size_t(f)] : zv(f);
            cOut = confOut[size_t(f)];
        } else if (inConf && acc && !s1pred && zc(f) >= cfg.confMeasMin
                   && confOut[size_t(f)] > cfg.reinitCap && std::isfinite(sig) && sig <= cfg.sigMeasMax) {
            // posterior-σ meas (H2): an accepted measurement inside a confirmed,
            // tightly-converged segment is label-grade even when the single frame
            // is blurry. The zconf floor keeps genuinely weak evidence out.
            tier = HeadTier::Meas;
            rOut = std::isfinite(rSm[size_t(f)]) ? rSm[size_t(f)] : zv(f);
            cOut = std::min(0.9, 0.5 * zc(f) + 0.5 * (1.0 - sig / cfg.sigMeasMax));
        } else if (inConf && acc) {
            // stage-1 pred frames: the ray is a kinematic guess so the emitted
            // position can't beat it — the radial measurement still fed the filter
            // but the OUTPUT stays pred-tier.
            tier = HeadTier::Pred;
            rOut = std::isfinite(rSm[size_t(f)]) ? rSm[size_t(f)] : zv(f);
            cOut = std::min(confOut[size_t(f)], cfg.reinitCap);
        }
        if (std::isfinite(rOut) && re > 0.0) rOut = std::min(rOut, re);

        HeadFrameResult &r = out[size_t(f)];
        r.tier     = tier;
        r.rOut     = (tier == HeadTier::Off) ? kNaN : rOut;
        r.headConf = float(cOut);
        r.sigmaR   = sig;   // NaN when no smoothed value
    }
    return out;
}

} // namespace pinpoint::analysis
