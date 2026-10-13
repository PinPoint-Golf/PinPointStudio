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

#pragma once

// Kinematic-sequence reduction — pure, header-only, no Qt-GUI.
//
// TWO LAYERS, and the second is the one the product reads:
//
//   1. The ORIGINAL peak-ordering over arbitrary speed series (SeqSeries → SeqNode) — kept as it
//      was, tested as it was. It emphasises ORDER and GAPS, not curves.
//
//   2. The KINEMATIC SEQUENCE proper (KsNode → KinematicSequence, docs/design/
//      kinematic_sequence_design.md §3.2): four named segments — pelvis, thorax, lead arm, club —
//      each a node with a peak instant, a peak angular speed, a σ on BOTH, and the route that
//      produced it. resolveKinematicSequence() turns the nodes into the one thing the golfer is
//      told: the order, and whether the data can actually support that order.
//
// THE VERDICT IS WITHHELD INSIDE THE σ. Cheetham (2008) puts the professional pelvis and thorax
// peaks 19 ms apart. A node carrying ±30 ms cannot be ordered against a neighbour 19 ms away, and a
// sequence that picked one anyway would be reporting the noise's opinion. So `orderResolved` is
// true only when every adjacent gap exceeds k·sqrt(σₙ² + σₙ₊₁²), and `verdict` reads "unresolved"
// otherwise — whichever route produced the nodes. An IMU makes the σ small enough to resolve; a
// single camera often will not, and saying so is the product's answer, not a failure of it.
//
// A NODE THAT COULD NOT BE PLACED IS STILL A NODE. `placed = false` means the route produced a
// curve but its timing σ was wider than the placement threshold (segment_rates.h). The strip greys
// it; the order skips it; nothing pretends it was measured. Dropping it would hide the one thing
// the reader needs — that this route cannot see this segment.

#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace pinpoint::analysis {

// ── Layer 1: peak ordering over named speed series ─────────────────────────────────────────────

// One named speed series: parallel time (µs, ascending) + value arrays.
struct SeqSeries {
    QString              key;
    std::vector<int64_t> tUs;
    std::vector<double>  value;
};

// One node in the resolved sequence: the peak of a series and where it sits.
struct SeqNode {
    QString key;
    int64_t tPeakUs = 0;      // time of the peak (µs, absolute grid domain)
    double  peak    = 0.0;    // the peak value (signed; magnitude drives selection)
    double  gapMs   = 0.0;    // ms since the previous node's peak (0 for the first)
    int     order   = 0;      // 0-based position in the ordered chain
};

// Peak-value index of a series: the earliest sample of largest magnitude. Returns
// -1 for an empty series. Ties resolve to the earliest occurrence (strict >).
inline int seqPeakIndex(const SeqSeries &s)
{
    const int n = int(std::min(s.tUs.size(), s.value.size()));
    if (n == 0) return -1;
    int    best  = 0;
    double bestM = std::abs(s.value[0]);
    for (int i = 1; i < n; ++i) {
        const double m = std::abs(s.value[i]);
        if (m > bestM) { bestM = m; best = i; }
    }
    return best;
}

// Ordered peak-speed events across `series`, sorted ascending by peak time, with
// `gapMs` = the delta from the previous node's peak. Empty series are dropped, so a
// partially-instrumented capture yields the measurable nodes only — never a fake
// one. A stable sort keeps input order for exact peak-time ties.
inline std::vector<SeqNode> kinematicSequenceNodes(const std::vector<SeqSeries> &series)
{
    std::vector<SeqNode> nodes;
    nodes.reserve(series.size());

    for (const SeqSeries &s : series) {
        const int pi = seqPeakIndex(s);
        if (pi < 0) continue;                 // no data → not a node (degrade, don't fake)
        SeqNode n;
        n.key     = s.key;
        n.tPeakUs = s.tUs[pi];
        n.peak    = s.value[pi];
        nodes.push_back(n);
    }

    std::stable_sort(nodes.begin(), nodes.end(),
                     [](const SeqNode &a, const SeqNode &b) { return a.tPeakUs < b.tPeakUs; });

    for (size_t i = 0; i < nodes.size(); ++i) {
        nodes[i].order = int(i);
        nodes[i].gapMs = (i == 0) ? 0.0
                                  : double(nodes[i].tPeakUs - nodes[i - 1].tPeakUs) / 1000.0;
    }
    return nodes;
}

// ── Layer 2: the kinematic sequence proper ─────────────────────────────────────────────────────

// The four segments, in proximal→distal order. The enum order IS the canonical sequence.
enum class SeqSegment { Pelvis = 0, Thorax = 1, LeadArm = 2, Club = 3 };

inline const char *seqSegmentKey(SeqSegment s)
{
    switch (s) {
    case SeqSegment::Pelvis:  return "pelvis";
    case SeqSegment::Thorax:  return "thorax";
    case SeqSegment::LeadArm: return "leadArm";
    case SeqSegment::Club:    return "club";
    }
    return "";
}

// The angular-speed series each node is read from (the catalogue keys).
inline const char *seqSegmentSeriesKey(SeqSegment s)
{
    switch (s) {
    case SeqSegment::Pelvis:  return "pelvisAngularSpeed";
    case SeqSegment::Thorax:  return "thoraxAngularSpeed";
    case SeqSegment::LeadArm: return "leadArmAngularSpeed";
    case SeqSegment::Club:    return "clubAngularSpeed";
    }
    return "";
}

inline bool seqSegmentFromKey(const QString &k, SeqSegment &out)
{
    for (SeqSegment s : { SeqSegment::Pelvis, SeqSegment::Thorax, SeqSegment::LeadArm, SeqSegment::Club })
        if (k == QLatin1String(seqSegmentKey(s))) { out = s; return true; }
    return false;
}

// One segment's node. `placed` false ⇒ the route produced a rate curve but could not locate its
// peak to within the placement threshold; the timing fields then carry the attempt (for the trace)
// and the strip shows the chip greyed.
struct KsNode {
    SeqSegment segment        = SeqSegment::Pelvis;
    bool       placed         = false;
    int64_t    tPeakUs        = 0;      // absolute, shared timeline
    double     beforeImpactMs = 0.0;    // impact − tPeak; positive = peaked before the ball
    double     peakDps        = 0.0;    // signed, in the segment's convention (design §3.1)
    double     tSigmaMs       = 0.0;    // 1σ on the peak instant
    double     peakSigmaDps   = 0.0;    // 1σ on the peak value
    QString    routeId;                 // "pelvisImu" | "faceOn" | "faceOnClub" | "clubSensorFused" | …
    bool       direct         = false;  // the route's quality: Direct (true) or Estimated (false)
    // A BOUND INSTEAD OF A NODE. A face-on span sees a segment's rate only away from square
    // (sensitivity ∝ sin θ); when the highest rate in sight sat where the view went blind, the
    // peak is somewhere the route could not see, and the node is not placed. What it can still
    // say: the peak came no EARLIER than this many ms before impact (the rate was still rising at
    // the edge of sight), or no LATER (it was still falling when sight returned). NaN = no bound.
    double     peakNoEarlierThanMs = std::numeric_limits<double>::quiet_NaN();
    double     peakNoLaterThanMs   = std::numeric_limits<double>::quiet_NaN();
    bool bounded() const { return std::isfinite(peakNoEarlierThanMs) || std::isfinite(peakNoLaterThanMs); }
};

struct KinematicSequence {
    bool                    valid = false;      // at least one node was produced
    int64_t                 impactUs = 0;
    std::vector<KsNode>     nodes;              // in SeqSegment order; only produced segments
    std::vector<SeqSegment> order;              // PLACED nodes, ascending tPeak
    std::vector<double>     gapsMs;             // between adjacent placed nodes (order.size() − 1)
    std::vector<double>     gainsDps;           // peak(n+1) − peak(n) along `order`
    bool                    orderResolved = false;
    QString                 verdict;            // unresolved | partial | proximalToDistal | armBeforeThorax | other
    int                     pelvisDecelerates = -1;   // −1 unknown, 0 no, 1 yes
    QString                 routeSummary;       // direct | mixed | estimated | ""

    const KsNode *node(SeqSegment s) const
    {
        for (const KsNode &n : nodes)
            if (n.segment == s) return &n;
        return nullptr;
    }
};

// Turn produced nodes into the sequence. `sigmaK` scales the resolution test (design §3.2: k = 1).
//
// verdict:
//   unresolved        — fewer than two placed nodes, or an adjacent gap inside its combined σ
//   proximalToDistal  — all four placed, in pelvis → thorax → arm → club order
//   armBeforeThorax   — thorax and arm both placed, arm first (Cheetham's amateur signature),
//                       whatever the others did
//   other             — all four placed, some other order
//   partial           — fewer than four placed, and those placed are in canonical order
inline KinematicSequence resolveKinematicSequence(std::vector<KsNode> nodes, int64_t impactUs,
                                                  double sigmaK = 1.0)
{
    KinematicSequence ks;
    ks.impactUs = impactUs;
    std::stable_sort(nodes.begin(), nodes.end(),
                     [](const KsNode &a, const KsNode &b) { return int(a.segment) < int(b.segment); });
    for (KsNode &n : nodes)
        n.beforeImpactMs = double(impactUs - n.tPeakUs) / 1000.0;
    ks.nodes = std::move(nodes);
    ks.valid = !ks.nodes.empty();
    if (!ks.valid) return ks;

    std::vector<const KsNode *> placed;
    int direct = 0, estimated = 0;
    for (const KsNode &n : ks.nodes) {
        if (!n.placed) continue;
        placed.push_back(&n);
        (n.direct ? direct : estimated)++;
    }
    ks.routeSummary = placed.empty() ? QString()
                    : (estimated == 0 ? QStringLiteral("direct")
                    : (direct == 0    ? QStringLiteral("estimated")
                                      : QStringLiteral("mixed")));
    std::stable_sort(placed.begin(), placed.end(),
                     [](const KsNode *a, const KsNode *b) { return a->tPeakUs < b->tPeakUs; });
    for (const KsNode *n : placed) ks.order.push_back(n->segment);

    bool resolved = placed.size() >= 2;
    for (size_t i = 1; i < placed.size(); ++i) {
        const double gap = double(placed[i]->tPeakUs - placed[i - 1]->tPeakUs) / 1000.0;
        ks.gapsMs.push_back(gap);
        ks.gainsDps.push_back(placed[i]->peakDps - placed[i - 1]->peakDps);
        const double s = std::hypot(placed[i]->tSigmaMs, placed[i - 1]->tSigmaMs);
        if (!(gap > sigmaK * s)) resolved = false;
    }
    ks.orderResolved = resolved;

    if (const KsNode *p = ks.node(SeqSegment::Pelvis); p && p->placed)
        ks.pelvisDecelerates = (p->beforeImpactMs > p->tSigmaMs) ? 1 : 0;

    if (!resolved) { ks.verdict = QStringLiteral("unresolved"); return ks; }

    const auto indexOf = [&](SeqSegment s) -> int {
        for (size_t i = 0; i < ks.order.size(); ++i)
            if (ks.order[i] == s) return int(i);
        return -1;
    };
    const int iT = indexOf(SeqSegment::Thorax), iA = indexOf(SeqSegment::LeadArm);
    bool canonical = true;
    for (size_t i = 1; i < ks.order.size(); ++i)
        if (int(ks.order[i]) < int(ks.order[i - 1])) canonical = false;

    if (iT >= 0 && iA >= 0 && iA < iT)      ks.verdict = QStringLiteral("armBeforeThorax");
    else if (ks.order.size() == 4)          ks.verdict = canonical ? QStringLiteral("proximalToDistal")
                                                                   : QStringLiteral("other");
    else                                    ks.verdict = canonical ? QStringLiteral("partial")
                                                                   : QStringLiteral("other");
    return ks;
}

} // namespace pinpoint::analysis
