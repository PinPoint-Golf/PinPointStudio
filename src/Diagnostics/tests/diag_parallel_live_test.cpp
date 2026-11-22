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

// diag_parallel_live_test — THE PARALLEL RUN AS THE APP WIRES IT, LIVE (refactor step 3.7-0, the D22 fix;
// build/refactor/step-3/3.7/D22-DIFF-NOTES.md §4).
//
// D22 failed because the app's parallel run read every live shot as an empty grid: its firing reader was built with
// writeSidecars=false, which also meant "never parse", and live no sidecar exists until the old model's End sweep. No
// test built the reader the way the app does, and the stored-session parity had sidecars already. This test uses the
// parallel run's doors (pinpoint::parallel::doors, which main.cpp called until 3.10) on a temporary library, in a live-shaped order:
//
//   PRE   an earlier session (a legacy folder, day 2026-09-01, twelve 7-iron shots) reduced by the old model, so the
//         live session's club baseline has a usual to freeze — the old model freezes club_baseline.json at End, after
//         the new owner loaded (D22-DIFF-NOTES §2, "L2")
//   LIVE  a session started; for each of ten shots: the swing allocated, its document written (a .ppsw, as capture
//         writes it) WITHOUT its launch-monitor reading and with no sidecar, EVT-01; then the PREVIOUS shot's late
//         reading folded in (EVT-22, the document rewritten — so any sidecar would now be stale), as live; the last shot's reading after the loop
//     a   the new owner kept every shot WITH its launch-monitor reading, the club the document declares and assessable
//         rows (on HEAD's wiring: none of these — every shot fired on an empty grid)
//     b   the new owner wrote NO file: no phase-grid or summary sidecar, no diagnostics.json, work_ons.json or
//         club_baseline.json; and every shot fired again changes nothing on disk (the whole tree, size and mtime)
//   END   the session ended; the old model run as the catch-up ran it at End until 3.7-e (synchronous, baseline
//         authority "always", reviewing): it writes the sidecars and freezes club_baseline.json; the new owner reloads its
//         club baseline (as the app's comparison hook did until 3.10 deleted it) and the two are compared
//     c   the frozen baseline holds the 7 iron's usual, and the new owner read it (its stamp moved)
//     d   parallel::differences(old form, new form) is EMPTY. No expected class applies here: the session is live
//         when both engines last see it (case 9's stage → closing is a stored-session class), the words are not in
//         the form (J-33's "pelvis rotation" is not compared), and both read the same frozen baseline (Fix 2)
//     e   the cost of one comparison is printed: a figure only, guarding nothing since the hook went (3.10)
//   (HOOK, CRASH and GONE tested the app's comparison hook; refactor 3.10 deleted the hook with PINPOINT_DIAG_PARALLEL,
//   since 3.7-e it compared nothing in the app, and those three tests with it.)
//   (F5) every live document is written as capture leaves it: a .ppsw with its summary block
//   RDR   the firing reader itself: the parallel reader (writeSidecars=false → SidecarPolicy::ParseNoWrite) reads a
//         document with no sidecar and one with a stale sidecar as the default reader does, and writes nothing
//
//   cmake --build build/tests --target diag_parallel_live_test -j 6
//   ctest --test-dir build/tests -R '^diag_parallel_live_test$' --output-on-failure

#include "../../DomainModel/athlete.h"
#include "../../DomainModel/session.h"
#include "../../DomainModel/session_diagnostics.h"
#include "../../DomainModel/shot.h"
#include "../../Core/club_vocabulary.h"
#include "../../Core/work_pool.h"
#include "../../Export/firing_reader_store.h"
#include "../../Export/recording_loader.h"
#include "../../Export/rule_data_install.h"
#include "../../Export/session_store.h"
#include "../../Export/swing_store.h"
#include "../../Export/swing_store_repository.h"
#include "../../Gui/diagnostics/diag_parallel_run.h"
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
#include <QMap>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>
#include <functional>
#include <memory>

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
static constexpr int   kShots   = 10;

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

// The fixture as capture writes it BEFORE the launch monitor's reading lands: no `launchMonitor` block and none of its
// lm.* series in the analysis (lm_7iron is a monitor-only swing, so its analysis keeps its phases alone), its swing
// number and capture instant its own.
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

// THE DOCUMENT AS CAPTURE LEAVES IT (review F5): a swing.ppsw with its summary block, never a JSON-era swing.json.
// Written as JSON, then rewritten once through the swing door (its club edit → saveDocument, the write every
// capture write ends in), which supersedes the swing.json; no sidecar is left. Before EVT-01, so no
// reader has seen the JSON form.
static bool writeCaptureDoc(RecordingRepository &door, const Shot *shot, const QJsonObject &root)
{
    const QString swingDir = shot->folder();
    if (!writeJson(QDir(swingDir).filePath(QStringLiteral("swing.json")), root)) return false;
    ReviewEdit club;
    club.club = kClub;
    const WriteResult w = door.editReview(shot->ref(), club);   // the swing door, as every writer goes
    return w.ok() && SwingStore::info(swingDir).format == SwingStore::Format::Ppsw
        && !QFile::exists(QDir(swingDir).filePath(QStringLiteral("swing.json")))
        && !QFile::exists(analysis::phaseGridPath(swingDir));
}

// The fixture's own reading, as a connector hands it over (every field the block holds, through the one field table).
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

// Every file under `root`, by its path under it: size and mtime.
using Tree = QMap<QString, QPair<qint64, qint64>>;
static Tree treeOf(const QString &root)
{
    Tree t;
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        t.insert(QDir(root).relativeFilePath(fi.filePath()), { fi.size(), fi.lastModified().toMSecsSinceEpoch() });
    }
    return t;
}
static QStringList cacheFilesIn(const Tree &t)
{
    static const QStringList caches{ QStringLiteral("swing_phasegrid.json"), QStringLiteral("swing_summary.json"),
                                     QStringLiteral("diagnostics.json"),     QStringLiteral("work_ons.json"),
                                     QStringLiteral("club_baseline.json") };
    QStringList out;
    for (auto it = t.begin(); it != t.end(); ++it)
        if (caches.contains(QFileInfo(it.key()).fileName())) out << it.key();
    return out;
}

// ── One scratch library with the app's parallel-run doors ────────────────────────────────────────────────────
struct Rig {
    QTemporaryDir                                  dir;
    SessionStore                                   store;
    std::unique_ptr<SwingStoreRecordingRepository> recordings;
    std::shared_ptr<RecordingReader>               reader;
    std::unique_ptr<WorkReceiver>                  receiver;   // made before the pool, destroyed after it
    std::unique_ptr<WorkPool>                      pool;
    std::unique_ptr<Athlete>                       athlete;    // destroyed first

    Rig()
    {
        store.setLockWaitForTest(200);
        store.setOsTrashForTest(false);
        recordings = std::make_unique<SwingStoreRecordingRepository>(store);
        reader     = std::make_shared<SwingStoreRecordingReader>(store);
        receiver   = std::make_unique<WorkReceiver>([this](const WorkAddress &a) {
            return diagnosticsWorkTarget(athlete ? athlete->loadedSession(SessionId::fromString(a.owner)) : nullptr, a);
        });
        pool = std::make_unique<WorkPool>();   // the app's (main.cpp: the default size)
        QDir().mkpath(athleteFolder());
        athlete = std::make_unique<Athlete>(nullptr, &store, kAthlete);
        AthleteRecord r;
        r.name = kName;
        athlete->setStored(r, athleteFolder(), SessionOwner{ kAthlete, kName, dir.path() }, Athlete::Source::Library, false);
        athlete->setShotDoors({ recordings.get(), reader });
        // THE PARALLEL RUN'S DOORS, from the one function main.cpp called until 3.10 (in memory only).
        athlete->setDiagnosticsDoors(parallel::doors(pool.get(), receiver.get(), RuleData::installed(), store, *recordings));
    }
    ~Rig()
    {
        athlete.reset();
        pool.reset();
    }
    QString athleteFolder() const { return dir.path() + QLatin1Char('/') + kToken; }
    static bool settled(SessionDiagnostics *d) { return d && pump([d] { return d->idle(); }); }
};

// PRE: an earlier, legacy session reduced by the old model (as a past session's panel or catch-up left it).
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

static void testLive()
{
    std::printf("\nLIVE — the app's parallel-run doors over a live-shaped session\n");
    Rig rig;
    check(QStringLiteral("PRE an earlier session of twelve 7-iron shots, reduced by the old model"), earlierSession(rig));

    Session *s = rig.athlete->startSession(QStringLiteral("swing"), kPat);
    SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
    check(QStringLiteral("the live session has its new owner (the app's doors attach one)"), d != nullptr);
    if (!d) return;
    const QString folder = s->entry() ? s->entry()->folder : QString();
    const qint64 t0 = QDateTime::currentMSecsSinceEpoch();

    // LIVE: EVT-01 of shot i, then the late reading of shot i-1 (EVT-22), as the cabin's order goes.
    QList<Shot *> shots;
    for (int i = 1; i <= kShots + 1; ++i) {
        if (i <= kShots) {
            Shot *shot = s->addShot();
            if (!shot) break;
            if (!writeCaptureDoc(*rig.recordings, shot, withoutReading(shot->swingNumber(), t0 + i * 45000)))
                std::printf("    shot %d: the capture-shaped .ppsw did not write\n", shot->swingNumber());
            s->shotDocumentWritten(shot->ref());   // EVT-01
            shots << shot;
        }
        if (i >= 2) {
            Shot *late = shots.value(i - 2);
            const WriteResult w = late ? late->completeCapture(fixtureReading(late->swingNumber())) : WriteResult{};
            if (!w.ok()) std::printf("    the late reading of shot %d did not save: %s\n", i - 1, qPrintable(w.error));
        }
        Rig::settled(d);
    }
    check(QStringLiteral("every shot was captured and settled"), shots.size() == kShots && Rig::settled(d));

    // a: what the new owner kept.
    int withLm = 0, withClub = 0, assessable = 0;
    for (Shot *shot : std::as_const(shots)) {
        const KeptShot *k = d->kept(shot->swingNumber());
        if (!k) continue;
        if (k->hasLaunchMonitor) ++withLm;
        if (canonicalClubId(k->findings.club) == canonicalClubId(kClub)) ++withClub;
        for (const analysis::ConditionRow &row : k->findings.rows)
            if (row.state != analysis::ShotState::NotAssessable) { ++assessable; break; }
    }
    std::printf("    kept %lld; with the launch monitor %d, the 7 iron %d, an assessable row %d (of %d)\n",
                (long long)d->keptShots().size(), withLm, withClub, assessable, kShots);
    check(QStringLiteral("a1 every shot kept WITH its launch-monitor reading (HEAD's wiring: none)"), withLm == kShots);
    check(QStringLiteral("a2 every shot kept with the club its document declares (HEAD's wiring: \"\")"), withClub == kShots);
    check(QStringLiteral("a3 every shot has an assessable row (HEAD's wiring: every row not assessable)"), assessable == kShots);

    // b: the new owner wrote nothing.
    const Tree live = treeOf(folder);
    const QStringList caches = cacheFilesIn(live);
    if (!caches.isEmpty()) std::printf("    cache files present: %s\n", qPrintable(caches.join(QStringLiteral(", "))));
    check(QStringLiteral("b1 no sidecar, diagnostics.json, work_ons.json or club_baseline.json in the session"), caches.isEmpty());
    d->fireAll();
    check(QStringLiteral("b2 every shot fired again settles"), Rig::settled(d));
    check(QStringLiteral("b3 ...and leaves the session's files exactly as they were (every path, size and mtime)"),
          treeOf(folder) == live);

    QElapsedTimer clock;   // e: one comparison's cost, printed below (a figure only; the hook it was paid by is gone)

    // END.
    const WriteResult ended = s->end();
    check(QStringLiteral("the session ends (EVT-09) and is not in progress"), ended.ok() && !s->inProgress());
    Rig::settled(d);

    const QString stampBefore = d->findingsStamp();
    SessionDiagnosticsModel old;   // the old engine as the catch-up ran it at End until 3.7-e (run P's reference)
    old.setSynchronous(true);
    old.setBaselineAuthority(SessionDiagnosticsModel::BaselineAuthority::Always);
    old.setReviewing(true);
    old.activateSession(folder);
    check(QStringLiteral("the old model graded every shot"), old.shotCount() == kShots);
    // LIKE WITH LIKE: the new owner re-reads the baseline the old model just froze, as the app's comparison hook did
    // before 3.10 deleted it (the hook compared nothing in the app since 3.7-e; CARRY R1-m9).
    d->reloadClubBaseline(/*keepWhenAbsent=*/true);
    check(QStringLiteral("the new owner's diagnosis of the same shots lands"), pump([d] { return d->idle() && d->diagnosis(); }));

    // c: the baseline.
    const QJsonObject baseline = readJson(folder + QStringLiteral("/club_baseline.json"));
    const bool has7 = QJsonDocument(baseline).toJson().contains("7 IRON");
    check(QStringLiteral("c1 the old model froze club_baseline.json with the 7 iron's usual"), has7);
    check(QStringLiteral("c2 the new owner read it (its findings stamp moved)"), d->findingsStamp() != stampBefore);

    // d: the comparison.
    clock.start();
    const QJsonObject oldForm = parallel::formOf(old, false), newForm = parallel::formOf(*d, false);
    const QStringList diffs = parallel::differences(oldForm, newForm);
    const double compareMs = clock.nsecsElapsed() / 1.0e6;
    for (int i = 0; i < std::min<int>(diffs.size(), 12); ++i) std::printf("    %s\n", qPrintable(diffs.at(i)));
    std::printf("    %lld difference(s)\n", (long long)diffs.size());
    check(QStringLiteral("d1 the old model and the new owner agree: no difference (none expected live)"), diffs.isEmpty());

    // e: a figure only (refactor 3.10, review m9): what one comparison costs; it guards nothing since the hook went.
    std::printf("    e: one comparison of this %d-shot session (both forms + every difference): %.2f ms\n", kShots, compareMs);

    old.activateSession(QString());
}

// RDR: the firing reader with the parallel run's policy, against the default.
static void testReader()
{
    std::printf("\nRDR — the parallel run's firing reader parses and writes nothing\n");
    Rig rig;
    Session *s = rig.athlete->startSession(QStringLiteral("swing"), kPat);
    Shot *shot = s ? s->addShot() : nullptr;
    if (!shot) { check(QStringLiteral("a shot allocated"), false); return; }
    const QString sw = shot->folder();
    QFile::copy(fixture(), sw + QStringLiteral("/swing.json"));
    QFile(sw + QStringLiteral("/swing.json")).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const std::shared_ptr<const FiringReader> parallelReader =
        parallel::doors(nullptr, nullptr, nullptr, rig.store, *rig.recordings).reader;
    const SwingStoreFiringReader indexing(rig.store, *rig.recordings);
    const auto *asStore = dynamic_cast<const SwingStoreFiringReader *>(parallelReader.get());
    check(QStringLiteral("R0 the app's parallel reader is ParseNoWrite"), asStore && asStore->policy() == SidecarPolicy::ParseNoWrite);
    const auto &pack = RuleData::installed()->snapshot()->pack->pack();

    const FiringRead a = parallelReader->read(shot->ref(), pack);
    check(QStringLiteral("R1 no sidecar: the parallel reader parses the document (a grid, the club, the monitor)"),
          a.ok && !a.readings.grid.isEmpty() && canonicalClubId(a.readings.grid.club) == canonicalClubId(kClub));
    check(QStringLiteral("R2 ...and writes no sidecar"),
          !QFile::exists(analysis::phaseGridPath(sw)) && !QFile::exists(sw + QStringLiteral("/swing_summary.json")));
    const FiringRead b = indexing.read(shot->ref(), pack);
    check(QStringLiteral("R3 the default reader reads the same grid and indexes it"),
          b.ok && QJsonDocument(analysis::savePhaseGrid(b.readings.grid, 0, 0)).toJson()
                      == QJsonDocument(analysis::savePhaseGrid(a.readings.grid, 0, 0)).toJson()
               && QFile::exists(analysis::phaseGridPath(sw)));

    // A stale sidecar: the document rewritten (the late reading's fold) after the sidecar was written.
    QJsonObject root = readJson(sw + QStringLiteral("/swing.json"));
    root[QStringLiteral("review")] = QJsonObject{ { QStringLiteral("club"), kClub }, { QStringLiteral("note"), QStringLiteral("rewritten") },
                                                  { QStringLiteral("rating"), 0 } };
    QThread::msleep(20);
    writeJson(sw + QStringLiteral("/swing.json"), root);
    const Tree before = treeOf(sw);
    const FiringRead c = parallelReader->read(shot->ref(), pack);
    check(QStringLiteral("R4 a stale sidecar: the parallel reader parses the rewritten document (HEAD's: an empty grid)"),
          c.ok && !c.readings.grid.isEmpty() && canonicalClubId(c.readings.grid.club) == canonicalClubId(kClub));
    check(QStringLiteral("R5 ...and leaves the stale sidecar and every file as they were"), treeOf(sw) == before);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    std::printf("diag_parallel_live_test\n");
    RuleData &rules = installProcessRuleData();
    check(QStringLiteral("the shipped pack is read"), rules.snapshot() && rules.snapshot()->pack);
    if (!rules.snapshot() || !rules.snapshot()->pack) return 1;

    testReader();
    testLive();

    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
