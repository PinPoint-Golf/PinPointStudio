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
    double shotMs = kNaN;               // peak time, ms from impact on the shot's clock (+ after)
    double tSigmaMs = kNaN;
    double peakDps = kNaN, peakSigmaDps = kNaN;
    int    rank = 0;                    // among the placed, 1 = first; 0 = not placed
    bool   late = false;                // D4
};

struct ShotSequence {
    int ordinal = 0;
    std::array<SegmentPeak, kSequenceSegments> segments;
    QStringList order;                  // the sequence's own order (placed segment ids, by time)
    int  placedCount = 0;
    bool anyLate = false;
};

struct SegmentSummary {
    int    placed = 0;                  // shots it was placed on
    double medianMs = kNaN, q1Ms = kNaN, q3Ms = kNaN;   // of the placed peak times (shot clocks)
    int    afterImpact = 0;             // placed peaks after impact
    int    late = 0;                    // shots it was late on
};

struct SessionSequence {
    std::vector<ShotSequence> shots;    // parallel to session.shots
    std::array<SegmentSummary, kSequenceSegments> segments;
    QString commonOrder;                // the most common order, ids joined by ","
    int     commonOrderCount = 0;
};

ShotSequence    shotSequence(const ShotSeries &shot);
SessionSequence sessionSequence(const SessionSnapshot &session);

} // namespace pinpoint::charts
