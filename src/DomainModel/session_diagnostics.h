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

// SessionDiagnostics — THE SESSION'S DIAGNOSTICS OWNER, one per Session, its child (refactor step 3.5-i;
// build/refactor/step-3/BRIEF.md §4.4, §4.5; docs/developer/information_model.md CLS-SESSION-DIAG,
// OBJ-SESSION-DIAG, FILE-SESS-03, M-SHOT-LAYERS, R-STAMP-1…3, R-CON-2…5; DEC-100, DEC-124, DEC-130, DEC-135,
// DEC-141, DEC-190, DEC-191).
//
// WHAT IT DOES (run 3.5-i: the owner and the re-fire; run 3.5-ii: the diagnosis on the lane, the typed summary,
// the persisted derived data and the parallel run):
//   • holds THE FIRING SNAPSHOT (FiringSnapshot below): one shared_ptr<const>, OWNING the pack and norms of a
//     RuleData snapshot (R-CON-4), the grade policy and SET-06 from that same snapshot (never a live read: audit
//     D-12), the flight table, the session's frozen club baseline (club_baseline.json, FILE-SESS-04), the session's
//     usuals per club, the athlete's record facts its norm cohort is read from, on each shot's own date (D6: DEC-171),
//     and the golfer's input resolved by the one J-33 rule (golfer_input_rules.h: the new home, else the older copy).
//     An input change builds a new one; an old one lives while a job holds it (DEC-110). The findings stamp
//     (firingStamp over what fireFindings reads) is computed once per change of those inputs, not per shot.
//   • KEEPS each shot's findings and derived data (DEC-135), stamped (R-STAMP-1), as the shots' asks come back.
//   • COMPUTES THE USUALS on its own (GUI) thread from the readings in the kept findings, never from findings
//     (DEC-130: sessionFlightUsuals), and compares them field by field after every change. ONLY A CHANGED VALUE
//     builds a new snapshot, and a usual's change RECOMPUTES THAT CLUB'S SHOTS' DERIVED DATA ONLY, never their
//     findings: fireFindings reads no usual (Mark, 16 Nov 2026: DEC-191). The chain ends: a derive changes no
//     reading, so the recomputed usual has the same value and nothing more is asked.
//   • RUNS §4.4 STEPS 1–6 AND 8–11 on the one pool (src/Core/work_pool.h; D15): each ask takes the next number of
//     the one process-wide counter and records it on its Shot as the Shot's latest (§4.4 step 3); its job holds
//     values only — the snapshot pointer, the ShotRef, the reader — and runs on THIS SESSION INSTANCE'S SERIAL
//     LANE (keyed by the instance's own number), at Low priority (D24), the live session's lane in front;
//     its result is posted to the composition root's receiver, which resolves the session by id and checks the
//     instance, the shot and the ask (§4.4 step 6; diagnosticsWorkTarget below) before it is handed here.
//     A FAILED READ COMES BACK UNREAD: nothing kept, the earlier kept result stays, the shot marked unread — a mark
//     PERSISTED as one key in diagnostics.json (DEC-191) — and re-asked at the next load; one line in the app log.
//     EVERY RESULT, KEPT OR DROPPED, SETTLES ITS BATCH (batchSettled()).
//   • WRITES WHAT IT KEPT (§4.4 step 8) through SessionRepository's id-keyed diagnostics methods, on its own
//     thread: debounced after a batch settles; at once when its Session is released (Athlete::releaseSession,
//     Session::releasing); and when it is destroyed with its Athlete (the app's quit). A failed write is said
//     (writeFailed) and logged (DEC-126). A newer build's file, or one that cannot be opened, is read and never
//     written (R-PERS-3). ⚠ RUN 3.5-i: ONLY WITH DiagnosticsDoors::persist — the old model stays diagnostics.json's
//     one writer until the cut-over (step 3.7); in the app nothing attaches a SessionDiagnostics yet.
//   • LOADS (§4.4 step 10): reads diagnostics.json, puts each kept row back as fired (the file holds rows checked
//     against the flight), rebuilds the usuals from the readings it holds BEFORE any shot is asked, then asks every
//     shot that has no kept result, is marked unread, or whose stamp is stale (its findings stamp, or its document's
//     size and mtime: R-STAMP-2). Each kept shot's DERIVED DATA is persisted with its findings (DEC-135, FILE-SESS-03:
//     the session block's `derived`, ids and numbers only, R-DER-9), stamped with what it was derived from; a load
//     takes it back when that stamp still holds and derives only the others again (run 3.5-ii).
//   • DIAGNOSES THE SESSION (§4.4 step 7; run 3.5-ii): when a batch settles, it copies what it keeps — the rows
//     checked against the usual flight, each shot's derived data and word, the usuals, the baseline, the golfer's
//     session-level word, the stage held — into one immutable value and posts reduceSession()
//     (src/Diagnostics/engine/session_reduction.h) to the same lane, under the next number of the one counter; the
//     receiver keeps only the latest (an older reduction is dropped as superseded), and the owner applies it: the
//     ledgers, the stage ratchet, the typed results (src/Coaching/session_results.h: work-ons, evidence, reasons,
//     each shot's "because" (J-14 (b)), per club the usual, carry today, mix and baseline window (D7)). The session's
//     status is read from the Session, its one writer (J-24): an ended session's diagnosis is closed.
//   • THE SUMMARY (FILE-SESS-05, DEC-55, DEC-192): summary() is the session's typed results with their provenance
//     (derivedFrom: its fingerprint over what it was worked out from); written to work_ons.json with what it keeps,
//     when it persists — THIS OBJECT IS THAT FILE'S ONE WRITER (step 3.6; in the app from the catch-up's cut-over, 3.7).
//   • THE CLUB BASELINE AT FIRST NEED (step 3.6; DEC-46, DEC-172): at load, a current club_baseline.json is read;
//     none, or an older rule's, is frozen from the athlete level's computation over the predecessors' summaries
//     (AthleteDiagnostics::clubBaselineFor) — through Session::freezeClubBaseline when persisting (EVT-16 on a rewrite),
//     in memory only when not — or not at all while it is not computable (loadOrFreezeClubBaseline).
//   • THE BOARD'S ANNOTATIONS (step 3.6; Stage 0.5's follow-up): the reduction reads each shot as the golfer annotated
//     it on the launch monitor board (Coaching/board_annotations.h), asked of the Shot (its summary).
//   • HEADLESS (§4.4 step 11; R-HL-2): DiagnosticsDoors::synchronous runs the same jobs on the calling thread and
//     hands each result to WorkReceiver::accept() itself; the same checks apply.
//
// THE DERIVED DATA (layer 3) is the shot's alone, with the context its session hands down: its rows checked
// against the session's usual flight (checkShotAgainstFlight), its outcome (labels, carry against the frozen usual,
// story) and its three factors. The outcome's "because" — which habit lay behind the miss — reads the SESSION'S
// links and tiers, so it is the session's to compose, in its diagnosis (run 3.5-ii's reasons), not the shot's: J-14
// (b), the lead's call under Mark's three-level principle (NOTES-3.5-i.md J3). So deriveShotData is called here
// with an empty session diagnosis.
//
// Ownership (information_model.md §10): owner SessionDiagnostics; level session; one writer (DEC-55, DEC-135):
// this object; persists in FILE-SESS-03 through SessionRepository; a view reaches it through its Session
// (Session::diagnostics()), until step 4 through the thin adapter (DEC-165).
//
// NEVER: fires a finding itself (the job runs the Rules ring's fireFindings), writes a shot's files, reads another
// session (CLS-SESSION-DIAG).
//
// Domain model ring (src/DomainModel): names no store type.
//
//   cmake --build build/tests --target session_diagnostics_test -j 6
//   ctest --test-dir build/tests -R '^session_diagnostics_test$' --output-on-failure

#include "athlete.h"          // DiagnosticsDoors
#include "firing_reader.h"
#include "rule_data.h"
#include "golfer_input_rules.h"
#include "../Coaching/board_annotations.h"   // BoardAnnotation (step 3.6)
#include "session_input.h"    // IntentRun
#include "../Coaching/shot_outcomes_pack.h"
#include "../Core/work_pool.h"
#include "../Diagnostics/engine/fire_findings.h"
#include "../Diagnostics/engine/session_reduction.h"
#include "../Coaching/session_day_readings.h"   // the summary's day readings (refactor 5.5d)
#include "../Identity/identity.h"

#include <QDate>
#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace pinpoint {

class Session;
class Shot;

// ── §4.4 step 1: the snapshot ───────────────────────────────────────────────────────────────────────────────
// Immutable once made; a job holds it by shared_ptr<const>. It carries no number that drops results.
struct FiringSnapshot {
    std::shared_ptr<const RuleDataSnapshot> rules;            // OWNS the pack and norms below (R-CON-4)
    // pack, norms, policy, SET-06, flight, baseline, usuals, σ. Its `cohort` is NOT the shot's: each fire sets it from
    // the record below on the shot's own date (DEC-171; cohortOnShotDate).
    analysis::FiringInputs                  inputs;
    std::shared_ptr<const analysis::ShotOutcomePack> outcomePack;   // from inputs.pack (3.3 review 1, note 2)
    // The athlete's record facts the cohort is read from (D6: DEC-171), and the session's day (the fallback date).
    QDate                                   dob;
    QString                                 sex;
    QDate                                   sessionDay;
    // The golfer's input the derived data reads, resolved by golfer_input_rules.h's one rule (J-33): the session's
    // intent run (new home, else the older copy) and diagnostics.json's older per-shot copy, which each shot's review
    // overrides field by field in its job.
    IntentRun                                             intentRun;
    QHash<int, QString>                                   legacyIntents;
    QHash<int, std::vector<analysis::ShotCorrection>>     legacyCorrections;
    QString                                 findingsStamp;    // firingStamp over what fireFindings reads (R-STAMP-1)
    quint64                                 derivedGeneration = 0;   // moves with every layer-3 input but the usuals
};

// THE COHORT ON THE SHOT'S DATE (DEC-171: "from the athlete record on the shot's date"): the shot's capture instant's
// local date (FiringRead::captureMs: clock.wallclock, never the file's mtime), else (no instant) the session's day.
QDate            shotCohortDate(qint64 timestampMs, const QDate &sessionDay);
analysis::Cohort cohortOnShotDate(const QDate &dob, const QString &sex, qint64 timestampMs, const QDate &sessionDay);

// One shot's layer 3, with what it was computed against.
struct ShotDerived {
    analysis::ShotRecord      checked;   // the findings' rows checked against the session's usual flight
    analysis::ShotDerivedData data;      // outcome (without the session's "because": J-14 (b)) and the three factors
    analysis::ClubFlightUsual usual;     // its club's usual it used (NaN where the club had none)
    // The part of the flight table its club reads, as it used it (analysis::flightNormsStampFor; refactor 6.3a): a
    // flight-table edit re-derives exactly the shots whose key moved (R-STAMP-1).
    QString                   flightKey;
    quint64                   generation = 0;   // the snapshot's derivedGeneration it used
    // The golfer's word it was computed with, resolved (its review over the older copy; the run for the intent).
    QString                               intent;
    std::vector<analysis::ShotCorrection> corrections;
};

// A derive that failed (its job threw): not asked again until one of its inputs moves (review 1 of 3.5-i, C3).
struct DeriveFailure {
    analysis::ClubFlightUsual usual;
    QString                   flightKey;
    quint64                   generation = 0;
    QString                   reason;
};

// One shot's kept result (DEC-135).
struct KeptShot {
    analysis::ShotRecord findings;          // AS FIRED: the rows before the flight check
    bool                 hasLaunchMonitor = false;
    DocumentStamp        document;          // the document's stamp, taken before its read (R-STAMP-2)
    QString              findingsStamp;     // the snapshot's findings stamp it fired under (R-STAMP-1)
    analysis::Cohort     cohort;            // the norm cohort it fired for (its date's; in memory only: the stamp holds its facts)
    std::optional<ShotDerived> derived;     // none until its layer 3 is computed (never persisted)
    // The shot's review as last read (its fire, or a derive of a result loaded from the file, which holds none):
    // none until read, so the next derive reads it on the worker (C1).
    std::optional<ShotReview>    review;
    std::optional<DeriveFailure> deriveFailed;   // its last derive threw, under these inputs (C3)
    // ITS DELIVERY READINGS for the summary's day readings (refactor 5.5d; DEC-74), off its phase grid: handed over by its
    // fire (the grid it read), else read once on the lane by the next reduction (a result loaded from the file). In
    // memory only: never in diagnostics.json. `deliveryRead` false: not read yet for `document`; then `delivery` none.
    bool                                       deliveryRead = false;
    std::optional<analysis::ShotDeliveryReading> delivery;   // none: it read neither a swing plane nor the monitor
};

// THE SUMMARY'S DAY READINGS, ASKED WITH A REDUCTION (refactor 5.5d; DEC-74): what the reduction was asked with — each kept
// shot's delivery reading (or the shot to read it from: a result loaded from the file), its readings at address off its
// rows as diagnostics.json holds them (checked against the flight), and the cameras (or the shots to read them from) —
// made into the summary's day readings on the lane. Values and the firing reader only (any thread).
struct DayReadingsAsk {
    struct Shot {
        int                                          swing = 0;
        ShotRef                                      ref;
        DocumentStamp                                document;
        bool                                         read = false;   // `delivery` is known; else read it on the lane
        std::optional<analysis::ShotDeliveryReading> delivery;
    };
    std::vector<Shot>                          shots;     // strike order
    std::vector<analysis::ShotAddressReading>  address;   // strike order
    bool                                       camerasKnown = false;   // `cameras` is the kept one; else read on the lane
    analysis::SessionCameras                   cameras;
    QString                                    camerasKey;
    std::shared_ptr<const FiringReader>        reader;
    std::shared_ptr<const analysis::ICharacteristicPackProvider> pack;
};

class SessionDiagnostics : public QObject
{
    Q_OBJECT
public:
    // The Session's SessionDiagnostics, made (and loaded) on the first call: its child, one per Session instance.
    // What DiagnosticsDoors::attach calls. Null when the doors are not usable (no receiver, rule data or reader;
    // no pool unless synchronous).
    static SessionDiagnostics *attach(Session *session, const DiagnosticsDoors &doors);
    ~SessionDiagnostics() override;

    // THE FINGERPRINT A SESSION'S SUMMARY WOULD CARRY IF IT WERE WRITTEN NOW, for a session not in hand (REVIEW-f R1-B1:
    // the athlete level's baseline freeze reads only fresh summaries, BRIEF-f 13): the session level's own rule
    // (session_reduction.h summaryFingerprintOf, as summaryFingerprint() and the catch-up's scan take it) over its
    // numbered swings' document stamps (the firing reader's stat), its frozen club baseline (through the repository), the
    // rule data's summary stamp (`rulesStamp`: summaryRulesStamp, the caller's, once per snapshot) and the cohort facts.
    static QString summaryFingerprintNow(SessionRepository &sessions, const FiringReader *reader, const SessionId &id,
                                         const QString &rulesStamp, const QString &cohort);

    SessionId sessionId() const { return m_id; }
    // This instance's number from the one counter (§4.4 step 3), taken when it was loaded; its lane's token.
    quint64   instance() const { return m_instance; }
    std::shared_ptr<const FiringSnapshot> snapshot() const { return m_snapshot; }

    // What it keeps (owner thread).
    const KeptShot *kept(int swingNumber) const;
    QList<int>      keptShots() const { return m_kept.keys(); }
    QSet<int>       unread() const { return m_unread; }
    const QHash<QString, analysis::ClubFlightUsual> &usuals() const { return m_usuals; }
    int             outstanding() const { return m_outstanding; }   // asks not yet come back
    // THE SHOT'S FLIGHT CHECK (DEC-122; refactor 6.3a): its rows as checked against the session's usual flight — the held
    // check of its current derived data (ShotDerived::checked), else (none current: a derive in flight or failed) the one
    // rule over its findings, now. What diagnostics.json's ledger, the diagnosis and the parity dump read; no reader
    // runs the check itself. Empty for a shot with nothing kept.
    analysis::ShotRecord checkedRows(int swingNumber) const;
    bool            readOnly() const { return m_readOnly; }
    // WHAT THE LOAD FOUND OF diagnostics.json (refactor 5.5a; J-74 Q2): the state this object already holds, read only —
    // for a screen that says why nothing is graded yet (the Charts Wrist sections' "not graded" sentence).
    enum class FileState { Missing, Read, Newer, Unopenable, Unparseable };
    FileState       fileState() const { return m_fileState; }
    bool            dirty() const { return m_dirty; }

    // What was asked, in order (the tests' and the log's view of §4.4 step 2).
    struct Ask {
        int     swing = 0;
        bool    derive = false;   // false: a fire (layers 4–5 and 3); true: layer 3 only
        quint64 number = 0;
    };
    const std::vector<Ask> &asks() const { return m_asks; }
    struct Counts {
        quint64 kept = 0, unread = 0, superseded = 0, itemGone = 0, writes = 0, writeFailures = 0, deriveFailures = 0;
        // §4.4 step 7 (run 3.5-ii): reductions asked, applied, dropped as superseded (a newer one in the lane), failed;
        // derived data taken back from the file at a load; summaries written.
        quint64 reductionsAsked = 0, reductions = 0, reductionsSuperseded = 0, reductionFailures = 0;
        quint64 derivedRestored = 0, summaryWrites = 0;
        quint64 summaryNewerKept = 0;   // a summary write skipped: the file on disk is a newer build's (R-PERS-3; 4-e)
        quint64 baselineFreezes = 0;   // a club baseline computed at first need (step 3.6), written or held in memory
        quint64 droppedWhileArchived = 0;   // results that came back while an archive was out: not kept, owed (3.7-a)
    };
    const Counts &counts() const { return m_counts; }
    // The last write that failed, in words for the golfer's notice (DEC-126): "" when none has (the catch-up reports it).
    QString lastWriteError() const { return m_lastWriteError; }

    // ── The diagnosis (§4.4 step 7; run 3.5-ii) ──────────────────────────────────────────────────────────
    // The latest applied, or null before the first reduction lands.
    const analysis::SessionDiagnosis *diagnosis() const { return m_diagnosis ? &*m_diagnosis : nullptr; }
    bool    reducing() const { return m_reductionInFlight; }
    // Nothing asked and not come back, no reduction in flight or owed: what is kept is what the diagnosis says.
    bool    idle() const { return m_outstanding == 0 && !m_reductionInFlight && !m_reductionOwed; }
    quint64 latestReduction() const { return m_reductionLatest; }   // the receiver's check for the owner's own work
    qint64  lastReductionMs() const { return m_lastReductionMs; }   // the last applied reduction's run time
    // THE SESSION SUMMARY (OBJ-SESSION-SUMMARY, FILE-SESS-05; DEC-192): the typed results with their provenance and,
    // when no shot had a clock, the Session's own start (its one writer, J-24). Empty before the first reduction.
    analysis::SessionResults summary() const;
    // The findings stamp of this snapshot (R-STAMP-1): what the parallel run shows beside the old model's stamp.
    QString findingsStamp() const { return m_findingsStamp; }
    // WHAT THE APPLIED DIAGNOSIS WAS REDUCED FROM (refactor 3.7-a): the immutable input its reduction was posted with —
    // the rows as checked against the flight, each shot's derived data and word, the usuals, the baseline, the golfer's
    // session-level word, the stage before it. The thin adapter (SessionDiagnosticsModel over this owner) shows these
    // rows beside diagnosis(), so a screen never pairs one reduction's ledgers with another's rows. Null before the first
    // reduction lands.
    std::shared_ptr<const analysis::SessionReductionInput> diagnosisInput() const { return m_diagnosisInput; }
    // A write of what is kept is waiting on its debounce (§4.4 step 8): the adapter's `busy` holds until it lands, so a
    // screen that re-reads diagnostics.json when busy goes false (Charts, Consistency) reads the written file.
    bool    writePending() const { return m_writeTimer.isActive(); }

    // ── Commands (owner thread) ─────────────────────────────────────────────────────────────────────────
    void fire(const ShotRef &shot);       // that shot (its own command's chain)
    void fireAll();                       // every shot (a pack, norm, baseline or cohort change)
    // EVT-16: club_baseline.json was rewritten — read it again; every shot fired when it changed. True if it did.
    // `keepWhenAbsent` (the parallel run's hook, refactor 3.7-0): no file → nothing changes (the baseline computed in
    // memory at first need is kept); without it, an absent file reads as "no usual numbers", as before.
    bool reloadClubBaseline(bool keepWhenAbsent = false);
    // Write what is kept now, if anything is unwritten (and persisting). False on a failed write. `final` (a release, the
    // quit: §4.4 step 8) writes a summary still owed for a live session too (summaryWaits).
    bool flush(bool final = false);
    // The catch-up's (3.7-e): flush(), and the summary written even when nothing is owed (its file is stale on disk).
    bool flushWithSummary();
    // THE CLUB BASELINE AT FIRST NEED, ASKED AGAIN (refactor 3.7-e; DEC-46; CARRY 3.7-0 and J-a8: ONE freeze rule, the
    // owner's): a session that holds no baseline reads club_baseline.json again (a freeze that landed meanwhile), else
    // freezes it now from AthleteDiagnostics' computation over its predecessors' summaries, when that is computable — what
    // load() does, asked again by the catch-up for every session it takes in hand (a session ended since its load, the live
    // one at its End). Never while an archive is out, for a read-only file, or once a baseline is held. True when the
    // baseline moved (then every shot is asked: EVT-16).
    bool freezeClubBaselineIfNeeded();
    bool holdsClubBaseline() const { return m_baseline.has_value(); }
    // The last attempt to freeze found it not computable (earlier sessions not summed up yet), and none is held: the
    // screens say so (refactor 3.7-e round 2; J-49: show with a warning, never hide).
    bool clubBaselineWaiting() const { return !m_baseline && m_baselineWaiting; }
    // Nothing is written now: an archive is out, or was made from this hold (J-a9).
    bool writesHeld() const { return m_archiveOut || m_archivedThisHold; }
    // Each kept shot's document stamp checked against its document on disk, as a load does; a stale one is asked again
    // (review R1-m2 (c): a session held here whose documents changed outside this process). True if any was asked.
    bool recheckDocuments();

    // The receiver's hand-off (§4.4 step 6): Kept, ItemGone or Superseded, on the owner thread.
    void deliver(WorkVerdict verdict, WorkResult &&result);

signals:
    void resultKept(const pinpoint::ShotRef &shot);
    void shotUnread(const pinpoint::ShotRef &shot, const QString &reason);
    void batchSettled();                        // every ask so far has come back, kept or dropped
    void diagnosisApplied();                    // a reduction was applied (§4.4 step 7)
    void written();
    void writeFailed(const QString &error);     // DEC-126: the caller shows it
    void clubBaselineChanged();                 // held, or waiting on earlier sessions, moved (refactor 3.7-e round 2)

private:
    SessionDiagnostics(Session *session, const DiagnosticsDoors &doors);

    void load();
    void loadTail();                                       // the re-asks and the derived data's take-back of a load
    void onArchiveOut();                                   // the archive window opened (3.7-a, B2)
    void onArchiveBack(bool ok);                           // the archive window closed (3.7-a, B2)
    void loadOrFreezeClubBaseline();                       // step 3.6 (DEC-46)
    analysis::BoardAnnotation boardOf(int swing) const;    // the board's word on a shot, asked of the Shot
    QString summaryFingerprint() const;                    // what the summary was worked out from (3.7-e: one rule)
    bool    summaryWaits() const;                          // a live session's summary waits for its End (3.7-e)
    bool    writeSummary();                                // work_ons.json alone (3.7-e)
    void hearShot(Shot *shot);
    void connectSession();
    std::shared_ptr<const FiringSnapshot> makeSnapshot() const;
    void refreshFindingsInputs();   // the stamp and the outcome pack, when a findings input changed
    void askFire(Shot *shot);
    void askDerive(Shot *shot, const KeptShot &kept);
    void submit(const WorkAddress &address, std::function<std::any()> work);
    void reconcile();               // the usuals again; every kept result whose inputs are not current asked again
    void settleIfDone();
    void askReduction();                                   // §4.4 step 7
    analysis::SessionReductionInput reductionInput() const;
    void deliverReduction(WorkVerdict verdict, WorkResult &&result);
    void readSessionWord();                                // focus, miss, screens: the one rule (J-33)
    QString derivedBaseStamp() const;                      // what every shot's derived stamp shares
    bool    sessionEnded() const;                          // the Session's status, through its repository (J-24)
    QString derivedStampOf(const QString &base, int swing, const analysis::ClubFlightUsual &usual,
                           const QString &flightKey) const;
    // The part of the snapshot's flight table `club` reads (flightNormsStampFor), once per club per table (refactor 6.3a).
    QString flightKeyOf(const QString &club) const;
    // The kept shot's derived data is current: its findings stamp, the layer-3 generation, its club's usual and flight key.
    bool    derivedCurrent(const KeptShot &k) const;
    analysis::ShotRecord checkedOf(const KeptShot &k) const;   // checkedRows' rule, on a kept shot
    void drop(int swing);
    void retire(bool writeFirst);
    bool write();
    QHash<QString, analysis::ClubFlightUsual> usualsFromKept() const;

    // Heard.
    void onRulesChanged();
    void onAthleteChanged();
    void onIntentRunChanged();
    void onSessionIntentChanged();   // EVT-10: the declared focus, miss or a screen answer
    DayReadingsAsk dayAsk() const;   // the day readings' ask, beside the reduction's input (refactor 5.5d)

    Session           *m_session  = nullptr;
    SessionRepository *m_sessions = nullptr;
    SessionId          m_id;
    DiagnosticsDoors   m_doors;
    quint64            m_instance = 0;

    // The snapshot's parts, and the snapshot.
    std::shared_ptr<const RuleDataSnapshot>              m_rules;
    std::shared_ptr<const analysis::ShotOutcomePack>     m_outcomePack;
    std::optional<QHash<QString, analysis::ClubBaselineEntry>> m_baseline;
    QDate                                                m_dob;
    QString                                              m_sex;
    // The athlete record's handedness (OBJ-ATHLETE-RECORD): the hand a shot that records none is named in (golferHand;
    // refactor 6.3b fix round, B-3, J-101). Read with the cohort facts; layer 3's words only, never a finding.
    QString                                              m_athleteHandedness;
    QDate                                                m_sessionDay;
    IntentRun                                            m_intentRun;
    QHash<int, QString>                                  m_legacyIntents;
    QHash<int, std::vector<analysis::ShotCorrection>>    m_legacyCorrections;
    QHash<QString, analysis::ClubFlightUsual>            m_usuals;
    mutable QHash<QString, QString>                      m_flightKeys;   // canonical club → flightKeyOf, for m_snapshot's table
    QHash<int, analysis::ShotRecord>                     m_loadedChecked;   // the file's checked rows, until loadTail takes them
    QString                                              m_findingsStamp;
    quint64                                              m_derivedGeneration = 1;
    std::shared_ptr<const FiringSnapshot>                m_snapshot;

    // What is kept and what is in flight.
    QMap<int, KeptShot> m_kept;        // by swing number
    QSet<int>           m_unread;      // persisted (DEC-191)
    // swing → its latest ask in flight: a derive or a fire, and (a derive) the inputs it was asked under (C3).
    struct InFlight {
        bool                      derive = false;
        analysis::ClubFlightUsual usual;
        QString                   flightKey;
        quint64                   generation = 0;
        quint64                   number = 0;   // its ask number: which asks are oldest when an archive drops the rest
    };
    QHash<int, InFlight> m_inFlight;
    QSet<int>           m_heard;       // the shots whose events it hears (each connected once)
    int                 m_outstanding = 0;
    int                 m_holdReconcile = 0;

    // The file as read: the session block's keys this object does not own, the older golfer keys (carried on every
    // write: J-33), the ledger's options.
    QJsonObject             m_sessionBlock;
    QJsonObject             m_legacyGolfer;
    analysis::LedgerOptions m_ledgerOptions;
    bool                    m_readOnly = false;
    FileState               m_fileState = FileState::Missing;   // what the last load read (fileState())
    bool                    m_dirty    = false;
    bool                    m_retired  = false;
    // THE ARCHIVE WINDOW (refactor 3.7-a, review B2): an archive job of the session is out (nothing asked or written);
    // an ask or a write was owed meanwhile; a golfer's command or a late part came meanwhile; an archive was made while
    // this instance held the session (its loaded results stand: the archive copied them).
    bool                    m_archiveOut       = false;
    bool                    m_owedAfterArchive = false;
    QSet<int>               m_eventSwings;              // shots a command or a late part reached meanwhile
    bool                    m_eventAll         = false;     // a change to every shot meanwhile (pack, norm, record, run)
    bool                    m_wordWhileOut     = false;     // the session's word (focus, miss, screen) meanwhile
    bool                    m_archivedThisHold = false;
    QTimer                  m_writeTimer;

    std::vector<Ask> m_asks;
    Counts           m_counts;
    QString          m_lastWriteError;

    // The diagnosis (run 3.5-ii): the golfer's session-level word, the stage ratchet held, the latest applied.
    DeclaredFocus        m_focus;
    QString              m_miss;
    QHash<QString, bool> m_screens;
    analysis::Stage      m_stage = analysis::Stage::Cold;
    bool                 m_reachedEstablished = false;
    bool                 m_stageHeld = false;      // false: none read or reduced yet (the ratchet replays)
    std::optional<analysis::SessionDiagnosis> m_diagnosis;
    quint64              m_reductionLatest = 0;
    // The input of the latest reduction asked (its number), and of the one applied (refactor 3.7-a; diagnosisInput()).
    std::shared_ptr<const analysis::SessionReductionInput> m_askedInput;
    std::shared_ptr<const analysis::SessionReductionInput> m_diagnosisInput;
    bool                 m_reductionInFlight = false;
    QString              m_summaryFingerprint;     // taken with the applied diagnosis (summaryFingerprint)
    mutable std::shared_ptr<const RuleDataSnapshot> m_contentStampOf;   // the snapshot m_contentStamp was hashed from
    mutable QString      m_contentStamp;
    bool                 m_summaryOwed = false;    // a diagnosis is written but its summary is not (summaryWaits)
    bool                 m_finalWrite  = false;    // a release's or the quit's write has begun
    bool                 m_baselineWaiting = false;   // the last freeze attempt was not computable (J-49)
    bool                 m_reductionOwed = false;  // what is kept moved since the last reduction was asked
    qint64               m_lastReductionMs = 0;
    // THE SUMMARY'S DAY READINGS (refactor 5.5d; DEC-74): made with each reduction from what it was asked with (the kept
    // shots' delivery readings and rows, the cameras), applied with its diagnosis; summary() carries them. The cameras
    // are read once per first kept shot and its document (m_camerasKey).
    analysis::SessionDayReadings m_dayReadings;
    analysis::SessionCameras     m_cameras;
    QString                      m_camerasKey;     // "" never read; else the swing and document they were read from
};

// THE COMPOSITION ROOT'S LOOKUP, for its WorkResolver (§4.4 step 6; U9: a function, no interface): what the receiver
// needs to check `address` against `session` (the identity map's answer for the address's SessionId, null when none is
// loaded) — its diagnostics instance's number, whether the shot is still the session's and its latest ask, and the
// hand-off to its SessionDiagnostics.
WorkTarget diagnosticsWorkTarget(Session *session, const WorkAddress &address);

} // namespace pinpoint
