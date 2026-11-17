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

// ShotCommandDoor — HOW TODAY'S WRITERS OF A SHOT'S FACTS REACH THE SHOT'S COMMANDS (refactor step 3.4b-i;
// build/refactor/step-3/BRIEF.md §3.3, §4.3, §4.10, §4.12; information_model.md CLS-SHOT, CLS-SESSION, P10,
// DEC-94, DEC-103, DEC-187; D1: DEC-164). Application ring (src/App).
//
// The carousel's model (ShotListModel: the rating, note and declared club; the launch monitor board's club and
// exclusion), the launch monitor's late reading, the phone-clip filer's late part, the markup tool's truth and the
// diagnostics model's golfer commands (a shot's intent and label correction; the session's focus, miss, screen
// answers and intent run) still hold a FOLDER until step 4 (R-QML-1). Each FORWARDS to the Shot's or the Session's
// command through this door and writes nothing itself, so every fact has one writer (P10; DEC-94). The door is
// `app` (App implements it): it translates the folder at the edge through the current Athlete (App::sessionAt;
// DEC-90) and runs the command on the Session's own Shot, which is reachable only through its Session (P10).
//
// NOT REACHED (`reached` false; for setShotIntent / correctLabel the call answers false and `later.done` is not
// called): no Session holds the shot — no current athlete, a session that is not the current athlete's, a
// session in the `unknown` folder (captured with no golfer), a corpus folder the markup tool opened — or the
// session holds no such shot. THE CALLER THEN KEEPS TODAY'S PATH (Mark, 15 Nov 2026: build/refactor/step-3/
// ANSWERS.md N1 of 3.4a-ii, "keep today's path" for a session no Athlete holds). A tool or test that sets no door
// keeps today's path everywhere. Both are the interim N1 names; they go when every session has its Athlete.
//
// THE ROWS AND THE READERS (refactor step 3.4b-ii): the carousel's rows read a held shot's facts from its Shot
// (shotSummary, sessionShots — the Shot's summary, never a copy the view made), trash it through its Session
// (trashShot, EVT-03), and re-analysis reads it through the Shot's RecordingSource and writes back through the
// Shot (recordingSourceOf, applyAnalysis, EVT-02). Not reached: today's path, as above.
//
// The adapters take this header only (no Session, Shot or App code), so the many test and tool targets that
// compile them link nothing new.

// The commands' values (and, with them, WriteResult, StreamArrival, TruthView, LabelCorrection, AnalysisOutcome,
// PersistOptions and ShotSummary: the Shot's command vocabulary). No repository is named here: the door reaches the objects, never a repository (P11).
#include "../DomainModel/shot_command_values.h"    // LabelCorrectionNote, SaveLater, SessionIntentEdit
#include "../DomainModel/recording_source.h"       // RecordingSource (the readers: refactor step 3.4b-ii)
#include "../Identity/identity.h"

#include <QDir>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <optional>

namespace pinpoint::lm { struct LaunchMonitorReading; }

namespace pinpoint {

// What a forwarded command did. `reached` false: no Session holds the shot, nothing was done (the caller keeps
// today's path). `reached` true: the command ran; `result` is its save's (a failed one emitted nothing).
struct ForwardResult {
    bool        reached = false;
    WriteResult result;
};

// One shot a Session holds, as a carousel row lists it (sessionShots): its ref, its folder (a property: the row's
// media and the swingDir it still carries until step 4) and the Shot's summary.
struct HeldShot {
    ShotRef     ref;
    QString     swingDir;
    ShotSummary summary;
};

// How a row asks for a Shot's summary (shotSummary).
enum class SummaryRead {
    KeptOnly,     // the summary the Shot keeps, if it has read one; else ok = false — nothing is read (a reviewed
                  // session's rows, made from the controller's own parallel parse of the same documents: #173)
    Kept,         // Shot::summary(): kept, else read once (after an edit)
    Again,        // Shot::rereadSummary(): read again (a refresh; the document is the truth)
    AgainCheap,   // the same, never parsing a whole document (a shot just captured: J5)
};

// A swing folder's session folder (its parent), as App::sessionAt takes it.
inline QString sessionFolderOf(const QString &swingFolder)
{
    return swingFolder.isEmpty() ? QString() : QDir::cleanPath(swingFolder + QStringLiteral("/.."));
}

class ShotCommandDoor
{
public:
    virtual ~ShotCommandDoor() = default;

    // The Shot's commands, for the shot `shot` of the session at `sessionFolder` (Shot::rate … setMarkupTruth).
    virtual ForwardResult rate(const QString &sessionFolder, const ShotRef &shot, int stars) = 0;
    virtual ForwardResult note(const QString &sessionFolder, const ShotRef &shot, const QString &text) = 0;
    virtual ForwardResult setDeclaredClub(const QString &sessionFolder, const ShotRef &shot, const QString &club) = 0;
    virtual ForwardResult correctClub(const QString &sessionFolder, const ShotRef &shot, const QString &club) = 0;
    virtual ForwardResult excludeReading(const QString &sessionFolder, const ShotRef &shot, bool excluded) = 0;
    virtual ForwardResult completeCapture(const QString &sessionFolder, const ShotRef &shot,
                                          const lm::LaunchMonitorReading &reading, const QJsonObject &followedClub,
                                          bool *clubReplaced) = 0;
    virtual ForwardResult completeCapture(const QString &sessionFolder, const ShotRef &shot,
                                          const StreamArrival &arrival) = 0;
    virtual ForwardResult setMarkupTruth(const QString &sessionFolder, const ShotRef &shot, TruthView view,
                                         const QJsonObject &truth) = 0;
    // True: the Shot took it; its save is made as `later` says and `later.done` is called once it has landed.
    virtual bool setShotIntent(const QString &sessionFolder, const ShotRef &shot, const QString &intent,
                               bool keepEmpty, const SaveLater &later) = 0;
    virtual bool correctLabel(const QString &sessionFolder, const ShotRef &shot,
                              const QVector<LabelCorrection> &corrections, const LabelCorrectionNote &note,
                              bool keepEmpty, const SaveLater &later) = 0;

    // The tuning log's "flag" line for a shot of the session (Athlete::flagForTuning; no event): the session's Athlete
    // appends it, so the log has one writer (review 1 of 3.4b-i).
    virtual ForwardResult flagForTuning(const QString &sessionFolder, const ShotRef &shot,
                                        const TuningFlagNote &note) = 0;

    // ── The rows and the readers (refactor step 3.4b-ii) ─────────────────────────────────────────────────────
    // The Shot's summary, read as `how` says. nullopt: not reached (N1).
    virtual std::optional<ShotSummary> shotSummary(const QString &sessionFolder, const ShotRef &shot,
                                                   SummaryRead how) = 0;
    // Every shot of the session at `sessionFolder` whose summary reads, by swing number, each summary read again
    // (the carousel's session load: the document is the truth). nullopt: no Session holds the folder (N1).
    virtual std::optional<QVector<HeldShot>> sessionShots(const QString &sessionFolder) = 0;
    // The carousel's delete: Session::trashShot (EVT-03; its number never reissued). `where`: where it went.
    virtual ForwardResult trashShot(const QString &sessionFolder, const ShotRef &shot, QString *where) = 0;
    // Re-analysis's read (the Shot's RecordingSource) and its write-back (Shot::applyAnalysis, EVT-02). Whether
    // the shot is launch-monitor-only is NOT asked here: that is the document's classification
    // (RecordingRepository::isDeviceOnly, R-REC-8), which a rescued camera shot changes (review 1: CN-1).
    virtual std::optional<RecordingSource> recordingSourceOf(const QString &sessionFolder, const ShotRef &shot) = 0;
    virtual ForwardResult applyAnalysis(const QString &sessionFolder, const ShotRef &shot,
                                        const AnalysisOutcome &outcome, const PersistOptions &options) = 0;

    // The Session's commands for its intent (Session::setSessionIntent, setIntentRun).
    virtual ForwardResult setSessionIntent(const QString &sessionFolder, const SessionIntentEdit &edit,
                                           const SessionInput &held) = 0;
    virtual ForwardResult setIntentRun(const QString &sessionFolder, const QVector<IntentRun> &runs,
                                       const SessionInput &held) = 0;
};

} // namespace pinpoint
