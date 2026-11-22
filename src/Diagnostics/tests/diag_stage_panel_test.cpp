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
//           diagnostics.json is written by it; its summary, work_ons.json, waits for End while it is live (3.7-e: no
//           per-shot summary write), and is written by it at End (the catch-up's cut-over: its one writer)
//   PANEL   the panel's adapter, made by C++ after the shots, shows them all: its rows are the owner's, its stage is
//           the owner's while live; the same object for the same (session, surface), another for the cast (LIVE-2: one
//           owner under both); it holds a use of the Session WHILE A VIEW IS BOUND (3.7-e), so the Session is not
//           released under a screen that shows it
//   READING the newest shot is graded WITH its launch-monitor reading: its row in the adapter has no reading until the
//           reading is written, then has one, with no panel call (CARRY D22b: HEAD's panel graded on shotProcessed and
//           readingApplied did not regrade)
//   CLOSING the panel shows Closing right after End (EVT-09), not reviewing (J-42 item 2; the old panel stayed
//           "established" until reopened)
//   CRASH   a session a crash left in progress (its file says so; not recorded here), opened through the adapter and NOT
//           in review, shows Closing — the old model on it showed its file's stage (CARRY 3.7-0 review 2)
//   FREEZE  (refactor 3.7-e: ONE freeze rule, J-a8) a panel's pointing freezes nothing; the catch-up service sums up an
//           earlier session (its owner writes a summary fresh by the scan's rule), then takes the ended live session in
//           hand, whose owner freezes club_baseline.json at first need from that summary (its findings stamp moves); it
//           releases the session it loaded and keeps the one it did not
//   CATCHUP (refactor 3.7-e; CARRY 3.6 review 2) a summary whose facts were made under a rule one version behind, and
//           nothing else, is stale; the catch-up takes it in hand and its owner writes it again, current
//   CATCHUP-TRASH (3.7-e) the session in hand trashed from under the catch-up: answered as gone, the next one taken
//   FPRINT  (3.7-e round 2) the summary's fingerprint moves with each R-STAMP-1 input on its own
//   LIVEBASE (3.7-e round 2) old-form predecessors summed up at Start (the catch-up paused); the live session freezes then
//   LIVEBASE g (3.7-f) the same asked at a resume of a session already loaded (no attach): AthleteCatchUp::followResumes
//   LIVEORDER (3.7-e round 3, R2-B1) the app's order: a session in hand, Start, then the pause — frozen without End
//   USE     (refactor 3.7-e; CARRY 3.7-a, 3.7-c R2-m2) an adapter holds a use of its Session only while a view is bound:
//           made and not shown it holds none, so the Session can be released; shown, it cannot
//   EVT13   (3.9's test, now that SessionDiagnostics is live: refactor 3.7-e) a pack edit through RuleData's command
//           publishes ONE EVT-13 and every loaded session's owner re-fires every shot under the new snapshot
//   UNHELD  a folder in the golfer's folder whose file names another athlete id: today's engine model, made by C++
//   LOADING an adapter made while its owner is still reading a reviewed session says so until the first diagnosis
//           (3.7-c review B3: the panel's "being read" notice)
//   IDLE    with no view bound the adapter does no view work; a view binding takes in what was owed (review M2)
//   ARCHIVE a session held only to archive it: its owner asks nothing and writes nothing (review B2)
//   NOGOLF  no Session (a folder no athlete of the library holds): an empty adapter, judging nothing — pointing it at
//           the folder writes no file there
//   UNASSIGNED (step 5.1) a capture with no golfer is the Unassigned owner's full Session: judged, no club baseline
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
//   PIPS    the carousel's pips (refactor 3.7-c): diagnosticsAdapters.shownFor answers the adapter a panel shows for the
//           session (the stage panel's, else the cast's), the engine model a panel shows for a folder no Session holds, or
//           nothing — and never makes one; each card's pips through it are the owner's; a panel rebuilt tells nothing;
//           with no panel the cards' own adapter gives every shot its pips with no view half (review B1); a shown
//           adapter destroyed is no longer answered (M3)
//
//   cmake --build build/tests --target diag_stage_panel_test -j 6
//   ctest --test-dir build/tests -R '^diag_stage_panel_test$' --output-on-failure

#include "../../DomainModel/athlete.h"
#include "../../DomainModel/athlete_diagnostics.h"   // the Unassigned owner freezes no baseline (step 5.1)
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
#include "../../Export/swing_paths.h"
#include "../../App/athlete_catch_up.h"
#include "../../Coaching/session_results.h"
#include "../../DomainModel/summary_rules.h"
#include "../../Diagnostics/engine/fire_findings.h"
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
#include <algorithm>
#include <cmath>
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

    explicit Rig(int writeDebounceMs = 0, RuleData *rules = nullptr)
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
        DiagnosticsDoors d = appDiagnosticsDoors(pool.get(), receiver.get(), rules ? rules : RuleData::installed(), store,
                                                 *recordings);
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
    // The composition root's callables over this rig's one athlete (the catch-up service's doors: D18 (c)).
    AthleteCallables callables()
    {
        AthleteCallables c;
        c.listAthletes = [] { return QList<AthleteId>{ kAthlete }; };
        c.loadAthlete  = [this](const AthleteId &id) -> Athlete * { return id == kAthlete ? athlete.get() : nullptr; };
        return c;
    }
};

// WHAT THE CATCH-UP'S SCAN COMPUTES FOR A FOLDER (WorkOnsController::summaryFingerprintFor, mirrored: this suite does not
// link the controller): the one rule (session_reduction.h summaryFingerprintOf) over the rule data, the session's frozen club
// baseline as a current club_baseline.json gives it, the rig athlete's cohort facts (none: "|") and each numbered swing.
static QString scanFingerprintOf(const QString &folder)
{
    std::vector<analysis::SummarySwing> swings;
    for (const QString &name : SwingPaths::swingFolders(folder)) {
        if (SwingPaths::swingNumberOf(name) <= 0) continue;
        const SwingStore::DocInfo doc = SwingStore::info(QDir(folder).filePath(name));
        swings.push_back(analysis::SummarySwing{ name, doc.exists(), doc.size, doc.mtimeMs });
    }
    std::optional<QHash<QString, analysis::ClubBaselineEntry>> baseline;
    const QJsonObject b = readJson(folder + QStringLiteral("/club_baseline.json"));
    const int schema = b.value(QStringLiteral("schema")).toInt(0);
    if (!b.isEmpty() && !(schema > 0 && schema < analysis::kClubBaselineSchemaVersion)) {
        bool ok = false;
        const analysis::ClubBaselineSnapshot snap = analysis::clubBaselineFromJson(b, &ok);
        if (ok) baseline = snap.clubs;
    }
    analysis::SummaryRules r;
    r.rules    = analysis::summaryRulesStamp(firingInputsOf(*RuleData::current()));
    r.baseline = analysis::clubBaselineStamp(baseline);
    r.cohort   = summaryCohortFacts(QString(), QString());
    return analysis::summaryFingerprintOf(r, std::move(swings));
}
// FRESH as the scan reads it: this build's summary, read strictly, its facts under the current rules, its fingerprint the
// folder's now.
static bool summaryFresh(const QString &folder, QString *why = nullptr)
{
    QFile f(folder + QStringLiteral("/work_ons.json"));
    if (!f.open(QIODevice::ReadOnly)) { if (why) *why = QStringLiteral("no work_ons.json"); return false; }
    bool ok = false;
    const analysis::SessionResults r = analysis::sessionResultsFromJson(QJsonDocument::fromJson(f.readAll()).object(),
                                                                        analysis::SessionResultsRead::Strict, &ok);
    if (why) *why = QStringLiteral("strict %1, facts %2, fingerprint %3").arg(ok).arg(r.hasFacts)
                        .arg(r.derivedFrom.fingerprint == scanFingerprintOf(folder) ? QStringLiteral("matches") : QStringLiteral("differs"));
    return ok && r.hasFacts && r.derivedFrom.fingerprint == scanFingerprintOf(folder);
}

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
    check(QStringLiteral("ALWAYSc its summary (work_ons.json) waits for End while the session is live: no per-shot write (3.7-e)"),
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
    {
        const int uses = s->uses();
        panel->addView();
        cast->addView();
        check(QStringLiteral("PANELf the two adapters hold a use of the Session each while a view is bound (3.7-e)"),
              s->uses() == uses + 2);
        panel->dropView();
        cast->dropView();
        check(QStringLiteral("PANELf2 …and none once no view is bound"), s->uses() == uses);
    }
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
    check(QStringLiteral("ALWAYSd at End the owner writes the session's summary, its one writer (3.7-e), fresh by the scan's rule"),
          QFile::exists(folder + QStringLiteral("/work_ons.json")) && d->counts().summaryWrites >= 1 && summaryFresh(folder));
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
    // LOADING (3.7-c review B3): an adapter made while the owner is still reading the session says so, and stops saying
    // so with its first diagnosis — the panel's "being read" notice, never an empty session shown as the session.
    {
        SessionDiagnosticsModel *early = rig.adapters->of(s, QStringLiteral("cast"));
        const bool none = !s->diagnostics() || !s->diagnostics()->diagnosis();
        std::printf("    the owner had %s diagnosis when the adapter was made\n", none ? "no" : "a");
        if (early) early->addView();   // a cast up on it
        check(QStringLiteral("LOADINGa while the owner has no diagnosis of a session with shots, its adapter is loading"),
              early && early->loading() == none);
        Rig::settled(s->diagnostics());
        spin();
        check(QStringLiteral("LOADINGb its first diagnosis lands: no longer loading, and the shots are there"),
              early && !early->loading() && early->shotCount() == 2);
        if (early) early->dropView();
    }
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

static Session *endedSession(Rig &rig);   // below (REPORT's)

static void testFreeze()
{
    std::printf("\nFREEZE — ONE freeze rule, the owner's; the catch-up service sums up and releases (3.7-e)\n");
    Rig rig;
    check(QStringLiteral("FREEZEa an earlier session of twelve 7-iron shots, reduced by the old model (no summary of the owner's)"),
          earlierSession(rig));
    const QString earlier = rig.athleteFolder() + QStringLiteral("/2026-09-01_%1_Swing_01").arg(kToken);
    Session *s = rig.athlete->startSession(QStringLiteral("swing"), kPat);
    SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
    const QString folder = s && s->entry() ? s->entry()->folder : QString();
    if (!d || folder.isEmpty()) { check(QStringLiteral("FREEZE a live session with its owner"), false); return; }
    const QString file = folder + QStringLiteral("/club_baseline.json");
    check(QStringLiteral("FREEZEb at Start the owner could not freeze one (the predecessor has no current summary)"),
          !QFile::exists(file) && !d->holdsClubBaseline());
    const QString stampBefore = d->findingsStamp();
    SessionDiagnosticsModel *panel = rig.adapters->of(s, QStringLiteral("panel"));
    panel->setBaselineAuthorityName(QStringLiteral("live"));
    panel->activateSession(folder);   // the panel up on the live session
    pump([panel] { return !panel->busy(); });
    spin(200);
    check(QStringLiteral("FREEZEc a panel's pointing freezes nothing itself (the adapter's interim live freeze, J-a2, is gone)"),
          !QFile::exists(file));
    const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
    liveShot(rig, s, t0);
    Rig::settled(d);
    check(QStringLiteral("FREEZE the live session ends"), s->end().ok());
    Rig::settled(d);
    SessionId earlierId;   // from the index: nothing loads it before the catch-up does
    for (const SessionIndexEntry &e : rig.athlete->index())
        if (QDir::cleanPath(e.folder) == QDir::cleanPath(earlier)) earlierId = e.id();
    check(QStringLiteral("FREEZE the earlier session is in the index, not loaded"),
          !earlierId.isNull() && !rig.athlete->loadedSession(earlierId));
    AthleteCatchUp catchUp(rig.callables());
    QStringList answered;
    QObject::connect(&catchUp, &AthleteCatchUp::sessionCaughtUp, &catchUp,
                     [&answered](const SessionId &id, const QString &) { answered << id.toString(); });
    catchUp.runSessions(kAthlete, { s->id(), earlierId });   // asked newest first: it takes them oldest first
    check(QStringLiteral("FREEZE the catch-up runs to its end"), pump([&catchUp] { return !catchUp.running(); }, 60000));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QString why;
    check(QStringLiteral("FREEZEd (CATCHUPa) the earlier session's summary is now its owner's, fresh by the scan's rule (%1)")
              .arg((summaryFresh(earlier, &why), why)),
          summaryFresh(earlier));
    const bool has7 = QJsonDocument(readJson(file)).toJson().contains("7 IRON");
    check(QStringLiteral("FREEZEe the ended live session, taken in hand, froze its baseline at first need from that summary"),
          has7 && d->holdsClubBaseline() && d->findingsStamp() != stampBefore);
    check(QStringLiteral("FREEZEf answered oldest first (%1); the session it loaded released, the one it did not kept")
              .arg(answered.join(QStringLiteral(", "))),
          answered.size() == 2 && answered.first() == earlierId.toString()
              && !rig.athlete->loadedSession(earlierId) && rig.athlete->loadedSession(s->id()) == s
              && catchUp.report().released == 1);
}

// CATCHUP (refactor 3.7-e; CARRY 3.6 review 2): a catch-up that skips fresh sessions still rewrites one whose summary's
// facts were made under an older rule — a version bump alone.
static void testCatchUpVersionBump()
{
    std::printf("\nCATCHUP — a facts' rule bump alone stales a summary; the catch-up writes it again\n");
    Rig rig;
    Session *s = endedSession(rig);
    if (!s) { check(QStringLiteral("CATCHUP pre: an ended session"), false); return; }
    const SessionId id = s->id();
    const QString folder = s->entry() ? s->entry()->folder : QString();
    QString why;
    const bool fresh0 = summaryFresh(folder, &why);
    check(QStringLiteral("CATCHUPa at End its owner wrote the summary, fresh by the scan's rule (%1)").arg(why), fresh0);
    rig.athlete->releaseSession(id);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    // The bump: the file's facts made under the theme rule one version behind — nothing else in it changes.
    QJsonObject root = readJson(folder + QStringLiteral("/work_ons.json"));
    QJsonObject rules = root.value(QStringLiteral("factsRules")).toObject();
    rules[QStringLiteral("theme")] = analysis::kThemeRuleVersion - 1;
    root[QStringLiteral("factsRules")] = rules;
    writeJson(folder + QStringLiteral("/work_ons.json"), root);
    check(QStringLiteral("CATCHUPb its facts' rule one version behind, nothing else: no longer fresh (%1)")
              .arg((summaryFresh(folder, &why), why)),
          !summaryFresh(folder));
    AthleteCatchUp catchUp(rig.callables());
    catchUp.runSessions(kAthlete, { id });
    pump([&catchUp] { return !catchUp.running(); }, 60000);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const QJsonObject after = readJson(folder + QStringLiteral("/work_ons.json"));
    check(QStringLiteral("CATCHUPc the catch-up took it in hand: its owner wrote it again, its facts current and fresh (%1)")
              .arg((summaryFresh(folder, &why), why)),
          summaryFresh(folder)
              && after.value(QStringLiteral("factsRules")).toObject().value(QStringLiteral("theme")).toInt() == analysis::kThemeRuleVersion
              && catchUp.report().written == 1 && catchUp.report().released == 1 && !rig.athlete->loadedSession(id));
}

// CATCHUP-TRASH (refactor 3.7-e): a session trashed from the Sessions list while the catch-up has it in hand is answered as
// gone, and the catch-up goes on to the next (it does not wait for ever on a settle that cannot come).
static void testCatchUpTrash()
{
    std::printf("\nCATCHUP-TRASH — the session in hand trashed\n");
    Rig rig;
    Session *a = endedSession(rig);
    Session *b = endedSession(rig);
    if (!a || !b) { check(QStringLiteral("CATCHUP-TRASH pre: two ended sessions"), false); return; }
    const SessionId ia = a->id(), ib = b->id();
    const QString fa = a->entry() ? a->entry()->folder : QString(), fb = b->entry() ? b->entry()->folder : QString();
    rig.athlete->releaseSession(ia);
    rig.athlete->releaseSession(ib);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QFile::remove(fa + QStringLiteral("/work_ons.json"));   // both stale for the scan
    QFile::remove(fb + QStringLiteral("/work_ons.json"));
    AthleteCatchUp catchUp(rig.callables());
    QStringList answered;
    int finished = 0;
    QObject::connect(&catchUp, &AthleteCatchUp::sessionCaughtUp, &catchUp,
                     [&answered](const SessionId &id, const QString &) { answered << id.toString(); });
    QObject::connect(&catchUp, &AthleteCatchUp::finished, &catchUp, [&finished] { ++finished; });
    catchUp.runSessions(kAthlete, { ia, ib });
    const bool inHand = catchUp.running() && rig.athlete->loadedSession(ia);
    const bool trashed = rig.athlete->trashSession(ia).ok();
    check(QStringLiteral("CATCHUP-TRASHa the first session in hand, then trashed from under it"), inHand && trashed);
    const bool done = pump([&catchUp] { return !catchUp.running(); }, 60000);
    pump([] { return false; }, 200);
    check(QStringLiteral("CATCHUP-TRASHb it is answered, the next taken and written, and the catch-up finishes (%1)")
              .arg(answered.join(QStringLiteral(", "))),
          done && finished == 1 && answered.size() == 2 && summaryFresh(fb) && !rig.athlete->loadedSession(ib));
}

// FPRINT (refactor 3.7-e round 2; review R1-B2, J-47, R-STAMP-1): the summary's fingerprint moves with each of its inputs on
// its own — the grade policy, the disabled sets, the flight norms (through the rule data's stamp), the club baseline, the
// athlete's cohort facts, a swing's document, a swing added — and with nothing the rule data's stamp leaves out (a session's
// usuals, a shot's cohort). The rule versions are in it by construction (kSessionReductionVersion among them); the pack
// and norms are firingStamp's, which fire_findings_test moves one by one.
static void testFingerprintInputs()
{
    std::printf("\nFPRINT — the summary's fingerprint moves with each input on its own\n");
    const auto snap = RuleData::current();
    const analysis::FiringInputs in = firingInputsOf(*snap);
    const auto rulesOf = [](const analysis::FiringInputs &x) { return analysis::summaryRulesStamp(x); };
    const std::vector<analysis::SummarySwing> sw{ { QStringLiteral("swing_0001"), true, 100, 1000 },
                                                  { QStringLiteral("swing_0002"), true, 200, 2000 } };
    analysis::SummaryRules base;
    base.rules    = rulesOf(in);
    base.baseline = analysis::clubBaselineStamp(std::nullopt);
    base.cohort   = summaryCohortFacts(QString(), QString());
    const QString f0 = analysis::summaryFingerprintOf(base, sw);
    const auto with = [&](const std::function<void(analysis::SummaryRules &, std::vector<analysis::SummarySwing> &)> &move) {
        analysis::SummaryRules r = base;
        std::vector<analysis::SummarySwing> w = sw;
        move(r, w);
        return analysis::summaryFingerprintOf(r, w);
    };
    check(QStringLiteral("FPRINTa the same inputs give the same fingerprint"), with([](auto &, auto &) {}) == f0);
    analysis::FiringInputs policy = in;   policy.gradePolicy = in.gradePolicy + QStringLiteral("-other");
    analysis::FiringInputs disabled = in; disabled.disabledNormSets << QStringLiteral("a-set");
    analysis::FiringInputs flight = in;   flight.flightNorms.notes << QStringLiteral("a flight note");
    check(QStringLiteral("FPRINTb the grade policy alone moves it"), with([&](auto &r, auto &) { r.rules = rulesOf(policy); }) != f0);
    check(QStringLiteral("FPRINTc the disabled norm sets alone move it"), with([&](auto &r, auto &) { r.rules = rulesOf(disabled); }) != f0);
    check(QStringLiteral("FPRINTd the flight norms alone move it"), with([&](auto &r, auto &) { r.rules = rulesOf(flight); }) != f0);
    QHash<QString, analysis::ClubBaselineEntry> clubs;
    analysis::ClubBaselineEntry e;
    e.club = kClub; e.carryMean = 150; e.n = 12;
    clubs.insert(kClub, e);
    check(QStringLiteral("FPRINTe the frozen club baseline alone moves it"),
          with([&](auto &r, auto &) { r.baseline = analysis::clubBaselineStamp(clubs); }) != f0);
    check(QStringLiteral("FPRINTf the athlete's cohort facts alone move it"),
          with([](auto &r, auto &) { r.cohort = summaryCohortFacts(QStringLiteral("1971-11-16"), QStringLiteral("female")); }) != f0);
    check(QStringLiteral("FPRINTg a swing's document alone moves it"), with([](auto &, auto &w) { w[1].mtimeMs += 1; }) != f0);
    check(QStringLiteral("FPRINTh a swing added alone moves it"),
          with([](auto &, auto &w) { w.push_back({ QStringLiteral("swing_0003"), false, 0, 0 }); }) != f0);
    analysis::FiringInputs perSession = in;
    perSession.usuals.insert(kClub, analysis::ClubFlightUsual{});
    perSession.cohort = analysis::cohortFor(QDate(1971, 11, 16), QStringLiteral("female"), QDate(2026, 11, 21));
    check(QStringLiteral("FPRINTi the rule data's stamp leaves a session's usuals and a shot's cohort out"),
          rulesOf(perSession) == base.rules);
}

// LIVEBASE (refactor 3.7-e round 2; review R1-B1, J-49): a session started here whose earlier sessions are not summed up by
// this build (their summaries an older form) freezes its club baseline AT START, not at End: the catch-up sums up just those
// predecessors — even though it is paused for the live session — and the live owner freezes. Until then the panel's adapter
// says the baseline is waiting (the notice's state); after, it does not. With the panel up and with it off.
static void testLiveBaselineAtStart()
{
    for (const bool panelUp : { false, true }) {
        std::printf("\nLIVEBASE (panel %s) — the live session's club baseline at Start\n", panelUp ? "up" : "off");
        const QString tag = panelUp ? QStringLiteral(" (panel up)") : QStringLiteral(" (panel off)");
        Rig rig;
        check(QStringLiteral("LIVEBASE pre: an earlier session reduced by the old model (no summary of this build's)") + tag,
              earlierSession(rig));
        const QString earlier = rig.athleteFolder() + QStringLiteral("/2026-09-01_%1_Swing_01").arg(kToken);
        AthleteCatchUp catchUp(rig.callables());
        catchUp.setPaused(true);   // the live pause, already in place (WorkOnsController's, at Start)
        int settled = 0;
        QObject::connect(&catchUp, &AthleteCatchUp::liveBaselineSettled, &catchUp, [&settled](const SessionId &) { ++settled; });
        Session *s = rig.athlete->startSession(QStringLiteral("swing"), kPat);
        SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
        const QString folder = s && s->entry() ? s->entry()->folder : QString();
        if (!d) { check(QStringLiteral("LIVEBASE a live session with its owner") + tag, false); continue; }
        SessionDiagnosticsModel *panel = panelUp ? rig.adapters->of(s, QStringLiteral("panel")) : nullptr;
        if (panel) panel->addView();
        spin();
        check(QStringLiteral("LIVEBASEa at Start no baseline could be frozen, and it says so (waiting)") + tag,
              !d->holdsClubBaseline() && d->clubBaselineWaiting() && (!panel || panel->baselineWaiting()));
        const QString stampBefore = d->findingsStamp();
        // What main.cpp's attach hook asks a turn after Start.
        check(QStringLiteral("LIVEBASEb the catch-up queues just the predecessors it waits on") + tag,
              catchUp.catchUpForBaseline(kAthlete, s->id()));
        check(QStringLiteral("LIVEBASEc …and sums them up while paused") + tag, pump([&settled] { return settled == 1; }, 60000));
        Rig::settled(d);
        spin(200);
        const bool has7 = QJsonDocument(readJson(folder + QStringLiteral("/club_baseline.json"))).toJson().contains("7 IRON");
        check(QStringLiteral("LIVEBASEd the old-form predecessor is rewritten by its owner, fresh by the scan's rule") + tag,
              summaryFresh(earlier));
        check(QStringLiteral("LIVEBASEe the live session froze its baseline at Start — still in progress, before End") + tag,
              s->inProgress() && d->holdsClubBaseline() && has7 && d->findingsStamp() != stampBefore && !d->clubBaselineWaiting());
        check(QStringLiteral("LIVEBASEf no longer waiting on the panel's adapter; the predecessor it loaded let go") + tag,
              (!panel || !panel->baselineWaiting()) && catchUp.report().released == 1);
        if (panel) panel->dropView();
    }
}

// LIVEBASE g (refactor 3.7-f; J-49's residual, CARRY 3.7-e round 2): a session RESUMED while already loaded gets no attach,
// so the catch-up hears the athlete's resumes itself (AthleteCatchUp::followResumes — main.cpp installs it at every attach)
// and asks for the baseline's predecessors as the attach hook does at Start. The resume's event is raised here as
// Athlete::resumeSession raises it for a session it holds (the command itself is session_test's).
static void testLiveBaselineAtResume()
{
    std::printf("\nLIVEBASE g — a session resumed while loaded, its club baseline still waiting\n");
    Rig rig;
    check(QStringLiteral("LIVEBASEg pre: an earlier session reduced by the old model"), earlierSession(rig));
    AthleteCatchUp catchUp(rig.callables());
    catchUp.setPaused(true);   // the live pause
    int settled = 0;
    QObject::connect(&catchUp, &AthleteCatchUp::liveBaselineSettled, &catchUp, [&settled](const SessionId &) { ++settled; });
    catchUp.followResumes(rig.athlete.get());
    catchUp.followResumes(rig.athlete.get());   // once per athlete, however often asked
    Session *s = rig.athlete->startSession(QStringLiteral("swing"), kPat);
    SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
    if (!d) { check(QStringLiteral("LIVEBASEg a live session with its owner"), false); return; }
    spin();
    check(QStringLiteral("LIVEBASEg1 loaded, recorded here, its baseline waiting (no attach hook in this test)"),
          s->runningHere() && s->inProgress() && d->clubBaselineWaiting() && settled == 0);
    emit rig.athlete->sessionResumed(s->id());   // EVT-23 for a session already held: no attach
    check(QStringLiteral("LIVEBASEg2 the resume asks the catch-up once: its predecessors summed up while paused"),
          pump([&settled] { return settled >= 1; }, 60000) && settled == 1);
    Rig::settled(d);
    spin(200);
    check(QStringLiteral("LIVEBASEg3 the resumed session froze its baseline, still in progress"),
          s->inProgress() && d->holdsClubBaseline() && !d->clubBaselineWaiting());
}

// LIVEORDER (review R2-B1 of 3.7-e): the app's own order — the catch-up has an ordinary session IN HAND (its launch pass),
// then Start (its owner attaches and main.cpp's hook asks catchUpForBaseline), and only then does the live pause arrive
// (WorkOnsController's, queued). The pause puts the session in hand back behind the waited-on predecessors, and while paused
// those are taken wherever they stand: the live session's baseline freezes without End. With the panel up and off.
static void testLiveBaselineAppOrder()
{
    for (const bool panelUp : { false, true }) {
        std::printf("\nLIVEORDER (panel %s) — a session in hand, then Start, then the pause\n", panelUp ? "up" : "off");
        const QString tag = panelUp ? QStringLiteral(" (panel up)") : QStringLiteral(" (panel off)");
        Rig rig;
        check(QStringLiteral("LIVEORDER pre: an earlier-day session with no summary of this build's") + tag, earlierSession(rig));
        // An ordinary session of today, ended and let go, for the launch pass to have in hand.
        Session *today = endedSession(rig);
        const SessionId todayId = today ? today->id() : SessionId();
        if (today) rig.athlete->releaseSession(todayId);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        AthleteCatchUp catchUp(rig.callables());
        int settled = 0;
        QObject::connect(&catchUp, &AthleteCatchUp::liveBaselineSettled, &catchUp, [&settled](const SessionId &) { ++settled; });
        catchUp.runSessions(kAthlete, { todayId });   // the launch pass
        const bool inHand = catchUp.running() && catchUp.report().sessions == 1 && rig.athlete->loadedSession(todayId);
        Session *s = rig.athlete->startSession(QStringLiteral("swing"), kPat);   // Start
        SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
        if (!d) { check(QStringLiteral("LIVEORDER a live session with its owner") + tag, false); continue; }
        SessionDiagnosticsModel *panel = panelUp ? rig.adapters->of(s, QStringLiteral("panel")) : nullptr;
        if (panel) panel->addView();
        const bool waiting = d->clubBaselineWaiting();
        const bool asked = catchUp.catchUpForBaseline(kAthlete, s->id());   // main.cpp's attach hook, a turn after Start
        catchUp.setPaused(true);                                            // …then the live pause arrives
        check(QStringLiteral("LIVEORDERa a session in hand, Start waiting for its baseline, asked before the pause") + tag,
              inHand && waiting && asked);
        check(QStringLiteral("LIVEORDERb the waited-on predecessor is summed up while paused") + tag,
              pump([&settled] { return settled == 1; }, 60000));
        Rig::settled(d);
        spin(200);
        const QString folder = s->entry() ? s->entry()->folder : QString();
        const bool has7 = QJsonDocument(readJson(folder + QStringLiteral("/club_baseline.json"))).toJson().contains("7 IRON");
        check(QStringLiteral("LIVEORDERc the live session froze its baseline without End; nothing waits") + tag,
              s->inProgress() && d->holdsClubBaseline() && has7 && !d->clubBaselineWaiting() && (!panel || !panel->baselineWaiting()));
        catchUp.setPaused(false);   // End: the pass takes the session it let go again, and finishes
        check(QStringLiteral("LIVEORDERd after the pause lifts, the session let go is taken again and the pass finishes") + tag,
              pump([&catchUp] { return !catchUp.running(); }, 60000));
        if (panel) panel->dropView();
    }
}

// USE (refactor 3.7-e; CARRY 3.7-a "tie the use to the screens", 3.7-c R2-m2): an adapter's use of its Session lasts as
// long as a view is bound to it — so nothing holds a session for good once no screen shows it.
static void testUse()
{
    std::printf("\nUSE — an adapter holds its Session only while a screen shows it\n");
    Rig rig;
    Session *s = endedSession(rig);
    if (!s) { check(QStringLiteral("USE pre: an ended session"), false); return; }
    const int uses0 = s->uses();
    QPointer<SessionDiagnosticsModel> cards = rig.adapters->of(s, QStringLiteral("cards"));
    QPointer<SessionDiagnosticsModel> panel = rig.adapters->of(s, QStringLiteral("panel"));
    check(QStringLiteral("USEa made and not shown, the cards' and the panel's adapters hold no use"), cards && panel && s->uses() == uses0);
    cards->addView();
    panel->addView();
    panel->addView();   // a panel rebuilt on a mode switch binds its incoming view before the outgoing one goes
    panel->dropView();
    QString whyNot;
    check(QStringLiteral("USEb shown, one use each (a rebuild counts once), and the Session is not released under them"),
          s->uses() == uses0 + 2 && !rig.athlete->releaseSession(s->id(), &whyNot) && whyNot.contains(QStringLiteral("in use")));
    cards->dropView();
    panel->dropView();
    const SessionId id = s->id();
    check(QStringLiteral("USEc no screen shows it: no use held, and it can be released (the catch-up's, a sweep's)"),
          s->uses() == uses0 && rig.athlete->releaseSession(id, &whyNot));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(QStringLiteral("USEd its adapters went with it"), !cards && !panel && !rig.athlete->loadedSession(id));
}

// EVT13 (3.9's test, which waited for SessionDiagnostics to be live: refactor 3.7-e): an edit through RuleData's command
// publishes ONE EVT-13, and every LOADED session's owner re-fires every shot under the new snapshot.
static void testPackEditRefires()
{
    std::printf("\nEVT13 — a pack edit publishes EVT-13 and every loaded session re-fires\n");
    QTemporaryDir data;   // the edit's files land here, never in the user's own diagnostics folder
    RuleData rules(makeRuleDataRepository(data.path()));
    Rig rig(0, &rules);
    Session *a = endedSession(rig);
    Session *b = endedSession(rig);
    SessionDiagnostics *da = a ? a->diagnostics() : nullptr;
    SessionDiagnostics *db = b ? b->diagnostics() : nullptr;
    if (!da || !db) { check(QStringLiteral("EVT13 pre: two loaded sessions"), false); return; }
    const size_t asksA = da->asks().size(), asksB = db->asks().size();
    const QString stampA = da->findingsStamp(), stampB = db->findingsStamp();
    const quint64 writesA = da->counts().writes, writesB = db->counts().writes;
    int events = 0;
    QObject::connect(&rules, &RuleData::packChanged, &rules, [&events](const QString &) { ++events; });
    const auto first = rules.snapshot();
    analysis::CharacteristicPack user;
    user.id      = QStringLiteral("user");
    user.version = QStringLiteral("1");
    if (!first->pack->pack().conditions.empty()) {
        analysis::Condition c = first->pack->pack().conditions.front();
        c.goal = QStringLiteral("a goal written by diag_stage_panel_test");
        user.conditions.push_back(c);
    }
    QString why;
    check(QStringLiteral("EVT13a the edit is saved through RuleData's command and ONE EVT-13 is published"),
          rules.saveUserPack(user, &why) && events == 1);
    Rig::settled(da);
    Rig::settled(db);
    const auto fires = [](SessionDiagnostics *d, size_t from) {
        int n = 0;
        for (size_t i = from; i < d->asks().size(); ++i) n += d->asks()[i].derive ? 0 : 1;
        return n;
    };
    std::printf("    fires after the edit: %d and %d (of %d shots each)\n", fires(da, asksA), fires(db, asksB), kShots);
    check(QStringLiteral("EVT13b every loaded session re-fired every shot, under a new findings stamp"),
          fires(da, asksA) >= kShots && fires(db, asksB) >= kShots && da->findingsStamp() != stampA && db->findingsStamp() != stampB);
    check(QStringLiteral("EVT13c …and each wrote what it kept"), da->counts().writes > writesA && db->counts().writes > writesB);
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

// ARCHIVEj (review R3-M2 of 3.7-a, for 3.7-e): the shot a command reaches while the archive is out is judged again after a
// made archive from its STUB (the archive thinned its document). Its own findings rows must equal the rows it fired from its
// full document before the archive (ARCHIVEh4 checked only the other shots).
static void testArchiveStub()
{
    std::printf("\nARCHIVEj — the edited shot judged from its stub fires the rows it fired from its full document\n");
    Rig rig(300);
    rig.store.setArchiveJobs(&SessionArchiveRun::archiveInOwnRun);
    Session *s = endedSession(rig);
    SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
    const QString folder = s && s->entry() ? s->entry()->folder : QString();
    if (!d || folder.isEmpty()) { check(QStringLiteral("ARCHIVEj pre: an ended session judged from its full documents"), false); return; }
    Rig::settled(d);
    const QString file = folder + QStringLiteral("/diagnostics.json");
    const auto rowsOfShot = [&file](int id) {
        analysis::LedgerOptions opt;
        for (const analysis::ShotRecord &r : analysis::fromJson(readJson(file).value(QStringLiteral("ledger")).toObject(), &opt))
            if (r.shotId == id) return r;
        return analysis::ShotRecord{};
    };
    const int two = s->shots().value(1) ? s->shots().value(1)->swingNumber() : 2;
    const analysis::ShotRecord before = rowsOfShot(two);
    QTemporaryDir archiveRoot;
    const SessionArchiveJob job = s->archiveJob(SessionArchiveOptions{ archiveRoot.path(), true });
    if (!job) { check(QStringLiteral("ARCHIVEj the archive job is out"), false); return; }
    SessionArchiveResult r;
    std::atomic_bool done{ false };
    std::thread worker([&] { r = job.run({}); done = true; });
    pump([&] { return done.load(); }, 120000);
    worker.join();
    Shot *shot = s->shot(ShotRef(s->id(), two));
    const WriteResult w = shot ? shot->setShotIntent(QStringLiteral("draw")) : WriteResult{};
    const quint64 fires = d->asks().size();
    s->archiveLanded(r);
    pump([&] { return d->idle() && !d->writePending() && d->asks().size() > fires; }, 30000);
    spin(500);
    const analysis::ShotRecord after = rowsOfShot(two);
    int same = 0, differ = 0;
    QStringList which;
    for (const analysis::ConditionRow &b : before.rows) {
        const auto it = std::find_if(after.rows.begin(), after.rows.end(),
                                     [&b](const analysis::ConditionRow &a) { return a.conditionId == b.conditionId; });
        const bool eq = it != after.rows.end() && it->state == b.state
                     && (std::isnan(it->value) ? std::isnan(b.value) : it->value == b.value);
        eq ? ++same : ++differ;
        if (!eq && which.size() < 6) which << b.conditionId;
    }
    std::printf("    shot %d after a made archive, judged from its stub: %d rows as before, %d differ (%s)\n", two, same, differ,
                qPrintable(which.join(QStringLiteral(", "))));
    check(QStringLiteral("ARCHIVEj its own rows from the stub equal those from its full document (%1 rows)").arg(before.rows.size()),
          r.ok && w.ok() && !before.rows.empty() && differ == 0 && after.rows.size() == before.rows.size());
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

// UNASSIGNED (refactor step 5.1; information_model.md §5.11; DEC-198; J-67): a capture with no golfer chosen is a full
// Session of the Unassigned owner — its session.json names the fixed id, its shots are judged like any golfer's (no norm
// cohort: no sex or date of birth), and it freezes no club baseline (R-UNASSIGNED-5): none is written, and none waits.
static void testUnassigned()
{
    std::printf("\nUNASSIGNED — a capture with no golfer: the Unassigned owner's full Session, judged, no club baseline\n");
    Rig rig;
    // The rig's athlete made the Unassigned owner, on the app's doors (as AthleteStore::unassigned hands it over).
    const QString folder = rig.dir.path() + QStringLiteral("/(unassigned)");
    QDir().mkpath(folder);
    rig.athlete = std::make_unique<Athlete>(nullptr, &rig.store, unassignedAthleteId());
    AthleteRecord r;
    r.name = QStringLiteral("Unassigned");
    rig.athlete->setStored(r, folder, SessionOwner{ unassignedAthleteId(), QStringLiteral("(unassigned)"), rig.dir.path() },
                           Athlete::Source::Library, false);
    rig.athlete->setShotDoors({ rig.recordings.get(), rig.reader });
    rig.athlete->setDiagnosticsDoors(appDiagnosticsDoors(rig.pool.get(), rig.receiver.get(), RuleData::installed(),
                                                         rig.store, *rig.recordings));
    Session *s = endedSession(rig);
    SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
    const QString dir = s && s->entry() ? s->entry()->folder : QString();
    check(QStringLiteral("UNASSa its session is in (unassigned)/ and its file names the fixed id"),
          !dir.isEmpty() && QDir::cleanPath(dir + QStringLiteral("/..")) == QDir::cleanPath(folder)
              && fileBytes(dir + QStringLiteral("/session.json")).contains(unassignedAthleteId().toString().toUtf8()));
    check(QStringLiteral("UNASSb its shots are judged like any golfer's (the owner's diagnosis, its diagnostics.json)"),
          d && int(d->keptShots().size()) == kShots && QFile::exists(dir + QStringLiteral("/diagnostics.json")));
    const ClubBaselineComputation c = AthleteDiagnostics::of(rig.athlete.get())->clubBaselineFor(s ? s->id() : SessionId());
    check(QStringLiteral("UNASSc no club baseline: none frozen, none computable, none waited on (no notice)"),
          !QFile::exists(dir + QStringLiteral("/club_baseline.json")) && !c.computable && c.waitingOn.isEmpty()
              && d && !d->clubBaselineWaiting() && !d->holdsClubBaseline());
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
          s->uses() == usesBefore + 1);   // the report's view (the panel adapter asked for in REPORTa has none: 3.7-e)
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
    check(QStringLiteral("REPORTh let go, its use of the Session is dropped"), !m && s->uses() == usesBefore);
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

// PIPS (refactor 3.7-c): the carousel's pip rows read the Session's owner through the adapter a diagnostics panel SHOWS
// for the carousel's session — diagnosticsAdapters.shownFor(app.sessionAt(folder), folder) — which never makes one, so a
// card adds no view work of its own: with no panel up there is nothing to read (the pips off, as before).
static int firedInOwnerRow(const SessionDiagnostics *d, int swing)
{
    const auto in = d ? d->diagnosisInput() : nullptr;
    if (!in) return -1;
    for (const analysis::ShotRecord &r : in->shots)
        if (r.shotId == swing) {
            int n = 0;
            for (const analysis::ConditionRow &row : r.rows)
                if (row.state == analysis::ShotState::Fired) ++n;
            return n;
        }
    return -1;
}

static void testPips()
{
    std::printf("\nPIPS — the carousel's pips read the adapter a panel shows (3.7-c)\n");
    Rig rig;
    rig.adapters->setAthleteFolder([&rig] { return rig.athleteFolder(); });
    Session *s = rig.athlete->startSession(QStringLiteral("swing"), kPat);
    SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
    const QString folder = s && s->entry() ? s->entry()->folder : QString();
    if (!d || folder.isEmpty()) { check(QStringLiteral("PIPS a live session with its owner"), false); return; }
    const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
    QList<Shot *> shots;
    for (int i = 1; i <= kShots; ++i) {
        if (Shot *shot = liveShot(rig, s, t0 + i * 45000)) shots << shot;
        if (i >= 2) shots.value(i - 2)->completeCapture(fixtureReading(shots.value(i - 2)->swingNumber()));
        Rig::settled(d);
    }
    int told = 0;
    QObject::connect(rig.adapters.get(), &SessionDiagnosticsAdapters::shownChanged, rig.adapters.get(), [&told] { ++told; });
    const auto adaptersOf = [s] { return s->findChildren<SessionDiagnosticsModel *>(Qt::FindDirectChildrenOnly).size(); };

    check(QStringLiteral("PIPSa no panel up: the cards find nothing to read (no pips, as with the panel off today)"),
          rig.adapters->shownFor(s, folder) == nullptr && rig.adapters->shownCount() == 0);
    check(QStringLiteral("PIPSb ...and asking made no adapter (no view work for a session no panel shows)"), adaptersOf() == 0);

    SessionDiagnosticsModel *panel = rig.adapters->of(s, QStringLiteral("panel"), folder);
    check(QStringLiteral("PIPSc a panel's adapter with no view bound shows nothing either"),
          panel && rig.adapters->shownFor(s, folder) == nullptr && told == 0);
    if (!panel) return;
    panel->addView();   // PpSessionDiagnosticsPanel._bindView
    check(QStringLiteral("PIPSd the panel binds: the cards are told once, and find its adapter"),
          told == 1 && rig.adapters->shownCount() == 1 && rig.adapters->shownFor(s, folder) == panel);

    // Each card's pips through it are the owner's: its shot found by its folder (the carousel row's swingDir), its fired
    // count the owner's reduced row's.
    bool same = true;
    int withPips = 0;
    for (Shot *shot : std::as_const(shots)) {
        const int id = panel->shotIdForSwingDir(shot->folder());
        same = same && id == shot->swingNumber() && panel->firedCountFor(id) == firedInOwnerRow(d, id);
        if (!panel->pipsFor(id).isEmpty()) ++withPips;
    }
    std::printf("    %d of %d cards with pips\n", withPips, int(shots.size()));
    check(QStringLiteral("PIPSe every card's pips are the owner's (its shot by folder; its fired count the owner's row's)"),
          same && withPips == int(shots.size()));

    // A mode switch rebuilds the stage panel: the incoming panel binds before the outgoing one goes.
    const int beforeSwitch = told;
    panel->addView();
    panel->dropView();
    check(QStringLiteral("PIPSf a panel rebuilt on a mode switch tells the cards nothing (no work per switch)"),
          told == beforeSwitch && rig.adapters->shownFor(s, folder) == panel);

    SessionDiagnosticsModel *cast = rig.adapters->of(s, QStringLiteral("cast"), folder);
    cast->addView();
    check(QStringLiteral("PIPSg the stage panel and the wall cast both up: the cards read the stage panel's"),
          rig.adapters->shownFor(s, folder) == panel && rig.adapters->shownCount() == 2);
    panel->dropView();
    check(QStringLiteral("PIPSh the stage panel closed, the cast still up: the cast's (it shows the same owner)"),
          rig.adapters->shownFor(s, folder) == cast && cast->firedCountFor(shots.last()->swingNumber())
                                                           == firedInOwnerRow(d, shots.last()->swingNumber()));
    cast->dropView();
    const int beforeOff = told;
    check(QStringLiteral("PIPSi both closed: nothing to read, and the cards were told"),
          rig.adapters->shownFor(s, folder) == nullptr && rig.adapters->shownCount() == 0 && beforeOff > beforeSwitch);

    // NO PANEL (review B1; BRIEF.md §3.1 "the Session, always there"): the session's CARDS adapter, which takes only what
    // the pips read from each diagnosis — no view half — and is never answered as a panel.
    SessionDiagnosticsModel *cards = rig.adapters->of(s, QStringLiteral("cards"), folder);
    cards->addView();   // PpShotCarousel._bindCardsView
    bool cardsSame = cards && cards->isAdapter();
    int cardsWithPips = 0;
    for (Shot *shot : std::as_const(shots)) {
        const int id = cards->shotIdForSwingDir(shot->folder());
        cardsSame = cardsSame && id == shot->swingNumber() && cards->firedCountFor(id) == firedInOwnerRow(d, id);
        if (!cards->pipsFor(id).isEmpty()) ++cardsWithPips;
    }
    check(QStringLiteral("PIPSp with no panel up the cards' adapter gives every shot its pips, the owner's"),
          cardsSame && cardsWithPips == int(shots.size()));
    check(QStringLiteral("PIPSq ...with no view half (no cards, no watching list built) and never answered as a panel"),
          cards->cards().isEmpty() && cards->watching().isEmpty() && rig.adapters->shownFor(s, folder) == nullptr
              && rig.adapters->shownCount() == 0);
    check(QStringLiteral("PIPSr anything judges this session here (no N1 warning); a no-golfer folder: nothing does"),
          rig.adapters->judgedHere(s, folder)
              && !rig.adapters->judgedHere(nullptr, rig.dir.path() + QStringLiteral("/unknown/2026-11-21_unknown_Swing_01")));
    {   // a new shot with only the cards bound: its pips come with the owner's diagnosis
        Shot *next = liveShot(rig, s, t0 + (kShots + 1) * 45000);
        Rig::settled(d);
        spin();
        const int id = next ? cards->shotIdForSwingDir(next->folder()) : -1;
        check(QStringLiteral("PIPSs a new shot with the panel off: its pips arrive with the owner's diagnosis"),
              next && id == next->swingNumber() && !cards->pipsFor(id).isEmpty()
                  && cards->firedCountFor(id) == firedInOwnerRow(d, id));
        if (next) shots << next;
    }
    cards->dropView();

    // A shown adapter that goes (review M3): the cards are told, and nothing stale is answered.
    {
        cast->addView();
        const int beforeGone = told;
        delete cast;
        cast = nullptr;
        spin();
        check(QStringLiteral("PIPSt a shown adapter destroyed: the cards are told, and it is no longer answered"),
              told > beforeGone && rig.adapters->shownCount() == 0 && rig.adapters->shownFor(s, folder) == nullptr);
    }

    // A new live shot with the panel up: its pips come with the owner's diagnosis; the cards' reads made nothing.
    panel->addView();
    Shot *fresh = liveShot(rig, s, t0 + (kShots + 2) * 45000);
    Rig::settled(d);
    spin();
    const int freshId = fresh ? panel->shotIdForSwingDir(fresh->folder()) : -1;
    check(QStringLiteral("PIPSj a new shot's pips come with the owner's diagnosis, and no adapter was made for the cards"),
          fresh && freshId == fresh->swingNumber() && panel->firedCountFor(freshId) == firedInOwnerRow(d, freshId)
              && !panel->pipsFor(freshId).isEmpty() && adaptersOf() == 2);   // the panel's and the cards'; the cast went
    panel->dropView();

    // UNHELD (J-a3): the same session under the golfer's folder, its file naming another athlete id: today's engine model,
    // shown by a panel, is what the cards read — for its own folder only.
    check(QStringLiteral("PIPSk the session ended"), s->end().ok());
    Rig::settled(d);
    const QString foreign = rig.athleteFolder() + QStringLiteral("/2026-09-03_") + kToken + QStringLiteral("_Swing_01");
    check(QStringLiteral("PIPSl a copy placed in the golfer's folder"), copyTree(folder, foreign));
    QJsonObject file = readJson(foreign + QStringLiteral("/session.json"));
    file[QStringLiteral("athleteId")] = QStringLiteral("b389a703-4c75-4912-8800-828ddc74fbcc");
    file[QStringLiteral("sessionId")] = QStringLiteral("0b0e5f5e-9c1a-4f1e-9d55-3a4c1f2e7b11");
    writeJson(foreign + QStringLiteral("/session.json"), file);
    QFile::remove(foreign + QStringLiteral("/diagnostics.json"));
    SessionDiagnosticsModel *unheld = rig.adapters->of(nullptr, QStringLiteral("panel"), foreign);
    if (!unheld || unheld->isAdapter()) { check(QStringLiteral("PIPS an engine-mode model for the unheld folder"), false); return; }
    unheld->setSynchronous(true);
    unheld->setBaselineAuthorityName(QStringLiteral("never"));
    unheld->addView();
    const int beforePoint = told;
    unheld->activateSession(foreign);
    check(QStringLiteral("PIPSm a panel's engine model on an unheld folder: the cards read it, and were told when it moved"),
          rig.adapters->shownFor(nullptr, foreign) == unheld && told > beforePoint && unheld->shotCount() == kShots + 2);
    check(QStringLiteral("PIPSn ...for that folder only (another folder finds nothing)"),
          rig.adapters->shownFor(nullptr, folder) == nullptr);
    unheld->activateSession(QString());
    unheld->dropView();

    // NOGOLF (N1): an empty adapter shown by a panel has nothing to read.
    SessionDiagnosticsModel *empty = rig.adapters->of(nullptr, QStringLiteral("panel"),
                                                      rig.dir.path() + QStringLiteral("/unknown/2026-11-21_unknown_Swing_01"));
    empty->addView();
    check(QStringLiteral("PIPSo a session with no golfer: nothing to read, so no pips (its panel says why)"),
          rig.adapters->shownFor(nullptr, rig.dir.path() + QStringLiteral("/unknown/2026-11-21_unknown_Swing_01")) == nullptr);
    empty->dropView();
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
    testCatchUpVersionBump();
    testCatchUpTrash();
    testFingerprintInputs();
    testLiveBaselineAtStart();
    testLiveBaselineAtResume();
    testLiveBaselineAppOrder();
    testUse();
    testPackEditRefires();
    testArchive();
    testArchiveEvents();
    testArchiveStub();
    testNoGolfer();
    testUnassigned();
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
    testPips();

    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
