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

// THE DECLARED FOCUS REACHES ITS SESSION (refactor 4-d; build/refactor/step-4/BRIEF-d.md deliverable 3, FOCUS-TRACE.md).
// app.state's start over the REAL session store, Athlete, Session, Shot and SessionDiagnostics (synchronous doors) and the
// panel's real adapter, on a temporary library — never the share.
//
// ⚠ EVERY EXPECTATION IS A STATED CONTRACT: the Session owns its declared focus (OBJ-SESSION-INTENT, R-INPUT-3,
// FILE-SESS-02); its one way in is Session::setSessionIntent (EVT-10); a wizard start declares it from the first shot;
// Extend keeps the session's saved intent; D7 (DEC-154) carries it to the new session, from its first shot. The oracle
// is the file on disk (parsed here) and what the owner diagnoses against (its reduction input).
//
//   L1  start with a focus, no diagnostics model or panel made, one shot, End: session_input.json holds {id, 0}, the
//       owner's diagnosis reads it, and the pick does not outlive the start; a start with no pick writes nothing
//   L2  End, then Extend, with the panel's adapter pointed at the session: the saved focus is kept (no EVT-10), with an
//       empty pick and with a stale one, and the panel shows it
//   NS  a Start that opens no session (an unusable library) keeps the pick; the session the first swing makes takes it
//       (review B1; J-64: the pick waits "until Start hands it to the Session"), and the owner diagnoses against it
//   FR  a failed D7 restart keeps the carried focus pending the same way, for the session the next swing makes
//   L2Q L2's QML side at the source: the only QML writers of a declared focus are the tap's and the wizard's Start
//   L3  the library restart's carry (ShotProcessor::onLibraryChanged's two calls, src/App/session_focus.h): the ended
//       session's focus — declared mid-session from a later shot — read from its Session, then declared on the new
//       session from shot 0; a folder that has gone reads as "cannot say", not as "no focus" (the caller then falls back
//       on the owner's last diagnosis input). ShotProcessor itself, and its no-golfer (N1) repository path, are not
//       linked here (the capture service drags the devices in)
//
//   cmake --build build/tests --target session_focus_test -j 6
//   ctest --test-dir build/tests -R '^session_focus_test$' --output-on-failure

#include "../app_state.h"
#include "../session_focus.h"
#include "../../DomainModel/athlete.h"
#include "../../DomainModel/session.h"
#include "../../DomainModel/session_diagnostics.h"
#include "../../DomainModel/shot.h"
#include "../../Core/work_pool.h"
#include "../../Export/firing_reader_store.h"
#include "../../Export/recording_loader.h"   // SwingStoreRecordingReader
#include "../../Export/rule_data_install.h"
#include "../../Export/session_store.h"
#include "../../Export/swing_store_repository.h"
#include "../../Gui/diagnostics/session_diagnostics_adapter.h"
#include "../../Gui/diagnostics/session_diagnostics_model.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>
#include <memory>

#ifndef PP_GUI_SRC_DIR
#  define PP_GUI_SRC_DIR "."
#endif
#ifndef PP_LIVE_SWINGS_DIR
#  define PP_LIVE_SWINGS_DIR "."
#endif

using namespace pinpoint;

static int g_fail = 0;
static int g_run  = 0;

static void check(const QString &label, bool ok)
{
    ++g_run;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", qPrintable(label));
    if (!ok) ++g_fail;
}

static void spin(int ms = 50)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
}

static const AthleteId kAthlete = AthleteId::fromString(u"5a1d9c3e-2b7f-4e8a-9c10-7d2e6f3b4a21");
static const QString   kName    = QStringLiteral("Test Golfer");
static const QString   kType    = QStringLiteral("wrist");
static const QString   kPattern = QStringLiteral("date-name-type");

static QString fixture(const char *name)
{
    return QDir(QLatin1String(PP_LIVE_SWINGS_DIR)).filePath(QLatin1String(name) + QLatin1String("/swing.json"));
}

// session_input.json as it is on disk: { present, conditionId, fromShot }.
struct OnDisk { bool present = false; QString id; int from = -99; QByteArray bytes; };
static OnDisk focusOnDisk(const QString &folder)
{
    OnDisk d;
    QFile f(QDir(folder).filePath(QStringLiteral("session_input.json")));
    if (!f.open(QIODevice::ReadOnly)) return d;
    d.bytes = f.readAll();
    const QJsonObject o = QJsonDocument::fromJson(d.bytes).object().value(QStringLiteral("declaredFocus")).toObject();
    d.present = !o.isEmpty();
    d.id      = o.value(QStringLiteral("conditionId")).toString();
    d.from    = o.value(QStringLiteral("fromShot")).toInt(-99);
    return d;
}

// One scratch library, the app's start and end through AppState over today's objects as main.cpp wires them (the capture
// service's half is the Athlete's start / resume and the Session's End; the pick is SessionController's, here a string).
struct Rig {
    QTemporaryDir                                  dir;
    SessionStore                                   store;
    std::unique_ptr<SwingStoreRecordingRepository> recordings;
    std::shared_ptr<RecordingReader>               reader;
    std::unique_ptr<WorkReceiver>                  receiver;
    std::unique_ptr<SessionDiagnosticsAdapters>    adapters;   // the screens' door (main.cpp's diagnosticsAdapters)
    std::unique_ptr<Athlete>                       athlete;
    AppState                                       state;
    QPointer<Session>                              session;    // the capture service's open session
    QString                                        folder;
    QString                                        pick;       // SessionController::focusConditionId
    bool                                           unusable = false;   // the library cannot take a session (B1)

    Rig()
    {
        store.setLockWaitForTest(200);
        store.setOsTrashForTest(false);
        recordings = std::make_unique<SwingStoreRecordingRepository>(store);
        reader     = std::make_shared<SwingStoreRecordingReader>(store);
        receiver   = std::make_unique<WorkReceiver>([this](const WorkAddress &a) {
            return diagnosticsWorkTarget(athlete ? athlete->loadedSession(SessionId::fromString(a.owner)) : nullptr, a);
        });
        adapters = std::make_unique<SessionDiagnosticsAdapters>();
        athlete  = std::make_unique<Athlete>(nullptr, &store, kAthlete);
        AthleteRecord r;
        r.name = kName;
        athlete->setStored(r, dir.path() + QStringLiteral("/Test-Golfer"), SessionOwner{ kAthlete, kName, dir.path() },
                           Athlete::Source::Library, false);
        athlete->setShotDoors({ recordings.get(), reader });
        DiagnosticsDoors d;
        d.receiver    = receiver.get();
        d.ruleData    = RuleData::installed();
        d.reader      = std::make_shared<SwingStoreFiringReader>(store, *recordings);
        d.persist     = false;
        d.synchronous = true;
        d.attach      = &SessionDiagnostics::attach;
        athlete->setDiagnosticsDoors(d);

        AppStateDoors doors;
        doors.sessionAt     = [this](const QString &f) { return athlete->sessionAt(f); };
        doors.captureFolder = [this] { return session ? folder : QString(); };
        doors.openCaptureSession = [this](bool extend) {   // ShotProcessor::beginSessionFolder, its two halves
            Session *s = nullptr;
            if (extend) {
                const SessionId id = athlete->todaysSession(kType);
                if (!id.isNull()) s = athlete->resumeSession(id, kType);
            }
            if (!s && !unusable) s = athlete->startSession(kType, kPattern);
            opened(s);
        };
        doors.closeCaptureSession = [this] { if (session) session->end(); session = nullptr; };
        doors.pendingFocus        = [this] { return pick; };
        doors.dropPendingFocus    = [this] { pick.clear(); };
        doors.declareStartFocus   = [this](const QString &f) {
            return session && declareStartFocus(session.data(), f).ok();   // ShotProcessor::declareStartFocus's held half
        };
        state.setDoors(std::move(doors));
    }
    ~Rig()
    {
        adapters.reset();
        athlete.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    // The capture service has a session (or none) open now, and says so: ShotProcessor emits activeSessionDirChanged on
    // every open (beginSessionFolder, captureSession's late start, D7's restart) and main.cpp's connection calls
    // AppState::refreshCaptureTarget — the hand-over's one call site. ShotProcessor itself is not linked here.
    void opened(Session *s)
    {
        session = s;
        const auto e = s ? store.open(s->id()) : std::nullopt;
        folder = e ? e->folder : QString();
        state.refreshCaptureTarget();
    }
    // captureSession's late start (shot_processor.cpp: "a session whose folder could not be made at Start: its first
    // swing makes it"): the start, then the emit.
    void firstSwingMakesTheSession() { opened(athlete->startSession(kType, kPattern)); }

    Shot *land(const char *doc)
    {
        Shot *shot = session ? session->addShot() : nullptr;
        if (!shot) return nullptr;
        QFile::copy(fixture(doc), QDir(shot->folder()).filePath(QStringLiteral("swing.json")));
        session->shotDocumentWritten(shot->ref());
        return shot;
    }
    QString diagnosedFocus() const
    {
        SessionDiagnostics *d = session ? session->diagnostics() : nullptr;
        const auto in = d ? d->diagnosisInput() : nullptr;
        return in ? in->focusConditionId : QStringLiteral("<no diagnosis>");
    }
    int diagnosedFrom() const
    {
        SessionDiagnostics *d = session ? session->diagnostics() : nullptr;
        const auto in = d ? d->diagnosisInput() : nullptr;
        return in ? in->focusFromShot : -99;
    }
};

static void testStartAndExtend()
{
    std::printf("\nL1 / L2 — a wizard start declares the focus on the Session; Extend keeps it\n");
    Rig rig;

    // L1: the wizard's Start (Main.qml: the pick, then app.state.startSession(false)), no model or panel made.
    rig.pick = QStringLiteral("early_extension");
    rig.state.startSession(false);
    Session *s = rig.session.data();
    check(QStringLiteral("L1a a new session started"), s && !rig.folder.isEmpty());
    if (!s) return;
    const SessionId id = s->id();
    const QString folder = rig.folder;
    OnDisk disk = focusOnDisk(folder);
    check(QStringLiteral("L1b session_input.json holds the focus from shot 0 before any shot (%1, %2)").arg(disk.id).arg(disk.from),
          disk.present && disk.id == QStringLiteral("early_extension") && disk.from == 0);
    check(QStringLiteral("L1c the pick does not outlive the start it was made for"), rig.pick.isEmpty());
    check(QStringLiteral("L1d no diagnostics model or panel was made"), !rig.adapters->shownFor(s, folder));

    Shot *one = rig.land("lm_7iron");
    spin();
    check(QStringLiteral("L1e the owner diagnoses the shot against the declared focus (%1 from %2)")
              .arg(rig.diagnosedFocus()).arg(rig.diagnosedFrom()),
          one && rig.diagnosedFocus() == QStringLiteral("early_extension") && rig.diagnosedFrom() == 0);

    rig.state.endSession(false);
    disk = focusOnDisk(folder);
    check(QStringLiteral("L1f after End the file still holds it (%1)").arg(disk.id),
          disk.present && disk.id == QStringLiteral("early_extension") && disk.from == 0);

    // L2: End, then Extend, with the panel's adapter pointed at the session — first with no pick (the toolbar's Extend).
    rig.state.startSession(true);
    Session *again = rig.session.data();
    check(QStringLiteral("L2a Extend resumed the same session"), again && again->id() == id);
    if (!again) return;
    QSignalSpy evt10(again, &Session::sessionIntentChanged);
    SessionDiagnosticsModel *panel = rig.adapters->of(again, QStringLiteral("panel"));
    check(QStringLiteral("L2b the panel's adapter is made for it"), panel && panel->isAdapter());
    if (!panel) return;
    panel->setBaselineAuthorityName(QStringLiteral("live"));
    panel->activateSession(folder);   // the panel's pointing, as each rebuilt stage panel points
    spin();
    panel->activateSession(folder);   // a Capture↔Replay flip rebuilds the panel: pointed again
    spin();
    disk = focusOnDisk(folder);
    check(QStringLiteral("L2c the saved focus is kept on disk (%1, %2)").arg(disk.id).arg(disk.from),
          disk.present && disk.id == QStringLiteral("early_extension") && disk.from == 0);
    check(QStringLiteral("L2d nothing rewrote the session's intent (EVT-10 x%1)").arg(evt10.count()), evt10.count() == 0);
    check(QStringLiteral("L2e the panel shows the Session's focus (%1)").arg(panel->focusConditionId()),
          panel->focusConditionId() == QStringLiteral("early_extension"));
    rig.state.endSession(false);

    // …and with a stale pick (a wizard pick that never reached a start): Extend still keeps the saved intent.
    const QByteArray before = focusOnDisk(folder).bytes;
    rig.pick = QStringLiteral("over_the_top");
    rig.state.startSession(true);
    check(QStringLiteral("L2f Extend with a stale pick resumed the same session"), rig.session && rig.session->id() == id);
    disk = focusOnDisk(folder);
    check(QStringLiteral("L2g …and kept the saved focus, byte for byte (%1)").arg(disk.id),
          disk.bytes == before && disk.id == QStringLiteral("early_extension"));
    check(QStringLiteral("L2h …and the stale pick is dropped, not kept for a later start"), rig.pick.isEmpty());
    rig.state.endSession(false);

    // A start with no pick: an open assessment, nothing written.
    rig.state.startSession(false);
    check(QStringLiteral("L1g a new session started with no pick"), rig.session && rig.session->id() != id);
    check(QStringLiteral("L1h …writes no focus (an open assessment)"), rig.session && !focusOnDisk(rig.folder).present);
    rig.state.endSession(false);
}

static void testLibraryRestartCarry()
{
    std::printf("\nL3 — the library restart carries the ended session's focus (D7, DEC-154)\n");
    Rig rig;
    rig.pick = QStringLiteral("early_extension");
    rig.state.startSession(false);
    Session *old = rig.session.data();
    if (!old) { check(QStringLiteral("L3 a session started"), false); return; }
    rig.land("lm_7iron");
    // A tap on the panel changes the focus mid-session, from a later shot (the model's command: EVT-10).
    SessionIntentEdit tap;
    tap.focus = DeclaredFocus{ QStringLiteral("over_the_top"), 3 };
    old->setSessionIntent(tap);

    // ShotProcessor::onLibraryChanged: read the ending session's focus, End it, start the new one, declare it there.
    const std::optional<QString> carried = declaredFocusOf(old);
    check(QStringLiteral("L3a the ending session's focus is read from the Session (%1)").arg(carried.value_or(QStringLiteral("<none>"))),
          carried && *carried == QStringLiteral("over_the_top"));
    old->end();
    Session *fresh = rig.athlete->startSession(kType, kPattern);
    const auto e = fresh ? rig.store.open(fresh->id()) : std::nullopt;
    const WriteResult w = declareStartFocus(fresh, carried.value_or(QString()));
    const OnDisk disk = focusOnDisk(e ? e->folder : QString());
    check(QStringLiteral("L3b the new session holds it, from its first shot (%1, %2)").arg(disk.id).arg(disk.from),
          w.ok() && disk.present && disk.id == QStringLiteral("over_the_top") && disk.from == 0);
    check(QStringLiteral("L3c an empty focus declares nothing"),
          declareStartFocus(fresh, QString()).status == WriteResult::Status::Unchanged);

    // A folder that has gone (a dropped share): the read cannot say, so the caller falls back on the owner.
    if (e) QDir(e->folder).removeRecursively();
    check(QStringLiteral("L3d a folder that has gone reads as 'cannot say', not as 'no focus'"),
          !declaredFocusOf(fresh).has_value());
}

static void testNoSessionAtStart()
{
    std::printf("\nNS / FR — the pick waits until a Session takes it (review B1; J-64)\n");
    {
        Rig rig;
        rig.unusable = true;   // the share unmounted at Start: no session opens
        rig.pick = QStringLiteral("early_extension");
        rig.state.startSession(false);
        check(QStringLiteral("NS1 Start on an unusable library opens no session"), !rig.session);
        check(QStringLiteral("NS2 …and the pick is kept, waiting (%1)").arg(rig.pick), rig.pick == QStringLiteral("early_extension"));
        rig.unusable = false;   // the share is back; the next swing makes the session
        rig.firstSwingMakesTheSession();
        const OnDisk disk = focusOnDisk(rig.folder);
        check(QStringLiteral("NS3 the session the first swing makes takes it, from shot 0 (%1, %2)").arg(disk.id).arg(disk.from),
              rig.session && disk.id == QStringLiteral("early_extension") && disk.from == 0);
        check(QStringLiteral("NS4 …and only then is the pick dropped"), rig.pick.isEmpty());
        rig.land("lm_7iron");
        spin();
        check(QStringLiteral("NS5 the owner diagnoses the first swing against it (%1)").arg(rig.diagnosedFocus()),
              rig.diagnosedFocus() == QStringLiteral("early_extension"));
        rig.state.endSession(false);
    }
    {
        Rig rig;
        rig.pick = QStringLiteral("early_extension");
        rig.state.startSession(false);
        Session *old = rig.session.data();
        if (!old) { check(QStringLiteral("FR a session started"), false); return; }
        rig.land("lm_7iron");
        SessionIntentEdit tap;
        tap.focus = DeclaredFocus{ QStringLiteral("over_the_top"), 3 };
        old->setSessionIntent(tap);
        // D7 as ShotProcessor::onLibraryChanged makes it: read the ending session's focus, End it, set the carried focus
        // as the pending pick, start — here the restart FAILS (the new library cannot take a session) — then the emit.
        const QString carried = declaredFocusOf(old).value_or(QString());
        old->end();
        rig.pick = carried;
        rig.opened(nullptr);
        check(QStringLiteral("FR1 a failed restart keeps the carried focus pending (%1)").arg(rig.pick),
              rig.pick == QStringLiteral("over_the_top"));
        rig.firstSwingMakesTheSession();
        const OnDisk disk = focusOnDisk(rig.folder);
        check(QStringLiteral("FR2 the session the next swing makes takes it, from shot 0 (%1, %2)").arg(disk.id).arg(disk.from),
              rig.session && rig.session != old && disk.id == QStringLiteral("over_the_top") && disk.from == 0 && rig.pick.isEmpty());
    }
}

// L2's QML SIDE, AT THE SOURCE (review m3): the only QML that writes a declared focus is the tap's handler
// (PpSessionDiagnosticsBody._declareFocus → the model's command), and the only QML that sets the pick is the wizard's
// Start in Main.qml. An adopter like the one 4-d deleted (it called clearFocus / declareFocusFrom from a panel) fails it.
// Not a QQmlEngine check: loading the panel needs the app's context (app, singletons, controllers).
static void testQmlWriters()
{
    std::printf("\nL2Q — the QML that writes the focus: the tap and the wizard's Start only\n");
    const QRegularExpression writes(QStringLiteral(R"(\.(clearFocus|declareFocus|declareFocusFrom)\s*\(|sessionController\.focusConditionId\s*=[^=])"));
    QStringList found;
    int files = 0;
    const QString root = QDir::cleanPath(QLatin1String(PP_GUI_SRC_DIR));   // the define holds "…/Analysis/tests/../../Gui"
    QDirIterator it(root, { QStringLiteral("*.qml"), QStringLiteral("*.js") }, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (QDir(root).relativeFilePath(path).startsWith(QStringLiteral("tests/"))) continue;   // the Gui's own tests
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) continue;
        ++files;
        int n = 0;
        for (const QString &line : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
            ++n;
            if (line.trimmed().startsWith(QStringLiteral("//"))) continue;
            if (writes.match(line).hasMatch()) found << QFileInfo(path).fileName() + QLatin1Char(':') + QString::number(n);
        }
    }
    const auto in = [&](const QString &file) { int c = 0; for (const QString &x : found) if (x.startsWith(file + QLatin1Char(':'))) ++c; return c; };
    check(QStringLiteral("L2Q1 the Gui's QML was read (%1 files)").arg(files), files > 100);
    check(QStringLiteral("L2Q2 the writers are the tap's two calls and the wizard's one: %1").arg(found.join(QStringLiteral(", "))),
          found.size() == 3 && in(QStringLiteral("PpSessionDiagnosticsBody.qml")) == 2 && in(QStringLiteral("Main.qml")) == 1);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    RuleData &rules = installProcessRuleData();
    check(QStringLiteral("the shipped rule data is installed"), rules.snapshot() && rules.snapshot()->pack);
    std::printf("session_focus_test — the declared focus reaches its Session (refactor 4-d)\n");
    testStartAndExtend();
    testLibraryRestartCarry();
    testNoSessionAtStart();
    testQmlWriters();
    std::printf("\n%d of %d checks passed\n", g_run - g_fail, g_run);
    return g_fail == 0 ? 0 : 1;
}
