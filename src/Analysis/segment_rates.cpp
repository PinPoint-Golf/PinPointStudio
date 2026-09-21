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

#include "segment_rates.h"

#include "angular_rate.h"        // localRates, placePeak
#include "metric_channel.h"      // phaseTimeOpt, interpChannel, nearestIndex
#include "../Diagnostics/anatomy_vocabulary.h"   // kp:: COCO indices

#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pinpoint::analysis {

namespace {

constexpr double kPi       = 3.14159265358979323846;
constexpr double kRadToDeg = 180.0 / kPi;

// Unwrap an angle series in place: each sample within π of its predecessor. NaN samples are
// skipped and do not break the chain.
void unwrapInPlace(std::vector<double> &a)
{
    double prev = std::nan("");
    for (double &x : a) {
        if (!std::isfinite(x)) continue;
        if (std::isfinite(prev)) {
            double d = x - prev;
            while (d >  kPi) { x -= 2.0 * kPi; d = x - prev; }
            while (d <= -kPi) { x += 2.0 * kPi; d = x - prev; }
        }
        prev = x;
    }
}

double medianOf(std::vector<double> v)
{
    v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return !std::isfinite(x); }), v.end());
    if (v.empty()) return 0.0;
    std::nth_element(v.begin(), v.begin() + long(v.size() / 2), v.end());
    return v[v.size() / 2];
}

// ── The shared tail: angle-with-σ → rate series → node ─────────────────────────────────────────

struct AngleTrack {
    std::vector<int64_t> t;
    std::vector<double>  angleRad;     // unwrapped
    std::vector<double>  sigmaRad;     // per sample
    std::vector<uint8_t> valid;        // per sample admission (empty ⇒ all)
    double               extraRelSigma = 0.0;   // added to the RATE σ as a fraction of |rate| (no-plane case)
    double               addressTurnRad = 0.0;  // span routes: how far from square the address read (§12.4)
    std::vector<uint8_t> nearSquare;             // span routes: admitted but dropped inside the sin floor
};

struct RateTrack {
    std::vector<int64_t> t;
    std::vector<double>  dps;          // signed °/s (NaN where the window could not fit)
    std::vector<double>  sigmaDps;
    std::vector<uint8_t> valid;
};

// Differentiate an angle track into °/s. `sign` = +1 or −1 applied to the derivative; `magnitude`
// takes |rate| (arm and club, whose sign carries no meaning the reader wants).
RateTrack differentiate(const AngleTrack &a, int64_t windowUs, double sign, bool magnitude)
{
    RateTrack r;
    r.t = a.t;
    const std::vector<RateSample> rs = localRates(a.t, a.angleRad, a.sigmaRad, a.valid, windowUs);
    r.dps.resize(rs.size());
    r.sigmaDps.resize(rs.size());
    r.valid.assign(rs.size(), 1u);
    for (size_t i = 0; i < rs.size(); ++i) {
        const bool admitted = a.valid.empty() || a.valid[i];
        if (!rs[i].ok || !admitted) {
            r.dps[i] = 0.0; r.sigmaDps[i] = 0.0; r.valid[i] = 0u;
            continue;
        }
        double v = sign * rs[i].rate * kRadToDeg;
        if (magnitude) v = std::fabs(v);
        r.dps[i]      = v;
        r.sigmaDps[i] = rs[i].rateSigma * kRadToDeg + a.extraRelSigma * std::fabs(v);
    }
    return r;
}

// Smooth a rate that a sensor already measured (IMU): the same time window, so the peak is as
// blurred as a camera route's, and σ = per-sample noise / √n.
RateTrack smoothRate(const std::vector<int64_t> &t, const std::vector<double> &dps,
                     double noiseDps, int64_t windowUs)
{
    RateTrack r;
    r.t = t;
    const std::vector<RateSample> rs = localRates(t, dps, {}, {}, windowUs);
    r.dps.resize(rs.size());
    r.sigmaDps.resize(rs.size());
    r.valid.assign(rs.size(), 1u);
    for (size_t i = 0; i < rs.size(); ++i) {
        if (!rs[i].ok) { r.dps[i] = 0.0; r.sigmaDps[i] = 0.0; r.valid[i] = 0u; continue; }
        r.dps[i]      = rs[i].smoothed;
        r.sigmaDps[i] = noiseDps / std::sqrt(double(std::max(rs[i].n, 1)));
    }
    return r;
}

struct Domain {
    int64_t fromUs = -1, toUs = -1;
    bool ok() const { return fromUs >= 0 && toUs > fromUs; }
};

// Mask the rate outside the domain, stamp the metadata, sample the phases, and place the node.
// How a route is allowed to claim a node. `may` is the route-level switch (§9 / §12). `sighted`,
// when set, is a per-sample mask parallel to the rate: the peak is searched over sighted samples
// only, and a peak found at the edge of sight — within one derivative window of the instant the
// segment entered the blind band (`blindFromUs`) or left it (`blindToUs`) — is not a peak the
// route saw: the rate was still rising when the view went blind. The node is then bounded, not
// placed (design §12.4).
struct PlacementGate {
    bool                        may         = true;
    const std::vector<uint8_t> *sighted     = nullptr;
    int64_t                     blindFromUs = -1;
    int64_t                     blindToUs   = -1;
    // The reversal-spike rule (below). Implied by `sighted` for the span rung; set explicitly by
    // a route that wants it without a blind band.
    bool                        spikeGuard  = false;
    // FOR A ROUTE WITH NO BLIND BAND. The span rung's bounds are about the part of the domain the
    // camera could SEE; a route that sees everywhere has a different way of failing to find a
    // peak — the peak is simply not inside the domain. When the rate over the last derivative
    // window before impact is rising AND is the largest valid windowed rate in the final 100 ms,
    // the honest output is "it had not peaked by the end of the domain": unplaced, with
    // peakNoEarlierThanMs = 0. That is evaluated on the LAST WINDOW, not on where the global
    // extremum happens to sit, so it is still true of a swing whose curve carries an artefact
    // somewhere earlier.
    bool                        endEdgeBound = false;
    // THE BAND IS A HOLE, NOT A HORIZON. For the span rung the blind band is the stretch near
    // square at the END of the downswing, so "the peak is at or after where sight was lost" is the
    // right edge test. For a route whose invalid samples are an INTERIOR hole, that test would
    // condemn every peak after the hole. With this set the edge test is PROXIMITY instead: a peak
    // within one derivative window of either edge of the hole was rising into it or emerging from
    // it and is bounded; a peak well clear of the hole is simply a peak.
    bool                        bandIsHole   = false;
};

void finishChannel(SegmentRateChannel &ch, RateTrack &&r, SeqSegment seg, const QString &label,
                   const QString &routeId, bool direct, const Domain &dom,
                   const std::vector<PhaseEvent> &phases, const SegmentRatesConfig &cfg,
                   std::vector<KsNode> &nodes, const PlacementGate &gate = {})
{
    if (r.t.size() < 2 || !dom.ok()) return;

    MetricSeries m;
    m.key   = QString::fromLatin1(seqSegmentSeriesKey(seg));
    m.label = label;
    m.unit  = QStringLiteral("°/s");
    m.t_us  = r.t;
    m.value = r.dps;
    m.valid = r.valid;
    for (size_t i = 0; i < m.t_us.size(); ++i)
        if (m.t_us[i] < dom.fromUs || m.t_us[i] > dom.toUs) m.valid[i] = 0u;

    // Phase samples where the ladder has the phase and the domain holds it.
    for (Phase p : { Phase::Transition, Phase::Delivery, Phase::Impact }) {
        const std::optional<int64_t> tp = phaseTimeOpt(phases, p);
        if (!tp || *tp < dom.fromUs || *tp > dom.toUs) continue;
        const int idx = nearestIndex(m.t_us, *tp);
        if (idx < 0 || !m.valid[size_t(idx)]) continue;
        m.phaseSamples.push_back({ p, *tp, m.value[size_t(idx)], QString() });
    }

    // Representative σ: the median over the domain's valid samples. Absent (0) means "not
    // characterised", so a channel whose route propagated nothing leaves the field unset.
    {
        std::vector<double> s;
        for (size_t i = 0; i < m.t_us.size(); ++i)
            if (m.valid[i] && r.sigmaDps[i] > 0.0) s.push_back(r.sigmaDps[i]);
        if (!s.empty()) m.sigma = medianOf(s);
    }
    // EMPTY MEANS EVERY SAMPLE VALID (the serialisation contract).
    if (std::find(m.valid.begin(), m.valid.end(), uint8_t(0)) == m.valid.end())
        m.valid.clear();

    ch.series      = std::move(m);
    ch.sampleSigma = std::move(r.sigmaDps);
    ch.routeId     = routeId;
    ch.direct      = direct;

    // The node: the shared peak finder over the domain — over the SIGHTED samples when the route
    // has a blind band (the series itself keeps every sample the chart should show).
    std::vector<uint8_t> searchValid;
    const size_t ns = ch.series.t_us.size();
    if (gate.sighted && gate.sighted->size() == ns) {
        searchValid.assign(ns, 1u);
        for (size_t i = 0; i < ns; ++i)
            searchValid[i] = (ch.series.valid.empty() || ch.series.valid[i]) && (*gate.sighted)[i];
    }
    SeriesView sv;
    sv.t = ch.series.t_us.data();
    sv.v = ch.series.value.data();
    sv.valid = !searchValid.empty() ? searchValid.data()
             : (ch.series.valid.empty() ? nullptr : ch.series.valid.data());
    sv.n = ns;
    const int64_t windowUs = int64_t(cfg.derivWindowMs * 1000.0);
    const PeakPlacement p = placePeak(sv, ch.sampleSigma, dom.fromUs, dom.toUs, windowUs);

    KsNode n;
    n.segment = seg;
    n.routeId = routeId;
    n.direct  = direct;
    if (p.ok) {
        n.tPeakUs      = p.tPeakUs;
        n.peakDps      = p.peak;
        n.peakSigmaDps = p.peakSigma;
        n.tSigmaMs     = p.tSigmaMs;
        // At the edge of sight: the highest sighted rate sits where the view went blind, so the
        // real peak is somewhere the route could not see. Bounded, not placed.
        bool atEntry = gate.blindFromUs >= 0 && gate.blindFromUs <= dom.toUs
                    && (gate.bandIsHole ? std::llabs(p.tPeakUs - gate.blindFromUs) <= windowUs
                                        : p.tPeakUs >= gate.blindFromUs - windowUs);
        const bool atExit = gate.blindToUs >= 0 && gate.blindToUs >= dom.fromUs
                         && (gate.bandIsHole ? std::llabs(p.tPeakUs - gate.blindToUs) <= windowUs
                                             : p.tPeakUs <= gate.blindToUs + windowUs)
                         && !atEntry;
        // RISING INTO THE BLIND BAND. An interior sighted maximum is the peak only if the rate had
        // come down by the time the view went blind. On the corpus (§12.4 item 5) every placed
        // pelvis had risen again to above its "peak" in the last two windows before the band —
        // the early bump was in sight, the real peak was not. So: the mean rate over the last
        // window before the band must be below the mean over the window before that. (Not "within
        // σ of the peak": the propagated rate σ is a third of the peak, which would refuse real
        // single-hump peaks whose tail is still high at the edge.)
        if (!atEntry && !atExit && gate.blindFromUs >= 0 && gate.blindFromUs <= dom.toUs
            && p.tPeakUs < gate.blindFromUs - windowUs) {
            double sumL = 0.0, sumE = 0.0; int nL = 0, nE = 0;
            for (size_t i = 0; i < ns; ++i) {
                if (!sv.valid || !sv.valid[i]) continue;
                const int64_t t = ch.series.t_us[i];
                if (t < gate.blindFromUs - 2 * windowUs || t >= gate.blindFromUs) continue;
                if (t >= gate.blindFromUs - windowUs) { sumL += ch.series.value[i]; ++nL; }
                else                                   { sumE += ch.series.value[i]; ++nE; }
            }
            if (nL > 0 && nE > 0 && sumL / nL > sumE / nE) atEntry = true;
        }
        // A PEAK ON THE HEELS OF A REVERSAL IS A SPIKE. A body segment that has just changed
        // direction (the rate through zero at the transition) cannot be at its peak rate one
        // window later — Cheetham's thorax peaks ~200 ms after it turns. On real spans the shoulder
        // keypoints jump as the arms cross the chest at the top, and the derivative reads that as
        // ±1000 °/s inside 30 ms. Not a node, and not a bound either: the view saw nothing there.
        bool spike = false;
        if ((gate.sighted || gate.spikeGuard) && !atEntry && !atExit) {
            int64_t lastNonPositive = -1;
            for (size_t i = 0; i < ns; ++i) {
                const int64_t t = ch.series.t_us[i];
                if (t < dom.fromUs || t > p.tPeakUs) continue;
                if (!(ch.series.valid.empty() || ch.series.valid[i])) continue;
                if (ch.series.value[i] <= 0.0) lastNonPositive = t;
            }
            spike = lastNonPositive >= 0 && p.tPeakUs - lastNonPositive < int64_t(cfg.minAfterReversalMs * 1000.0);
        }
        // A bound is a claim about the part of the domain the route SAW. When the band opens at
        // the domain start, nothing was in sight and "no earlier than the transition" is the
        // whole domain — not a bound, and not printed as one (§12.4). Likewise a band that closes
        // at impact. The node stays unplaced with no bound: "not in sight".
        const bool entryVacuous = gate.blindFromUs <= dom.fromUs + windowUs;
        const bool exitVacuous  = gate.blindToUs   >= dom.toUs   - windowUs;
        if (atEntry && !entryVacuous) n.peakNoEarlierThanMs = double(dom.toUs - gate.blindFromUs) * 1e-3;
        if (atExit  && !exitVacuous)  n.peakNoLaterThanMs   = double(dom.toUs - gate.blindToUs) * 1e-3;
        // STILL RISING AT THE END OF THE DOMAIN (a route with no blind band). The highest rate in
        // [transition, impact] sits at impact and the curve is still climbing into it: the peak is
        // after the ball, where this metric is not defined. "No earlier than 0 ms before impact"
        // IS the statement — it did not peak in the downswing — and it uses the bound field the
        // strip already prints rather than inventing a state for it. The search is NOT extended
        // past impact: whether the sequence should look there is a design decision, not a fix
        // (pair_span_turn_20260920.md G5), and the down-the-line hip confidence falls by 0.12 in
        // exactly that window.
        bool risingAtEnd = false;
        if (gate.endEdgeBound) {
            // The windowed mean of the valid samples ending at `tEnd`.
            const auto windowMean = [&](int64_t tEnd, double &out) {
                double sum = 0.0; int cnt = 0;
                for (size_t i = 0; i < ns; ++i) {
                    if (sv.valid && !sv.valid[i]) continue;
                    const int64_t t = ch.series.t_us[i];
                    if (t <= tEnd - windowUs || t > tEnd) continue;
                    sum += ch.series.value[i]; ++cnt;
                }
                if (cnt == 0) return false;
                out = sum / cnt;
                return true;
            };
            double mLast = 0.0, mPrev = 0.0;
            if (windowMean(dom.toUs, mLast) && windowMean(dom.toUs - windowUs, mPrev)
                && mLast > mPrev) {
                // …and it must be the LARGEST windowed rate in the final 100 ms, so a curve that
                // peaked 60 ms before impact and is merely ticking up again does not claim it.
                bool isMax = true;
                for (size_t i = 0; i < ns && isMax; ++i) {
                    if (sv.valid && !sv.valid[i]) continue;
                    const int64_t t = ch.series.t_us[i];
                    if (t < dom.toUs - 100000 || t >= dom.toUs) continue;
                    double m = 0.0;
                    if (windowMean(t, m) && m > mLast) isMax = false;
                }
                risingAtEnd = isMax;
            }
        }
        if (risingAtEnd) n.peakNoEarlierThanMs = 0.0;
        // Placed only when the σ is inside the threshold, the route is allowed to place this
        // segment at all (§9 / §12), and the peak was in sight. The attempt is kept for the trace.
        // ⚠ `gate.may` GATES THE RING, NEVER THE BOUND. Every bound above is computed before this
        // line and survives it: a switch that says "do not claim a peak here" is not a licence to
        // stop telling the reader what the route did establish.
        n.placed       = gate.may && !atEntry && !atExit && !spike && !risingAtEnd
                      && p.tSigmaMs <= cfg.maxPlaceSigmaMs;
    }
    nodes.push_back(n);
}

// ── Face-on geometry ───────────────────────────────────────────────────────────────────────────

struct PoseView {
    const std::vector<PoseFrame2D> *frames = nullptr;
    const std::vector<PoseKpAux>   *aux    = nullptr;   // parallel to frames when non-null
    double W = 1.0, H = 1.0;
    QPointF px(size_t i, int k) const
    {
        const QPointF &p = (*frames)[i].kp[size_t(k)];
        return QPointF(p.x() * W, p.y() * H);
    }
    float conf(size_t i, int k) const { return (*frames)[i].conf[size_t(k)]; }
    double sigmaPx(size_t i, int k, double fallback) const
    {
        if (aux && i < aux->size()) {
            const float s = (*aux)[i].sigma[size_t(k)];
            if (s > 0.f) return double(s);
        }
        return fallback;
    }
};

// The unfolded turn of a body line from its foreshortened span (design §5.3, reference per
// §12.4). `a` and `b` are the line's two keypoints. Returns an empty track when the reference
// cannot be formed.
//
// THE REFERENCE IS THE WIDEST THE LINE EVER IMAGES, not the address span. A span goes as
// w₀·cos θ, so the widest the camera ever sees the line is the closest it came to square — and on
// all 61 corpus swings that was NOT address: the address span sat 3.6 % (hips) / 5.6 % (shoulders)
// below the downswing maximum, which an address reference reads as 15° / 19° of turn at address
// and then clamps through the whole downswing (the 2026-09-17 spike). The square-up instant the
// sign unfold already locates is where the reference is read, robustly; the address median is
// kept as a floor for a golfer who was widest at address.
AngleTrack spanTurnTrack(const PoseView &pv, int a, int b, int64_t addressUs, int64_t searchFromUs,
                         int64_t searchToUs, const SegmentRatesConfig &cfg)
{
    AngleTrack out;
    const size_t n = pv.frames->size();
    if (n < 2) return out;

    std::vector<double> span(n, std::nan(""));
    std::vector<uint8_t> ok(n, 0u);
    for (size_t i = 0; i < n; ++i) {
        if (pv.conf(i, a) < cfg.confMin || pv.conf(i, b) < cfg.confMin) continue;
        const QPointF d = pv.px(i, a) - pv.px(i, b);
        span[i] = std::hypot(d.x(), d.y());
        ok[i] = 1u;
    }

    // Address span: median over the address window; fall back to the first admitted frames when
    // the window is thin. A floor on the reference, and the diagnostic §12.4 reports.
    std::vector<double> ref;
    for (size_t i = 0; i < n; ++i) {
        const int64_t t = (*pv.frames)[i].t_us;
        if (!ok[i]) continue;
        if (addressUs >= 0 && (t < addressUs || t > addressUs + cfg.addrWindowUs)) continue;
        ref.push_back(span[i]);
    }
    if (int(ref.size()) < cfg.addrMinFrames) {
        ref.clear();
        for (size_t i = 0; i < n && int(ref.size()) < cfg.addrMinFrames; ++i)
            if (ok[i]) ref.push_back(span[i]);
    }
    if (int(ref.size()) < cfg.addrMinFrames) return out;
    const double wAddr = medianOf(ref);
    if (!(wAddr >= cfg.minSpanPx)) return out;

    // The square-up instant: the span maximum inside the downswing search window, read off a
    // local median so a single wide sample does not set it. Before it the body is still closed
    // (turn positive), after it open (negative). A ladder inference, which is why the rung is
    // Estimated — see the header.
    const int64_t halfUs = int64_t(cfg.derivWindowMs * 500.0);
    const auto localMedian = [&](size_t i) {
        std::vector<double> v;
        const int64_t ti = (*pv.frames)[i].t_us;
        for (size_t j = 0; j < n; ++j) {
            const int64_t tj = (*pv.frames)[j].t_us;
            if (ok[j] && tj >= ti - halfUs && tj <= ti + halfUs) v.push_back(span[j]);
        }
        return v.empty() ? span[i] : medianOf(v);
    };
    int64_t tSq = std::numeric_limits<int64_t>::max();
    double  wSq = -1.0;
    for (size_t i = 0; i < n; ++i) {
        const int64_t t = (*pv.frames)[i].t_us;
        if (!ok[i] || t < searchFromUs || t > searchToUs) continue;
        const double m = localMedian(i);
        if (m > wSq) { wSq = m; tSq = t; }
    }
    const double w0 = std::max(wAddr, wSq);
    out.addressTurnRad = std::acos(std::clamp(wAddr / w0, 0.0, 1.0));

    out.t.resize(n);
    out.angleRad.assign(n, 0.0);
    out.sigmaRad.assign(n, 0.0);
    out.valid.assign(n, 0u);
    out.nearSquare.assign(n, 0u);
    for (size_t i = 0; i < n; ++i) {
        const int64_t t = (*pv.frames)[i].t_us;
        out.t[i] = t;
        if (!ok[i]) continue;
        const double r    = std::clamp(span[i] / w0, 0.0, 1.0);
        // NEAR SQUARE THE ESTIMATOR HAS NOTHING TO SAY, and it must not pretend otherwise: at
        // r → 1 the acos has infinite slope, so a pixel of span jitter across the reference width
        // becomes a step in the angle and a spike in its derivative — and a spike's curvature is
        // exactly what makes the timing σ look confident (design §12). Samples inside the floor
        // angle are therefore INVALID rather than clamped to zero; the curve shows the gap.
        if (r >= std::cos(std::asin(std::min(cfg.sinFloor, 1.0)))) { out.nearSquare[i] = 1u; continue; }
        const double turn = std::acos(r);
        out.angleRad[i] = (t < tSq) ? turn : -turn;
        out.sigmaRad[i] = cfg.spanNoisePx / (w0 * std::max(std::sin(turn), cfg.sinFloor));
        out.valid[i]    = 1u;
    }
    return out;
}

// The sighted band of a span track: samples whose |turn| is at least `sightedDeg`, where the
// span's slope carries the rate (sensitivity ∝ sin θ). Fills the gate's mask and the instants the
// segment entered and left the blind band inside [fromUs, toUs]. The mask governs the PEAK SEARCH
// only: the derivative keeps every admitted sample (blind ones carry their 1/sin θ σ), so the
// charted series and its σ are unchanged by the band.
void sightedBand(const AngleTrack &a, double sightedDeg, int64_t fromUs, int64_t toUs,
                 std::vector<uint8_t> &mask, PlacementGate &gate)
{
    const size_t n = a.t.size();
    const double lim = sightedDeg * kPi / 180.0;
    mask.assign(n, 0u);
    gate.blindFromUs = gate.blindToUs = -1;
    bool inBlind = false;
    for (size_t i = 0; i < n; ++i) {
        const bool admitted = !a.valid.empty() && a.valid[i];
        const bool sighted  = admitted && std::fabs(a.angleRad[i]) >= lim;
        mask[i] = sighted ? 1u : 0u;
        if (a.t[i] < fromUs || a.t[i] > toUs) continue;
        // Blind = admitted-but-near-square OR dropped inside the sin floor (the gap the track
        // shows). Unadmitted for confidence is neither and does not move the band.
        const bool blind = !sighted && (admitted || (i < a.nearSquare.size() && a.nearSquare[i]));
        if (blind && !inBlind && gate.blindFromUs < 0) { gate.blindFromUs = a.t[i]; inBlind = true; }
        else if (sighted && inBlind && gate.blindToUs < 0) { gate.blindToUs = a.t[i]; inBlind = false; }
    }
    gate.sighted = &mask;
}

// ── The paired face-on + down-the-line geometry (design §5.2; the offline measurement that
//    decided its shape is docs/research/data/kinematic_sequence/pair_span_turn_20260920.md) ─────
//
// THE OBSERVABLE IS THE SIGNED HORIZONTAL SEPARATION IN BOTH VIEWS, not a span distance. For a
// body line of length W, tilted τ out of horizontal and turned ψ about the vertical,
//
//     d_fo  = s_F · W · cos τ · cos ψ          (face-on, pixels)
//     d_dtl = s_D · W · cos τ · sin ψ          (down-the-line, pixels)
//
// so W and the TILT — which is large for the shoulders in the downswing and is exactly what a
// 2-D span distance absorbs — cancel in the ratio, and
//
//     ψ = atan2(d_dtl / r, d_fo),   r = s_D / s_F
//
// is continuous through square with no unfold, no reference width and no square-up inference.
// Its σ has no singularity either: the atan2 Jacobian is bounded everywhere, which is the whole
// reason this rung exists (the face-on acos has infinite slope at square — §12.4).
//
// ⚠ γ, THE ANGLE BETWEEN THE TWO VIEWS, IS NOT 90°. Measured at 75–84° on two rigs: the
// down-the-line camera sits behind the BALL, not behind the hands. A γ ≠ 90 biases ψ's LEVEL —
// d_dtl carries a cos γ share of the face-on component — and therefore the published °/s; it does
// NOT move the peak's time, which is what a sequence node is. Fitting γ per swing is possible
// (the offline rig does it) and is deliberately not done here: it would buy magnitude this route
// is not allowed to publish anyway (G4), at the cost of a fit that can fail. What IS done is the
// cheap sanity of `corr` below.
static double percentileOf(std::vector<double> v, double q)
{
    v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return !std::isfinite(x); }), v.end());
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double pos = std::clamp(q, 0.0, 1.0) * double(v.size() - 1);
    const size_t lo = size_t(std::floor(pos));
    const size_t hi = std::min(lo + 1, v.size() - 1);
    return v[lo] + (pos - double(lo)) * (v[hi] - v[lo]);
}

double pearson(const std::vector<double> &a, const std::vector<double> &b)
{
    const size_t n = std::min(a.size(), b.size());
    if (n < 3) return 0.0;
    double ma = 0.0, mb = 0.0;
    for (size_t i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
    ma /= double(n); mb /= double(n);
    double sab = 0.0, saa = 0.0, sbb = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double da = a[i] - ma, db = b[i] - mb;
        sab += da * db; saa += da * da; sbb += db * db;
    }
    return (saa > 0.0 && sbb > 0.0) ? sab / std::sqrt(saa * sbb) : 0.0;
}

// The PIXEL-SCALE RATIO between the views, with no calibration. Both cameras are level and both
// see VERTICAL undistorted by the turn, so the body's vertical extent at address — ankle mid-point
// to shoulder mid-point — is the same physical length imaged twice, and its ratio is r = s_D/s_F.
// Refused when either view is missing the address hold or images the golfer too small for the
// ratio to mean anything.
struct PairScale {
    bool    ok = false;
    double  r  = 1.0;
    double  extFo = 0.0, extDtl = 0.0;
    int     n = 0;
    QString refusal;
};

double addressVerticalExtent(const PoseView &pv, int64_t addressUs, const SegmentRatesConfig &cfg,
                             int &samplesOut)
{
    std::vector<double> ext;
    const size_t n = pv.frames->size();
    for (size_t i = 0; i < n; ++i) {
        const int64_t t = (*pv.frames)[i].t_us;
        if (addressUs < 0 || t < addressUs || t > addressUs + cfg.addrWindowUs) continue;
        if (pv.conf(i, kp::LeftAnkle) < cfg.confMin || pv.conf(i, kp::RightAnkle) < cfg.confMin) continue;
        if (pv.conf(i, kp::LeftShoulder) < cfg.confMin || pv.conf(i, kp::RightShoulder) < cfg.confMin) continue;
        const double ankleY = 0.5 * (pv.px(i, kp::LeftAnkle).y() + pv.px(i, kp::RightAnkle).y());
        const double shldrY = 0.5 * (pv.px(i, kp::LeftShoulder).y() + pv.px(i, kp::RightShoulder).y());
        ext.push_back(std::fabs(ankleY - shldrY));
    }
    samplesOut = int(ext.size());
    return ext.empty() ? 0.0 : medianOf(ext);
}

PairScale pairScale(const PoseView &fo, const PoseView &dtl, int64_t addressUs,
                    const SegmentRatesConfig &cfg)
{
    PairScale s;
    if (addressUs < 0) { s.refusal = QStringLiteral("no Address on the ladder"); return s; }
    int nF = 0, nD = 0;
    s.extFo  = addressVerticalExtent(fo,  addressUs, cfg, nF);
    s.extDtl = addressVerticalExtent(dtl, addressUs, cfg, nD);
    s.n = std::min(nF, nD);
    if (nF < cfg.addrMinFrames || nD < cfg.addrMinFrames) {
        s.refusal = QStringLiteral("address window holds %1 / %2 usable frames (need %3)")
                        .arg(nF).arg(nD).arg(cfg.addrMinFrames);
        return s;
    }
    if (s.extFo < cfg.pairMinExtentPx || s.extDtl < cfg.pairMinExtentPx) {
        s.refusal = QStringLiteral("vertical extent %1 / %2 px below the %3 px floor")
                        .arg(s.extFo, 0, 'f', 0).arg(s.extDtl, 0, 'f', 0)
                        .arg(cfg.pairMinExtentPx, 0, 'f', 0);
        return s;
    }
    s.r  = s.extDtl / s.extFo;
    s.ok = true;
    return s;
}

// One view's signed horizontal separation of a keypoint pair, with its σ, over every admitted
// frame. `lead`/`trail` come from handedness, so the sign mirrors with the golfer.
struct SepTrack {
    std::vector<int64_t> t;
    std::vector<double>  d;      // px, SIGNED: x(lead) − x(trail)
    std::vector<double>  sigma;  // px
    std::vector<uint8_t> ok;     // 0 ⇒ this instant carries no usable separation for the pair
    int valid() const { return int(std::count(ok.begin(), ok.end(), uint8_t(1))); }
};

SepTrack separations(const PoseView &pv, int lead, int trail, const SegmentRatesConfig &cfg)
{
    SepTrack s;
    const size_t n = pv.frames->size();
    for (size_t i = 0; i < n; ++i) {
        if (pv.conf(i, lead) < cfg.confMin || pv.conf(i, trail) < cfg.confMin) continue;
        s.t.push_back((*pv.frames)[i].t_us);
        s.d.push_back(pv.px(i, lead).x() - pv.px(i, trail).x());
        s.sigma.push_back(std::hypot(pv.sigmaPx(i, lead, cfg.kpSigmaPx),
                                     pv.sigmaPx(i, trail, cfg.kpSigmaPx)));
        s.ok.push_back(1u);
    }
    return s;
}

// ── THE RIGID-BODY RATE LIMIT ──────────────────────────────────────────────────────────────────
//
// A line of image half-width W turning about the vertical at no more than ω_max cannot change its
// horizontal separation by more than W·sin(ω_max·Δt) between two frames. That is arithmetic about
// a rigid body, not a smoothness preference, and with ω_max at 2000 °/s it is generous by a factor
// of nearly three on the thorax (Cheetham's professionals peak it at 727 ± 61). A step past that
// bound, plus two keypoint σ of slack, did not come from a golfer.
//
// It is what the face-on SHOULDERS need at their own square-up. There the true separation passes
// through zero, the keypoints are least certain, and the measured series reads −121, +12, −108,
// −26, +96 px in 27 ms — 200 px steps against a 92 px bound. atan2 is perfectly conditioned at
// x = 0; its x ARGUMENT is not, and this is where that is said out loud instead of being turned
// into 13 000 °/s. The offending sample and its two neighbours go, because a step implicates both
// of its ends and the local quadratic reaches one sample further.
int rateLimitInPlace(SepTrack &s, double wView, const SegmentRatesConfig &cfg)
{
    const size_t n = s.t.size();
    if (n < 2 || !(wView > 0.0)) return 0;
    const double wMaxRad = cfg.pairMaxTurnDps * kPi / 180.0;
    std::vector<uint8_t> kill(n, 0u);
    for (size_t i = 1; i < n; ++i) {
        const double dt = double(s.t[i] - s.t[i - 1]) * 1e-6;
        if (!(dt > 0.0)) continue;
        const double lim = wView * std::sin(std::min(wMaxRad * dt, kPi / 2.0)) + 2.0 * cfg.kpSigmaPx;
        if (std::fabs(s.d[i] - s.d[i - 1]) <= lim) continue;
        kill[i - 1] = kill[i] = 1u;
        if (i + 1 < n) kill[i + 1] = 1u;
    }
    int hit = 0;
    for (size_t i = 0; i < n; ++i)
        if (kill[i] && s.ok[i]) { s.ok[i] = 0u; ++hit; }
    return hit;
}

// ── THE LEFT/RIGHT RELABEL GUARD ───────────────────────────────────────────────────────────────
//
// A pose model labels left and right BY APPEARANCE. With the golfer's back toward the lens near
// the top it gets them the wrong way round, and the signed separation steps from one side of zero
// to the other with its MAGNITUDE INTACT — −108 px to +96 px in 7 ms on a real corpus swing. An
// unsigned span distance never sees it, which is why nothing before this route ever had to care;
// the paired angle sees a ~180° step and the 25 ms derivative calls it 2000 °/s.
//
// A GENUINE CROSSING LOOKS DIFFERENT, and the difference is physics rather than preference. For a
// line to cross square TO THIS CAMERA, cos ψ must pass through zero, so |d| has to COLLAPSE on the
// way through. At the fastest rate a trunk reaches — call it 1500 °/s — a rigid line turns ≤ 10°
// per frame at 150 fps, so it cannot get from one side of zero to the other while |d| stays near
// its maximum. So: a sign change WITH a collapse is kinematics and is left alone; a sign change
// WITHOUT one is a relabel and is undone. `pairSwapMinFrac` (0.35 of the view's own p95
// separation, i.e. 20° from square) is where the collapse is called.
//
// The walk is anchored at the ADDRESS WINDOW — the same window the orientation bit is read in,
// where the golfer is near square to the face-on camera and the labels are unambiguous — forward
// to the end and backward to the start, so the parity is always relative to a known-good stretch
// rather than to whichever end of the recording we happened to begin at.
//
// WHERE THE LABELS ALTERNATE FASTER THAN THE BODY CAN TURN, neither reading is trustworthy: three
// or more flips inside 100 ms is not a golfer, and those frames are dropped for this route rather
// than corrected. Saying "no data here" is the honest output; a parity guessed inside a flutter is
// the §12.1 trap by another name.
struct SwapReport { int flips = 0; int dropped = 0; };

SwapReport deswapInPlace(SepTrack &s, int64_t addrFromUs, int64_t addrToUs, double wView,
                         const SegmentRatesConfig &cfg)
{
    SwapReport rep;
    const size_t n = s.t.size();
    if (n < 3 || !(wView > 0.0)) return rep;

    std::vector<double> gaps;
    for (size_t i = 1; i < n; ++i) gaps.push_back(double(s.t[i] - s.t[i - 1]));
    const double stepUs = medianOf(gaps);
    if (!(stepUs > 0.0)) return rep;
    // Three frame intervals. Across a wider hole the two samples carry no continuity claim, so the
    // parity is CARRIED but never CHANGED there — a conservative choice that leaves a swap across a
    // sparse stretch uncorrected rather than inventing one.
    const int64_t maxGapUs = int64_t(3.0 * stepUs);
    const double  floorPx  = cfg.pairSwapMinFrac * wView;

    size_t a0 = n, a1 = 0;
    for (size_t i = 0; i < n; ++i)
        if (s.t[i] >= addrFromUs && s.t[i] <= addrToUs) { a0 = std::min(a0, i); a1 = std::max(a1, i); }
    if (a0 > a1) { a0 = a1 = 0; }

    std::vector<int64_t> flipT;
    const auto step = [&](double raw, double prev, int64_t dtUs) {
        return raw * prev < 0.0 && std::min(std::fabs(raw), std::fabs(prev)) >= floorPx
            && dtUs <= maxGapUs;
    };
    {   // forward from the last address-window sample
        double p = 1.0, prev = s.d[a1];
        int64_t prevT = s.t[a1];
        for (size_t i = a1 + 1; i < n; ++i) {
            if (step(s.d[i] * p, prev, s.t[i] - prevT)) { p = -p; ++rep.flips; flipT.push_back(s.t[i]); }
            s.d[i] *= p;
            prev = s.d[i]; prevT = s.t[i];
        }
    }
    {   // backward from the first address-window sample
        double p = 1.0, prev = s.d[a0];
        int64_t prevT = s.t[a0];
        for (size_t k = a0; k-- > 0; ) {
            if (step(s.d[k] * p, prev, prevT - s.t[k])) { p = -p; ++rep.flips; flipT.push_back(s.t[k]); }
            s.d[k] *= p;
            prev = s.d[k]; prevT = s.t[k];
        }
    }

    // Rapid alternation: ≥ 3 flips inside 100 ms ⇒ drop the frames the cluster spans.
    std::sort(flipT.begin(), flipT.end());
    std::vector<uint8_t> drop(n, 0u);
    for (size_t i = 0; i + 2 < flipT.size(); ++i) {
        if (flipT[i + 2] - flipT[i] > 100000) continue;
        const int64_t lo = flipT[i] - int64_t(stepUs), hi = flipT[i + 2];
        for (size_t j = 0; j < n; ++j) if (s.t[j] >= lo && s.t[j] <= hi) drop[j] = 1u;
    }
    for (size_t i = 0; i < n; ++i)
        if (drop[i] && s.ok[i]) { s.ok[i] = 0u; ++rep.dropped; }
    return rep;
}

// The median of a signed separation over a time window — the two ORIENTATION BITS the route needs.
//
// WHY THESE ARE MEASURED AND NOT DERIVED. Which image side a golfer's lead hip lands on, and which
// side of the target line the down-the-line camera stands, are properties of the RIG. `leadIsLeft`
// picks the two keypoints; it cannot know either of those. Both bits are read where the quantity
// concerned is at its largest and least ambiguous — the face-on separation over the address hold
// (the golfer is near square, |d_fo| is near its maximum), the down-the-line separation at the top
// (the body is 40–90° closed, |d_dtl| is near ITS maximum). That is the opposite of the flat-
// maximum square-up inference §12.4 condemned: nothing here depends on locating an extremum in
// time, only on the sign of a large number over a wide window.
double medianOver(const SepTrack &s, int64_t fromUs, int64_t toUs)
{
    std::vector<double> v;
    for (size_t i = 0; i < s.t.size(); ++i)
        if (s.ok[i] && s.t[i] >= fromUs && s.t[i] <= toUs) v.push_back(s.d[i]);
    return v.empty() ? 0.0 : medianOf(v);
}

struct PairInputs {
    int64_t addressUs = -1, takeawayUs = -1, topUs = -1, impactUs = -1;
    double  r = 1.0;
    int     signFo = 0, signDtl = 0;   // filled by the first segment, reused by the second
};

// Both tiers of both views. THE DETECTOR RUNS ON RAW, THE PRODUCER CONSUMES SMOOTHED WHERE IT CAN.
// A relabel is a step and the RTS smoother has already turned it into a ramp, so it has to be
// looked for on the raw keypoints. But where a view/segment carries NO relabel and no flutter —
// the hips in both views on 21 of 21 corpus swings, the down-the-line shoulders on 21 of 21 —
// there is nothing to protect against and the smoothed track is simply the better measurement.
// Only a view/segment that actually relabels pays the noise cost of being read raw.
struct PairViews {
    PoseView foRaw, foSm, dtlRaw, dtlSm;
    bool     foHasSm = false, dtlHasSm = false;
};

// The paired turn of one body line. Returns an empty track (with `diag.refusal` set) when the pair
// cannot be formed; the caller then falls through to the face-on span rung.
AngleTrack pairTurnTrack(const PairViews &vw, int lead, int trail,
                         const PairInputs &pin, const Domain &dom, const SegmentRatesConfig &cfg,
                         PairSegmentDiag &diag)
{
    AngleTrack out;
    SepTrack sfRaw = separations(vw.foRaw,  lead, trail, cfg);
    SepTrack sdRaw = separations(vw.dtlRaw, lead, trail, cfg);
    if (sfRaw.t.size() < 3 || sdRaw.t.size() < 3) {
        diag.refusal = QStringLiteral("too few admitted frames (%1 face-on, %2 down-the-line)")
                           .arg(sfRaw.t.size()).arg(sdRaw.t.size());
        return out;
    }

    // Each view's own scale, from its own p95 separation over takeaway→impact. Magnitudes are what
    // a relabel leaves alone, so this is safe to read before undoing one.
    const int64_t tkUs = pin.takeawayUs >= 0 ? pin.takeawayUs : pin.addressUs;
    const auto p95Of = [&](const SepTrack &s) {
        std::vector<double> v;
        for (size_t i = 0; i < s.t.size(); ++i)
            if (s.t[i] >= tkUs && s.t[i] <= pin.impactUs) v.push_back(std::fabs(s.d[i]));
        if (v.size() < 3) { v.clear(); for (double x : s.d) v.push_back(std::fabs(x)); }
        return percentileOf(v, 0.95);
    };
    const double wFo = p95Of(sfRaw), wDtl = p95Of(sdRaw);
    const SwapReport rf = deswapInPlace(sfRaw, pin.addressUs, pin.addressUs + cfg.addrWindowUs, wFo, cfg);
    const SwapReport rd = deswapInPlace(sdRaw, pin.addressUs, pin.addressUs + cfg.addrWindowUs, wDtl, cfg);
    diag.nSwapsFo = rf.flips;
    diag.nSwapsDtl = rd.flips;
    diag.nSwapFramesDropped = rf.dropped + rd.dropped;

    // Source per view per segment: smoothed where nothing had to be undone, the de-swapped raw
    // series where something did.
    const bool foClean  = rf.flips == 0 && rf.dropped == 0 && vw.foHasSm;
    const bool dtlClean = rd.flips == 0 && rd.dropped == 0 && vw.dtlHasSm;
    SepTrack sf = foClean  ? separations(vw.foSm,  lead, trail, cfg) : std::move(sfRaw);
    SepTrack sd = dtlClean ? separations(vw.dtlSm, lead, trail, cfg) : std::move(sdRaw);
    diag.srcFo  = foClean  ? QStringLiteral("smoothed") : QStringLiteral("rawDeswapped");
    diag.srcDtl = dtlClean ? QStringLiteral("smoothed") : QStringLiteral("rawDeswapped");

    // The rigid-body rate limit, on whichever series is actually consumed.
    diag.nRateLimitedFo  = rateLimitInPlace(sf, wFo,  cfg);
    diag.nRateLimitedDtl = rateLimitInPlace(sd, wDtl, cfg);

    if (sf.valid() < 3 || sd.valid() < 3) {
        diag.refusal = QStringLiteral("only %1 / %2 usable separations left after the guards")
                           .arg(sf.valid()).arg(sd.valid());
        return AngleTrack{};
    }

    // The orientation bits (see medianOver). Face-on over the address hold; down-the-line over the
    // 200 ms ending just after the Top, which is where the body is most closed.
    const double mF = medianOver(sf, pin.addressUs, pin.addressUs + cfg.addrWindowUs);
    const double mD = medianOver(sd, pin.topUs - 150000, pin.topUs + 50000);
    if (!(std::fabs(mF) > 0.0) || !(std::fabs(mD) > 0.0)) {
        diag.refusal = QStringLiteral("no orientation: |d_fo| at address %1 px, |d_dtl| at the top %2 px")
                           .arg(std::fabs(mF), 0, 'f', 1).arg(std::fabs(mD), 0, 'f', 1);
        return out;
    }
    const double sgnF = mF > 0.0 ? 1.0 : -1.0;     // ⇒ d_fo positive at square
    const double sgnD = mD > 0.0 ? 1.0 : -1.0;     // ⇒ ψ positive when CLOSED, as the span rung's θ is
    diag.signFo  = int(sgnF);
    diag.signDtl = int(sgnD);
    diag.refusal.clear();

    // The two streams share the window clock but not their phase or frame count: resample the
    // down-the-line separation onto the face-on sample instants by linear interpolation, and call
    // a face-on sample unpaired when its bracket is wider than pairMaxGapFrames DTL intervals.
    std::vector<double> gaps;
    for (size_t i = 1; i < sd.t.size(); ++i) gaps.push_back(double(sd.t[i] - sd.t[i - 1]));
    const double dtlStepUs = gaps.empty() ? 0.0 : medianOf(gaps);
    const double maxBracketUs = dtlStepUs > 0.0 ? cfg.pairMaxGapFrames * dtlStepUs : 0.0;
    if (!(maxBracketUs > 0.0)) {
        diag.refusal = QStringLiteral("the down-the-line stream carries no usable frame interval");
        return out;
    }
    // Interpolate over the down-the-line samples the guards LEFT: a masked frame is a hole in that
    // leg too, and the bracket rule then refuses to reach across it.
    std::vector<int64_t> tD;
    std::vector<double>  vD, gD;
    for (size_t i = 0; i < sd.t.size(); ++i)
        if (sd.ok[i]) { tD.push_back(sd.t[i]); vD.push_back(sd.d[i]); gD.push_back(sd.sigma[i]); }
    if (tD.size() < 3) {
        diag.refusal = QStringLiteral("only %1 usable down-the-line separations").arg(tD.size());
        return AngleTrack{};
    }

    const size_t n = sf.t.size();
    out.t = sf.t;
    out.angleRad.assign(n, 0.0);
    out.sigmaRad.assign(n, 0.0);
    out.valid.assign(n, 0u);
    std::vector<double> dFsigned(n, std::nan("")), dDsigned(n, std::nan(""));

    size_t k = 0;
    for (size_t i = 0; i < n; ++i) {
        const int64_t t = sf.t[i];
        if (!sf.ok[i]) continue;          // a face-on hole is a pair hole: both legs feed one validity
        if (t < tD.front() || t > tD.back()) continue;
        // tD is ascending and k only ever advances, so after this tD[k] ≤ t ≤ tD[k+1] (or k is the
        // last index and t == tD.back()).
        while (k + 1 < tD.size() && tD[k + 1] < t) ++k;
        double dD = 0.0, sD = 0.0;
        if (tD[k] == t) { dD = vD[k]; sD = gD[k]; }
        else if (k + 1 < tD.size()) {
            if (double(tD[k + 1] - tD[k]) > maxBracketUs) continue;
            const double f = double(t - tD[k]) / double(tD[k + 1] - tD[k]);
            dD = vD[k] + f * (vD[k + 1] - vD[k]);
            sD = gD[k] + f * (gD[k + 1] - gD[k]);
        } else {
            continue;
        }
        const double x  = sgnF * sf.d[i];
        const double y  = sgnD * dD / pin.r;
        const double sy = sD / pin.r, sx = sf.sigma[i];
        const double den = x * x + y * y;
        if (!(den > 0.0)) continue;
        out.angleRad[i] = std::atan2(y, x);
        out.sigmaRad[i] = std::sqrt((x * x * sy * sy + y * y * sx * sx)) / den;
        out.valid[i]    = 1u;
        dFsigned[i] = x;
        dDsigned[i] = sgnD * dD;
    }

    // Unwrap over the admitted samples only (invalid ones hold 0 and would wrap against them).
    {
        std::vector<double> adm;
        for (size_t i = 0; i < n; ++i) if (out.valid[i]) adm.push_back(out.angleRad[i]);
        if (adm.size() < 3) {
            diag.refusal = QStringLiteral("only %1 paired samples").arg(adm.size());
            return AngleTrack{};
        }
        unwrapInPlace(adm);
        size_t q = 0;
        for (size_t i = 0; i < n; ++i) if (out.valid[i]) out.angleRad[i] = adm[q++];
    }

    // ── The diagnostics, and the ONE gate (design decision D3) ─────────────────────────────────
    // W is face-on's own p95 separation over takeaway→impact; the down-the-line leg should track
    // the out-of-plane component that implies, sqrt(W² − d_fo²). This is a consistency check on
    // the PAIRING, not on the geometry — the measured figures are 0.80 (hips) and 0.89 (shoulders)
    // — and it is taken on |d_dtl| rather than the signed value on purpose: a golfer who squares
    // up inside the domain takes the signed separation through zero and out the other side while
    // sqrt(·) stays non-negative, and correlating those two would refuse exactly the swings this
    // route was built for. The signed figure is reported beside it.
    std::vector<double> absF, absD;
    for (size_t i = 0; i < n; ++i) {
        if (!out.valid[i] || sf.t[i] < tkUs || sf.t[i] > pin.impactUs) continue;
        absF.push_back(std::fabs(dFsigned[i]));
        absD.push_back(std::fabs(dDsigned[i]));
    }
    const double W = percentileOf(absF, 0.95);
    diag.rEllipse = percentileOf(absF, 0.90) > 0.0
                        ? percentileOf(absD, 0.90) / percentileOf(absF, 0.90) : 0.0;

    std::vector<double> yAbs, ySgn, pred, closure;
    for (size_t i = 0; i < n; ++i) {
        if (!out.valid[i] || sf.t[i] < dom.fromUs || sf.t[i] > dom.toUs) continue;
        yAbs.push_back(std::fabs(dDsigned[i]) / pin.r);
        ySgn.push_back(dDsigned[i] / pin.r);
        pred.push_back(std::sqrt(std::max(0.0, W * W - dFsigned[i] * dFsigned[i])));
        if (W > 0.0) {
            const double a = dFsigned[i] / W, b = dDsigned[i] / (pin.r * W);
            closure.push_back(std::fabs(a * a + b * b - 1.0));
        }
    }
    diag.nPaired    = int(yAbs.size());
    {
        int inDom = 0, valid = 0;
        for (size_t i = 0; i < n; ++i) {
            if (sf.t[i] < dom.fromUs || sf.t[i] > dom.toUs) continue;
            ++inDom;
            if (out.valid[i]) ++valid;
        }
        diag.invalidFrac = inDom > 0 ? double(inDom - valid) / double(inDom) : 0.0;
    }
    diag.corrAbs    = std::fabs(pearson(yAbs, pred));
    diag.corrSigned = pearson(ySgn, pred);
    diag.closureP50 = percentileOf(closure, 0.50);
    diag.closureP90 = percentileOf(closure, 0.90);

    if (diag.nPaired < cfg.addrMinFrames) {
        diag.refusal = QStringLiteral("only %1 paired samples in the domain").arg(diag.nPaired);
        return AngleTrack{};
    }
    if (diag.corrAbs < cfg.pairMinCorr) {
        diag.refusal = QStringLiteral("|corr| %1 below the %2 floor")
                           .arg(diag.corrAbs, 0, 'f', 2).arg(cfg.pairMinCorr, 0, 'f', 2);
        return AngleTrack{};
    }
    diag.produced = true;
    return out;
}

// A RUN OF INVALID SAMPLES IS A BLIND BAND. The span rung goes blind because the geometry stops
// carrying the rate; the pair goes blind because the guards threw the samples away. The reader's
// question is the same either way — "could the route see the peak?" — so it goes through the same
// machinery. Leading invalid samples at the domain start are NOT a band: nothing was lost there,
// the domain simply had not begun, and treating them as one would bound every swing vacuously.
void validityBand(const AngleTrack &a, int64_t fromUs, int64_t toUs,
                  std::vector<uint8_t> &mask, PlacementGate &gate)
{
    const size_t n = a.t.size();
    mask.assign(n, 0u);
    for (size_t i = 0; i < n; ++i) mask[i] = (!a.valid.empty() && a.valid[i]) ? 1u : 0u;
    gate.blindFromUs = gate.blindToUs = -1;
    bool started = false, inBlind = false;
    for (size_t i = 0; i < n; ++i) {
        if (a.t[i] < fromUs || a.t[i] > toUs) continue;
        if (!started) { if (!mask[i]) continue; started = true; }
        if (!mask[i] && !inBlind && gate.blindFromUs < 0) { gate.blindFromUs = a.t[i]; inBlind = true; }
        else if (mask[i] && inBlind && gate.blindToUs < 0) { gate.blindToUs = a.t[i]; inBlind = false; }
    }
    gate.sighted = &mask;
}

struct PlaneParams {
    bool   have = false;
    double k    = 1.0;     // minor/major
    double nu   = 0.0;     // rad, major-axis bearing in the image atan2 convention
};

PlaneParams planeFrom(const ShaftTrack2D *shaft, const SegmentRatesConfig &cfg)
{
    PlaneParams p;
    if (!shaft || !shaft->plane.valid) return p;
    const ShaftPlaneChannel &c = (shaft->plane.channel == 1) ? shaft->plane.synth
                                                              : shaft->plane.measured;
    if (!c.fitted || !(c.ratioDown >= cfg.planeRatioFloor) || !(c.ratioDown <= 1.0)) return p;
    p.have = true;
    p.k    = c.ratioDown;
    p.nu   = c.nodeDownDeg * kPi / 180.0;
    return p;
}

// Apply the de-projection to an image-angle track in place, propagating σ through the gain.
void deprojectTrack(AngleTrack &a, const PlaneParams &pl, const SegmentRatesConfig &cfg)
{
    if (!pl.have) { a.extraRelSigma = cfg.noPlaneRelSigma; return; }
    for (size_t i = 0; i < a.angleRad.size(); ++i) {
        if (!a.valid.empty() && !a.valid[i]) continue;
        const double g = deprojectGain(a.angleRad[i], pl.nu, pl.k);
        a.angleRad[i] = deprojectPlaneAngle(a.angleRad[i], pl.nu, pl.k);
        a.sigmaRad[i] *= g;
    }
    unwrapInPlace(a.angleRad);
}

} // namespace

// ── Exposed geometry ───────────────────────────────────────────────────────────────────────────

double deprojectPlaneAngle(double psiRad, double nuRad, double k)
{
    const double phi = psiRad - nuRad;
    // tan α = tan φ / k, continuous through ±90° via atan2 on (sin φ, k cos φ), then carried into
    // φ's own sheet so an unwrapped ψ stays unwrapped.
    const double alpha0 = std::atan2(std::sin(phi), k * std::cos(phi));
    const double sheet  = std::round((phi - alpha0) / (2.0 * kPi)) * 2.0 * kPi;
    return alpha0 + sheet;
}

double deprojectGain(double psiRad, double nuRad, double k)
{
    // dα/dψ for α = atan2(sin φ, k cos φ): k / (sin²φ + k² cos²φ). Until 2026-09-21 the two terms
    // were the other way round — k / (cos²φ + k² sin²φ) — which is exact only where they are equal
    // and wrong by k² at the axes: along the node line the image angle moves SLOWER than the plane
    // angle (gain 1/k) and this returned k. It only ever scaled σ, never an angle, so no rate or
    // peak time moved; the σ that gates a node's placement did.
    const double phi = psiRad - nuRad;
    const double c = std::cos(phi), s = std::sin(phi);
    const double den = s * s + k * k * c * c;
    return den > 1e-12 ? k / den : 1.0;
}

// ── The producer ───────────────────────────────────────────────────────────────────────────────

SegmentRatesResult buildSegmentRates(const SegmentRatesInputs &in, const SegmentRatesConfig &cfg)
{
    SegmentRatesResult res;
    if (!cfg.enabled || !in.phases) return res;
    const std::vector<PhaseEvent> &phases = *in.phases;

    const std::optional<int64_t> impactP = phaseTimeOpt(phases, Phase::Impact);
    const int64_t impactUs = impactP ? *impactP : in.impactUs;
    if (impactUs < 0) return res;

    // The downswing domain: Transition when the ladder found one, else the Top. Nothing without
    // either — a sequence is defined on the downswing and a domain guessed from the recording's
    // start would place backswing peaks as nodes.
    Domain dom;
    if (const std::optional<int64_t> tr = phaseTimeOpt(phases, Phase::Transition)) dom.fromUs = *tr;
    else if (const std::optional<int64_t> tp = phaseTimeOpt(phases, Phase::Top))   dom.fromUs = *tp;
    dom.toUs = impactUs;
    if (!dom.ok()) return res;

    // The club's domain ends at the P7 KNOT of the track where there is one (the synth rate steps
    // there — kinematic_series.cpp's maskAfter), else at impact.
    Domain clubDom = dom;
    if (in.shaft && in.shaft->valid)
        for (const ShaftPosition &p : in.shaft->positions)
            if (p.p == 7) { clubDom.toUs = p.t_us; break; }
    if (!clubDom.ok()) clubDom = dom;

    const int64_t windowUs = int64_t(std::max(cfg.derivWindowMs, 1.0) * 1000.0);
    const double  sign     = in.leadIsLeft ? 1.0 : -1.0;   // opening = +ω_z for a right-hander
    std::vector<KsNode> nodes;

    // ── IMU routes, per segment ────────────────────────────────────────────────────────────────
    const SegmentStream *pelvisImu = nullptr, *thoraxImu = nullptr, *armImu = nullptr, *clubImu = nullptr;
    if (in.streams && !in.streams->timeGrid.empty()) {
        pelvisImu = in.streams->streamFor(SegmentRole::Pelvis);
        thoraxImu = in.streams->streamFor(SegmentRole::Thorax);
        armImu    = in.streams->streamFor(SegmentRole::LeadUpperArm);
        if (!armImu) armImu = in.streams->streamFor(SegmentRole::LeadForearm);
        clubImu   = in.streams->streamFor(SegmentRole::Club);
    }
    const auto streamUsable = [&](const SegmentStream *s) {
        return s && in.streams && s->qAnat.size() == in.streams->timeGrid.size() && s->qAnat.size() >= 2;
    };

    // Axial (vertical-axis) rate: the world gyro's z, signed so opening is positive. When the
    // stream carries no gyro (older documents), the bearing of the medio-lateral axis is
    // differentiated instead — the same construction body_rotation.cpp's IMU tier uses.
    const auto axialFromImu = [&](const SegmentStream &s) -> RateTrack {
        const std::vector<int64_t> &grid = in.streams->timeGrid;
        if (s.gyroDps.size() == s.qAnat.size()) {
            std::vector<double> dps(grid.size());
            for (size_t i = 0; i < grid.size(); ++i) {
                const QVector3D w = s.qAnat[i].rotatedVector(s.gyroDps[i]);
                dps[i] = sign * double(w.z());
            }
            return smoothRate(grid, dps, cfg.gyroNoiseDps, windowUs);
        }
        AngleTrack a;
        a.t = grid;
        a.angleRad.resize(grid.size());
        a.sigmaRad.assign(grid.size(), 0.0);   // uncharacterised: this path carries no angle σ
        for (size_t i = 0; i < grid.size(); ++i) {
            const QVector3D ml = s.qAnat[i].rotatedVector(QVector3D(1.f, 0.f, 0.f));
            a.angleRad[i] = std::atan2(double(ml.y()), double(ml.x()));
        }
        unwrapInPlace(a.angleRad);
        return differentiate(a, windowUs, sign, /*magnitude*/ false);
    };

    // Swing rate of a long axis (e_y in the anatomical frame): |ω − (ω·â)â| in world.
    const auto swingFromImu = [&](const SegmentStream &s) -> RateTrack {
        const std::vector<int64_t> &grid = in.streams->timeGrid;
        std::vector<double> dps(grid.size(), 0.0);
        if (s.gyroDps.size() == s.qAnat.size()) {
            for (size_t i = 0; i < grid.size(); ++i) {
                const QVector3D w    = s.qAnat[i].rotatedVector(s.gyroDps[i]);
                const QVector3D axis = s.qAnat[i].rotatedVector(QVector3D(0.f, 1.f, 0.f));
                const QVector3D perp = w - QVector3D::dotProduct(w, axis) * axis;
                dps[i] = double(perp.length());
            }
            return smoothRate(grid, dps, cfg.gyroNoiseDps, windowUs);
        }
        // No gyro: the long axis's direction rate from the orientation stream.
        std::vector<QVector3D> axis(grid.size());
        for (size_t i = 0; i < grid.size(); ++i)
            axis[i] = s.qAnat[i].rotatedVector(QVector3D(0.f, 1.f, 0.f));
        for (size_t i = 0; i < grid.size(); ++i) {
            const size_t a0 = i > 0 ? i - 1 : 0, b0 = std::min(i + 1, grid.size() - 1);
            const double dt = double(grid[b0] - grid[a0]) * 1e-6;
            if (dt <= 0.0) continue;
            const double d = std::clamp(double(QVector3D::dotProduct(axis[a0], axis[b0])), -1.0, 1.0);
            dps[i] = std::acos(d) / dt * kRadToDeg;
        }
        return smoothRate(grid, dps, cfg.gyroNoiseDps, windowUs);
    };

    if (streamUsable(pelvisImu))
        finishChannel(res.pelvis, axialFromImu(*pelvisImu), SeqSegment::Pelvis,
                      QStringLiteral("Pelvis angular speed"), QStringLiteral("pelvisImu"), true,
                      dom, phases, cfg, nodes);
    if (streamUsable(thoraxImu))
        finishChannel(res.thorax, axialFromImu(*thoraxImu), SeqSegment::Thorax,
                      QStringLiteral("Thorax angular speed"), QStringLiteral("thoraxImu"), true,
                      dom, phases, cfg, nodes);
    if (streamUsable(armImu))
        finishChannel(res.leadArm, swingFromImu(*armImu), SeqSegment::LeadArm,
                      QStringLiteral("Lead arm angular speed"), QStringLiteral("leadArmImus"), true,
                      dom, phases, cfg, nodes);
    if (streamUsable(clubImu))
        finishChannel(res.club, swingFromImu(*clubImu), SeqSegment::Club,
                      QStringLiteral("Club angular speed"), QStringLiteral("clubSensorFused"), true,
                      clubDom, phases, cfg, nodes);

    // ── Face-on routes, for whatever the IMUs did not cover ────────────────────────────────────
    const bool havePose = in.pose && in.frameW > 0 && in.frameH > 0
                       && (in.pose->smoothed.size() >= 2 || in.pose->frames.size() >= 2);
    const PlaneParams plane = planeFrom(in.shaft, cfg);

    if (havePose) {
        PoseView pv;
        const bool useSmoothed = in.pose->smoothed.size() >= 2;
        pv.frames = useSmoothed ? &in.pose->smoothed : &in.pose->frames;
        pv.aux    = (useSmoothed && in.pose->smoothedAux.size() == in.pose->smoothed.size())
                        ? &in.pose->smoothedAux : nullptr;
        pv.W = in.frameW;
        pv.H = in.frameH;

        const int64_t addressUs = phaseTime(phases, Phase::Address, -1);
        const int64_t topUs     = phaseTime(phases, Phase::Top, dom.fromUs);
        const int64_t finishUs  = phaseTime(phases, Phase::Finish, pv.frames->back().t_us);

        // ── The PAIR route, between the IMU rung and the face-on span rung ─────────────────────
        // Pelvis and thorax only. It needs both poses, and it needs them on one clock; nothing
        // else about the swing changes when they are absent.
        const bool havePair = cfg.pairTrunkEnabled && in.poseDtl
                           && in.dtlFrameW > 0 && in.dtlFrameH > 0
                           && in.pose->frames.size() >= 2 && in.poseDtl->frames.size() >= 2
                           && (!res.pelvis.produced() || !res.thorax.produced());
        if (havePair) {
            res.pair.attempted = true;
            // ⚠ THE PAIR READS THE RAW KEYPOINTS, not the smoothed companion track the span rung
            // prefers. The relabel guard keys on a STEP — a sign change with the magnitude intact —
            // and the RTS smoother has already smeared that step into a fast ramp, which is both
            // undetectable and, at 200 px over three frames, exactly the artefact it was meant to
            // remove. Detect it where it is still a step.
            //
            // AND NOTHING IS RE-SMOOTHED AFTERWARDS, deliberately. angular_rate.h's local quadratic
            // over a window fixed in TIME is this producer's ONE smoothing step — the thing that
            // makes an IMU node and a camera node comparable to the millisecond (design §6). A
            // second filter here would make the pair the only route smoothed twice and its σ_t the
            // only one that does not mean what the others' mean. The cost is that the raw track
            // carries no posterior σ, so the per-keypoint σ falls back to cfg.kpSigmaPx — 3 px,
            // which is what the corpus measured the smoother's posterior to be anyway (§12.4).
            PairViews vw;
            vw.foRaw.frames = &in.pose->frames;
            vw.foRaw.W = in.frameW; vw.foRaw.H = in.frameH;
            vw.dtlRaw.frames = &in.poseDtl->frames;
            vw.dtlRaw.W = in.dtlFrameW; vw.dtlRaw.H = in.dtlFrameH;
            vw.foHasSm = in.pose->smoothed.size() >= 2;
            if (vw.foHasSm) {
                vw.foSm.frames = &in.pose->smoothed;
                vw.foSm.aux    = (in.pose->smoothedAux.size() == in.pose->smoothed.size())
                                     ? &in.pose->smoothedAux : nullptr;
                vw.foSm.W = in.frameW; vw.foSm.H = in.frameH;
            }
            vw.dtlHasSm = in.poseDtl->smoothed.size() >= 2;
            if (vw.dtlHasSm) {
                vw.dtlSm.frames = &in.poseDtl->smoothed;
                vw.dtlSm.aux    = (in.poseDtl->smoothedAux.size() == in.poseDtl->smoothed.size())
                                      ? &in.poseDtl->smoothedAux : nullptr;
                vw.dtlSm.W = in.dtlFrameW; vw.dtlSm.H = in.dtlFrameH;
            }

            const PairScale ps = pairScale(vw.foRaw, vw.dtlRaw, addressUs, cfg);
            res.pair.rVertical   = ps.r;
            res.pair.extentFoPx  = ps.extFo;
            res.pair.extentDtlPx = ps.extDtl;
            res.pair.addrSamples = ps.n;
            res.pair.refusal     = ps.refusal;
            if (ps.ok) {
                PairInputs pin;
                pin.addressUs  = addressUs;
                pin.takeawayUs = phaseTime(phases, Phase::Takeaway, addressUs);
                pin.topUs      = topUs;
                pin.impactUs   = impactUs;
                pin.r          = ps.r;
                const auto trunkFromPair = [&](SegmentRateChannel &ch, SeqSegment seg, int a, int b,
                                               const QString &label, PairSegmentDiag &diag) {
                    const int lead  = in.leadIsLeft ? a : b;
                    const int trail = in.leadIsLeft ? b : a;
                    AngleTrack at = pairTurnTrack(vw, lead, trail, pin, dom, cfg, diag);
                    if (at.t.empty() || !diag.produced) return;
                    // The geometry has no singularity — that is the point of the rung — so the only
                    // thing the route cannot see is what the guards took away. Those holes go
                    // through the span rung's own blind-band machinery, read as HOLES rather than
                    // as a horizon (PlacementGate::bandIsHole). Plus the reversal-spike guard and
                    // the end-edge bound.
                    std::vector<uint8_t> seen;
                    PlacementGate gate;
                    // The thorax has its own switch: the two segments fail differently on this
                    // route (the hips never relabel, the shoulders do), so one gate for both would
                    // close the one that works to silence the one that does not. Both switches gate
                    // the RING only — every bound below is emitted either way.
                    gate.may          = cfg.pairTrunkPlacement
                                     && (seg != SeqSegment::Thorax || cfg.pairTrunkThoraxPlacement);
                    gate.spikeGuard   = true;
                    gate.endEdgeBound = true;
                    gate.bandIsHole   = true;
                    validityBand(at, dom.fromUs, dom.toUs, seen, gate);
                    const size_t before = nodes.size();
                    finishChannel(ch, differentiate(at, windowUs, /*opening = −dψ/dt*/ -1.0, false),
                                  seg, label, QStringLiteral("faceOn+dtl"), false, dom, phases, cfg,
                                  nodes, gate);
                    // NEITHER A NODE NOR A BOUND IS WORSE THAN TODAY. If the pair could not place
                    // this segment AND could not bound it, it has told the reader less than the
                    // face-on span rung would have. Withdraw it — channel and node — and let the
                    // span rung run for this segment on this swing.
                    if (nodes.size() > before) {
                        const KsNode &n = nodes.back();
                        if (!n.placed && !n.bounded()) {
                            nodes.pop_back();
                            ch = SegmentRateChannel{};
                            diag.refusal = QStringLiteral("neither a placement nor a bound — "
                                                          "fell through to the face-on span");
                            diag.produced = false;
                        }
                    }
                };
                if (!res.pelvis.produced())
                    trunkFromPair(res.pelvis, SeqSegment::Pelvis, kp::LeftHip, kp::RightHip,
                                  QStringLiteral("Pelvis angular speed"), res.pair.pelvis);
                if (!res.thorax.produced())
                    trunkFromPair(res.thorax, SeqSegment::Thorax, kp::LeftShoulder, kp::RightShoulder,
                                  QStringLiteral("Thorax angular speed"), res.pair.thorax);
            }
        }

        // The trunk from its spans. The node is claimed only where the camera can see the rate —
        // the sighted band, |turn| ≥ sightedTurnDeg — and bounded where it could not (§12.4).
        const auto trunkFromSpan = [&](SegmentRateChannel &ch, SeqSegment seg, int a, int b, const QString &label) {
            AngleTrack at = spanTurnTrack(pv, a, b, addressUs, topUs, finishUs, cfg);
            if (at.t.empty()) return;
            std::vector<uint8_t> sighted;
            PlacementGate gate;
            gate.may = cfg.faceOnTrunkPlacement;
            sightedBand(at, cfg.sightedTurnDeg, dom.fromUs, dom.toUs, sighted, gate);
            finishChannel(ch, differentiate(at, windowUs, /*opening = −dθ/dt*/ -1.0, false), seg, label,
                          QStringLiteral("faceOn"), false, dom, phases, cfg, nodes, gate);
        };
        if (!res.pelvis.produced())
            trunkFromSpan(res.pelvis, SeqSegment::Pelvis, kp::LeftHip, kp::RightHip,
                          QStringLiteral("Pelvis angular speed"));
        if (!res.thorax.produced())
            trunkFromSpan(res.thorax, SeqSegment::Thorax, kp::LeftShoulder, kp::RightShoulder,
                          QStringLiteral("Thorax angular speed"));
        if (!res.leadArm.produced()) {
            const int sh = in.leadIsLeft ? kp::LeftShoulder : kp::RightShoulder;
            const int wr = in.leadIsLeft ? kp::LeftWrist    : kp::RightWrist;
            const size_t n = pv.frames->size();
            AngleTrack a;
            a.t.resize(n); a.angleRad.assign(n, 0.0); a.sigmaRad.assign(n, 0.0); a.valid.assign(n, 0u);
            for (size_t i = 0; i < n; ++i) {
                a.t[i] = (*pv.frames)[i].t_us;
                if (pv.conf(i, sh) < cfg.confMin || pv.conf(i, wr) < cfg.confMin) continue;
                const QPointF d = pv.px(i, wr) - pv.px(i, sh);
                const double L  = std::hypot(d.x(), d.y());
                if (!(L > 1.0)) continue;
                a.angleRad[i] = std::atan2(d.y(), d.x());
                const double ss = pv.sigmaPx(i, sh, cfg.kpSigmaPx), sw = pv.sigmaPx(i, wr, cfg.kpSigmaPx);
                a.sigmaRad[i] = std::hypot(ss, sw) / L;
                a.valid[i]    = 1u;
            }
            // Invalid samples hold 0, which would wrap against their neighbours: unwrap over the
            // admitted ones only.
            {
                std::vector<double> adm;
                for (size_t i = 0; i < n; ++i) if (a.valid[i]) adm.push_back(a.angleRad[i]);
                unwrapInPlace(adm);
                size_t j = 0;
                for (size_t i = 0; i < n; ++i) if (a.valid[i]) a.angleRad[i] = adm[j++];
            }
            deprojectTrack(a, plane, cfg);
            finishChannel(res.leadArm, differentiate(a, windowUs, 1.0, /*magnitude*/ true),
                          SeqSegment::LeadArm, QStringLiteral("Lead arm angular speed"),
                          QStringLiteral("faceOn"), false, dom, phases, cfg, nodes);
        }
    }

    if (!res.club.produced() && in.shaft && in.shaft->valid) {
        const std::vector<ShaftSample2D> &track =
            (in.shaft->synth.size() >= 2) ? in.shaft->synth : in.shaft->samples;
        if (track.size() >= 2) {
            const size_t n = track.size();
            AngleTrack a;
            a.t.resize(n); a.angleRad.resize(n); a.sigmaRad.resize(n); a.valid.assign(n, 1u);
            for (size_t i = 0; i < n; ++i) {
                a.t[i]        = track[i].t_us;
                a.angleRad[i] = track[i].thetaRad;
                a.sigmaRad[i] = cfg.shaftThetaSigmaRad / std::max(double(track[i].conf), 0.2);
            }
            unwrapInPlace(a.angleRad);
            // The fused two-camera plane where the fusion offered one (kinematic_sequence_design.md
            // §14): on 07-04 the ellipse's node bearing wandered −42…+45° swing to swing and its
            // ratio 0.81–0.98, where the fused plane held 0.86–0.91 and a level node.
            PlaneParams clubPlane = plane;
            const bool fused = in.fusedClubPlane.have
                            && in.fusedClubPlane.ratio >= cfg.planeRatioFloor && in.fusedClubPlane.ratio <= 1.0;
            if (fused) {
                clubPlane.have = true;
                clubPlane.k    = in.fusedClubPlane.ratio;
                clubPlane.nu   = in.fusedClubPlane.nodeRad;
            }
            deprojectTrack(a, clubPlane, cfg);
            // A track whose own clubhead speed at impact is not credible is a broken track, and
            // the synth tier's shaft angle on one is a straight line between anchors. The curve is
            // still produced (it is what the track says); the node is not claimed.
            const bool credible = in.clubheadSpeedImpactMph < 0.0
                               || in.clubheadSpeedImpactMph >= cfg.minCredibleClubMph;
            PlacementGate gate;
            gate.may = credible;
            finishChannel(res.club, differentiate(a, windowUs, 1.0, /*magnitude*/ true),
                          SeqSegment::Club, QStringLiteral("Club angular speed"),
                          fused ? QStringLiteral("faceOn+dtl") : QStringLiteral("faceOnClub"),
                          false, clubDom, phases, cfg, nodes, gate);
        }
    }

    res.valid = res.pelvis.produced() || res.thorax.produced() || res.leadArm.produced() || res.club.produced();
    if (res.valid)
        res.sequence = resolveKinematicSequence(std::move(nodes), impactUs, cfg.sigmaK);
    return res;
}

std::vector<MetricSeries> segmentRateSeries(const SegmentRatesResult &res)
{
    std::vector<MetricSeries> out;
    for (SeqSegment s : { SeqSegment::Pelvis, SeqSegment::Thorax, SeqSegment::LeadArm, SeqSegment::Club }) {
        const SegmentRateChannel &ch = res.channel(s);
        if (ch.produced()) out.push_back(ch.series);
    }
    return out;
}

} // namespace pinpoint::analysis
