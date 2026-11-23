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
// What it does NOT do: the panel's view state (the cards' hysteretic order, the explanation's
// root set, the chains' rails, the coverage line), which are the thin adapter's (3.7); any golfer-facing sentence
// (R-DER-9).
//
// Rules ring (src/Diagnostics/engine, beside fire_findings): no reader, no file, no QObject.

#include "fire_findings.h"
#include "../../Coaching/diagnostic_ledger.h"
#include "../../Coaching/sequence_checks.h"   // the session's sequence-consistency reading (SessionDiagnosis::sequence)
#include "../../Coaching/session_results.h"
#include "../../Coaching/shot_outcomes_pack.h"

#include <QCryptographicHash>
#include <QDate>
#include <QHash>
#include <QString>

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

namespace pinpoint::analysis {

// The reduction's own version (R-STAMP-3; C-05's other half is 3.6's golden). 1 (16 Nov 2026): refactor 3.5-ii.
// 2 (16 Nov 2026, refactor 3.6; Stage 0.5's follow-up): its input is each shot as the launch monitor board annotated it
// (Coaching/board_annotations.h; SessionDiagnostics::reductionInput) — the effective club, a left-out reading not read.
// 3 (16 Nov 2026, refactor 3.6, round 2): its results carried the shots as judged (withdrawn). 4 (round 3): they carry
// the compact per-shot facts (SessionResults version 3), and a shot the board re-labelled is composed under its new club
// (review CN-1).
inline constexpr int kSessionReductionVersion = 4;

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
    // THE WORK-ON ORDER OF EVERY SESSION PATTERN, UNCAPPED (refactor 5.5a; J-74 Q2): the work-on list's own reduction
    // with no cap — the cap cuts after the sort, so its first five are results.workOns' five, in their order. In memory
    // only, never persisted: the Charts Wrist sections order their findings by it.
    QStringList                  workOnOrder;
    // THE SESSION'S SEQUENCE-CONSISTENCY READING (refactor 5.6d; the Solution Architect, 23 Nov 2026, J-80/J-82): the
    // order share and the peak-timing spread over these rows (`shots`, `options`), worked out ONCE by this reduction;
    // the session-scope ledgers are read from it. In memory only, never persisted: worked out again with the rest of
    // the diagnosis whenever it is reduced (a load included). The report's session-level sentences read it.
    SessionSequenceConsistency   sequence;
};

SessionDiagnosis reduceSession(const SessionReductionInput &in);

// ── FIELD-LIST §1.8's `fingerprint`: WHAT A SESSION SUMMARY WAS WORKED OUT FROM, ONE RULE (refactor 3.7-e; round 2:
// R-STAMP-1, review R1-B2) ─────────────────────────────────────────────────────────────────────────────────────────────
// Every version the summary is produced under — today's keys' schema and rule, DEC-192's growth, the facts' rules
// (`factsRules`), the reduction's (kSessionReductionVersion), the club baseline's schema, the flight check's rule and
// thresholds (flightCheckRuleStamp, refactor 6.3a) — and every input both its writer
// (SessionDiagnostics) and the catch-up's scan (WorkOnsController) can compute: the rule data (`rules`:
// summaryRulesStamp — the row shape and grid versions, the pack, the norms, the grade policy, the disabled sets, the flight
// norms), the session's frozen club baseline (`baseline`: clubBaselineStamp), the athlete record's facts its norm cohort
// is read from (`cohort`: "dob|sex" as stored, DEC-171), and each swing folder of the session by name with its document's
// size and modification time (or none). Not in it: kOutcomeScopeRuleVersion — Home's shot filter, which no summary field
// is made under. Any of them moving makes the summary stale, and the catch-up writes it again (R-STAMP-2).
struct SummaryRules {
    QString rules;      // summaryRulesStamp over the rule data
    QString baseline;   // clubBaselineStamp of the session's frozen club baseline
    QString cohort;     // "dob|sex", the athlete record's, as stored
};
inline QString summaryFingerprintOf(const SummaryRules &in, std::vector<SummarySwing> swings)
{
    std::sort(swings.begin(), swings.end(), [](const SummarySwing &a, const SummarySwing &b) { return a.folder < b.folder; });
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(QByteArrayLiteral("summary2"));
    for (const int v : { kWorkOnSchemaVersion, kWorkOnRuleVersion, kSessionResultsVersion, kTendencyRuleVersion,
                         kThemeRuleVersion, kBoardAnnotationRuleVersion, kSessionReductionVersion, kClubBaselineSchemaVersion }) {
        h.addData(QByteArray::number(v));
        h.addData("|");
    }
    // The flight check's rule and thresholds (refactor 6.3a; R-STAMP-1): the rows the summary is reduced from are checked
    // against the flight. The table itself is in `rules` (summaryRulesStamp).
    h.addData(flightCheckRuleStamp().toUtf8());
    h.addData("|");
    for (const QString *t : { &in.rules, &in.baseline, &in.cohort }) {
        h.addData(t->toUtf8());
        h.addData("|");
    }
    for (const SummarySwing &s : swings) {
        h.addData(s.folder.toUtf8());
        h.addData(s.exists ? QByteArray::number(s.size) + ':' + QByteArray::number(s.mtimeMs) : QByteArrayLiteral("none"));
        h.addData("|");
    }
    return QString::fromLatin1(h.result().toHex().left(16));
}

} // namespace pinpoint::analysis
