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

// THE SHOT'S COMMANDS FOR ITS OWN FACTS, AND TODAY'S WRITERS FORWARDING TO THEM (refactor step 3.4b-i;
// build/refactor/step-3/BRIEF.md §4.3, §4.12, §6 row 3.4b). Over the REAL athlete store, session store and swing
// door, on a temporary library (QTemporaryDir) and temporary settings — never the share, never the OS trash.
//
// ⚠ EVERY EXPECTATION COMES FROM A STATED CONTRACT, NEVER FROM RUNNING THE CODE: information_model.md CLS-SHOT,
// CLS-SESSION, P10, FILE-SHOT-02, FILE-SHOT-12, FILE-SESS-02, OBJ-TUNING-LOG; EVT-04/05/06/07/08/10/21/22;
// DEC-43, DEC-69, DEC-70, DEC-94, DEC-103, DEC-126, DEC-187.
//
//   C1  rate and note: saved on the review (its rating, its note), NO event (DEC-70)
//   C2  setDeclaredClub: review.club; EVT-21 once
//   C3  correctClub: review.clubOverride; EVT-04 once; the same club again changes nothing and emits nothing
//   C4  excludeReading: review.excluded; EVT-05 once; again, nothing
//   C5  setShotIntent: review.shotIntent; EVT-07 once — now, and on a worker (SaveLater: the event only once the
//       save has landed, on this thread, then `done` once)
//   C6  correctLabel: review.labelCorrections; EVT-06 once; the Athlete appends ONE tuning-log line with the
//       correction (DEC-43), in the athlete's folder
//   C7  completeCapture (a launch monitor reading, a phone clip's stream): saved; EVT-22 once per saved command
//       (the repository folds the same reading in again as a write, so a repeat emits once more)
//   C8  the markup truth: saved and read back; no event (DEC-187)
//   C9  A FAILED SAVE EMITS NOTHING AND IS REPORTED (DEC-126): a shot with no document — every command answers
//       not-ok with the reason, no event, no tuning line; on a worker `done` is told the failure
//   C10 the Session's intent: setSessionIntent (focus, miss, a screen answer) EVT-10, setIntentRun EVT-08; field by
//       field, the caller's `held` for a field the file does not hold yet (J-33)
//   C11 the door (`app`): a held shot's command is reached and saved; a folder no Session holds is not reached
//       and nothing is written (the caller keeps today's path: N1)
//   C12 THE CAROUSEL'S EDITS GO TO THE SHOT: ShotListModel with the door — the rating saved by Shot::rate (the
//       rating alone); the declared club EVT-21, the board's club EVT-04, its exclusion EVT-05; without a door,
//       today's write-through (rating, note and club together)
//   C13 (moved to session_diagnostics_model_test in refactor 5.4a, onto the adapter) A PANEL EDIT THROUGH THE OLD MODEL EMITS THE SAME EVENT: SessionDiagnosticsModel's setShotIntent → EVT-07,
//       correctOutcome → EVT-06 (and the Athlete's tuning line), declareMiss → EVT-10, setIntentFrom → EVT-08,
//       each saved where it always was; flagForReview → the Athlete appends its "flag" line (one writer of the
//       tuning log: review 1), no event
//   (refactor step 3.4b-ii: the rows and the readers)
//   C14 applyAnalysis: re-analysis's write-back by the Shot (writeAnalysis), EVT-02 once when it changed the
//       document, nothing when it did not (a launch-monitor-only shot whose rows are current); the Session hears it.
//       THE SUMMARY IS THE SHOT'S: read from its document (the repository's cheap read), then kept and updated by
//       its own commands' saves; a late part or a re-analysis makes it read again; a failed save changes nothing
//   C15 THE CAROUSEL'S ROWS OVER THE SHOTS: a session load lists the Session's Shots (rows made from their summaries,
//       the swingDir as today's text); an edit's row shows what the Shot holds after the save, so a refused save
//       leaves the row as the shot is; a live row's review facts are its Shot's; the carousel's delete is the
//       Session's (Session::trashShot: EVT-03, the Shot let go, the number never reissued); without a door, today's
//       paths (its own load, edit and trash)
//   C16 RE-ANALYSIS THROUGH THE SHOT: ReanalysisController reads a held shot through the Shot's RecordingSource (the
//       analysis stubbed, it records what it was handed) and writes back by Shot::applyAnalysis through `app`
//       (EVT-02 once); a launch-monitor-only shot's rows through the Shot too (current: no event); a shot no Session
//       holds keeps today's write through the repository; and (round 2, review 1 CN-1) a camera shot whose folder the
//       launch monitor filled (the rescue: allocated captured, its document device-only) is routed by its DOCUMENT's
//       classification, as before — its rows refreshed through the Shot, never analysed (53a3bb0a)
//   (round 2, CN-3b) C16g the Shot's write-back of a camera shot writes the same document and the same swing-file bytes
//       as HEAD's path (persistReanalysis straight to the repository), for the same result and options
//   (round 2, CN-2) C15k a session its Athlete already holds is answered from memory: its session.json unreadable,
//       Athlete::sessionAt still answers the same Session (nothing is read again per row, edit or refresh)
//
//   cmake --build build/tests --target shot_commands_test -j 6
//   ctest --test-dir build/tests -R '^shot_commands_test$' --output-on-failure

#include "../athlete.h"
#include "../session.h"
#include "../shot.h"
#include "../../App/app.h"
#include "../../App/app_state.h"
#include "../../App/session_shot_source.h"
#include "../../Core/pp_settings.h"
#include "../../Core/same_folder.h"
#include "../../Export/athlete_files.h"
#include "../../Export/athlete_store.h"
#include "../../Export/rule_data_install.h"
#include "../../Export/session_diagnostics_files.h"
#include "../../Export/session_files.h"
#include "../../Export/session_store.h"
#include "../../Export/swing_store.h"
#include "../../Export/swing_store_repository.h"
#include "../../Gui/diagnostics/session_diagnostics_model.h"
#include "../../Gui/shot/reanalysis_controller.h"
#include "../../Gui/shot/shot_list_model.h"
#include "../../Vocabulary/Analysis/swing_analysis.h"
#include "../../Analysis/capture_integrity_check.h"
#include "../../Vocabulary/LaunchMonitor/launch_monitor_reading.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThreadPool>

#include <cstdio>
#include <memory>
#include "../../Core/tests/isolated_stores.h"   // pinpoint::test::IsolatedStores (#187)
#include "../../Core/tests/refused_file.h"      // pinpoint::test::RefusedFile: a save made to fail on every platform (#244)

using namespace pinpoint;

// THE ANALYSIS, STUBBED (C16): re-analysis's analysis half needs the whole pipeline; this test is about where the
// shot is read from and where it is written back, so the stub records the source it was handed and answers a
// result set here (the reanalysis_controller_test's stub, with a memory).
namespace {
ShotRef                         g_stubReadShot;
bool                            g_stubHadReader = false;
int                             g_stubCalls     = 0;
pinpoint::analysis::ReanalyzeResult g_stubResult;
}
namespace pinpoint::analysis {
ReanalyzeResult reanalyzeShot(const RecordingSource &source, const ReanalyzeOptions &)
{
    ++g_stubCalls;
    g_stubReadShot  = source.shot();
    g_stubHadReader = !source.isNull();
    return g_stubResult;
}
} // namespace pinpoint::analysis

static int g_fail = 0;
static int g_run  = 0;

static void check(const QString &label, bool ok)
{
    ++g_run;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", qPrintable(label));
    if (!ok) ++g_fail;
}

#ifndef PP_LIVE_SWINGS_DIR
#  define PP_LIVE_SWINGS_DIR "."
#endif

static const QString kSwing = QStringLiteral("swing");
static const QString kPat   = QStringLiteral("date-name-type");

// One scratch library: the real stores and doors over it, one athlete made by the athlete store, and `app`.
struct Rig {
    QTemporaryDir                                  dir;
    SessionStore                                   store;
    std::unique_ptr<SwingStoreRecordingRepository> recordings;
    std::unique_ptr<AthleteStore>                  athletes;
    Athlete                                       *athlete = nullptr;
    std::unique_ptr<App>                           app;
    QStringList                                    events;   // every event heard, in order

    Rig()
    {
        store.setLockWaitForTest(200);
        store.setOsTrashForTest(false);   // never the OS trash
        recordings = std::make_unique<SwingStoreRecordingRepository>(store);
        const QString lib = dir.path() + QStringLiteral("/lib");
        QDir().mkpath(lib);
        athletes = std::make_unique<AthleteStore>([lib] { return lib; }, &store);
        athletes->setOsTrashForTest(false);
        athletes->setLockWaitForTest(200);
        athletes->setShotDoors({ recordings.get(), nullptr });
        AthleteRecord r;
        r.name = QStringLiteral("Command Golfer");
        athlete = athletes->create(r).athlete;
        app = std::make_unique<App>([this] { return athlete; });
    }

    void listen(Shot *s)
    {
        const auto rec = [this](const char *evt) {
            return [this, evt](const ShotRef &r) { events << QString::fromLatin1(evt) + QLatin1Char(' ') + r.toString(); };
        };
        QObject::connect(s, &Shot::declaredClubChanged, s, rec("EVT-21"), Qt::DirectConnection);
        QObject::connect(s, &Shot::clubCorrected,       s, rec("EVT-04"), Qt::DirectConnection);
        QObject::connect(s, &Shot::readingExcluded,     s, rec("EVT-05"), Qt::DirectConnection);
        QObject::connect(s, &Shot::labelCorrected,      s, rec("EVT-06"), Qt::DirectConnection);
        QObject::connect(s, &Shot::shotIntentChanged,   s, rec("EVT-07"), Qt::DirectConnection);
        QObject::connect(s, &Shot::captureCompleted,    s, rec("EVT-22"), Qt::DirectConnection);
    }
    void listen(Session *s)
    {
        QObject::connect(s, &Session::sessionIntentChanged, s,
                         [this](const SessionId &id) { events << QStringLiteral("EVT-10 ") + id.toString(); });
        QObject::connect(s, &Session::intentRunChanged, s,
                         [this](const SessionId &id) { events << QStringLiteral("EVT-08 ") + id.toString(); });
    }
    QString tuningLog() const { return AthleteFiles::tuningLogFile(athlete->folder()); }
};

static QString ev(const char *evt, const ShotRef &r) { return QString::fromLatin1(evt) + QLatin1Char(' ') + r.toString(); }
static QString ev(const char *evt, const SessionId &id) { return QString::fromLatin1(evt) + QLatin1Char(' ') + id.toString(); }

// A shot of `s` with a real swing document (a corpus fixture, as golfer_input_test stages them).
static Shot *shotWithDocument(Session *s, Rig &rig)
{
    SwingAllocation a;
    Shot *shot = s->addShot(recording::ShotKind::Captured, &a);
    if (!shot) return nullptr;
    const QString src = QDir(QLatin1String(PP_LIVE_SWINGS_DIR)).filePath(QStringLiteral("rich_7iron/swing.json"));
    if (!QFile::copy(src, QDir(a.swingFolder).filePath(QStringLiteral("swing.json")))) return nullptr;
    rig.listen(shot);
    return shot;
}

static QJsonObject reviewOf(Rig &rig, const ShotRef &ref)
{
    return SwingStore::load(rig.recordings->swingFolder(ref)).value(QStringLiteral("review")).toObject();
}

static QList<QJsonObject> tuningLines(const QString &path)
{
    QList<QJsonObject> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return out;
    for (const QByteArray &line : f.readAll().split('\n'))
        if (!line.trimmed().isEmpty()) out << QJsonDocument::fromJson(line).object();
    return out;
}

static lm::LaunchMonitorReading reading(const char *id)
{
    lm::LaunchMonitorReading r;
    r.deviceShotId  = QString::fromLatin1(id);
    r.deviceKind    = QStringLiteral("fsx2020");
    r.clubheadSpeed = 80.0;
    r.ballSpeed     = 112.0;
    r.launchAngle   = 17.0;
    return r;
}

static bool waitFor(const std::function<bool()> &cond, int ms = 10000)
{
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return cond();
}

// ── C1–C9: the Shot's commands ───────────────────────────────────────────────────────────────────────────

static void shotCommands()
{
    std::printf("\nC1–C9: the Shot's commands save, then emit once; a failed save emits nothing and is reported\n");
    Rig rig;
    check(QStringLiteral("C0 a library, an athlete made by its store"), rig.dir.isValid() && rig.athlete);
    if (!rig.athlete) return;
    Session *s = rig.athlete->startSession(kSwing, kPat);
    Shot *shot = s ? shotWithDocument(s, rig) : nullptr;
    check(QStringLiteral("C0b a session and a shot with its document"), s && shot);
    if (!shot) return;
    const ShotRef ref = shot->ref();

    // C1
    QJsonObject rest = reviewOf(rig, ref);
    rest.remove(QStringLiteral("rating"));
    WriteResult w = shot->rate(4);
    QJsonObject now = reviewOf(rig, ref);
    check(QStringLiteral("C1a rate: saved"), w.ok() && now.value(QStringLiteral("rating")).toInt() == 4);
    now.remove(QStringLiteral("rating"));
    check(QStringLiteral("C1b …the rating alone: every other review key as it was (no club declared by a rating)"), now == rest);
    w = shot->note(QStringLiteral("felt thin"));
    check(QStringLiteral("C1c note: saved"), w.ok() && reviewOf(rig, ref).value(QStringLiteral("note")).toString() == QStringLiteral("felt thin"));
    check(QStringLiteral("C1d a rating or a note emits nothing (DEC-70)"), rig.events.isEmpty());

    // C2
    rig.events.clear();
    w = shot->setDeclaredClub(QStringLiteral("6 IRON"));
    check(QStringLiteral("C2a setDeclaredClub: review.club"), w.ok() && reviewOf(rig, ref).value(QStringLiteral("club")).toString() == QStringLiteral("6 IRON"));
    check(QStringLiteral("C2b EVT-21 once"), rig.events == QStringList{ ev("EVT-21", ref) });

    // C3
    rig.events.clear();
    w = shot->correctClub(QStringLiteral("5 IRON"));
    check(QStringLiteral("C3a correctClub: review.clubOverride"), w.changed() && reviewOf(rig, ref).value(QStringLiteral("clubOverride")).toString() == QStringLiteral("5 IRON"));
    check(QStringLiteral("C3b EVT-04 once"), rig.events == QStringList{ ev("EVT-04", ref) });
    w = shot->correctClub(QStringLiteral("5 IRON"));
    check(QStringLiteral("C3c the same club again: Unchanged, no event"), w.status == WriteResult::Status::Unchanged && rig.events.size() == 1);

    // C4
    rig.events.clear();
    w = shot->excludeReading(true);
    check(QStringLiteral("C4a excludeReading: review.excluded"), w.changed() && reviewOf(rig, ref).value(QStringLiteral("excluded")).toBool());
    check(QStringLiteral("C4b EVT-05 once"), rig.events == QStringList{ ev("EVT-05", ref) });
    w = shot->excludeReading(true);
    check(QStringLiteral("C4c again: Unchanged, no event"), w.status == WriteResult::Status::Unchanged && rig.events.size() == 1);

    // C5
    rig.events.clear();
    w = shot->setShotIntent(QStringLiteral("fade"));
    check(QStringLiteral("C5a setShotIntent: review.shotIntent"), w.changed() && reviewOf(rig, ref).value(QStringLiteral("shotIntent")).toString() == QStringLiteral("fade"));
    check(QStringLiteral("C5b EVT-07 once"), rig.events == QStringList{ ev("EVT-07", ref) });
    rig.events.clear();
    QThreadPool pool;
    pool.setMaxThreadCount(1);
    int doneCalls = 0;
    WriteResult doneWith;
    shot->setShotIntent(QStringLiteral("draw"), false, SaveLater{ &pool, [&](const WriteResult &r) { ++doneCalls; doneWith = r; } });
    pool.waitForDone();
    check(QStringLiteral("C5c on a worker: no event before the save's landing is handled on this thread"), rig.events.isEmpty() && doneCalls == 0);
    check(QStringLiteral("C5d …then EVT-07 once and `done` once, with the save's result"),
          waitFor([&] { return doneCalls == 1; }) && rig.events == QStringList{ ev("EVT-07", ref) } && doneWith.ok()
              && reviewOf(rig, ref).value(QStringLiteral("shotIntent")).toString() == QStringLiteral("draw"));

    // C6
    rig.events.clear();
    QFile::remove(rig.tuningLog());
    LabelCorrectionNote note;
    note.family = QStringLiteral("direction"); note.detected = QStringLiteral("push"); note.corrected = QStringLiteral("pull");
    note.measures = QJsonObject{ { QStringLiteral("face"), 1.5 } };
    note.sessionName = QStringLiteral("the-session"); note.swingDir = QStringLiteral("/elsewhere/swing_0001");
    w = shot->correctLabel({ LabelCorrection{ QStringLiteral("direction"), QStringLiteral("push"), QStringLiteral("pull") } }, note);
    const QJsonArray lc = reviewOf(rig, ref).value(QStringLiteral("labelCorrections")).toArray();
    check(QStringLiteral("C6a correctLabel: review.labelCorrections"),
          w.changed() && lc.size() == 1 && lc.first().toObject().value(QStringLiteral("to")).toString() == QStringLiteral("pull"));
    check(QStringLiteral("C6b EVT-06 once"), rig.events == QStringList{ ev("EVT-06", ref) });
    const QList<QJsonObject> lines = tuningLines(rig.tuningLog());
    check(QStringLiteral("C6c the Athlete appended ONE tuning line, in its folder (DEC-43)"), lines.size() == 1);
    if (!lines.isEmpty()) {
        const QJsonObject l = lines.first();
        check(QStringLiteral("C6d …today's keys and values"),
              l.value(QStringLiteral("kind")).toString() == QStringLiteral("correction")
                  && l.value(QStringLiteral("session")).toString() == QStringLiteral("the-session")
                  && l.value(QStringLiteral("shotId")).toInt() == ref.swingNumber()
                  && l.value(QStringLiteral("swingDir")).toString() == QStringLiteral("/elsewhere/swing_0001")
                  && l.value(QStringLiteral("family")).toString() == QStringLiteral("direction")
                  && l.value(QStringLiteral("detected")).toString() == QStringLiteral("push")
                  && l.value(QStringLiteral("corrected")).toString() == QStringLiteral("pull")
                  && l.value(QStringLiteral("measures")).toObject().value(QStringLiteral("face")).toDouble() == 1.5
                  && l.value(QStringLiteral("atMs")).toDouble() > 0);
    }

    // C7
    rig.events.clear();
    w = shot->completeCapture(reading("c7"));
    check(QStringLiteral("C7a completeCapture (a reading): saved"), w.ok());
    check(QStringLiteral("C7b EVT-22 once when it changed the shot"),
          rig.events == (w.changed() ? QStringList{ ev("EVT-22", ref) } : QStringList{}) && w.changed());
    // The same reading again: the repository folds it in again (idempotent in content, its status Written:
    // RecordingRepository::completeCapture), so the command saved and emits once more — once per saved command.
    w = shot->completeCapture(reading("c7"));
    check(QStringLiteral("C7c the same reading again: saved again, exactly one more EVT-22"),
          w.ok() && rig.events.size() == (w.changed() ? 2 : 1));
    const int before = rig.events.size();
    StreamArrival arrival;
    arrival.alias = QStringLiteral("phone");
    arrival.origin.transport = QStringLiteral("ppcp");
    arrival.origin.captureId = QStringLiteral("cap-1");
    arrival.origin.completeness = QStringLiteral("complete");
    w = shot->completeCapture(arrival);
    check(QStringLiteral("C7d completeCapture (a phone clip's stream): saved, EVT-22 once"),
          w.changed() && rig.events.size() == before + 1 && rig.events.last() == ev("EVT-22", ref));

    // C8
    rig.events.clear();
    const QJsonObject truth{ { QStringLiteral("schema"), QStringLiteral("probe") }, { QStringLiteral("events"), QJsonArray{} } };
    w = shot->setMarkupTruth(TruthView::FaceOn, truth);
    check(QStringLiteral("C8a the markup truth: saved and read back"),
          w.ok() && rig.recordings->truth(ref, TruthView::FaceOn).value(QStringLiteral("schema")).toString() == QStringLiteral("probe"));
    check(QStringLiteral("C8b no event (DEC-187)"), rig.events.isEmpty());

    // C9
    std::printf("\n");
    SwingAllocation a;
    Shot *bare = s->addShot(recording::ShotKind::Captured, &a);   // its folder, no document
    check(QStringLiteral("C9a a shot with no document"), bare != nullptr);
    if (!bare) return;
    rig.listen(bare);
    rig.events.clear();
    const int linesBefore = tuningLines(rig.tuningLog()).size();
    const QList<WriteResult> results{
        bare->rate(3), bare->note(QStringLiteral("x")), bare->setDeclaredClub(QStringLiteral("7 IRON")),
        bare->correctClub(QStringLiteral("7 IRON")), bare->excludeReading(true), bare->setShotIntent(QStringLiteral("fade")),
        bare->correctLabel({ LabelCorrection{ QStringLiteral("direction"), QStringLiteral("push"), QStringLiteral("pull") } }, note),
        bare->completeCapture(reading("c9")), bare->completeCapture(arrival),
    };
    bool allRefused = true, allSay = true;
    for (const WriteResult &r : results) {
        allRefused = allRefused && !r.ok();
        allSay     = allSay && !r.error.isEmpty();
    }
    check(QStringLiteral("C9b every command's save refused (not ok)"), allRefused);
    check(QStringLiteral("C9c …each reported with its reason (DEC-126)"), allSay);
    check(QStringLiteral("C9d no event at all"), rig.events.isEmpty());
    check(QStringLiteral("C9e no tuning line for a correction that was not saved"), tuningLines(rig.tuningLog()).size() == linesBefore);
    doneCalls = 0;
    bare->setShotIntent(QStringLiteral("draw"), false, SaveLater{ &pool, [&](const WriteResult &r) { ++doneCalls; doneWith = r; } });
    check(QStringLiteral("C9f on a worker: `done` told the failure once, no event"),
          waitFor([&] { return doneCalls == 1; }) && !doneWith.ok() && rig.events.isEmpty());
}

// ── C10: the Session's intent ───────────────────────────────────────────────────────────────────────────

static void sessionIntent()
{
    std::printf("\nC10: the Session's intent: field by field, EVT-10 / EVT-08\n");
    Rig rig;
    Session *s = rig.athlete ? rig.athlete->startSession(kSwing, kPat) : nullptr;
    check(QStringLiteral("C10a a session"), s != nullptr);
    if (!s) return;
    rig.listen(s);
    SessionInput held;   // the caller's: a miss the file does not hold yet (J-33's fallback)
    held.declaredMiss  = QStringLiteral("slice");
    held.screenAnswers = QMap<QString, bool>();
    held.intentRuns    = QVector<IntentRun>();
    SessionIntentEdit focus;
    focus.focus = DeclaredFocus{ QStringLiteral("over_the_top"), 2 };
    WriteResult w = s->setSessionIntent(focus, held);
    SessionInputRead r = rig.store.readSessionInput(s->id());
    check(QStringLiteral("C10b the focus written, and the held miss for the field the file did not hold"),
          w.ok() && r.state == SessionInputRead::State::Read && r.input.declaredFocus
              && r.input.declaredFocus->conditionId == QStringLiteral("over_the_top") && r.input.declaredMiss
              && *r.input.declaredMiss == QStringLiteral("slice"));
    check(QStringLiteral("C10c EVT-10 once"), rig.events == QStringList{ ev("EVT-10", s->id()) });

    rig.events.clear();
    SessionIntentEdit screen;
    screen.screenId = QStringLiteral("early_extension");
    screen.screenPresent = true;
    SessionInput heldOther;   // a held value that differs from the file: the file's wins
    heldOther.declaredMiss = QStringLiteral("hook");
    w = s->setSessionIntent(screen, heldOther);
    r = rig.store.readSessionInput(s->id());
    check(QStringLiteral("C10d a screen answer added; the file's miss kept over a different held one"),
          w.ok() && r.input.screenAnswers && r.input.screenAnswers->value(QStringLiteral("early_extension"))
              && *r.input.declaredMiss == QStringLiteral("slice"));
    check(QStringLiteral("C10e EVT-10 once"), rig.events == QStringList{ ev("EVT-10", s->id()) });

    rig.events.clear();
    w = s->setIntentRun({ IntentRun{ 3, QStringLiteral("fade") } }, {});
    r = rig.store.readSessionInput(s->id());
    check(QStringLiteral("C10f the intent run written"),
          w.ok() && r.input.intentRuns && r.input.intentRuns->size() == 1 && r.input.intentRuns->first().shape == QStringLiteral("fade"));
    check(QStringLiteral("C10g EVT-08 once"), rig.events == QStringList{ ev("EVT-08", s->id()) });
}

// ── C11, C12: the door and the carousel ─────────────────────────────────────────────────────────────────

static void doorAndCarousel()
{
    std::printf("\nC11–C12: the door (`app`) and the carousel's edits\n");
    Rig rig;
    Session *s = rig.athlete ? rig.athlete->startSession(kSwing, kPat) : nullptr;
    Shot *shot = s ? shotWithDocument(s, rig) : nullptr;
    check(QStringLiteral("C11a a session and a shot"), shot != nullptr);
    if (!shot) return;
    const ShotRef ref = shot->ref();
    const QString sessionFolder = s->entry()->folder;

    ForwardResult f = rig.app->rate(sessionFolder, ref, 2);
    check(QStringLiteral("C11b a held shot: reached, saved"), f.reached && f.result.ok() && reviewOf(rig, ref).value(QStringLiteral("rating")).toInt() == 2);
    f = rig.app->rate(rig.dir.path() + QStringLiteral("/elsewhere"), ref, 5);
    check(QStringLiteral("C11c a folder no athlete holds: not reached, nothing written"),
          !f.reached && reviewOf(rig, ref).value(QStringLiteral("rating")).toInt() == 2);
    f = rig.app->rate(sessionFolder, ShotRef(SessionId::create(), 1), 5);
    check(QStringLiteral("C11d another session's ShotRef: not reached"), !f.reached);
    rig.events.clear();
    f = rig.app->correctClub(sessionFolder, ref, QStringLiteral("8 IRON"));
    check(QStringLiteral("C11e through the door, the Shot's event"), f.reached && f.result.changed() && rig.events == QStringList{ ev("EVT-04", ref) });

    // C12: the carousel, a view of the Session (refactor step 5.2; R-QML-5)
    ShotListModel model;
    model.setSource(shotSourceOf(s));
    const int id = model.rowCount() == 1 ? model.data(model.index(0), ShotListModel::ShotIdRole).toInt() : -1;
    check(QStringLiteral("C12a the carousel holds the shot"), id > 0);
    rig.events.clear();
    QJsonObject before = reviewOf(rig, ref);
    before.remove(QStringLiteral("rating"));
    model.setRating(id, 5);
    QJsonObject after = reviewOf(rig, ref);
    check(QStringLiteral("C12b the rating saved by the Shot"), after.value(QStringLiteral("rating")).toInt() == 5);
    after.remove(QStringLiteral("rating"));
    check(QStringLiteral("C12c …the rating alone: every other review key as it was"), after == before);
    check(QStringLiteral("C12d …and no event"), rig.events.isEmpty());
    model.setClub(id, QStringLiteral("9 IRON"));
    model.setLmClubOverride(id, QStringLiteral("PITCHING WEDGE"));
    model.setLmExcluded(id, true);
    check(QStringLiteral("C12e the declared club, the board's club, its exclusion: EVT-21, EVT-04, EVT-05, once each"),
          rig.events == QStringList{ ev("EVT-21", ref), ev("EVT-04", ref), ev("EVT-05", ref) });
    const QJsonObject rv = reviewOf(rig, ref);
    check(QStringLiteral("C12f …saved to today's keys"),
          rv.value(QStringLiteral("club")).toString() == QStringLiteral("9 IRON")
              && rv.value(QStringLiteral("clubOverride")).toString() == QStringLiteral("PITCHING WEDGE")
              && rv.value(QStringLiteral("excluded")).toBool());

    // A view no Session is given to (no factory, or none holds the folder): no rows of its own, nothing written.
    ShotListModel plain;
    plain.showSession(sessionFolder);
    rig.events.clear();
    const QJsonObject beforePlain = reviewOf(rig, ref);
    plain.setNote(1, QStringLiteral("not saved"));
    check(QStringLiteral("C12g no Session: no rows of its own; nothing written, no event"),
          plain.rowCount() == 0 && reviewOf(rig, ref) == beforePlain && rig.events.isEmpty());
}

// ── V1–V8: the carousel as a view of the Session (refactor step 5.2; R-QML-5, DEC-199) ──────────────────────────
//   V1 EVT-01 (the document lands) inserts one row; no reset
//   V2 a Shot's event from elsewhere (a panel's club, intent, a late reading: EVT-21, 07, 22) changes its row only
//   V3 EVT-03 from elsewhere removes the row; shotTrashed once
//   V4 a re-analysis (EVT-02): the row keeps its id and place; app.state's selection and the replay are kept
//   V5 End (the capture target lets go, today's session the same): no reset, the same ids, the selection kept
//   V6 re-entering the screen (the same live folder): no reset
//   V7 the Session trashed (EVT-20): the view empties, nothing dangles
//   V8 a camera shot that saved nothing, then its device-only document: one row, the same id
//   V9 (lateClipOnTheCard) a late phone clip (EVT-22): the rewrite tells the card its video flag and thumbnail (#208)
static void carouselView()
{
    std::printf("\nV1–V8: the carousel as a view of the Session\n");
    Rig rig;
    Session *s = rig.athlete ? rig.athlete->startSession(kSwing, kPat) : nullptr;
    Shot *first = s ? shotWithDocument(s, rig) : nullptr;
    check(QStringLiteral("V0 a session with a shot"), first != nullptr);
    if (!first) return;
    s->shotDocumentWritten(first->ref());
    const QString folder = s->entry()->folder;

    // app.state over this session, as main.cpp wires it: the live carousel follows liveSessionChanged.
    App &app = *rig.app;
    AppState &st = *app.state();
    QString capture = folder, loaded;
    QStringList stops;
    AppStateDoors d;
    d.currentAthlete = [&rig] { return rig.athlete; };
    d.sessionAt      = [&app](const QString &f) { return app.sessionAt(f); };
    d.captureFolder  = [&capture] { return capture; };
    d.closeCaptureSession = [&capture, &st] { capture.clear(); st.refreshCaptureTarget(); };
    d.todaysFolder   = [&folder] { return folder; };
    d.replayStart    = [&loaded](int, const QString &dir, bool) { loaded = dir; };
    d.replayStop     = [&loaded, &stops] { stops << loaded; loaded.clear(); };
    d.replayFolder   = [&loaded] { return loaded; };
    st.setDoors(d);
    ShotListModel live;
    live.setSourceFactory([&app](const QString &f) { return shotSourceOf(app.sessionAt(f)); });
    QObject::connect(&st, &AppState::liveSessionChanged, &live, [&st, &live] { live.showSession(st.liveSessionFolder()); });
    st.refreshCaptureTarget();
    check(QStringLiteral("V0 the live carousel shows the capture target's session"), live.rowCount() == 1 && live.sessionFolder() == folder);
    QSignalSpy resets(&live, &QAbstractItemModel::modelReset);
    QSignalSpy inserted(&live, &QAbstractItemModel::rowsInserted);
    QSignalSpy changed(&live, &QAbstractItemModel::dataChanged);
    QSignalSpy trashedRows(&live, &ShotListModel::shotTrashed);

    // V1
    Shot *second = shotWithDocument(s, rig);
    check(QStringLiteral("V1 a shot allocated, its document not announced: no row"), second && live.rowCount() == 1);
    s->shotDocumentWritten(second->ref());
    check(QStringLiteral("V1 EVT-01: one row inserted, at the top, no reset"),
          live.rowCount() == 2 && inserted.count() == 1 && resets.isEmpty()
              && live.data(live.index(0), ShotListModel::OrdinalRole).toInt() == second->summary().ordinal);
    const int idFirst = live.idOfShotRef(first->ref()), idSecond = live.idOfShotRef(second->ref());

    // V2
    changed.clear();
    first->setDeclaredClub(QStringLiteral("6 IRON"));            // EVT-21, as a panel sets it
    const int rowFirst = live.data(live.index(1), ShotListModel::ShotIdRole).toInt() == idFirst ? 1 : 0;
    check(QStringLiteral("V2 EVT-21 from elsewhere: that row's dataChanged, showing the Shot's club"),
          changed.count() == 1 && changed.first().at(0).toModelIndex().row() == rowFirst
              && live.data(live.index(rowFirst), ShotListModel::ClubRole).toString() == QStringLiteral("6 IRON"));
    changed.clear();
    first->setShotIntent(QStringLiteral("fade"));                // EVT-07: the row stale before (survey row 2)
    check(QStringLiteral("V2 EVT-07: the row is told"), changed.count() == 1);
    changed.clear();
    const WriteResult late = first->completeCapture(reading("V2"));   // EVT-22: a late reading
    const QList<int> roles = changed.isEmpty() ? QList<int>{} : changed.first().at(2).value<QList<int>>();
    check(QStringLiteral("V2 EVT-22: the row re-reads its Shot, with the board's device role (its cache drops)"),
          late.ok() && changed.count() == 1 && roles.contains(ShotListModel::LmDeviceRole)
              && live.data(live.index(rowFirst), ShotListModel::LmDeviceRole).toString() == first->summary().lmDevice);

    // V4 (before V3, while both rows are there): a re-analysis keeps the row, the selection and the replay.
    st.enterReplay(first->ref().toString(), first->folder());
    const QString replaying = loaded;
    changed.clear();
    QObject::connect(first, &Shot::reanalysed, first, [&rig](const ShotRef &r) { rig.events << QStringLiteral("EVT-02 ") + r.toString(); },
                     Qt::DirectConnection);
    analysis::SwingAnalysis fresh;
    fresh.timeBase      = analysis::TimeBase::Window;
    fresh.score.overall = 41;
    AnalysisOutcome outcome;
    outcome.analysis = &fresh;
    const WriteResult re = first->applyAnalysis(outcome);
    check(QStringLiteral("V4 EVT-02: the row changes in place (the same id and place), no reset"),
          re.changed() && changed.count() == 1 && live.idOfShotRef(first->ref()) == idFirst
              && live.data(live.index(rowFirst), ShotListModel::ShotIdRole).toInt() == idFirst && resets.isEmpty());
    check(QStringLiteral("V4 …the score the Shot now holds"),
          live.data(live.index(rowFirst), ShotListModel::ScoreRole).toInt() == first->summary().score);
    check(QStringLiteral("V4 …app.state's selection and the replay kept"),
          st.selection()->shotRef() == first->ref().toString() && !replaying.isEmpty() && loaded == replaying && stops.isEmpty());

    // V5: End — the capture target lets go, today's session is the same one.
    st.endSession(true);
    check(QStringLiteral("V5 End: no reset, the same ids, the selection kept"),
          resets.isEmpty() && live.idOfShotRef(first->ref()) == idFirst && live.idOfShotRef(second->ref()) == idSecond
              && st.selection()->valid() && stops.isEmpty() && app.session() == s);
    // V6: re-entering the screen names the same session.
    st.setLiveFolder(folder + QLatin1Char('/'));
    check(QStringLiteral("V6 re-entering the screen: no reset"), resets.isEmpty() && live.rowCount() == 2);

    // V3: anyone's trash (EVT-03).
    QString where;
    const ShotRef refSecond = second->ref();
    const WriteResult t = s->trashShot(refSecond, &where);
    check(QStringLiteral("V3 EVT-03 from elsewhere: the row leaves, shotTrashed once"),
          t.ok() && live.rowCount() == 1 && live.idOfShotRef(refSecond) < 0 && trashedRows.count() == 1
              && trashedRows.first().at(0).toInt() == idSecond);

    // V8: a camera shot that saved nothing, then its device-only document (the launch monitor's rescue).
    capture = folder;
    st.refreshCaptureTarget();
    SwingAllocation a;
    Shot *rescued = s->addShot(recording::ShotKind::Captured, &a);
    ShotListModel::CapturedShot card;
    card.shot = rescued ? rescued->ref() : ShotRef();
    card.club = QStringLiteral("7 IRON");
    const int cardId = live.landShot(card, /*documentWritten*/ false);
    const int rowsWithCard = live.rowCount();
    // V8b (refactor 5.10): the golfer edits the card while nothing is saved for it — the rating, the note, the board's
    // exclusion, the declared club and the board's club correction.
    live.setRating(cardId, 4);
    live.setNote(cardId, QStringLiteral("thin, off the toe"));
    live.setLmExcluded(cardId, true);
    live.setClub(cardId, QStringLiteral("6 IRON"));
    live.setLmClubOverride(cardId, QStringLiteral("5 IRON"));
    const QModelIndex cardIx = live.index(0);
    check(QStringLiteral("V8b the card shows the golfer's edits while nothing is saved"),
          live.data(cardIx, ShotListModel::RatingRole).toInt() == 4
              && live.data(cardIx, ShotListModel::NoteRole).toString() == QStringLiteral("thin, off the toe")
              && live.data(cardIx, ShotListModel::LmExcludedRole).toBool()
              && live.data(cardIx, ShotListModel::RecordedClubRole).toString() == QStringLiteral("6 IRON")
              && live.data(cardIx, ShotListModel::ClubRole).toString() == QStringLiteral("5 IRON"));
    rig.events.clear();
    if (rescued) rig.listen(rescued);
    DeviceOnlyMeta meta;
    meta.swingId    = QFileInfo(a.swingFolder).fileName();
    meta.swingIndex = rescued ? rescued->swingNumber() : 0;
    meta.club       = QStringLiteral("8 IRON");   // the document carries a club of its own: the golfer's later edit wins
    const bool wrote = rescued && rig.recordings->create(rescued->ref(), reading("V8"), meta).ok();
    if (wrote) s->shotDocumentWritten(rescued->ref());
    check(QStringLiteral("V8 the card, then its document: one row, the same id, now its Shot's"),
          wrote && rowsWithCard == 2 && live.rowCount() == 2 && live.idOfShotRef(rescued->ref()) == cardId
              && !live.data(live.index(0), ShotListModel::SwingDirRole).toString().isEmpty());
    // V8c: the card's edits are handed to the Shot (its own commands) as the document lands — on the Shot, in the saved
    // document, and on the row, which reads them from the Shot.
    const ShotSummary rs = rescued ? rescued->summary() : ShotSummary();
    check(QStringLiteral("V8c the card's edits are the Shot's: rating, note, exclusion, declared club, correction"),
          rs.rating == 4 && rs.note == QStringLiteral("thin, off the toe") && rs.lmExcluded
              && rs.club == QStringLiteral("6 IRON") && rs.lmClubOverride == QStringLiteral("5 IRON"));
    const QJsonObject rv8 = wrote ? reviewOf(rig, rescued->ref()) : QJsonObject();
    check(QStringLiteral("V8c …saved in the document's review block"),
          rv8.value(QStringLiteral("rating")).toInt() == 4
              && rv8.value(QStringLiteral("note")).toString() == QStringLiteral("thin, off the toe")
              && rv8.value(QStringLiteral("excluded")).toBool()
              && rv8.value(QStringLiteral("club")).toString() == QStringLiteral("6 IRON")
              && rv8.value(QStringLiteral("clubOverride")).toString() == QStringLiteral("5 IRON"));
    check(QStringLiteral("V8c …the row shows them, read from the Shot"),
          live.data(live.index(0), ShotListModel::RatingRole).toInt() == 4
              && live.data(live.index(0), ShotListModel::NoteRole).toString() == QStringLiteral("thin, off the toe")
              && live.data(live.index(0), ShotListModel::LmExcludedRole).toBool()
              && live.data(live.index(0), ShotListModel::RecordedClubRole).toString() == QStringLiteral("6 IRON")
              && live.data(live.index(0), ShotListModel::ClubRole).toString() == QStringLiteral("5 IRON"));
    std::printf("    V8c events: %s\n", qPrintable(rig.events.join(QStringLiteral(", "))));
    check(QStringLiteral("V8c …each handed edit is the Shot's event (club EVT-21, correction EVT-04, exclusion EVT-05)"),
          wrote && rig.events.contains(ev("EVT-21", rescued->ref())) && rig.events.contains(ev("EVT-04", rescued->ref()))
              && rig.events.contains(ev("EVT-05", rescued->ref())));

    // V7: the Session trashed by its Athlete (EVT-20): the view empties.
    capture.clear();
    st.refreshCaptureTarget();
    const SessionId sid = s->id();
    QString gone;
    s->end();
    const WriteResult ts = rig.athlete->trashSession(sid, &gone);
    QCoreApplication::processEvents();
    check(QStringLiteral("V7 the Session trashed: the view is empty and shows no session"),
          ts.ok() && live.rowCount() == 0 && live.sessionFolder().isEmpty());
}

// ── V9: a late phone clip reaches the card (#208) ──────────────────────────────────────────────────────────────
//   A camera shot whose only video is a phone's: its document lands with the clip asked for (a video element with no
//   `file`) and no thumbnail, so its row says "no video". The clip lands after (after End, in the issue): EVT-22. The
//   row's rewrite must tell the delegates every role the re-read summary can move — the video flag and the thumbnail
//   among them — or the card keeps "no video" until it is rebuilt.
static void lateClipOnTheCard()
{
    std::printf("\nV9: a late phone clip reaches the card (#208)\n");
    Rig rig;
    Session *s = rig.athlete ? rig.athlete->startSession(kSwing, kPat) : nullptr;
    Shot *shot = s ? shotWithDocument(s, rig) : nullptr;
    check(QStringLiteral("V9 a session with a shot"), shot != nullptr);
    if (!shot) return;
    // The phone's clip asked for, not arrived: no `file` on its video element, no thumbnail (the fixture's own clip
    // and thumbnail taken out).
    const QString doc = QDir(shot->folder()).filePath(QStringLiteral("swing.json"));
    {
        QFile f(doc);
        QJsonObject root = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
        f.close();
        QJsonArray streams;
        for (const QJsonValue &v : root.value(QStringLiteral("streams")).toArray()) {
            QJsonObject el = v.toObject();
            if (el.value(QStringLiteral("kind")).toString() == QLatin1String("video")) {
                el.remove(QStringLiteral("file"));
                el[QStringLiteral("alias")] = QStringLiteral("phone");
            }
            streams.append(el);
        }
        root[QStringLiteral("streams")] = streams;
        root.remove(QStringLiteral("thumbnail"));
        check(QStringLiteral("V9 the document staged with the clip owed"),
              !root.isEmpty() && f.open(QIODevice::WriteOnly | QIODevice::Truncate)
                  && f.write(QJsonDocument(root).toJson()) > 0);
    }
    s->shotDocumentWritten(shot->ref());

    ShotListModel live;
    live.setSource(shotSourceOf(s));
    check(QStringLiteral("V9 the card: one row, no video yet"),
          live.rowCount() == 1 && !live.data(live.index(0), ShotListModel::HasVideoRole).toBool());
    QSignalSpy changed(&live, &QAbstractItemModel::dataChanged);
    QSignalSpy resets(&live, &QAbstractItemModel::modelReset);

    StreamArrival clip;
    clip.alias = QStringLiteral("phone");
    clip.origin.transport    = QStringLiteral("ppcp");
    clip.origin.captureId    = QStringLiteral("cap-v9");
    clip.origin.completeness = QStringLiteral("complete");
    clip.fileName = QStringLiteral("phone.mp4");
    const WriteResult w = shot->completeCapture(clip);   // EVT-22: the late part
    const QList<int> roles = changed.isEmpty() ? QList<int>{ -1 } : changed.first().at(2).value<QList<int>>();
    // An empty role list is "every role" (QAbstractItemModel::dataChanged).
    const auto told = [&roles](int role) { return roles.isEmpty() || roles.contains(role); };
    check(QStringLiteral("V9 the clip saved: the Shot now holds video"), w.changed() && shot->summary().hasVideo);
    check(QStringLiteral("V9 EVT-22: one dataChanged for the row, no reset"), changed.count() == 1 && resets.isEmpty());
    check(QStringLiteral("V9 …it carries the video flag and the thumbnail (#208)"),
          told(ShotListModel::HasVideoRole) && told(ShotListModel::ThumbnailSourceRole));
    check(QStringLiteral("V9 …and the time and the number, which a re-read summary can move too"),
          told(ShotListModel::TimestampLabelRole) && told(ShotListModel::OrdinalRole));
    check(QStringLiteral("V9 …and the card shows the video"), live.data(live.index(0), ShotListModel::HasVideoRole).toBool());
}

// ── C13 (refactor 5.4a): a panel edit through the adapter is in session_diagnostics_model_test ("C13 — a panel edit
// through the adapter emits the Shot's and the Session's events"): until then it went through the old model here. ──────

// ── C14: applyAnalysis and the Shot's summary (refactor step 3.4b-ii) ────────────────────────────────────────

// A launch-monitor-only shot of `s` (its document written as the board's standalone shot writes it).
static Shot *deviceOnlyShot(Session *s, Rig &rig, const char *id)
{
    SwingAllocation a;
    Shot *shot = s->addShot(recording::ShotKind::DeviceOnly, &a);
    if (!shot) return nullptr;
    DeviceOnlyMeta meta;
    meta.swingId    = QFileInfo(a.swingFolder).fileName();
    meta.swingIndex = shot->swingNumber();   // as the launch monitor records it
    meta.standalone = true;
    if (!rig.recordings->create(shot->ref(), reading(id), meta).ok()) return nullptr;
    rig.listen(shot);
    QObject::connect(shot, &Shot::reanalysed, shot,
                     [&rig](const ShotRef &r) { rig.events << QStringLiteral("EVT-02 ") + r.toString(); },
                     Qt::DirectConnection);
    return shot;
}

static void analysisAndSummary()
{
    std::printf("\nC14: applyAnalysis (EVT-02) and the Shot's summary\n");
    Rig rig;
    Session *s = rig.athlete ? rig.athlete->startSession(kSwing, kPat) : nullptr;
    Shot *shot = s ? shotWithDocument(s, rig) : nullptr;
    Shot *lm   = s ? deviceOnlyShot(s, rig, "C14") : nullptr;
    check(QStringLiteral("C14a a session, a camera shot and a launch-monitor-only one"), shot && lm);
    if (!shot || !lm) return;
    QObject::connect(shot, &Shot::reanalysed, shot,
                     [&rig](const ShotRef &r) { rig.events << QStringLiteral("EVT-02 ") + r.toString(); },
                     Qt::DirectConnection);
    const ShotRef ref = shot->ref();

    // The summary: the repository's cheap read, field for field.
    const ShotSummary disk = rig.recordings->readSummary(ref);
    const ShotSummary &sum = shot->summary();
    check(QStringLiteral("C14b summary(): read from its document (readSummary), field for field"),
          sum.ok && disk.ok && sum.ordinal == disk.ordinal && sum.club == disk.club && sum.score == disk.score
              && sum.metrics == disk.metrics && sum.rating == disk.rating && sum.note == disk.note
              && sum.lmExcluded == disk.lmExcluded && sum.lmClubOverride == disk.lmClubOverride
              && sum.hasVideo == disk.hasVideo && sum.dataWarning == disk.dataWarning);
    shot->rate(3);
    shot->note(QStringLiteral("summary note"));
    shot->setDeclaredClub(QStringLiteral("8 IRON"));
    shot->correctClub(QStringLiteral("9 IRON"));
    shot->excludeReading(true);
    const ShotSummary now = rig.recordings->readSummary(ref);
    check(QStringLiteral("C14c the summary follows its saves (kept, not read again), as its document now reads"),
          shot->summary().rating == 3 && shot->summary().note == QStringLiteral("summary note")
              && shot->summary().club == QStringLiteral("8 IRON") && shot->summary().lmClubOverride == QStringLiteral("9 IRON")
              && shot->summary().lmExcluded
              && now.rating == 3 && now.note == QStringLiteral("summary note") && now.club == QStringLiteral("8 IRON")
              && now.lmClubOverride == QStringLiteral("9 IRON") && now.lmExcluded);

    // applyAnalysis: an analysis written → EVT-02 once; the summary read again.
    rig.events.clear();
    analysis::SwingAnalysis fresh;
    fresh.timeBase      = analysis::TimeBase::Window;
    fresh.score.overall = 37;
    AnalysisOutcome outcome;
    outcome.analysis = &fresh;
    WriteResult w = shot->applyAnalysis(outcome);
    check(QStringLiteral("C14d applyAnalysis: written by the Shot (writeAnalysis)"), w.changed());
    check(QStringLiteral("C14e EVT-02 once"), rig.events == QStringList{ ev("EVT-02", ref) });
    const ShotSummary after = rig.recordings->readSummary(ref);
    check(QStringLiteral("C14f …and the summary read again: what the document now holds (its review kept)"),
          shot->summary().score == after.score && shot->summary().metrics == after.metrics
              && shot->summary().rating == 3 && after.rating == 3);

    // A launch-monitor-only shot whose rows are current: nothing changed, nothing emitted.
    rig.events.clear();
    w = lm->applyAnalysis(AnalysisOutcome{});
    check(QStringLiteral("C14g launch monitor only, rows current: Unchanged, no event"),
          w.ok() && !w.changed() && rig.events.isEmpty());

    // A failed save changes nothing in the summary and emits nothing.
    SwingAllocation a;
    Shot *bare = s->addShot(recording::ShotKind::Captured, &a);
    rig.events.clear();
    const bool bareRead = bare && bare->summary().ok;
    w = bare ? bare->rate(5) : WriteResult{};
    check(QStringLiteral("C14h a shot with no document: no summary yet; rate refused, the summary unchanged, no event"),
          bare && !bareRead && !w.ok() && bare->summary().rating == 0 && rig.events.isEmpty());
    w = bare ? bare->applyAnalysis(outcome) : WriteResult{};
    check(QStringLiteral("C14i applyAnalysis on it: NoDocument (the empty-document guard), no event"),
          w.status == WriteResult::Status::NoDocument && rig.events.isEmpty());
}

// ── C15: the carousel's rows over the Shots ─────────────────────────────────────────────────────────────────

static void rowsOverShots()
{
    std::printf("\nC15: the carousel's rows over the Shots\n");
    Rig rig;
    Session *s = rig.athlete ? rig.athlete->startSession(kSwing, kPat) : nullptr;
    Shot *one = s ? shotWithDocument(s, rig) : nullptr;
    Shot *two = s ? deviceOnlyShot(s, rig, "C15") : nullptr;
    check(QStringLiteral("C15a a session with two shots"), one && two);
    if (!one || !two) return;
    QStringList trashed;
    QObject::connect(s, &Session::shotTrashed, s, [&trashed](const ShotRef &r) { trashed << r.toString(); },
                     Qt::DirectConnection);
    const QString dir = s->entry()->folder;

    ShotListModel model;
    model.setSource(shotSourceOf(s));
    const auto roleOf = [&model](int row, int role) { return model.data(model.index(row), role); };
    const QString dirOne = one->folder();   // the Shot's folder (a property of the Shot)
    check(QStringLiteral("C15b the view lists the Session's Shots, newest first, the swingDir the Shot's folder"),
          model.rowCount() == 2 && roleOf(0, ShotListModel::OrdinalRole).toInt() == 2
              && roleOf(1, ShotListModel::OrdinalRole).toInt() == 1
              && roleOf(1, ShotListModel::SwingDirRole).toString() == dirOne);
    const ShotSummary &sum = one->summary();
    check(QStringLiteral("C15c a row is its Shot's summary (club, score, metrics, lmDevice)"),
          roleOf(1, ShotListModel::ClubRole).toString() == sum.club && roleOf(1, ShotListModel::ScoreRole).toInt() == sum.score
              && roleOf(1, ShotListModel::MetricsRole).toMap() == sum.metrics
              && roleOf(0, ShotListModel::LmDeviceRole).toString() == two->summary().lmDevice);

    const int idOne = roleOf(1, ShotListModel::ShotIdRole).toInt();
    model.setRating(idOne, 4);
    model.setClub(idOne, QStringLiteral("7 IRON"));
    model.setLmExcluded(idOne, true);
    check(QStringLiteral("C15d an edit's row shows what the Shot holds after its save"),
          roleOf(1, ShotListModel::RatingRole).toInt() == 4 && one->summary().rating == 4
              && roleOf(1, ShotListModel::ClubRole).toString() == QStringLiteral("7 IRON")
              && roleOf(1, ShotListModel::LmExcludedRole).toBool() && one->summary().lmExcluded);

    // A save that fails (the swing document made unreplaceable: its folder read-only on POSIX, the document held open
    // with no delete share on Windows, which ignores a folder's permissions; #244): the row shows the shot as it still is.
    {
        test::RefusedFile refused(SwingStore::ppswPath(one->folder()), test::RefusedFile::Refuse::Replace);
        model.setRating(idOne, 1);
        const bool wasRefused = refused.refused();
        refused.release();
        const int row = roleOf(1, ShotListModel::RatingRole).toInt(), held = one->summary().rating,
                  onFile = reviewOf(rig, one->ref()).value(QStringLiteral("rating")).toInt();
        std::printf("        refused %d, row %d, Shot %d, file %d\n", int(wasRefused), row, held, onFile);
        check(QStringLiteral("C15e a failed save: the row keeps the Shot's value (4), the file too"),
              wasRefused && row == 4 && held == 4 && onFile == 4);
    }

    // A live view: capture's call finds the Shot's row, whose facts are its Shot's.
    ShotListModel live;
    live.setSource(shotSourceOf(s));
    ShotListModel::CapturedShot captured;
    captured.shot = one->ref();
    captured.club = QStringLiteral("DRIVER");   // capture's value: not shown, the Shot's is
    const int liveId = live.landShot(captured, /*documentWritten*/ true);
    int liveRow = -1;   // its row: the view shows the session's two shots, newest first
    for (int r = 0; r < live.rowCount(); ++r)
        if (live.data(live.index(r), ShotListModel::ShotIdRole).toInt() == liveId) liveRow = r;
    check(QStringLiteral("C15f a live row: its number and review facts from its Shot"),
          liveRow >= 0 && live.ordinalForId(liveId) == one->summary().ordinal
              && live.data(live.index(liveRow), ShotListModel::RatingRole).toInt() == 4
              && live.data(live.index(liveRow), ShotListModel::ClubRole).toString() == QStringLiteral("7 IRON"));

    // The carousel's delete: the Session's trashShot (EVT-03), the Shot let go, the number kept.
    const int idTwo = roleOf(0, ShotListModel::ShotIdRole).toInt();
    const ShotRef refTwo = two->ref();
    int finished = -1;
    QObject::connect(&model, &ShotListModel::trashFinished, &model, [&finished](int, int moved, const QString &) { finished = moved; });
    model.trashShots({ idTwo });
    waitFor([&finished] { return finished >= 0; }, 3000);
    check(QStringLiteral("C15g the delete is the Session's: EVT-03 once, the row gone"),
          finished == 1 && trashed == QStringList{ refTwo.toString() } && model.rowCount() == 1);
    check(QStringLiteral("C15h …the Shot let go by its Session, its folder in the library's own trash"),
          s->shot(refTwo) == nullptr && !QFileInfo::exists(dir + QStringLiteral("/swing_0002")));
    SwingAllocation next;
    Shot *third = s->addShot(recording::ShotKind::Captured, &next);
    check(QStringLiteral("C15i …and its number never reissued (the next is 3)"), third && third->swingNumber() == 3);

    // A view no Session is given to: no rows of its own, so nothing of it can be trashed — the swing stays, no event.
    ShotListModel plain;
    plain.showSession(dir);
    rig.events.clear();
    const bool plainMoved = plain.moveToTrash(1);
    check(QStringLiteral("C15j no Session: no rows of its own; nothing trashed (the swing stays, no event)"),
          plain.rowCount() == 0 && !plainMoved && QFileInfo::exists(one->folder()) && rig.events.isEmpty()
              && trashed.size() == 1);

    // C15k: a held session is answered from memory (round 2: no session.json read per row, edit or refresh).
    {
        const QString file = SessionFiles::sessionFile(dir);
        // Unreadable: its permissions cleared on POSIX, held open by this test on Windows (#244).
        test::RefusedFile unopenable(file, test::RefusedFile::Refuse::Open);
        const bool wasRefused = unopenable.refused();
        Session *again = rig.athlete->sessionAt(dir);
        unopenable.release();
        check(QStringLiteral("C15k a held session: Athlete::sessionAt answers it without reading its session file"),
              wasRefused && QFile::exists(file) && again == s);
    }
}

// ── C16: re-analysis through the Shot ───────────────────────────────────────────────────────────────────────

// `app`, counting the write-backs it carries to a Shot.
struct WriteBackCountingApp : App {
    using App::App;
    int writeBacks = 0;
    ForwardResult applyAnalysis(const QString &sessionFolder, const ShotRef &shot, const AnalysisOutcome &outcome,
                                const PersistOptions &options) override
    {
        const ForwardResult f = App::applyAnalysis(sessionFolder, shot, outcome, options);
        if (f.reached) ++writeBacks;
        return f;
    }
};

static void reanalysisThroughTheShot()
{
    std::printf("\nC16: re-analysis reads through the Shot and writes back by Shot::applyAnalysis\n");
    Rig rig;
    Session *s = rig.athlete ? rig.athlete->startSession(kSwing, kPat) : nullptr;
    Shot *shot = s ? shotWithDocument(s, rig) : nullptr;
    Shot *lm   = s ? deviceOnlyShot(s, rig, "C16") : nullptr;
    check(QStringLiteral("C16a a session, a camera shot and a launch-monitor-only one"), shot && lm);
    if (!shot || !lm) return;
    QObject::connect(shot, &Shot::reanalysed, shot,
                     [&rig](const ShotRef &r) { rig.events << QStringLiteral("EVT-02 ") + r.toString(); },
                     Qt::DirectConnection);
    WriteBackCountingApp door([&rig] { return rig.athlete; });

    ReanalysisController ctrl;
    ctrl.setShotCommands(&door);
    int ok = -1, failed = -1;
    QObject::connect(&ctrl, &ReanalysisController::reanalyseFinished, &ctrl,
                     [&ok, &failed](int a, int b, const QString &) { ok = a; failed = b; });

    g_stubResult = analysis::ReanalyzeResult{};
    g_stubResult.ok = true;
    g_stubResult.analysis.score  = 55;
    g_stubResult.analysis.detail = std::make_shared<analysis::SwingAnalysis>();
    g_stubResult.analysis.detail->timeBase      = analysis::TimeBase::Window;
    g_stubResult.analysis.detail->score.overall = 55;
    g_stubCalls = 0;
    rig.events.clear();
    ctrl.reanalyse(QVariantList{ shot->folder() });
    waitFor([&ok] { return ok >= 0; }, 5000);
    check(QStringLiteral("C16b the worker read the shot through its Shot's RecordingSource (its ShotRef)"),
          g_stubCalls == 1 && g_stubReadShot == shot->ref());
    check(QStringLiteral("C16c the write-back is the Shot's (through `app`): EVT-02 once"),
          ok == 1 && failed == 0 && door.writeBacks == 1 && rig.events == QStringList{ ev("EVT-02", shot->ref()) });

    ok = failed = -1;
    rig.events.clear();
    g_stubCalls = 0;
    ctrl.reanalyse(QVariantList{ lm->folder() });
    waitFor([&ok] { return ok >= 0; }, 3000);
    check(QStringLiteral("C16d launch monitor only (its Shot's kind): rows through the Shot, current — no analysis, no event"),
          ok == 1 && g_stubCalls == 0 && door.writeBacks == 2 && rig.events.isEmpty());

    // No door (step 5.1: N1's reader and write-back are gone): refused, nothing read or written, no event.
    ReanalysisController plain;
    ok = failed = -1;
    QObject::connect(&plain, &ReanalysisController::reanalyseFinished, &plain,
                     [&ok, &failed](int a, int b, const QString &) { ok = a; failed = b; });
    rig.events.clear();
    g_stubCalls = 0;
    plain.reanalyse(QVariantList{ shot->folder() });
    waitFor([&ok] { return ok >= 0; }, 5000);
    check(QStringLiteral("C16e no door: refused — nothing read, nothing written, no event"),
          ok == 0 && failed == 1 && g_stubCalls == 0 && door.writeBacks == 2 && rig.events.isEmpty());

    // C16f (round 2, CN-1): the rescue — a camera shot (allocated captured) whose export failed and whose folder the
    // launch monitor filled. Device-only by its document (R-REC-8), so its rows are derived again, through its Shot.
    SwingAllocation ra;
    Shot *rescued = s->addShot(recording::ShotKind::Captured, &ra);
    DeviceOnlyMeta rm;
    rm.swingId    = QFileInfo(ra.swingFolder).fileName();
    rm.swingIndex = rescued ? rescued->swingNumber() : 0;
    rm.standalone = false;   // the rescue records no kind: the content rule reads it
    const bool rescueWritten = rescued && rig.recordings->create(rescued->ref(), reading("C16f"), rm).ok();
    ok = failed = -1;
    g_stubCalls = 0;
    ctrl.reanalyse(QVariantList{ rescued ? rescued->folder() : QString() });
    waitFor([&ok] { return ok >= 0; }, 3000);
    check(QStringLiteral("C16f a rescued camera shot (its Shot says captured, its document device-only): not analysed, "
                         "its rows refreshed through the Shot, as before"),
          rescueWritten && rescued->kind() == recording::ShotKind::Captured
              && rig.recordings->isDeviceOnly(rescued->ref()) && ok == 1 && failed == 0 && g_stubCalls == 0
              && door.writeBacks == 3);

    // C16g (round 2, review 1 CN-3b): THE SAME BYTES AS HEAD'S WRITE-BACK. Two shots with the same camera document:
    // A re-analysed through the controller (the Shot's applyAnalysis, through `app`), B written as HEAD wrote it
    // (persistReanalysis straight to the repository's writeAnalysis), with the same result and options. Their
    // documents must be the same, block for block, and the swing files byte for byte.
    Shot *a = shotWithDocument(s, rig);
    Shot *b = shotWithDocument(s, rig);
    if (a && b) {
        g_stubResult = analysis::ReanalyzeResult{};
        g_stubResult.ok = true;
        g_stubResult.analysis.score  = 61;
        g_stubResult.analysis.detail = std::make_shared<analysis::SwingAnalysis>();
        g_stubResult.analysis.detail->timeBase      = analysis::TimeBase::Window;
        g_stubResult.analysis.detail->score.overall = 61;
        CaptureIntegrityVerdict cv;
        cv.camerasChecked = 1;
        g_stubResult.captureIntegrity = cv;   // the IMU verdict unreachable this pass: withdrawn on both
        ok = failed = -1;
        ctrl.reanalyse(QVariantList{ a->folder() });
        waitFor([&ok] { return ok >= 0; }, 5000);
        const ShotRef refB = b->ref();
        const analysis::PersistedReanalysis headWay = analysis::persistReanalysis(
            [&rig, refB](const AnalysisOutcome &o, const PersistOptions &opt) { return rig.recordings->writeAnalysis(refB, o, opt); },
            &g_stubResult, PersistOptions{});
        const QJsonObject docA = SwingStore::load(a->folder());
        const QJsonObject docB = SwingStore::load(b->folder());
        QStringList differ;
        for (const QString &k : docA.keys() + docB.keys())
            if (docA.value(k) != docB.value(k) && !differ.contains(k)) differ << k;
        const auto bytes = [](const QString &dir) {
            QFile f(QFile::exists(SwingStore::ppswPath(dir)) ? SwingStore::ppswPath(dir)
                                                             : dir + QStringLiteral("/swing.json"));
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        };
        const QByteArray bytesA = bytes(a->folder()), bytesB = bytes(b->folder());
        if (!differ.isEmpty())
            std::printf("    C16g: blocks that differ: %s\n", qPrintable(differ.join(QStringLiteral(", "))));
        check(QStringLiteral("C16g the Shot's write-back writes what HEAD's did: the same document, block for block, "
                             "and the same swing-file bytes"),
              ok == 1 && headWay.ok() && door.writeBacks == 4 && !docA.isEmpty() && differ.isEmpty()
                  && !bytesA.isEmpty() && bytesA == bytesB);
    } else {
        check(QStringLiteral("C16g two shots with the same camera document"), false);
    }
}

// C17 (refactor step 5.1; J-67 1: a session belongs to its athlete whoever is current): a late reading — the launch
// monitor's, a phone clip's, a re-analysis' — that lands after End and a switch of golfer reaches its own session through
// the door, and is saved there, never refused or written elsewhere.
static void lateWriteAfterASwitch()
{
    std::printf("\nC17 a late reading after End and a switch of golfer: its own session's Shot\n");
    Rig rig;
    rig.app->setAthletes([&rig] { return rig.athletes->heldAthletes(); });   // main.cpp's
    Athlete *first = rig.athlete;
    Session *s = first ? first->startSession(kSwing, kPat) : nullptr;
    Shot *shot = s ? shotWithDocument(s, rig) : nullptr;
    check(QStringLiteral("C17a the first golfer's session and shot"), shot != nullptr);
    if (!shot) return;
    const ShotRef ref = shot->ref();
    const QString folder = s->entry()->folder;
    check(QStringLiteral("C17b End"), s->end().ok());
    AthleteRecord other;
    other.name = QStringLiteral("Next Golfer");
    rig.athlete = rig.athletes->create(other).athlete;   // the switch: `app`'s current athlete is now the next golfer
    check(QStringLiteral("C17c the next golfer is current"), rig.athlete && rig.athlete != first);
    rig.events.clear();
    bool replaced = false;
    const ForwardResult f = rig.app->completeCapture(folder, ref, reading("c17"), QJsonObject(), &replaced);
    check(QStringLiteral("C17d the late reading reaches the first golfer's Shot and is saved (EVT-22 once)"),
          f.reached && f.result.ok() && rig.events == QStringList{ ev("EVT-22", ref) });
    const ForwardResult r = rig.app->rate(folder, ref, 3);
    check(QStringLiteral("C17e …and so does a late edit: saved in its own session's swing"),
          r.reached && r.result.ok() && reviewOf(rig, ref).value(QStringLiteral("rating")).toInt() == 3);
    check(QStringLiteral("C17f the next golfer holds no session for that folder (nothing written elsewhere)"),
          rig.athlete->sessionAt(folder) == nullptr && rig.athlete->index().isEmpty());
}

// ── C18: the Shot's measurements follow its late part and its re-analysis (refactor 6.1a; R-INS-2, EVT-22) ──────

static void measurementsFollowTheShot()
{
    std::printf("\nC18: the Shot's one set of measurements: a late reading lands in it, before the event\n");
    using analysis::Instrument;
    using analysis::MeasurementKind;
    Rig rig;
    Session *s = rig.athlete ? rig.athlete->startSession(kSwing, kPat) : nullptr;
    Shot *shot = s ? shotWithDocument(s, rig) : nullptr;
    check(QStringLiteral("C18a a session and a camera shot"), shot != nullptr);
    if (!shot) return;
    const ShotRef ref = shot->ref();
    auto lmSpeed = [shot] {
        return shot->measurements().values(Instrument::LaunchMonitor, MeasurementKind::Reading)
                   .value(QStringLiteral("lm.clubheadSpeed"), -1.0);
    };
    check(QStringLiteral("C18b before any reading: no launch monitor reading in its set"), lmSpeed() == -1.0);

    WriteResult w = shot->completeCapture(reading("c18"));
    check(QStringLiteral("C18c a reading lands: the set holds it, tagged the monitor's reading (80 mph)"),
          w.ok() && lmSpeed() == 80.0);
    const analysis::Measurement *m = shot->measurements().byKey(QStringLiteral("lm.ballSpeed"));
    check(QStringLiteral("C18d …each value with its metric, source and kind"),
          m && m->metric == QStringLiteral("ballSpeed") && m->source.instrument == Instrument::LaunchMonitor
              && m->kind == MeasurementKind::Reading && m->value && *m->value == 112.0);

    // The set is KEPT now. A fuller reading follows (the late club data): inside the EVT-22 listener the set is the new one.
    double seenInEvent = -2.0;
    const auto c = QObject::connect(shot, &Shot::captureCompleted, shot,
                                    [&](const ShotRef &) { seenInEvent = lmSpeed(); }, Qt::DirectConnection);
    lm::LaunchMonitorReading fuller = reading("c18");
    fuller.clubheadSpeed = 85.5;
    w = shot->completeCapture(fuller);
    QObject::disconnect(c);
    check(QStringLiteral("C18e a later reading: the Shot dropped its kept set BEFORE EVT-22 (the listener reads 85.5, got %1)")
              .arg(seenInEvent),
          w.changed() && seenInEvent == 85.5);

    // A re-analysis: the set is read again with it, and the carried reading stays in it.
    bool sameAsDocument = false;
    const auto c2 = QObject::connect(shot, &Shot::reanalysed, shot, [&](const ShotRef &) {
        const analysis::MeasurementSet disk = rig.recordings->readMeasurements(ref);
        const analysis::MeasurementSet &kept = shot->measurements();
        sameAsDocument = disk.items.size() == kept.items.size();
        for (size_t i = 0; sameAsDocument && i < disk.items.size(); ++i)
            sameAsDocument = disk.items[i].key == kept.items[i].key && disk.items[i].value == kept.items[i].value;
    }, Qt::DirectConnection);
    analysis::SwingAnalysis fresh;
    fresh.timeBase      = analysis::TimeBase::Window;
    fresh.score.overall = 41;
    AnalysisOutcome outcome;
    outcome.analysis = &fresh;
    w = shot->applyAnalysis(outcome);
    QObject::disconnect(c2);
    check(QStringLiteral("C18f a re-analysis (EVT-02): the set is the document's again, read before the event"),
          w.changed() && sameAsDocument);
    check(QStringLiteral("C18g …and the monitor's reading is still in it (re-analysis carries what it cannot recompute)"),
          lmSpeed() == 85.5);

    // Not reached: a late reading for a folder no Session holds is refused, and nothing is written.
    bool replaced = false;
    const ForwardResult f = rig.app->completeCapture(rig.dir.path() + QStringLiteral("/nobody/2026-01-01_session_01"),
                                                     ref, reading("c18x"), QJsonObject(), &replaced);
    check(QStringLiteral("C18h a folder no Session holds: not reached (the controller refuses it: no write of its own)"),
          !f.reached && lmSpeed() == 85.5);
}

// ── C19: a reading spelled another way still reaches its Shot (refactor 6.1a fix round; Windows is the primary platform) ──
//
// The controller names the swing by the folder the device's shot was filed under (a path string) and translates it at
// the edge: RecordingRepository::shotOfFolder, then the door's Athlete::sessionAt. A trailing separator, a doubled one,
// a "." step, and on Windows a backslash or another case (C:\ against c:/) is the SAME folder, and must never refuse
// a reading (sameFolder: cleaned, and case-insensitive on Windows).
static void spellingsReachTheShot()
{
    std::printf("\nC19: a late reading whose folder is spelled another way still reaches its Shot\n");
    using analysis::Instrument;
    using analysis::MeasurementKind;
    // The rule itself, both platforms' cases, on any machine.
    check(QStringLiteral("C19a one folder: a trailing slash, a doubled one, a \".\" step"),
          sameFolder(QStringLiteral("/lib/golfer/s1/"), QStringLiteral("/lib//golfer/./s1"))
              && !sameFolder(QStringLiteral("/lib/golfer/s1"), QStringLiteral("/lib/golfer/s2")));
    check(QStringLiteral("C19b Windows' rule: another case (and drive letter) is the same folder"),
          sameFolder(QStringLiteral("C:/Users/Golfer/Lib/S1"), QStringLiteral("c:/users/golfer/lib/s1/"), Qt::CaseInsensitive));
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity platformCase = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity platformCase = Qt::CaseSensitive;
#endif
    check(QStringLiteral("C19c this platform's rule (%1)")
              .arg(kFolderNameCase == Qt::CaseInsensitive ? QStringLiteral("case-insensitive") : QStringLiteral("case-sensitive")),
          kFolderNameCase == platformCase);

    Rig rig;
    Session *s = rig.athlete ? rig.athlete->startSession(kSwing, kPat) : nullptr;
    Shot *shot = s ? shotWithDocument(s, rig) : nullptr;
    check(QStringLiteral("C19d a session and a camera shot"), shot != nullptr);
    if (!shot) return;
    const ShotRef ref = shot->ref();
    const QString folder = rig.recordings->swingFolder(ref);
    QString dotted = folder;
    dotted.insert(dotted.lastIndexOf(QLatin1Char('/')), QStringLiteral("/."));
    QString doubled = folder;
    doubled.insert(doubled.lastIndexOf(QLatin1Char('/')), QLatin1Char('/'));
    QStringList spellings{ folder + QLatin1Char('/'), dotted, doubled };
#ifdef Q_OS_WIN
    // Windows: backslashes, and the library's path in another case (the swing folder's own name is ours, as allocated).
    const int cut = folder.lastIndexOf(QLatin1Char('/'));
    const QString above = folder.left(cut), swingName = folder.mid(cut);
    spellings << QDir::toNativeSeparators(folder) << above.toUpper() + swingName
              << QDir::toNativeSeparators(above.toLower() + swingName) + QLatin1Char('\\');
#endif
    double speed = 80.0;
    for (const QString &spelt : spellings) {
        // As LaunchMonitorController::completeCapture does it: the repository's edge, then the door.
        speed += 1.0;
        lm::LaunchMonitorReading r = reading("c19");
        r.clubheadSpeed = speed;
        const ShotRef found = rig.recordings->shotOfFolder(spelt);
        bool replaced = false;
        const ForwardResult f = rig.app->completeCapture(sessionFolderOf(spelt), found, r, QJsonObject(), &replaced);
        const double got = shot->measurements().values(Instrument::LaunchMonitor, MeasurementKind::Reading)
                               .value(QStringLiteral("lm.clubheadSpeed"), -1.0);
        check(QStringLiteral("C19e \"%1\": the same Shot, reached and saved (club speed %2)").arg(spelt).arg(got),
              found == ref && f.reached && f.result.ok() && got == speed);
    }
}

int main(int argc, char **argv)
{
    // ppSettings() is IniFormat/UserScope: pointed at a scratch folder before the first QSettings (never the live INI).
    pinpoint::test::IsolatedStores stores;   // #187
    QCoreApplication app(argc, argv);
    pinpoint::installProcessRuleData();   // the model's packs and norms (refactor step 3.2)
    std::printf("shot_commands_test\n");
    if (qEnvironmentVariableIsEmpty("PINPOINT_CORE_PACK")) {
        std::printf("PINPOINT_CORE_PACK is unset — run this through ctest, which sets the shipped content.\n");
        return 1;
    }
    shotCommands();
    sessionIntent();
    doorAndCarousel();
    analysisAndSummary();
    rowsOverShots();
    carouselView();
    lateClipOnTheCard();
    reanalysisThroughTheShot();
    lateWriteAfterASwitch();
    measurementsFollowTheShot();
    spellingsReachTheShot();
    std::printf("\n%s: %d of %d checks failed\n", g_fail ? "FAIL" : "PASS", g_fail, g_run);
    return g_fail ? 1 : 0;
}
