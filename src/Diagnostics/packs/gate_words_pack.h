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

// The issue vocabulary and `LoadResult<T>` — the only things this registry shares with the packs.
// pack_io.h rather than characteristic_pack.h, because nothing here reads a characteristic.
#include "../engine/pack_io.h"

#include <QByteArray>
#include <QString>

#include <vector>

// The gate words registry — WHAT WE COULD NOT CHECK, IN A GOLFER'S WORDS (session_report_design.md
// §9.3 item 5, §10 rule 7).
//
// The gate catalogue's own claim words ("club peak drawn, not judged", "swing plane not one plane")
// are written for an analyst; a client report says the same thing once, on its back page, in these.
// They used to be compiled into the app (src/Coaching/report_gate_words.h), which meant a studio could
// not read, correct or add to the words its own reports print, and a gate added to the catalogue got no
// golfer wording at all — the report simply left that line out. Now they are content, beside the drills,
// and the Diagnostic Model's Gates page shows and edits them next to the gate they belong to (#60).
//
// TWO LISTS, because a line on the back page is built from two parts:
//   gates      one CLAUSE per catalogue id — per-swing gates and engine rules alike — written WITHOUT
//              ITS SUBJECT. One gate (sequence.sightedBand) refuses several readings, so the clause is
//              written to follow the reading's name: "when your hips turned fastest: the face-on camera
//              cannot time a turn while you are square to it…"
//   subjects   the reading a gate acted on (a GateRecord subject: "ks.pelvis", "track.club", "shot"), in
//              golfer words. A METRIC subject is not here: the caller uses that measure's `golferName`,
//              which the pack already carries, so a second copy here could only drift from it
//
// Both are lower case and carry no digits (gateWordsProblem() below): the clause sits mid-sentence after
// a colon, and a number in it would be a threshold the golfer has no way to judge. A gate with no clause
// returns empty, and the report then leaves that line out rather than printing the analyst's words — an
// internal phrase on a client document is worse than a gap the technical appendix fills.
//
// THE CATALOGUE IS NOT CONSULTED HERE. Which gate ids exist is the gate catalogue's (Metrics/
// gate_catalogue.h), and this layer does not link it, for the same reason characteristic_pack.h never
// looks a metric up. core_pack_test holds the SHIPPED file to the catalogue in both directions — every
// catalogued id has a clause, every clause is a catalogued id — which is the only place that question is
// answerable and the only place it matters; a user's override of a gate a later build retires is harmless.
//
// Same shape as the drill registry, and for the same reasons — see drill_pack.h and screen_pack.h,
// including why this is a flat set with a user layer merged by id rather than a provider hierarchy.

namespace pinpoint::analysis {

// Refused above this, never partially read — the same contract and the same reasoning as
// kScreenSchemaVersion, which see.
inline constexpr int kGateWordSchemaVersion = 1;

// One entry of either list. `id` is a catalogue gate id ("sequence.sightedBand", "rule.diag.noCorridor")
// in `gates`, and a gate subject ("ks.pelvis") in `subjects`.
struct GateWord {
    QString id;
    QString golfer;   // the clause, or the subject's words
};

struct GateWordSet {
    QString               id;
    QString               version;
    int                   schemaVersion = kGateWordSchemaVersion;
    QString               sourceLabel;
    bool                  readOnly = false;
    std::vector<GateWord> gates;
    std::vector<GateWord> subjects;

    // Null when the id is not in that list.
    const GateWord *gate(const QString &gateId) const;
    const GateWord *subject(const QString &subjectId) const;
};

// The clause for a gate or rule id, or the words for a subject; empty when the set does not name it —
// which is what the report reads as "leave this line out" and "use the measure's golferName".
QString gateWordsFor(const GateWordSet &set, const QString &gateId);
QString subjectWordsFor(const GateWordSet &set, const QString &subjectId);

// What is wrong with one clause or one subject's words, as a short phrase that reads after "has" — the
// style of the pack loader's problems ("more than 60 characters", "no capital first letter"):
//   "no words"                the text is empty or white space
//   "a capital first letter"  it starts with a capital; it follows a colon mid-sentence
//   "a number"                it carries a digit
// Empty when the text is clean. The FIRST problem only, in that order, as the loader's do.
QString gateWordsProblem(const QString &text);

// The whole message for that problem, in the loader's voice, naming the id and quoting the text:
// what the Diagnostic Model shows under the field while the user types, and what validateGateWordSet()
// reports. Empty when `problem` is. `id` is a gate or a subject id; the message reads for either.
QString gateWordsMessage(const QString &id, const QString &text, const QString &problem);

// LOAD-TIME ERRORS: parse (not JSON) · schemaTooNew (from a newer build — kGateWordSchemaVersion)
// ERRORS: duplicateId (two entries of ONE list share an id, or an entry has none — the lookup would
//         answer with whichever came first)
// WARNINGS: gateWordsWording — an entry gateWordsProblem() refuses. A warning, as golferPhraseWording
//         is, so a user's layer still loads: the report prints a clause with a capital in it, or (empty)
//         leaves the line out, and neither is worth taking the user's whole layer down over.
//         core_pack_test holds the shipped file to zero.
ValidationReport validateGateWordSet(const GateWordSet &set);

using GateWordLoadResult = LoadResult<GateWordSet>;

GateWordLoadResult loadGateWordSet(const QByteArray &json, const QString &sourceLabel);
QByteArray         saveGateWordSet(const GateWordSet &set);

// The ASSEMBLED set: core, with the user layer's entries replacing core's by id (each list on its own)
// and any id core does not carry appended. What the report reads. Cached; resetSharedGateWordSet()
// drops the cache so the next read sees a saved edit — the Diagnostic Model's save() calls it, as it
// calls resetSharedDrillSet(), and the report builder calls it before every make, as it re-reads the
// pack. Both on the UI thread: a reset frees the set, so no reference to it may be held across one,
// and nothing may read it from a worker.
const GateWordSet &sharedGateWordSet();
void               resetSharedGateWordSet();

// The layers apart, for the editor. See screen_pack.h.
//
// coreGateWordSet() reads `:/diagnostics/gate_words.json`, or the file PINPOINT_CORE_GATE_WORDS names
// (a standalone test binary has no Qt resource), and is cached for the life of the process — the
// shipped words do not change under it. The user layer is `gate_words.json` in diagnosticsDataDir()
// and is read fresh on every call; it holds only what the user changed or added, so an override of
// one clause is a file with one entry in it.
const GateWordSet &coreGateWordSet();
GateWordSet        loadUserGateWordSet();
QString            userGateWordSetPath();
bool               saveUserGateWordSet(const GateWordSet &set, QString *whyNot = nullptr);

} // namespace pinpoint::analysis
