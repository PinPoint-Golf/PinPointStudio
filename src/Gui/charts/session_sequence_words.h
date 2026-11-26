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

// THE KINEMATIC SEQUENCE IN WORDS: why a node is not placed, the shot's peak times as a sentence, and the gates that
// fired on a segment, opened. Split out of session_sequence.h, whose rules stay pure numbers (platform refactor
// B1.15b); these are the Charts' and the trust view's words over its SegmentPeak.

#include "../../Diagnostics/derived/series/session_sequence.h"
#include "charts_words.h"

#include <QString>
#include <QVariantList>

namespace pinpoint::charts {

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

// THE SHOT'S PEAK TIMES IN WORDS, each against impact by the one rule (Vocabulary/Common/figure_format.h impactTime: "at
// impact" within 2 ms): "The club peaked 49 ms before impact, the pelvis 20 ms after impact." — the pelvis' OWN
// time, never the club→pelvis gap; "The club peaked at impact." Empty when neither is placed; figures are runs.
words::Sentence peakTimesSentence(const SegmentPeak &club, const SegmentPeak &pelvis);
// The chest's probable peak, when, appended to `s`: "40 ± 8 ms before impact", or "at impact (± 8 ms)" — the ±
// kept either way (never withheld).
void appendProbableWhen(words::Sentence &s, double probableMs, double sigmaMs);
// A curve's peak, when, appended to `s`: "Clubhead speed peaks 12 ms before impact", "… peaks at impact".
void appendPeaksWhen(words::Sentence &s, const QString &label, double shotMs);

// THE GATES THAT FIRED ON ONE SEGMENT, opened (§7.1): one entry per record, the refusing one first —
// { gate, effect, title, evidence (the filled sentence), explain, tuning ("20° (sequence.sightedTurnDeg)"),
// lift }. Empty when the shot carries no records.
QVariantList segmentGates(const SegmentPeak &p, int segment);

} // namespace pinpoint::charts
