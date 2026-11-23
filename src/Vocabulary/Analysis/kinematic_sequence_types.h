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

// The kinematic sequence's RESULT (docs/design/kinematic_sequence_design.md §3.2): the four
// segments and their keys, one node per segment, and the resolved sequence. Plain value types the
// shot's analysis record holds (SwingAnalysis::kinematicSequence, swing_analysis.h), so they sit in
// the vocabulary; the reduction that resolves them (resolveKinematicSequence) and the layer-1 peak
// ordering stay in Kinematics/kinematic_sequence.h, which includes this. Moved out of
// kinematic_sequence.h unchanged (refactor 7.3, A-11).

#include "gate_record.h"   // GateRecord — why a node is what it is

#include <QString>

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace pinpoint::analysis {

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
    // DRAWN BUT NOT JUDGED. Non-empty = why the route's node must not carry a diagnostic verdict
    // (MetricSeries::withheld); the node is placed and charted with its σ as usual.
    QString    withheld;
    // A PROBABLE PEAK (gap_bridge.h; design §12.6). An UNPLACED node whose curve the route lost for a short gap
    // at impact, bridged by a Gaussian process: the peak the gap hides, with its ±, and the bridge itself (its
    // posterior mean and 1σ every ms across the gap). `placed` stays false — the order, gaps and verdict above
    // are the placed nodes' — but the node's readings are emitted from here, downgraded (sequence.gapBridged).
    // Times are ms from impact (+ after), so a reload needs no re-timing.
    bool                probable = false;
    double              probableMs = 0.0, probableSigmaMs = 0.0;          // peak instant, ms from impact, ±1σ
    double              probableDps = 0.0, probableSigmaDps = 0.0;        // peak value, ±1σ
    // HOW IT WAS READ (Mark, 25 Oct 2026: "always be reported so long as we warn the user"): "bridge" — the fit's
    // peak inside the gap; "rising" / "fell" — the fit still rising at the search's end / falling into the gap,
    // so this is the earliest / latest the peak can be; "seen" — no gap to bridge, the highest rate the camera
    // saw (sequence.chestIndicated). `bridge*` is empty for "seen".
    QString             probableHow = QStringLiteral("bridge");
    std::vector<double> bridgeMs, bridgeMean, bridgeSd;                   // the fit across the gap
    double              earlierMs = std::numeric_limits<double>::quiet_NaN();   // a taller measured value earlier
    double              earlierDps = std::numeric_limits<double>::quiet_NaN();  //   in the downswing (read as the measurement)
    // WHY THE NODE IS WHAT IT IS (trust_and_gates_design.md §4.2): every gate that fired on this
    // node — the sighted band (also when it only masked samples, so the chart can shade them), the σ
    // ceiling, the spike guard, the route switch, the club floor. Travels WITH the node so a rung
    // that withdraws its node withdraws its reasons too; buildSegmentRates flattens them into
    // SegmentRatesResult::gates. Not part of the kinematicSequence JSON — `analysis.gates` is.
    std::vector<GateRecord> gates;
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

} // namespace pinpoint::analysis
