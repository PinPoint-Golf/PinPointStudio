/*
 * PinPoint Studio — the shaft's angle to the ground as the down-the-line camera sees it
 *
 * Copyright (C) 2026 Mark Liversedge
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 */
#pragma once

// ── shaftLie — the shaft's angle to the ground, down the line ───────────────────────────
//
// What a coach reads off a down-the-line frame at address and again at impact: how upright the
// shaft stands. The ANGLE BETWEEN THE SHAFT LINE AND THE GROUND in the DTL image, 0° lying flat
// along it and 90° straight up, read at Address and at Impact, with the card's Δ = impact −
// address: POSITIVE means the shaft came back STEEPER (more upright) than it was set up, negative
// that it came back FLATTER (Mark, 2026-10-01: "+ve delta is steepening").
//
// It is the shaft's inclination, not the sole's lie — a fitter's dynamic lie is this angle plus
// the club's own built-in lie, which is a constant per club, so the Δ is the same number either
// way. The launch monitor's `lm.lieAngle` (the sole toe-up at impact) is a different quantity.
//
// UNSIGNED FOLD, deliberately. The line's angle to the ground folds at the horizontal and at the
// vertical: a shaft at 95° past vertical reads 85°. At address and impact the shaft sits at
// 55–65° on every club, nowhere near either fold, which is where this metric is read; between
// them the curve rises to the vertical around P3 and P5 and falls toward the ground at the top,
// and a signed form would have to invent a side for the shaft to have passed on. The fold also
// needs no ball-side convention — it reads the same for a golfer facing either way — so it does
// not inherit the posture producer's feet-and-ball refusals.
//
// THE CAMERA. The absolute angle depends on where the DTL camera stands: a camera above hand
// height or off the target line projects the shaft at a different inclination, and the corpus
// camera is both. Within one swing the camera does not move, so the Δ is sound; the two readings
// are comparable within a session and not across cameras.
//
// WHAT IS READ. Only MEASURED DTL samples (dtlMeasured — Ray, Seg or Band tiers), so a held or
// predicted frame never becomes a reading. Address and impact are the two bands where the DTL
// shaft runs down the trouser line and the tracker stands on the grip→ball prior (dtl_shaft_track.h
// §4.3), so a swing whose DTL ball was not found usually has no sample at either instant, and the
// phase reading is then simply absent — the card prints "—". The curve carries every DTL frame of
// the span so it plots, with the gaps bridged by interpolation and flagged `valid` 0, which the
// chart draws as not-measured; a reading is only ever taken off a frame whose `valid` is 1.

#include "dtl_shaft_track.h"
#include "../Core/pp_tuned_constants.h"   // tuned::uncertainty (σ budget)
#include "swing_analysis.h"

#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace pinpoint::analysis {

struct DtlShaftLieResult {
    bool    valid = false;
    QString reason;                      // why not, when not
    MetricSeries series;                 // key "shaftLie"
    int     nMeasured = 0;               // DTL frames the curve was read from
    double  addressDeg = std::numeric_limits<double>::quiet_NaN();
    double  impactDeg  = std::numeric_limits<double>::quiet_NaN();
    int64_t addressSnapUs = -1, impactSnapUs = -1;   // |reading − phase instant|, −1 when absent
};

// The fold: the angle between the line through θ and the image horizontal, 0..90°.
inline double dtlShaftLieDeg(double thetaRad)
{
    return std::atan2(std::fabs(std::sin(thetaRad)), std::fabs(std::cos(thetaRad))) * 180.0 / M_PI;
}

// `maxSnapUs`: a phase reading is the nearest MEASURED sample to the phase's instant, and only
// within this of it — one and a half frames at 120 fps. Further than that and the band that
// covers the instant is missing, which is an absence, not a reading from the nearest band that
// does exist.
// `withSigma` (uncertainty design §6): each reading carries its frame's σθ_D (the fold has slope
// ±1) ⊕ the lie's rate there × the instant's timing σ (sigTAddrUs / sigTImpUs), and the frame's
// gross risk. The DTL σ is address-calibrated and propagated beyond it, so the kind is Propagated
// unless the per-metric calibration said otherwise.
inline DtlShaftLieResult buildDtlShaftLie(const DtlShaftTrack2D &track,
                                          const std::vector<PhaseEvent> &phases,
                                          int64_t maxSnapUs = 12500,
                                          bool withSigma = false,
                                          double sigTAddrUs = 0.0, double sigTImpUs = 0.0)
{
    DtlShaftLieResult res;
    auto refuse = [&res](const QString &why) { res.reason = why; return res; };
    if (!track.valid || track.samples.empty()) return refuse(QStringLiteral("no down-the-line shaft track"));

    auto phaseUs = [&phases](Phase p) -> std::optional<int64_t> {
        for (const PhaseEvent &e : phases) if (e.phase == p) return e.t_us;
        return std::nullopt;
    };
    const auto addrUs = phaseUs(Phase::Address);
    if (!addrUs) return refuse(QStringLiteral("no Address on the ladder"));
    const auto impactUs = phaseUs(Phase::Impact);

    MetricSeries &m = res.series;
    m.key   = QStringLiteral("shaftLie");
    m.label = QStringLiteral("Shaft lie");
    m.unit  = QStringLiteral("°");

    // Every frame, on the face-on clock; measured frames carry their fold, the rest are
    // bridged below. `tier` is the whole gate: a Held frame inside a band has a finite θ too,
    // and it is not a measurement.
    struct Pt { int64_t t; double v; bool measured; };
    std::vector<Pt> pts;
    pts.reserve(track.samples.size());
    for (const DtlSample &s : track.samples) {
        const bool meas = dtlMeasured(s.tier) && std::isfinite(s.thetaRad);
        pts.push_back({ s.t_us - track.clockOffsetUs, meas ? dtlShaftLieDeg(s.thetaRad) : 0.0, meas });
        if (meas) ++res.nMeasured;
    }
    if (res.nMeasured == 0) return refuse(QStringLiteral("no measured down-the-line shaft frame"));
    std::stable_sort(pts.begin(), pts.end(), [](const Pt &a, const Pt &b) { return a.t < b.t; });

    // Bridge the unmeasured frames: linear between the measured neighbours, held flat past the
    // first and last measurement. `valid` says which is which.
    {
        int prev = -1;
        for (int i = 0; i < int(pts.size()); ++i) {
            if (pts[size_t(i)].measured) { prev = i; continue; }
            int next = i + 1;
            while (next < int(pts.size()) && !pts[size_t(next)].measured) ++next;
            if (prev < 0 && next >= int(pts.size())) break;               // unreachable: nMeasured > 0
            if (prev < 0)                       pts[size_t(i)].v = pts[size_t(next)].v;
            else if (next >= int(pts.size()))   pts[size_t(i)].v = pts[size_t(prev)].v;
            else {
                const Pt &a = pts[size_t(prev)], &b = pts[size_t(next)];
                const double w = b.t > a.t ? double(pts[size_t(i)].t - a.t) / double(b.t - a.t) : 0.0;
                pts[size_t(i)].v = a.v + w * (b.v - a.v);
            }
        }
    }
    m.t_us.reserve(pts.size()); m.value.reserve(pts.size()); m.valid.reserve(pts.size());
    for (const Pt &p : pts) {
        m.t_us.push_back(p.t);
        m.value.push_back(p.v);
        m.valid.push_back(p.measured ? 1 : 0);
    }

    // The two readings: the nearest MEASURED frame within maxSnapUs of the instant.
    auto readAt = [&](Phase ph, int64_t at, double &outDeg, int64_t &outSnap) {
        int best = -1;
        int64_t bestD = std::numeric_limits<int64_t>::max();
        for (int i = 0; i < int(pts.size()); ++i) {
            if (!pts[size_t(i)].measured) continue;
            const int64_t d = std::llabs(pts[size_t(i)].t - at);
            if (d < bestD) { bestD = d; best = i; }
        }
        if (best < 0 || bestD > maxSnapUs) return;
        outDeg  = pts[size_t(best)].v;
        outSnap = bestD;
        PhaseSample ps{ ph, at, outDeg, QString() };
        if (withSigma) {
            // The DTL sample the reading came from, by time (pts is sorted; samples are not).
            const DtlSample *src = nullptr;
            for (const DtlSample &s : track.samples)
                if (s.t_us - track.clockOffsetUs == pts[size_t(best)].t && dtlMeasured(s.tier)) { src = &s; break; }
            if (src && std::isfinite(src->sigmaThetaDeg)) {
                // The lie's rate at the reading: the neighbouring measured frames' slope.
                double rate = 0.0;
                int a = best - 1, b = best + 1;
                while (a >= 0 && !pts[size_t(a)].measured) --a;
                while (b < int(pts.size()) && !pts[size_t(b)].measured) ++b;
                if (a >= 0 && b < int(pts.size()) && pts[size_t(b)].t > pts[size_t(a)].t)
                    rate = (pts[size_t(b)].v - pts[size_t(a)].v) / (double(pts[size_t(b)].t - pts[size_t(a)].t) * 1e-6);
                const double sigT = ph == Phase::Address ? sigTAddrUs : sigTImpUs;
                const double sig  = std::sqrt(src->sigmaThetaDeg * src->sigmaThetaDeg
                                              + std::pow(std::abs(rate) * sigT * 1e-6, 2.0))
                                    * tuned::uncertainty::kInflateLie;
                ps.sigma     = sig;
                ps.sigmaKind = uint8_t(tuned::uncertainty::kCalLie ? SigmaKind::Calibrated : SigmaKind::Propagated);
                ps.grossRisk = float(std::isfinite(src->pGross) ? src->pGross : -1.0);
            }
        }
        m.phaseSamples.push_back(ps);
    };
    readAt(Phase::Address, *addrUs, res.addressDeg, res.addressSnapUs);
    if (impactUs) readAt(Phase::Impact, *impactUs, res.impactDeg, res.impactSnapUs);

    res.valid = true;
    return res;
}

} // namespace pinpoint::analysis
