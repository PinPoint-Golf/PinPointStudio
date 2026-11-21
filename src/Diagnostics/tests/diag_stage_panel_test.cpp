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

// diag_stage_panel_test — THE STAGE PANEL'S CUT-OVER AND ALWAYS FIRE, AS THE APP WIRES THEM (refactor step 3.7-a;
// build/refactor/step-3/3.7/BRIEF.md item 1, NOTES-a.md).
//
// The app's doors (pinpoint::appDiagnosticsDoors, which main.cpp calls) on a temporary library, live-shaped as in
// diag_parallel_live_test (each shot's document written as capture leaves it, without its reading; the reading folded
// in late, EVT-22), and the panel's thin adapter as the panel reaches it (SessionDiagnosticsAdapters::of):
//
//   ALWAYS  no panel, no old model: every live shot is judged by the Session's owner (DEC-02; closes D-02), and
//           diagnostics.json is written by it; work_ons.json is not (WorkOnsController's until 3.7-e)
//   PANEL   the panel's adapter, made by C++ after the shots, shows them all: its rows are the owner's, its stage is
//           the owner's while live; the same object for the same (session, surface), another for the cast (LIVE-2: one
//           owner under both); it holds a use of the Session, so the Session is not released under it
//   READING the newest shot is graded WITH its launch-monitor reading: its row in the adapter has no reading until the
//           reading is written, then has one, with no panel call (CARRY D22b: HEAD's panel graded on shotProcessed and
//           readingApplied did not regrade)
//   CLOSING the panel shows Closing right after End (EVT-09), not reviewing (J-42 item 2; the old panel stayed
//           "established" until reopened)
//   CRASH   a session a crash left in progress (its file says so; not recorded here), opened through the adapter and NOT
//           in review, shows Closing — the old model on it showed its file's stage (CARRY 3.7-0 review 2)
//   FREEZE  a live panel pointed at its session freezes club_baseline.json from the predecessors' ledgers, as the
//           panel's model did, through the Session's own command; the owner reads it (its findings stamp moves)
//   UNHELD  a folder in the golfer's folder whose file names another athlete id: today's engine model, made by C++
//   IDLE    with no view bound the adapter does no view work; a view binding takes in what was owed (review M2)
//   ARCHIVE a session held only to archive it: its owner asks nothing and writes nothing (review B2)
//   NOGOLF  no Session (a session with no golfer: N1): an empty adapter, judging nothing — pointing it at the folder
//           writes no file there
//   REPORT  (refactor 3.7-d) the session report's adapter, set up as SessionReportModel sets it: the owner's rows AS
//           THE GOLFER ANNOTATED THEM on the launch monitor board (a club override is the row's club; a reading left out
//           leaves its monitor-driven rows not assessable), the work-on list over them; it holds a use of the Session
//           while it lives and drops it when the report lets it go; it writes nothing into the session
//   STAMP   (refactor 3.7-d) SessionDiagnosticsModel::currentContentStamp() is the stamp a model computes (the report's
//           fingerprint, now made with no model)
//   TRASH   (review 3.7-d B1) a session trashed while the report holds its adapter, before and after its owner settles:
//           the adapter goes with the Session (its `destroyed` is what SessionReportModel ends the make on), nothing
//           lands on it afterwards
//   SIGMA   (refactor 3.7-d) the gate report's switch through the owner's doors (DiagnosticsDoors::withSigma): σ off
//           moves the findings stamp, so a session loaded under it is judged again, and no row carries a σ
//
//   cmake --build build/tests --target diag_stage_panel_test -j 6
//   ctest --test-dir build/tests -R '^diag_stage_panel_test$' --output-on-failure

#include "../../DomainModel/athlete.h"
#include "../../DomainModel/session.h"
#include "../../DomainModel/session_diagnostics.h"
#include "../../DomainModel/shot.h"
#include "../../Coaching/board_annotations.h"
#include "../../Core/club_vocabulary.h"
#include "../../Core/work_pool.h"
#include "../../Export/firing_reader_store.h"
#include "../../Export/recording_loader.h"
#include "../../Export/rule_data_install.h"
#include "../../Export/session_archiver.h"
#include "../../Export/session_store.h"
#include "../../Export/swing_store.h"
#include "../../Export/swing_store_repository.h"
#include "../../Gui/diagnostics/diag_parallel_run.h"
#include "../../Gui/diagnostics/session_diagnostics_adapter.h"
#include "../../Gui/diagnostics/session_diagnostics_model.h"
#include "../../LaunchMonitor/launch_monitor_reading.h"
#include "../measures/measure_sample.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QPointer>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>
#include <cstdio>
#include <functional>
#include <memory>
#include <thread>

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

static const QString   kName    = QStringLiteral("Test Golfer");
static const QString   kToken   = QStringLiteral("Test-Golfer");
static const AthleteId kAthlete = AthleteId::fromString(u"7c2e9a41-5d3b-4f6e-8a20-1b9c4d7e3f52");
static const QString   kPat     = QStringLiteral("date-name-type");
static const QString   kClub    = QStringLiteral("7 IRON");
static constexpr int   kShots   = 6;

static QString fixture() { return QDir(QLatin1String(PP_LIVE_SWINGS_DIR)).filePath(QStringLiteral("lm_7iron/swing.json")); }

static QJsonObject readJson(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}
static bool writeJson(const QString &path, const QJsonObject &o)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(QJsonDocument(o).toJson(QJsonDocument::Compact)) > 0;
}

static bool pump(const std::function<bool()> &done, int ms = 30000)
{
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThread::msleep(1);
    }
    return true;
}
// A few more turns of the event loop: the adapter's queued busy update and anything posted after a settle.
static void spin(int ms = 50)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
}

// The fixture as capture writes it BEFORE the launch monitor's reading lands (diag_parallel_live_test's).
static QJsonObject withoutReading(int swing, qint64 wallclockMs)
{
    QJsonObject root = readJson(fixture());
    root.remove(QStringLiteral("launchMonitor"));
    QJsonObject an = root.value(QStringLiteral("analysis")).toObject();
    QJsonArray kept;
    for (const QJsonValue &m : an.value(QStringLiteral("metrics")).toArray())
        if (!m.toObject().value(QStringLiteral("key")).toString().startsWith(QLatin1String("lm."))) kept.append(m);
    an[QStringLiteral("metrics")] = kept;
    root[QStringLiteral("analysis")] = an;
    root[QStringLiteral("swing")] = QJsonObject{ { QStringLiteral("id"), QStringLiteral("swing_%1").arg(swing, 4, 10, QLatin1Char('0')) },
                                                 { QStringLiteral("index"), swing } };
    QJsonObject clock = root.value(QStringLiteral("clock")).toObject();
    clock[QStringLiteral("wallclock")] = QDateTime::fromMSecsSinceEpoch(wallclockMs, Qt::UTC).toString(Qt::ISODateWithMs);
    root[QStringLiteral("clock")] = clock;
    return root;
}

// The document as capture leaves it: a .ppsw with its summary block (diag_parallel_live_test's).
static bool writeCaptureDoc(RecordingRepository &door, const Shot *shot, const QJsonObject &root)
{
    const QString swingDir = shot->folder();
    if (!writeJson(QDir(swingDir).filePath(QStringLiteral("swing.json")), root)) return false;
    ReviewEdit club;
    club.club = kClub;
    const WriteResult w = door.editReview(shot->ref(), club);
    return w.ok() && SwingStore::info(swingDir).format == SwingStore::Format::Ppsw;
}

static lm::LaunchMonitorReading fixtureReading(int swing)
{
    const QJsonObject block = readJson(fixture()).value(QStringLiteral("launchMonitor")).toObject();
    lm::LaunchMonitorReading r;
    r.deviceShotId = QString::number(100 + swing);
    r.deviceClub   = block.value(QStringLiteral("deviceClub")).toString();
    r.deviceKind   = block.value(QStringLiteral("kind")).toString();
    r.sourcePath   = block.value(QStringLiteral("sourcePath")).toString();
    r.readAtMs     = QDateTime::currentMSecsSinceEpoch();
    for (const lm::FieldDef &f : lm::fieldDefs()) {
        const QJsonValue v = block.value(QLatin1String(f.rawName));
        if (v.isDouble()) r.*(f.member) = v.toDouble();
    }
    return r;
}

static bool copyTree(const QString &from, const QString &to)
{
    QDir().mkpath(to);
    QDirIterator it(from, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QString rel = QDir(from).relativeFilePath(it.filePath());
        QDir().mkpath(QFileInfo(QDir(to).filePath(rel)).absolutePath());
        if (!QFile::copy(it.filePath(), QDir(to).filePath(rel))) return false;
    }
    return true;
}

// ── One scratch library with the APP's doors ──────────────────────────────────────────────────────────────────
struct Rig {
    QTemporaryDir                                  dir;
    SessionStore                                   store;
    std::unique_ptr<SwingStoreRecordingRepository> recordings;
    std::shared_ptr<RecordingReader>               reader;
    std::unique_ptr<WorkReceiver>                  receiver;
    std::unique_ptr<WorkPool>                      pool;
    std::unique_ptr<SessionDiagnosticsAdapters>    adapters;   // the screens' door (main.cpp's diagnosticsAdapters)
    std::unique_ptr<Athlete>                       athlete;    // destroyed first

    explicit Rig(int writeDebounceMs = 0)
    {
        store.setLockWaitForTest(200);
        store.setOsTrashForTest(false);
        recordings = std::make_unique<SwingStoreRecordingRepository>(store);
        reader     = std::make_shared<SwingStoreRecordingReader>(store);
        receiver   = std::make_unique<WorkReceiver>([this](const WorkAddress &a) {
            return diagnosticsWorkTarget(athlete ? athlete->loadedSession(SessionId::fromString(a.owner)) : nullptr, a);
        });
        pool     = std::make_unique<WorkPool>();
        adapters = std::make_unique<SessionDiagnosticsAdapters>();
        QDir().mkpath(athleteFolder());
        athlete = std::make_unique<Athlete>(nullptr, &store, kAthlete);
        AthleteRecord r;
        r.name = kName;
        athlete->setStored(r, athleteFolder(), SessionOwner{ kAthlete, kName, dir.path() }, Athlete::Source::Library, false);
        athlete->setShotDoors({ recordings.get(), reader });
        // THE APP'S DOORS, from the one function main.cpp calls (a short debounce, so a test need not wait 1.5 s).
        DiagnosticsDoors d = appDiagnosticsDoors(pool.get(), receiver.get(), RuleData::installed(), store, *recordings);
        d.writeDebounceMs = writeDebounceMs;
        athlete->setDiagnosticsDoors(d);
    }
    ~Rig()
    {
        athlete.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        adapters.reset();
        pool.reset();
    }
    QString athleteFolder() const { return dir.path() + QLatin1Char('/') + kToken; }
    static bool settled(SessionDiagnostics *d) { return d && pump([d] { return d->idle() && !d->writePending(); }); }
};

// PRE: an earlier, legacy session reduced by the old model (as a past session's catch-up left it), so a live session
// has predecessors whose ledgers a freeze reads.
static bool earlierSession(Rig &rig)
{
    const QString folder = rig.athleteFolder() + QStringLiteral("/2026-09-01_%1_Swing_01").arg(kToken);
    for (int i = 1; i <= 12; ++i) {
        const QString sw = folder + QStringLiteral("/swing_%1").arg(i, 4, 10, QLatin1Char('0'));
        QDir().mkpath(sw);
        QFile::copy(fixture(), sw + QStringLiteral("/swing.json"));
        QFile(sw + QStringLiteral("/swing.json")).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    SessionDiagnosticsModel m;
    m.setSynchronous(true);
    m.setBaselineAuthority(SessionDiagnosticsModel::BaselineAuthority::Never);
    m.activateSession(folder);
    const bool ok = m.shotCount() == 12 && QFile::exists(folder + QStringLiteral("/diagnostics.json"));
    m.activateSession(QString());
    return ok;
}

// A live shot: allocated, its document written without its reading, EVT-01.
static Shot *liveShot(Rig &rig, Session *s, qint64 at)
{
    Shot *shot = s->addShot();
    if (!shot) return nullptr;
    writeCaptureDoc(*rig.recordings, shot, withoutReading(shot->swingNumber(), at));
    s->shotDocumentWritten(shot->ref());   // EVT-01
    return shot;
}

static QString stageWord(analysis::Stage st)
{
    switch (st) {
    case analysis::Stage::Cold:        return QStringLiteral("cold");
    case analysis::Stage::Forming:     return QStringLiteral("forming");
    case analysis::Stage::Established: return QStringLiteral("established");
    case analysis::Stage::Closing:     return QStringLiteral("closing");
    }
    return QString();
}

// The adapter's shot-log row for `swing` (its "noReading" flag is the row's launch-monitor reading, as the panel shows it).
static QVariantMap logRowOf(const SessionDiagnosticsModel &m, int swing)
{
    for (const QVariant &v : m.shotLog())
        if (v.toMap().value(QStringLiteral("shotId")).toInt() == swing) return v.toMap();
    return {};
}

static void testAlwaysPanelReadingClosing()
{
    std::printf("\nALWAYS · PANEL · READING · CLOSING — a live session on the app's doors\n");
    Rig rig;
    Session *s = rig.athlete->startSession(QStringLiteral("swing"), kPat);
    SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
    check(QStringLiteral("the live session has its owner (the app's doors attach one to every loaded session)"), d != nullptr);
    if (!d) return;
    const QString folder = s->entry() ? s->entry()->folder : QString();
    const qint64 t0 = QDateTime::currentMSecsSinceEpoch();

    // ALWAYS: no panel. Each shot's document, then the previous shot's late reading (EVT-22), as live.
    QList<Shot *> shots;
    for (int i = 1; i <= kShots; ++i) {
        if (Shot *shot = liveShot(rig, s, t0 + i * 45000)) shots << shot;
        if (i >= 2) shots.value(i - 2)->completeCapture(fixtureReading(shots.value(i - 2)->swingNumber()));
        Rig::settled(d);
    }
    check(QStringLiteral("ALWAYSa every live shot was judged with no panel up (D-02 closed): kept"),
          shots.size() == kShots && d->keptShots().size() == kShots && d->diagnosis() != nullptr);
    check(QStringLiteral("ALWAYSb diagnostics.json is written by the owner (the panel's model, its writer, is gone)"),
          QFile::exists(folder + QStringLiteral("/diagnostics.json")) && d->counts().writes >= 1);
    check(QStringLiteral("ALWAYSc work_ons.json is NOT written by it (WorkOnsController's until 3.7-e)"),
          !QFile::exists(folder + QStringLiteral("/work_ons.json")) && d->counts().summaryWrites == 0);

    // PANEL: the adapter, made by C++ now, after the shots.
    SessionDiagnosticsModel *panel = rig.adapters->of(s, QStringLiteral("panel"));
    check(QStringLiteral("PANELa the panel's adapter is made by C++ for the Session, as its child"),
          panel && panel->isAdapter() && panel->parent() == s);
    if (!panel) return;
    panel->setBaselineAuthorityName(QStringLiteral("live"));
    panel->activateSession(folder);   // the panel's pointing
    spin();
    check(QStringLiteral("PANELb it shows every shot the owner judged before it existed"), panel->shotCount() == kShots);
    check(QStringLiteral("PANELc its stage is the owner's, live (not Closing)"),
          panel->stage() == stageWord(d->diagnosis()->stage) && panel->stage() != QLatin1String("closing"));
    check(QStringLiteral("PANELd the same object for the same session and surface"), rig.adapters->of(s, QStringLiteral("panel")) == panel);
    SessionDiagnosticsModel *cast = rig.adapters->of(s, QStringLiteral("cast"));
    check(QStringLiteral("PANELe the cast gets its own view state over the SAME owner (LIVE-2)"),
          cast && cast != panel && cast->shotCount() == panel->shotCount() && cast->stage() == panel->stage());
    check(QStringLiteral("PANELf the two adapters hold a use of the Session each"), s->uses() >= 2);
    const QString lastFolder = shots.isEmpty() ? QString() : shots.last()->folder();
    check(QStringLiteral("PANELg the carousel's folder resolves to the adapter's shot (the pips' key)"),
          !lastFolder.isEmpty() && panel->shotIdForSwingDir(lastFolder) == shots.last()->swingNumber());

    // READING: the newest shot, graded without its reading so far, then its reading written. No view is bound to the
    // adapter yet (review M2): the owner judges it, and the adapter does no view work until a view binds.
    Shot *newest = shots.last();
    const QVariantMap before = logRowOf(*panel, newest->swingNumber());
    check(QStringLiteral("READINGa before its reading lands the newest shot reads as unread by the monitor"),
          before.value(QStringLiteral("noReading")).toBool());
    const WriteResult w = newest->completeCapture(fixtureReading(newest->swingNumber()));   // EVT-22
    Rig::settled(d);
    spin();
    const KeptShot *k = d->kept(newest->swingNumber());
    check(QStringLiteral("READINGb its reading written, the owner judges it again with it, with no panel call"),
          w.ok() && k && k->hasLaunchMonitor);
    check(QStringLiteral("IDLEa with no view bound the adapter did no view work on that diagnosis (still the old row)"),
          panel->views() == 0 && logRowOf(*panel, newest->swingNumber()).value(QStringLiteral("noReading")).toBool());
    panel->addView();   // the panel binds (PpSessionDiagnosticsPanel._bindView)
    const QVariantMap after = logRowOf(*panel, newest->swingNumber());
    check(QStringLiteral("READINGc / IDLEb a view binds: the owed diagnosis is taken in, and the row has the reading"),
          !after.isEmpty() && !after.value(QStringLiteral("noReading")).toBool());

    // CLOSING: End, not reviewing.
    QStringList busyTrail;
    QObject::connect(panel, &SessionDiagnosticsModel::busyChanged, panel, [&] { busyTrail << (panel->busy() ? "1" : "0"); });
    check(QStringLiteral("CLOSINGa End (EVT-09)"), s->end().ok() && !s->inProgress());
    Rig::settled(d);
    spin();
    check(QStringLiteral("CLOSINGb the panel shows Closing right after End, not reviewing (the old panel: still established)"),
          !panel->reviewing() && panel->stage() == QLatin1String("closing"));
    check(QStringLiteral("CLOSINGc busy fell back once the owner wrote (Charts re-reads the file then)"), !panel->busy());
    std::printf("    busy after End: %s\n", qPrintable(busyTrail.join(QStringLiteral(" → "))));
    QString whyNot;
    check(QStringLiteral("CLOSINGd ended, it is still not released while a screen shows it (Q-18: in use)"),
          !rig.athlete->releaseSession(s->id(), &whyNot) && whyNot.contains(QStringLiteral("in use")));
}

static void testCrashLeftSession()
{
    std::printf("\nCRASH — a session a crash left in progress, opened through the adapter, shows Closing\n");
    QString copied;
    QTemporaryDir carry;
    {
        Rig left;   // the process that crashed: its session started, two shots, never ended
        Session *s = left.athlete->startSession(QStringLiteral("swing"), kPat);
        const QString folder = s && s->entry() ? s->entry()->folder : QString();
        const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
        for (int i = 1; i <= 2 && s; ++i) liveShot(left, s, t0 + i * 45000);
        Rig::settled(s ? s->diagnostics() : nullptr);
        copied = carry.path() + QLatin1Char('/') + QFileInfo(folder).fileName();
        check(QStringLiteral("CRASHa the session, its file saying in progress, copied as the crash left it"),
              !folder.isEmpty() && copyTree(folder, copied));
    }
    Rig rig;
    const QString folder = rig.athleteFolder() + QLatin1Char('/') + QFileInfo(copied).fileName();
    check(QStringLiteral("CRASHb placed in this process's library"), copyTree(copied, folder));
    // BEFORE (the old model, as the panel made it, not reviewing): its file's stage.
    QString oldStage;
    {
        SessionDiagnosticsModel old;
        old.setSynchronous(true);
        old.setBaselineAuthority(SessionDiagnosticsModel::BaselineAuthority::Never);
        old.setPersistEnabled(false);   // a reader: the copy stays as the crash left it
        old.activateSession(folder);
        oldStage = old.stage();
        old.activateSession(QString());
    }
    Session *s = rig.athlete->sessionAt(folder);
    check(QStringLiteral("CRASHc in progress by its file, but not being recorded here"), s && s->inProgress() && !s->runningHere());
    if (!s) return;
    Rig::settled(s->diagnostics());
    SessionDiagnosticsModel *panel = rig.adapters->of(s, QStringLiteral("panel"));
    panel->setBaselineAuthorityName(QStringLiteral("live"));
    panel->activateSession(folder);   // NOT reviewing: e.g. the carousel's live folder after a relaunch
    spin();
    std::printf("    the old model's stage: %s; the adapter's: %s (owner: %s)\n", qPrintable(oldStage), qPrintable(panel->stage()),
                s->diagnostics() && s->diagnostics()->diagnosis()
                    ? qPrintable(stageWord(s->diagnostics()->diagnosis()->stage)) : "-");
    check(QStringLiteral("CRASHd the adapter shows Closing, not reviewing (a view mapping: the session stays in progress)"),
          !panel->reviewing() && panel->stage() == QLatin1String("closing") && s->inProgress());
    check(QStringLiteral("CRASHe the old model showed it otherwise (the cut-over's change)"), oldStage != QLatin1String("closing"));

    // UNHELD (J-a3): the same session under the golfer's folder, its file naming ANOTHER athlete id (a cabin session read
    // on a machine whose record of the golfer has another id, until M1): no Session holds it, and the screens keep
    // today's path for it — an engine-mode model made by C++, which judges it as the panel's model did.
    const QString foreign = rig.athleteFolder() + QStringLiteral("/2026-09-02_") + kToken + QStringLiteral("_Swing_01");
    check(QStringLiteral("UNHELDa a copy placed in the golfer's folder"), copyTree(copied, foreign));
    QJsonObject file = readJson(foreign + QStringLiteral("/session.json"));
    file[QStringLiteral("athleteId")] = QStringLiteral("b389a703-4c75-4912-8800-828ddc74fbcc");
    file[QStringLiteral("sessionId")] = QStringLiteral("0b0e5f5e-9c1a-4f1e-9d55-3a4c1f2e7b10");
    writeJson(foreign + QStringLiteral("/session.json"), file);
    QFile::remove(foreign + QStringLiteral("/diagnostics.json"));
    check(QStringLiteral("UNHELDb the golfer's Athlete holds no Session for it"), rig.athlete->sessionAt(foreign) == nullptr);
    rig.adapters->setAthleteFolder([&rig] { return rig.athleteFolder(); });
    SessionDiagnosticsModel *unheld = rig.adapters->of(nullptr, QStringLiteral("panel"), foreign);
    check(QStringLiteral("UNHELDc today's path: an engine-mode model (not an adapter), so no no-golfer notice"),
          unheld && !unheld->isAdapter());
    if (unheld) {
        unheld->setSynchronous(true);
        unheld->setBaselineAuthorityName(QStringLiteral("never"));
        unheld->activateSession(foreign);
        check(QStringLiteral("UNHELDd ...which judges its two shots as the panel's model did"), unheld->shotCount() == 2);
        unheld->activateSession(QString());
    }
}

static void testFreeze()
{
    std::printf("\nFREEZE — a live panel freezes the club baseline through the Session's command\n");
    Rig rig;
    check(QStringLiteral("FREEZEa an earlier session of twelve 7-iron shots, reduced by the old model"), earlierSession(rig));
    Session *s = rig.athlete->startSession(QStringLiteral("swing"), kPat);
    SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
    const QString folder = s && s->entry() ? s->entry()->folder : QString();
    if (!d || folder.isEmpty()) { check(QStringLiteral("FREEZE a live session with its owner"), false); return; }
    const QString file = folder + QStringLiteral("/club_baseline.json");
    check(QStringLiteral("FREEZEb at Start the owner could not freeze one (the predecessors' summaries are an older form)"),
          !QFile::exists(file));
    const QString stampBefore = d->findingsStamp();
    SessionDiagnosticsModel *panel = rig.adapters->of(s, QStringLiteral("panel"));
    panel->setBaselineAuthorityName(QStringLiteral("live"));
    panel->activateSession(folder);   // the panel up on the live session (today's rule: a live panel freezes)
    check(QStringLiteral("FREEZEc the freeze lands (read off the GUI thread)"), pump([panel] { return !panel->busy(); }));
    const bool has7 = QJsonDocument(readJson(file)).toJson().contains("7 IRON");
    check(QStringLiteral("FREEZEd club_baseline.json frozen with the 7 iron's usual"), has7);
    check(QStringLiteral("FREEZEe the owner read it (its findings stamp moved)"), d->findingsStamp() != stampBefore);
}

// ARCHIVE (review B2): housekeeping holds an ended session to archive it — loads it (its owner attaches) and asks for the
// archive job in the same turn — runs the job off the GUI thread while the GUI turns, lands it and lets the session go.
// The owner asks nothing and writes nothing meanwhile, nor at the release: the library's diagnostics.json is the
// archive's copy, byte for byte.
static void testArchive()
{
    std::printf("\nARCHIVE — a session held only to archive it: its owner asks and writes nothing\n");
    Rig rig;
    rig.store.setArchiveJobs(&SessionArchiveRun::archiveInOwnRun);   // main.cpp's wiring (ArchiveController::archiveJobs)
    check(QStringLiteral("ARCHIVEa an ended session reduced by the old model (its stamps are stale for the owner)"), earlierSession(rig));
    const QString folder = rig.athleteFolder() + QStringLiteral("/2026-09-01_%1_Swing_01").arg(kToken);
    const QString file   = folder + QStringLiteral("/diagnostics.json");
    const auto bytes = [](const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); };
    const QByteArray before = bytes(file);
    QTemporaryDir archiveRoot;

    // housekeeping.cpp's prepare, on the owner's thread, in one turn: the hold, then the job.
    Session *s = rig.athlete->sessionAt(folder);
    SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
    check(QStringLiteral("ARCHIVEb held, with its owner attached (every loaded session's diagnostics run: DEC-02)"), d != nullptr);
    if (!d) return;
    const SessionArchiveJob job = s->archiveJob(SessionArchiveOptions{ archiveRoot.path(), true });
    check(QStringLiteral("ARCHIVEc the job is out and the Session says so"), bool(job) && s->archiving());
    s->addUse();
    // The job off the GUI thread while the GUI thread turns (its deferred load, the debounce).
    SessionArchiveResult r;
    std::atomic_bool done{ false };
    std::thread worker([&] { r = job.run({}); done = true; });
    pump([&] { return done.load(); }, 120000);
    worker.join();
    spin(200);
    std::printf("    the owner's asks %lld (the load's, dropped unrun when the job went out), kept %lld, writes %lld\n",
                (long long)d->asks().size(), (long long)d->counts().kept, (long long)d->counts().writes);
    check(QStringLiteral("ARCHIVEd while the archive was out the owner kept nothing (a result landing then is dropped, owed) and wrote nothing"),
          d->counts().kept == 0 && d->counts().writes == 0);
    s->archiveLanded(r);   // housekeeping's land
    s->dropUse();
    spin(200);
    check(QStringLiteral("ARCHIVEe archived"), r.ok && !s->archiving());
    const SessionId id = s->id();
    const quint64 writesBefore = d->counts().writes;
    QString whyNot;
    const bool released = rig.athlete->releaseSession(id, &whyNot);
    std::printf("    released %s%s; writes before the release %lld, after %lld\n", released ? "yes" : "no",
                released ? "" : qPrintable(QStringLiteral(" (") + whyNot + QLatin1Char(')')), (long long)writesBefore,
                (long long)(released ? writesBefore : d->counts().writes));
    check(QStringLiteral("ARCHIVEf released (the pass loaded it): nothing written at the release"),
          released && writesBefore == 0);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    spin(200);
    const QByteArray after = bytes(file);
    const QString mirror = r.archivePath + QStringLiteral("/diagnostics.json");
    std::printf("    library diagnostics.json %lld bytes before, %lld after; the archive's %lld\n",
                (long long)before.size(), (long long)after.size(), (long long)bytes(mirror).size());
    check(QStringLiteral("ARCHIVEg the library's diagnostics.json is unchanged, and equal to the archive's copy"),
          !before.isEmpty() && after == before && bytes(mirror) == after);
}

// The ledger's rows of every shot but `except`, each as its own JSON (diagnostic_ledger.h's writer), from a diagnostics.json.
static QMap<int, QByteArray> rowsOf(const QString &file, int except)
{
    const QJsonObject root = readJson(file);
    analysis::LedgerOptions opt;
    const std::vector<analysis::ShotRecord> rows = analysis::fromJson(root.value(QStringLiteral("ledger")).toObject(), &opt);
    QMap<int, QByteArray> out;
    for (const analysis::ShotRecord &r : rows)
        if (r.shotId != except)
            out.insert(r.shotId, QJsonDocument(analysis::toJson({ r }, opt)).toJson(QJsonDocument::Compact));
    return out;
}

// ARCHIVEh (review R2-B1): a golfer's command reaches one shot WHILE its session's archive is out. After the archive lands
// (made): that shot alone is judged again and the session written once; the command is in the file; every other shot's
// rows are the ledger's as before the archive (none judged again from its stub). ARCHIVEi: a FAILED archive asks what the
// hold owed (the stale shots) and writes them.
static void testArchiveEvents()
{
    std::printf("\nARCHIVEh/i — a command while an archive is out; a failed archive\n");
    const auto bytes = [](const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); };
    {
        Rig rig(300);   // a debounce, as the app's (one write per settled change)
        rig.store.setArchiveJobs(&SessionArchiveRun::archiveInOwnRun);
        check(QStringLiteral("ARCHIVEh setup: an ended session reduced by the old model"), earlierSession(rig));
        const QString folder = rig.athleteFolder() + QStringLiteral("/2026-09-01_%1_Swing_01").arg(kToken);
        const QString file   = folder + QStringLiteral("/diagnostics.json");
        const QMap<int, QByteArray> before = rowsOf(file, 5);
        QTemporaryDir archiveRoot;
        Session *s = rig.athlete->sessionAt(folder);
        SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
        const SessionArchiveJob job = s ? s->archiveJob(SessionArchiveOptions{ archiveRoot.path(), true }) : SessionArchiveJob{};
        if (!d || !job) { check(QStringLiteral("ARCHIVEh a held session with its job out"), false); return; }
        SessionArchiveResult r;
        std::atomic_bool done{ false };
        std::thread worker([&] { r = job.run({}); done = true; });
        pump([&] { return done.load(); }, 120000);
        worker.join();
        // The command, while the archive is still out (it has not landed): the golfer sets shot 5's intent.
        Shot *five = s->shot(ShotRef(s->id(), 5));
        const WriteResult w = five ? five->setShotIntent(QStringLiteral("draw")) : WriteResult{};
        spin(100);
        const quint64 writesOut = d->counts().writes;
        s->archiveLanded(r);
        check(QStringLiteral("ARCHIVEh1 archived, the command saved in its shot (EVT-07) while the archive was out, nothing written meanwhile"),
              r.ok && w.ok() && writesOut == 0
                  && rig.recordings->readReview(five->ref()).shotIntent == QLatin1String("draw"));
        pump([&] { return d->idle() && !d->writePending() && d->counts().writes > 0; }, 30000);
        spin(500);
        const QJsonObject derived = readJson(file).value(QStringLiteral("session")).toObject()
                                        .value(QStringLiteral("derived")).toObject().value(QStringLiteral("5")).toObject();
        std::printf("    writes after it landed %lld; shot 5's derived intent \"%s\"\n", (long long)d->counts().writes,
                    qPrintable(derived.value(QStringLiteral("intent")).toString()));
        check(QStringLiteral("ARCHIVEh2 after it lands: shot 5 judged again, the session written exactly once"),
              d->counts().writes == 1);
        check(QStringLiteral("ARCHIVEh3 diagnostics.json reflects the command (shot 5's derived intent)"),
              derived.value(QStringLiteral("intent")).toString() == QLatin1String("draw"));
        const QMap<int, QByteArray> after = rowsOf(file, 5);
        int same = 0;
        for (auto it = before.constBegin(); it != before.constEnd(); ++it) same += after.value(it.key()) == it.value();
        std::printf("    the other shots' rows equal before the archive: %d of %lld\n", same, (long long)before.size());
        check(QStringLiteral("ARCHIVEh4 every other shot's rows are the ledger's as before the archive"),
              before.size() == 11 && same == before.size() && after.size() == before.size());
    }
    {
        Rig rig(300);
        rig.store.setArchiveJobs(&SessionArchiveRun::archiveInOwnRun);
        check(QStringLiteral("ARCHIVEi setup: an ended session reduced by the old model"), earlierSession(rig));
        const QString folder = rig.athleteFolder() + QStringLiteral("/2026-09-01_%1_Swing_01").arg(kToken);
        const QString file   = folder + QStringLiteral("/diagnostics.json");
        const QByteArray before = bytes(file);
        QTemporaryDir tmp;
        const QString notADir = tmp.path() + QStringLiteral("/a-file");   // an archive root that is a file: the run fails
        QFile f(notADir);
        f.open(QIODevice::WriteOnly);
        f.write("x");
        f.close();
        Session *s = rig.athlete->sessionAt(folder);
        SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
        const SessionArchiveJob job = s ? s->archiveJob(SessionArchiveOptions{ notADir, true }) : SessionArchiveJob{};
        if (!d || !job) { check(QStringLiteral("ARCHIVEi a held session with its job out"), false); return; }
        SessionArchiveResult r;
        std::atomic_bool done{ false };
        std::thread worker([&] { r = job.run({}); done = true; });
        pump([&] { return done.load(); }, 120000);
        worker.join();
        spin(100);
        const size_t asksOut = d->asks().size();
        const quint64 writesOut = d->counts().writes;
        s->archiveLanded(r);
        pump([&] { return d->idle() && !d->writePending() && d->counts().writes > 0; }, 60000);
        std::printf("    archive %s (%s); asks %lld while out, %lld after; kept %lld; writes %lld\n", r.ok ? "made" : "failed",
                    qPrintable(r.error), (long long)asksOut, (long long)d->asks().size(), (long long)d->counts().kept,
                    (long long)d->counts().writes);
        check(QStringLiteral("ARCHIVEi1 the archive failed, nothing written while it was out"), !r.ok && writesOut == 0);
        check(QStringLiteral("ARCHIVEi2 what was owed (every stale shot) is judged after it lands, and written"),
              d->asks().size() >= asksOut + 12 && d->counts().kept >= 12 && d->counts().writes >= 1 && bytes(file) != before);
    }
}

static void testNoGolfer()
{
    std::printf("\nNOGOLF — a session no golfer owns: an empty adapter, nothing judged\n");
    Rig rig;
    const QString folder = rig.dir.path() + QStringLiteral("/unknown/2026-11-21_unknown_Swing_01");
    const QString sw = folder + QStringLiteral("/swing_0001");
    QDir().mkpath(sw);
    QFile::copy(fixture(), sw + QStringLiteral("/swing.json"));
    check(QStringLiteral("NOGOLFa the athlete holds no Session for it (app.sessionAt answers null)"),
          rig.athlete->sessionAt(folder) == nullptr);
    SessionDiagnosticsModel *empty = rig.adapters->of(nullptr, QStringLiteral("panel"));
    check(QStringLiteral("NOGOLFb an empty adapter, never null"), empty && empty->isAdapter() && empty->shotCount() == 0);
    empty->activateSession(folder);
    spin();
    check(QStringLiteral("NOGOLFc pointing it at the folder judges nothing and writes no file there"),
          empty->shotCount() == 0 && !QFile::exists(folder + QStringLiteral("/diagnostics.json"))
              && !QFile::exists(folder + QStringLiteral("/club_baseline.json")));
    check(QStringLiteral("NOGOLFd one empty adapter per surface"), rig.adapters->of(nullptr, QStringLiteral("panel")) == empty
                                                                && rig.adapters->of(nullptr, QStringLiteral("cast")) != empty);
}

// The bytes of a session file, "" when there is none.
static QByteArray fileBytes(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// An ended session of kShots shots, each with its reading, on the rig's doors.
static Session *endedSession(Rig &rig)
{
    Session *s = rig.athlete->startSession(QStringLiteral("swing"), kPat);
    SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
    if (!d) return nullptr;
    const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
    for (int i = 1; i <= kShots; ++i) {
        Shot *shot = liveShot(rig, s, t0 + i * 45000);
        if (shot) shot->completeCapture(fixtureReading(shot->swingNumber()));
        Rig::settled(d);
    }
    if (!s->end().ok()) return nullptr;
    Rig::settled(d);
    return s;
}

static void testReport()
{
    std::printf("\nREPORT — the session report's adapter (refactor 3.7-d)\n");
    Rig rig;
    Session *s = endedSession(rig);
    SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
    check(QStringLiteral("REPORT pre: an ended session of %1 shots, judged by its owner").arg(kShots),
          d && int(d->keptShots().size()) == kShots);
    if (!d) return;
    const QString folder = s->entry() ? s->entry()->folder : QString();
    // The golfer's two board annotations, through the Shot's own commands (the board's): shot 2 re-labelled, shot 3's
    // reading left out.
    const QList<Shot *> shots = s->shots();
    Shot *relabelled = shots.value(1), *leftOut = shots.value(2);
    const bool marked = relabelled && leftOut && relabelled->correctClub(QStringLiteral("8 IRON")).ok()   // EVT-04
                        && leftOut->excludeReading(true).ok();                                              // EVT-05
    Rig::settled(d);
    check(QStringLiteral("REPORT pre: shot 2 re-labelled 8 IRON and shot 3's reading left out, on the board"), marked);
    const QByteArray diagBefore = fileBytes(folder + QStringLiteral("/diagnostics.json"));
    const bool baselineBefore = QFile::exists(folder + QStringLiteral("/club_baseline.json"));
    const int usesBefore = s->uses();

    // As SessionReportModel::start sets it up (main.cpp's door: diagnosticsAdapters.of(app.sessionAt(dir), "report", dir)).
    QPointer<SessionDiagnosticsModel> m = rig.adapters->of(s, QStringLiteral("report"), folder);
    check(QStringLiteral("REPORTa the report's adapter is the Session's, its own surface (not the panel's)"),
          m && m->isAdapter() && m->parent() == s && m != rig.adapters->of(s, QStringLiteral("panel"), folder));
    if (!m) return;
    m->setReviewing(true);
    m->setPersistEnabled(false);
    m->setBaselineAuthority(SessionDiagnosticsModel::BaselineAuthority::Never);
    m->addView();
    m->activateSession(folder);
    spin();
    check(QStringLiteral("REPORTb it holds a use of the Session while the report holds it (CARRY: every holder addUse()s)"),
          s->uses() == usesBefore + 1 + 1);   // the report's, and the panel adapter asked for in REPORTa
    check(QStringLiteral("REPORTc it shows every shot the owner kept, not busy"), m->shotCount() == kShots && !m->busy());
    const analysis::ShotRecord *two = nullptr, *three = nullptr;
    for (const analysis::ShotRecord &r : m->shotRecords()) {
        if (r.shotId == relabelled->swingNumber()) two = &r;
        if (r.shotId == leftOut->swingNumber()) three = &r;
    }
    check(QStringLiteral("REPORTd the re-labelled shot's row reads the board's club (the kicker's and the pages' club)"),
          two && two->club == QLatin1String("8 IRON"));
    const QSet<QString> lm = analysis::launchMonitorMeasures(RuleData::installed()->snapshot()->pack->pack());
    int lmRows = 0, lmAssessed = 0;
    if (three)
        for (const analysis::ConditionRow &r : three->rows)
            if (lm.contains(r.drivingMeasureId)) {
                ++lmRows;
                if (r.state != analysis::ShotState::NotAssessable) ++lmAssessed;
            }
    check(QStringLiteral("REPORTe the left-out shot's monitor-driven rows are not assessable (%1 rows)").arg(lmRows),
          three && lmRows > 0 && lmAssessed == 0);
    check(QStringLiteral("REPORTf the work-on list is reduced over those rows (sessionWorkOns, the report's FRESH list)"),
          m->sessionWorkOns().shotCount == kShots);
    check(QStringLiteral("REPORTg nothing written into the session by it (diagnostics.json byte-equal; no baseline frozen)"),
          fileBytes(folder + QStringLiteral("/diagnostics.json")) == diagBefore
              && QFile::exists(folder + QStringLiteral("/club_baseline.json")) == baselineBefore);
    // The report lets it go (SessionReportModel::letModelGo): its view dropped, the adapter deleted.
    m->dropView();
    m->deleteLater();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(QStringLiteral("REPORTh let go, its use of the Session is dropped"), !m && s->uses() == usesBefore + 1);
}

static void testTrashDuringMake()
{
    std::printf("\nTRASH — the session trashed while the report holds its adapter (review 3.7-d B1)\n");
    for (const bool settled : { false, true }) {
        const QString when = settled ? QStringLiteral("after the owner settled") : QStringLiteral("before the owner settled");
        Rig rig;
        Session *s = endedSession(rig);
        if (!s) { check(QStringLiteral("TRASH pre: an ended session"), false); return; }
        const SessionId id = s->id();
        const QString folder = s->entry() ? s->entry()->folder : QString();
        // Let it go, then load it as the report's door does (a session no screen holds): its owner has work out.
        rig.athlete->releaseSession(id);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        Session *again = rig.athlete->sessionAt(folder);
        SessionDiagnostics *d = again ? again->diagnostics() : nullptr;
        QPointer<SessionDiagnosticsModel> m = again ? rig.adapters->of(again, QStringLiteral("report"), folder) : nullptr;
        if (!d || !m) { check(QStringLiteral("TRASH pre: the report's adapter of a reloaded session"), false); return; }
        bool gone = false;
        QObject::connect(m.data(), &QObject::destroyed, [&gone] { gone = true; });
        m->addView();
        m->activateSession(folder);
        const bool busyAtStart = m->busy() || !d->idle();
        if (settled) { Rig::settled(d); spin(); }   // the adapter's busy falls a turn after the owner's
        check(QStringLiteral("TRASH %1: pre — the owner %2").arg(when, settled ? QStringLiteral("idle") : QStringLiteral("still busy")),
              settled ? (d->idle() && !m->busy()) : busyAtStart);
        QString where;
        const WriteResult t = rig.athlete->trashSession(id, &where);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        spin(300);   // anything the owner had out lands now, on nothing
        check(QStringLiteral("TRASH %1: the trash goes through though the report holds a use").arg(when), t.ok());
        check(QStringLiteral("TRASH %1: the report's adapter goes with its Session, and says so (destroyed)").arg(when),
              gone && !m && rig.athlete->loadedSession(id) == nullptr);
    }
}

static void testSigmaDoor()
{
    std::printf("\nSIGMA — the gate report's σ switch through the owner's doors (refactor 3.7-d)\n");
    Rig rig;
    Session *s = endedSession(rig);
    SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
    if (!d) { check(QStringLiteral("SIGMA pre: an ended session"), false); return; }
    const SessionId id = s->id();
    const QString folder = s->entry() ? s->entry()->folder : QString();
    const KeptShot *k = d->kept(1);
    const QString stampOn = k ? k->findingsStamp : QString();
    int quantifiedOn = 0;
    for (const int n : d->keptShots())
        if (const KeptShot *x = d->kept(n))
            for (const analysis::ConditionRow &r : x->findings.rows) quantifiedOn += r.quantified ? 1 : 0;
    check(QStringLiteral("SIGMA pre: with the tuned default some rows carry a σ (%1)").arg(quantifiedOn),
          !stampOn.isEmpty() && (quantifiedOn > 0) == tuned::diagUncertainty::kEnabled);
    QString whyNot;
    const bool released = rig.athlete->releaseSession(id, &whyNot);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    DiagnosticsDoors off = appDiagnosticsDoors(rig.pool.get(), rig.receiver.get(), RuleData::installed(), rig.store, *rig.recordings);
    off.writeDebounceMs = 0;
    off.persist   = false;   // as the tool runs it: nothing written
    off.withSigma = false;
    rig.athlete->setDiagnosticsDoors(off);
    Session *again = released ? rig.athlete->sessionAt(folder) : nullptr;
    SessionDiagnostics *d2 = again ? again->diagnostics() : nullptr;
    Rig::settled(d2);
    const KeptShot *k2 = d2 ? d2->kept(1) : nullptr;
    int quantifiedOff = 0;
    if (d2)
        for (const int n : d2->keptShots())
            if (const KeptShot *x = d2->kept(n))
                for (const analysis::ConditionRow &r : x->findings.rows) quantifiedOff += r.quantified ? 1 : 0;
    check(QStringLiteral("SIGMAa σ off moves the findings stamp, so the session is judged again under it"),
          k2 && !k2->findingsStamp.isEmpty() && k2->findingsStamp != stampOn && int(d2->keptShots().size()) == kShots);
    check(QStringLiteral("SIGMAb and no row carries a σ"), d2 && quantifiedOff == 0);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    std::printf("diag_stage_panel_test\n");
    RuleData &rules = installProcessRuleData();
    check(QStringLiteral("the shipped pack is read"), rules.snapshot() && rules.snapshot()->pack);
    if (!rules.snapshot() || !rules.snapshot()->pack) return 1;

    testAlwaysPanelReadingClosing();
    testCrashLeftSession();
    testFreeze();
    testArchive();
    testArchiveEvents();
    testNoGolfer();
    testReport();
    testTrashDuringMake();
    testSigmaDoor();

    // STAMP (refactor 3.7-d): the report's fingerprint takes the content stamp with no model of its own; it is the one a
    // model computes, so a report made before 3.7-d still reads "current" after it.
    {
        std::printf("\nSTAMP — the content stamp without a model (refactor 3.7-d)\n");
        SessionDiagnosticsModel m;
        const QString stamp = SessionDiagnosticsModel::currentContentStamp();
        check(QStringLiteral("STAMPa the model-free stamp equals a model's (%1)").arg(stamp),
              !stamp.isEmpty() && stamp == m.contentStamp());
    }

    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
