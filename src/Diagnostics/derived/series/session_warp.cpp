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

#include "session_warp.h"

#include <QHash>

#include <algorithm>
#include <cmath>
#include <map>

namespace pinpoint::charts {

namespace {

// ── tuned constants (the header says why each) ──────────────────────────────────────────────
constexpr double kPeakFromMs        = -200.0;  // the downswing speed peak is searched in [−200, +40]
constexpr double kPeakToMs          =   40.0;
constexpr double kPeakFlankMs       =   40.0;  // a peak, not a plateau: the curve ±40 ms ≤ 98 %
constexpr double kPeakFlankShare    =   0.98;
constexpr double kDipFromPeakMs     = -600.0;  // the change of direction: [peak − 600, peak − 60]
constexpr double kDipToPeakMs       =  -60.0;
constexpr double kDipShareOfPeak    =   0.25;  // the dip is ≤ 25 % of the downswing peak …
constexpr double kDipBackswingRatio =   2.0;   // … and the backswing maximum is ≥ 2× it
constexpr double kBackFromDipMs     = -900.0;  // the backswing maximum: [dip − 900, dip − 50]
constexpr double kBackToDipMs       =  -50.0;
constexpr double kOnsetShare        =   0.25;  // motion onset: 25 % of the backswing maximum
constexpr double kTempoFromMs       = -900.0;  // the tempo fit's window without a canonical P1
constexpr double kTempoToMs         =   60.0;
constexpr double kTempoGridMs       =    2.0;
constexpr double kTempoMinOverlap   =   0.6;
constexpr double kSlopeBox          =  2.999;  // strictly inside Fritsch–Carlson's (0, 3δ)
constexpr double kEndSlopeFloor     =  0.05;   // an end slope never below 5 % of its secant

bool isFinite(double x) { return std::isfinite(x); }

// A candidate anchor in the outward walk. `id` names it for the session's segment table: a phase id,
// or 100 + landmark for a landmark.
struct Cand {
    int      id = -1;
    int      phase = -1;
    Landmark landmark = Landmark::None;
    double   ms = kNaN;
    double   conf = kNaN;
    double   canon = kNaN;      // pass 2 / landmarks: the canonical time; NaN = not canonical
    bool     confOk = true;
};

int landmarkId(Landmark l) { return 100 + int(l); }

// The shot's last-written time of each phase (the mocks' phaseMs: a later entry overwrites).
QHash<int, std::pair<double, double>> phaseTimes(const ShotSeries &s)
{
    QHash<int, std::pair<double, double>> out;
    for (const PhaseMark &p : s.phases)
        out.insert(p.phase, { double(p.tUs - s.impactUs) / 1000.0, p.conf });
    return out;
}

// The session's median segment between two anchor ids (signed so that a segment running away from
// impact is positive), over the shots that have both through gate 1. Built once per alignment.
struct SegmentTable {
    std::map<std::pair<int, int>, std::vector<double>> lens;   // (inner, outer) → lengths
    int minShots = 3;
    double ref(int inner, int outer) const
    {
        auto it = lens.find({ inner, outer });
        if (it == lens.end() || int(it->second.size()) < minShots)
            return kNaN;
        return quantile7(it->second, 0.5);
    }
};

// The outward walk on one side of impact (see the header's gates). `sign` −1 before impact, +1
// after. Candidates arrive in ladder order outward from impact. With `useCanon` the canonical half of
// gate 2 and gate 4 apply; `refFromCanon` judges gate 3 against the canonical segment (landmarks)
// rather than the session table (P-anchors).
void walkSide(const std::vector<Cand> &cands, int sign, const SegmentTable *table, bool useCanon,
              bool refFromCanon, double ratio, std::vector<WarpAnchor> &kept,
              std::vector<GatedAnchor> &considered)
{
    int    innerId = phase::Impact;
    double innerMs = 0.0, innerCanon = 0.0;
    for (const Cand &c : cands) {
        GatedAnchor g;
        g.phase = c.phase; g.landmark = c.landmark; g.shotMs = c.ms; g.conf = c.conf;
        // gate rule: PP_GATE("rule.charts.warpAnchors")
        if (!c.confOk) {
            g.gate = AnchorGate::LowConfidence;
        } else if (useCanon && !isFinite(c.canon)) {
            g.gate = AnchorGate::NotCanonical;
        } else if (!(sign * (c.ms - innerMs) > 0.0)
                   || (useCanon && !(sign * (c.canon - innerCanon) > 0.0))) {
            g.gate = AnchorGate::RunsBackwards;
        } else {
            const double len = sign * (c.ms - innerMs);
            const double ref = refFromCanon ? sign * (c.canon - innerCanon)
                                            : (table ? table->ref(innerId, c.id) : kNaN);
            g.segmentMs = len;
            g.sessionSegmentMs = ref;
            if (isFinite(ref) && ref > 0.0 && (len > ratio * ref || len < ref / ratio)) {
                g.gate = AnchorGate::OutlierSegment;
            } else {
                g.gate = AnchorGate::Kept;
                kept.push_back(WarpAnchor{ c.phase, c.landmark, c.ms, useCanon ? c.canon : kNaN });
                innerId = c.id; innerMs = c.ms; innerCanon = c.canon;
            }
        }
        considered.push_back(g);
    }
}

// Ladder phases before / after impact, outward from it.
std::vector<int> preImpactOutward()
{
    std::vector<int> out;
    for (int p : pLadder()) { if (p == phase::Impact) break; out.push_back(p); }
    std::reverse(out.begin(), out.end());
    return out;
}
std::vector<int> postImpactOutward()
{
    std::vector<int> out;
    bool after = false;
    for (int p : pLadder()) { if (after) out.push_back(p); if (p == phase::Impact) after = true; }
    return out;
}

// Candidates for one shot on one side, in walk order.
std::vector<Cand> phaseCands(const QHash<int, std::pair<double, double>> &pt, const std::vector<int> &order,
                             const WarpOptions &o, const QHash<int, double> *canon)
{
    std::vector<Cand> out;
    for (int p : order) {
        auto it = pt.constFind(p);
        if (it == pt.constEnd()) continue;
        Cand c;
        c.id = p; c.phase = p; c.ms = it->first; c.conf = it->second;
        c.confOk = !(isFinite(c.conf) && c.conf < o.minConf);
        if (canon) c.canon = canon->value(p, kNaN);
        out.push_back(c);
    }
    return out;
}

// Sort anchors by shot time and keep them strictly ascending in both coordinates (the walk already
// guarantees it; this only merges the two sides around impact).
std::vector<WarpAnchor> mergeSides(std::vector<WarpAnchor> pre, const std::vector<WarpAnchor> &post)
{
    std::reverse(pre.begin(), pre.end());
    pre.push_back(WarpAnchor{ phase::Impact, Landmark::None, 0.0, 0.0 });
    pre.insert(pre.end(), post.begin(), post.end());
    return pre;
}

// ── landmarks on one curve ───────────────────────────────────────────────────────────────────

// Index of the extreme measured sample with t in [a, b]; -1 when none. `interior` reports whether it
// is neither the first nor the last measured sample of that window.
int extremeIn(const TraceCurve &c, double a, double b, bool wantMax, bool *interior)
{
    int best = -1, first = -1, last = -1;
    for (size_t i = 0; i < c.t.size(); ++i) {
        if (!c.ok[i] || !isFinite(c.v[i]) || c.t[i] < a || c.t[i] > b) continue;
        if (first < 0) first = int(i);
        last = int(i);
        if (best < 0 || (wantMax ? c.v[i] > c.v[size_t(best)] : c.v[i] < c.v[size_t(best)])) best = int(i);
    }
    if (interior) *interior = best >= 0 && best != first && best != last;
    return best;
}

} // namespace

// ── the ladder ──────────────────────────────────────────────────────────────────────────────

const std::vector<int> &pLadder()
{
    static const std::vector<int> ladder { phase::Address, phase::P2, phase::P3, phase::Top, phase::P5,
                                           phase::P6, phase::Impact, phase::P8, phase::P9, phase::Finish };
    return ladder;
}

// ── the warp ────────────────────────────────────────────────────────────────────────────────

int ShotWarp::droppedCount() const
{
    return int(std::count_if(considered.begin(), considered.end(),
                             [](const GatedAnchor &g) { return g.gate != AnchorGate::Kept; }));
}

qint64 ShotWarp::usAtShotMs(double shotMs) const
{
    return impactUs + qint64(std::llround(shotMs * 1000.0));
}

ShotWarp makeWarp(std::vector<WarpAnchor> anchors, WarpInterp interp)
{
    ShotWarp w;
    w.interp  = interp;
    w.anchors = std::move(anchors);
    const size_t n = w.anchors.size();
    if (interp != WarpInterp::Pchip || n < 2)
        return w;
    // Secants, then Fritsch–Butland interior slopes (the weighted harmonic mean; every secant is
    // positive because the anchors ascend strictly) — every slope held strictly inside (0, 3δ) of each
    // adjacent secant, which keeps the cubic strictly increasing on every segment.
    std::vector<double> h(n - 1), d(n - 1);
    for (size_t k = 0; k + 1 < n; ++k) {
        h[k] = w.anchors[k + 1].shotMs - w.anchors[k].shotMs;
        d[k] = (w.anchors[k + 1].canonMs - w.anchors[k].canonMs) / h[k];
    }
    w.slopes.assign(n, 0.0);
    for (size_t k = 1; k + 1 < n; ++k) {
        const double w1 = 2.0 * h[k] + h[k - 1], w2 = h[k] + 2.0 * h[k - 1];
        double m = (w1 + w2) / (w1 / d[k - 1] + w2 / d[k]);
        m = std::min({ m, kSlopeBox * d[k - 1], kSlopeBox * d[k] });
        w.slopes[k] = m;
    }
    // The ends: the usual three-point (one-sided, shape-preserving) estimate, held inside the box —
    // with a floor of 5 % of the secant rather than 0, so the end segment stays STRICTLY increasing.
    // Two anchors: the secant itself (the warp is then linear between them).
    const auto endSlope = [](double h0, double h1, double d0, double d1) {
        const double e = ((2.0 * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
        return std::clamp(e, kEndSlopeFloor * d0, kSlopeBox * d0);
    };
    if (n == 2) {
        w.slopes[0] = w.slopes[1] = d[0];
    } else {
        w.slopes[0]     = endSlope(h[0], h[1], d[0], d[1]);
        w.slopes[n - 1] = endSlope(h[n - 2], h[n - 3], d[n - 2], d[n - 3]);
    }
    return w;
}

double ShotWarp::warpMs(double t) const
{
    if (route == AlignRoute::TempoFitted && isFinite(tempoScale))
        return tempoScale * t;
    const size_t n = anchors.size();
    if (n == 0)
        return t;
    if (t <= anchors[0].shotMs)
        return t - anchors[0].shotMs + anchors[0].canonMs;
    for (size_t i = 1; i < n; ++i) {
        if (t <= anchors[i].shotMs) {
            const WarpAnchor &a = anchors[i - 1], &b = anchors[i];
            const double hh = b.shotMs - a.shotMs;
            const double f = (t - a.shotMs) / hh;
            if (interp == WarpInterp::Linear || slopes.size() != n)
                return a.canonMs + f * (b.canonMs - a.canonMs);
            const double f2 = f * f, f3 = f2 * f;
            return (2 * f3 - 3 * f2 + 1) * a.canonMs + (f3 - 2 * f2 + f) * hh * slopes[i - 1]
                 + (-2 * f3 + 3 * f2) * b.canonMs + (f3 - f2) * hh * slopes[i];
        }
    }
    const WarpAnchor &L = anchors[n - 1];
    return t - L.shotMs + L.canonMs;
}

double ShotWarp::unwarpMs(double c) const
{
    if (route == AlignRoute::TempoFitted && isFinite(tempoScale))
        return c / tempoScale;
    const size_t n = anchors.size();
    if (n == 0)
        return c;
    if (c <= anchors[0].canonMs)
        return c - anchors[0].canonMs + anchors[0].shotMs;
    for (size_t i = 1; i < n; ++i) {
        if (c <= anchors[i].canonMs) {
            const WarpAnchor &a = anchors[i - 1], &b = anchors[i];
            if (interp == WarpInterp::Linear || slopes.size() != n) {
                const double f = (c - a.canonMs) / (b.canonMs - a.canonMs);
                return a.shotMs + f * (b.shotMs - a.shotMs);
            }
            if (c == b.canonMs) return b.shotMs;
            if (c == a.canonMs) return a.shotMs;
            // Strictly increasing on [a, b], so bisection converges to the last bit.
            double lo = a.shotMs, hi = b.shotMs;
            for (int it = 0; it < 200 && hi - lo > 0.0; ++it) {
                const double mid = 0.5 * (lo + hi);
                if (mid <= lo || mid >= hi) break;
                if (warpMs(mid) < c) lo = mid; else hi = mid;
            }
            return std::abs(warpMs(lo) - c) <= std::abs(warpMs(hi) - c) ? lo : hi;
        }
    }
    const WarpAnchor &L = anchors[n - 1];
    return c - L.canonMs + L.shotMs;
}

TraceCurve alignedCurve(const TraceCurve &c, const ShotWarp &w)
{
    TraceCurve out;
    out.ordinal = c.ordinal;
    out.key     = c.key;
    out.axis    = TimeAxis::CanonicalMs;
    if (c.empty() || !w.aligned())
        return out;
    out.t.reserve(c.t.size());
    for (double x : c.t) out.t.push_back(w.warpMs(x));
    out.v = c.v; out.raw = c.raw; out.ok = c.ok; out.sigma = c.sigma;
    return out;
}

// ── landmarks ───────────────────────────────────────────────────────────────────────────────

std::vector<LandmarkFind> findLandmarks(const TraceCurve &c)
{
    std::vector<LandmarkFind> out { { Landmark::MotionOnset, false, kNaN },
                                    { Landmark::ChangeOfDirection, false, kNaN },
                                    { Landmark::SpeedPeak, false, kNaN } };
    if (c.empty())
        return out;

    // The downswing speed peak: a maximum, not the edge of a search window, and not a plateau.
    bool interior = false;
    const int pk = extremeIn(c, kPeakFromMs, kPeakToMs, true, &interior);
    double peakV = pk >= 0 ? c.v[size_t(pk)] : kNaN;
    if (pk >= 0 && interior && peakV > 0.0) {
        const double l = valueAt(c, c.t[size_t(pk)] - kPeakFlankMs);
        const double r = valueAt(c, c.t[size_t(pk)] + kPeakFlankMs);
        if (isFinite(l) && isFinite(r) && l <= kPeakFlankShare * peakV && r <= kPeakFlankShare * peakV) {
            out[2].found = true;
            out[2].shotMs = c.t[size_t(pk)];
        }
    }

    // The change of direction: the speed's dip between the backswing and the downswing.
    const double from = out[2].found ? out[2].shotMs : 0.0;
    const int dip = extremeIn(c, from + kDipFromPeakMs, from + kDipToPeakMs, false, &interior);
    if (dip < 0 || !interior || !(peakV > 0.0))
        return out;
    const double dipT = c.t[size_t(dip)], dipV = c.v[size_t(dip)];
    bool backInterior = false;
    const int bm = extremeIn(c, dipT + kBackFromDipMs, dipT + kBackToDipMs, true, &backInterior);
    if (bm < 0)
        return out;
    const double backV = c.v[size_t(bm)];
    if (dipV <= kDipShareOfPeak * peakV && backV > 0.0 && backV >= kDipBackswingRatio * dipV) {
        out[1].found = true;
        out[1].shotMs = dipT;
    } else {
        return out;
    }

    // Motion onset: walking back from the backswing maximum, the crossing of 25 % of it.
    if (!backInterior)
        return out;
    const double theta = kOnsetShare * backV;
    int firstOk = -1;
    for (size_t i = 0; i < c.t.size(); ++i) if (c.ok[i]) { firstOk = int(i); break; }
    for (int j = bm - 1; j >= 0; --j) {
        if (!c.ok[size_t(j)] || !(c.v[size_t(j)] <= theta)) continue;
        if (j <= firstOk) break;                         // the curve starts below: no quiet before it
        const size_t a = size_t(j), b = size_t(j + 1);
        double t = c.t[a];
        if (c.ok[b] && c.v[b] > c.v[a])
            t = c.t[a] + (theta - c.v[a]) / (c.v[b] - c.v[a]) * (c.t[b] - c.t[a]);
        out[0].found = true;
        out[0].shotMs = t;
        break;
    }
    return out;
}

// ── the session ─────────────────────────────────────────────────────────────────────────────

const ShotWarp *SessionAlignment::warpFor(int ordinal) const
{
    for (const ShotWarp &w : shots) if (w.ordinal == ordinal) return &w;
    return nullptr;
}

const CanonicalPhase *SessionAlignment::canonicalOf(int ph) const
{
    for (const CanonicalPhase &c : canonical) if (c.phase == ph) return &c;
    return nullptr;
}

int SessionAlignment::countRoute(AlignRoute r) const
{
    return int(std::count_if(shots.begin(), shots.end(), [r](const ShotWarp &w) { return w.route == r; }));
}

namespace {

CanonicalPhase canonicalFrom(int ph, const std::vector<double> &xs)
{
    CanonicalPhase c;
    c.phase = ph;
    c.n     = int(xs.size());
    c.tMs   = quantile7(xs, 0.5);
    c.q1Ms  = quantile7(xs, 0.25);
    c.q3Ms  = quantile7(xs, 0.75);
    c.loMs  = *std::min_element(xs.begin(), xs.end());
    c.hiMs  = *std::max_element(xs.begin(), xs.end());
    return c;
}

// The mocks' ladder exactly (variant_a/lib/traces.js canonical / anchors), for WarpOptions::gates false.
void alignAsTheMocks(const SessionSnapshot &s, SessionAlignment &out)
{
    const auto &ladder = pLadder();
    std::vector<QHash<int, std::pair<double, double>>> pts;
    for (const auto &sh : s.shots) pts.push_back(sh->impactUs >= 0 ? phaseTimes(*sh) : decltype(pts)::value_type{});
    QHash<int, double> canon;
    for (int p : ladder) {
        std::vector<double> xs;
        for (const auto &pt : pts) { auto it = pt.constFind(p); if (it != pt.constEnd()) xs.push_back(it->first); }
        if (xs.empty()) continue;
        out.canonical.push_back(canonicalFrom(p, xs));
        canon.insert(p, out.canonical.back().tMs);
    }
    out.shots.reserve(s.shots.size());
    for (size_t i = 0; i < s.shots.size(); ++i) {
        const ShotSeries &sh = *s.shots[i];
        std::vector<WarpAnchor>  a;
        std::vector<GatedAnchor> considered;
        for (int p : ladder) {
            auto it = pts[i].constFind(p);
            if (it == pts[i].constEnd() || !canon.contains(p)) continue;
            const double sm = it->first, cm = canon.value(p);
            GatedAnchor g; g.phase = p; g.shotMs = sm; g.conf = it->second;
            if (!a.empty() && (sm <= a.back().shotMs || cm <= a.back().canonMs))
                g.gate = AnchorGate::RunsBackwards;
            else
                a.push_back(WarpAnchor{ p, Landmark::None, sm, cm });
            considered.push_back(g);
        }
        const bool anyP = std::any_of(a.begin(), a.end(), [](const WarpAnchor &x) { return x.phase != phase::Impact; });
        ShotWarp w = makeWarp(std::move(a), out.options.interp);
        w.ordinal    = sh.ordinal;
        w.impactUs   = sh.impactUs;
        w.considered = std::move(considered);
        w.route      = sh.impactUs < 0 ? AlignRoute::Unaligned : anyP ? AlignRoute::PAnchors : AlignRoute::ImpactShift;
        out.shots.push_back(std::move(w));
    }
}

} // namespace

SessionAlignment alignSession(const SessionSnapshot &s, const WarpOptions &o)
{
    SessionAlignment out;
    out.options = o;
    if (!o.gates) {
        alignAsTheMocks(s, out);
        return out;
    }

    const size_t N = s.shots.size();
    const std::vector<int> pre = preImpactOutward(), post = postImpactOutward();
    std::vector<QHash<int, std::pair<double, double>>> pts(N);
    int nImpact = 0;
    for (size_t i = 0; i < N; ++i) {
        if (s.shots[i]->impactUs < 0) continue;
        pts[i] = phaseTimes(*s.shots[i]);
        ++nImpact;
    }

    // The session's segment table: every (inner, outer) pair along the walk order, from the shots
    // that have both through gate 1. Built over every pair a walk could form (the inner neighbour may
    // be any kept anchor nearer impact), so a dropped anchor's neighbours are still judged.
    SegmentTable table;
    table.minShots = o.minSegmentShots;
    for (size_t i = 0; i < N; ++i) {
        if (s.shots[i]->impactUs < 0) continue;
        for (const std::vector<int> *side : { &pre, &post }) {
            const int sign = side == &pre ? -1 : 1;
            const std::vector<Cand> cs = phaseCands(pts[i], *side, o, nullptr);
            for (size_t b = 0; b < cs.size(); ++b) {
                if (!cs[b].confOk) continue;
                table.lens[{ phase::Impact, cs[b].id }].push_back(sign * cs[b].ms);
                for (size_t a = 0; a < b; ++a)
                    if (cs[a].confOk) table.lens[{ cs[a].id, cs[b].id }].push_back(sign * (cs[b].ms - cs[a].ms));
            }
        }
    }

    // Pass 1: gates 1–3 on the shot clock; the canonical P-times from what survived.
    std::map<int, std::vector<double>> keptTimes;
    for (size_t i = 0; i < N; ++i) {
        if (s.shots[i]->impactUs < 0) continue;
        std::vector<WarpAnchor> kPre, kPost;
        std::vector<GatedAnchor> cons;
        walkSide(phaseCands(pts[i], pre, o, nullptr), -1, &table, false, false, o.maxSegmentRatio, kPre, cons);
        walkSide(phaseCands(pts[i], post, o, nullptr), +1, &table, false, false, o.maxSegmentRatio, kPost, cons);
        for (const WarpAnchor &a : kPre)  keptTimes[a.phase].push_back(a.shotMs);
        for (const WarpAnchor &a : kPost) keptTimes[a.phase].push_back(a.shotMs);
    }
    const int need = std::max(1, int(std::ceil(nImpact / 2.0)));
    QHash<int, double> canon;
    for (int p : pLadder()) {
        if (p == phase::Impact) {
            if (nImpact > 0) {
                CanonicalPhase c; c.phase = p; c.n = nImpact; c.tMs = c.q1Ms = c.q3Ms = c.loMs = c.hiMs = 0.0;
                out.canonical.push_back(c);
                canon.insert(p, 0.0);
            }
            continue;
        }
        auto it = keptTimes.find(p);
        if (it == keptTimes.end() || int(it->second.size()) < need) continue;
        out.canonical.push_back(canonicalFrom(p, it->second));
        canon.insert(p, out.canonical.back().tMs);
    }

    // Pass 2: the same walk with the canonical ladder (gate 4, gate 2 on the canonical axis).
    out.shots.resize(N);
    std::vector<size_t> needFallback;
    for (size_t i = 0; i < N; ++i) {
        const ShotSeries &sh = *s.shots[i];
        ShotWarp &w = out.shots[i];
        w.ordinal = sh.ordinal; w.impactUs = sh.impactUs; w.interp = o.interp;
        if (sh.impactUs < 0) { w.route = AlignRoute::Unaligned; continue; }
        std::vector<WarpAnchor> kPre, kPost;
        std::vector<GatedAnchor> cons;
        walkSide(phaseCands(pts[i], pre, o, &canon), -1, &table, true, false, o.maxSegmentRatio, kPre, cons);
        walkSide(phaseCands(pts[i], post, o, &canon), +1, &table, true, false, o.maxSegmentRatio, kPost, cons);
        const bool anyP = !kPre.empty() || !kPost.empty();
        ShotWarp built = makeWarp(mergeSides(std::move(kPre), kPost), o.interp);
        built.ordinal = sh.ordinal; built.impactUs = sh.impactUs; built.considered = std::move(cons);
        built.route = anyP ? AlignRoute::PAnchors : AlignRoute::ImpactShift;
        w = std::move(built);
        if (!anyP) needFallback.push_back(i);
    }
    if (needFallback.empty() || (!o.useLandmarks && !o.useTempo))
        return out;

    // ── fallback 1: curve landmarks ──
    std::vector<QHash<QString, TraceCurve>> curves(N);
    auto curveOf = [&](size_t i, const QString &key) -> const TraceCurve & {
        auto it = curves[i].find(key);
        if (it == curves[i].end()) it = curves[i].insert(key, shotCurve(*s.shots[i], key));
        return it.value();
    };
    if (o.useLandmarks) {
        const int needLm = std::max(2, int(std::ceil(nImpact / 2.0)));
        // finds[i][landmark] for the key chosen per landmark
        std::vector<std::vector<LandmarkFind>> finds(N, std::vector<LandmarkFind>(3));
        for (int lm = 0; lm < 3; ++lm) {
            for (const QString &key : o.landmarkKeys) {
                std::vector<LandmarkFind> got(N);
                int n = 0;
                for (size_t i = 0; i < N; ++i) {
                    if (s.shots[i]->impactUs < 0) continue;
                    got[i] = findLandmarks(curveOf(i, key))[size_t(lm)];
                    if (got[i].found) ++n;
                }
                if (n < needLm) continue;
                CanonicalLandmark cl;
                cl.which = Landmark(lm); cl.key = key; cl.nFound = n;
                std::vector<double> aligned, raw;
                for (size_t i = 0; i < N; ++i) {
                    if (!got[i].found) continue;
                    raw.push_back(got[i].shotMs);
                    if (out.shots[i].route == AlignRoute::PAnchors) aligned.push_back(out.shots[i].warpMs(got[i].shotMs));
                }
                cl.fromAligned = aligned.size() >= 3;
                cl.tMs = quantile7(cl.fromAligned ? aligned : raw, 0.5);
                out.landmarks.push_back(cl);
                for (size_t i = 0; i < N; ++i) finds[i][size_t(lm)] = got[i];
                break;
            }
        }
        for (size_t idx = 0; idx < needFallback.size();) {
            const size_t i = needFallback[idx];
            std::vector<Cand> cPre, cPost;
            for (const CanonicalLandmark &cl : out.landmarks) {
                const LandmarkFind &f = finds[i][size_t(cl.which)];
                if (!f.found) continue;
                Cand c; c.id = landmarkId(cl.which); c.landmark = cl.which; c.ms = f.shotMs; c.canon = cl.tMs;
                (cl.tMs < 0.0 ? cPre : cPost).push_back(c);
            }
            std::sort(cPre.begin(), cPre.end(), [](const Cand &a, const Cand &b) { return a.canon > b.canon; });
            std::sort(cPost.begin(), cPost.end(), [](const Cand &a, const Cand &b) { return a.canon < b.canon; });
            std::vector<WarpAnchor> kPre, kPost;
            std::vector<GatedAnchor> cons = out.shots[i].considered;
            walkSide(cPre, -1, nullptr, true, true, o.maxSegmentRatio, kPre, cons);
            walkSide(cPost, +1, nullptr, true, true, o.maxSegmentRatio, kPost, cons);
            if (kPre.empty() && kPost.empty()) { out.shots[i].considered = std::move(cons); ++idx; continue; }
            ShotWarp built = makeWarp(mergeSides(std::move(kPre), kPost), o.interp);
            built.ordinal = s.shots[i]->ordinal; built.impactUs = s.shots[i]->impactUs;
            built.considered = std::move(cons); built.route = AlignRoute::Landmarks;
            out.shots[i] = std::move(built);
            needFallback.erase(needFallback.begin() + std::ptrdiff_t(idx));
        }
    }
    if (needFallback.empty() || !o.useTempo)
        return out;

    // ── fallback 2: one tempo scale about impact, fitted to the median curve ──
    const CanonicalPhase *p1 = out.canonicalOf(phase::Address);
    const double t0 = (p1 && isFinite(p1->tMs)) ? p1->tMs : kTempoFromMs;
    const std::vector<double> grid = timeGrid(t0, kTempoToMs, kTempoGridMs);
    for (const QString &key : o.landmarkKeys) {
        if (needFallback.empty()) break;
        // the reference: aligned shots' curves when at least 3, else every shot's at impact
        std::vector<TraceCurve> ref;
        for (size_t i = 0; i < N; ++i) {
            const ShotWarp &w = out.shots[i];
            if (w.route != AlignRoute::PAnchors && w.route != AlignRoute::Landmarks) continue;
            TraceCurve a = alignedCurve(curveOf(i, key), w);
            if (!a.empty()) ref.push_back(std::move(a));
        }
        if (ref.size() < 3) {
            ref.clear();
            for (size_t i = 0; i < N; ++i)
                if (s.shots[i]->impactUs >= 0 && !curveOf(i, key).empty()) ref.push_back(curveOf(i, key));
        }
        if (ref.size() < 3) continue;
        std::vector<double> med(grid.size(), kNaN);
        int nMed = 0;
        for (size_t g = 0; g < grid.size(); ++g) {
            std::vector<double> xs;
            for (const TraceCurve &c : ref) { const double v = valueAt(c, grid[g]); if (isFinite(v)) xs.push_back(v); }
            if (xs.size() >= 3) { med[g] = quantile7(xs, 0.5); ++nMed; }
        }
        if (nMed == 0) continue;
        for (size_t idx = 0; idx < needFallback.size();) {
            const size_t i = needFallback[idx];
            const TraceCurve &c = curveOf(i, key);
            if (c.empty()) { ++idx; continue; }
            const int steps = int(std::floor((o.tempoMax - o.tempoMin) / o.tempoStep + 1e-9));
            double bestS = kNaN, bestCost = kNaN;
            for (int k = 0; k <= steps; ++k) {
                const double sc = o.tempoMin + k * o.tempoStep;
                double sum = 0.0; int m = 0;
                for (size_t g = 0; g < grid.size(); ++g) {
                    if (!isFinite(med[g])) continue;
                    const double v = valueAt(c, grid[g] / sc);
                    if (!isFinite(v)) continue;
                    sum += (v - med[g]) * (v - med[g]); ++m;
                }
                if (m < kTempoMinOverlap * nMed) continue;
                const double cost = sum / m;
                if (!isFinite(bestCost) || cost < bestCost) { bestCost = cost; bestS = sc; }
            }
            if (!isFinite(bestS) || bestS <= o.tempoMin + 0.5 * o.tempoStep || bestS >= o.tempoMax - 0.5 * o.tempoStep) {
                ++idx; continue;
            }
            ShotWarp &w = out.shots[i];
            w.route = AlignRoute::TempoFitted;
            w.tempoScale = bestS;
            needFallback.erase(needFallback.begin() + std::ptrdiff_t(idx));
        }
    }
    return out;
}

} // namespace pinpoint::charts
