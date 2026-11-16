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
//   C13 A PANEL EDIT THROUGH THE OLD MODEL EMITS THE SAME EVENT: SessionDiagnosticsModel's setShotIntent → EVT-07,
//       correctOutcome → EVT-06 (and the Athlete's tuning line), declareMiss → EVT-10, setIntentFrom → EVT-08,
//       each saved where it always was; flagForReview → the Athlete appends its "flag" line (one writer of the
//       tuning log: review 1), no event
//
//   cmake --build build/tests --target shot_commands_test -j 6
//   ctest --test-dir build/tests -R '^shot_commands_test$' --output-on-failure

#include "../athlete.h"
#include "../session.h"
#include "../shot.h"
#include "../../App/app.h"
#include "../../Core/pp_settings.h"
#include "../../Export/athlete_files.h"
#include "../../Export/athlete_store.h"
#include "../../Export/rule_data_install.h"
#include "../../Export/session_diagnostics_files.h"
#include "../../Export/session_files.h"
#include "../../Export/session_store.h"
#include "../../Export/swing_store.h"
#include "../../Export/swing_store_repository.h"
#include "../../Gui/diagnostics/session_diagnostics_model.h"
#include "../../Gui/shot/shot_list_model.h"
#include "../../LaunchMonitor/launch_monitor_reading.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QThreadPool>

#include <cstdio>
#include <memory>

using namespace pinpoint;

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
    check(QStringLiteral("C11c a folder no Session holds: not reached, nothing written (N1)"),
          !f.reached && reviewOf(rig, ref).value(QStringLiteral("rating")).toInt() == 2);
    f = rig.app->rate(sessionFolder, ShotRef(SessionId::create(), 1), 5);
    check(QStringLiteral("C11d another session's ShotRef: not reached"), !f.reached);
    rig.events.clear();
    f = rig.app->correctClub(sessionFolder, ref, QStringLiteral("8 IRON"));
    check(QStringLiteral("C11e through the door, the Shot's event"), f.reached && f.result.changed() && rig.events == QStringList{ ev("EVT-04", ref) });

    // C12: the carousel, through the door
    ShotListModel model;
    model.setRecordingRepository(rig.recordings.get());
    model.setShotCommands(rig.app.get());
    model.loadSessionDir(sessionFolder);
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

    // Without a door: today's write-through (the interim N1 path), rating with the note and club.
    ShotListModel plain;
    plain.setRecordingRepository(rig.recordings.get());
    plain.loadSessionDir(sessionFolder);
    rig.events.clear();
    plain.setNote(plain.data(plain.index(0), ShotListModel::ShotIdRole).toInt(), QStringLiteral("today's path"));
    const QJsonObject pv = reviewOf(rig, ref);
    check(QStringLiteral("C12g no door: today's write-through (note, with rating and club), no event"),
          pv.value(QStringLiteral("note")).toString() == QStringLiteral("today's path")
              && pv.value(QStringLiteral("rating")).toInt() == 5 && pv.value(QStringLiteral("club")).toString() == QStringLiteral("9 IRON")
              && rig.events.isEmpty());
}

// ── C13: the old model's golfer commands ────────────────────────────────────────────────────────────────

// `app`, counting the flags it carries to the Athlete (so the test can tell the door's append from the model's own).
struct CountingApp : App {
    using App::App;
    int flags = 0;
    ForwardResult flagForTuning(const QString &sessionFolder, const ShotRef &shot, const TuningFlagNote &note) override
    {
        ++flags;
        return App::flagForTuning(sessionFolder, shot, note);
    }
};

static void oldModel()
{
    std::printf("\nC13: a panel edit through the old model emits the same event\n");
    Rig rig;
    Session *s = rig.athlete ? rig.athlete->startSession(kSwing, kPat) : nullptr;
    Shot *one = s ? shotWithDocument(s, rig) : nullptr;
    Shot *two = s ? shotWithDocument(s, rig) : nullptr;
    check(QStringLiteral("C13a a session with two shots"), one && two);
    if (!one || !two) return;
    rig.listen(s);
    const QString dir = s->entry()->folder;
    QFile::remove(rig.tuningLog());

    CountingApp door([&rig] { return rig.athlete; });
    SessionDiagnosticsModel::setShotCommands(&door);
    {
        SessionDiagnosticsModel m;
        m.setSynchronous(true);
        m.setCadence(QStringLiteral("everyShot"));
        m.setBaselineAuthority(SessionDiagnosticsModel::BaselineAuthority::Never);
        m.activateSession(dir);
        check(QStringLiteral("C13b the model graded both shots"), m.shotCount() == 2);
        rig.events.clear();

        m.setShotIntent(1, QStringLiteral("draw"));
        check(QStringLiteral("C13c setShotIntent → the Shot's EVT-07, once"), rig.events == QStringList{ ev("EVT-07", one->ref()) });
        check(QStringLiteral("C13d …saved on the shot's review, as before"),
              reviewOf(rig, one->ref()).value(QStringLiteral("shotIntent")).toString() == QStringLiteral("draw"));

        rig.events.clear();
        m.correctOutcome(2, QStringLiteral("direction"), QStringLiteral("pull"));
        check(QStringLiteral("C13e correctOutcome → the Shot's EVT-06, once"), rig.events == QStringList{ ev("EVT-06", two->ref()) });
        const QJsonArray lc = reviewOf(rig, two->ref()).value(QStringLiteral("labelCorrections")).toArray();
        check(QStringLiteral("C13f …the correction on the shot's review"),
              lc.size() == 1 && lc.first().toObject().value(QStringLiteral("family")).toString() == QStringLiteral("direction"));
        const QList<QJsonObject> lines = tuningLines(rig.tuningLog());
        check(QStringLiteral("C13g …and ONE tuning line, appended by the Athlete, with the model's facts"),
              lines.size() == 1 && lines.first().value(QStringLiteral("shotId")).toInt() == 2
                  && lines.first().value(QStringLiteral("corrected")).toString() == QStringLiteral("pull")
                  && lines.first().value(QStringLiteral("session")).toString() == QFileInfo(dir).fileName()
                  && !lines.first().value(QStringLiteral("measures")).toObject().isEmpty());

        rig.events.clear();
        m.declareMiss(QStringLiteral("slice"));
        m.recordScreenResult(QStringLiteral("early_extension"), true);
        m.declareFocusFrom(QStringLiteral("over_the_top"), 1);
        m.setIntentFrom(2, QStringLiteral("fade"));
        check(QStringLiteral("C13h declareMiss, a screen answer, declareFocus → EVT-10 each; setIntentFrom → EVT-08"),
              rig.events == QStringList{ ev("EVT-10", s->id()), ev("EVT-10", s->id()), ev("EVT-10", s->id()),
                                         ev("EVT-08", s->id()) });
        const QJsonObject in = QJsonDocument::fromJson([&] { QFile f(SessionFiles::sessionInputFile(dir));
                                                             return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }()).object();
        check(QStringLiteral("C13i …saved in session_input.json, as before"),
              in.value(QStringLiteral("declaredMiss")).toString() == QStringLiteral("slice")
                  && in.value(QStringLiteral("screenAnswers")).toObject().value(QStringLiteral("early_extension")).toBool()
                  && in.value(QStringLiteral("declaredFocus")).toObject().value(QStringLiteral("conditionId")).toString() == QStringLiteral("over_the_top")
                  && in.value(QStringLiteral("intentRuns")).toArray().size() == 1);
        check(QStringLiteral("C13j the model reads its edits back as it holds them"),
              m.golferInputHeld().shotIntent.value(1) == QStringLiteral("draw") && m.golferInputHeld().corrections.contains(2));

        // C13k (review 1): the tuning log has ONE writer — a "flag for review" goes to the Athlete too.
        rig.events.clear();
        m.flagForReview(1);
        const QList<QJsonObject> after = tuningLines(rig.tuningLog());
        check(QStringLiteral("C13k flagForReview → through the door to the Athlete (once), one \"flag\" line, today's keys"),
              door.flags == 1 && after.size() == 2 && after.last().value(QStringLiteral("kind")).toString() == QStringLiteral("flag")
                  && after.last().value(QStringLiteral("shotId")).toInt() == 1
                  && after.last().value(QStringLiteral("session")).toString() == QFileInfo(dir).fileName()
                  && after.last().contains(QStringLiteral("phrase")) && after.last().value(QStringLiteral("labels")).isArray()
                  && !after.last().value(QStringLiteral("measures")).toObject().isEmpty()
                  && after.last().value(QStringLiteral("atMs")).toDouble() > 0);
        check(QStringLiteral("C13l …and no event (the reference names none for a flag)"), rig.events.isEmpty());
    }
    SessionDiagnosticsModel::setShotCommands(nullptr);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir cfg;
    if (!cfg.isValid()) return 1;
    // ppSettings() is IniFormat/UserScope: pointed at a scratch folder before the first QSettings (never the live INI).
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, cfg.path());
    pinpoint::installProcessRuleData();   // the model's packs and norms (refactor step 3.2)
    std::printf("shot_commands_test\n");
    if (qEnvironmentVariableIsEmpty("PINPOINT_CORE_PACK")) {
        std::printf("PINPOINT_CORE_PACK is unset — run this through ctest, which sets the shipped content.\n");
        return 1;
    }
    shotCommands();
    sessionIntent();
    doorAndCarousel();
    oldModel();
    std::printf("\n%s: %d of %d checks failed\n", g_fail ? "FAIL" : "PASS", g_fail, g_run);
    return g_fail ? 1 : 0;
}
