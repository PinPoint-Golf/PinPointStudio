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

// imu_retime.h — dating a host-stamped IMU lane by its own sample clock
// (trunk_imu_sequence_grf_design.md §7.1 step 1, W2.4).
//
// A Witmotion sample is stamped when it ARRIVES at the host
// (EventBuffer::nowMicros() in ImuInstance's quaternionUpdated lambda). The
// WT901BLE67 packs 8 samples into one notification and the host's connection
// events quantise delivery to ~30 ms, so a lane arrives as bursts of 8 stamps
// ~0.2 ms apart, 30 or 60 ms between bursts (W0 desk session, §17.4). An
// arrival therefore dates its sample only to within about one burst, and a
// fuser that interpolates on arrivals smears every trunk peak by up to 30 ms.
//
// The sample itself was taken on the device's clock, one period after the one
// before. So: retimed_i = c + T·k_i, where k_i is the sample's device index
// (its arrival index plus every sample lost before it), T the stream's OWN
// period, and c the LOWER ENVELOPE of the arrivals — an arrival can only ever
// be later than its sample, never earlier.
//
//   THE PERIOD IS FITTED, NEVER ASSUMED. Every W0 unit runs 198.6 Hz at a
//     configured 200: the device clock is ~0.7 % slow. Read against 5.000 ms
//     that slow clock looks exactly like a sample lost every ~0.7 s.
//
//   THE FIT IS THE LOWER CONVEX HULL, not least squares. Minimising the mean
//     of (arrival − line) with the line held under every arrival is a linear
//     programme whose optimum is the hull edge spanning the mean index — the
//     standard skew estimator for one-way network delay. It touches the
//     fastest deliveries, which are the ones that carry the timing.
//
// ⚠ THE FLOOR IS A SAWTOOTH, AND THAT IS WHY LOSS IS NOT "A STEP IN THE
// ENVELOPE OVER A SHORT WINDOW". 8 samples every 40.3 ms against connection
// events every 30 ms beat: each burst waits a slightly different time for its
// connection event, and the best wait in each cycle drifts 0 → 10 ms and
// snaps back about every 1.4 s. Read through short windows each snap is a
// 10 ms step up — two samples' worth — so a detector tuned to one period
// "finds" ~100 two-sample losses in two minutes of a link that lost nothing.
// trunk_hw.py's rate deficit counted exactly those, and the first draft of
// this header did too. The W0 records prove there was no loss: the device's
// own integrated yaw drift advances by the same amount every sample across
// every one of those steps, and the burst sizes never fall out of step.
//
// So loss is read two ways, both immune to the sawtooth:
//
//   BATCH RESIDUE (part of a batch). When one delivery size dominates (8 on
//     the WT901BLE67), the running sample count at the end of every full
//     delivery keeps one residue modulo that size; a split delivery (3 + 5)
//     leaves it alone. A sample lost from a batch moves the residue for good.
//     Exact, and it says WHICH delivery came up short. A lane sent one sample
//     at a time is clustered by the connection interval too, but its residue
//     keeps walking; more than 2 changes per 10 s and it is not read as batches.
//
//   ENVELOPE STEP (whole batches, or any loss on a lane with no batching).
//     The floor of arrival − T·k is compared over 0.5 s either side of each
//     delivery; a rise of at least 20 ms (twice the W0 sawtooth) is loss,
//     rounded to whole periods — or, when batching, 70 % of a batch, rounded
//     to whole batches, since the residue has already taken the rest.
//     It is placed at the longest delivery gap where the floor rose: a lost
//     notification is a missing delivery. A loss smaller than 20 ms on a lane
//     WITHOUT batching is not seen; nothing on the W0 records lost even one.
//
// A lane whose arrivals stop for longer than 250 ms (8× the connection
// interval; the W0 maximum was 89 ms) is cut into segments, each anchored
// again, because a reconnect can change the delivery floor.
//
// Pure, header-only, no Qt: the W0 records replay through it in a test.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace pinpoint::imu_retime {

struct RetimeConfig {
    double segmentGapUs         = 250'000.0;  // an arrival gap this long re-anchors (a new segment)
    std::size_t minFitSamples   = 64;         // fewer: the period is borrowed, never fitted
    double minFitSpanUs         = 2'000'000.0;// shorter segments borrow the longest segment's period
                                              // (a sub-beat span fits the sawtooth's ramp, not the clock)
    double burstSplitFrac       = 0.5;        // arrivals closer than this × period are one delivery
    double envelopeWindowUs     = 500'000.0;  // each side of the envelope-step test
    std::size_t envelopeMinBursts = 4;        // deliveries needed on each side to test at all
    double envelopeStepMinUs    = 20'000.0;   // the smallest floor rise read as loss
    double batchMinFraction     = 0.6;        // share of deliveries at the dominant size for batch mode
    int    residuePersist       = 2;          // full deliveries a new residue must hold to count
    double residueMaxChangesPer10s = 2.0;     // more (min 2 per lane) and the sizes are not batches
    double maxResidualP95Us     = 150'000.0;  // beyond this the arrivals do not follow one clock
    int    maxIterations        = 4;
};

enum class RetimeLossKind { BatchResidue, EnvelopeStep, SegmentBreak };

// `samples` were lost immediately BEFORE arrival[index]. A SegmentBreak count
// is an estimate across a gap where both sides were anchored separately.
struct RetimeLoss {
    std::size_t    index   = 0;
    int            samples = 0;
    RetimeLossKind kind    = RetimeLossKind::EnvelopeStep;
};

enum class RetimePeriodSource { Fitted, Borrowed, Nominal };

struct RetimeSegment {
    std::size_t        begin = 0, end = 0;   // [begin, end) into the arrivals
    double             periodUs = 0.0;
    double             anchorUs = 0.0;       // retimed time of the segment's first sample
    RetimePeriodSource source = RetimePeriodSource::Fitted;
};

struct RetimeResult {
    bool                       applied = false;   // false: retimedUs == the arrivals, see `why`
    const char                *why     = "";
    std::vector<int64_t>       retimedUs;         // one per arrival, strictly increasing when applied
    std::vector<RetimeSegment> segments;
    std::vector<RetimeLoss>    losses;
    double periodUs        = 0.0;   // the longest fitted segment's period (else the borrowed/nominal one)
    double nominalPeriodUs = 0.0;   // 1e6 / nominalHz (0 = none given)
    int    batchSize       = 0;     // dominant delivery size, 0 = no batching seen
    int    lostSamples     = 0;     // every loss, segment-break estimates included
    int    lossEvents      = 0;
    // arrival − retimed over every sample: how late the arrivals were.
    double residualP50Us = 0.0, residualP95Us = 0.0, residualMaxUs = 0.0;

    double periodHz() const { return periodUs > 0.0 ? 1.0e6 / periodUs : 0.0; }
};

namespace detail {

// Lower convex hull of (k, a) (k strictly increasing); the edge spanning the
// mean k is the LP optimum. Returns false when there is no edge.
inline bool hullFit(const std::vector<double> &k, const std::vector<double> &a,
                    double &c, double &T)
{
    const std::size_t n = k.size();
    if (n < 2)
        return false;
    std::vector<std::size_t> h;
    h.reserve(64);
    for (std::size_t i = 0; i < n; ++i) {
        while (h.size() >= 2) {
            const std::size_t i1 = h[h.size() - 2], i2 = h.back();
            const double cr = (k[i2] - k[i1]) * (a[i] - a[i1]) - (a[i2] - a[i1]) * (k[i] - k[i1]);
            if (cr <= 0.0) h.pop_back(); else break;
        }
        h.push_back(i);
    }
    double kbar = 0.0;
    for (double v : k) kbar += v;
    kbar /= double(n);
    std::size_t i1 = h.front(), i2 = h.back();
    for (std::size_t j = 0; j + 1 < h.size(); ++j)
        if (k[h[j]] <= kbar && kbar <= k[h[j + 1]]) { i1 = h[j]; i2 = h[j + 1]; break; }
    if (k[i2] <= k[i1])
        return false;
    T = (a[i2] - a[i1]) / (k[i2] - k[i1]);
    c = a[i1] - T * k[i1];
    return std::isfinite(T) && T > 0.0;
}

// Deliveries: runs of arrivals closer than `splitUs`. Returns the start index of each.
inline std::vector<std::size_t> deliveries(const std::vector<double> &a, double splitUs)
{
    std::vector<std::size_t> s;
    if (a.empty()) return s;
    s.push_back(0);
    for (std::size_t i = 1; i < a.size(); ++i)
        if (a[i] - a[i - 1] >= splitUs) s.push_back(i);
    return s;
}

inline int dominantSize(const std::vector<std::size_t> &starts, std::size_t n, double minFraction)
{
    if (starts.size() < 8) return 0;
    std::map<std::size_t, std::size_t> hist;
    for (std::size_t b = 0; b < starts.size(); ++b)
        ++hist[(b + 1 < starts.size() ? starts[b + 1] : n) - starts[b]];
    std::size_t best = 0, cnt = 0;
    for (const auto &[sz, c] : hist)
        if (c > cnt) { best = sz; cnt = c; }
    if (best < 2 || double(cnt) < minFraction * double(starts.size()))
        return 0;
    return int(best);
}

// Batch residue: the running count at the end of every full delivery keeps one
// residue mod B; a persistent change is a partial-batch loss. Adds to `lost`;
// returns how many changes it read.
inline int residueLosses(const std::vector<std::size_t> &starts, std::size_t n, int B,
                          int persist, std::map<std::size_t, int> &lost,
                          std::map<std::size_t, RetimeLossKind> &kind)
{
    if (B < 2) return 0;
    int changes = 0;
    std::size_t cum = 0;
    int settled = -1, cand = -1, candCount = 0;
    std::size_t lastSettled = 0;   // delivery index of the last full delivery at `settled`
    for (std::size_t b = 0; b < starts.size(); ++b) {
        const std::size_t sz = (b + 1 < starts.size() ? starts[b + 1] : n) - starts[b];
        cum += sz;
        if (sz % std::size_t(B) != 0)
            continue;
        const int rho = int(cum % std::size_t(B));
        if (settled < 0) { settled = rho; lastSettled = b; continue; }
        if (rho == settled) { cand = -1; candCount = 0; lastSettled = b; continue; }
        if (rho == cand) ++candCount; else { cand = rho; candCount = 1; }
        if (candCount >= persist) {
            const int m = ((settled - rho) % B + B) % B;
            const std::size_t at = starts[std::min(lastSettled + 1, starts.size() - 1)];
            lost[at] += m;
            kind[at] = RetimeLossKind::BatchResidue;
            settled = rho; cand = -1; candCount = 0; lastSettled = b;
            ++changes;
        }
    }
    return changes;
}

// Envelope steps of the floor of a − T·k (k already carrying `base` losses).
inline std::map<std::size_t, int> envelopeLosses(const std::vector<double> &a,
                                                 const std::vector<double> &k,
                                                 const std::vector<std::size_t> &starts,
                                                 double T, int B, const RetimeConfig &cfg)
{
    std::map<std::size_t, int> out;
    const std::size_t n = a.size(), nb = starts.size();
    if (nb < 2 * cfg.envelopeMinBursts) return out;
    std::vector<double> tb(nb), fb(nb);
    for (std::size_t b = 0; b < nb; ++b) {
        const std::size_t e = b + 1 < nb ? starts[b + 1] : n;
        tb[b] = a[starts[b]];
        double f = a[starts[b]] - T * k[starts[b]];
        for (std::size_t i = starts[b] + 1; i < e; ++i) f = std::min(f, a[i] - T * k[i]);
        fb[b] = f;
    }
    constexpr double kNone = 1e300;
    std::vector<double> back(nb, kNone), fwd(nb, kNone);
    std::size_t lo = 0, hi = 0;
    for (std::size_t b = 0; b < nb; ++b) {
        while (tb[lo] < tb[b] - cfg.envelopeWindowUs) ++lo;
        if (hi < b) hi = b;
        while (hi + 1 < nb && tb[hi + 1] <= tb[b] + cfg.envelopeWindowUs) ++hi;
        if (b - lo >= cfg.envelopeMinBursts)
            back[b] = *std::min_element(fb.begin() + long(lo), fb.begin() + long(b));
        if (hi + 1 - b >= cfg.envelopeMinBursts)
            fwd[b] = *std::min_element(fb.begin() + long(b), fb.begin() + long(hi + 1));
    }
    // With batching only whole batches are left to find (the residue took the rest), so
    // the bar is most of a batch: a connection interval that divides the batch period
    // turns the beat into a ramp-and-snap as tall as the interval itself (≤ 30 ms here).
    const double thr = B >= 2 ? std::max(cfg.envelopeStepMinUs, 0.7 * double(B) * T)
                              : std::max(cfg.envelopeStepMinUs, 0.6 * T);
    const double tol = std::max(0.4 * T, 2000.0);
    auto stepAt = [&](std::size_t b) {
        return (back[b] < kNone && fwd[b] < kNone) ? fwd[b] - back[b] : -kNone;
    };
    std::size_t b = 1;
    while (b < nb) {
        if (stepAt(b) < thr) { ++b; continue; }
        const std::size_t rs = b;
        const double pre = back[rs];
        double S = 0.0;
        std::size_t re = rs;
        while (re < nb && stepAt(re) >= thr) { S = std::max(S, stepAt(re)); ++re; }
        const int L = B >= 2 ? B * int(std::floor(S / (double(B) * T) + 0.3))
                             : int(std::lround(S / T));
        if (L < 1) { b = re; continue; }
        std::size_t bstar = rs;
        for (std::size_t j = rs; j < re; ++j)
            if (fwd[j] - pre >= S - tol) { bstar = j; break; }
        std::size_t btouch = bstar;
        for (std::size_t j = bstar; j < nb; ++j)
            if (fb[j] - pre <= S + tol) { btouch = j; break; }
        // The floor rose somewhere in [bstar, btouch]: a lost delivery is a missing one,
        // so the longest gap between deliveries there is where it went.
        std::size_t at = std::max<std::size_t>(1, bstar);
        double best = -1.0;
        for (std::size_t j = std::max<std::size_t>(1, bstar); j <= btouch; ++j)
            if (tb[j] - tb[j - 1] > best) { best = tb[j] - tb[j - 1]; at = j; }
        out[starts[at]] += L;
        b = std::max(re, btouch + 1);
    }
    return out;
}

inline double percentile(std::vector<double> v, double p)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double x = (double(v.size()) - 1.0) * p / 100.0;
    const std::size_t i = std::size_t(x);
    const std::size_t j = std::min(i + 1, v.size() - 1);
    return v[i] + (v[j] - v[i]) * (x - double(i));
}

struct SegFit {
    double c = 0.0, T = 0.0;
    std::vector<double> k;
    std::map<std::size_t, int> lost;              // local index -> samples
    std::map<std::size_t, RetimeLossKind> kind;
    int batch = 0;
};

// One segment (arrivals relative to its first). fixedT > 0: the period is given.
inline SegFit fitSegment(const std::vector<double> &a, double fixedT, double hintT,
                         const RetimeConfig &cfg)
{
    SegFit f;
    const std::size_t n = a.size();
    f.k.resize(n);
    for (std::size_t i = 0; i < n; ++i) f.k[i] = double(i);

    double c = 0.0, T = fixedT;
    if (T <= 0.0 && !hullFit(f.k, a, c, T))
        T = hintT;
    const std::vector<std::size_t> starts = deliveries(a, cfg.burstSplitFrac * T);
    f.batch = dominantSize(starts, n, cfg.batchMinFraction);

    std::map<std::size_t, int> residue;
    std::map<std::size_t, RetimeLossKind> kind;
    const int changes = residueLosses(starts, n, f.batch, cfg.residuePersist, residue, kind);
    // ⚠ A DOMINANT DELIVERY SIZE IS NOT ALWAYS A DEVICE BATCH. A lane sent one sample at a
    // time is clustered by the connection interval too — 3 per 15 ms event at 200 Hz — and
    // its sizes wander (3, 3, 4, 3 …) to keep the mean at interval / period, so its
    // residue walks for ever. Real batching (the WT901BLE67's 8) never moved on W0. Many
    // changes therefore mean "not batches": no residue losses, and whole-period rounding.
    const double spanS = n > 1 ? (a[n - 1] - a[0]) / 1.0e6 : 0.0;
    if (double(changes) > std::max(2.0, cfg.residueMaxChangesPer10s * spanS / 10.0)) {
        residue.clear();
        kind.clear();
        f.batch = 0;
    }

    auto indexWith = [&](const std::map<std::size_t, int> &L) {
        std::vector<double> k(n);
        int cum = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (auto it = L.find(i); it != L.end()) cum += it->second;
            k[i] = double(i) + double(cum);
        }
        return k;
    };
    const std::vector<double> kResidue = indexWith(residue);
    std::map<std::size_t, int> env, all = residue;
    f.k = kResidue;
    for (int it = 0; it < cfg.maxIterations; ++it) {
        if (fixedT <= 0.0) {
            double c2, T2;
            if (hullFit(f.k, a, c2, T2)) T = T2;
        }
        const std::map<std::size_t, int> env2 = envelopeLosses(a, kResidue, starts, T, f.batch, cfg);
        all = residue;
        for (const auto &[i, L] : env2) all[i] += L;
        f.k = indexWith(all);
        if (env2 == env && it > 0) break;
        env = env2;
    }
    if (fixedT <= 0.0) {
        double c2, T2;
        if (hullFit(f.k, a, c2, T2)) T = T2;
    }
    // Anchor on the lower envelope at the final period.
    c = a[0] - T * f.k[0];
    for (std::size_t i = 0; i < n; ++i) c = std::min(c, a[i] - T * f.k[i]);
    for (const auto &[i, L] : env)
        if (kind.find(i) == kind.end()) kind[i] = RetimeLossKind::EnvelopeStep;
    f.c = c;
    f.T = T;
    f.lost = all;
    f.kind = kind;
    return f;
}

} // namespace detail

// Re-time one host-stamped lane. `arrivalUs` ascending (ties allowed);
// `nominalHz` the configured rate (0 = unknown) — used only when no segment
// can be fitted, and recorded beside the fitted period.
inline RetimeResult retimeArrivals(const std::vector<int64_t> &arrivalUs, double nominalHz,
                                   const RetimeConfig &cfg = {})
{
    RetimeResult r;
    const std::size_t n = arrivalUs.size();
    r.retimedUs = arrivalUs;
    r.nominalPeriodUs = nominalHz > 0.0 ? 1.0e6 / nominalHz : 0.0;
    if (n < 2) { r.why = "fewer than two samples"; return r; }

    // Segments, cut where the lane went quiet.
    std::vector<std::size_t> cuts{ 0 };
    for (std::size_t i = 1; i < n; ++i)
        if (double(arrivalUs[i] - arrivalUs[i - 1]) > cfg.segmentGapUs) cuts.push_back(i);
    cuts.push_back(n);

    const double t0 = double(arrivalUs[0]);
    auto rel = [&](std::size_t b, std::size_t e) {
        std::vector<double> a(e - b);
        for (std::size_t i = b; i < e; ++i) a[i - b] = double(arrivalUs[i]) - double(arrivalUs[b]);
        return a;
    };
    const double meanT = (double(arrivalUs[n - 1]) - t0) / double(n - 1);
    const double hintT = r.nominalPeriodUs > 0.0 ? r.nominalPeriodUs : meanT;

    // Fit the period on the segments long enough to span the delivery beat.
    struct Seg { std::size_t b, e; bool fit; detail::SegFit f; };
    std::vector<Seg> segs;
    std::size_t longest = SIZE_MAX;
    for (std::size_t s = 0; s + 1 < cuts.size(); ++s) {
        const std::size_t b = cuts[s], e = cuts[s + 1];
        const bool fit = e - b >= cfg.minFitSamples
                      && double(arrivalUs[e - 1] - arrivalUs[b]) >= cfg.minFitSpanUs;
        segs.push_back({ b, e, fit, {} });
        if (fit && (longest == SIZE_MAX || e - b > segs[longest].e - segs[longest].b))
            longest = segs.size() - 1;
    }
    // None spans the beat: fit the longest anyway if it has the samples — the best on offer.
    if (longest == SIZE_MAX) {
        for (std::size_t s = 0; s < segs.size(); ++s)
            if (segs[s].e - segs[s].b >= cfg.minFitSamples
                && (longest == SIZE_MAX || segs[s].e - segs[s].b > segs[longest].e - segs[longest].b))
                longest = s;
        if (longest != SIZE_MAX) segs[longest].fit = true;
    }
    for (Seg &s : segs)
        if (s.fit) s.f = detail::fitSegment(rel(s.b, s.e), 0.0, hintT, cfg);
    const double borrowT = longest != SIZE_MAX ? segs[longest].f.T : hintT;
    for (Seg &s : segs)
        if (!s.fit) s.f = detail::fitSegment(rel(s.b, s.e), borrowT, hintT, cfg);

    r.periodUs = borrowT;
    std::vector<double> res(n);
    double prevEnd = -1e300;   // previous segment's last retimed time (absolute, relative to t0)
    for (std::size_t si = 0; si < segs.size(); ++si) {
        const Seg &s = segs[si];
        const double base = double(arrivalUs[s.b]) - t0;
        RetimeSegment out;
        out.begin = s.b; out.end = s.e;
        out.periodUs = s.f.T;
        out.anchorUs = t0 + base + s.f.c + s.f.T * s.f.k[0];
        out.source = s.fit ? RetimePeriodSource::Fitted
                   : (longest != SIZE_MAX ? RetimePeriodSource::Borrowed : RetimePeriodSource::Nominal);
        r.segments.push_back(out);
        if (s.f.batch > r.batchSize) r.batchSize = s.f.batch;
        for (const auto &[i, L] : s.f.lost) {
            if (L <= 0) continue;
            const auto kit = s.f.kind.find(i);
            r.losses.push_back({ s.b + i, L, kit != s.f.kind.end() ? kit->second : RetimeLossKind::EnvelopeStep });
        }
        if (si > 0) {
            // Samples across the break, estimated from the two anchors.
            const double gapStart = prevEnd;
            const double first = base + s.f.c + s.f.T * s.f.k[0];
            const int L = int(std::lround((first - gapStart) / s.f.T)) - 1;
            if (L > 0) r.losses.push_back({ s.b, L, RetimeLossKind::SegmentBreak });
        }
        for (std::size_t i = s.b; i < s.e; ++i) {
            const double t = base + s.f.c + s.f.T * s.f.k[i - s.b];
            r.retimedUs[i] = int64_t(std::llround(t0 + t));
            res[i] = double(arrivalUs[i]) - (t0 + t);
        }
        prevEnd = base + s.f.c + s.f.T * s.f.k[s.e - s.b - 1];
    }
    std::sort(r.losses.begin(), r.losses.end(),
              [](const RetimeLoss &x, const RetimeLoss &y) { return x.index < y.index; });
    for (const RetimeLoss &l : r.losses) { r.lostSamples += l.samples; ++r.lossEvents; }
    for (std::size_t i = 1; i < n; ++i)   // rounding can only tie; keep the series strictly ordered
        if (r.retimedUs[i] <= r.retimedUs[i - 1]) r.retimedUs[i] = r.retimedUs[i - 1] + 1;

    r.residualP50Us = detail::percentile(res, 50.0);
    r.residualP95Us = detail::percentile(res, 95.0);
    r.residualMaxUs = *std::max_element(res.begin(), res.end());
    if (!(r.periodUs > 0.0) || !std::isfinite(r.periodUs)) {
        r.retimedUs = arrivalUs;
        r.why = "no usable period";
        return r;
    }
    if (r.residualP95Us > cfg.maxResidualP95Us) {
        r.retimedUs = arrivalUs;
        r.why = "arrivals do not follow one sample clock";
        return r;
    }
    r.applied = true;
    return r;
}

} // namespace pinpoint::imu_retime
