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

// THE SESSION SUMMARY'S GATE COUNTS (platform refactor P-3b; information_model.md FILE-SESS-05 `gates`; the Solution
// Architect's approval, build/platform/JUDGEMENT-CALLS.md PR-21). Over the REAL session store, swing door, Athletes,
// Sessions, Shots and SessionDiagnostics, the real firing reader and the process's RuleData over the shipped content, on a
// temporary library, headless (synchronous diagnostics). Never the real library or a real store: IsolatedStores (#187) is
// main()'s first line.
//
//   W   the writer: a session of four swings — one analysed before records existed, two gatesVersions, a gate named twice
//       on one swing — has its block written by its SessionDiagnostics with exactly the counts the records give; and the
//       same block again when its results are loaded from the file (the grids read on the reduction's lane)
//   R   the reader: SessionIndexSummaries::read and Athlete::deferredSummary give the block back; a summary without it,
//       and no summary, read as not summarised yet (`has` false); the JSON's own round trip
//   S   the scan: the sum over two athletes' session indexes (read as the panel's worker reads them: the athlete repository's
//       readIndexWithSummaries, each summary through deferredSummary) equals the library walk on the same library — with every
//       session summarised (none read swing by swing), with one not summarised (exactly one read swing by swing), and
//       with none summarised
//
//   cmake --build build/tests --target session_gates_test -j 3
//   ctest --test-dir build/tests -R '^session_gates_test$' --output-on-failure

#include "../athlete.h"
#include "../session.h"
#include "../session_diagnostics.h"
#include "../shot.h"
#include "../../Core/work_pool.h"
#include "../../Core/tests/isolated_stores.h"   // pinpoint::test::IsolatedStores (#187)
#include "../../Coaching/session_gates.h"
#include "../../Coaching/session_results.h"
#include "../../Export/firing_reader_store.h"
#include "../../Export/recording_loader.h"
#include "../../Export/rule_data_install.h"
#include "../../Export/athlete_store.h"
#include "../../Export/session_index_summary.h"
#include "../../Export/session_store.h"
#include "../../Export/swing_store_repository.h"
#include "../../Gui/diagnosticmodel/gate_count_index.h"
#include "../../Gui/diagnosticmodel/gate_count_reads.h"
#include "../../Export/phase_grid_store.h"   // the folder read's door, for this suite's own definition of it (below)

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>
#include <functional>
#include <memory>
#include <thread>

using namespace pinpoint;

// The gate count's fallback reads a swing by folder through corridor_grid_read.h, which model_browser.cpp defines; this
// suite compiles gate_count_reads.h without the model browser, so it defines the read itself: the same one line onto the
// grid's file door (B1.14, R11).
analysis::SwingPhaseGrid diagmodel::readPhaseGrid(const QString &swingDir, bool writeSidecar,
                                                 const analysis::PhaseGridConfig &cfg)
{
    return PhaseGridStore::read(swingDir, sidecarPolicyOf(writeSidecar), cfg);
}

static int g_fail = 0;
static int g_run  = 0;

static void check(const QString &label, bool ok)
{
    ++g_run;
    if (!ok) ++g_fail;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", qPrintable(label));
}

static const QString kPat = QStringLiteral("date-name-type");

static QString fixture(const char *name)
{
    return QDir(QLatin1String(PP_LIVE_SWINGS_DIR)).filePath(QLatin1String(name) + QLatin1String("/swing.json"));
}

static bool pump(const std::function<bool()> &done, int ms = 20000)
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

// One swing's gate records, as the analysis writes them (schema 8): gatesVersion 0 is "before records existed".
struct Gates {
    int         version = 0;
    QStringList gates;   // one record per entry (a gate may be named twice)
};

// lm_7iron's document with `g` laid into its analysis.
static bool writeDocument(const QString &swingFolder, const Gates &g)
{
    QFile in(fixture("lm_7iron"));
    if (!in.open(QIODevice::ReadOnly)) return false;
    QJsonObject root = QJsonDocument::fromJson(in.readAll()).object();
    QJsonObject analysis = root.value(QStringLiteral("analysis")).toObject();
    if (g.version > 0) {
        analysis.insert(QStringLiteral("gatesVersion"), g.version);
        QJsonArray records;
        int i = 0;
        for (const QString &id : g.gates)
            records.append(QJsonObject{ { QStringLiteral("gate"), id },
                                        { QStringLiteral("subject"), QStringLiteral("subject.%1").arg(++i) },
                                        { QStringLiteral("effect"), QStringLiteral("refused") } });
        analysis.insert(QStringLiteral("gates"), records);
    }
    root.insert(QStringLiteral("analysis"), analysis);
    QFile out(QDir(swingFolder).filePath(QStringLiteral("swing.json")));
    return out.open(QIODevice::WriteOnly) && out.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) > 0;
}

// ── One scratch library: the real store and doors, two athletes ──────────────────────────────────────────────
struct Lib {
    QTemporaryDir                                  dir;
    SessionStore                                   store;
    std::unique_ptr<SwingStoreRecordingRepository> recordings;
    std::shared_ptr<RecordingReader>               reader;
    std::unique_ptr<WorkReceiver>                  receiver;
    std::vector<std::unique_ptr<Athlete>>          athletes;   // destroyed first: their sessions' diagnostics write

    Lib()
    {
        store.setLockWaitForTest(200);
        store.setOsTrashForTest(false);
        store.setSummaryReads(&SessionIndexSummaries::read);   // as main.cpp sets it
        recordings = std::make_unique<SwingStoreRecordingRepository>(store);
        reader     = std::make_shared<SwingStoreRecordingReader>(store);
        receiver   = std::make_unique<WorkReceiver>([this](const WorkAddress &a) {
            for (const auto &x : athletes)
                if (Session *s = x->loadedSession(SessionId::fromString(a.owner))) return diagnosticsWorkTarget(s, a);
            return diagnosticsWorkTarget(nullptr, a);
        });
    }
    ~Lib() { athletes.clear(); }

    Athlete *athlete(const char *uuid, const QString &name)
    {
        const AthleteId id = AthleteId::fromString(QString::fromLatin1(uuid));
        auto a = std::make_unique<Athlete>(nullptr, &store, id);
        AthleteRecord r;
        r.name = name;
        a->setStored(r, dir.path() + QLatin1Char('/') + name, SessionOwner{ id, name, dir.path() }, Athlete::Source::Library,
                     false);
        a->setShotDoors({ recordings.get(), reader });
        DiagnosticsDoors d;
        d.pool            = nullptr;
        d.receiver        = receiver.get();
        d.ruleData        = RuleData::installed();
        d.reader          = std::make_shared<SwingStoreFiringReader>(store, *recordings);
        d.persist         = true;    // this suite's own scratch library only
        d.synchronous     = true;
        d.writeDebounceMs = 0;
        d.attach          = &SessionDiagnostics::attach;
        a->setDiagnosticsDoors(d);
        athletes.push_back(std::move(a));
        return athletes.back().get();
    }

    // An ended session of one swing per entry, its summary written by its SessionDiagnostics.
    SessionId session(Athlete *a, const QVector<Gates> &swings)
    {
        Session *s = a->startSession(QStringLiteral("swing"), kPat);
        if (!s) return {};
        for (const Gates &g : swings) {
            Shot *shot = s->addShot();
            if (!shot || !writeDocument(shot->folder(), g)) return {};
            s->shotDocumentWritten(shot->ref());   // EVT-01
        }
        SessionDiagnostics *d = s->diagnostics();
        if (!d || !pump([d] { return d->outstanding() == 0; })) return {};
        const SessionId id = s->id();
        s->end();
        d->flush(/*final=*/true);
        return id;
    }
    QString folderOf(const SessionId &id) const { const auto e = store.open(id); return e ? e->folder : QString(); }
};

// The count's sessions as the app's source hands them over: the owners taken here; each athlete's index read through the
// athlete repository's worker-safe read, its summaries filled through deferredSummary (what the panel's worker runs).
static QList<diagmodel::GateCountSession> readIndex(const AthleteStore &repo, const QList<SessionOwner> &owners)
{
    QList<SessionIndexEntry> index;
    for (const SessionOwner &o : owners) index += repo.readIndexWithSummaries(o);
    return diagmodel::gateCountSessionsOf(index);
}

static QByteArray gatesJson(const analysis::SessionGates &g)
{
    return QJsonDocument(analysis::sessionGatesToJson(g)).toJson(QJsonDocument::Compact);
}

static analysis::SessionGates writtenGates(Lib &lib, const SessionId &id, bool *ok = nullptr)
{
    bool read = false;
    const analysis::SessionResults r = analysis::sessionResultsFromJson(lib.store.readWorkOns(id).root,
                                                                        analysis::SessionResultsRead::Strict, &read);
    if (ok) *ok = read;
    return r.gates;
}

static bool removeGatesKey(const QString &sessionFolder)
{
    const QString path = QDir(sessionFolder).filePath(QStringLiteral("work_ons.json"));
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) return false;
    QJsonObject root = QJsonDocument::fromJson(in.readAll()).object();
    in.close();
    if (!root.contains(QStringLiteral("gates"))) return false;
    root.remove(QStringLiteral("gates"));
    QFile out(path);
    return out.open(QIODevice::WriteOnly | QIODevice::Truncate) && out.write(QJsonDocument(root).toJson()) > 0;
}

static bool sameCounts(const ModelGateCounts &a, const ModelGateCounts &b, QString *why)
{
    QStringList d;
    if (a.swings != b.swings) d << QStringLiteral("swings %1/%2").arg(a.swings).arg(b.swings);
    if (a.notRecorded != b.notRecorded) d << QStringLiteral("notRecorded %1/%2").arg(a.notRecorded).arg(b.notRecorded);
    if (a.byVersion != b.byVersion) d << QStringLiteral("byVersion");
    if (a.fired != b.fired) d << QStringLiteral("fired");
    if (a.sessions != b.sessions) d << QStringLiteral("sessions");
    if (a.capped != b.capped) d << QStringLiteral("capped");
    if (why) *why = d.join(QStringLiteral(", "));
    return d.isEmpty();
}

// The four swings of the writer's session (W) and the expected block.
static const QVector<Gates> kSessionA = {
    { 0, {} },                                                                     // 1: before records existed
    { 2, { QStringLiteral("g.alpha"), QStringLiteral("g.alpha"), QStringLiteral("g.beta") } },   // 2: alpha named twice
    { 3, { QStringLiteral("g.beta") } },                                           // 3
    { 3, {} },                                                                     // 4: recorded, none fired
};
static const QVector<Gates> kSessionB = {
    { 3, { QStringLiteral("g.beta") } },
    { 3, { QStringLiteral("g.gamma"), QStringLiteral("g.alpha") } },
    { 0, {} },
};

static void testWriterReaderScan()
{
    Lib lib;
    if (!lib.dir.isValid()) { check(QStringLiteral("setup: a temporary library"), false); return; }
    Athlete *a = lib.athlete("5a1d9c3e-2b7f-4e8a-9c10-7d2e6f3b4a21", QStringLiteral("Gates-One"));
    Athlete *b = lib.athlete("0b6f2c4d-8e1a-4f3b-9d27-1c5e7a9b3d42", QStringLiteral("Gates-Two"));

    // ── W: the writer ──────────────────────────────────────────────────────────────────────────────────────
    std::printf("\nW — the writer: SessionDiagnostics writes the summary's gate counts\n");
    const SessionId sa = lib.session(a, kSessionA);
    const SessionId sb = lib.session(b, kSessionB);
    check(QStringLiteral("W0 two ended sessions, their summaries written"), !sa.isNull() && !sb.isNull()
              && lib.store.readWorkOns(sa).exists && lib.store.readWorkOns(sb).exists);
    if (sa.isNull() || sb.isNull()) return;
    bool ok = false;
    const analysis::SessionGates ga = writtenGates(lib, sa, &ok);
    const QMap<int, int> wantVersions{ { 2, 1 }, { 3, 2 } };
    const QMap<QString, QList<int>> wantFired{ { QStringLiteral("g.alpha"), { 2 } }, { QStringLiteral("g.beta"), { 2, 3 } } };
    check(QStringLiteral("W1 the block is there, in this build's summary (resultsVersion %1, unchanged)")
              .arg(lib.store.readWorkOns(sa).root.value(QStringLiteral("resultsVersion")).toInt()),
          ok && ga.has && lib.store.readWorkOns(sa).root.contains(QStringLiteral("gates"))
              && lib.store.readWorkOns(sa).root.value(QStringLiteral("resultsVersion")).toInt() == analysis::kSessionResultsVersion);
    check(QStringLiteral("W2 four swings counted, one with nothing recorded (%1, %2)").arg(ga.swings).arg(ga.notRecorded),
          ga.swings == 4 && ga.notRecorded == 1);
    check(QStringLiteral("W3 the count per gatesVersion: v2 one swing, v3 two"), ga.byVersion == wantVersions);
    check(QStringLiteral("W4 per gate the swings it fired on, each once: alpha [2], beta [2, 3] (%1)")
              .arg(QString::fromUtf8(gatesJson(ga))),
          ga.fired == wantFired);

    // A RESULT LOADED FROM THE FILE: released, its summary removed, held again — the grids read on the reduction's lane.
    const QString folderA = lib.folderOf(sa);
    if (!a->releaseSession(sa)) { check(QStringLiteral("W5 released"), false); return; }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QFile::remove(QDir(folderA).filePath(QStringLiteral("work_ons.json")));
    Session *again = a->session(sa);
    SessionDiagnostics *d2 = again ? again->diagnostics() : nullptr;
    if (!d2 || !pump([d2] { return d2->outstanding() == 0; })) { check(QStringLiteral("W5 held again"), false); return; }
    int fires = 0;   // the asks since it was held: a fire would make the block off the fire's grid, not the lane's read
    for (const auto &ask : d2->asks()) if (!ask.derive) ++fires;
    d2->flushWithSummary();
    check(QStringLiteral("W5 loaded from its file, no shot fired (%1): the same block, the grids read on the reduction's lane").arg(fires),
          fires == 0 && writtenGates(lib, sa).has && gatesJson(writtenGates(lib, sa)) == gatesJson(ga));
    a->releaseSession(sa);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    // ── R: the reader ──────────────────────────────────────────────────────────────────────────────────────
    std::printf("\nR — the reader: the session index's summary carries the block; without it, not summarised yet\n");
    const SessionIndexSummary viaStore = SessionIndexSummaries::read(folderA);
    check(QStringLiteral("R1 SessionIndexSummaries::read gives the block back as written"),
          viaStore.read && viaStore.gates.has && gatesJson(viaStore.gates) == gatesJson(ga));
    std::function<SessionIndexSummary()> deferred;
    for (const SessionIndexEntry &e : a->index())
        if (e.id() == sa) deferred = a->deferredSummary(e.id());
    const SessionIndexSummary viaAthlete = deferred ? deferred() : SessionIndexSummary{};
    check(QStringLiteral("R2 …and so does the Athlete's deferredSummary over its index"),
          viaAthlete.gates.has && gatesJson(viaAthlete.gates) == gatesJson(ga));
    const QString folderB = lib.folderOf(sb);
    const SessionIndexSummary beforeStrip = SessionIndexSummaries::read(folderB);
    const bool stripped = removeGatesKey(folderB);
    const SessionIndexSummary noBlock = SessionIndexSummaries::read(folderB);
    check(QStringLiteral("R3 a summary without the block reads as not summarised yet; its other fields as before"),
          stripped && beforeStrip.gates.has && !noBlock.gates.has && noBlock.workOn.known == beforeStrip.workOn.known
              && noBlock.workOn.id == beforeStrip.workOn.id && noBlock.swings.swings == beforeStrip.swings.swings);
    QTemporaryDir empty;
    const SessionIndexSummary none = SessionIndexSummaries::read(empty.path());
    check(QStringLiteral("R4 no summary at all: not summarised yet"), none.read && !none.gates.has);
    analysis::SessionResults older = analysis::sessionResultsFromJson(lib.store.readWorkOns(sb).root,
                                                                      analysis::SessionResultsRead::Strict, &ok);
    check(QStringLiteral("R5 the typed reader reads the stripped summary in full, without the block"), ok && older.hasGrowth && !older.gates.has);
    const analysis::SessionGates back = analysis::sessionGatesFromJson(analysis::sessionGatesToJson(ga));
    check(QStringLiteral("R6 the block's JSON round trip is exact; a value that is not an object reads as absent"),
          back.has && gatesJson(back) == gatesJson(ga) && !analysis::sessionGatesFromJson(QJsonValue()).has
              && !analysis::sessionGatesFromJson(QJsonValue(3)).has);

    // ── S: the scan ────────────────────────────────────────────────────────────────────────────────────────
    std::printf("\nS — the scan: the sum over two athletes' session indexes equals the library walk\n");
    const QString root = lib.dir.path();
    QString why;
    // The athlete repository over this library, its summary reads set as main.cpp sets them.
    AthleteStore repo([root] { return root; }, &lib.store);
    repo.setSummaryReads(&SessionIndexSummaries::read);
    {
        AthleteStore none([root] { return root; }, &lib.store);   // no summary reads set: no summary, every session falls back
        const ModelGateCounts unset = diagmodel::gateCountsOfIndex(root, readIndex(none, diagmodel::gateCountOwnersOf({ a, b })));
        check(QStringLiteral("S0b a repository with no summary reads set reads no summary: both sessions read swing by swing"),
              unset.summarised == 0 && unset.readSwingBySwing == 2);
    }
    // B's summary written again, with the block (it was stripped above).
    {
        Session *sB = b->session(sb);
        SessionDiagnostics *dB = sB ? sB->diagnostics() : nullptr;
        if (!dB || !pump([dB] { return dB->outstanding() == 0; })) { check(QStringLiteral("S0 B held again"), false); return; }
        dB->flushWithSummary();
        b->releaseSession(sb);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    const ModelGateCounts walk = diagmodel::gateCountsByWalk(root);
    check(QStringLiteral("S0 the walk sees both sessions' seven swings, two with nothing recorded (%1, %2)")
              .arg(walk.swings).arg(walk.notRecorded),
          walk.valid && !walk.indexed && walk.swings == 7 && walk.notRecorded == 2);
    const ModelGateCounts all = diagmodel::gateCountsOfIndex(root, readIndex(repo, diagmodel::gateCountOwnersOf({ a, b })));
    check(QStringLiteral("S1 every session summarised: both from their summaries, none read swing by swing (%1, %2)")
              .arg(all.summarised).arg(all.readSwingBySwing),
          all.valid && all.indexed && all.summarised == 2 && all.readSwingBySwing == 0);
    check(QStringLiteral("S2 …and the sum equals the walk (%1)").arg(why), sameCounts(all, walk, &why));
    {   // the read as the panel runs it: the owners taken on this thread, the index and summaries read on another
        const QList<SessionOwner> owners = diagmodel::gateCountOwnersOf({ a, b });
        ModelGateCounts onWorker;
        std::thread worker([&] {
            onWorker = diagmodel::gateCountsOfIndex(root, readIndex(repo, owners));
        });
        worker.join();
        check(QStringLiteral("S2b read on a worker thread: the same sum; the app's store still holds the session where it was (%1)").arg(why),
              onWorker.summarised == 2 && sameCounts(onWorker, all, &why) && lib.store.open(sa).has_value() && lib.folderOf(sa) == folderA);
    }
    check(QStringLiteral("S3 per gate and session: alpha 1 + 1, beta 2 + 1, gamma 0 + 1"),
          all.fired.value(QStringLiteral("g.alpha")) == 2 && all.fired.value(QStringLiteral("g.beta")) == 3
              && all.fired.value(QStringLiteral("g.gamma")) == 1
              && all.sessions.value(QStringLiteral("g.beta")).value(QFileInfo(folderA).fileName()) == 2
              && all.sessions.value(QStringLiteral("g.beta")).value(QFileInfo(folderB).fileName()) == 1);
    if (!removeGatesKey(folderB)) { check(QStringLiteral("S4 B's block removed"), false); return; }
    const ModelGateCounts mixed = diagmodel::gateCountsOfIndex(root, readIndex(repo, diagmodel::gateCountOwnersOf({ a, b })));
    check(QStringLiteral("S4 one session not summarised: exactly that one read swing by swing (%1, %2)")
              .arg(mixed.summarised).arg(mixed.readSwingBySwing),
          mixed.summarised == 1 && mixed.readSwingBySwing == 1);
    check(QStringLiteral("S5 …and the sum still equals the walk (%1)").arg(why), sameCounts(mixed, walk, &why));
    QFile::remove(QDir(folderA).filePath(QStringLiteral("work_ons.json")));
    QFile::remove(QDir(folderB).filePath(QStringLiteral("work_ons.json")));
    const ModelGateCounts bare = diagmodel::gateCountsOfIndex(root, readIndex(repo, diagmodel::gateCountOwnersOf({ a, b })));
    check(QStringLiteral("S6 no summary at all: both read swing by swing, and the sum is the walk (%1)").arg(why),
          bare.summarised == 0 && bare.readSwingBySwing == 2 && sameCounts(bare, walk, &why));
}

int main(int argc, char **argv)
{
    pinpoint::test::IsolatedStores stores;   // #187: every per-user store points at a scratch folder, first
    QCoreApplication app(argc, argv);
    std::printf("session_gates_test\n");
    RuleData &rules = installProcessRuleData();
    check(QStringLiteral("the shipped pack is read"), rules.snapshot() && rules.snapshot()->pack);
    if (!(rules.snapshot() && rules.snapshot()->pack)) return 1;

    testWriterReaderScan();

    std::printf("\n%d/%d passed\n", g_run - g_fail, g_run);
    return g_fail == 0 ? 0 : 1;
}
