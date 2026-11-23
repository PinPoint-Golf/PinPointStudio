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

// FIRING A SHOT'S FINDINGS — pure: values in, values out (refactor step 3.3; information_model.md
// M-SHOT-LAYERS layers 3–5, DEC-100, DEC-121, DEC-135; R-STAMP-1, R-STAMP-3; audit C-01/G-03 in part, C-05,
// D-05 in part).
//
// Two functions, and the one struct of inputs both read:
//   • fireFindings()      layers 4–5: the shot's readings (its phase grid and the few document facts below),
//                         assessed against the diagnostic graph (pack + norms, at the grade policy, for the
//                         cohort, with the club baseline) → the shot's findings: one row per condition, each
//                         sequence check's verdict, the placed peak times, the readings as recorded, the
//                         swing length the assessment was gated on, and the wrist checkpoint profile
//                         (refactor 3.8-ii; DEC-123: the wrist engine against the same norms, at the same
//                         grade policy, for each style; no cohort: the wrist bands are not cohort-qualified).
//   • deriveShotData()    layer 3: what the shot came to — its outcome (labels, carry against the usual) and
//                         its three factors (strike · direction · flight) — from its record, the golfer's word
//                         on it and the session's usuals. The rules are Coaching's (composeShotOutcome,
//                         shot_outcomes.h; shotFactors, shot_factors.h); this is the one place they are
//                         composed with the inputs below.
//
// NO QObject, no file or settings access, no reader or repository include, no global or singleton read: the
// caller reads the Recording (readLivePhaseGrid, the document's summary) and hands the values in. Today's
// caller is the firing read (SwingStoreFiringReader, then fireFindings) and the factor compositions; refactor 3.5's firing job calls
// the same functions on a worker with a FiringSnapshot (build/refactor/step-3/BRIEF.md §4.4).
//
// THE INPUTS ARE THE STAMP'S (R-STAMP-1). FiringInputs names every input a shot's findings and derived data
// depend on besides its own readings; firingStamp() fingerprints it. Item 3.3 defines and tests the stamp but
// does NOT write it: diagnostics.json keeps today's content stamp (SessionDiagnosticsModel::
// computeContentStamp) until 3.5's parallel run, so parity A shows no difference.

#include "../measures/club_baseline_provider.h"
#include "../measures/live_measure_source.h"   // detectForSwing and the grid it reads — the Rules ring's own seam
#include "../norms/norm.h"                      // Cohort
#include "../norms/norm_provider.h"
#include "../packs/pack_provider.h"
#include "../../Coaching/diagnostic_ledger.h"   // ShotRecord, the rows
#include "../../Coaching/shot_factors.h"        // ShotFactors (and, through it, ShotOutcome, composeShotOutcome)
#include "../../Coaching/strike_from_flight.h"  // FlightNorms, ClubFlightUsual
#include "../../Core/pp_tuned_constants.h"
#include "../../Wrist/wrist_profile_assess.h"   // WristInput, the wrist checkpoint profile (DEC-123)

#include <QHash>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>
#include <vector>

namespace pinpoint::analysis {

// ── The rule versions (R-STAMP-3: hand-kept beside the rule they version, checked by rule_version_golden_test,
//    which fails when fireFindings' output on its fixtures, over a frozen pack and norms, moves and neither of these did) ─────────────────────

// WHAT A NOT-JUDGED ROW SAYS, versioned apart from the row shape (kDiagSchemaVersion, diagnostic_ledger.h): the
// shape did not change when the rows began naming the engine rule behind a refusal and the capture gate behind
// an excluded shot (trust stage 6 session 2), but a ledger written before did not fill them. Hashed into the
// content stamp, so such a ledger regrades once on activation.
// 1 (trust stage 6 session 2): the rows name the engine rule behind a refusal and the gate behind an excluded shot.
inline constexpr int kRowWordsVersion = 1;

// HOW A ROW IS GRADED, versioned apart from the row shape and the words: a change to what detection decides for
// a swing, with no new field to fill, would otherwise leave every stored ledger looking current. Hashed into the
// content stamp, so a ledger graded under an older rule regrades once on activation (re-reading each swing's
// stored readings; it never re-runs pose, ball or shaft).
// 1 (14 Nov 2026, audit B-07): a swing that declares no club is graded as an undeclared context — its corridor
//   readings carry the engine's inferred-context demotion (×0.7, "club assumed") and its rows
//   ConditionRow::contextInferred. Before, the live path graded it as a declared full swing at full confidence
//   and every row said not inferred.
inline constexpr int kRowGradingVersion = 1;

// ── The inputs ──────────────────────────────────────────────────────────────────────────────────────────────

// Everything a shot's findings and derived data depend on besides its own readings (R-STAMP-1; build/refactor/
// step-3/BRIEF.md §4.4 step 1). Each field says which function reads it. Values only: the two shared_ptrs are to
// immutable rule data (R-CON-4), never to a QObject.
struct FiringInputs {
    // The diagnostic graph and its corridors, from a RuleData snapshot (RuleDataSnapshot::pack, ::norms). Read by
    // fireFindings(); both required.
    std::shared_ptr<const ICharacteristicPackProvider> pack;
    std::shared_ptr<const INormProvider>               norms;
    // SET-15, the preset's name (gradePolicyByName). Read by fireFindings().
    QString gradePolicy = QStringLiteral("standard");
    // SET-06, as stored. RECORDED, NOT APPLIED — nothing in the app applies it today (RuleDataSnapshot's own
    // note), so it moves the stamp and never the output.
    QStringList disabledNormSets;
    // The flight table (flight_norms.json). Read by deriveShotData() / deriveShotFactors().
    FlightNorms flightNorms;
    // The session's frozen club baseline, by canonical club (Contract A: club_baseline.json); nullopt until the
    // session has one. Read by fireFindings() (the two history measures, the swing length) and deriveShotData()
    // (the carry against the usual).
    std::optional<QHash<QString, ClubBaselineEntry>> clubBaseline;
    // The session's usual flight per canonical club (GL-25, DEC-130: from the shots' readings,
    // sessionFlightUsuals). Read by deriveShotData() / deriveShotFactors() (Sky; the strike read from the flight).
    QHash<QString, ClubFlightUsual> usuals;
    // The norm cohort on the shot's date (D6, DEC-171). Read by fireFindings(). Unqualified today: no caller
    // passes one yet (refactor 3.5's snapshot does), which resolves exactly as before cohorts existed.
    Cohort cohort;
    // Whether readings carry their measurement σ (session_diagnostics_design.md §A8.3). Read by fireFindings().
    bool withSigma = tuned::diagUncertainty::kEnabled;
};

// The club baseline's entry for `club` (any spelling; the keys are canonical), or null when the session has no
// baseline or none for this club.
const ClubBaselineEntry *clubUsualOf(const FiringInputs &inputs, const QString &club);

// A club baseline provider over `clubs` (canonical keys) that answers the swing's own spelling of a club. The one
// the session model hands the history measures, and the one fireFindings() builds from FiringInputs.
std::shared_ptr<IClubBaselineProvider> canonicalClubBaselineProvider(QHash<QString, ClubBaselineEntry> clubs);

// R-STAMP-1's fingerprint: the rule versions fireFindings() answers to (kDiagSchemaVersion, kRowWordsVersion,
// kRowGradingVersion, kPhaseGridSchemaVersion) and every field of `inputs`, as one short hash. Changing any of
// them changes it. NOT WRITTEN YET: refactor 3.5 records it in the stamp of each shot's findings.
QString firingStamp(const FiringInputs &inputs);

// THE CONTENT STAMP (refactor step 3.7-e: ONE rule, the old model's contentStampOf moved here so the owner and the screens'
// model hash alike): the diagnostic graph, the norms and their contexts, and the row shape's versions (schema, words,
// grading, the phase grid) — "which pack and norms graded the rows" (FIELD-LIST §1.8). What a session summary's freshness
// fingerprint carries (Coaching/session_results.h summaryFingerprintOf) and the catch-up's scan compares; the screens'
// SessionDiagnosticsModel::contentStamp(). Empty without both. The rows it speaks for are checked against the ball
// flight, so it carries the flight table and the flight check's rule and thresholds too (C-02; refactor 6.3a: R-STAMP-1);
// a null `flight` hashes as the empty table, as an empty snapshot's rows are checked.
QString contentStamp(const ICharacteristicPackProvider *pack, const INormProvider *norms, const FlightNorms *flight);

// THE SESSION SUMMARY'S RULE-DATA STAMP (refactor 3.7-e round 2, R-STAMP-1; review R1-B2): firingStamp over `inputs` with
// the per-session and per-shot parts made neutral — no usuals, no cohort, no club baseline, σ as tuned — so it covers the
// row shape's and the grid's versions, the pack, the norms and their contexts, the grade policy (SET-15), the disabled sets
// (SET-06) and the flight norms: what a session's summary was graded under, computable from the rule data alone.
QString summaryRulesStamp(FiringInputs inputs);
// A club baseline's stamp (every entry, as firingStamp hashes it); "none" for none.
QString clubBaselineStamp(const std::optional<QHash<QString, ClubBaselineEntry>> &baseline);

// ── Layers 4–5: the findings ────────────────────────────────────────────────────────────────────────────────

// One shot's readings, as the caller read them off its Recording. Nothing here is read by this module.
struct FiringReadings {
    SwingPhaseGrid grid;            // readLivePhaseGrid(swingDir, pack, …): the analysis, as measures
    QString        handedness;      // the document's summary, as recorded ("Left", "right", "")
    bool           dataWarning = false;   // the summary's data warning
    bool           captureBroken = false; // …because frames were lost during capture (dataWarningDetail.capture)
    bool           imuBroken = false;     // …because the IMU record does not reproduce (dataWarningDetail.imu)
    // The shot's wrist series and phases (refactor 3.8-ii; DEC-123): what its wrist checkpoint profile is assessed from
    // (Wrist/wrist_profile_assess.h). Read only for a shot whose grid carries a wrist DOF (readWristInput); empty else.
    WristInput     wrist;
};

struct FiredShot {
    bool       ok = false;          // false only without a pack or norms
    // rows, signalRows, peakTimes, the readings and ungradedReadings, club, contextId, clubheadSpeed,
    // swingLength, handedness, dataWarning. shotId and timestampMs are the caller's to set.
    ShotRecord record;
    bool       hasLaunchMonitor = false;   // the grid carries launch-monitor metrics
};

// The shot's findings (see the file comment). Deterministic: the same readings and inputs give the same record.
FiredShot fireFindings(FiringReadings readings, const FiringInputs &inputs);

// ── Layer 3: the derived data ───────────────────────────────────────────────────────────────────────────────

struct ShotDerivedData {
    ShotOutcome outcome;   // composeShotOutcome: labels, story, carry against the usual
    ShotFactors factors;   // shotFactors: strike · direction · flight
};

// The shot's derived data. `shot` is its record as the session holds it (its rows checked against the flight);
// `corrections` and `intent` the golfer's word on it; `diagnosis` the session's links and tiers, which the
// outcome's "because" reads (its flightUsuals are ignored: the usuals come from `inputs`).
ShotDerivedData deriveShotData(const ShotRecord &shot, const std::vector<ShotCorrection> &corrections,
                               const QString &intent, const ShotOutcomePack &outcomePack,
                               const ShotOutcomeSessionFacts &diagnosis, const FiringInputs &inputs);

// The three factors of a shot whose outcome is already composed (deriveShotData's second half): the session's owner
// calls this on the outcome it holds (until refactor 5.4b the old session model's club mix did too).
ShotFactors deriveShotFactors(const ShotRecord &shot, const ShotOutcome &outcome, const ShotOutcomePack &outcomePack,
                              const FiringInputs &inputs);

// ── Words the session model shares with the rows ────────────────────────────────────────────────────────────

// A measure that is not live (held, planned, no producer…) is not graded at all: say it is held back, which is
// the honest reason, rather than "not measured" — the series may well be on the swing. ONE sentence for a
// composite's unread part and a plain row the engine refused as NotLive (#176), so the two can never word the
// same fact differently. `status` is measureStatusName's spelling.
QString notLiveWords(const QString &status);

} // namespace pinpoint::analysis
