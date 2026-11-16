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
// §4.3; docs/developer/information_model.md OBJ-SHOT, CLS-SHOT, LC-SHOT, CLS-SHOT-DIAG; P10, DEC-95,
// DEC-141, DEC-142).
//
//   ref()              its ShotRef (ID-SHOT): its session's id and its swing number. Never a folder
//   kind()             captured or device-only; never changes (CLS-SHOT). Given by Session::addShot; for a shot
//                      the session lists from disk, read ONCE from its document when first asked
//                      (RecordingRepository::isDeviceOnly — the one classification, R-REC-8)
//   summary()          the small summary a carousel row needs (ShotSummary below). THE SHAPE ONLY in step
//                      3.4a: its fields are filled by step 3.4b, with the Shot's commands for its own facts
//   diagnostics()      ShotDiagnostics: a read-only view of its findings, EMPTY until step 3.5 fires them
//                      ("-", never a zero: DEC-96)
//   recordingSource()  a RecordingSource: how any thread reads this shot's Recording (below)
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
//   setMarkupTruth(view, json)   the video markup's ground truth (FILE-SHOT-12); no event (DEC-187: IM-S3-9)
// EACH SAVES THROUGH RecordingRepository BY ITS ShotRef — every write there takes the session's lock for that one
// write (step 2.8b) — THEN EMITS ITS EVENT ONCE, on this thread, carrying only the ShotRef (DEC-69, DEC-103). An
// event says the fact CHANGED: a save that changed nothing (WriteResult Unchanged: the value was already so)
// emits nothing. A FAILED SAVE EMITS NOTHING; its WriteResult goes back to the caller and the app log says which
// command failed and why. Only the diagnostics panel's model SHOWS it to the golfer (its write notice); the
// carousel, the launch monitor's late reading and the clip filer log it only, as before this step — the gap
// against DEC-126 is scheduled for step 3.7's cut-overs (information_model.md §11). A save is made now, on this thread, unless
// the caller names its worker (SaveLater: setShotIntent and correctLabel, the diagnostics panel's, whose save is
// made off the GUI thread today; D25, DEC-183). Its Session hears every event (P10); nothing consumes them for a
// re-fire until step 3.5.
// NOT YET: applyAnalysis (EVT-02) is run 3.4b-ii's; the Shot fires nothing until step 3.5 (fireFindings, its ask
// number); the rows (summary()) are filled in run 3.4b-ii.
//
// Domain model ring (src/DomainModel): names no store type.

#include "athlete.h"            // ShotDoors
#include "recording_repository.h"   // WriteResult, StreamArrival, TruthView, LabelCorrection
#include "shot_command_values.h"    // LabelCorrectionNote, SaveLater
#include "recording_reader.h"   // RecordingReader, LoadedRecording, RecordingLoadOptions (RecordingSource)
#include "../Identity/identity.h"
#include "../Recording/recording.h"   // recording::ShotKind

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

#include <memory>
#include <optional>

namespace pinpoint {

class Session;

// HOW ANY THREAD READS ONE SHOT'S RECORDING (BRIEF §2.2, §4.3; 04 step 3 item 8: the analysis services and the
// firing job "ask the Shot"). An immutable value: the ShotRef and the reader the Shot was given. It ONLY WRAPS
// THE EXISTING READER (RecordingReader, keyed by ShotRef: DEC-160), which resolves the shot's folder only
// through SessionRepository::swingFolder, the one store call any thread may make (the v1.9 reviewer's
// condition). So a source whose session is no longer published (trashed, released, its library gone) reads
// NOTHING: ok = false, with the reader's reason. It adds no ShotRef→folder translation and no read path of
// its own. Made on the GUI thread by Shot::recordingSource(); read from any thread.
class RecordingSource
{
public:
    RecordingSource() = default;
    RecordingSource(const ShotRef &shot, std::shared_ptr<RecordingReader> reader)
        : m_shot(shot), m_reader(std::move(reader)) {}

    const ShotRef &shot() const { return m_shot; }
    // No shot, or no reader to read it with: read() answers ok = false.
    bool isNull() const { return m_shot.isNull() || !m_reader; }

    // The shot's Recording (a LoadedRecording) through the reader (RecordingReader::load). A TEMPLATE, ITS
    // RESULT TYPE DEPENDENT, ONLY SO ITS BODY IS COMPILED WHERE IT IS CALLED: a LoadedRecording owns a
    // SwingWindow (src/Buffer), which the caller — who destroys the result — must see whole anyway, while the
    // many targets that link the Session and its shots never call it and so need none of the Buffer. Options
    // defaults to RecordingLoadOptions.
    template <typename Options = RecordingLoadOptions>
    auto read(const Options &options = Options()) const
    {
        using Result = decltype(m_reader->load(m_shot, options));   // LoadedRecording
        if (isNull()) {
            Result none;
            none.error = m_shot.isNull() ? QStringLiteral("no shot was named")
                                         : QStringLiteral("the shot %1 has no reader").arg(m_shot.toString());
            return none;
        }
        return m_reader->load(m_shot, options);
    }

private:
    ShotRef                          m_shot;
    std::shared_ptr<RecordingReader> m_reader;
};

// The small summary a carousel row shows (BRIEF §4.3 "a small summary for the rows"; today's
// ShotListModel::Shot, shot_list_model.h). THE SHAPE ONLY in step 3.4a: every field is at its default until
// step 3.4b fills it from the Shot's own facts (its review and its analysis) — so a default here means
// "not filled yet", and nothing may read one as the shot's value before then.
struct ShotSummary {
    QString club;               // the shot's club, as the row names it ("" = none known yet)
    int     rating = 0;         // the golfer's stars, 0–5
    bool    hasVideo = false;
    bool    dataWarning = false;    // an integrity block warns
    bool    lmExcluded = false;     // the launch monitor board's exclusion
    QString lmClubOverride;         // the board's club correction ("" = none)
};

// ShotDiagnostics (OBJ-SHOT-DIAG, CLS-SHOT-DIAG): a read-only view of the shot's findings and derived data,
// reached through the Shot; owns and caches nothing. EMPTY IN STEP 3.4a: nothing fires until step 3.5, so it
// has no value yet, and a view shows "-" for it, never a zero (DEC-96).
class ShotDiagnostics
{
public:
    // Whether the shot's findings have fired. False until step 3.5.
    bool fired() const { return false; }
};

class Shot : public QObject
{
    Q_OBJECT
public:
    ShotRef               ref() const { return m_ref; }
    int                   swingNumber() const { return m_ref.swingNumber(); }
    recording::ShotKind   kind() const;
    const ShotSummary    &summary() const { return m_summary; }
    const ShotDiagnostics &diagnostics() const { return m_diagnostics; }
    RecordingSource       recordingSource() const { return RecordingSource(m_ref, m_doors.reader); }

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

    // The label correction this Shot last saved: what its Athlete appends to the tuning log on EVT-06 (DEC-43).
    const LabelCorrectionNote &lastLabelCorrection() const { return m_lastCorrection; }

signals:
    void declaredClubChanged(const pinpoint::ShotRef &shot);   // EVT-21 ShotDeclaredClubChanged
    void clubCorrected(const pinpoint::ShotRef &shot);         // EVT-04 ShotClubCorrected
    void readingExcluded(const pinpoint::ShotRef &shot);       // EVT-05 ShotReadingExcluded
    void labelCorrected(const pinpoint::ShotRef &shot);        // EVT-06 LabelCorrected
    void shotIntentChanged(const pinpoint::ShotRef &shot);     // EVT-07 ShotIntentChanged
    void captureCompleted(const pinpoint::ShotRef &shot);      // EVT-22 ShotCaptureCompleted

private:
    friend class Session;   // P10: made, held and removed by its Session only
    // `kind` known (addShot) or not (a shot listed from disk: read from its document when first asked).
    Shot(const ShotRef &ref, std::optional<recording::ShotKind> kind, const ShotDoors &doors, Session *parent);

    enum class Event { None, DeclaredClub, ClubCorrected, ReadingExcluded, LabelCorrected, ShotIntent,
                       CaptureCompleted };
    // A save's result: logged when it failed, the event emitted once when it changed the fact.
    WriteResult finish(const WriteResult &result, Event event, const char *command);
    WriteResult saveReview(const ReviewEdit &edit, Event event, const char *command);
    void        saveReviewLater(const ReviewEdit &edit, Event event, const char *command, const SaveLater &later,
                                const std::optional<LabelCorrectionNote> &note);
    void        emitEvent(Event event);

    ShotRef                                    m_ref;
    ShotDoors                                  m_doors;
    mutable std::optional<recording::ShotKind> m_kind;
    ShotSummary                                m_summary;
    ShotDiagnostics                            m_diagnostics;
    LabelCorrectionNote                        m_lastCorrection;
};

} // namespace pinpoint
