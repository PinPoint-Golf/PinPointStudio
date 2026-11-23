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

// THE SESSION, ITS SHOTS AND THE LIFECYCLE (refactor step 3.4a; build/refactor/step-3/BRIEF.md §4.3, §4.8, §6 row
// 3.4a): the Athlete's lifecycle commands, Session::addShot / trashShot / end, the Shot as its Session's child,
// RecordingSource, app.sessionAt, and SessionRepository's id-keyed diagnostics files (J-42). Over the REAL
// session store, swing door and reading door, on a temporary library (QTemporaryDir) — never the share.
//
// ⚠ EVERY EXPECTATION COMES FROM A STATED CONTRACT, NEVER FROM RUNNING THE CODE: information_model.md LC-SESSION,
// LC-SHOT, CLS-SESSION, CLS-SHOT, EVT-01/03/09/17/20/23 and DEC-69, DEC-71, DEC-73, DEC-77, DEC-78, DEC-91,
// DEC-93, DEC-134, DEC-142, DEC-143, DEC-164, DEC-168; J-24, J-42, J-46; the v1.9 reviewer's RecordingSource
// condition.
//
//   S1  start ends another InProgress session first (DEC-78): EVT-09 for it before EVT-17 for the new one
//   S2  resume: today's session of the same type only, with its own id (DEC-77, DEC-134); EVT-23; the session
//       running here ended first; another type, or a session not started today, refused
//   S3  an empty session ended → trashed by its Athlete, EVT-20 and no EVT-09 (DEC-143); a session with a swing,
//       or a resumed one, ends with EVT-09 and stays
//   S4  addShot's order (DEC-142): allocate (the folder made, the high-water raised) → the Shot made, in the
//       collection → returned; NO EVT-01 until capture's document write lands (D19: DEC-168), then once
//   S5  trashShot: EVT-03, the swing in the trash, its number never issued again (DEC-91); the Shot taken out
//       and deleted LATER (deleteLater), alive until the event loop runs
//   S6  the events in command order (DEC-69)
//   S7  a Shot is reachable only through its Session (P10): no public constructor; one instance per ref; one
//       Session per id
//   S8  a RecordingSource of an unpublished session reads nothing (the reader resolves only through
//       SessionRepository::swingFolder)
//   S9  app.sessionAt(folder) through the current Athlete (D1: DEC-164); another athlete's folder → none
//   S10 the session's diagnostics files by its id (J-42): written and read back where SessionFiles names them;
//       an id not held refused
//   S11 trashSession refuses a session in progress (J-46)
//   S12 (3.4a-ii) a session started ON DEMAND (capture's, for a swing outside a session) is never the empty
//       discard's: ended empty, it stays, with EVT-09 (DEC-93, today's rule); the repository's own End never
//       discards (the discard is Session::end's alone)
//   S13 (3.4a-ii) resuming the session already running here: the same object, Unchanged, no write, no event —
//       and it is resumed work from then on, so ended empty it stays (today's extend ended and resumed it); as
//       another type it is refused (DEC-134), review 1
//   S3i–k (4-a review) a trashed folder gives no Session, holds none and makes nothing on disk
//   S14 (3.4a-ii) addShot hands the capture service the whole allocation (its ShotRef, folder and names) for a
//       device-only shot as for a captured one; its kind is the caller's; EVT-01 only on the document's report
//
// Every trash stays in the temporary folder: the store is set with setOsTrashForTest(false), so the trashes land in
// the library's own .pinpoint-trash (asserted in S3, S5, S8), never in the OS trash (review 1, round 2).
//
//   cmake --build build/tests --target session_test -j 6
//   ctest --test-dir build/tests -R '^session_test$' --output-on-failure

#include "../athlete.h"
#include "../session.h"
#include "../shot.h"
#include "../../App/app.h"
#include "../../Export/recording_loader.h"
#include "../../Export/session_files.h"
#include "../../Export/session_report_io.h"   // the report's files, looked at directly (S11)
#include "../../Export/session_store.h"
#include "../../Export/swing_paths.h"
#include "../../Export/swing_store_repository.h"
#include "../../Buffer/swing_window.h"   // a LoadedRecording owns one
#include "../../LaunchMonitor/launch_monitor_reading.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimeZone>

#include <cstdio>
#include <memory>
#include <type_traits>

using namespace pinpoint;

static int g_fail = 0;
static int g_run  = 0;

static void check(const QString &label, bool ok)
{
    ++g_run;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", qPrintable(label));
    if (!ok) ++g_fail;
}

static const QString   kName    = QStringLiteral("Test Golfer");
static const QString   kToken   = QStringLiteral("Test-Golfer");
static const AthleteId kAthlete = AthleteId::fromString(u"5a1d9c3e-2b7f-4e8a-9c10-7d2e6f3b4a21");
static const AthleteId kOther   = AthleteId::fromString(u"0b8e7d6c-5a4f-4e3d-8c2b-1a0f9e8d7c6b");
static const QString   kSwing   = QStringLiteral("swing");
static const QString   kWrist   = QStringLiteral("wrist");
static const QString   kPat     = QStringLiteral("date-name-type");

// One scratch library: the real store and doors over it, and the athlete as its repository would hold it.
struct Rig {
    QTemporaryDir                                  dir;
    SessionStore                                   store;
    std::unique_ptr<SwingStoreRecordingRepository> recordings;
    std::shared_ptr<RecordingReader>               reader;
    std::unique_ptr<Athlete>                       athlete;
    QStringList                                    events;   // every event, in the order it was emitted

    explicit Rig(const AthleteId &id = kAthlete, const QString &name = kName, const QString &token = kToken)
    {
        store.setLockWaitForTest(200);
        store.setOsTrashForTest(false);   // every trash into the library's own, inside `dir` (never the OS's)
        recordings = std::make_unique<SwingStoreRecordingRepository>(store);
        reader     = std::make_shared<SwingStoreRecordingReader>(store);
        athlete    = makeAthlete(id, name, token);
    }

    bool valid() const { return dir.isValid(); }

    std::unique_ptr<Athlete> makeAthlete(const AthleteId &id, const QString &name, const QString &token)
    {
        auto a = std::make_unique<Athlete>(nullptr, &store, id);
        AthleteRecord r;
        r.name = name;
        a->setStored(r, dir.path() + QLatin1Char('/') + token, SessionOwner{ id, name, dir.path() },
                     Athlete::Source::Library, false);
        a->setShotDoors({ recordings.get(), reader });
        QObject::connect(a.get(), &Athlete::sessionStarted, a.get(),
                         [this](const SessionId &s) { events << QStringLiteral("EVT-17 ") + s.toString(); },
                         Qt::DirectConnection);
        QObject::connect(a.get(), &Athlete::sessionResumed, a.get(),
                         [this](const SessionId &s) { events << QStringLiteral("EVT-23 ") + s.toString(); },
                         Qt::DirectConnection);
        QObject::connect(a.get(), &Athlete::sessionTrashed, a.get(),
                         [this](const SessionId &s) { events << QStringLiteral("EVT-20 ") + s.toString(); },
                         Qt::DirectConnection);
        return a;
    }

    // The session's own events, heard as a listener would (DirectConnection: DEC-69).
    void listen(Session *s)
    {
        QObject::connect(s, &Session::shotAdded, s,
                         [this](const ShotRef &r) { events << QStringLiteral("EVT-01 ") + r.toString(); },
                         Qt::DirectConnection);
        QObject::connect(s, &Session::shotTrashed, s,
                         [this](const ShotRef &r) { events << QStringLiteral("EVT-03 ") + r.toString(); },
                         Qt::DirectConnection);
        QObject::connect(s, &Session::sessionEnded, s,
                         [this](const SessionId &i) { events << QStringLiteral("EVT-09 ") + i.toString(); },
                         Qt::DirectConnection);
    }

    // The library's own trash for an athlete folder's token (FILE-LIB-01), where this suite's trashes go.
    QString trashDir(const QString &token = kToken) const
    {
        return dir.path() + QStringLiteral("/.pinpoint-trash/") + token;
    }
    // Exactly one entry in it, and its name ends with `_<suffix>` (SwingPaths: <stamp>_[<session>_]<name>).
    bool trashedAs(const QString &suffix) const
    {
        const QStringList in = QDir(trashDir()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        return in.size() == 1 && in.first().endsWith(QLatin1Char('_') + suffix);
    }

    QString folderOf(const SessionId &id) const
    {
        const auto e = store.open(id);
        return e ? e->folder : QString();
    }
};

static void flushDeletes() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }

static QString ev(const char *evt, const SessionId &id) { return QString::fromLatin1(evt) + QLatin1Char(' ') + id.toString(); }
static QString ev(const char *evt, const ShotRef &ref) { return QString::fromLatin1(evt) + QLatin1Char(' ') + ref.toString(); }

static lm::LaunchMonitorReading reading()
{
    lm::LaunchMonitorReading r;
    r.deviceShotId  = QStringLiteral("7");
    r.deviceKind    = QStringLiteral("fsx2020");
    r.clubheadSpeed = 80.0;
    r.ballSpeed     = 130.0;
    r.launchAngle   = 14.0;
    return r;
}

// ── S1 ───────────────────────────────────────────────────────────────────────────────────────────
static void testStartEndsAnother()
{
    std::printf("\nS1 — a start ends the session running here first (DEC-78)\n");
    Rig rig;
    if (!rig.valid()) { check(QStringLiteral("S1 temp dir"), false); return; }
    WriteResult r;
    Session *a = rig.athlete->startSession(kSwing, kPat, &r);
    check(QStringLiteral("S1a a session starts (EVT-17), in progress"), a && r.ok() && a->inProgress());
    if (!a) return;
    rig.listen(a);
    check(QStringLiteral("S1b it holds a swing"), a->addShot() != nullptr);
    const SessionId aId = a->id();
    Session *b = rig.athlete->startSession(kWrist, kPat, &r);
    check(QStringLiteral("S1c a second start makes another session"), b && b != a && b->id() != aId);
    if (!b) return;
    check(QStringLiteral("S1d the first is ended, the second in progress"),
          a->status() == SessionStatus::Ended && b->inProgress());
    int inProgress = 0;
    for (const SessionIndexEntry &e : rig.athlete->index())
        if (e.status == SessionStatus::InProgress) ++inProgress;
    check(QStringLiteral("S1e one session of the athlete in progress (the index)"), inProgress == 1);
    const QStringList want{ ev("EVT-17", aId), ev("EVT-09", aId), ev("EVT-17", b->id()) };
    check(QStringLiteral("S1f events: EVT-17 A, EVT-09 A, then EVT-17 B (got %1)").arg(rig.events.join(QStringLiteral(", "))),
          rig.events == want);
    check(QStringLiteral("S1g the ended session, which held a swing, is still in the library"),
          QFileInfo::exists(rig.folderOf(aId)));
}

// ── S2 ───────────────────────────────────────────────────────────────────────────────────────────
static void testResume()
{
    std::printf("\nS2 — resume: today's session of the same type only (DEC-77, DEC-134)\n");
    Rig rig;
    if (!rig.valid()) { check(QStringLiteral("S2 temp dir"), false); return; }
    Session *a = rig.athlete->startSession(kSwing, kPat);
    if (!a) { check(QStringLiteral("S2 start"), false); return; }
    a->addShot();
    const SessionId aId = a->id();
    check(QStringLiteral("S2a ended"), a->end().ok() && a->status() == SessionStatus::Ended);

    check(QStringLiteral("S2b today's session of its type is found"), rig.athlete->todaysSession(kSwing) == aId);
    check(QStringLiteral("S2c no session of another type today"), rig.athlete->todaysSession(kWrist).isNull());

    rig.events.clear();
    WriteResult r;
    check(QStringLiteral("S2d resuming it as another type is refused"),
          rig.athlete->resumeSession(aId, kWrist, &r) == nullptr && !r.ok() && a->status() == SessionStatus::Ended);
    check(QStringLiteral("S2e a refused resume emits nothing"), rig.events.isEmpty());

    Session *c = rig.athlete->startSession(kWrist, kPat);   // running here when the resume comes
    if (!c) { check(QStringLiteral("S2 second start"), false); return; }
    rig.listen(c);
    c->addShot();
    rig.events.clear();
    Session *again = rig.athlete->resumeSession(aId, kSwing, &r);
    check(QStringLiteral("S2f resumed: the same object, the same id, in progress"),
          again == a && r.ok() && a->id() == aId && a->inProgress());
    check(QStringLiteral("S2g the session running here was ended first"), c->status() == SessionStatus::Ended);
    check(QStringLiteral("S2h events: EVT-09 for it, then EVT-23 (got %1)").arg(rig.events.join(QStringLiteral(", "))),
          rig.events == QStringList{ ev("EVT-09", c->id()), ev("EVT-23", aId) });

    // A session not started today: started "yesterday" by the store's clock, then the clock back.
    const QDateTime yesterday = QDateTime::currentDateTimeUtc().addDays(-1);
    rig.store.setClockForTest([yesterday] { return yesterday; });
    Session *old = rig.athlete->startSession(kSwing, kPat);
    rig.store.setClockForTest({});
    if (!old) { check(QStringLiteral("S2 yesterday's start"), false); return; }
    old->addShot();
    old->end();
    rig.events.clear();
    check(QStringLiteral("S2i a session started yesterday is not today's"), rig.athlete->todaysSession(kSwing) != old->id());
    check(QStringLiteral("S2j resuming it is refused"),
          rig.athlete->resumeSession(old->id(), kSwing, &r) == nullptr && !r.ok()
              && old->status() == SessionStatus::Ended);
}

// ── S3 ───────────────────────────────────────────────────────────────────────────────────────────
static void testEmptyEnd()
{
    std::printf("\nS3 — an empty session ended goes to the trash (DEC-143)\n");
    Rig rig;
    if (!rig.valid()) { check(QStringLiteral("S3 temp dir"), false); return; }
    Session *s = rig.athlete->startSession(kSwing, kPat);
    if (!s) { check(QStringLiteral("S3 start"), false); return; }
    rig.listen(s);
    const SessionId id = s->id();
    const QString folder = rig.folderOf(id);
    QPointer<Session> watch(s);
    rig.events.clear();
    const WriteResult r = s->end();
    check(QStringLiteral("S3a End answers ok"), r.ok());
    check(QStringLiteral("S3b the empty session left the library"), !folder.isEmpty() && !QFileInfo::exists(folder));
    check(QStringLiteral("S3b2 into the library's own trash"), rig.trashedAs(QFileInfo(folder).fileName()));
    check(QStringLiteral("S3c EVT-20, and no EVT-09 (got %1)").arg(rig.events.join(QStringLiteral(", "))),
          rig.events == QStringList{ ev("EVT-20", id) });
    check(QStringLiteral("S3d the athlete no longer hands it out"), rig.athlete->session(id) == nullptr);
    check(QStringLiteral("S3e its object lives until the event loop runs"), !watch.isNull());
    flushDeletes();
    check(QStringLiteral("S3f then it is deleted"), watch.isNull());
    // The trashed folder is no session any more (refactor 4-a review): asked for again — as a view's binding asked for
    // it during the discard — it gives none, holds none, and nothing is made where it was.
    const int heldBefore = int(rig.athlete->loadedSessions().size());
    check(QStringLiteral("S3i the trashed folder gives no Session"), rig.athlete->sessionAt(folder) == nullptr);
    check(QStringLiteral("S3j …none is held for it"), int(rig.athlete->loadedSessions().size()) == heldBefore);
    check(QStringLiteral("S3k …and nothing is made on disk where it was"), !QFileInfo::exists(folder));

    // With a swing: EVT-09, kept.
    Session *k = rig.athlete->startSession(kSwing, kPat);
    if (!k) { check(QStringLiteral("S3 second start"), false); return; }
    rig.listen(k);
    k->addShot();
    rig.events.clear();
    check(QStringLiteral("S3g a session with a swing ends with EVT-09 and stays"),
          k->end().ok() && rig.events == QStringList{ ev("EVT-09", k->id()) } && QFileInfo::exists(rig.folderOf(k->id())));

    // A resumed session that captured nothing: somebody's earlier work, never discarded (DEC-93).
    Session *e = rig.athlete->startSession(kWrist, kPat);
    if (!e) { check(QStringLiteral("S3 third start"), false); return; }
    rig.listen(e);
    e->addShot();
    const QString eFolder = rig.folderOf(e->id());
    e->end();
    // Empty it on disk as another program could, then resume and end it.
    QDir(eFolder + QStringLiteral("/swing_0001")).removeRecursively();
    Session *er = rig.athlete->resumeSession(e->id(), kWrist);
    rig.events.clear();
    check(QStringLiteral("S3h a resumed empty session ends with EVT-09 and is not trashed"),
          er == e && e->end().ok() && rig.events == QStringList{ ev("EVT-09", e->id()) } && QFileInfo::exists(eFolder));
}

// ── S4 ───────────────────────────────────────────────────────────────────────────────────────────
static void testAddShot()
{
    std::printf("\nS4 — addShot: allocate → create → return; EVT-01 when the document lands (DEC-142, DEC-168)\n");
    Rig rig;
    if (!rig.valid()) { check(QStringLiteral("S4 temp dir"), false); return; }
    Session *s = rig.athlete->startSession(kSwing, kPat);
    if (!s) { check(QStringLiteral("S4 start"), false); return; }
    rig.listen(s);
    rig.events.clear();
    WriteResult r;
    Shot *shot = s->addShot(&r);
    check(QStringLiteral("S4a a Shot comes back"), shot && r.ok());
    if (!shot) return;
    const QString folder = rig.folderOf(s->id());
    check(QStringLiteral("S4b allocated first: swing 1, its folder made"),
          shot->ref() == ShotRef(s->id(), 1) && QFileInfo(rig.store.swingFolder(shot->ref())).isDir());
    check(QStringLiteral("S4c the high-water recorded (FILE-SESS-10)"), SessionFiles::recordedSwingHighWater(folder) == 1);
    check(QStringLiteral("S4d the Shot is in the collection, its Session's child"),
          s->shots() == QList<Shot *>{ shot } && s->shot(shot->ref()) == shot && shot->parent() == s);
    // No diagnostics owner in this suite: the view names none, so it has no value ("-"). Its reads are the owner's
    // (session_diagnostics.cpp, not linked here); outcome_summary_test reads them over a real owner (refactor 4-b).
    check(QStringLiteral("S4e a captured shot; its diagnostics empty (\"-\": no owner to read)"),
          shot->kind() == recording::ShotKind::Captured && !shot->diagnostics().attached());
    check(QStringLiteral("S4f no EVT-01 at the allocation (D19)"), rig.events.isEmpty());

    // Capture writes the document (here a launch monitor shot, through the one swing door), then says so.
    DeviceOnlyMeta meta;
    meta.swingId     = QStringLiteral("swing_0001");
    meta.swingIndex  = 1;
    meta.athleteName = kName;
    meta.club        = QStringLiteral("7 IRON");
    meta.wallclockMs = QDateTime::currentMSecsSinceEpoch();
    check(QStringLiteral("S4g the document written into its folder"), rig.recordings->create(shot->ref(), reading(), meta).ok());
    check(QStringLiteral("S4h the landing reported: EVT-01 once"),
          s->shotDocumentWritten(shot->ref()) && rig.events == QStringList{ ev("EVT-01", shot->ref()) });
    check(QStringLiteral("S4i a second report emits nothing"), !s->shotDocumentWritten(shot->ref()) && rig.events.size() == 1);
    check(QStringLiteral("S4j a shot not in the collection emits nothing"),
          !s->shotDocumentWritten(ShotRef(s->id(), 9)) && rig.events.size() == 1);

    Shot *device = s->addShot(recording::ShotKind::DeviceOnly);
    check(QStringLiteral("S4k a device-only shot: swing 2, its kind as allocated"),
          device && device->swingNumber() == 2 && device->kind() == recording::ShotKind::DeviceOnly);
}

// ── S5 ───────────────────────────────────────────────────────────────────────────────────────────
static void testTrashShot()
{
    std::printf("\nS5 — trashShot never reissues a number; the Shot is deleted later (J-24, DEC-91)\n");
    Rig rig;
    if (!rig.valid()) { check(QStringLiteral("S5 temp dir"), false); return; }
    Session *s = rig.athlete->startSession(kSwing, kPat);
    if (!s) { check(QStringLiteral("S5 start"), false); return; }
    rig.listen(s);
    Shot *one = s->addShot();
    Shot *two = s->addShot();
    if (!one || !two) { check(QStringLiteral("S5 two shots"), false); return; }
    const ShotRef twoRef = two->ref();
    const QString twoDir = rig.store.swingFolder(twoRef);
    QPointer<Shot> watch(two);
    rig.events.clear();
    QString where;
    const WriteResult r = s->trashShot(twoRef, &where);
    check(QStringLiteral("S5a the newest shot trashed (%1)").arg(where), r.ok() && !where.isEmpty());
    check(QStringLiteral("S5a2 into the library's own trash, named for its session"),
          where.startsWith(rig.trashDir() + QLatin1Char('/'))
              && rig.trashedAs(QFileInfo(rig.folderOf(s->id())).fileName() + QStringLiteral("_swing_0002")));
    check(QStringLiteral("S5b its folder left the session"), !QFileInfo::exists(twoDir));
    check(QStringLiteral("S5c EVT-03"), rig.events == QStringList{ ev("EVT-03", twoRef) });
    check(QStringLiteral("S5d out of the collection at once"), s->shot(twoRef) == nullptr && s->shots() == QList<Shot *>{ one });
    check(QStringLiteral("S5e its object lives until the event loop runs"), !watch.isNull());
    flushDeletes();
    check(QStringLiteral("S5f then it is deleted"), watch.isNull());
    Shot *three = s->addShot();
    check(QStringLiteral("S5g the next shot is swing 3, never 2 again"), three && three->swingNumber() == 3);
    check(QStringLiteral("S5h a shot not in the session is refused"),
          s->trashShot(twoRef).status == WriteResult::Status::Failed && rig.events.size() == 1);
}

// ── S6 ───────────────────────────────────────────────────────────────────────────────────────────
static void testCommandOrder()
{
    std::printf("\nS6 — the events in command order (DEC-69)\n");
    Rig rig;
    if (!rig.valid()) { check(QStringLiteral("S6 temp dir"), false); return; }
    Session *s = rig.athlete->startSession(kSwing, kPat);
    if (!s) { check(QStringLiteral("S6 start"), false); return; }
    rig.listen(s);
    Shot *a = s->addShot();
    Shot *b = s->addShot();
    if (!a || !b) { check(QStringLiteral("S6 shots"), false); return; }
    const ShotRef ar = a->ref(), br = b->ref();
    s->shotDocumentWritten(br);
    s->shotDocumentWritten(ar);
    s->trashShot(br);
    s->end();
    const QStringList want{ ev("EVT-17", s->id()), ev("EVT-01", br), ev("EVT-01", ar), ev("EVT-03", br),
                            ev("EVT-09", s->id()) };
    check(QStringLiteral("S6a EVT-17, EVT-01 ×2 as reported, EVT-03, EVT-09 (got %1)").arg(rig.events.join(QStringLiteral(", "))),
          rig.events == want);
}

// ── S7 ───────────────────────────────────────────────────────────────────────────────────────────
static void testReachOnlyThroughSession()
{
    std::printf("\nS7 — a Shot only through its Session (P10); one instance per id\n");
    static_assert(!std::is_default_constructible_v<Shot>, "a Shot is made by its Session only");
    static_assert(!std::is_constructible_v<Shot, const ShotRef &, std::optional<recording::ShotKind>,
                                           const ShotDoors &, Session *>,
                  "a Shot's constructor is its Session's");
    static_assert(!std::is_constructible_v<Session, Athlete *, SessionRepository *, const ShotDoors &,
                                           const SessionId &, bool>,
                  "a Session is made by its Athlete only");
    check(QStringLiteral("S7a no public constructor of a Shot or a Session (compile-time)"), true);
    Rig rig;
    if (!rig.valid()) { check(QStringLiteral("S7 temp dir"), false); return; }
    Session *s = rig.athlete->startSession(kSwing, kPat);
    if (!s) { check(QStringLiteral("S7 start"), false); return; }
    Shot *shot = s->addShot();
    s->end();
    check(QStringLiteral("S7b the same Session for its id, and the same Shot for its ref"),
          rig.athlete->session(s->id()) == s && s->shot(shot->ref()) == shot);
    check(QStringLiteral("S7c a ref of another session is not this session's"),
          s->shot(ShotRef(SessionId::create(), shot->swingNumber())) == nullptr);
    // A second Athlete object over the same repository (as a tool's would be) loads the session afresh from
    // disk, and lists its shot from the folder.
    std::unique_ptr<Athlete> again = rig.makeAthlete(kAthlete, kName, kToken);
    Session *loaded = again->session(s->id());
    check(QStringLiteral("S7d loaded from disk: its shots listed from its folder, kind read from the document"),
          loaded && loaded != s && loaded->shots().size() == 1 && loaded->shots().first()->ref() == shot->ref()
              && loaded->shots().first()->kind() == recording::ShotKind::Captured);
    check(QStringLiteral("S7e every session of the athlete through sessions()"),
          again->sessions().size() == 1 && again->sessions().first() == loaded);
}

// ── S8 ───────────────────────────────────────────────────────────────────────────────────────────
static void testRecordingSource()
{
    std::printf("\nS8 — a RecordingSource of an unpublished session reads nothing\n");
    Rig rig;
    if (!rig.valid()) { check(QStringLiteral("S8 temp dir"), false); return; }
    Session *s = rig.athlete->startSession(kSwing, kPat);
    if (!s) { check(QStringLiteral("S8 start"), false); return; }
    Shot *shot = s->addShot(recording::ShotKind::DeviceOnly);
    if (!shot) { check(QStringLiteral("S8 shot"), false); return; }
    DeviceOnlyMeta meta;
    meta.swingId     = QStringLiteral("swing_0001");
    meta.swingIndex  = 1;
    meta.athleteName = kName;
    meta.wallclockMs = QDateTime::currentMSecsSinceEpoch();
    meta.standalone  = true;
    rig.recordings->create(shot->ref(), reading(), meta);
    const RecordingSource source = shot->recordingSource();
    check(QStringLiteral("S8a the source names the shot and has a reader"), source.shot() == shot->ref() && !source.isNull());
    const LoadedRecording before = source.read();
    check(QStringLiteral("S8b while the session is published, the read reaches the shot's folder (%1)")
              .arg(before.ok ? QStringLiteral("ok") : before.error),
          !rig.store.swingFolder(shot->ref()).isEmpty() && !before.error.contains(QStringLiteral("not open")));
    const SessionId id = s->id();
    s->end();
    const QString sessionName = QFileInfo(rig.folderOf(id)).fileName();
    QString where;
    check(QStringLiteral("S8c the session trashed (unpublished), into the library's own trash"),
          rig.athlete->trashSession(id, &where).ok() && where.startsWith(rig.trashDir() + QLatin1Char('/'))
              && rig.trashedAs(sessionName));
    const LoadedRecording after = source.read();
    check(QStringLiteral("S8d the same source now reads nothing: %1").arg(after.error),
          rig.store.swingFolder(source.shot()).isEmpty() && !after.ok && !after.window
              && after.error.contains(QStringLiteral("its session is not open")));
    check(QStringLiteral("S8e a source with no reader reads nothing"), !RecordingSource(source.shot(), nullptr).read().ok);
}

// ── S9 ───────────────────────────────────────────────────────────────────────────────────────────
static void testAppSessionAt()
{
    std::printf("\nS9 — app.sessionAt(folder) through the current Athlete (D1: DEC-164)\n");
    Rig rig;
    if (!rig.valid()) { check(QStringLiteral("S9 temp dir"), false); return; }
    Session *s = rig.athlete->startSession(kSwing, kPat);
    if (!s) { check(QStringLiteral("S9 start"), false); return; }
    s->addShot();
    s->end();
    Athlete *current = rig.athlete.get();
    App app([&current] { return current; });
    const QString folder = rig.folderOf(s->id());
    check(QStringLiteral("S9a the folder's Session: the athlete's own instance"), app.sessionAt(folder) == s);
    check(QStringLiteral("S9b the folder spelled with a trailing slash names the same session"),
          app.sessionAt(folder + QLatin1Char('/')) == s);
    check(QStringLiteral("S9c a folder that is no session: none"), app.sessionAt(rig.dir.path() + QStringLiteral("/nowhere")) == nullptr);
    std::unique_ptr<Athlete> other = rig.makeAthlete(kOther, QStringLiteral("Other Golfer"), QStringLiteral("Other-Golfer"));
    current = other.get();
    check(QStringLiteral("S9d another athlete's session: none"), app.sessionAt(folder) == nullptr);
    current = nullptr;
    check(QStringLiteral("S9e no current athlete: none"), app.sessionAt(folder) == nullptr);
}

// ── S10 ──────────────────────────────────────────────────────────────────────────────────────────
static QJsonObject readJson(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}

static void testDiagnosticsById()
{
    std::printf("\nS10 — the session's diagnostics files by its id (J-42)\n");
    Rig rig;
    if (!rig.valid()) { check(QStringLiteral("S10 temp dir"), false); return; }
    Session *s = rig.athlete->startSession(kSwing, kPat);
    if (!s) { check(QStringLiteral("S10 start"), false); return; }
    const SessionId id = s->id();
    const QString folder = rig.folderOf(id);

    check(QStringLiteral("S10a nothing written yet: Missing"), rig.store.readDiagnostics(id).state == DiagnosticsRead::State::Missing);
    const QJsonObject block{ { QStringLiteral("stage"), QStringLiteral("final") } };
    const QJsonObject ledger{ { QStringLiteral("schemaVersion"), 1 } };
    check(QStringLiteral("S10b diagnostics.json written by id"), rig.store.writeDiagnostics(id, block, ledger, {}).ok());
    const QJsonObject onDisk = readJson(SessionFiles::diagnosticsFile(folder));
    check(QStringLiteral("S10c at SessionFiles' name, in the envelope's shape"),
          onDisk.value(QStringLiteral("session")).toObject() == block
              && onDisk.value(QStringLiteral("ledger")).toObject() == ledger
              && onDisk.value(QStringLiteral("schemaVersion")).toInt() == 1);
    const DiagnosticsRead back = rig.store.readDiagnostics(id);
    check(QStringLiteral("S10d read back by id"),
          back.state == DiagnosticsRead::State::Read && back.envelope.session == block && back.envelope.ledger == ledger);

    SessionInput in;
    in.declaredMiss = QStringLiteral("slice");
    check(QStringLiteral("S10e session_input.json written and read back by id"),
          rig.store.writeSessionInput(id, in).ok() && rig.store.readSessionInput(id).input.declaredMiss == in.declaredMiss
              && QFileInfo::exists(SessionFiles::sessionInputFile(folder)));
    const QJsonObject small{ { QStringLiteral("k"), 2 } };
    check(QStringLiteral("S10f club_baseline.json and work_ons.json by id"),
          rig.store.writeClubBaseline(id, small).ok() && rig.store.readClubBaseline(id).root == small
              && rig.store.writeWorkOns(id, small).ok() && rig.store.readWorkOns(id).root == small
              && QFileInfo::exists(SessionFiles::clubBaselineFile(folder)) && QFileInfo::exists(SessionFiles::workOnsFile(folder)));
    QString backup;
    check(QStringLiteral("S10g a backup by id"),
          rig.store.backupDiagnostics(id, QStringLiteral("t1"), &backup).ok() && QFileInfo::exists(backup));

    const SessionId stranger = SessionId::create();
    check(QStringLiteral("S10h an id not held: reads empty, writes refused"),
          rig.store.readDiagnostics(stranger).state == DiagnosticsRead::State::Missing
              && !rig.store.readDiagnostics(stranger).error.isEmpty()
              && rig.store.writeDiagnostics(stranger, block, ledger, {}).status == WriteResult::Status::Failed
              && rig.store.writeSessionInput(stranger, in).status == WriteResult::Status::Failed
              && !rig.store.readWorkOns(stranger).exists);
}

// ── S11 ──────────────────────────────────────────────────────────────────────────────────────────
static void testTrashRefusedInProgress()
{
    std::printf("\nS11 — trashSession refuses a session in progress (J-46)\n");
    Rig rig;
    if (!rig.valid()) { check(QStringLiteral("S11 temp dir"), false); return; }
    Session *s = rig.athlete->startSession(kSwing, kPat);
    if (!s) { check(QStringLiteral("S11 start"), false); return; }
    rig.events.clear();
    const WriteResult r = rig.athlete->trashSession(s->id());
    check(QStringLiteral("S11a refused, nothing moved, no event"),
          !r.ok() && QFileInfo::exists(rig.folderOf(s->id())) && rig.events.isEmpty() && rig.athlete->session(s->id()) == s);
}

// ── S12 ──────────────────────────────────────────────────────────────────────────────────────────
static void testOnDemandNeverDiscarded()
{
    std::printf("\nS12 — a session started on demand is never the empty discard's (DEC-93)\n");
    Rig rig;
    if (!rig.valid()) { check(QStringLiteral("S12 temp dir"), false); return; }
    Session *s = rig.athlete->startSession(kSwing, kPat, nullptr, SessionStart::OnDemand);
    if (!s) { check(QStringLiteral("S12 start"), false); return; }
    rig.listen(s);
    const QString folder = rig.folderOf(s->id());
    check(QStringLiteral("S12a it says it was started on demand, and is running here"), s->onDemand() && s->runningHere());
    rig.events.clear();
    const bool ended = s->end().ok();
    check(QStringLiteral("S12b ended empty: EVT-09, and it stays (got %1)").arg(rig.events.join(QStringLiteral(", "))),
          ended && rig.events == QStringList{ ev("EVT-09", s->id()) } && QFileInfo::exists(folder));
    check(QStringLiteral("S12c ended, nothing in the trash"),
          s->status() == SessionStatus::Ended && !QFileInfo::exists(rig.trashDir()));

    // The repository's End never discards, whatever the session: only Session::end makes the discard.
    const SessionStartResult direct = rig.store.start(rig.athlete->sessionOwner(), kSwing, kPat);
    check(QStringLiteral("S12d the repository's End of an empty session it started keeps it"),
          direct.ok() && rig.store.end(direct.id).ok() && QFileInfo::exists(direct.folder)
              && !QFileInfo::exists(rig.trashDir()));
}

// ── S13 ──────────────────────────────────────────────────────────────────────────────────────────
static void testResumeTheRunningOne()
{
    std::printf("\nS13 — resuming the session already running here changes nothing\n");
    Rig rig;
    if (!rig.valid()) { check(QStringLiteral("S13 temp dir"), false); return; }
    Session *s = rig.athlete->startSession(kSwing, kPat);
    if (!s) { check(QStringLiteral("S13 start"), false); return; }
    rig.listen(s);
    const QString folder = rig.folderOf(s->id());
    const QByteArray before = [&] { QFile f(folder + QStringLiteral("/session.json")); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }();
    rig.events.clear();
    WriteResult r;
    // DEC-134 still holds for the session running here: another type is refused, and nothing changes.
    Session *wrong = rig.athlete->resumeSession(s->id(), kWrist, &r);
    check(QStringLiteral("S13w resuming the running session as another type is refused: no write, no event, still running"),
          wrong == nullptr && !r.ok() && r.status != WriteResult::Status::Unchanged && s->inProgress() && s->runningHere()
              && rig.events.isEmpty());
    r = {};
    Session *again = rig.athlete->resumeSession(s->id(), kSwing, &r);
    const QByteArray after = [&] { QFile f(folder + QStringLiteral("/session.json")); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }();
    check(QStringLiteral("S13a the same object, Unchanged, still in progress"),
          again == s && r.status == WriteResult::Status::Unchanged && s->inProgress());
    check(QStringLiteral("S13b no event, and the session file not rewritten"),
          rig.events.isEmpty() && !before.isEmpty() && before == after);
    check(QStringLiteral("S13c resumed work: ended empty, it stays with EVT-09"),
          s->end().ok() && rig.events == QStringList{ ev("EVT-09", s->id()) } && QFileInfo::exists(folder));
}

// ── S14 ──────────────────────────────────────────────────────────────────────────────────────────
static void testAddShotAllocation()
{
    std::printf("\nS14 — addShot hands the capture service the allocation (DEC-142)\n");
    Rig rig;
    if (!rig.valid()) { check(QStringLiteral("S14 temp dir"), false); return; }
    Session *s = rig.athlete->startSession(kSwing, kPat);
    if (!s) { check(QStringLiteral("S14 start"), false); return; }
    rig.listen(s);
    rig.events.clear();
    SwingAllocation a;
    Shot *shot = s->addShot(recording::ShotKind::DeviceOnly, &a);
    check(QStringLiteral("S14a the allocation is the Shot's: its ref, a folder made under the session's, its name"),
          shot && a.ok() && a.shot == shot->ref() && QFileInfo(a.swingFolder).isDir()
              && QFileInfo(a.swingFolder).path() == rig.folderOf(s->id()) && a.swingName == QStringLiteral("swing_0001")
              && a.sessionFolderName == QFileInfo(rig.folderOf(s->id())).fileName());
    check(QStringLiteral("S14b its kind is the caller's"), shot && shot->kind() == recording::ShotKind::DeviceOnly);
    check(QStringLiteral("S14c no EVT-01 at the allocation; one at the document's report"),
          rig.events.isEmpty() && shot && s->shotDocumentWritten(shot->ref())
              && rig.events == QStringList{ ev("EVT-01", shot->ref()) });
    SwingAllocation b;
    s->end();
    check(QStringLiteral("S14d an ended session that is not held allocates nothing, and says why"),
          rig.athlete->trashSession(s->id()).ok() && s->addShot(recording::ShotKind::Captured, &b) == nullptr
              && !b.ok() && !b.result.error.isEmpty());
}

// ── S11 ──────────────────────────────────────────────────────────────────────────────────────────
// THE REPORT'S RECORDS THROUGH THE SESSION (refactor step 5.6a; FILE-SESS-07, 13; DEC-201, J-66): an older
// report_meta.json is split when the session is first held (Athlete::hold → Session::migrateReport), read unopened
// through Athlete::readReportAt without a Session, and written by the Session's commands only.
static QByteArray rawOf(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }

static void testReportThroughSession()
{
    std::printf("\nS11 — the report's records through the Session (step 5.6a)\n");
    namespace io = session_report_io;
    Rig rig;
    if (!rig.valid()) { check(QStringLiteral("S11 temp dir"), false); return; }
    // Six ended sessions, each with a shot (an empty one would be discarded).
    QStringList folders;
    for (int i = 0; i < 6; ++i) {
        Session *s = rig.athlete->startSession(kSwing, kPat);
        if (!s) { check(QStringLiteral("S11 start"), false); return; }
        s->addShot();
        s->end();
        folders << rig.folderOf(s->id());
    }
    const QByteArray legacy = R"({"schema":1,"note":"Legacy note","options":{"appendix":true},"photosIncluded":false,
        "preparedMs":2000,"prepared":[1000,2000],"pdf":"r_session-report.pdf","sha256":"abc","sizeBytes":3,"fingerprint":"fp",
        "sends":[{"ms":3000,"method":"mail-app","sha256":"abc"}]})";
    const auto putLegacy = [&](const QString &folder) {
        QDir().mkpath(io::reportDir(folder));
        QFile m(io::metaFile(folder)); m.open(QIODevice::WriteOnly); m.write(legacy); m.close();
        QFile p(io::reportDir(folder) + QStringLiteral("/r_session-report.pdf")); p.open(QIODevice::WriteOnly); p.write("pdf"); p.close();
    };
    const QString legacyF = folders[0], crashF = folders[1], newerF = folders[2], freshF = folders[3], readOnlyF = folders[4],
                  noFileF = folders[5];
    putLegacy(legacyF);
    putLegacy(crashF);
    io::writeInput(crashF, io::read(crashF).input);   // a crash between the two writes: 13 written, 07 still legacy
    putLegacy(newerF);
    { QFile m(io::metaFile(newerF)); m.open(QIODevice::WriteOnly); m.write(R"({"schema":9,"note":"newer","pdf":"x.pdf"})"); }
    putLegacy(readOnlyF);
    {   // a newer build's session file: the Session never writes into it
        QJsonObject o = readJson(SessionFiles::sessionFile(readOnlyF));
        o.insert(QStringLiteral("schema"), QStringLiteral("pinpoint.session/9"));
        QFile f(SessionFiles::sessionFile(readOnlyF)); f.open(QIODevice::WriteOnly); f.write(QJsonDocument(o).toJson()); f.close();
    }
    putLegacy(noFileF);
    QFile::remove(SessionFiles::sessionFile(noFileF));

    // A fresh store and athlete over the same library: nothing held yet (the app's first launch after 5.6a).
    SessionStore store2;
    store2.setLockWaitForTest(200);
    Athlete a2(nullptr, &store2, kAthlete);
    AthleteRecord r;
    r.name = kName;
    a2.setStored(r, rig.dir.path() + QLatin1Char('/') + kToken, SessionOwner{ kAthlete, kName, rig.dir.path() },
                 Athlete::Source::Library, false);

    // Read unopened: the older shape's facts, no file written, no Session made.
    const QByteArray before = rawOf(io::metaFile(legacyF));
    const ReportRead unopened = a2.readReportAt(legacyF);
    check(QStringLiteral("S11a an unopened legacy record read through the Athlete: its note and send log"),
          unopened.legacy && unopened.input.note == QLatin1String("Legacy note") && unopened.input.sends.size() == 1
              && unopened.record.fingerprint == QLatin1String("fp") && unopened.pdfPresent);
    check(QStringLiteral("S11b …with nothing written and no Session held"),
          rawOf(io::metaFile(legacyF)) == before && !QFileInfo::exists(io::inputFile(legacyF)) && a2.loadedSessions().isEmpty());
    const ReportRead noFile = a2.readReportAt(noFileF);
    check(QStringLiteral("S11c a session with no session.json: its report still found (D3's id)"),
          noFile.legacy && noFile.input.note == QLatin1String("Legacy note"));

    // First held: split. Then the Session's commands.
    Session *s = a2.sessionAt(legacyF);
    const ReportRead held = s ? s->readReport() : ReportRead{};
    check(QStringLiteral("S11d first held: report_input.json written, report_meta.json derived only, nothing lost"),
          s && QFileInfo::exists(io::inputFile(legacyF)) && !held.legacy && held.input == unopened.input
              && !readJson(io::metaFile(legacyF)).contains(QStringLiteral("note"))
              && held.record.fingerprint == QLatin1String("fp"));
    if (s) {
        const QByteArray meta0 = rawOf(io::metaFile(legacyF));
        check(QStringLiteral("S11e recordReportSend: appended with the record's hash; report_meta.json untouched"),
              s->recordReportSend(QStringLiteral("smtp"), 4000).ok() && s->readReport().input.sends.size() == 2
                  && s->readReport().input.sends.last().sha256 == s->readReport().record.sha256
                  && s->readReport().input.sends.last().method == QLatin1String("smtp") && rawOf(io::metaFile(legacyF)) == meta0);
        const QByteArray in0 = rawOf(io::inputFile(legacyF));
        ReportRecord rec = s->readReport().record;
        rec.fingerprint = QStringLiteral("fp2");
        check(QStringLiteral("S11f saveReport: report_meta.json rebuilt, report_input.json byte for byte"),
              s->saveReport(rec).ok() && s->readReport().record.fingerprint == QLatin1String("fp2") && rawOf(io::inputFile(legacyF)) == in0);
        check(QStringLiteral("S11g recordReportMake: the note, options and one more make in report_input.json"),
              s->recordReportMake(QStringLiteral("New note"), { { QStringLiteral("appendix"), false } }, 5000).ok()
                  && s->readReport().input.note == QLatin1String("New note")
                  && s->readReport().input.preparedHistory == QVector<qint64>({ 1000, 2000, 5000 })
                  && s->readReport().input.sends.size() == 2);
    }

    // A crash between the writes, then held: finished. A newer report file, a fresh session, a newer session file: untouched.
    Session *c = a2.sessionAt(crashF);
    check(QStringLiteral("S11h after a crash between the writes, the first hold finishes the split"),
          c && !io::read(crashF).legacy && io::read(crashF).input.note == QLatin1String("Legacy note"));
    const QByteArray newer0 = rawOf(io::metaFile(newerF));
    check(QStringLiteral("S11i a newer report_meta.json: held, nothing written"),
          a2.sessionAt(newerF) && rawOf(io::metaFile(newerF)) == newer0 && !QFileInfo::exists(io::inputFile(newerF)));
    check(QStringLiteral("S11j a fresh session: held, no report folder made"),
          a2.sessionAt(freshF) && !QFileInfo::exists(io::reportDir(freshF)));
    Session *ro = a2.sessionAt(readOnlyF);
    check(QStringLiteral("S11k a newer build's session file: held, its report never split"),
          ro && rawOf(io::metaFile(readOnlyF)) == legacy && !QFileInfo::exists(io::inputFile(readOnlyF)));

    // A second open (a third store, nothing held): a no-op.
    const QByteArray m1 = rawOf(io::metaFile(legacyF)), i1 = rawOf(io::inputFile(legacyF));
    SessionStore store3;
    Athlete a3(nullptr, &store3, kAthlete);
    a3.setStored(r, rig.dir.path() + QLatin1Char('/') + kToken, SessionOwner{ kAthlete, kName, rig.dir.path() },
                 Athlete::Source::Library, false);
    check(QStringLiteral("S11l a second open: nothing written"),
          a3.sessionAt(legacyF) && rawOf(io::metaFile(legacyF)) == m1 && rawOf(io::inputFile(legacyF)) == i1);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);   // QTemporaryDir, deleteLater
    std::printf("session_test — the Session, its Shots and the lifecycle (step 3.4a)\n");
    testStartEndsAnother();
    testResume();
    testEmptyEnd();
    testAddShot();
    testTrashShot();
    testCommandOrder();
    testReachOnlyThroughSession();
    testRecordingSource();
    testAppSessionAt();
    testDiagnosticsById();
    testTrashRefusedInProgress();
    testOnDemandNeverDiscarded();
    testResumeTheRunningOne();
    testAddShotAllocation();
    testReportThroughSession();
    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
