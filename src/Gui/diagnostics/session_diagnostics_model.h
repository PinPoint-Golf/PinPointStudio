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

#include "../../Coaching/diagnostic_ledger.h"
#include "../../Coaching/firing_side.h"            // conditionFiringSide (#16)
#include "../../Coaching/shot_outcomes.h"
#include "../../Coaching/tendencies.h"
#include "../../Coaching/work_ons.h"
#include "../../Diagnostics/engine/characteristic_engine.h"
#include "../../Diagnostics/engine/relation_resolver.h"
#include "../../DomainModel/session_input.h"
#include "../../DomainModel/session_identity.h"   // the held session's identity (its id names its shots)
#include "session_spread.h"

#include "../../Coaching/session_results.h"   // SessionResults (the thin adapter's view of its owner: 3.7-a)

#include <optional>
#include <QHash>
#include <QJsonObject>
#include <QDate>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>
#include <vector>

namespace pinpoint {
class Session;
class SessionDiagnostics;
struct WriteResult;
class ShotCommandDoor;
struct LabelCorrectionNote;
}

namespace pinpoint::analysis {
struct SessionReductionInput;
}

namespace pinpoint::analysis {
class ICharacteristicPackProvider;
class INormProvider;
}

// THE QML FAÇADE for PpSessionDiagnosticsPanel — the one object between a recorded swing and
// the panel that says what keeps happening.
//
// It is a JOIN and an ORCHESTRATOR, and deliberately not a second place where diagnostics
// arithmetic lives. Everything it publishes is one of three things:
//
//   · a reduction from `src/Coaching/diagnostic_ledger.h` (every tier, recurrence, trend,
//     link grade, chain, stage, bookend and anti-churn ordering),
//   · a fact from the authored pack (`src/Diagnostics`: names, consequences, screens,
//     causal edges, the explanation pass), or
//   · a STRING assembled from the two, here, because "QML positions and paints, C++ decides
//     the numbers" (analysis pipeline guide §6.2) means the panel gets sentences and not
//     ingredients. There is no arithmetic in this file that diagnostic_ledger.h could have
//     done, and no wording in the QML that this file could have written.
//
// INSTANTIATED BY THE PANEL, not registered in main.cpp as a context property — the same
// decision LmSessionModel documents and for the same reason, sharpened. The session
// diagnostics panel is OFF by default (the user turns it on in View), and this model owns a
// characteristic pack, a norm provider and a worker thread that re-runs detection on every
// shot. A golfer who never opens the panel should not be paying for any of it, and a model
// that reached for the singletons itself would additionally be untestable — which matters
// more here than anywhere, because the thing under test is a claim about a golfer.
//
// ── The five decisions worth knowing before reading the .cpp ────────────────────────────
//
// 1. AND 2. (THREADING, PERSISTENCE) ARE THE OWNER'S since refactor 5.4b, which deleted the old engine: the Session's
//    SessionDiagnostics fires each shot on the session's lane, reduces once per batch and is diagnostics.json's one
//    writer (src/DomainModel/session_diagnostics.h). This class takes each applied diagnosis in (its link: mirror) and
//    writes nothing of the session; the golfer's edits forward to the Session's and the Shot's commands.
//
// 3. WHEN explain() RUNS. Over the PATTERN-TIER set only, and only when that set's
//    membership changes (design §A5) — per-shot re-ranking would churn the resolver's greedy
//    set cover on every ball and reorder the footer under the golfer's eyes. The footer is
//    then debounced a second time, by kDiagDriverDebounceShots, so it appears only once the
//    pattern set has held still: better absent than flickering (§B8).
//
// 4. WHAT CADENCE GATES. `quiet` and `afterShotDelta`. Nothing else, ever. The owner's firing does
//    not consult it, the ledger does not see it, and the persisted rows are byte-identical
//    under both modes — brief §5.6. A cadence check anywhere near the ingest path is the
//    single most likely way for this panel to start lying, so there is exactly one place in
//    the .cpp where m_cadence is read and it is named surfaceThisShot().
//
// (Until refactor 3.8-ii a section here said what the athlete's fault_profile.json might touch: it was read at every
// activation and written at a close nothing called, so it was never written; H-11, D-07. Deleted with its reader.)
// Every property below is the shape the stage panel and the cast window bind to. Unit-tested standalone in
// src/Diagnostics/tests/session_diagnostics_model_test.cpp.
//
// ── THE THIN ADAPTER (refactor step 3.7-a; build/refactor/step-3/BRIEF.md §4.9, §4.10; D3: DEC-165) ─────────────
//
// The stage panel (and the wall cast, which hosts the same panel file) no longer make one of these. They reach the
// Session the panel's folder names through app.sessionAt (D1: DEC-164) and ask SessionDiagnosticsAdapters for that
// Session's adapter: an instance of this class MADE BY C++ (adapterFor), the Session's child, one per (Session, surface),
// that judges nothing and writes nothing of the session's diagnostics. It shows the Session's ONE SessionDiagnostics
// (LIVE-2): on each applied diagnosis it takes the owner's rows (as the reduction read them), ledgers, links, stage,
// composed outcomes and typed results (reasons, carry today, club mix) and runs only this class's VIEW half — the
// graph's marshalling, the chains' rails, the explanation, the cards' hysteretic order, every surface builder and the
// words (R-DER-9: the adapter composes them). The golfer's commands forward to the Session's and the Shot's commands
// as before (3.4b-i); the owner hears their events and diagnoses again. Every live shot is judged whether or not a
// panel is up (DEC-02; 04 step 3 item 4): the owner runs for every loaded session. What stays this class's in the
// adapter: the view state (selection, cadence, the open detail, reviewing) and the same golfer's other sessions
// (historyOf). The club baseline is the owner's alone (refactor 3.7-e: ONE freeze rule,
// SessionDiagnostics' from the predecessors' summaries; the adapter's interim live freeze, J-a2, is gone). An adapter
// holds a use of its Session only while a screen shows it (a view bound: addView), so a sweep may release a session no
// screen shows (3.7-e; Q-18).
//
// MADE BY C++ ONLY (refactor 3.7-e: "cut to a thin adapter created by C++"): QML names the type and never creates it
// (QML_UNCREATABLE). Since refactor 5.4b it is ONLY an adapter: the old engine half (the per-shot firing, the reduction, its
// stores, its pools and baseline authorities) is deleted, and a folder no Session holds gets the empty adapter.
class SessionDiagnosticsModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Made by C++ only (refactor 3.7-e): diagnosticsAdapters.of(app.sessionAt(folder), surface)")

    // ── Inputs the panel binds, passed in rather than reached for ────────────────────
    //
    // Same rule LmSessionModel states for `gradePolicy` and `connected`: the panel already
    // has `appSettings` in scope, and a model that read the singleton itself would couple
    // the panel to main.cpp and could not be constructed in a test. Bind `cadence` to
    // AppSettings::sessionDiagnosticsCadence and `gradePolicy` to
    // AppSettings::diagnosticsGradePolicy — the latter must be the SAME policy the other
    // metric surfaces grade against, or one reading would sit outside its corridor on one
    // panel and inside it on another.
    Q_PROPERTY(QString cadence READ cadence WRITE setCadence NOTIFY cadenceChanged)
    Q_PROPERTY(QString gradePolicy READ gradePolicy WRITE setGradePolicy NOTIFY gradePolicyChanged)

    // Whether the shots being reduced are a LOADED session's rather than the live one's —
    // LmSessionModel's `reviewing`, with one addition: review also FREEZES the ledger at
    // Closing, because a finished session's panel is its summary and must not re-open
    // because it was looked at.
    Q_PROPERTY(bool reviewing READ reviewing WRITE setReviewing NOTIFY reviewingChanged)

    // The shot the stage carousel has focused. -1 for "none". The panel reads the carousel;
    // it never owns a selection of its own (brief §8).
    Q_PROPERTY(int selectedShotId READ selectedShotId WRITE setSelectedShotId NOTIFY selectedShotIdChanged)

    // ⚠ THE SELECTION BY ShotRef (R-APP-4; refactor step 4-a), which is how the panel sets it: app.state.selection's
    // `shotRef` (ShotRef's text form) and the folder it was picked with. The carousel's shot ids are its own counter,
    // and this model's are the swing number, so the ShotRef is the identity both sides share: it resolves to this
    // model's shot only when its session is this model's session (a selection never reaches another session's shot
    // of the same number: E-05). The folder is used only when either side has no id (a session no owner can be named
    // for: J-a4), and then only as shotIdForSwingDir allows. Both re-resolve as shots arrive. Empty clears.
    Q_PROPERTY(QString selectedShot     READ selectedShot     WRITE setSelectedShot     NOTIFY selectedSwingDirChanged)
    Q_PROPERTY(QString selectedSwingDir READ selectedSwingDir WRITE setSelectedSwingDir NOTIFY selectedSwingDirChanged)

    // ── Session identity and lifecycle ──────────────────────────────────────────────
    Q_PROPERTY(QString sessionDir READ sessionDir NOTIFY sessionChanged)
    // "cold" | "forming" | "established" | "closing". Ratcheted one way within a session.
    Q_PROPERTY(QString stage READ stage NOTIFY surfaceChanged)
    Q_PROPERTY(bool closed READ closed NOTIFY surfaceChanged)
    Q_PROPERTY(int shotCount READ shotCount NOTIFY surfaceChanged)
    Q_PROPERTY(int patternCount READ patternCount NOTIFY surfaceChanged)
    // A detection is in flight. The panel shows the previous state while it is true — never
    // an empty one, because a blank panel between balls reads as "nothing is wrong".
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // An adapter whose Session's owner has not yet applied its first diagnosis of a session that has shots: a reviewed
    // session is loaded (and a stale shot re-fired) off the GUI thread, so for a moment the adapter holds nothing. The
    // panel says so rather than show an empty session as if it were the session (3.7-c review B3).
    Q_PROPERTY(bool loading READ loading NOTIFY busyChanged)
    // THE SESSION'S CLUB BASELINE IS NOT READY (refactor 3.7-e round 2; J-49): its owner holds none and could not freeze one
    // (earlier sessions not summed up by this build yet), so its findings against the golfer's usual numbers wait. The
    // panel and the carousel say so (show with a warning, never hide). An adapter's only (false for an engine model).
    Q_PROPERTY(bool baselineWaiting READ baselineWaiting NOTIFY busyChanged)
    // A Session's thin adapter (refactor step 3.7-a), not an engine of its own: the panel shows its no-golfer notice when
    // it is handed the EMPTY adapter (session_diagnostics_adapter.h).
    Q_PROPERTY(bool adapter READ isAdapter CONSTANT)

    // ── Session intent (design §A6) ─────────────────────────────────────────────────
    //
    // Both declarations shape ATTENTION and EXPERIMENT STRUCTURE and never evidence. The
    // focus contract supplies the FocusSplit that lets a link earn Moved-together honestly;
    // the declared miss pre-arms the chains upstream of the outcome. Neither is an input to
    // detection, to a corridor, or to a tier — asserted by test.
    Q_PROPERTY(QString focusConditionId READ focusConditionId NOTIFY intentChanged)
    Q_PROPERTY(int focusFromShot READ focusFromShot NOTIFY intentChanged)
    Q_PROPERTY(QString declaredMiss READ declaredMiss NOTIFY intentChanged)
    // The declared miss's authored label, so the panel's header chip can name it without
    // resolving an id against a pack it does not have. Empty when nothing is declared.
    Q_PROPERTY(QString declaredMissName READ declaredMissName NOTIFY intentChanged)
    // Everything authored upstream of the declared miss — the chains it pre-arms. Names and
    // ids, so the panel can badge a card as "on the way to your declared miss".
    Q_PROPERTY(QVariantList missChain READ missChain NOTIFY intentChanged)

    // ── The zones (design §B2, brief §4) ────────────────────────────────────────────
    //
    // One property per region of the panel, each already carrying its final strings. The
    // QML binds and positions; it computes nothing.
    Q_PROPERTY(QVariantMap headerInfo READ headerInfo NOTIFY surfaceChanged)
    Q_PROPERTY(QVariantList thisShot READ thisShot NOTIFY surfaceChanged)
    // Cadence's ONLY two outputs.
    Q_PROPERTY(bool quiet READ quiet NOTIFY surfaceChanged)
    Q_PROPERTY(QVariantMap afterShotDelta READ afterShotDelta NOTIFY surfaceChanged)
    Q_PROPERTY(QVariantList cards READ cards NOTIFY surfaceChanged)
    Q_PROPERTY(QVariantList watching READ watching NOTIFY surfaceChanged)
    Q_PROPERTY(QVariantList chains READ chains NOTIFY surfaceChanged)
    Q_PROPERTY(QVariantList unchained READ unchained NOTIFY surfaceChanged)
    Q_PROPERTY(QString unchainedLine READ unchainedLine NOTIFY surfaceChanged)
    Q_PROPERTY(QVariantMap driver READ driver NOTIFY surfaceChanged)
    Q_PROPERTY(QString coverageLine READ coverageLine NOTIFY surfaceChanged)
    // trust_and_gates_design.md §4.4: the conditions a gate kept from being judged on some shot —
    // [{ id, name, judged, of, subject, text }] — and the same as one sentence (empty when none).
    Q_PROPERTY(QVariantList notJudged READ notJudged NOTIFY surfaceChanged)
    Q_PROPERTY(QString notJudgedLine READ notJudgedLine NOTIFY surfaceChanged)
    // Closing only; empty otherwise.
    Q_PROPERTY(QVariantList bookends READ bookends NOTIFY surfaceChanged)
    // Cold only; empty otherwise. Expectations to TEST, never findings — the wording says so
    // and the panel must keep saying so.
    Q_PROPERTY(QVariantList expectations READ expectations NOTIFY surfaceChanged)

    // ── The condition detail (user-requested drill-in) ──────────────────────────────
    //
    // DESIGN-REVIEW: brief §9 lists "tapping a chain node through" as not designed. This is the
    // minimum honest answer to the request and it wants a design pass — see
    // docs/design/session_diagnostics_build_findings.md.
    //
    // A PROPERTY PAIR RATHER THAN AN INVOKABLE THE PANEL RE-CALLS, and the reason is the same one
    // that made shotReadout() an invokable: shotReadout answers a question about a shot the
    // CAROUSEL owns, so somebody outside this object has to say when to ask it again. The detail
    // answers a question about a condition THIS object owns the answer to, and its answer changes
    // on exactly the events the zones' answers change on — a shot lands, a screen is entered, the
    // reviewed shot moves. Published as a property, the detail is live by construction and a panel
    // that opened one mid-session watches it accumulate; published as an invokable it would need a
    // second re-fetch path in the panel, and a detail one shot stale is a detail that is lying.
    //
    // ITS OWN SIGNAL, and not surfaceChanged, because it is a projection of ONE MORE INPUT — which
    // condition is open — that no zone reads. Everything the zones share one signal for is one
    // projection of one reduction; this is that reduction seen through a second question.
    Q_PROPERTY(QString detailConditionId READ detailConditionId NOTIFY detailChanged)
    Q_PROPERTY(QVariantMap detail READ detail NOTIFY detailChanged)

    // ── Shot outcomes: the session's outcome strip and reasons (shot_outcomes_design.md §4.2) ───
    //
    // shotLog      [{ shotId, cell, lines: [string], labels: [displayId], reasonId, reasonIndex, clean,
    //                 intended, corrected, noReading, swingDir }] in strike order. `cell` is the
    //                 word lines joined; "" on a clean shot.
    // shotMix      { line: "11 clean · 6 slice · …", corrections: 2, correctionsLine: "2 labels corrected by you" }
    // reasons      [{ id, conditionId, name, golfer, shots, produced: [{phrase, count}], cost, costText,
    //                 tier, tierText, strength, chainConfirmed, trend (+1|0|-1), trendWords, colourIndex,
    //                 ticks: [{state, shotId}], flaggable }], ordered by shots then cost, the unexplained
    //                 row last (id/conditionId "", name "No reason found", tierText "Gap in the model",
    //                 flaggable true). state: "fired" | "clean" | "notAssessable".
    // carryToday   [{ club, clubId, mean, sd, n, usualMean, usualSd, usualN, line }], clubs in first-hit
    //                 order, full swings with strike misses left out. usual* NaN and no
    //                 usual in `line` until the club has kClubBaselineMinShots behind it.
    Q_PROPERTY(QVariantList shotLog READ shotLog NOTIFY surfaceChanged)
    Q_PROPERTY(QVariantMap  shotMix READ shotMix NOTIFY surfaceChanged)
    Q_PROPERTY(QVariantList reasons READ reasons NOTIFY surfaceChanged)
    Q_PROPERTY(QVariantList carryToday READ carryToday NOTIFY surfaceChanged)
    // clubMix      [{ club, clubId, judged, notRead, line: "Good 50% · Thin 25% · Slice 25%",
    //                 parts: [{ key, label, kind: "good"|"main"|"other", count, percent }] }], clubs in first-hit order.
    Q_PROPERTY(QVariantList clubMix READ clubMix NOTIFY surfaceChanged)

public:
    ~SessionDiagnosticsModel() override;

    QString cadence() const { return m_cadence; }
    void    setCadence(const QString &mode);
    QString gradePolicy() const { return m_policyName; }
    void    setGradePolicy(const QString &name);
    bool    reviewing() const { return m_reviewing; }
    void    setReviewing(bool on);
    // Whether the owner holds this session's club baseline (its club_baseline.json, Contract A), as its diagnosis says.
    bool    baselineFrozen() const { return m_baselineFrozen; }

    int     selectedShotId() const { return m_selectedShotId; }
    void    setSelectedShotId(int id);
    QString selectedSwingDir() const { return m_selectedSwingDir; }
    void    setSelectedSwingDir(const QString &dir);
    QString selectedShot() const { return m_selectedShot; }
    void    setSelectedShot(const QString &shotRef);

    // This model's id for a swing folder, or -1: the exact path, else — for a folder inside this
    // model's own session folder — its swing number (SwingPaths) when that is a shot this ledger
    // holds. Never by the folder's name alone: a swing_0003 of another session is not this one's
    // (E-05; the basename fallback is gone, refactor step 4-a).
    Q_INVOKABLE int shotIdForSwingDir(const QString &dir) const;

    QString sessionDir() const { return m_sessionDir; }
    // The held session's identity: its day (the start time's; a legacy folder's name's, read by
    // SessionIdentity) and its start time (C++ only; step 2.7a: the work-ons record's clock fallback).
    pinpoint::SessionIdentity sessionIdentity() const { return m_sessionIdentity; }
    QString stage() const;
    bool    closed() const { return m_closed; }
    int     shotCount() const { return int(m_shots.size()); }
    int     patternCount() const;
    // The adapter: its owner has asks or a diagnosis outstanding, or a write waiting (so busy going false means the
    // written file is there).
    bool    busy() const { return m_adapterBusy; }
    bool    loading() const { return m_adapterLoading; }
    bool    baselineWaiting() const { return m_adapterBaselineWaiting; }

    QString focusConditionId() const { return m_focusConditionId; }
    int     focusFromShot() const { return m_focusFromShot; }
    QString declaredMiss() const { return m_declaredMiss; }
    QString declaredMissName() const
    { return m_declaredMiss.isEmpty() ? QString() : conditionName(m_declaredMiss); }
    QVariantList missChain() const { return m_missChain; }

    QVariantMap  headerInfo() const { return m_headerInfo; }
    QVariantList thisShot() const { return m_thisShot; }
    bool         quiet() const { return m_quiet; }
    QVariantMap  afterShotDelta() const { return m_afterShotDelta; }
    QVariantList cards() const { return m_cards; }
    QVariantList watching() const { return m_watching; }
    QVariantList chains() const { return m_chains; }
    QVariantList unchained() const { return m_unchained; }
    QString      unchainedLine() const { return m_unchainedLine; }
    QVariantMap  driver() const { return m_driver; }
    QString      coverageLine() const { return m_coverageLine; }
    QVariantList notJudged() const { return m_notJudged; }
    QString      notJudgedLine() const { return m_notJudgedLine; }
    QVariantList bookends() const { return m_bookends; }
    // The Cold state's "Usually yours" list. ALWAYS EMPTY since refactor 3.8-ii: its one source, the athlete's
    // fault_profile.json, is deleted (H-11, D-07; it was never written). The block that draws it (PpExpectations) draws
    // nothing when empty; retiring the property with the block is a design-system change, for Mark (J-58, DS-1).
    QVariantList expectations() const { return {}; }
    QString      detailConditionId() const { return m_detailConditionId; }
    QVariantMap  detail() const { return m_detail; }
    QVariantList shotLog() const { ensureOutcomes(); return m_shotLog; }
    QVariantMap  shotMix() const { ensureOutcomes(); return m_shotMix; }
    QVariantList reasons() const { ensureOutcomes(); return m_reasons; }
    QVariantList carryToday() const { ensureOutcomes(); return m_carryToday; }
    // clubMix: [{ club, clubId, judged (shots with at least one factor read),
    //             strike, direction, flight: { judged (shots that factor was read on), notRead, line,
    //                                          parts: [{ key, label, kind, count, percent }] } }]
    // One mix per factor (shot_factors.h), no lead miss across them. A shot is counted under every value it
    // shows, so a factor's parts can add up to more than its shots. `kind`: "good" (Solid, Straight),
    // "value" (Fade, Draw, Low, Mid, High), "main" (the factor's commonest fault), "other".
    QVariantList clubMix() const { ensureOutcomes(); return m_clubMix; }

    // ── Session lifecycle ───────────────────────────────────────────────────────────

    // A screen points its adapter at the session it shows (the folder it chose, which named this adapter's Session): the
    // profile and history read, the owner's club baseline brought up to date at first need, then the owner's diagnosis
    // shown. It reads nothing of the session itself: the owner holds it.
    Q_INVOKABLE void activateSession(const QString &sessionDir);

    // (Refactor 5.4b: ingestShot, regradeShot and forgetShot went with the old engine. The Session's owner hears each
    // shot's document land (EVT-01), a re-analysis (EVT-02), a late reading (EVT-22) and a trash (EVT-03) itself.)

    // (No closeSession since refactor 3.8-ii: nothing called it, and a session's End is its Session's — EVT-09, J-24 —
    // which writes `closed` into diagnostics.json; a session loaded closed reads it from there.)

    // ── Intent (design §A6) ─────────────────────────────────────────────────────────

    // "Working on X." Records the split point at the CURRENT shot count, so shots already
    // struck are baseline and everything after is intervention. Declaring a focus is the
    // only thing that puts the Moved-together grade on offer at all.
    Q_INVOKABLE void declareFocus(const QString &conditionId);
    // The same, with the split given: shots before fromShot are baseline. Clamped to
    // [0, shotCount()]. For a focus declared elsewhere (SessionController, at session start
    // or on another surface's model) and adopted here after the fact, when the row count now
    // says nothing about when it was declared. Re-declaring the focus already held is a no-op.
    Q_INVOKABLE void declareFocusFrom(const QString &conditionId, int fromShot);
    Q_INVOKABLE void clearFocus();

    // The session's opening question — "what is the bad shot?". Pre-arms the chains upstream
    // of that outcome and marks it as the chain's outcome node. Verified against launch
    // monitor data where the session has any. An EMPTY id clears the declaration.
    Q_INVOKABLE void declareMiss(const QString &missId);

    // What the panel may offer as an answer to that question: every condition the pack
    // authors as an OUTCOME, as {id, name}, in pack order.
    //
    // THE CLASSIFICATION IS THE PACK'S KIND, not this file's opinion and not the ball-flight
    // GROUP. ConditionKind::Outcome is "what the ball did — where an explanation ENDS", it is
    // intrinsic (it never moves when a producer lands), and it is the same field
    // outcomeReachOf() asks when it counts what a condition reaches. The group is coextensive
    // with it over shipped content and core_pack_test asserts that in both directions, but
    // the group answers WHERE IN THE SWING and pressing it into this job is exactly the
    // conflation the kind axis was added to end. A pack that authors no kinds yields no
    // candidates, which is the honest answer: nothing there is marked as an outcome.
    Q_INVOKABLE QVariantList missCandidates() const;

    // A physical screen answered at the range counts immediately (design §A5). Feeds
    // explain()'s knownScreenResults and NodeSpec::screenEntered — which is what lets a
    // screened root stop anchoring nothing. UNWIRED in the panel today (PpSessionDiagnosticsPanel.qml: deliberately
    // unconnected; audit D-19): kept as the command a screens surface will call.
    Q_INVOKABLE void recordScreenResult(const QString &conditionId, bool present);

    // ── What happened on a shot (shot_outcomes_design.md §4) ────────────────────────────────

    // One shot's words, or {} for a shot not in the ledger. A shot with no launch-monitor reading has
    // noReading true and strike labels only.
    //   { ready, noReading, golferMirrored, phrase, story, noReasonText, clean,
    //     labels: [{ id, rowId, family, short, abbr, golfer, intended, corrected, miss,
    //                evidence: [{ measureId, label, value, unit, source: "lm"|"impact"|"model",
    //                             sourceText, text }] }],
    //     driver: { id, displayId, name, golfer, strength, explains, of } | {},
    //     links: [{ fromId, toId, grade, words, physics }],
    //     carry: { usual, usualSd, actual, unit:"yd", shortBy, provenance, line, residual,
    //              residualShown, parts: [{ id, name, loss, detail, lineWords }] } | {} }
    // golferMirrored: a left-hander's labels are glossed (`id` is the displayed condition, `rowId` the
    // ledger's). ids passed to correctOutcome/setShotIntent are the golfer's (Contract D).
    Q_INVOKABLE QVariantMap shotOutcome(int shotId) const;

    // Shaped as conditionDetail() for PpCauseGraph: { id, name, causeHeadline, noCausesLine, outcomeHeadline,
    // noEffectsLine, causes: [rails], effects: [], causesCapped, effectsCapped, rivals: [], driverId }. Rails
    // carry nodes and links as conditionDetail's, the driver's path marked `primary`; physics steps have
    // grade "physics" and word "from the fat · clear". conditionDetail() never shows LeadsTo; this does.
    Q_INVOKABLE QVariantMap shotOutcomeDetail(int shotId) const;

    // §4.4. toId is the golfer's condition; "" means none in that family, and passing the detected one
    // again withdraws the correction. Never touches a row, a corridor or a tier (A6).
    Q_INVOKABLE void correctOutcome(int shotId, const QString &family, const QString &toId);
    Q_INVOKABLE void setShotIntent(int shotId, const QString &shape);       // "" = straight
    // "Hitting fades from the next shot": shots with id ≥ fromShot, including ones not yet struck.
    Q_INVOKABLE void setIntentFrom(int fromShot, const QString &shape);
    // Appends to <athlete>/outcome_corrections.jsonl: the tuning log's "no reason found" entries.
    Q_INVOKABLE void flagForReview(int shotId);

    // ── Review (brief §6, option 13a) ───────────────────────────────────────────────

    // The this-shot strip for a selected shot, read INSIDE the finished ledger: session
    // counts, session tiers, this shot's own reading. Empty map for a shot not in the ledger.
    Q_INVOKABLE QVariantMap shotReadout(int shotId) const;

    // The carousel cell's pip row — one pip per tracked condition for that shot.
    Q_INVOKABLE QVariantList pipsFor(int shotId) const;
    // The count that sits beside the pips, in the state colour.
    Q_INVOKABLE int firedCountFor(int shotId) const;

    // ── The kinematic sequence checks (trunk_imu_sequence_grf_design.md §10.6, E4) ──────
    //
    // The thirteen sequence checks for one shot (marks and reasons; no count), read off the
    // engine's own signal verdicts that the per-shot firing recorded (Coaching/sequence_checks.h has the
    // shape). Empty map for a shot not in the ledger or one recorded before schema 5.
    Q_INVOKABLE QVariantMap sequenceChecks(int shotId) const;
    // The session's swings best-sequenced first (§10.6: checks failed with pFire > 0.5, then
    // Σ pFire, then clubhead speed), as shot ids; swings with no check assessed are left out.
    // For the replay's best/worst — first is "your best-sequenced swing", last "your worst".
    Q_INVOKABLE QVariantList sequenceRanking() const;
    // The session's sequence consistency (F10, §10.8 E2): "In sequence on 9 of 14 swings", the
    // peak-timing spread against the professional spread with the cohort named, and the engine's
    // verdict on the session condition (Coaching/sequence_checks.h sequenceConsistencyToVariant has
    // the shape), plus `countLine` / `afterLine`: the session card's count and the line under it, named
    // by the clause that fired (sequenceConsistencyCountLine / AfterLine; absent with no session
    // condition in the pack). Read over the session, never one swing. The Charts KS Session view binds
    // this and must not recompute it.
    Q_INVOKABLE QVariantMap sequenceConsistency() const;

    // ── The condition detail ────────────────────────────────────────────────────────

    // What might have caused this condition, and what it most likely leads to, with this
    // session's evidence on every link. A PURE READ: it takes no lock on the panel's state,
    // consults no cadence, moves no ratchet, writes nothing to disk, and returns the same map in
    // live and in review for the same ledger. Empty for a condition the pack does not author.
    //
    // The map (every string authored here, exactly like the zones):
    //   header          one pattern-card map for the condition itself, plus `mark`/`kindText`
    //                   for a condition the capture never measured
    //   causeHeadline   the top-ranked cause in one sentence, or the honest absence of one
    //   outcomeHeadline the most likely outcome in one sentence, with its evidence
    //   causes          rails, most-evidenced first, upstream to the roots. `primary` on the first
    //   effects         rails, most-evidenced first, downstream to the outcomes
    //   rivals          parents this session could not adjudicate between — named, never ranked
    // Rails carry the SAME node and link maps the chain rail consumes, off the same helpers, so
    // the detail speaks the panel's existing visual language rather than a second dialect.
    Q_INVOKABLE QVariantMap conditionDetail(const QString &conditionId) const;

    // Open/close the published `detail`. Opening a different condition from inside the detail
    // RE-TARGETS it — there is no navigation stack, deliberately: one step back to the panel is
    // the whole of the model, and a stack would be a second place for "where am I" to be wrong.
    Q_INVOKABLE void openDetail(const QString &conditionId);
    Q_INVOKABLE void closeDetail();
    // Which of the open condition's measures the detail draws, when it is read by more than one
    // (detail.measures, one tab each). Empty = the default: the one that fired most. Re-opening
    // a different condition resets it; an id the condition does not read is declined.
    Q_INVOKABLE void setDetailMeasure(const QString &measureId);

    // ── How far, not only whether (session_spread.h) ────────────────────────────────
    //
    // The corridor strip and the value-by-shot run for one condition — the same map every
    // pattern card carries as `spread` (cardMap), published by id as well so a surface that is
    // not a card (a probe, a test, a future export) reads exactly what the card draws rather
    // than re-deriving it. Empty for a condition this session holds no ledger for.
    //
    //   axisLo/axisHi, decimals   the axis in the measure's own units, and how it is quoted
    //   corridorKnown, shape      whether there is anything to grade against, and which sides
    //   bands[]                   { grade: ideal|good|watch|action|open, lo, hi, f0, f1, edge } — `edge`
    //                             the zone edge the band starts at (edgeKindAt; "" for the first band)
    //   faultLines[]              { value, f, text, shortText } — where Action begins, per graded side
    //   normTag/normTagLong/normCitation/normSource   what the corridor is: judgement or data
    //   dots[]                    one per ASSESSABLE shot: { index, shotId, swingDir, value, f,
    //                             clipped, stack, state, current, recent, valueText }
    //   stackMax                  the deepest beeswarm column, so the QML can choose a pitch
    //   clipLo/HiCount, clipLo/HiText   the dots pinned to each edge, and the one label for them
    //   run[]                     one per SHOT, not-assessable included: { index, shotId,
    //                             swingDir, state, assessable, value, fx, fy, clipped, current,
    //                             selected, valueText, z } — z: the shot on the common scale (ladderZ,
    //                             folded by ladderSide), absent where there is no reading or no corridor
    //   median[]                  { index, fx, fy, value, z } the trailing rolling median
    //   medianWindow, notAssessable, pastFault, caption, captionShort, muF, muText
    //   run[].readout, currentReadout   the hover line: "shot 12 · 14 % hand rise · 1 % hand rise
    //                             past the fault line · fired"; "shot 7 · - · not measured"
    //   dirLeft/dirCentre/dirRight, highMeans   which way is better, off the corridor's shape
    //   yTicks[]                  { value, f, text } round Y labels for the run (and history)
    //   medianNumber, currentNumber, currentState   the card's two figures, unit-less; "-" for none
    Q_INVOKABLE QVariantMap spreadOf(const QString &conditionId) const;

    // ACROSS SESSIONS (review only on the panel; this read answers whenever asked). The same
    // golfer's other sessions — the session folder's siblings, read off their diagnostics.json
    // off the GUI thread and NEVER written — plus this one from memory, oldest first, each as its
    // dots, median and interquartile range against this condition's bands as they stand today.
    //   columns[]  { label, sessionDir, current, n, values[{f, clipped, stack, value}], median,
    //                q1, q3, fMedian, fQ1, fQ3, medianText }
    //   axisLo/axisHi, bands[], faultLines[], caption, loading, missing, hiddenEarlier, hiddenLater
    Q_INVOKABLE QVariantMap historyOf(const QString &conditionId) const;

    // ── Test seam ───────────────────────────────────────────────────────────────────
    //
    // Detection off the GUI thread is the right production shape and the wrong test shape:
    // a unit test asserting a stage progression should not also be asserting that an event
    // loop ran. This is the whole of the concession — the code path is otherwise identical.
    Q_INVOKABLE void setSynchronous(bool on) { m_synchronous = on; }

    // ── The golfer's commands forward (refactor step 3.4b-i; BRIEF.md §4.12) ─────────────────────────
    //
    // correctOutcome and setShotIntent FORWARD to the shot's own commands (Shot::correctLabel, EVT-06, whose
    // Athlete appends the tuning log; Shot::setShotIntent, EVT-07); declareFocus / clearFocus, declareMiss and
    // recordScreenResult to Session::setSessionIntent (EVT-10), setIntentFrom to Session::setIntentRun (EVT-08).
    // This model then writes none of those facts itself (one writer, P10; DEC-94): it keeps its in-memory copy
    // for its outcome surfaces and its notices, as a reader. THE DOOR IS PROCESS-WIDE because the panel's model
    // is made by QML: main.cpp sets `app` (src/App/shot_command_door.h) once at launch. A session no Athlete
    // holds — and every model in a tool or test, which sets no door — keeps today's writes (N1's interim).
    static void setShotCommands(pinpoint::ShotCommandDoor *door);

    // ── The thin adapter (refactor step 3.7-a; the class comment) ────────────────────────────────────
    // The adapter of `session` for `surface` ("panel", "cast"): made on the first ask, the Session's child (so it goes
    // with the Session), C++-owned, holding a use of the Session for its life (Session::addUse: Q-18, so no sweep
    // releases a session a screen shows). Null when the Session has no SessionDiagnostics (no doors: a tool or test).
    static SessionDiagnosticsModel *adapterFor(pinpoint::Session *session, const QString &surface);   // ibid.
    bool isAdapter() const { return m_adapter; }
    // THE VIEWS BOUND TO AN ADAPTER (refactor 3.7-a, review M2): a panel adds itself while it shows the adapter and drops
    // itself when it goes. With none bound, the adapter does no view work on a new diagnosis (it is owed, and done when a
    // view binds again). An engine-mode model counts them and ignores them.
    Q_INVOKABLE void addView();
    Q_INVOKABLE void dropView();
    int views() const { return m_views; }
    // The surface an adapter was made for ("panel", "cast"); empty for an engine-mode model. SessionDiagnosticsAdapters
    // ::shownFor reads it with views() (refactor 3.7-c: the carousel's pips read the adapter a panel shows).
    QString adapterSurface() const { return m_adapterSurface; }

    // The measurement-uncertainty switches (session_diagnostics_design.md §A8.8), for the report
    // tool that grades each stage against the others. Production leaves them at the tuned defaults.
    // Set BEFORE activateSession(): detection reads withSigma as each shot is ingested.
    void setUncertaintyModes(bool withSigma, bool softTier, bool posteriorRank)
    {
        m_withSigma              = withSigma;
        m_opt.softTier           = softTier;
        m_explainOpt.posteriorRank = posteriorRank;
        // The explanation is cached by its pattern set (refreshExplanation); new options re-rank it at the next rebuild,
        // as a change of the screens does — a thin adapter (refactor 3.7-d) has explained before its tool sets these.
        m_patternSet.clear();
    }
    // Read-outs for the same tool — the evidence and what was concluded from it.
    const std::vector<pinpoint::analysis::ShotRecord>      &shotRecords() const { return m_shots; }
    const std::vector<pinpoint::analysis::ConditionLedger> &ledgerRows()  const { return m_ledgers; }
    const pinpoint::analysis::Explanation                  &explanation() const { return m_explanation; }
    const std::vector<pinpoint::analysis::LinkEvidence>    &linkEvidence() const { return m_links; }
    // The stage ratchet's latch (rebuild): the session ever established. A pure read, for the parallel run.
    bool reachedEstablished() const { return m_reachedEstablished; }
    Q_INVOKABLE bool synchronous() const { return m_synchronous; }

    // ── Work-ons (docs/design/work_ons_design.md) ───────────────────────────────────
    //
    // No work-on list of its own (refactor step 5.6b; DEC-81: no figure computed twice): the session's list is its
    // owner's reduction (SessionDiagnostics::diagnosis()->results.workOns), read through the Session.
    // What the rows are graded against, so a holder of this model can tell when a record it
    // derived earlier has gone stale. The same stamp gradedFrom carries.
    QString contentStamp() const { return computeContentStamp(); }
    // The same stamp with no model (refactor step 3.7-d): the pack and norms as they stand now, as a model made now would
    // read them (makeCharacteristicPackProvider, sharedNormProvider). For the session report's fingerprint, which no
    // longer makes a model of its own; one rule with computeContentStamp (the hash below).
    static QString currentContentStamp();
    // This session's evidence per condition, for work_ons.json: what the tendency reduction needs to
    // re-compose a shot without re-deriving the session (Contract: per condition, not per shot).
    pinpoint::analysis::TendencySessionEvidence outcomeEvidence() const;
    // The corridor a condition's run of readings is drawn against (spreadCorridorOf, at the focus
    // shot), for a holder that builds its own figures from ledgerRows() — the session report does.
    // `measureId` empty = the driving measure. An empty corridor when the condition has no ledger.
    pinpoint::analysis::SpreadCorridor corridorOf(const QString &conditionId, const QString &measureId = QString()) const
    {
        const pinpoint::analysis::ConditionLedger *l = ledger(conditionId);
        return l ? spreadCorridorOf(*l, focusIndex(), nullptr, measureId) : pinpoint::analysis::SpreadCorridor{};
    }
    // Spin the caller's event loop until the Session's owner is idle (busy false). Returns false on timeout. For a
    // probe or a tool; a screen never waits.
    Q_INVOKABLE bool waitForIdle(int msTimeout = 60000);

    // ── Where a reading sits against its corridor, for the figures (C21) ─────────────────
    //
    // Decided HERE, off the corridor itself, and published; the QML used to rebuild both from the band
    // words ("good|watch" is a pass edge) and from a z ladder of its own, and got a one-sided corridor
    // wrong. Static and public so the test can pin them on a hand-built corridor.
    //   edgeKindAt   the zone edge at `v` (a boundary between two of spreadBands()): "fault" where Action
    //                begins, "pass" at the signal edge (Watch begins), "aim" where a one-sided corridor's
    //                open side meets Ideal, "grid" at an Ideal edge; "" for none. Edges that coincide (a
    //                monitor bound at the signal edge empties Watch) name the stronger: fault, pass, aim.
    //   ladderZ      the reading on THE COMMON SCALE the session tiles share: 1 at the Ideal edge, 2 at the
    //                pass edge, 3 at the fault line, linear between them and on past the outermost edge,
    //                times `side`. On a one-sided corridor the open side is no distance and reads 0
    //                (readingStrength's rule). Edges that coincide take the outer rung: where a monitor bound
    //                empties Watch (or Good and Watch), on one side or two, the shared edge is the fault line
    //                at 3 (the QML's ladder put it at 2, or 1). NaN with no corridor.
    //   ladderSide   the side that scale folds up: a one-sided corridor's graded tail, always; a two-sided
    //                one, the side the session median is on (+1 high, −1 low).
    static QString edgeKindAt(const pinpoint::analysis::SpreadCorridor &c, double v);
    static double  ladderZ(const pinpoint::analysis::SpreadCorridor &c, double v, int side = 1);
    static int     ladderSide(const pinpoint::analysis::SpreadCorridor &c, double median);

signals:
    void cadenceChanged();
    void gradePolicyChanged();
    void reviewingChanged();
    void selectedShotIdChanged();
    void selectedSwingDirChanged();
    void sessionChanged();
    void intentChanged();
    void busyChanged();
    // Every derived surface, in one signal. The zones are ONE projection of ONE reduction
    // computed in rebuild(), exactly as LmSessionModel's tiles and graphics are — a signal
    // per zone would be a second chance for two regions of the same panel to disagree about
    // one shot.
    void surfaceChanged();
    // A first view bound, or the last one dropped (addView / dropView crossing zero; refactor 3.7-c): whether a panel shows
    // this model changed. Not emitted while a rebuilt panel's incoming view overlaps its outgoing one.
    void viewedChanged();
    // Which condition the detail is open on, and the detail itself. See the property block.
    void detailChanged();
    // One shot finished reducing. The panel's cue for the after-shot moment; carries whether
    // cadence decided to surface it, so the QML does not have to ask a second question.
    void shotIngested(int shotId, bool surfaced);
    // SHOW WITH A WARNING, NEVER HIDE (Stage 0.4c). Writes to `sessionDir`'s diagnostics.json have
    // just been switched off for this activation (m_ledgerWriteBlocked): the file is there but could
    // not be opened (blockLedgerWrites), or could not be read and could not be set aside either
    // (setAsideUnreadableLedger). The panel keeps taking the golfer's input and grading in memory,
    // and none of it is kept, so the golfer has to be told. Emitted ONCE per activation, on the
    // change from writable to blocked; `reason` is one plain sentence saying why, for the notice.
    // ledgerWritesResumed: an activation found `sessionDir`'s file readable (or found none), so
    // what was warned about is over for that session. Emitted at every such activation — the
    // listener's dismissal is idempotent and the model does not know whether anyone warned.
    // BOTH ARE QUEUED to the next turn of the event loop: the panel activates its model from a
    // binding while it is still being built, before its Connections exist (markLedgerWritesBlocked).
    // ⚠ REFACTOR-TEMPORARY WIRING: the model is created in QML, so it cannot reach the C++
    // NotificationCenter; PpSessionDiagnosticsPanel turns these into a notification. When the
    // Session owns SessionDiagnostics (target architecture, step 3) it posts directly and these go.
    // Known limits until then (session_diagnostics_design.md §A2): one panel's resumed clears the
    // shared notice while another panel's model may still be blocked; and the notice outlives the
    // panel leaving the session (it goes when dismissed or when the session next opens readably).
    void ledgerWritesBlocked(const QString &sessionDir, const QString &reason);
    void ledgerWritesResumed(const QString &sessionDir);

private:
    // ── The thin adapter's half (refactor step 3.7-a) ────────────────────────────────────────────────
    //
    // ITS SESSION-FACING HALF IS A LINK, in its own file (session_diagnostics_adapter.cpp): the Session, its owner, the
    // use it holds, the owner's signals heard, and mirrorOwner — so THIS file links nothing of the domain model, and every
    // tool and QML suite that compiles it (with no Session or SessionDiagnostics) builds as before.
public:
    struct AdapterLink {
        virtual ~AdapterLink() = default;
        virtual void mirror() = 0;                                   // the owner's diagnosis taken in, or rebuild()
        virtual bool notRecordedHere() const = 0;                    // in progress by its file, not recorded here
        virtual void freezeBaselineIfNeeded() = 0;                   // SessionDiagnostics' (the one rule: 3.7-e)
        virtual void wake() = 0;                                     // a view is bound again: mirror what was owed
        virtual void viewed(bool on) = 0;                            // the first view bound / the last dropped (3.7-e)
    };
private:
    friend class SessionDiagnosticsOwnerLink;   // session_diagnostics_adapter.cpp
    friend class SessionDiagnosticsAdapters;    // makes the adapters
    SessionDiagnosticsModel(pinpoint::Session *session, const QString &surface);   // ibid.
    // The common part of both (the pack, the norms, the pools): no model is made any other way (refactor 5.4b: the old
    // engine's own model went with it).
    explicit SessionDiagnosticsModel(QObject *parent);
    // What activateSession() is for an adapter, which never re-reads its session: the profile and the history read,
    // the owner's club baseline at first need asked again (the one rule: a freeze landed elsewhere is read, else frozen
    // from the summaries when computable: 3.7-e); then the owner's diagnosis shown.
    void pointAdapter();
    // The reasons, the carry today and the club mix, worded from the owner's typed results (ensureOutcomes).
    void composeOwnerOutcomes(QHash<QString, int> &reasonIndex) const;
    // The shot log and the session's mix, worded from m_composed (both halves).
    void composeShotLog(const QHash<QString, int> &reasonIndex) const;
    bool    m_adapter = false;
    QString m_adapterSurface;
    std::unique_ptr<AdapterLink> m_link;   // null: the empty adapter (no Session)
    // The owner's last applied diagnosis as the adapter took it (mirrorOwner): what its rows were reduced from, its
    // composed outcomes (aligned with m_shots) and its typed results.
    std::shared_ptr<const pinpoint::analysis::SessionReductionInput> m_ownerInput;
    pinpoint::analysis::SessionResults             m_ownerResults;
    std::vector<pinpoint::analysis::ShotOutcome>   m_ownerComposed;
    bool m_adapterBusy = false;
    bool m_adapterLoading = false;   // loading(): set by the adapter's link (updateBusy)
    bool m_adapterBaselineWaiting = false;   // baselineWaiting(): ditto
    int  m_views = 0;

    // The content every row is graded against — pack, norms and contexts — as one short hash (contentStamp()).
    QString computeContentStamp() const;

    // Everything derived, from the rows and nothing else. Cheap enough to do wholesale —
    // tens of shots by ~150 conditions is arithmetic no profiler will find — so there are no
    // running totals to keep correct across a back-fill or a re-activation.
    void rebuild();
    // The session-scope conditions as ledgers, appended to m_ledgers (§10.8 E2).
    void appendSessionScope();

    // The pack's causal graph, marshalled into the ledger's pack-agnostic NodeSpec/EdgeSpec.
    // See the .cpp for WHICH conditions are marshalled and why it is not all of them.
    void buildGraph();

    // buildGraph()'s second half, on any node set: pack order in, NodeSpec/EdgeSpec out. Shared
    // with conditionDetail(), which needs the SAME marshalling over a different neighbourhood —
    // a detail's downstream reaches past the session's own graph, and a second marshaller would
    // be a second answer to "is this node measurable on this capture".
    void marshalGraph(const QSet<QString> &keep,
                      std::vector<pinpoint::analysis::NodeSpec> &nodes,
                      std::vector<pinpoint::analysis::EdgeSpec> &edges) const;

    // explain() over the pattern-tier set, run only when that set's membership changes.
    void refreshExplanation();

    // hystereticOrder()'s input: a display score of this session's evidence alone.
    std::vector<pinpoint::analysis::RankedCondition> rankScores() const;

    // ── The golfer's word, forwarded (refactor 3.4b-i; 5.4b: the old engine's own writes went with it) ──────────────
    // The session-level members as session_input.json holds them (all four fields): the `held` a forwarded edit carries.
    pinpoint::SessionInput sessionInputOf() const;
    // A forwarded edit that failed, or reached no Session (DEC-126): said once (ledgerWritesBlocked, the panel's notice),
    // and taken back (ledgerWritesResumed) by the next one that lands.
    void         reportWriteFailure(const QString &what, const QString &why);
    void         reportWriteSuccess();
    // The golfer's session-level input through the Session's commands (EVT-10, EVT-08): the field `changed` (for a
    // screen answer, `screenId`'s entry). Nothing is written when no Session is reached; the golfer is told.
    enum class InputField { Focus, Miss, Screen, Run };
    void         writeSessionInput(InputField changed, const QString &screenId = QString());
    // One shot's intent or corrections through the Shot's commands (EVT-07, EVT-06), by its ShotRef (this session's id
    // and its swing number), saved on this model's save lane (SaveLater: D25, DEC-183), inline when synchronous.
    // `correction`: the label correction just made (correctOutcome), for the tuning log the Shot's Athlete appends.
    void         writeShotInput(int shotId, bool intent, bool corrections,
                                const pinpoint::LabelCorrectionNote *correction = nullptr);
    void         onShotInputWritten(const pinpoint::WriteResult &result);

    // ── The owner's club baseline, as its diagnosis carries it ─────────────────────────────
    void         adoptClubBaseline(const QHash<QString, pinpoint::analysis::ClubBaselineEntry> &clubs);
    const pinpoint::analysis::ClubBaselineEntry *usualFor(const QString &club) const;
    QJsonObject  shotMeasures(const pinpoint::analysis::ShotRecord &s) const;

    // ── Shot outcomes, composed lazily once per reduction ────────────────────────────
    void         ensureOutcomes() const;
    QString      intentFor(int shotId) const;
    pinpoint::analysis::ShotOutcomeSessionFacts outcomeSessionFacts() const;
    QVariantMap  outcomeLabelMap(const pinpoint::analysis::ShotOutcomeLabel &l, const pinpoint::analysis::ShotRecord &s,
                                 const pinpoint::analysis::ClubBaselineEntry *usual) const;

    // ── Surface builders (one per zone; all pure over the reduction) ────────────────
    void buildHeader();
    void buildThisShot();
    void buildCards();
    void buildChains();
    void buildDriver();
    void buildBookends();
    void buildDetail();

    // ── The card, node, link and rival maps — ONE marshaller each ───────────────────
    //
    // Extracted out of buildCards()/buildChains()/buildDriver() rather than copied into the
    // detail: the detail draws the same pattern card, the same node cards and the same graded
    // link strokes as the panel behind it, and two marshallers would be two chances for the same
    // link to read "Coherent" on one surface and "Present together" on the other. Everything they
    // publish is a function of (ledger, node spec, link evidence, focused shot) and nothing else.
    QVariantMap cardMap(const pinpoint::analysis::ConditionLedger &l,
                        int focusIdx, int selectedTick) const;
    QVariantMap nodeMap(const QString &id, const pinpoint::analysis::NodeSpec *ns,
                        pinpoint::analysis::ChainNodeKind kind,
                        const std::vector<pinpoint::analysis::LinkEvidence> &links,
                        int focusIdx, int selectedTick) const;
    QVariantMap linkMap(const pinpoint::analysis::LinkEvidence &e) const;
    QVariantMap rivalMap(const pinpoint::analysis::RivalParent &rp) const;
    // The node's honesty mark, in the model's words. Generalised past the declared miss: ANY
    // outcome-kind condition can be the end of a detail's downstream, and one whose rows the
    // launch monitor verified says so on the same terms the declared miss always has.
    QString markFor(const QString &id, pinpoint::analysis::ChainNodeKind kind,
                    const pinpoint::analysis::NodeSpec *ns) const;
    // Why a node carries no reading — five answers off the pack's own MeasureStatus, because
    // "the measure is planned" and "this capture did not answer it" are different facts and only
    // one of them is anybody's fault. See the .cpp: this is the bug hanging_back exposed.
    QString ghostMark(const QString &id, const pinpoint::analysis::NodeSpec *ns) const;
    // id -> "#3=" for the ranked card row. Rebuilt in buildCards(), read by cardMap() — which
    // the condition detail also calls, so a card and its own page cannot disagree about where
    // it sits. Empty for anything not on the ranked row.
    QHash<QString, QString> m_rankText;
    // The two filter facts, per ranked condition. Rebuilt in buildCards() beside the badge,
    // because they are answers about the SESSION's pattern set and change with it.
    QHash<QString, bool> m_reachesBall;
    QHash<QString, bool> m_rootHere;
    // The first reason this session gave for withholding a condition, in the rows' own
    // mechanical vocabulary. Empty when it never had a row.
    QString withheldReason(const QString &id) const;
    // One line of evidence prose: the contingency sentence where an authored parent was actually
    // tested, the pack's consequence line otherwise. Never both, never manufactured.
    QString evidenceProse(const QString &id,
                          const std::vector<pinpoint::analysis::LinkEvidence> &links) const;

    // ── Small shared formatters, so an export and the screen cannot disagree ────────
    QString conditionName(const QString &id) const;
    QString consequenceOf(const QString &id) const;
    QString measureLabelOf(const QString &measureId) const;
    QString measureUnitOf(const QString &measureId) const;
    QString measurePhaseOf(const QString &measureId) const;
    QVariantList ticksFor(const pinpoint::analysis::ConditionLedger &l, int selectedIndex) const;

    // ── How far (session_spread.h) ──────────────────────────────────────────────────
    // The corridor a condition's readings are drawn against: the norm resolved for the focus
    // shot's context (else the session's commonest), from the pack's measure shape and the
    // session's grade policy — and only when no norm resolves, the Ideal band the row stored.
    // `normInfo` gets what the corridor is (source, n, citation) for the tag.
    pinpoint::analysis::SpreadCorridor corridorForRow(const pinpoint::analysis::ConditionRow &r) const;
    pinpoint::analysis::SpreadCorridor spreadCorridorOf(const pinpoint::analysis::ConditionLedger &l,
                                                        int focusIdx, QVariantMap *normInfo,
                                                        const QString &measureId = QString()) const;
    // `measureId` empty = the ledger's driving measure; otherwise that measure's own readings
    // (ConditionRow::readings), each shot drawn by that measure's own verdict.
    QVariantMap spreadFor(const pinpoint::analysis::ConditionLedger &l, int focusIdx,
                          int selectedTick, const QString &measureId = QString()) const;
    QVariantMap historyFor(const pinpoint::analysis::ConditionLedger &l, int focusIdx,
                           const QString &measureId = QString()) const;
    // The distinct measures a condition's signals read, in pack order.
    QStringList conditionMeasures(const QString &conditionId) const;

    // ── The variability marks on the strip and the history (docs/design/system/variability.md) ──
    //
    // TECHNICAL LAYER, PAIRED: the box and the per-session boxes stay exactly as drawn; beside them, a
    // consistency grade (src/Metrics/variability/) per club, never across clubs (Mark's ruling R2b).
    //   spread.clubBadges   [{ club, n, result }]   one per club with a reading, perClubInputs' order
    //                                               (readings descending); `result` a variabilityResult
    //   history.funnels     [{ club, n, sessions[{ rho, grade, breakBefore }], trend, basisText,
    //                          measureName }]      one per club of THIS session with ≥ N_MIN readings
    // The scale is the measure's NORM corridor half-width (σLo + σHi)/2 in the club's context, as it
    // grades today. UNGRADED — nothing published, nothing in its place: a measure no corridor signal
    // of the condition reads (a threshold test's fault line, an order, a ratio), a composite, or a
    // club whose context resolves no norm with a σ. No propagated σ: the ledger rows carry none per
    // reading, so the measurement-limited test (R3) has nothing to compare against here.
    bool    consistencyGradable(const pinpoint::analysis::ConditionLedger &l, const QString &measureId) const;
    double  consistencyScaleOf(const QString &measureId, const QString &contextId) const;
    QVariantList spreadClubBadges(const pinpoint::analysis::ConditionLedger &l, const QString &measureId,
                                  const std::vector<double> &values, int decimals, const QString &unit) const;

    // ── A composite's evidence (posture_metrics_design.md §8, principle 5) ──────────
    //
    // A COMPOSITE's number is never shown. Every surface that would print a row's value prints the
    // DOMINANT PART's change instead (its own unit, its golferName), and the detail opens on
    // "What changed": one line per part, built here so the three registers draw one answer.
    //   whatChanged   { mode: coherent|distance, startWord, endWord, rows[{ name, measureId, read,
    //                   startText, endText, changeText, valueText, idealText, unit, grade, share,
    //                   shareWord, dominant }], dominant{ name, changeText, figure, unit, measureId, valueText },
    //                   partsRead, partsTotal, coverageText, trustLines[{ kind, text }] }
    //   Empty for a row whose measure is not a composite.
    bool        isCompositeMeasure(const QString &measureId) const;
    // …or the ledger's rows say so: a row graded as a composite carries its parts (schema 9).
    bool        readsComposite(const pinpoint::analysis::ConditionLedger &l, const QString &measureId) const;
    QVariantMap whatChangedOf(const pinpoint::analysis::ConditionRow &r) const;
    // The card's "how far" for a composite measure: the dominant part's figures in place of the
    // composite's, the counts the strip states, and NO dots, run, bands, ticks or box — a run of
    // composite readings would put its number on screen (§12: no z-unit curve).
    QVariantMap compositeSpreadFor(const pinpoint::analysis::ConditionLedger &l, int focusIdx,
                                   const QString &measureId) const;
    // A part's grade against its OWN norm, at the row's context: the band word (ideal | good |
    // watch | action | open, "" without a corridor), the axis's decimals, and its Ideal band.
    struct PartBand { QString grade; int decimals = 1; QString idealText; };
    PartBand partBandOf(const QString &measureId, const QString &contextId, double value) const;
    const pinpoint::analysis::ConditionLedger *ledger(const QString &id) const;
    int  indexOfShot(int shotId) const;
    // Which shot the panel is talking about: the selected one while reviewing, the newest
    // one live. One answer, in one place.
    int  focusIndex() const;
    // Which shot the CONDITION DETAIL is about: the carousel's pick whenever the ledger holds it —
    // live as well as in review — and focusIndex() otherwise. The panel's own zones keep
    // focusIndex(): live, their tense is "the swing just struck". The detail is a page the golfer
    // opened to read a swing they chose, and it must follow that choice.
    int  detailFocusIndex() const;
    // The stage the panel DRAWS. `m_stage` is the session's own ratcheted stage; reviewing a
    // session shows it frozen at Closing without being able to close it — see rebuild().
    pinpoint::analysis::Stage effectiveStage() const;

    // ── Owned registries ────────────────────────────────────────────────────────────
    // The pack: a view of RuleData's current one, made again (with m_outcomePack) when a saved pack edit (EVT-13) has
    // published another — at the next rebuild() and at once on the event (followPackSave, refactor 5.7e). m_packOf is the
    // provider it was made over, held, the key. Only on the GUI thread.
    std::unique_ptr<pinpoint::analysis::ICharacteristicPackProvider> m_packProv;
    std::shared_ptr<const pinpoint::analysis::ICharacteristicPackProvider> m_packOf;
    bool    followPackSave();
    std::shared_ptr<const pinpoint::analysis::INormProvider>         m_norms;
    // The shared norm set's generation m_norms was taken at (sharedNormProviderGeneration()). A saved
    // corridor edit moves it; the next rebuild() re-takes the set (followNormSave), the content stamp
    // moves with the norms it hashes, and the rows graded under the old set regrade through the
    // existing stale-stamp path. Only on the GUI thread, with no detection job in flight.
    quint64 m_normsGeneration = 0;
    bool    followNormSave();
    QString m_policyName = QStringLiteral("standard");

    // ── The evidence ────────────────────────────────────────────────────────────────
    std::vector<pinpoint::analysis::ShotRecord> m_shots;
    pinpoint::analysis::LedgerOptions           m_opt;
    pinpoint::analysis::ExplainOptions          m_explainOpt;
    bool m_withSigma = pinpoint::tuned::diagUncertainty::kEnabled;   // §A8.3 — readings carry σ
    QSet<int> m_ingested;      // shot ids already in the ledger or in flight
    QSet<int> m_lmShots;       // shot ids whose capture carried launch-monitor data
    QHash<int, QString> m_swingDirs;

    // ── The reduction ───────────────────────────────────────────────────────────────
    std::vector<pinpoint::analysis::ConditionLedger> m_ledgers;
    QVariantMap                                      m_sequenceConsistency;   // appendSessionScope()
    std::vector<pinpoint::analysis::NodeSpec>        m_nodes;
    std::vector<pinpoint::analysis::EdgeSpec>        m_edges;
    std::vector<pinpoint::analysis::LinkEvidence>    m_links;
    pinpoint::analysis::ChainRails                   m_rails;
    pinpoint::analysis::Coverage                     m_coverage;
    pinpoint::analysis::Explanation                  m_explanation;
    QStringList m_patternSet;      // the set explain() last ran over
    // A new diagnosis of the session has landed since explain() last ran (refactor 5.4x): at Closing the footer is
    // explained again from it, whatever the membership (refreshExplanation; §B7 "definitive").
    bool        m_explainOwed = true;
    QStringList m_displayOrder;    // hystereticOrder()'s previous order — the anti-churn state

    // ── Lifecycle + intent ──────────────────────────────────────────────────────────
    QString m_sessionDir;
    // WHERE THIS SESSION SITS: its identity (id, local day, type), read once per activation through the
    // session store (SessionDiagnosticsFiles::identityOf; step 2.7a, audit F-06/C-11). The session's day is
    // its start time's, and for a session without a session file its name's — read by SessionIdentity,
    // never parsed here. Empty with no session held; a null id (and still a day) for a legacy folder no
    // athlete can be named for.
    pinpoint::SessionIdentity m_sessionIdentity;
    pinpoint::analysis::Stage m_stage = pinpoint::analysis::Stage::Cold;
    // DID THIS SESSION EVER ACTUALLY ESTABLISH? `m_stage` cannot answer it once the session is
    // closed, because sessionStage() returns Closing outright for a closed session and the
    // Forming/Established distinction is lost with it. The panel needs the distinction: the
    // chain rail is the composition of a session that EARNED a chain, and a Forming session
    // reviewed at its close must keep the flat pattern cards it had (design 12a: "Forming,
    // flat cards, no chain because these two patterns share no authored edge"). Ratcheted the
    // same way the stage is, and persisted with it so reloading a closed session agrees.
    bool    m_reachedEstablished = false;
    bool    m_closed = false;
    bool    m_reviewing = false;
    bool    m_destroying = false;       // ~SessionDiagnosticsModel: no notice from a model being torn down
    bool    m_writeFailing = false;      // reportWriteFailure said so; the next good write takes it back
    // The forwarded edit's save lane (SaveLater; D25, DEC-183): a shot's edits land in the order they were made, off the
    // GUI thread. maxThreadCount 1.
    QThreadPool m_reviewPool;
    int     m_selectedShotId = -1;
    QString m_selectedSwingDir;
    QString m_selectedShot;
    // Re-points m_selectedShotId at m_selectedShot (else m_selectedSwingDir) without a rebuild of its own — for the
    // callers that are about to rebuild anyway. True when the id changed.
    bool    resolveSelectedSwingDir();
    QString m_cadence = QStringLiteral("bandwidth");
    QString m_focusConditionId;
    int     m_focusFromShot = -1;
    QString m_declaredMiss;
    QVariantList m_missChain;
    QHash<QString, bool> m_screens;


    // ── Published surface ───────────────────────────────────────────────────────────
    QVariantMap  m_headerInfo;
    QVariantList m_thisShot;
    bool         m_quiet = false;
    QVariantMap  m_afterShotDelta;
    QVariantList m_cards;
    QVariantList m_watching;
    QVariantList m_chains;
    QVariantList m_unchained;
    QString      m_unchainedLine;
    QVariantMap  m_driver;
    QString      m_coverageLine;
    QVariantList m_notJudged;
    QString      m_notJudgedLine;
    QVariantList m_bookends;
    // Which condition the drill-in is open on, and its map. Panel-local in every sense that
    // matters — never persisted, never in the envelope, cleared on activation: which page was
    // being read is not a fact about the session.
    QString      m_detailConditionId;
    QString      m_detailMeasureId;     // the tab picked; empty = the default
    QVariantMap  m_detail;

    // ── The same golfer's other sessions (historyOf) ────────────────────────────────
    //
    // READ-ONLY, OFF THE GUI THREAD, AND NOT PART OF THE EVIDENCE. Each sibling session's ledger
    // is reduced to the readings it holds, per condition and per driving measure — the values,
    // never the verdicts: those were graded against whatever the norms were on that day, and the
    // columns are drawn against the bands as they stand today so that every column is measured
    // with one ruler. Nothing here reaches a tier, a count or this session's diagnostics.json.
    // `club` and `contextId` for the funnel (one series per club, a break where the context changes).
    struct HistoryReading { QString measureId; double value = 0.0; QString club; QString contextId; };
    struct HistorySession {
        QString sessionDir;
        QString label;
        qint64  firstMs = 0;
        QDate   day;          // SessionIdentity's local day (step 2.7a); invalid when it has none
        int     nOfDay = 0;   // its place in that day; 0 when not placed
        QHash<QString, std::vector<HistoryReading>> byCondition;
    };
    // `selfNOfDay`: this session's own place in its day, from the same listing of the athlete's sessions.
    static std::vector<HistorySession> readHistory(const QString &sessionDir, int *missing, int *selfNOfDay);
    void scanHistory();
    void applyHistory(int generation, const std::vector<HistorySession> &sessions, int missing, int selfNOfDay);
    std::vector<HistorySession> m_history;
    int m_historySelfNOfDay = 0;

    // ── The owner's club baseline, as its applied diagnosis carries it (mirror) ──
    bool m_baselineFrozen = false;
    QHash<QString, pinpoint::analysis::ClubBaselineEntry> m_baselineClubs;
    std::shared_ptr<pinpoint::analysis::IClubBaselineProvider> m_clubBaseline;

    // ── Corrections and intent (envelope; never a row) ───────────────────────────────
    QHash<int, std::vector<pinpoint::analysis::ShotCorrection>> m_corrections;
    QHash<int, QString> m_shotIntent;
    int     m_intentFromShot = -1;
    QString m_intentFromShape;

    // ── Composed on first read after each reduction (ensureOutcomes) ─────────────────
    mutable bool m_outcomesDirty = true;
    pinpoint::analysis::ShotOutcomePack m_outcomePack;
    mutable pinpoint::analysis::TendencySessionEvidence m_outcomeEvidence;   // grade > Unanchored only
    mutable std::vector<pinpoint::analysis::NodeSpec>     m_outcomeNodes;
    mutable std::vector<pinpoint::analysis::LinkEvidence> m_outcomeLinks;     // every graded link, for the detail
    mutable std::vector<pinpoint::analysis::ShotOutcome> m_composed;     // aligned with m_shots
    mutable QVariantList m_shotLog;
    mutable QVariantMap  m_shotMix;
    mutable QVariantList m_reasons;
    mutable QVariantList m_carryToday;
    mutable QVariantList m_clubMix;
    int  m_historyMissing = 0;      // sibling sessions with swings and no ledger yet
    bool m_historyLoading = false;
    int  m_historyGen     = 0;      // a scan for a session no longer loaded is discarded on arrival

    // ── Threading ───────────────────────────────────────────────────────────────────
    // A pool of one for the history scan, so reading other sessions never waits behind a shot's save.
    QThreadPool m_historyPool;
    bool        m_synchronous = false;
};

