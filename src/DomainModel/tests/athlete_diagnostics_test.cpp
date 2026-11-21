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
//
//   cmake --build build/tests --target athlete_diagnostics_test -j 6
//   ctest --test-dir build/tests -R '^athlete_diagnostics_test$' --output-on-failure

#include "../athlete.h"
#include "../athlete_diagnostics.h"
#include "../session.h"
#include "../session_diagnostics.h"
#include "../shot.h"
#include "../../App/athlete_catch_up.h"
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
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>

#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>

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
            return diagnosticsWorkTarget(athlete ? athlete->loadedSession(SessionId::fromString(a.owner)) : nullptr, a);
        });
        if (async) pool = std::make_unique<WorkPool>(2);
        QDir().mkpath(athleteFolder());
        athlete = std::make_unique<Athlete>(nullptr, spy.get(), kAthlete);
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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    pinpoint::installProcessRuleData();
    std::printf("athlete_diagnostics_test\n");
    testPraise();
    testBoard();
    testSummariesOnlyAndLazy();
    testCatchUpAndFreeze();
    testPauseAndCancel();
    testBlockAtRelease();
    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
