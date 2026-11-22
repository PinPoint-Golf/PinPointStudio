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

// THE ATHLETE LEVEL'S OWNER AND THE CATCH-UP (refactor step 3.6; build/refactor/step-3/BRIEF.md §4.6, §6 row 3.6;
// docs/developer/information_model.md CLS-ATHLETE-DIAG, R-DER-3, OBJ-CLUB-BASELINE; DEC-46, DEC-60, DEC-172, DEC-181).
// Over the REAL session store, Athlete, Session, swing door, firing reader and SessionDiagnostics, the process's RuleData
// over the shipped content, on a temporary library (every trash into its own .pinpoint-trash). Every expectation comes
// from a stated contract, never from running the code.
//
//   PRA   the praise rule (04 step 3 item 5; D23: DEC-181): praised only when graded Repeatable or Grooved; ungraded,
//         grade 0, 3 and below, and a measurement-limited grade are not; the order of what is kept is kept
//   BRD   the board's annotations (Stage 0.5's follow-up): the override is the club; a left-out reading is not read —
//         its monitor readings NaN, its monitor-driven rows NotAssessable, its camera rows as they were; none: unchanged
//   SUM   reads SUMMARIES ONLY (R-DER-3): a recompute reads work_ons.json and never a ledger, a club baseline or a
//         session input (a spying session repository counts every call); the work-ons and distances are the summaries'
//   LZY   lazy (DEC-60): stale until read; a read recomputes once, a second read nothing; an event (EVT-20) marks it stale
//         and says so once; a marked session's summary is read again, the others are not
//   CUP   the catch-up: every ended session oldest first, written by its SessionDiagnostics (diagnostics.json and the
//         summary with DEC-192's growth: their one writer), each club baseline frozen from the predecessors' summaries
//         with what it read (D7: equal to the ledger path's on an unannotated window), each session released
//   S05   the summaries honour the board: a left-out reading is out of its club's window and carry today; an override
//         puts the shot under its new club
//   FRZ   frozen once (a second load leaves the file as it is); an older rule's file is rewritten, the rewrite recorded;
//         not computable while an earlier session holds swings and no current summary (nothing frozen)
//   S05d  (review CN-1) a re-labelled shot is composed under its new club: its own outcome has no carry against the old
//         club's usual, and its factors are the tendency facts' (one composition, DEC-81)
//   FCT   (review 2) a summary whose per-shot facts were made under another tendency or theme rule reads without them,
//         the athlete level does without them, and the catch-up writes them again under today's rules
//   BLK   a write block is reported (writeBlocked: what, where, why), never only logged; (b, review CN-2) also when it
//         fails at the release of a session a pause let go of
//   D15   a pause lets go of the session in hand at once and takes it again first when the pause lifts; cancel drops all
//   TND   round 2 (DEC-192 amended): the tendencies (with their distances) and the themes AthleteDiagnostics reduces from
//         the summaries alone are the ones the ledger path gives (each session's ledger, golfer input, evidence and frozen
//         baseline, the board applied), field for field — and reading them reads no ledger
//   JOB   (3.7-f, J-53) Home's jobs: the tendencies, the themes and the marks asked inline equal the lazy reads; a second ask
//         with nothing moved is Current; on the pool (the owner's own lane, routed by the receiver) they land the same
//   PER   (3.7-f) FILE-ATH-02/03 written by the owner through its AthleteRepository, their one writer, with derivedFrom; the
//         themes' stored file read back by a fresh owner (no reduction); a write block said (writeFailed) and persist()
//         reporting it
//   ASOF  (3.7-f) the tendencies as of a session: that session and the ones before it, the distances pooled the same way
//   CNT   (3.7-f, J-51) a session with shots whose summary this build does not read is counted (notSummedUp), never silent;
//         (4-e) a newer build's summary told apart from a stale one (newerFolders), alone and both at once
//   OUT   (3.7-f) a summary written outside this process is read again after rereadAll()
//   SCP   (3.7-f, DEC-195) a club choice's views from each session's judged rows through its own SessionDiagnostics, the
//         sessions loaded for it released; Current when nothing moved; its own file, read back by a fresh owner
//
//   cmake --build build/tests --target athlete_diagnostics_test -j 6
//   ctest --test-dir build/tests -R '^athlete_diagnostics_test$' --output-on-failure

#include "../athlete.h"
#include "../athlete_diagnostics.h"
#include "../session.h"
#include "../session_diagnostics.h"
#include "../shot.h"
#include "../../App/athlete_catch_up.h"
#include "../../Export/athlete_cache_repository.h"
#include "../../Coaching/board_annotations.h"
#include "../../Coaching/swing_themes.h"
#include "../../Coaching/tendencies_pack.h"
#include "../../Coaching/shot_outcomes_pack.h"
#include "../../Diagnostics/packs/swing_themes_pack.h"
#include "../../Core/work_pool.h"
#include "../../Export/firing_reader_store.h"
#include "../../Export/recording_loader.h"
#include "../../Export/rule_data_install.h"
#include "../../Export/session_diagnostics_files.h"
#include "../../Export/session_files.h"
#include "../../Export/session_store.h"
#include "../../Export/swing_store_repository.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>

#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <algorithm>
#include <atomic>

#ifndef PP_LIVE_SWINGS_DIR
#  define PP_LIVE_SWINGS_DIR "."
#endif

using namespace pinpoint;
using pinpoint::analysis::ShotRecord;

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
static const AthleteId kAthlete = AthleteId::fromString(u"7b2e4c1a-9d3f-4a6b-8c21-5e0f1d2a3b47");

// lm_7iron, a 7 iron the monitor read, its strike height made central (cleanStrike) so it is not a thin mishit: a shot
// in its club's baseline window and distances.
static const char *kDoc = "lm_7iron";

static QString fixture(const char *name)
{
    return QDir(QLatin1String(PP_LIVE_SWINGS_DIR)).filePath(QLatin1String(name) + QLatin1String("/swing.json"));
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

static QJsonObject readJson(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}
static bool writeJson(const QString &path, const QJsonObject &o)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(QJsonDocument(o).toJson()) > 0;
}

// ── A session repository that counts what is asked of it, and forwards everything to the real store ─────────────
class SpyRepository final : public SessionRepository
{
public:
    explicit SpyRepository(SessionStore &inner) : m(inner) {}
    mutable int diagnosticsReads = 0, inputReads = 0, baselineReads = 0, workOnsReads = 0;
    std::atomic<int> deferredReads{ 0 };   // summary reads handed to another thread (readAhead, J-57)
    QSet<Qt::HANDLE> deferredThreads;      // the threads they ran on (GUI thread only writes it after the job)
    std::mutex       threadsLock;

    SessionStartResult start(const SessionOwner &o, const QString &t, const QString &p) override { return m.start(o, t, p); }
    SessionId todaysSession(const SessionOwner &o, const QString &t) override { return m.todaysSession(o, t); }
    WriteResult resume(const SessionId &id, const QString &t) override { return m.resume(id, t); }
    WriteResult end(const SessionId &id) override { return m.end(id); }
    QList<SessionIndexEntry> enumerate(const SessionOwner &o) override { return m.enumerate(o); }
    std::optional<SessionIndexEntry> open(const SessionId &id) const override { return m.open(id); }
    SessionId openFolder(const QString &f, const AthleteId &a) override { return m.openFolder(f, a); }
    ShotRef openSwingFolder(const QString &f, const AthleteId &a) override { return m.openSwingFolder(f, a); }
    SwingAllocation allocateSwing(const SessionId &id) override { return m.allocateSwing(id); }
    QList<ShotRef> shots(const SessionId &id) const override { return m.shots(id); }
    QString swingFolder(const ShotRef &s) const override { return m.swingFolder(s); }
    bool folderPresent(const SessionId &id) const override { return m.folderPresent(id); }
    SessionArchiveResult archive(const SessionId &id, const SessionArchiveOptions &o, const SessionArchiveProgress &p) override
    { return m.archive(id, o, p); }
    SessionArchiveResult restore(const SessionId &id, const SessionArchiveProgress &p) override { return m.restore(id, p); }
    WriteResult trash(const SessionId &id, QString *w) override { return m.trash(id, w); }
    WriteResult trashShot(const ShotRef &s, QString *w) override { return m.trashShot(s, w); }
    DiagnosticsRead readDiagnostics(const SessionId &id) override { ++diagnosticsReads; return m.readDiagnostics(id); }
    WriteResult writeDiagnostics(const SessionId &id, const QJsonObject &s, const QJsonObject &l, const QJsonObject &f,
                                 DiagnosticsRead::State *st) override { return m.writeDiagnostics(id, s, l, f, st); }
    WriteResult backupDiagnostics(const SessionId &id, const QString &t, QString *p) override { return m.backupDiagnostics(id, t, p); }
    WriteResult setAsideDiagnostics(const SessionId &id, QString *p) override { return m.setAsideDiagnostics(id, p); }
    SessionInputRead readSessionInput(const SessionId &id) override { ++inputReads; return m.readSessionInput(id); }
    WriteResult writeSessionInput(const SessionId &id, const SessionInput &in) override { return m.writeSessionInput(id, in); }
    JsonFileRead readClubBaseline(const SessionId &id) override { ++baselineReads; return m.readClubBaseline(id); }
    WriteResult writeClubBaseline(const SessionId &id, const QJsonObject &r) override { return m.writeClubBaseline(id, r); }
    JsonFileRead readWorkOns(const SessionId &id) override { ++workOnsReads; return m.readWorkOns(id); }
    std::function<JsonFileRead()> deferredReadWorkOns(const SessionId &id) override
    {
        std::function<JsonFileRead()> read = m.deferredReadWorkOns(id);
        return [this, read] {
            ++deferredReads;
            { std::lock_guard<std::mutex> g(threadsLock); deferredThreads.insert(QThread::currentThreadId()); }
            return read();
        };
    }
    WriteResult writeWorkOns(const SessionId &id, const QJsonObject &r) override { return m.writeWorkOns(id, r); }

private:
    SessionStore &m;
};

static bool cleanStrike(const QString &path);

// ── One scratch library: the real store and doors, an athlete, its legacy sessions (one per day) ──────────────────
struct Rig {
    QTemporaryDir                                  dir;
    SessionStore                                   store;
    std::unique_ptr<SpyRepository>                 spy;
    std::unique_ptr<SwingStoreRecordingRepository> recordings;
    std::shared_ptr<RecordingReader>               reader;
    std::unique_ptr<WorkReceiver>                  receiver;
    std::unique_ptr<WorkPool>                      pool;
    AthleteCacheRepository                         caches;    // FILE-ATH-02/03 (refactor 3.7-f)
    std::unique_ptr<Athlete>                       athlete;   // destroyed first

    // `diagnostics`: the Athlete's sessions get their SessionDiagnostics (persisting into this library); `async`: on
    // the pool, else headless (synchronous).
    explicit Rig(bool diagnostics, bool async = false)
    {
        store.setLockWaitForTest(200);
        store.setOsTrashForTest(false);
        spy        = std::make_unique<SpyRepository>(store);
        recordings = std::make_unique<SwingStoreRecordingRepository>(store);
        reader     = std::make_shared<SwingStoreRecordingReader>(store);
        receiver   = std::make_unique<WorkReceiver>([this](const WorkAddress &a) {
            if (isAthleteWorkOwner(a.owner))   // the athlete level's own lane (3.7-f, J-53)
                return athleteDiagnosticsWorkTarget(athlete && athleteOfWorkOwner(a.owner) == athlete->id() ? athlete.get() : nullptr, a);
            return diagnosticsWorkTarget(athlete ? athlete->loadedSession(SessionId::fromString(a.owner)) : nullptr, a);
        });
        if (async) pool = std::make_unique<WorkPool>(2);
        QDir().mkpath(athleteFolder());
        athlete = std::make_unique<Athlete>(&caches, spy.get(), kAthlete);
        AthleteRecord r;
        r.name = kName;
        athlete->setStored(r, athleteFolder(), SessionOwner{ kAthlete, kName, dir.path() }, Athlete::Source::Library, false);
        athlete->setShotDoors({ recordings.get(), reader });
        if (diagnostics) {
            DiagnosticsDoors d;
            d.pool            = pool.get();
            d.receiver        = receiver.get();
            d.ruleData        = RuleData::installed();
            d.reader          = std::make_shared<SwingStoreFiringReader>(store, *recordings);
            d.persist         = true;    // this suite's own scratch library only
            d.synchronous     = !async;
            d.writeDebounceMs = 600000;
            d.rebuild         = true;    // every shot fired: the documents are fresh copies
            d.attach          = &SessionDiagnostics::attach;
            athlete->setDiagnosticsDoors(d);
        }
    }
    ~Rig() { athlete.reset(); pool.reset(); }

    QString athleteFolder() const { return dir.path() + QLatin1Char('/') + kToken; }
    // A legacy session (no session file: Ended, its day from its name) on 2026-09-<day>, `swings` copies of kDoc.
    QString legacySession(int day, int swings)
    {
        const QString folder = athleteFolder() + QStringLiteral("/2026-09-%1_%2_Swing_01").arg(day, 2, 10, QLatin1Char('0')).arg(kToken);
        for (int i = 1; i <= swings; ++i) {
            const QString sw = folder + QStringLiteral("/swing_%1").arg(i, 4, 10, QLatin1Char('0'));
            QDir().mkpath(sw);
            QFile::copy(fixture(kDoc), sw + QStringLiteral("/swing.json"));
            QFile(sw + QStringLiteral("/swing.json")).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            cleanStrike(sw + QStringLiteral("/swing.json"));
        }
        if (swings == 0) QDir().mkpath(folder + QStringLiteral("/swing_0001"));
        return folder;
    }
    SessionId idOf(const QString &folder)
    {
        for (const SessionIndexEntry &e : athlete->index())
            if (QDir::cleanPath(e.folder) == QDir::cleanPath(folder)) return e.id();
        return {};
    }
    AthleteCallables callables()
    {
        AthleteCallables c;
        c.listAthletes = [this] { return QList<AthleteId>{ kAthlete }; };
        c.loadAthlete  = [this](const AthleteId &id) { return id == kAthlete ? athlete.get() : nullptr; };
        return c;
    }
};

// The fixture's strike height (the monitor's, -14 mm: a thin) made central, in its reading and its phase grid.
static QJsonValue centred(const QJsonValue &v)
{
    if (v.isArray()) {
        QJsonArray a;
        for (const QJsonValue &e : v.toArray()) a.append(centred(e));
        return a;
    }
    if (!v.isObject()) return v;
    QJsonObject o = v.toObject();
    const bool strike = o.value(QStringLiteral("key")).toString() == QLatin1String("lm.strikeHeight");
    for (auto it = o.begin(); it != o.end(); ++it) {
        if (it.key() == QLatin1String("strikeHeight") && it.value().isDouble()) it.value() = 0.0;
        else if (strike && it.key() == QLatin1String("phaseSamples")) {
            QJsonArray samples;
            for (const QJsonValue &sv : it.value().toArray()) { QJsonObject so = sv.toObject(); so[QStringLiteral("value")] = 0.0; samples.append(so); }
            it.value() = samples;
        } else {
            it.value() = centred(it.value());
        }
    }
    return o;
}
static bool cleanStrike(const QString &path)
{
    return writeJson(path, centred(readJson(path)).toObject());
}

// The swing document's review block, set as the launch monitor board's commands would leave it.
static bool annotate(const QString &swingDir, bool excluded, const QString &clubOverride)
{
    const QString path = swingDir + QStringLiteral("/swing.json");
    QJsonObject root = readJson(path);
    QJsonObject review = root.value(QStringLiteral("review")).toObject();
    if (excluded) review[QStringLiteral("excluded")] = true;
    if (!clubOverride.isEmpty()) review[QStringLiteral("clubOverride")] = clubOverride;
    root[QStringLiteral("review")] = review;
    return writeJson(path, root);
}

// ── PRA ─────────────────────────────────────────────────────────────────────────────────────────────────────────
static void testPraise()
{
    std::printf("\nPRA — praise needs Repeatable or Grooved (04 step 3 item 5; D23)\n");
    using analysis::PraiseGrade;
    check(QStringLiteral("PRAa Grooved (5) and Repeatable (4) are praised"),
          analysis::praisedForConsistency(PraiseGrade{ true, 5, false }) && analysis::praisedForConsistency(PraiseGrade{ true, 4, false }));
    check(QStringLiteral("PRAb 3 and below are not"),
          !analysis::praisedForConsistency(PraiseGrade{ true, 3, false }) && !analysis::praisedForConsistency(PraiseGrade{ true, 1, false }));
    check(QStringLiteral("PRAc ungraded (D23) and \"not enough swings\" (grade 0) are not praised"),
          !analysis::praisedForConsistency(PraiseGrade{}) && !analysis::praisedForConsistency(PraiseGrade{ true, 0, false }));
    check(QStringLiteral("PRAd a measurement-limited grade (shown \"Checking\") is not praised"),
          !analysis::praisedForConsistency(PraiseGrade{ true, 5, true }));
    std::vector<analysis::DoWellRow> rows;
    for (const char *id : { "a", "b", "c", "d" }) { analysis::DoWellRow r; r.conditionId = QLatin1String(id); rows.push_back(r); }
    const QHash<QString, PraiseGrade> g{ { QStringLiteral("a"), { true, 4, false } }, { QStringLiteral("b"), {} },
                                         { QStringLiteral("c"), { true, 5, false } }, { QStringLiteral("d"), { true, 2, false } } };
    analysis::applyPraiseRule(rows, [&g](const QString &id) { return g.value(id); });
    check(QStringLiteral("PRAe the rule keeps exactly the graded-repeatable rows, in their order"),
          rows.size() == 2 && rows[0].conditionId == QLatin1String("a") && rows[1].conditionId == QLatin1String("c"));
}

// ── BRD ─────────────────────────────────────────────────────────────────────────────────────────────────────────
static void testBoard()
{
    std::printf("\nBRD — the launch monitor board's annotations, as every reduction reads them\n");
    const auto snap = RuleData::installed()->snapshot();
    const QSet<QString> lm = analysis::launchMonitorMeasures(snap->pack->pack());
    check(QStringLiteral("BRDa the monitor's measures are the pack's lm. keys: carry, face-to-path in; the P4 shaft angle out"),
          lm.contains(QStringLiteral("m_carryDistance")) && lm.contains(QStringLiteral("m_faceToPath"))
              && !lm.contains(QStringLiteral("m_shaftAngleP4")));
    ShotRecord r;
    r.shotId = 3;
    r.club = QStringLiteral("7 IRON");
    r.carry = 150.0;
    r.shaftAngleP4 = 80.0;
    analysis::ConditionRow ball, body;
    ball.conditionId = QStringLiteral("x.ball");  ball.state = analysis::ShotState::Fired; ball.drivingMeasureId = QStringLiteral("m_faceToPath");
    body.conditionId = QStringLiteral("x.body");  body.state = analysis::ShotState::Fired; body.drivingMeasureId = QStringLiteral("m_shaftAngleP4");
    r.rows = { ball, body };
    const auto same = [](const ShotRecord &a, const ShotRecord &b) {
        return QJsonDocument(analysis::toJson(std::vector<ShotRecord>{ a })).toJson()
            == QJsonDocument(analysis::toJson(std::vector<ShotRecord>{ b })).toJson();
    };
    check(QStringLiteral("BRDb no annotation: the shot is unchanged"), same(analysis::annotatedShot(r, {}, lm), r));
    const ShotRecord o = analysis::annotatedShot(r, analysis::BoardAnnotation{ false, QStringLiteral("8 IRON") }, lm);
    check(QStringLiteral("BRDc an override is the club; nothing else moves"), o.club == QLatin1String("8 IRON") && o.carry == 150.0
              && o.rows[0].state == analysis::ShotState::Fired);
    const ShotRecord x = analysis::annotatedShot(r, analysis::BoardAnnotation{ true, QString() }, lm);
    check(QStringLiteral("BRDd left out: the monitor's readings are not read, its rows NotAssessable, the camera's untouched"),
          std::isnan(x.carry) && x.shaftAngleP4 == 80.0 && x.rows[0].state == analysis::ShotState::NotAssessable
              && x.rows[1].state == analysis::ShotState::Fired && x.club == r.club);
}

// One session's summary as a test writes it: its work-ons (one listed condition, its evidence) and one club's window.
static analysis::SessionResults summaryOf(const QString &folderName, qint64 startMs, const QList<double> &carries)
{
    analysis::SessionResults r;
    r.workOns.sessionId = folderName;
    r.workOns.startMs = r.workOns.endMs = startMs;
    r.workOns.shotCount = int(carries.size());
    analysis::WorkOnEntry e;
    e.id = QStringLiteral("test.cond");
    e.rank = 1;
    e.fired = int(carries.size());
    e.assessable = int(carries.size());
    r.workOns.entries.push_back(e);
    analysis::WorkOnEvidence ev;
    ev.id = e.id;
    ev.state = analysis::SessionEvidence::Pattern;
    ev.fired = ev.assessable = int(carries.size());
    r.workOns.evidence.push_back(ev);
    r.hasGrowth = true;
    analysis::ClubSessionPool pool;
    pool.club = canonicalClubId(QStringLiteral("7 IRON"));
    for (int i = 0; i < carries.size(); ++i) {
        analysis::ClubPoolShot s;
        s.position = i;
        s.lm.insert(QStringLiteral("lm.carryDistance"), carries[i]);
        pool.shots.push_back(s);
    }
    r.clubWindows.push_back(pool);
    return r;
}

// ── SUM + LZY ───────────────────────────────────────────────────────────────────────────────────────────────────
static void testSummariesOnlyAndLazy()
{
    std::printf("\nSUM / LZY — summaries only (R-DER-3); stale on an event, recomputed when read (DEC-60)\n");
    Rig rig(/*diagnostics=*/false);
    const QStringList folders{ rig.legacySession(1, 0), rig.legacySession(2, 0), rig.legacySession(3, 0) };
    const QList<QList<double>> carries{ { 150, 152 }, { 148, 151 }, { 160, 162 } };
    for (int i = 0; i < folders.size(); ++i) {
        writeJson(SessionFiles::workOnsFile(folders[i]),
                  analysis::sessionResultsToJson(summaryOf(QFileInfo(folders[i]).fileName(), 1'000'000LL * (i + 1), carries[i])));
        // A ledger that would say something else: never read.
        writeJson(SessionFiles::diagnosticsFile(folders[i]), QJsonObject{ { QStringLiteral("schemaVersion"), 1 } });
    }
    AthleteDiagnostics *d = AthleteDiagnostics::of(rig.athlete.get());
    int changed = 0;
    QObject::connect(d, &AthleteDiagnostics::changed, [&changed] { ++changed; });
    check(QStringLiteral("LZYa made stale: nothing computed until read"), d && d->stale() && d->counts().recomputes == 0);
    if (!d) return;
    const std::vector<analysis::WorkOnSummary> list = d->workOns();
    check(QStringLiteral("SUMa a read reads the three summaries and NO ledger, club baseline or session input"),
          d->counts().recomputes == 1 && rig.spy->workOnsReads == 3 && rig.spy->diagnosticsReads == 0
              && rig.spy->baselineReads == 0 && rig.spy->inputReads == 0);
    check(QStringLiteral("SUMb the athlete work-ons are the summaries' (one condition, listed by three sessions)"),
          list.size() == 1 && list[0].id == QLatin1String("test.cond") && list[0].sessionsListed == 3);
    const std::vector<analysis::ClubDistanceItem> dist = d->distances();
    check(QStringLiteral("SUMc the distances are the summaries' windows (one 7 iron row, six shots, three sessions, mean 153.83)"),
          dist.size() == 1 && dist[0].shots == 6 && dist[0].sessions == 3 && std::fabs(dist[0].carryMean - 923.0 / 6.0) < 1e-9);
    d->workOns();
    d->distances();
    check(QStringLiteral("LZYb read again, not stale: nothing recomputed, nothing read"),
          d->counts().recomputes == 1 && rig.spy->workOnsReads == 3 && !d->stale());
    emit rig.athlete->sessionTrashed(rig.idOf(folders[1]));   // EVT-20, as the Athlete emits it
    check(QStringLiteral("LZYc an event marks it stale and says so once"), d->stale() && changed == 1);
    emit rig.athlete->sessionStarted(rig.idOf(folders[0]));
    check(QStringLiteral("LZYd … a second event before a read says nothing more"), changed == 1);
    d->workOns();
    check(QStringLiteral("LZYe recomputed when read; a session not marked is not read again"),
          d->counts().recomputes == 2 && rig.spy->workOnsReads == 3 && !d->stale());
    writeJson(SessionFiles::workOnsFile(folders[2]),
              analysis::sessionResultsToJson(summaryOf(QFileInfo(folders[2]).fileName(), 3'000'000LL, { 170, 170 })));
    d->markStale(rig.idOf(folders[2]));
    const std::vector<analysis::ClubDistanceItem> dist2 = d->distances();
    check(QStringLiteral("LZYf a marked session's summary is read again (one read) and the result follows it"),
          rig.spy->workOnsReads == 4 && dist2.size() == 1 && std::fabs(dist2[0].carryMean - 941.0 / 6.0) < 1e-9);
    check(QStringLiteral("SUMd still no ledger read"), rig.spy->diagnosticsReads == 0);
}

// ── CUP, S05, FRZ, BLK ─────────────────────────────────────────────────────────────────────────────────────────
static void testCatchUpAndFreeze()
{
    std::printf("\nCUP / S05 / FRZ / BLK — the catch-up, the freeze, the board in the summaries, a write block\n");
    Rig rig(/*diagnostics=*/true);
    const QString s1 = rig.legacySession(1, 2), s2 = rig.legacySession(2, 2), s3 = rig.legacySession(3, 2);
    annotate(s2 + QStringLiteral("/swing_0002"), /*excluded=*/true, QString());
    annotate(s3 + QStringLiteral("/swing_0001"), false, QStringLiteral("8 IRON"));
    AthleteCatchUp catchUp(rig.callables());
    QStringList blocked;
    QObject::connect(&catchUp, &AthleteCatchUp::writeBlocked,
                     [&blocked](const QString &, const QString &where, const QString &) { blocked << where; });
    catchUp.run(kAthlete);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const AthleteCatchUp::Report &r = catchUp.report();
    check(QStringLiteral("CUPa three sessions taken in hand, written and released; nothing blocked; done"),
          r.sessions == 3 && r.written == 3 && r.released == 3 && r.blocked == 0 && !catchUp.running()
              && rig.athlete->loadedSessions().isEmpty());
    for (const QString &line : r.why) std::printf("        why: %s\n", qPrintable(line));

    const auto summary = [](const QString &folder) {
        bool ok = false;
        const analysis::SessionResults res = analysis::sessionResultsFromJson(readJson(SessionFiles::workOnsFile(folder)),
                                                                              analysis::SessionResultsRead::Strict, &ok);
        return std::make_pair(ok, res);
    };
    const auto [ok1, r1] = summary(s1);
    const auto [ok2, r2] = summary(s2);
    const auto [ok3, r3] = summary(s3);
    check(QStringLiteral("CUPb each session's summary is written by its SessionDiagnostics, with DEC-192's growth"),
          ok1 && ok2 && ok3 && r1.hasGrowth && r2.hasGrowth && r3.hasGrowth && r1.workOns.shotCount == 2
              && QFileInfo::exists(SessionFiles::diagnosticsFile(s1)));
    for (const auto *res : { &r1, &r2, &r3 }) {   // what each summary holds per club: the window and the shots' clubs
        QStringList w, c;
        for (const analysis::ClubSessionPool &p : res->clubWindows)
            w << QStringLiteral("%1:%2 shots/%3 mishits").arg(p.club).arg(p.shots.size()).arg(p.mishits.size());
        for (const analysis::ShotResult &x : res->shots) c << QStringLiteral("%1=%2").arg(x.shotId).arg(x.club);
        std::printf("        summary: windows {%s} shots {%s}\n", qPrintable(w.join(QStringLiteral(", "))), qPrintable(c.join(QLatin1Char(' '))));
    }
    const QJsonObject b1 = readJson(SessionFiles::clubBaselineFile(s1)), b2 = readJson(SessionFiles::clubBaselineFile(s2)),
                      b3 = readJson(SessionFiles::clubBaselineFile(s3));
    {
        AthleteDiagnostics *ad = AthleteDiagnostics::of(rig.athlete.get());
        const ClubBaselineComputation c1 = ad->clubBaselineFor(rig.idOf(s1)), c2 = ad->clubBaselineFor(rig.idOf(s2)),
                                      c3 = ad->clubBaselineFor(rig.idOf(s3));
        check(QStringLiteral("CUPc each club baseline frozen (this rule's schema; a first freeze records no rewrite), computed from "
                             "the earlier sessions' summaries: none, the first, the second then the first"),
              b1.value(QStringLiteral("schema")).toInt() == analysis::kClubBaselineSchemaVersion && !b1.contains(QStringLiteral("rewrites"))
                  && !b3.contains(QStringLiteral("rewrites")) && c1.computable && c1.read.isEmpty()
                  && c2.read == QStringList{ QFileInfo(s1).fileName() }
                  && c3.read == (QStringList{ QFileInfo(s2).fileName(), QFileInfo(s1).fileName() }));
    }
    // D7 (DEC-172): the second session's baseline (an unannotated window: the first session's) is the ledger path's.
    {
        const DiagnosticsRead read = SessionDiagnosticsFiles::readDiagnostics(s1);
        analysis::ClubDistanceSession led;
        led.folder = QFileInfo(s1).fileName();
        led.day = QDate(2026, 9, 1);
        led.shots = analysis::fromJson(read.envelope.ledger);
        const auto snap = RuleData::installed()->snapshot();
        const analysis::FlightNorms flight = snap->flightNorms ? *snap->flightNorms : analysis::FlightNorms{};
        const auto fromLedgers = analysis::clubBaselineAsOf({ led }, QDate(2026, 9, 2), flight);
        bool okb = false;
        const auto frozen = analysis::clubBaselineFromJson(b2, &okb).clubs;
        const QString seven = canonicalClubId(QStringLiteral("7 IRON"));
        std::printf("        baseline from summaries: %d club(s), 7 iron n %d; from the ledger: %d club(s), n %d; ledger shots %d\n",
                    int(frozen.size()), frozen.value(seven).n, int(fromLedgers.size()), fromLedgers.value(seven).n, int(led.shots.size()));
        check(QStringLiteral("CUPd D7: that baseline equals the one from the first session's ledger (7 iron, n 2)"),
              okb && frozen.size() == fromLedgers.size() && frozen.contains(seven) && fromLedgers.contains(seven)
                  && frozen.value(seven).n == 2 && fromLedgers.value(seven).n == 2
                  && std::fabs(frozen.value(seven).carryMean - fromLedgers.value(seven).carryMean) < 1e-9);
    }
    // S05: the board in the summaries.
    {
        const QString seven = canonicalClubId(QStringLiteral("7 IRON")), eight = canonicalClubId(QStringLiteral("8 IRON"));
        const auto windowShots = [](const analysis::SessionResults &res, const QString &club) {
            for (const analysis::ClubSessionPool &p : res.clubWindows) if (p.club == club) return int(p.shots.size());
            return 0;
        };
        check(QStringLiteral("S05a the left-out reading is out of its club's window: one 7 iron shot in the second session"),
              windowShots(r2, seven) == 1 && windowShots(r1, seven) == 2);
        check(QStringLiteral("S05b the override puts the third session's first shot under the 8 iron (its club and its window)"),
              !r3.shots.empty() && r3.shots[0].club == eight && windowShots(r3, eight) == 1 && windowShots(r3, seven) == 1);
        AthleteDiagnostics *d = AthleteDiagnostics::of(rig.athlete.get());
        int sevens = 0, eights = 0;
        for (const analysis::ClubDistanceItem &it : d->distances()) {
            if (it.club == seven) sevens = it.shots;
            if (it.club == eight) eights = it.shots;
        }
        check(QStringLiteral("S05c Your distances from the summaries follow the board (7 iron 4 shots, 8 iron 1)"),
              sevens == 4 && eights == 1);
        // S05d (CN-1): the re-labelled shot (the third session's first) is composed in the reduction under the 8 iron: its own
        // outcome's factors are the ones its tendency facts were judged with under that club (failed before the fix: the kept
        // derived data, composed under the recorded 7 iron, was passed through).
        const auto factorsJson = [](const analysis::ShotFactors &f) {
            return QJsonDocument(analysis::shotFactorsToJson(f)).toJson(QJsonDocument::Compact);
        };
        if (r3.shots.size() == 2 && r3.tendencyShots.size() == 2)
            std::printf("        S05d: club %s; carry present %d / %d; factors equal %d; tendency club %s\n",
                        qPrintable(r3.shots[0].club), int(r3.shots[0].outcome.carry.present), int(r3.shots[1].outcome.carry.present),
                        int(factorsJson(r3.shots[0].factors) == factorsJson(r3.tendencyShots[0].factors)), qPrintable(r3.tendencyShots[0].club));
        check(QStringLiteral("S05d a re-labelled shot is composed under its new club (its factors the tendency facts')"),
              r3.shots.size() == 2 && r3.tendencyShots.size() == 2 && r3.shots[0].club == eight
                  && factorsJson(r3.shots[0].factors) == factorsJson(r3.tendencyShots[0].factors)
                  && r3.tendencyShots[0].club == QLatin1String("8 IRON"));
    }
    // TND (round 2): the tendencies and the themes from the summaries alone = the ledger path's.
    {
        const auto snap = RuleData::installed()->snapshot();
        const analysis::CharacteristicPack &pack = snap->pack->pack();
        const QSet<QString> lm = analysis::launchMonitorMeasures(pack);
        std::vector<analysis::TendencySessionInput> led;
        std::vector<analysis::ThemeSessionInput> themeLed;
        for (const auto &[folder, day] : { std::make_pair(s1, 1), std::make_pair(s2, 2), std::make_pair(s3, 3) }) {
            const DiagnosticsRead read = SessionDiagnosticsFiles::readDiagnostics(folder);
            analysis::TendencySessionInput in;
            in.session.folder = QFileInfo(folder).fileName();
            in.session.day    = QDate(2026, 9, day);
            in.session.nOfDay = 1;
            in.session.shots  = analysis::fromJson(read.envelope.ledger);
            const GolferInput g = SessionDiagnosticsFiles::golferInput(folder, read.envelope.legacyGolferInput);
            // The board as the test set it (a legacy swing whose document names no athlete is not named by the folder
            // reader, so its review is not read there: 3.6 NOTES F4); the session read it through its Shots.
            QHash<int, analysis::BoardAnnotation> board;
            if (day == 2) board.insert(2, analysis::BoardAnnotation{ true, QString() });
            if (day == 3) board.insert(1, analysis::BoardAnnotation{ false, QStringLiteral("8 IRON") });
            analysis::annotateShots(in.session.shots, board, lm);
            in.session.corrections = g.corrections;
            in.intents  = analysis::resolvedShotIntents(g.shotIntent, in.session.shots, g.intentRun.fromShot, g.intentRun.shape);
            in.evidence = analysis::tendencyEvidenceFromJson(readJson(SessionFiles::workOnsFile(folder)).value(QStringLiteral("outcomeEvidence")).toObject());
            bool okb = false;
            const auto b = analysis::clubBaselineFromJson(readJson(SessionFiles::clubBaselineFile(folder)), &okb);
            if (okb) in.baseline = b.clubs;
            themeLed.push_back(analysis::ThemeSessionInput{ in.session.folder, in.session.shots });
            led.push_back(std::move(in));
        }
        const analysis::ShotOutcomePack outcomes = analysis::shotOutcomePackFor(&pack);
        const analysis::FlightNorms &flight = snap->flightNorms ? *snap->flightNorms : analysis::shippedFlightNorms();
        const analysis::TendencySet fromLedgers = analysis::tendencySetFromSessions(led, outcomes, snap->norms->contexts(), flight);
        AthleteDiagnostics *d = AthleteDiagnostics::of(rig.athlete.get());
        d->markStale();
        const int ledgersBefore = rig.spy->diagnosticsReads;
        const analysis::TendencySet &fromSummaries = d->tendencies();
        check(QStringLiteral("TNDa the tendencies from the summaries alone are the ledger path's, field for field (%1 shots)")
                  .arg(fromSummaries.countedShots),
              fromSummaries.countedShots == 5 && analysis::tendencySetToJson(fromSummaries) == analysis::tendencySetToJson(fromLedgers));
        const analysis::SwingThemes ledThemes = analysis::reduceSwingThemes(
            themeLed, analysis::themeMeasureInfo(pack),
            analysis::themeConditionInfo(pack, snap->drills ? snap->drills->merged : analysis::DrillSet{}));
        const analysis::SwingThemes sumThemes = d->themes();
        check(QStringLiteral("TNDb the themes from the summaries alone are the ledger path's (%1 swings over %2 sessions)")
                  .arg(sumThemes.allSwings).arg(sumThemes.allSessions),
              sumThemes.allSwings == 6 && analysis::toJson(sumThemes) == analysis::toJson(ledThemes));
        check(QStringLiteral("TNDc … and reading them read no ledger"), rig.spy->diagnosticsReads == ledgersBefore);
    }
    // FCT (review 2): facts of another rule read as absent, and are made again.
    {
        const QString path = SessionFiles::workOnsFile(s1);
        const QJsonObject current = readJson(path);
        const auto aged = [&current](const char *rule) {
            QJsonObject root = current;
            QJsonObject fr = root.value(QStringLiteral("factsRules")).toObject();
            fr[QLatin1String(rule)] = fr.value(QLatin1String(rule)).toInt() - 1;
            root[QStringLiteral("factsRules")] = fr;
            return root;
        };
        bool okT = false, okH = false, okC = false;
        const analysis::SessionResults t = analysis::sessionResultsFromJson(aged("tendency"), analysis::SessionResultsRead::Strict, &okT);
        const analysis::SessionResults h = analysis::sessionResultsFromJson(aged("theme"), analysis::SessionResultsRead::Strict, &okH);
        const analysis::SessionResults c = analysis::sessionResultsFromJson(current, analysis::SessionResultsRead::Strict, &okC);
        check(QStringLiteral("FCTa facts made under an older tendency or theme rule read as absent; the rest of the summary reads"),
              okT && okH && okC && !t.hasFacts && !h.hasFacts && t.hasGrowth && h.hasGrowth && c.hasFacts
                  && t.tendencyShots.empty() && h.themeShots.empty()
                  && current.value(QStringLiteral("factsRules")).toObject().value(QStringLiteral("tendency")).toInt()
                         == analysis::kTendencyRuleVersion);
        AthleteDiagnostics *d = AthleteDiagnostics::of(rig.athlete.get());
        writeJson(path, aged("tendency"));
        d->markStale(rig.idOf(s1));
        const int without = d->tendencies().countedShots;
        AthleteCatchUp again(rig.callables());
        again.run(kAthlete);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        bool okR = false;
        const analysis::SessionResults back = analysis::sessionResultsFromJson(readJson(path), analysis::SessionResultsRead::Strict, &okR);
        const int with = d->tendencies().countedShots;
        check(QStringLiteral("FCTb the athlete level does without them (%1 of 5 shots), and the catch-up makes them again (%2)")
                  .arg(without).arg(with),
              without == 3 && okR && back.hasFacts && with == 5);
    }
    // FRS (BRIEF-f 13; REVIEW-e R2-m4, REVIEW-f R1-B1): a predecessor counts only when its summary is FRESH by the one
    // fingerprint rule. The rule data moved with no version bump (the grade policy, SET-15): the first session's summary
    // still reads strictly, with its windows, but is stale — so the second's baseline waits on it, and is computable
    // once the catch-up has written it again. (Failed before the fix: computable from the stale windows.)
    {
        AthleteDiagnostics *d = AthleteDiagnostics::of(rig.athlete.get());
        RuleData *rules = RuleData::installed();
        const QString policy = rules->snapshot()->gradePolicy;
        const ClubBaselineComputation before = d->clubBaselineFor(rig.idOf(s2));
        rules->setGradePolicy(policy == QLatin1String("strict") ? QStringLiteral("standard") : QStringLiteral("strict"));
        bool okS = false;
        const analysis::SessionResults stale = analysis::sessionResultsFromJson(readJson(SessionFiles::workOnsFile(s1)),
                                                                                analysis::SessionResultsRead::Strict, &okS);
        const ClubBaselineComputation moved = d->clubBaselineFor(rig.idOf(s2));
        std::printf("        FRS: before computable %d; moved computable %d, waiting on %d, why [%s]\n", int(before.computable),
                    int(moved.computable), int(moved.waitingOn.size()), qPrintable(moved.why));
        check(QStringLiteral("FRSa the rule data moved with no version bump: the first session's summary still reads (with its "
                             "windows) but is stale, so the second's baseline is not computable, waiting on the first"),
              before.computable && okS && stale.hasGrowth && !moved.computable
                  && moved.waitingOn == QList<SessionId>{ rig.idOf(s1) } && moved.why.contains(QFileInfo(s1).fileName()));
        AthleteCatchUp again(rig.callables());
        again.run(kAthlete);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const ClubBaselineComputation after = d->clubBaselineFor(rig.idOf(s2));
        check(QStringLiteral("FRSb once the catch-up has written it again, computable from it"),
              after.computable && after.read == QStringList{ QFileInfo(s1).fileName() } && after.waitingOn.isEmpty());
        rules->setGradePolicy(policy);
        AthleteCatchUp back(rig.callables());
        back.run(kAthlete);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    // FRZ: frozen once; an older rule's file rewritten; not computable while an earlier session is not summed up.
    {
        const QByteArray before = [&] { QFile f(SessionFiles::clubBaselineFile(s3)); f.open(QIODevice::ReadOnly); return f.readAll(); }();
        Session *again = rig.athlete->session(rig.idOf(s3));
        const QByteArray after = [&] { QFile f(SessionFiles::clubBaselineFile(s3)); f.open(QIODevice::ReadOnly); return f.readAll(); }();
        check(QStringLiteral("FRZa a second load leaves a frozen baseline as it is (DEC-46: frozen once)"),
              again && !before.isEmpty() && before == after && again->diagnostics()->counts().baselineFreezes == 0);
        rig.athlete->releaseSession(rig.idOf(s3));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        writeJson(SessionFiles::clubBaselineFile(s2), QJsonObject{ { QStringLiteral("schema"), 2 }, { QStringLiteral("clubs"), QJsonObject() } });
        Session *two = rig.athlete->session(rig.idOf(s2));
        const QJsonObject rewritten = readJson(SessionFiles::clubBaselineFile(s2));
        const QJsonArray rewrites = rewritten.value(QStringLiteral("rewrites")).toArray();
        const QJsonObject rw = rewrites.at(0).toObject();
        check(QStringLiteral("FRZb an older rule's file is rewritten from the summaries, the rewrite recorded: when, and what it read"),
              two && rewritten.value(QStringLiteral("schema")).toInt() == analysis::kClubBaselineSchemaVersion
                  && rewrites.size() == 1 && !rw.value(QStringLiteral("at")).toString().isEmpty()
                  && rw.value(QStringLiteral("read")).toArray() == QJsonArray{ QFileInfo(s1).fileName() }
                  && two->diagnostics()->counts().baselineFreezes == 1);
        rig.athlete->releaseSession(rig.idOf(s2));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        QFile::remove(SessionFiles::workOnsFile(s1));
        const ClubBaselineComputation c = AthleteDiagnostics::of(rig.athlete.get())->clubBaselineFor(rig.idOf(s2));
        check(QStringLiteral("FRZc not computable while an earlier session holds swings and no summary (and it says which)"),
              !c.computable && c.why.contains(QFileInfo(s1).fileName()));
    }
    // BLK: the first session's summary cannot be written (its path is a folder): reported, with where.
    {
        QDir().mkpath(SessionFiles::workOnsFile(s1));
        AthleteCatchUp second(rig.callables());
        QStringList where;
        QObject::connect(&second, &AthleteCatchUp::writeBlocked,
                         [&where](const QString &, const QString &w, const QString &) { where << w; });
        second.run(kAthlete);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        std::printf("        blocked %d, where [%s], why [%s]\n", second.report().blocked, qPrintable(where.join(QStringLiteral(", "))),
                    qPrintable(second.report().why.join(QStringLiteral(" | "))));
        check(QStringLiteral("BLKa a write block is reported (writeBlocked, with the session's folder) and counted"),
              second.report().blocked == 1 && where.size() == 1 && QDir::cleanPath(where[0]) == QDir::cleanPath(s1));
        QDir(SessionFiles::workOnsFile(s1)).removeRecursively();
    }
    check(QStringLiteral("CUPe nothing blocked in the first run"), blocked.isEmpty());
}

// ── BLK b (review CN-2): a write that fails at the release of a session a pause let go of is reported ──────────
static void testBlockAtRelease()
{
    std::printf("\nBLK b — a write that fails at the release of a session let go of is reported\n");
    Rig rig(/*diagnostics=*/true, /*async=*/true);
    const QString s1 = rig.legacySession(1, 6);
    writeJson(SessionFiles::clubBaselineFile(s1), analysis::clubBaselineToJson(analysis::ClubBaselineSnapshot{}));   // frozen: nothing written at load
    QFile::setPermissions(s1, QFileDevice::ReadOwner | QFileDevice::ExeOwner);   // the folder refuses every write
    AthleteCatchUp catchUp(rig.callables());
    QStringList where;
    QObject::connect(&catchUp, &AthleteCatchUp::writeBlocked,
                     [&where](const QString &, const QString &w, const QString &) { where << w; });
    catchUp.run(kAthlete);
    SessionDiagnostics *d = rig.athlete->loadedSessions().isEmpty() ? nullptr : rig.athlete->loadedSessions().first()->diagnostics();
    const bool midway = d && pump([d] { return d->counts().kept >= 1 && d->outstanding() > 0; }, 30000);
    catchUp.setPaused(true);   // let go now: its release writes what it kept, into a folder that refuses it
    check(QStringLiteral("BLKb paused with results kept and unwritten: the failed write at its release is reported "
                         "(writeBlocked with the folder; the session counted blocked, and interrupted)"),
          midway && catchUp.report().interrupted == 1 && catchUp.report().blocked == 1 && !where.isEmpty()
              && QDir::cleanPath(where.first()) == QDir::cleanPath(s1));
    QFile::setPermissions(s1, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    catchUp.cancel();
}

// ── D15 ─────────────────────────────────────────────────────────────────────────────────────────────────────────
static void testPauseAndCancel()
{
    std::printf("\nD15 — a pause lets go of the session in hand at once; cancel drops the queue\n");
    Rig rig(/*diagnostics=*/true, /*async=*/true);
    rig.legacySession(1, 2);
    rig.legacySession(2, 2);
    rig.legacySession(3, 2);
    AthleteCatchUp catchUp(rig.callables());
    catchUp.run(kAthlete);
    const bool inHand = catchUp.running() && catchUp.report().sessions == 1;
    catchUp.setPaused(true);
    check(QStringLiteral("D15a paused with a session in hand: it is let go at once (released, unfinished) and nothing else is taken"),
          inHand && catchUp.report().interrupted == 1 && catchUp.report().released == 1 && catchUp.report().written == 0
              && catchUp.running() && catchUp.report().sessions == 1);
    pump([] { return false; }, 200);
    check(QStringLiteral("D15b … and stays paused"), catchUp.report().sessions == 1);
    catchUp.setPaused(false);
    check(QStringLiteral("D15c the pause lifting takes it again first, then the rest: three written"),
          pump([&catchUp] { return !catchUp.running(); }) && catchUp.report().written == 3 && catchUp.report().sessions == 4);

    AthleteCatchUp other(rig.callables());
    other.run(kAthlete);
    other.cancel();
    pump([] { return false; }, 200);
    check(QStringLiteral("D15d cancel drops the queue and the session in hand"), !other.running() && other.report().written == 0
              && other.report().interrupted == 1);
}

// ── JOB / PER / ASOF / CNT / OUT (refactor 3.7-f) ─────────────────────────────────────────────────────────────
static void testHomeJobs(bool onPool)
{
    std::printf("\nJOB / PER / ASOF / CNT / OUT (%s) — Home's jobs on the athlete level\n", onPool ? "on the pool" : "inline");
    const QString tag = onPool ? QStringLiteral(" (pool)") : QStringLiteral(" (inline)");
    Rig rig(/*diagnostics=*/true, onPool);
    const QString s1 = rig.legacySession(1, 2), s2 = rig.legacySession(2, 2), s3 = rig.legacySession(3, 2);
    AthleteCatchUp catchUp(rig.callables());
    catchUp.run(kAthlete);
    pump([&catchUp] { return !catchUp.running(); });
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    AthleteDiagnostics *d = AthleteDiagnostics::of(rig.athlete.get());
    d->markStale();
    const auto land = [&](AthleteDiagnostics::Ask a, const std::function<bool()> &done) {
        if (a == AthleteDiagnostics::Ask::Running) return pump(done, 120000);
        return a == AthleteDiagnostics::Ask::Landed;
    };

    // JOB: the asks land what the lazy reads give.
    const analysis::TendencySet lazy = d->tendencies();
    const bool t1 = land(d->askTendencies(), [d] { return !d->tendenciesRunning(); });
    check(QStringLiteral("JOBa the tendencies asked land what the lazy read gives (%1 shots)").arg(d->landedTendencies().countedShots) + tag,
          t1 && d->tendenciesReady() && analysis::tendencySetToJson(d->landedTendencies()) == analysis::tendencySetToJson(lazy));
    check(QStringLiteral("JOBb asked again with nothing moved: Current, nothing reduced") + tag,
          d->askTendencies() == AthleteDiagnostics::Ask::Current && d->counts().tendencyJobs == 1);
    const analysis::SwingThemes lazyThemes = d->themes();
    if (rig.pool) rig.pool->waitForDone(30000);
    const int lanesBefore = rig.pool ? rig.pool->laneCount() : 0;
    const bool h1 = land(d->askThemes(), [d] { return !d->themesRunning(); });
    check(QStringLiteral("JOBc the themes asked land what the lazy read gives (%1 swings)").arg(d->landedThemes().allSwings) + tag,
          h1 && d->themesReady() && analysis::toJson(d->landedThemes()) == analysis::toJson(lazyThemes) && d->counts().themeJobs == 1);
    if (rig.pool) {
        // LANE (REVIEW-f R1-m2): the replicates' fan-out lets its helper lanes go once it closes (failed before: one empty
        // lane left in the pool for good per helper, per reduction).
        rig.pool->waitForDone(30000);
        const int lanesAfter = rig.pool->laneCount();
        check(QStringLiteral("LANEa the themes' fan-out leaves no lane behind in the pool (%1 lanes before, %2 after)")
                  .arg(lanesBefore).arg(lanesAfter) + tag,
              lanesAfter == lanesBefore);
    }
    std::vector<AthleteDiagnostics::ConsistencyRequest> reqs;
    for (const analysis::WorkOnSummary &w : d->conditions())
        reqs.push_back({ w.id, w.hasLatestEvidence ? w.latestEvidence.measureId : (w.hasLatest ? w.latest.measureId : QString()) });
    const bool m1 = land(d->askConsistency(analysis::ClubScope(), reqs), [d] { return !d->consistencyRunning(); });
    check(QStringLiteral("JOBd the marks are graded from the summaries' facts (%1 requests, %2 graded, %3 sessions read)")
              .arg(reqs.size()).arg(d->consistency().size()).arg(d->counts().consistencySessions) + tag,
          m1 && d->counts().consistencyJobs == 1 && d->counts().consistencySessions == 3 && !reqs.empty());
    check(QStringLiteral("JOBe … and asked again with nothing moved, Current") + tag,
          d->askConsistency(analysis::ClubScope(), reqs) == AthleteDiagnostics::Ask::Current);
    {
        // JOBg (REVIEW-f R1-m1): the same ask while it runs is not queued twice — Running, one job.
        analysis::ClubScope seven;
        seven.key     = QStringLiteral("club:7 IRON");
        seven.clubIds = { canonicalClubId(QStringLiteral("7 IRON")) };
        const quint64 jobs = d->counts().consistencyJobs, asked = d->counts().jobsAsked;
        const AthleteDiagnostics::Ask first = d->askConsistency(seven, reqs);
        const AthleteDiagnostics::Ask second = d->askConsistency(seven, reqs);
        const quint64 queued = d->counts().jobsAsked - asked;
        const bool landed = land(first, [d] { return !d->consistencyRunning(); });
        check(QStringLiteral("JOBg the same marks ask while it runs answers Running (or Current once landed): one job queued, "
                             "not two (%1)").arg(queued) + tag,
              landed && queued == 1 && d->counts().consistencyJobs == jobs + 1
                  && (onPool ? second == AthleteDiagnostics::Ask::Running : second == AthleteDiagnostics::Ask::Current));
    }
    analysis::ClubScope none;
    none.key = QStringLiteral("club:PUTTER");
    none.clubIds = { QStringLiteral("PUTTER") };
    const bool m2 = land(d->askConsistency(none, reqs), [d] { return !d->consistencyRunning(); });
    check(QStringLiteral("JOBf a club choice with no shots grades nothing") + tag,
          m2 && d->consistency().isEmpty() && d->consistencyScopeKey() == none.key);

    // PER: the one writer of FILE-ATH-02/03.
    const QJsonObject tend = readJson(AthleteFiles::tendenciesFile(rig.athleteFolder()));
    const QJsonObject them = readJson(AthleteFiles::themesFile(rig.athleteFolder()));
    check(QStringLiteral("PERa tendencies.json and swing_themes.json written by the owner, each stamped with its inputs") + tag,
          tend.value(QStringLiteral("derivedFrom")).toObject().value(QStringLiteral("fingerprint")).toString() == d->landedTendenciesFingerprint()
              && them.value(QStringLiteral("derivedFrom")).toObject().value(QStringLiteral("fingerprint")).toString() == d->landedThemesFingerprint()
              && rig.caches.writes == 2);
    check(QStringLiteral("PERb persist() with nothing new writes nothing") + tag,
          d->persist().status == WriteResult::Status::Unchanged && rig.caches.writes == 2);
    {
        // A fresh owner (a new launch) finds the stored themes current: read, not reduced.
        delete static_cast<QObject *>(d);   // its destructor is the Athlete's to call; through the base, as Qt's own deletes do
        d = AthleteDiagnostics::of(rig.athlete.get());
        const AthleteDiagnostics::Ask a = d->askThemes();
        check(QStringLiteral("PERc a fresh owner reads the current swing_themes.json back, reducing nothing") + tag,
              a == AthleteDiagnostics::Ask::Landed && d->counts().themeCacheReads == 1 && d->counts().themeJobs == 0
                  && analysis::toJson(d->landedThemes()) == analysis::toJson(lazyThemes));
        // A write block is said, and persist() reports it.
        delete static_cast<QObject *>(d);   // its destructor is the Athlete's to call; through the base, as Qt's own deletes do
        d = AthleteDiagnostics::of(rig.athlete.get());
        rig.caches.failWrites = QStringLiteral("the disk is full");
        QStringList said;
        QObject::connect(d, &AthleteDiagnostics::writeFailed, [&said](const QString &what, const QString &why) { said << what + QStringLiteral(": ") + why; });
        land(d->askTendencies(), [d] { return !d->tendenciesRunning(); });
        const WriteResult again = d->persist();
        check(QStringLiteral("PERd a failed write of tendencies.json is said (%1) and persist() reports it").arg(said.join(QStringLiteral("; "))) + tag,
              said.size() == 1 && said.first().contains(QStringLiteral("tendencies")) && !again.ok());
        rig.caches.failWrites.clear();
        check(QStringLiteral("PERe … and written once it can be") + tag, d->persist().ok() && rig.caches.writes == 3);

        // PERf (refactor 4-e; R-PERS-3): a cache a NEWER build saved is read-only here — the library is shared between
        // machines that may run different builds. A fresh owner computes both and persists; neither file is touched.
        const QString tFile = AthleteFiles::tendenciesFile(rig.athleteFolder()), hFile = AthleteFiles::themesFile(rig.athleteFolder());
        QJsonObject tNew = readJson(tFile), hNew = readJson(hFile);
        const QJsonObject tWas = tNew, hWas = hNew;
        tNew[QStringLiteral("schema")]        = analysis::kTendencySchemaVersion + 1;
        hNew[QStringLiteral("schemaVersion")] = analysis::kThemeSchemaVersion + 1;
        writeJson(tFile, tNew);
        writeJson(hFile, hNew);
        const auto bytes = [](const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); };
        const QByteArray tBytes = bytes(tFile), hBytes = bytes(hFile);
        delete static_cast<QObject *>(d);
        d = AthleteDiagnostics::of(rig.athlete.get());
        const int writesBefore = rig.caches.writes;
        const bool tLanded = land(d->askTendencies(), [d] { return !d->tendenciesRunning(); });
        const bool hLanded = land(d->askThemes(), [d] { return !d->themesRunning(); });
        d->persist();
        check(QStringLiteral("PERf (4-e) a newer build's tendencies.json and swing_themes.json are left byte-identical, "
                             "nothing written over them (%1 kept)").arg(d->counts().cacheNewerKept) + tag,
              tLanded && hLanded && d->tendenciesReady() && d->themesReady() && bytes(tFile) == tBytes && bytes(hFile) == hBytes
                  && rig.caches.writes == writesBefore && d->counts().cacheNewerKept >= 2);
        writeJson(tFile, tWas);   // back to this build's, for what follows
        writeJson(hFile, hWas);
    }

    // ASOF: as of each session.
    const auto asOf = [&](const QString &folder) { return d->tendenciesAsOf(rig.idOf(folder)); };
    const analysis::TendencySet a1 = asOf(s1), a3 = asOf(s3);
    check(QStringLiteral("ASOFa as of the first session: its shots alone (%1 over %2)").arg(a1.countedShots).arg(a1.countedSessions) + tag,
          a1.countedSessions <= 1 && a1.countedShots <= 2);
    check(QStringLiteral("ASOFb as of the last: every session's, the athlete's own tendencies (%1 shots)").arg(a3.countedShots) + tag,
          analysis::tendencySetToJson(a3) == analysis::tendencySetToJson(d->tendencies()));

    // SCP (DEC-195): a club scope's views from each session's judged rows, read through that session's own SessionDiagnostics
    // (the Session held for the read and released after), stored by the owner and read back by a fresh one.
    {
        QStringList clubs;
        for (const auto &r : d->records()) clubs += r.clubs;
        const std::vector<analysis::ClubScopeGroupOption> options = analysis::clubScopeOptions(clubs, [](const QString &) { return QStringLiteral("mid_irons"); });
        const analysis::ClubScope scope = analysis::resolveClubScope(
            options.empty() || options.front().clubs.empty() ? QString() : options.front().clubs.front().value, options);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const bool s1ok = land(d->askScoped(scope, analysis::ShotFilter(), false), [d] { return !d->scopedRunning(); });
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const AthleteLevel::ScopedResult &r = d->scopedResult();
        check(QStringLiteral("SCPa a club choice (%1): every session's record and the summary from its rows (%2 sessions, %3 swings), "
                             "the sessions loaded for it released").arg(scope.key).arg(r.records.size()).arg(r.themes.allSwings) + tag,
              s1ok && !scope.isAll() && r.key == scope.key && r.records.size() == 3 && r.themes.allSwings == 6
                  && d->rowsMissing().isEmpty() && rig.athlete->loadedSessions().isEmpty());
        check(QStringLiteral("SCPb asked again with nothing moved: Current") + tag,
              d->askScoped(scope, analysis::ShotFilter(), false) == AthleteDiagnostics::Ask::Current);
        check(QStringLiteral("SCPc its file is the owner's: swing_themes.<club>.json, with its records") + tag,
              readJson(AthleteFiles::themesFile(rig.athleteFolder(), { analysis::clubScopeFileKey(scope.key) }))
                      .value(QStringLiteral("records")).toArray().size() == 3);
        delete static_cast<QObject *>(d);
        d = AthleteDiagnostics::of(rig.athlete.get());
        const quint64 reads = d->counts().themeCacheReads;
        check(QStringLiteral("SCPd a fresh owner reads the current choice back, loading no session") + tag,
              d->askScoped(scope, analysis::ShotFilter(), false) == AthleteDiagnostics::Ask::Landed
                  && d->counts().themeCacheReads == reads + 1 && d->scopedResult().records.size() == 3
                  && rig.athlete->loadedSessions().isEmpty());

        // SCPe (REVIEW-f R1-B2): a session the pass loaded that another holder has a use on at let-go (the catch-up, a
        // report make, Review — none releases what it did not load) is released once that use drops, not left loaded for
        // the run. (Failed before the fix: still loaded after the use was dropped.)
        QFile::remove(AthleteFiles::themesFile(rig.athleteFolder(), { analysis::clubScopeFileKey(scope.key) }));
        delete static_cast<QObject *>(d);
        d = AthleteDiagnostics::of(rig.athlete.get());
        d->setReleaseRetryMsForTest(20);
        const SessionId first = rig.idOf(s1);
        QPointer<Session> other;
        const QMetaObject::Connection hold = QObject::connect(rig.athlete.get(), &Athlete::sessionHeld, [&other, first](Session *s) {
            if (s && s->id() == first && !other) { s->addUse(); other = s; }   // another holder takes it as the pass loads it
        });
        const bool s2ok = land(d->askScoped(scope, analysis::ShotFilter(), false), [d] { return !d->scopedRunning(); });
        QObject::disconnect(hold);
        const bool keptForHolder = other && rig.athlete->loadedSession(first) && d->releasesOwed() == 1;
        if (other) other->dropUse();
        const bool released = pump([&] { return !rig.athlete->loadedSession(first); }, 10000);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        check(QStringLiteral("SCPe a session the pass loaded, held by another use at let-go, is released once that use drops") + tag,
              s2ok && d->scopedResult().records.size() == 3 && keptForHolder && released && d->releasesOwed() == 0
                  && rig.athlete->loadedSessions().isEmpty());

        // SCPf (REVIEW-e B1; R-PERS-3): a scoped file a NEWER build saved — a record's work-on rule, or its outcome block's
        // rule, above this build's (the themes' own versions unchanged) — is shown recomputed and left byte-identical.
        const QString scopedFile = AthleteFiles::themesFile(rig.athleteFolder(), { analysis::clubScopeFileKey(scope.key) });
        const QJsonObject scopedWas = readJson(scopedFile);
        const auto scopedBytes = [&scopedFile] { QFile f(scopedFile); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); };
        for (int variant = 0; variant < 2; ++variant) {
            QJsonObject newerScoped = scopedWas;
            if (variant == 0) {
                QJsonArray recs = newerScoped.value(QStringLiteral("records")).toArray();
                QJsonObject r0 = recs.isEmpty() ? QJsonObject() : recs.at(0).toObject();
                r0[QStringLiteral("ruleVersion")] = analysis::kWorkOnRuleVersion + 1;
                if (!recs.isEmpty()) recs.replace(0, r0);
                newerScoped[QStringLiteral("records")] = recs;
            } else {
                newerScoped[QStringLiteral("outcome")] = QJsonObject{ { QStringLiteral("rule"), analysis::kOutcomeScopeRuleVersion + 1 } };
                // Under every shot the reader would take it back as current (it reads no outcome block): a moved
                // fingerprint makes this owner recompute, so the write is tried.
                QJsonObject from = newerScoped.value(QStringLiteral("derivedFrom")).toObject();
                from[QStringLiteral("fingerprint")] = QStringLiteral("a newer build's");
                newerScoped[QStringLiteral("derivedFrom")] = from;
            }
            writeJson(scopedFile, newerScoped);
            const QByteArray before = scopedBytes();
            delete static_cast<QObject *>(d);
            d = AthleteDiagnostics::of(rig.athlete.get());
            const quint64 kept = d->counts().cacheNewerKept;
            const bool landedNewer = land(d->askScoped(scope, analysis::ShotFilter(), false), [d] { return !d->scopedRunning(); });
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            check(QStringLiteral("SCPf (REVIEW-e B1) a newer build's %1 in swing_themes.<club>.json: recomputed for the screen, "
                                 "the file byte-identical").arg(variant == 0 ? QStringLiteral("record rule") : QStringLiteral("outcome rule")) + tag,
                  landedNewer && d->scopedResult().records.size() == 3 && scopedBytes() == before && d->counts().cacheNewerKept > kept);
        }
        writeJson(scopedFile, scopedWas);   // back to this build's, for what follows
    }

    // CNT: a session whose summary this build does not read is counted.
    check(QStringLiteral("CNTa every session summed up: none counted") + tag, d->notSummedUp().count == 0);
    QJsonObject old = readJson(SessionFiles::workOnsFile(s2));
    old[QStringLiteral("ruleVersion")] = old.value(QStringLiteral("ruleVersion")).toInt() - 1;
    writeJson(SessionFiles::workOnsFile(s2), old);
    d->rereadAll();   // OUT: a file written outside this process
    const AthleteDiagnostics::NotSummedUp n = d->notSummedUp();
    check(QStringLiteral("CNTb / OUTa an older rule's summary written outside is read again, and counted (%1: %2)")
              .arg(n.count).arg(n.folders.join(QStringLiteral(", "))) + tag,
          n.count == 1 && n.folders == QStringList{ QFileInfo(s2).fileName() } && d->summaryOf(QFileInfo(s2).fileName()) == nullptr
              && d->records().size() == 2);
    check(QStringLiteral("CNTc (4-e) an older rule's is stale, the catch-up's: none said to be a newer build's") + tag,
          n.stale() == 1 && n.newer() == 0 && n.newerFolders.isEmpty());

    // CNTd/e (refactor 4-e): a summary a NEWER build saved is told apart from a stale one, by the owner.
    QJsonObject newer = readJson(SessionFiles::workOnsFile(s3));
    newer[QStringLiteral("schemaVersion")] = newer.value(QStringLiteral("schemaVersion")).toInt() + 1;
    writeJson(SessionFiles::workOnsFile(s3), newer);
    d->rereadAll();
    const AthleteDiagnostics::NotSummedUp m = d->notSummedUp();
    check(QStringLiteral("CNTd (4-e) both at once: a newer schema's is the newer build's (%1), the older rule's stale (%2 of %3)")
              .arg(m.newerFolders.join(QStringLiteral(", "))).arg(m.stale()).arg(m.count) + tag,
          m.count == 2 && m.newerFolders == QStringList{ QFileInfo(s3).fileName() } && m.newer() == 1 && m.stale() == 1
              && m.folders.contains(QFileInfo(s2).fileName()));
    // A newer results version reads as this build's work-ons (read) without its per-shot facts: counted, and a newer build's.
    QJsonObject grown = readJson(SessionFiles::workOnsFile(s1));
    grown[QStringLiteral("resultsVersion")] = analysis::kSessionResultsVersion + 1;
    writeJson(SessionFiles::workOnsFile(s1), grown);
    QJsonObject older = readJson(SessionFiles::workOnsFile(s3));
    older[QStringLiteral("schemaVersion")] = newer.value(QStringLiteral("schemaVersion")).toInt() - 1;   // back to today's
    writeJson(SessionFiles::workOnsFile(s3), older);
    d->rereadAll();
    const AthleteDiagnostics::NotSummedUp g = d->notSummedUp();
    check(QStringLiteral("CNTe (4-e) a newer results version (read, no facts) is a newer build's; s3 back to today's is used") + tag,
          g.count == 2 && g.newerFolders == QStringList{ QFileInfo(s1).fileName() } && g.stale() == 1
              && d->summaryOf(QFileInfo(s1).fileName()) != nullptr && !g.folders.contains(QFileInfo(s3).fileName()));

    // CNTf (refactor 4-e; R-PERS-3): THE CATCH-UP NEVER WRITES OVER A NEWER BUILD'S SUMMARY. s2's work_ons.json is made a
    // newer schema's (its diagnostics.json stays this build's, so the session is taken and written as usual); a full catch-up
    // leaves it byte-identical, and the owner reports it as a newer build's. (Failed before the fix: rewritten as today's.)
    grown[QStringLiteral("resultsVersion")] = analysis::kSessionResultsVersion;   // s1 back to today's (CNTe made it newer)
    writeJson(SessionFiles::workOnsFile(s1), grown);
    QJsonObject s2New = readJson(SessionFiles::workOnsFile(s2));
    s2New[QStringLiteral("schemaVersion")] = analysis::kWorkOnSchemaVersion + 1;
    s2New[QStringLiteral("ruleVersion")]   = analysis::kWorkOnRuleVersion;
    writeJson(SessionFiles::workOnsFile(s2), s2New);
    const auto fileBytes = [](const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); };
    const QByteArray s2Bytes = fileBytes(SessionFiles::workOnsFile(s2));
    const QByteArray s2Diag  = fileBytes(SessionFiles::diagnosticsFile(s2));
    const qint64     s2DiagMtime = QFileInfo(SessionFiles::diagnosticsFile(s2)).lastModified().toMSecsSinceEpoch();
    // CNTg (REVIEW-e B2): s3's summary is this build's, its LEDGER a newer build's (ledger.schemaVersion above this build's,
    // the envelope unchanged): taken, read, and never written over.
    QJsonObject s3Diag = readJson(SessionFiles::diagnosticsFile(s3));
    {
        QJsonObject ledger = s3Diag.value(QStringLiteral("ledger")).toObject();
        ledger[QStringLiteral("schemaVersion")] = analysis::kDiagSchemaVersion + 1;
        s3Diag[QStringLiteral("ledger")] = ledger;
        writeJson(SessionFiles::diagnosticsFile(s3), s3Diag);
    }
    const QByteArray s3DiagBytes = fileBytes(SessionFiles::diagnosticsFile(s3));
    QThread::msleep(20);   // an mtime moved by a write would show
    {
        AthleteCatchUp again(rig.callables());
        again.run(kAthlete);
        pump([&again] { return !again.running(); });
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        std::printf("        CNTf: %d taken, %d written, %d blocked\n", again.report().sessions, again.report().written,
                    again.report().blocked);
        d->rereadAll();
        const AthleteDiagnostics::NotSummedUp k = d->notSummedUp();
        check(QStringLiteral("CNTf (4-e) a full catch-up leaves a newer build's work_ons.json byte-identical, and it is reported "
                             "as a newer build's (%1 of %2 left out); the others are summed up").arg(k.newer()).arg(k.count) + tag,
              fileBytes(SessionFiles::workOnsFile(s2)) == s2Bytes
                  && k.newerFolders == QStringList{ QFileInfo(s2).fileName() } && k.count == 1 && k.stale() == 0);
        const bool declined = std::any_of(again.report().why.begin(), again.report().why.end(), [](const QString &w) {
            return w.contains(QStringLiteral("newer build's"));
        });
        check(QStringLiteral("CNTh (REVIEW-e §5) a newer build's session is not taken (%1 of 3 taken): its diagnostics.json is "
                             "byte-identical, its mtime unchanged").arg(again.report().sessions) + tag,
              again.report().sessions == 2 && declined && fileBytes(SessionFiles::diagnosticsFile(s2)) == s2Diag
                  && QFileInfo(SessionFiles::diagnosticsFile(s2)).lastModified().toMSecsSinceEpoch() == s2DiagMtime);
        check(QStringLiteral("CNTg (REVIEW-e B2) a newer ledger (ledger.schemaVersion %1 > %2) is left byte-identical by the session "
                             "that read it").arg(analysis::kDiagSchemaVersion + 1).arg(analysis::kDiagSchemaVersion) + tag,
              fileBytes(SessionFiles::diagnosticsFile(s3)) == s3DiagBytes);
    }
}

// ── RDA (REVIEW-f R1-m9 a; J-57): the summaries read on the owner's lane, applied on the GUI thread, latest wins ──────
static void testReadAhead(bool onPool)
{
    std::printf("\nRDA (%s) — the summaries read off the GUI thread\n", onPool ? "on the pool" : "inline");
    const QString tag = onPool ? QStringLiteral(" (pool)") : QStringLiteral(" (inline)");
    Rig rig(/*diagnostics=*/true, onPool);
    const QString s1 = rig.legacySession(1, 2), s2 = rig.legacySession(2, 2), s3 = rig.legacySession(3, 2);
    AthleteCatchUp catchUp(rig.callables());
    catchUp.run(kAthlete);
    pump([&catchUp] { return !catchUp.running(); });
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    AthleteDiagnostics *d = AthleteDiagnostics::of(rig.athlete.get());
    const QByteArray syncRecords = [&] {   // what a synchronous read gives: the reference
        QJsonObject o;
        const QHash<QString, analysis::SessionWorkOns> recs = d->records();
        for (auto it = recs.constBegin(); it != recs.constEnd(); ++it) o.insert(it.key(), QString::number(it->shotCount));
        return QJsonDocument(o).toJson(QJsonDocument::Compact) + QByteArray::number(int(d->distances().size()));
    }();
    d->rereadAll();
    int changed = 0;
    QObject::connect(d, &AthleteDiagnostics::changed, [&changed] { ++changed; });
    const int guiReads = rig.spy->workOnsReads;
    const quint64 asked = d->counts().jobsAsked;
    const AthleteDiagnostics::Ask a = d->readAhead();
    if (!onPool) {
        check(QStringLiteral("RDAa inline doors: read here at once (Current), nothing stale") + tag,
              a == AthleteDiagnostics::Ask::Current && !d->stale());
        return;
    }
    const bool outAtAsk = a == AthleteDiagnostics::Ask::Running && d->stale() && d->summariesReading();
    check(QStringLiteral("RDAa asked: Running, the summaries out on the lane, no file read on the GUI thread") + tag,
          outAtAsk && rig.spy->workOnsReads == guiReads);
    check(QStringLiteral("RDAb asked again while it runs: Running, not queued twice") + tag,
          d->readAhead() == AthleteDiagnostics::Ask::Running && d->counts().jobsAsked == asked + 1);
    d->markStale(rig.idOf(s2));   // stale again before it lands: its result is not taken, and it asks again
    const bool landed = pump([d] { return !d->summariesReading() && !d->stale(); }, 30000);
    const QByteArray asyncRecords = [&] {
        QJsonObject o;
        const QHash<QString, analysis::SessionWorkOns> recs = d->records();
        for (auto it = recs.constBegin(); it != recs.constEnd(); ++it) o.insert(it.key(), QString::number(it->shotCount));
        return QJsonDocument(o).toJson(QJsonDocument::Compact) + QByteArray::number(int(d->distances().size()));
    }();
    bool offGui = false;
    { std::lock_guard<std::mutex> g(rig.spy->threadsLock);
      offGui = !rig.spy->deferredThreads.isEmpty() && !rig.spy->deferredThreads.contains(QThread::currentThreadId()); }
    std::printf("        RDA: %d deferred read(s), %llu applied, ask %lld ms, apply %lld ms, %d changed\n",
                rig.spy->deferredReads.load(), (unsigned long long)d->counts().summariesReadAhead,
                (long long)d->counts().readAheadAskMs, (long long)d->counts().readAheadApplyMs, changed);
    check(QStringLiteral("RDAc landed: every summary read on a pool thread, none on the GUI thread, then changed()") + tag,
          landed && offGui && rig.spy->workOnsReads == guiReads && changed >= 1 && d->counts().readAheadApplyMs >= 0);
    check(QStringLiteral("RDAd latest wins: the staleness before the landing asked again (two reads, the second taken)") + tag,
          d->counts().jobsAsked >= asked + 2 && d->counts().summariesReadAhead >= 1);
    check(QStringLiteral("RDAe what it applied is what a synchronous read gives (records, distances)") + tag,
          asyncRecords == syncRecords && d->summaries().size() == 3);
    check(QStringLiteral("RDAf read and current: Current, nothing asked") + tag,
          d->readAhead() == AthleteDiagnostics::Ask::Current && !d->summariesReading());
    // RDAg (REVIEW-f R2-m1): a synchronous read while the lane read is out (a reader that did not wait) leaves nothing to
    // take — but the landing still says changed(), so the reader waiting for it (Home) reads again.
    d->rereadAll();
    const int changedBefore = changed;
    const bool out = d->readAhead() == AthleteDiagnostics::Ask::Running;
    const size_t n = d->summaries().size();   // read here, now
    const bool settled = pump([d] { return !d->summariesReading(); }, 30000);
    check(QStringLiteral("RDAg a synchronous read meanwhile: the landing takes nothing and still says changed() (failed before)") + tag,
          out && n == 3 && settled && !d->stale() && changed > changedBefore);
    Q_UNUSED(s1); Q_UNUSED(s3);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    // This suite's own settings (FRS sets the grade policy, SET-15): never the machine's.
    QTemporaryDir settingsDir;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.filePath(QStringLiteral("settings")));
    pinpoint::installProcessRuleData();
    std::printf("athlete_diagnostics_test\n");
    testPraise();
    testBoard();
    testSummariesOnlyAndLazy();
    testCatchUpAndFreeze();
    testPauseAndCancel();
    testBlockAtRelease();
    testHomeJobs(/*onPool=*/false);
    testHomeJobs(/*onPool=*/true);
    testReadAhead(/*onPool=*/false);
    testReadAhead(/*onPool=*/true);
    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
