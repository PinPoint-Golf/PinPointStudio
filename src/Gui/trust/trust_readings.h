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

// THE READINGS A GATE TOUCHED, said once for every surface (docs/design/trust_and_gates_design.md §4.3,
// §7). The Trust panel (PpTrustPanel), the in-place fallback under a claim (PpTrustInline) and the
// marks on the Charts panel all read this, so the panel, the chart and the figure never disagree about
// why a reading was refused.
//
// One READING per subject a record names (a sequence node today; metrics, routes and findings as their
// producers start recording): its label, its TRUST LEVEL (§4.3, derived from the records, never
// authored), the short figure the claim shows ("after −129 ms"), and every gate behind it in the
// catalogue's words (Metrics/gate_catalogue.h).
//
//  READING = { subject ("ks.pelvis"), area ("Kinematic sequence"), label ("Pelvis peak"), key (the series key,
//              for the reading's hue), level ("bounded" | "notSeen" | "notJudged" | "estimated" | "measured"),
//              levelWord ("Bounded" | "Not seen" | "Drawn, not judged" | "Estimated" | "Measured"),
//              levelTip (the level's one-line meaning), figure ("after −129 ms" | "-"), shortText ("out of sight"),
//              gates: [GATE] }
//  GATE    = { gate, effect, effectWord, title, evidence (the filled sentence), explain, tuning
//              ("Threshold 20°"), lift ("Timed on swings with …"), primary (the record the claim shows) }

#include "../charts/session_series_types.h"

#include <QVariantList>
#include <QVariantMap>

namespace pinpoint::trust {

// The readings on one shot that a gate touched, in subject order (pelvis, chest, lead arm, club). Empty
// when the shot carries no records (analysed before records existed: see trustShot's `recorded`).
QVariantList trustReadings(const charts::ShotSeries &shot);

// One shot, for the panel: { recorded, readings: [READING], head (one sentence: what this shot could not
// measure), counts: { bounded, notSeen, notJudged } }.
QVariantMap trustShot(const charts::ShotSeries &shot);

// The session, for the panel: { n, recorded (shots carrying records), gates: [{ gate, title, area,
// subjects: [{ subject, label, shots }], shots (any subject), of (recorded) }], head }.
QVariantMap trustSession(const charts::SessionSnapshot &session);

// A subject as the reader names the reading ("ks.pelvis" → "Pelvis peak").
QString subjectLabel(const QString &subject);

} // namespace pinpoint::trust
