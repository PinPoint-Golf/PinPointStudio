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
// THE PROFILE IS THE FINDINGS' (refactor step 3.8-ii; DEC-123, J-58): fireFindings() runs the wrist engine over the
// shot's wrist series (Wrist/wrist_profile_assess.h, the one rule) and the shot's row keeps the result
// (ChartShotFindings::wrist, diagnostics.json schema 16): the Δ from address at each checkpoint, and each cell's band and
// RAG under each style with the style Auto resolved. This file GRADES NOTHING: it lays that profile out as the payload
// below — picking the cells of the style asked for — with the DOFs' words (wrist_dof_metadata.h) and the checkpoint
// times from the shot's own phases (wristCheckpoints(), on the series' window clock).
//
// A SHOT WITH WRIST READINGS AND NO PROFILE YET (its findings not written, a ledger older than schema 16, a newer build's
// file this one cannot add to, a picked shot not in the snapshot yet) is never drawn empty and never as "no wrist
// metrics": its payload is PENDING — `pending` true and `pendingWhy` the reason — with its DOFs and checkpoints and no
// cell measured, and the Wrist chart says "not graded yet" in the cells (J-58: show, never hide).
//
// The ★ reference swing of ANOTHER session keeps today's read (J-58, J-ii3): its own document, its Δ only, through the
// same rule with no band (wristDeltasOnly) — nothing is graded for it.
//
// ── THE PAYLOAD (QVariantMap; EMPTY when the shot has no wrist DOF) ─────────────────────────────
//  { archetype: int (0 neutral | 1 bowed | 2 cupped — the RESOLVED band model),
//    archetypeName: "Neutral" | "Bowed" | "Cupped",
//    archetypeMode: int (-1 auto | 0..2 — what it was asked to grade against),
//    resemblance: { neutral, bowed, cupped } (ints, %; the analysis' score.resemblance) | {},
//    pending: bool, pendingWhy: string ("" unless pending),
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
//            DOFs the shot carries }
//
// Pure: no QObject, no files, no threads of its own. Safe on any thread.

#include "session_series_types.h"
#include "../../Wrist/wrist_profile_assess.h"   // WristInput

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace pinpoint::charts {

// The payload of `shot` at `archetypeMode` (-1 = Auto: the style its findings resolved), from its findings' profile
// (shot.diagnostics.wrist). With wrist readings and no profile: the PENDING payload, `pendingWhy` its reason. Empty when
// the shot carries no wrist DOF.
QVariantMap shotWristPayload(const ShotSeries &shot, int archetypeMode, const QString &pendingWhy = QString());

// The ★ reference swing read from its own document (another session's): its Δ only, every cell unbanded (J-ii3).
QVariantMap referenceWristPayload(const ShotSeries &shot);

// The shot's series and phases as the profile's rule reads them (every metric in the swing's own order).
pinpoint::analysis::WristInput wristInputOfSeries(const ShotSeries &shot);

// The analysis block's resemblance index (analysis.score.resemblance), normalised to
// { neutral, bowed, cupped } ints; {} when the analysis carries none (an adherence score, a /2 doc).
QVariantMap wristResemblanceOf(const QJsonObject &analysis);

// "Neutral" | "Bowed" | "Cupped" for 0..2 (anything else reads Neutral, as the engine resolves it).
QString wristArchetypeName(int archetype);

} // namespace pinpoint::charts
