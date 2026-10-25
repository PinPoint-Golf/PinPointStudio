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

// THE KINEMATIC SEQUENCE ACROSS THE SESSION (design §2.2: "an order barcode (rank per shot, late
// ranks marked) and a peak-time dot plot on one ms axis"; variant_a SeqSession.qml, variant_b
// SeqTiming.qml).
//
// Read from each shot's kinematicSequence map (kinematic_sequence_json.h's shape) the way
// ChartMetrics::sequenceTable reads it: a node with tPeakUs == 0 has no peak and is absent; a node is
// PLACED when its `placed` flag says so or the sequence's `order` lists it. ChartMetrics' parsing is
// inline in sequenceTable (no free function to call), and step 2 is about to change that function's
// outOfTurn, so the few lines are read here rather than extracted from under it.
//
// TIMES ARE ON EACH SHOT'S OWN CLOCK: ms from impact, + after (−beforeImpactMs). They are never
// warped — a peak time is a reading, and the dot plot's one axis is "ms from impact".
//
// LATE (D4): of each pair of placed segments that peaked in the wrong order, only the one that should
// have come FIRST and came later is late (shot 8: the pelvis, after the arm and the club). Rank = the
// order a placed segment peaked in among the placed ones (1 = first). Neither is ChartMetrics'
// outOfTurn, which marks both ends of an inverted pair.

#include "session_series_types.h"

#include <QString>
#include <QStringList>

#include <array>
#include <vector>

namespace pinpoint::charts {

// The segments in the order they should peak: pelvis, thorax (chest), lead arm, club.
constexpr int kSequenceSegments = 4;
const std::array<QString, kSequenceSegments> &sequenceSegmentIds();     // "pelvis", "thorax", "leadArm", "club"
QString sequenceSeriesKey(int segment);                                  // "pelvisAngularSpeed", …

struct SegmentPeak {
    bool   present = false;             // the route found a peak
    bool   placed = false;
    bool   risingAtImpact = false;      // peakNoEarlierThanMs ≤ 0: not peaked by the end of its domain
    // the bound an unplaced node kept (out of the camera's sight), on the shot's clock (+ after
    // impact): the peak came after `afterMs` / before `beforeMs`; NaN = no such bound
    double afterMs = kNaN, beforeMs = kNaN;
    double shotMs = kNaN;               // peak time, ms from impact on the shot's clock (+ after)
    double tSigmaMs = kNaN;
    double peakDps = kNaN, peakSigmaDps = kNaN;
    int    rank = 0;                    // among the timed (placed or probable), 1 = first; 0 = neither
    bool   late = false;                // D4
    // A PROBABLE PEAK (session_bridge.h): the Session view's estimate for a node the analysis did not
    // place, read off a fit bridging a short gap in its curve — never the analysis' own. Set only by
    // applyBridges(); `placed` and `shotMs` keep what the analysis said.
    bool   probable = false;
    double probableMs = kNaN, probableSigmaMs = kNaN;       // ms from impact (+ after), ±σ
    double probableDps = kNaN, probableSigmaDps = kNaN;
    bool   timed() const { return placed || probable; }
    double timeMs() const { return placed ? shotMs : probable ? probableMs : kNaN; }
    // FROM THE GATE RECORDS (trust_and_gates_design.md §6), when the analysis wrote them:
    bool        reasonsRecorded = false;    // the shot carries analysis.gates (gatesVersion > 0)
    QVariantMap refusal;                    // the record that refused / bounded the node (the one that says the most)
    QVariantList records;                   // every record against this node, in the order written
    // the stretch the route could not see (sequence.sightedBand / validityHole), ms from impact (+ after);
    // NaN = no band; `blindToMs` NaN with a finite `blindFromMs` = blind to the end of the curve
    double  blindFromMs = kNaN, blindToMs = kNaN;
    QString blindGate;
};

struct ShotSequence {
    int ordinal = 0;
    std::array<SegmentPeak, kSequenceSegments> segments;
    QStringList order;                  // the sequence's own order (placed segment ids, by time)
    int  placedCount = 0;
    int  timedCount = 0;                // placed or probable
    bool anyLate = false;
};

struct SegmentSummary {
    int    placed = 0;                  // shots it was placed on
    int    probable = 0;                // shots it has a probable peak on (session_bridge.h)
    double medianMs = kNaN, q1Ms = kNaN, q3Ms = kNaN;   // of the timed peak times (shot clocks)
    int    afterImpact = 0;             // timed peaks after impact
    int    late = 0;                    // shots it was late on
};

struct SessionSequence {
    std::vector<ShotSequence> shots;    // parallel to session.shots
    std::array<SegmentSummary, kSequenceSegments> segments;
    QString commonOrder;                // the most common order, ids joined by ","
    int     commonOrderCount = 0;
};

// WHY A NODE IS NOT PLACED, said as the gate that fired, never a field that merely happens to be there.
// From the node's gate record when the analysis wrote one (§9 stage 3: the catalogue's words, the
// record's bound). A shot analysed before records existed falls back to what its fields say (stage 0:
// the bound, still rising at the end) and otherwise says the reason was not recorded.
struct UnplacedWhy {
    QString figure;     // for a figure slot: "after −129" (with unit "ms"), "rising", or "-"
    QString unit;       // "ms" when `figure` is a time
    QString shortText;  // "out of sight" · "still rising" · "timing uncertain" · "not placed"
    QString sentence;   // "peaked after −129 ms, out of the camera's sight" …
};
UnplacedWhy unplacedWhy(const SegmentPeak &p);

// THE GATES THAT FIRED ON ONE SEGMENT, opened (§7.1): one entry per record, the refusing one first —
// { gate, effect, title, evidence (the filled sentence), explain, tuning ("20° (sequence.sightedTurnDeg)"),
// lift }. Empty when the shot carries no records.
QVariantList segmentGates(const SegmentPeak &p, int segment);

ShotSequence    shotSequence(const ShotSeries &shot);
SessionSequence sessionSequence(const SessionSnapshot &session);

// The two passes sessionSequence() runs after reading the nodes, exposed so a probable peak added
// later (applyBridges) is ranked and summarised by the same rules: rank, late and the order over the
// TIMED segments of one shot; then the session's per-segment counts, quartiles and commonest order.
void rankSequence(ShotSequence &shot);
void summariseSequence(SessionSequence &session);

} // namespace pinpoint::charts
