/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

// THE SESSION'S DIAGNOSIS, pure (refactor step 3.5-ii; build/refactor/step-3/BRIEF.md §4.4 step 7, §4.5; DEC-100).
//
// reduceSession() is what SessionDiagnostics posts to its session's lane when a batch of shots settles: values in,
// values out (R-CON-2), reading nothing but its argument. It reduces the session's kept findings (the rows as checked
// against the session's usual flight) and each shot's derived data to the session's diagnosis — the ledgers (tiers),
// the session-scope conditions, the pattern graph's links, the stage ratchet — and to its typed results
// (src/Coaching/session_results.h): the work-ons and evidence, the outcome evidence, the reasons and each shot's
// "because" (J-14 (b): the session's links and tiers name a shot's driver; the shot's own outcome carries none),
// the shot mix, per club the carry today, the factor mix and the baseline window (D7), and the usuals (G-10).
//
// THE SAME RULES AS SessionDiagnosticsModel::rebuild()/ensureOutcomes() TODAY, on the same free functions, so the
// parallel run (§4.9) can show the move onto the lane changes nothing — except where step 3.5-ii makes two rules one
// in the new owner only (run P's listed case 4): G-08, a reason's carry cost counts only against an EARNED usual
// (ClubBaselineEntry::earned, B-22), as the carry waterfall and carry today already do. The old model keeps its rule
// until its cut-over (3.7). G-09 changes no figure (the two counting rules are named apart: session_results.h).
//
// What it does NOT do: the panel's view state (the cards' hysteretic order, the fault profile's bias, the explanation's
// root set, the chains' rails, the coverage line), which are the thin adapter's (3.7); any golfer-facing sentence
// (R-DER-9).
//
// Rules ring (src/Diagnostics/engine, beside fire_findings): no reader, no file, no QObject.

#include "fire_findings.h"
#include "../../Coaching/diagnostic_ledger.h"
#include "../../Coaching/session_results.h"
#include "../../Coaching/shot_outcomes_pack.h"

#include <QDate>
#include <QHash>
#include <QString>

#include <memory>
#include <optional>
#include <vector>

namespace pinpoint::analysis {

// The reduction's own version (R-STAMP-3; C-05's other half is 3.6's golden). 1 (16 Nov 2026): refactor 3.5-ii.
inline constexpr int kSessionReductionVersion = 1;

struct SessionReductionInput {
    // The rule data the session's snapshot holds (R-CON-4): the pack, the outcome pack built from it.
    std::shared_ptr<const ICharacteristicPackProvider> pack;
    std::shared_ptr<const ShotOutcomePack>             outcomePack;
    LedgerOptions options;
    bool          withSigma = tuned::diagUncertainty::kEnabled;

    // The kept findings, CHECKED against the session's usual flight, in strike order (by swing number).
    std::vector<ShotRecord> shots;
    // Each shot's derived data as kept (layer 3: its outcome without the session's "because", its factors), and the
    // golfer's word it was derived with (its intent resolved against the run; its corrections), by swing number.
    QHash<int, ShotDerivedData>             derived;
    QHash<int, QString>                     intents;
    QHash<int, std::vector<ShotCorrection>> corrections;

    // The context the session hands down: its usuals (GL-25), the frozen club baseline, the flight table.
    QHash<QString, ClubFlightUsual>                  usuals;
    std::optional<QHash<QString, ClubBaselineEntry>> baseline;
    FlightNorms                                      flightNorms;

    // The golfer's session-level word (FILE-SESS-02, resolved by golfer_input_rules.h): the declared focus (the
    // moved-together split), the declared miss (a seed of the pattern graph), the screen answers.
    QString              focusConditionId;
    int                  focusFromShot = -1;
    QString              declaredMiss;
    QHash<QString, bool> screens;

    // The stage ratchet: the stage before this reduction, whether it was ever established, whether the session is
    // ended (the Session's status: its one writer, J-24). `replay`: no stage was held (a cold load or a regrade), so
    // the ratchet steps once per shot over the strike order, as the live session would have — the same steps the old
    // model's per-shot back-fill takes headless.
    Stage previousStage = Stage::Cold;
    bool  reachedEstablished = false;
    bool  closed = false;
    bool  replay = false;

    // The session, for its summary: its id as today's files key it (the FOLDER NAME: FIELD-LIST F1), its folder name
    // and day (the baseline window's place).
    QString sessionId;
    QString folderName;
    QDate   day;
};

struct SessionDiagnosis {
    std::vector<ConditionLedger> ledgers;      // per condition, the session scope's appended (rebuild's order)
    std::vector<LinkEvidence>    links;        // the pattern graph's graded links
    Stage                        stage = Stage::Cold;
    bool                         reachedEstablished = false;
    QStringList                  patterns;     // pattern-tier condition ids, sorted
    // Each shot's outcome composed WITH the session's evidence (the "because" joined), strike order: what a screen
    // shows, and what the reasons were tallied from.
    std::vector<ShotOutcome>     composed;
    SessionResults               results;
};

SessionDiagnosis reduceSession(const SessionReductionInput &in);

} // namespace pinpoint::analysis
