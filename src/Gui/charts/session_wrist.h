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

// ONE SHOT'S WRIST CHECKPOINT PROFILE, as the Charts panel carries it (build/wrist-tab/CONTRACT.md I3).
//
// The wrist engine (src/Wrist/wrist_assessment_engine.h) stays THE CELL GRADER: it samples each wrist
// DOF at the eight checkpoints (wristCheckpoints(), the adapter's own table — never a phase list of
// ours), takes the change from address, and grades each cell against the norm band of the resolved
// archetype (NormBandProvider, the diagnostics norms). Its RULES and its score do not reach the panel:
// findings and the score are the diagnostics conditions' (CONTRACT decision 3). `engineScore` is kept
// only so the morning's before/after table can show the two side by side.
//
// INPUT IS THE ShotSeries ITSELF — the metrics and phases SessionSeriesModel already copied out — so a
// change of archetype re-grades every shot from memory, and nothing of the swing document is held for
// it. The angle source is built by the adapter's own buildWristAngleSource (same DOF keys, same
// last-key-wins rule, same right-handed producer convention, same gimbal proxy), so this profile is the
// one WristDiagnosticsModel draws for the same swing; the only difference is honesty about a missing
// sample: a non-finite value is a gap (unavailable), never a 0.
//
// ── THE PAYLOAD (QVariantMap; EMPTY when the shot has no wrist DOF) ─────────────────────────────
//  { archetype: int (0 neutral | 1 bowed | 2 cupped — the RESOLVED band model),
//    archetypeName: "Neutral" | "Bowed" | "Cupped",
//    archetypeMode: int (-1 auto | 0..2 — what it was asked to grade against),
//    resemblance: { neutral, bowed, cupped } (ints, %; the analysis' score.resemblance) | {},
//    checkpoints: [ 8 × { tag: "P1".."P8", phase: int (the Phase enum it samples), note,
//                         tUs: qint64 (window-relative; ABSENT when the swing has no such phase),
//                         available: bool } ],
//    dofs: [ { dof: "leadWristRadUln" | "leadWristFlexExt" | "leadForearmRot" | "trailWristFlexExt" |
//                   "leadElbowFlex",
//              metricKey: the series key it was read from ("hm.leadWristFlexExt"),
//              name, sub, axisName, source ("IMU" | "pose" | …), confidence (0..1),
//              poleTop, poleBottom, hasStrip: bool (false = a numbers-only row: lead elbow),
//              cells: [ 8 × { deltaDeg: double (ABSENT when not measured), available: bool,
//                             rag: "green" | "amber" | "red" | "ref" | "none",
//                             banded: bool, bandLo, bandHi (null when !banded) } ] } ]
//            in display order: hinge, bow / cup, forearm roll, trail wrist, lead elbow; only the
//            DOFs the shot carries,
//    engineScore: int }
//
// Pure apart from the shared norm set (sharedNormProvider, read-only once built): no QObject, no
// files, no threads of its own. Safe on any thread once warmWristBands() has run on one.

#include "session_series_types.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace pinpoint::charts {

// The profile of `shot` graded against `archetypeMode` (-1 = Auto: the engine resolves the swing's
// own style). `resemblance` is carried through as given ({} = none). Empty when no wrist DOF.
QVariantMap shotWristPayload(const ShotSeries &shot, int archetypeMode, const QVariantMap &resemblance);

// The same payload re-graded against another archetype, or `payload` itself when it already is.
// For a payload that is empty, returns it unchanged.
QVariantMap regradeWristPayload(const ShotSeries &shot, const QVariantMap &payload, int archetypeMode);

// The analysis block's resemblance index (analysis.score.resemblance), normalised to
// { neutral, bowed, cupped } ints; {} when the analysis carries none (an adherence score, a /2 doc).
QVariantMap wristResemblanceOf(const QJsonObject &analysis);

// "Neutral" | "Bowed" | "Cupped" for 0..2 (anything else reads Neutral, as the engine resolves it).
QString wristArchetypeName(int archetype);

// Build the shared norm set on the calling thread. SessionSeriesModel calls it on construction (the GUI
// thread) so the first worker read never races another first read for the process-wide slot.
void warmWristBands();

} // namespace pinpoint::charts
