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

// Shot — one shot, ITS OWN OBJECT, a child of its Session (refactor step 3.4a; build/refactor/step-3/BRIEF.md
// §4.3; governance/artefacts/information_model.md OBJ-SHOT, CLS-SHOT, LC-SHOT, CLS-SHOT-DIAG; P10, DEC-95,
// DEC-141, DEC-142).
//
//   ref()              its ShotRef (ID-SHOT): its session's id and its swing number. Never a folder
//   kind()             captured or device-only; never changes (CLS-SHOT). Given by Session::addShot; for a shot
//                      the session lists from disk, read ONCE from its document when first asked
//                      (RecordingRepository::isDeviceOnly — the one classification, R-REC-8)
//   summary()          the small summary a carousel row shows (ShotSummary, recording_repository.h): read from
//                      its document through its repository (RecordingRepository::readSummary, the cheap read)
//                      when first asked, then kept, and updated by its own commands' saves (refactor step
//                      3.4b-ii). rereadSummary() reads it again (the carousel's session load and refresh: the
//                      document is the truth, as the carousel has always read it). A read that finds no
//                      document is not kept, so a shot whose document capture is still writing is read again
//   diagnostics()      ShotDiagnostics: a read-only view of its findings and derived data, as its Session's
//                      SessionDiagnostics keeps them (refactor 4-b); no value before they land ("-", never a zero: DEC-96)
//   measurements()     its MEASUREMENTS, the one source-tagged set (OBJ-MEASUREMENT, R-INS-1, R-INS-2; refactor 6.1a):
//                      every reading (the launch monitor's, HackMotion's) and every derived measurement, each with its
//                      source and kind, read through its repository (RecordingRepository::readMeasurements: the analysis
//                      without its tracks) when first asked, then kept — scalars only, never the document — and read
//                      again after a late part or a re-analysis it saved (EVT-22, EVT-02) and with every summary re-read
//                      (the document is the truth). Readers filter it by source and kind; which source wins for a metric
//                      is the one precedence rule's (measurement_precedence.h)
//   recordingSource()  a RecordingSource (recording_source.h): how any thread reads this shot's Recording
//   folder()           its folder, as a property only (its media; a row's swingDir until step 4)
//
// OBTAINABLE ONLY FROM ITS SESSION (P10): its constructor is private and Session is its one friend;
// Session::shot(ref) and Session::shots() hand out the instance the session holds. It is removed by
// Session::trashShot, which deletes it LATER (deleteLater), so a view still bound to it never touches freed
// memory.
//
// "HOLDS ITS RECORDING" (CLS-SHOT) READS AS "OWNS AND REACHES", NOT "KEEPS IN MEMORY" (BRIEF §4.3, J-4): the
// Shot never keeps the parsed swing document (28–33 MB); it reads through its repository when asked.
//
// ITS COMMANDS FOR ITS OWN FACTS (refactor step 3.4b-i; CLS-SHOT; DEC-94, DEC-103, DEC-187; P10):
//   rate(stars), note(text)      the review's rating and note; no event (DEC-70: no derived fact reads them)
//   setDeclaredClub(club)        the review's declared club (the popover's pick)          EVT-21
//   correctClub(club)            the launch monitor board's club for the shot ("" = none)  EVT-04
//   excludeReading(excluded)     the board leaves the shot's reading out of its figures    EVT-05
//   correctLabel(list, note)     the shot's label corrections, whole (FILE-SHOT-02)        EVT-06
//                                (its Athlete appends `note` to the tuning log on it: DEC-43)
//   setShotIntent(intent)        the shot intent ("" = none)                               EVT-07
//   completeCapture(part)        a late part: a launch monitor reading (and follow-club), or a phone clip's
//                                stream (R-REC-2)                                         EVT-22
//   setMarkupTruth(view, json)   the video markup's ground truth (FILE-SHOT-12); no event (DEC-187: IM-S3-9);
//                                markupTruth(view) reads it back, kept nowhere (refactor 5.7d)
//   applyAnalysis(outcome)       re-analysis's write-back: the analysis and both integrity verdicts a pass
//                                reached, or (no analysis) a launch-monitor-only shot's rows derived again
//                                (refactor step 3.4b-ii; DEC-103)                          EVT-02
// EACH SAVES THROUGH RecordingRepository BY ITS ShotRef — every write there takes the session's lock for that one
// write (step 2.8b) — THEN EMITS ITS EVENT ONCE, on this thread, carrying only the ShotRef (DEC-69, DEC-103). An
// event says the fact CHANGED: a save that changed nothing (WriteResult Unchanged: the value was already so)
// emits nothing. A FAILED SAVE EMITS NOTHING; its WriteResult goes back to the caller and the app log says which
// command failed and why. The caller shows it to the golfer (DEC-126): the diagnostics panel's write notice; from step
// 3.7-a the carousel's and the launch monitor's notices (their saveFailed, posted by main.cpp); the clip filer still
// logs only (information_model.md §11). A save is made now, on this thread, unless
// the caller names its worker (SaveLater: setShotIntent and correctLabel, the diagnostics panel's, whose save is
// made off the GUI thread today; D25, DEC-183). Its Session hears every event (P10), and its SessionDiagnostics
// re-asks the shot on each (step 3.5-i).
// THE SUMMARY FOLLOWS THE SAVES: a rating, note, declared club, club correction or exclusion saved updates the kept
// summary's field (nothing is read again); a late part or a re-analysis saved marks it to be read again on the next
// summary() (their write changes what the summary block holds). A failed save changes nothing in it.
// ITS FINDINGS (step 3.5-i): fired by its Session's SessionDiagnostics, which keeps them (DEC-135); the Shot holds its
// latest ask number (latestAsk). ShotDiagnostics, the view, reads what the owner keeps (refactor 4-b).
//
// Domain model ring (src/DomainModel): names no store type.

#include "athlete.h"            // ShotDoors
#include "recording_repository.h"   // WriteResult, StreamArrival, TruthView, LabelCorrection
#include "shot_command_values.h"    // LabelCorrectionNote, SaveLater
#include "recording_source.h"   // RecordingSource (its own header: the analysis services take it alone)
#include "../Vocabulary/Identity/identity.h"
#include "../Vocabulary/Recording/recording.h"   // recording::ShotKind

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

#include <memory>
#include <optional>

namespace pinpoint {

class Session;

// ShotDiagnostics (OBJ-SHOT-DIAG, CLS-SHOT-DIAG): a read-only view of the shot's findings and derived data,
// reached through the Shot; owns and caches nothing (refactor 4-b). It names its Session's SessionDiagnostics (the
// owner, DEC-135) and the shot's swing number, and every read goes to what the owner keeps NOW: a value taken from it
// lives only as long as the caller's statement. Before the owner has kept a result for the shot (or with no owner: a
// session nothing judges) it has no value, and a view shows "-" for it, never a zero (DEC-96).
//
// THE READS ARE DEFINED WITH THE OWNER (session_diagnostics.cpp), so only a target that links the owner can call them;
// `attached()` is inline, for the ones that do not.
class SessionDiagnostics;
namespace analysis { struct ShotFactors; struct ShotRecord; struct ShotOutcomePack; }

class ShotDiagnostics
{
public:
    ShotDiagnostics() = default;
    ShotDiagnostics(const SessionDiagnostics *owner, int swingNumber) : m_owner(owner), m_swing(swingNumber) {}

    bool attached() const { return m_owner != nullptr; }   // its Session has a diagnostics owner
    bool fired() const;                                    // the owner keeps findings for the shot
    bool hasLaunchMonitor() const;                         // …and they were read off a launch-monitor reading
    // Its findings as fired (the launch monitor's readings among them: carry, in yards); null before they fire.
    const analysis::ShotRecord  *findings() const;
    // Its derived data's three factors (strike · direction · flight); null until its layer 3 is computed.
    const analysis::ShotFactors *factors() const;
    // The outcome pack the factors' words come from (the owner's snapshot's); null with no owner.
    const analysis::ShotOutcomePack *outcomePack() const;

private:
    const SessionDiagnostics *m_owner = nullptr;
    int                       m_swing = 0;
};

class Shot : public QObject
{
    Q_OBJECT
public:
    ShotRef               ref() const { return m_ref; }
    int                   swingNumber() const { return m_ref.swingNumber(); }
    recording::ShotKind   kind() const;
    // The DOCUMENT's classification as it stands now (RecordingRepository::isDeviceOnly, R-REC-8), read afresh: unlike
    // kind(), a camera shot the launch monitor then filled (the rescue) reads device-only here (re-analysis's question).
    bool                  documentIsDeviceOnly() const;
    // The row's summary (above): read once when first asked, else as kept; rereadSummary() reads it again.
    const ShotSummary    &summary();
    const ShotSummary    &rereadSummary(bool parseIfUnindexed = true);   // RecordingRepository::readSummary's flag
    bool                  summaryKept() const { return m_summaryRead; }   // a read summary is kept (no read needed)
    ShotDiagnostics       diagnostics() const;   // a view over its Session's owner, made on each call (4-b)
    // Its measurements (above): read once when first asked (a read that finds none is not kept), then kept.
    const analysis::MeasurementSet &measurements();
    RecordingSource       recordingSource() const { return RecordingSource(m_ref, m_doors.reader); }
    // Its folder, AS A PROPERTY (its media; the swingDir a carousel row still carries until step 4), through its
    // repository (RecordingRepository::swingFolder); "" when its session is not open. Never its key (CLS-SHOT).
    QString               folder() const { return m_doors.recordings ? m_doors.recordings->swingFolder(m_ref) : QString(); }

    // ── The commands for its own facts (refactor step 3.4b-i; above) ─────────────────────────────────────
    WriteResult rate(int stars);                        // 0–5, clamped by the repository
    WriteResult note(const QString &text);
    WriteResult setDeclaredClub(const QString &club);   // EVT-21
    WriteResult correctClub(const QString &club);       // EVT-04; "" restores the default
    WriteResult excludeReading(bool excluded);          // EVT-05
    // EVT-07. `keepEmpty`: a cleared intent is written as an explicit empty value while diagnostics.json's
    // older copy still holds an answer for the shot (step 2.5's J-33; ReviewEdit::keepEmptyGolferInput).
    WriteResult setShotIntent(const QString &intent, bool keepEmpty = false);
    void        setShotIntent(const QString &intent, bool keepEmpty, const SaveLater &later);
    // EVT-06. `corrections` is the shot's whole list after the correction (empty = none); `note` the correction
    // just made, for the tuning log; `keepEmpty` as setShotIntent's.
    WriteResult correctLabel(const QVector<LabelCorrection> &corrections, const LabelCorrectionNote &note,
                             bool keepEmpty = false);
    void        correctLabel(const QVector<LabelCorrection> &corrections, const LabelCorrectionNote &note,
                             bool keepEmpty, const SaveLater &later);
    // EVT-22, on a late part (R-REC-2). The reading's `followedClub` and `clubReplaced` as
    // RecordingRepository::completeCapture's (follow-club, DEC-82).
    WriteResult completeCapture(const lm::LaunchMonitorReading &reading, const QJsonObject &followedClub = {},
                                bool *clubReplaced = nullptr);
    WriteResult completeCapture(const StreamArrival &arrival);
    // The markup truth (FILE-SHOT-12), written whole; no event (DEC-187: IM-S3-9).
    WriteResult setMarkupTruth(TruthView view, const QJsonObject &truth);
    // …and read back through the same repository, as it is stored NOW (OBJ-MARKUP-TRUTH: the Shot its one reader and
    // its one writer; refactor 5.7d). Nothing is kept: the truth has no event, so a truth written by any path is seen
    // at the next read. {} when there is none.
    QJsonObject markupTruth(TruthView view) const;
    // EVT-02 (refactor step 3.4b-ii; DEC-103): re-analysis's write-back through RecordingRepository::writeAnalysis —
    // the pass's analysis and verdicts (a verdict it could not reach withdrawn), or with no analysis a
    // launch-monitor-only shot's rows derived again; the repository's empty-document guard and `options` (Save pose
    // keypoints) as before. Made on the caller's thread (the GUI thread in the app, as the write-back always was).
    WriteResult applyAnalysis(const AnalysisOutcome &outcome, const PersistOptions &options = {});

    // ITS ASK NUMBER (refactor step 3.5-i; BRIEF.md §4.4 step 3): each ask of its findings or derived data takes the
    // next number of the one process-wide counter (nextWorkNumber, src/Core/work_pool.h) and records it here as the
    // Shot's LATEST; a result is kept only when its number is exactly this. Recorded by its Session's
    // SessionDiagnostics, which assembles the job (it holds the snapshot and the doors); 0 = never asked.
    quint64 latestAsk() const { return m_latestAsk; }
    void    recordAsk(quint64 number) { m_latestAsk = number; }

    // The label correction this Shot last saved: what its Athlete appends to the tuning log on EVT-06 (DEC-43).
    const LabelCorrectionNote &lastLabelCorrection() const { return m_lastCorrection; }

signals:
    void declaredClubChanged(const pinpoint::ShotRef &shot);   // EVT-21 ShotDeclaredClubChanged
    void clubCorrected(const pinpoint::ShotRef &shot);         // EVT-04 ShotClubCorrected
    void readingExcluded(const pinpoint::ShotRef &shot);       // EVT-05 ShotReadingExcluded
    void labelCorrected(const pinpoint::ShotRef &shot);        // EVT-06 LabelCorrected
    void shotIntentChanged(const pinpoint::ShotRef &shot);     // EVT-07 ShotIntentChanged
    void captureCompleted(const pinpoint::ShotRef &shot);      // EVT-22 ShotCaptureCompleted
    void reanalysed(const pinpoint::ShotRef &shot);            // EVT-02 ShotReanalysed

private:
    friend class Session;   // P10: made, held and removed by its Session only
    // `kind` known (addShot) or not (a shot listed from disk: read from its document when first asked).
    Shot(const ShotRef &ref, std::optional<recording::ShotKind> kind, const ShotDoors &doors, Session *parent);

    enum class Event { None, DeclaredClub, ClubCorrected, ReadingExcluded, LabelCorrected, ShotIntent,
                       CaptureCompleted, Reanalysed };
    // A save's result: logged when it failed, the event emitted once when it changed the fact.
    WriteResult finish(const WriteResult &result, Event event, const char *command);
    WriteResult saveReview(const ReviewEdit &edit, Event event, const char *command);
    // A saved review edit's fields into the kept summary (the summary follows the saves).
    void        keepInSummary(const ReviewEdit &edit);
    void        saveReviewLater(const ReviewEdit &edit, Event event, const char *command, const SaveLater &later,
                                const std::optional<LabelCorrectionNote> &note);
    void        emitEvent(Event event);

    ShotRef                                    m_ref;
    ShotDoors                                  m_doors;
    mutable std::optional<recording::ShotKind> m_kind;
    ShotSummary                                m_summary;
    bool                                       m_summaryRead = false;   // m_summary holds a read (ok) summary
    analysis::MeasurementSet                   m_measurements;
    bool                                       m_measurementsRead = false;   // m_measurements holds a read set
    LabelCorrectionNote                        m_lastCorrection;
    quint64                                    m_latestAsk = 0;
};

} // namespace pinpoint
