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
                    && p.tPeakUs >= gate.blindFromUs - windowUs;
        const bool atExit = gate.blindToUs >= 0 && gate.blindToUs >= dom.fromUs
                         && p.tPeakUs <= gate.blindToUs + windowUs && !atEntry;
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
        if (gate.sighted && !atEntry && !atExit) {
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
        // Placed only when the σ is inside the threshold, the route is allowed to place this
        // segment at all (§9 / §12), and the peak was in sight. The attempt is kept for the trace.
        n.placed       = gate.may && !atEntry && !atExit && !spike && p.tSigmaMs <= cfg.maxPlaceSigmaMs;
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
    const double phi = psiRad - nuRad;
    const double c = std::cos(phi), s = std::sin(phi);
    const double den = c * c + k * k * s * s;
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
            deprojectTrack(a, plane, cfg);
            // A track whose own clubhead speed at impact is not credible is a broken track, and
            // the synth tier's shaft angle on one is a straight line between anchors. The curve is
            // still produced (it is what the track says); the node is not claimed.
            const bool credible = in.clubheadSpeedImpactMph < 0.0
                               || in.clubheadSpeedImpactMph >= cfg.minCredibleClubMph;
            PlacementGate gate;
            gate.may = credible;
            finishChannel(res.club, differentiate(a, windowUs, 1.0, /*magnitude*/ true),
                          SeqSegment::Club, QStringLiteral("Club angular speed"),
                          QStringLiteral("faceOnClub"), false, clubDom, phases, cfg, nodes, gate);
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
