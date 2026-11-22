// athlete_dump — the athlete-level PARITY DUMP (refactor Step G, build/refactor/step-G/BRIEF.md §2 item 3):
// run the app's own work-ons catch-up, headlessly, over one or more athlete folders of a SCRATCH library,
// and write what it leaves behind — <athlete>/tendencies.json, every session's work_ons.json and
// club_baseline.json, and the per-club distances — as one canonical JSON file (parity_json.h: keys sorted,
// numbers "%.17g", no timestamps or cache fingerprints, folders as "<athlete>"/"<session>"), so two runs
// diff byte for byte.
//
// THE CATCH-UP IS THE APP'S (this paragraph is `--engine old`; the default, new, is described below). WorkOnsController
// is the object main.cpp binds to the current athlete; with
// setSynchronous(true) and waitForIdle() it scans and reduces the tendencies, and — since the catch-up's cut-over
// (refactor 3.7-e) — hands each stale session to the catch-up service exactly as main.cpp wires it (its CatchUpDoor over
// AthleteCatchUp::runFolders, here over the headless owners below: each Session's SessionDiagnostics re-judges, freezes
// a club baseline it lacks at first need, reduces and writes the record). Nothing here re-derives anything; the tool only
// reads the files back.
//
// --fresh deletes the CACHES the catch-up would otherwise trust — each session's work_ons.json and the
// athlete's tendencies.json — first, so the records are re-derived by the code under test and not read back
// from a file an older build wrote under the same fingerprint (a code change moves no content stamp). The
// ledgers are not forced: for a parity run, regrade each session with `regrade_ledger --dump` FIRST, then run
// this with --fresh. club_baseline.json is a frozen record, not a cache, and is kept.
//
// SCRATCH ONLY. The catch-up writes into every session it derives (diagnostics.json, work_ons.json,
// club_baseline.json) and into the athlete folder (tendencies.json, swing_themes*.json), so the tool refuses
// any folder whose real path has no `scratch` component: never the live library, never the corpus.
//
// Content comes from the environment, as for regrade_ledger, and EVERY PINPOINT_CORE_* must be set
// (parity_json.h contentVariables()): the tool carries no Qt resources, so a missing flight table would
// read no flight on any shot, silently.
//
//   export PINPOINT_CORE_PACK=src/Resources/diagnostics/core.json   (NORMS, FLIGHT_NORMS, CONTEXTS, SCREENS,
//                                                                    DRILLS, GATE_WORDS, REFERENCES likewise)
//   ./build/tests/Analysis/athlete_dump --fresh --out athlete.json /mnt/swingdata/scratch/<run>/<Athlete>
//
// Shape:
//   { "tool": "athlete_dump", "dumpVersion": 1, "content": { PINPOINT_CORE_<X>: sha1 },
//     "athletes": [ by folder name:
//       { "athlete", "settled", "sessionsFound", "sessionCount", "tendenciesReady",
//         "tendencies":    tendencies.json without its clubDistances (null + "tendenciesWhy" when absent),
//         "clubDistances": tendencies.json's clubDistances (the per-club distances Home shows),
//         "sessions": [ by folder name: { "session", "workOns": work_ons.json, "clubBaseline": club_baseline.json,
//                                         "workOnsWhy"/"clubBaselineWhy" when either is absent } ] } ] }
//
// NOT A TEST, and not registered with ctest — it writes into the folders it is given.
//
// --engine new|old (refactor step 3.6; build/refactor/step-3/3.6/NOTES.md, the athlete parity). The default is new since
// refactor 5.3, as regrade_ledger's; until then it was old.
//   old — the above: WorkOnsController, what Home shows, with the praise rule and Stage 0.5's follow-up
//       wired through it; since 3.7-e its records are the new owners' (its private model is gone), so "old" names Home's
//       reductions from the ledgers (tendencies, themes), not an older engine. Its dump adds `doWell` { themes, praised }:
//       the themes' "what you do well" condition ids before and after the praise rule.
//   new — THE NEW OWNERS: the catch-up service (src/App/athlete_catch_up.h) over the real session store, Athlete,
//       Session and SessionDiagnostics (headless, every shot fired again: DiagnosticsDoors::rebuild; each session's club
//       baseline frozen from its predecessors' summaries when it has none or an older rule's), oldest session first,
//       each released after; then AthleteDiagnostics (src/DomainModel/athlete_diagnostics.h), which reads the summaries
//       only: its tendencies (with the distances, from the club windows), its themes and its athlete work-ons (the
//       summaries' compact per-shot facts: DEC-192 as Mark amended it, 16 Nov "b"). The athlete is the folder's own.
// Under either engine the athlete's id is the one its sessions' session.json files name, read through the session
// repository (refactor 5.3c; NewEngine::athleteOf). A folder that holds sessions but leaves 0 counted is a FAILURE
// (exit 1, with words): until 5.3c a migrated library gave an empty dump that exited 0.
// Both dumps carry `themes` (every club; the stored swing_themes.json read back for old, AthleteDiagnostics' for new,
// each through the one serialiser, its provenance left out).
// Both dumps add `athleteWorkOns` (the athlete work-ons, DEC-138: ids, status and counts — old from the summaries' files
// through the same aggregate, new from AthleteDiagnostics) and, per session, `boardAnnotations` (how many of its shots
// the launch monitor board annotated: Stage 0.5's follow-up has something to change only there).

#include "Gui/diagnostics/work_ons_controller.h"
#include "../../src/App/athlete_catch_up.h"
#include "../../src/DomainModel/athlete.h"
#include "../../src/DomainModel/athlete_diagnostics.h"
#include "../../src/DomainModel/rule_data.h"
#include "../../src/DomainModel/session.h"
#include "../../src/DomainModel/session_diagnostics.h"
#include "../../src/DomainModel/summary_rules.h"
#include "../../src/Core/work_pool.h"
#include "../../src/Diagnostics/packs/swing_themes_pack.h"   // themeMeasureInfo, themeConditionInfo (the bench)
#include "../../src/Export/athlete_cache_repository.h"   // the athlete's own caches (FILE-ATH-02/03), as the app keeps them (3.7-f)
#include "../../src/Export/firing_reader_store.h"
#include "../../src/Export/recording_loader.h"
#include "../../src/Export/rule_data_install.h"
#include "../../src/Export/session_diagnostics_files.h"
#include "../../src/Export/session_store.h"
#include "../../src/Export/swing_store_repository.h"
#include "Core/PpMessageLog.h"
#include "Export/session_files.h"
#include "parity_json.h"

#include <QCryptographicHash>
#include <QSemaphore>
#include <QSet>
#include <QThreadPool>
#include <atomic>
#include <map>
#include <memory>
#include <optional>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <algorithm>
#include <cstdio>

namespace {

void printUsage(FILE *to)
{
    std::fprintf(to,
        "usage: athlete_dump [--engine new|old] [--fresh] [--timeout <seconds>] --out <out.json> <athlete-dir>...\n"
        "\n"
        "Runs the athlete level (by default the catch-up service and the new owners, headless) over each athlete folder of a SCRATCH\n"
        "library and writes the PARITY DUMP: tendencies.json, the per-club distances, and every session's\n"
        "work_ons.json and club_baseline.json, as canonical JSON (keys sorted, %%.17g numbers, no timestamps\n"
        "or fingerprints, folders as <athlete>/<session>), so two runs diff byte for byte.\n"
        "WRITES INTO THE FOLDERS: refuses any whose real path has no 'scratch' component.\n"
        "Each athlete's id is the one its sessions' session.json files name (refused when they name more than one);\n"
        "a folder holding sessions that leaves 0 counted fails the run (exit 1).\n"
        "\n"
        "  --out <file>       where the dump goes (required)\n"
        "  --fresh            delete each session's work_ons.json and the athlete's tendencies.json first, so\n"
        "                     the catch-up re-derives them with this build (for a parity run: regrade_ledger\n"
        "                     --dump each session first, then this with --fresh)\n"
        "  --timeout <s>      how long one athlete's catch-up may take (default 1800)\n"
        "  --engine new|old   new (default): the catch-up service and the new owners (SessionDiagnostics,\n"
        "                     AthleteDiagnostics), every shot fired again; old: WorkOnsController, what Home showed\n"
        "                     before 3.7-f (Home's reductions from the ledgers)\n"
        "  --help             this text\n"
        "\n"
        "Needs every PINPOINT_CORE_* (PACK NORMS FLIGHT_NORMS CONTEXTS SCREENS DRILLS GATE_WORDS REFERENCES).\n");
}

bool onScratch(const QString &dir)
{
    const QString real = QFileInfo(dir).canonicalFilePath();
    return !real.isEmpty() && real.split(QLatin1Char('/'), Qt::SkipEmptyParts).contains(QStringLiteral("scratch"));
}

// The athlete's session folders: the ones holding swings or a ledger, by name.
QStringList sessionDirs(const QString &athleteDir)
{
    QStringList out;
    const QFileInfoList kids = QDir(athleteDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &fi : kids) {
        const QDir d(fi.absoluteFilePath());
        // The session's files by their one owner's names (Export/session_files.h; step 2.5).
        if (QFileInfo::exists(pinpoint::SessionFiles::diagnosticsFile(d.path()))
            || QFileInfo::exists(pinpoint::SessionFiles::workOnsFile(d.path()))
            || !d.entryList({ QStringLiteral("swing_*") }, QDir::Dirs | QDir::NoDotAndDotDot).isEmpty())
            out << fi.absoluteFilePath();
    }
    return out;
}

bool removeIfThere(const QString &path, int *removed)
{
    if (!QFile::exists(path)) return true;
    if (!QFile::remove(path)) {
        std::fprintf(stderr, "athlete_dump: could not remove %s\n", qPrintable(path));
        return false;
    }
    ++*removed;
    return true;
}

// The athlete work-ons (DEC-138) as the dump holds them: ids, status and counts, in the aggregate's order.
QVariantList athleteWorkOnsDump(const std::vector<pinpoint::analysis::WorkOnSummary> &list)
{
    QVariantList out;
    for (const pinpoint::analysis::WorkOnSummary &w : list)
        out.append(QVariantMap{ { QStringLiteral("id"), w.id },
                                { QStringLiteral("status"), pinpoint::analysis::workOnStatusToString(w.status) },
                                { QStringLiteral("score"), w.score },
                                { QStringLiteral("sessionsListed"), w.sessionsListed },
                                { QStringLiteral("sessionsPattern"), w.sessionsPattern },
                                { QStringLiteral("swingsFired"), w.swingsFired },
                                { QStringLiteral("swingsAssessable"), w.swingsAssessable } });
    return out;
}

// The old engine's: the same aggregate over the summaries it wrote, read by today's reader.
QVariantList athleteWorkOnsFromFiles(const QStringList &sessions)
{
    std::vector<pinpoint::analysis::SessionWorkOns> records;
    for (const QString &s : sessions) {
        const pinpoint::JsonFileRead f = pinpoint::SessionDiagnosticsFiles::readWorkOns(s);
        bool ok = false;
        if (f.exists && f.ok) {
            pinpoint::analysis::SessionWorkOns r = pinpoint::analysis::sessionWorkOnsFromJson(f.root, &ok);
            if (ok) records.push_back(std::move(r));
        }
    }
    return athleteWorkOnsDump(pinpoint::analysis::aggregateWorkOns(std::move(records)));
}

// How many of a session's shots the launch monitor board annotated (Stage 0.5's follow-up has only those to change).
int boardAnnotations(const QString &sessionDir)
{
    return int(pinpoint::SessionDiagnosticsFiles::golferInput(sessionDir, QJsonObject()).board.size());
}

// J-53's BENCH: Home's swing summary (every club; the 500 replicates) reduced from the owner's inputs three times on each
// fan-out — WorkOnsController's until 3.7-f (a QThreadPool of every core but one, low priority) and the owner's (a job on
// its own lane of the one pool, WorkPool::defaultThreadCount() workers, the replicates over the pool's other workers) —
// the median of each printed. Scratch folders only, like the rest of the tool.
void benchThemesOf(const QString &name, pinpoint::Athlete *a)
{
    pinpoint::AthleteDiagnostics *ad = pinpoint::AthleteDiagnostics::of(a);
    const auto snap = pinpoint::RuleData::installed()->snapshot();
    if (!ad || !snap || !snap->pack) return;
    const pinpoint::analysis::CharacteristicPack &pack = snap->pack->pack();
    const auto inputs = ad->themeInputs();
    const auto measures = pinpoint::analysis::themeMeasureInfo(pack);
    const auto conditions = pinpoint::analysis::themeConditionInfo(pack, snap->drills ? snap->drills->merged : pinpoint::analysis::DrillSet{});
    QThreadPool old;
    old.setMaxThreadCount(std::max(1, QThread::idealThreadCount() - 1));
    old.setThreadPriority(QThread::LowPriority);
    const pinpoint::analysis::ThemeParallelFor oldFor = [&old](int n, const std::function<void(int)> &body) {
        std::atomic<int> next{ 0 };
        const auto drain = [&]() { for (int i = next.fetch_add(1); i < n; i = next.fetch_add(1)) body(i); };
        const int helpers = std::max(0, std::min(n - 1, old.maxThreadCount()));
        QSemaphore done;
        for (int h = 0; h < helpers; ++h) old.start([&]() { drain(); done.release(); });
        drain();
        done.acquire(helpers);
    };
    pinpoint::WorkPool pool;
    std::vector<qint64> oldMs, laneMs;
    for (int k = 0; k < 3; ++k) {
        QElapsedTimer t;
        t.start();
        (void)pinpoint::analysis::reduceSwingThemes(inputs, measures, conditions, {}, oldFor);
        oldMs.push_back(t.elapsed());
        QSemaphore landed;
        qint64 ms = 0;
        pool.submit(pinpoint::nextWorkNumber(), [&]() {
            QElapsedTimer u;
            u.start();
            (void)pinpoint::analysis::reduceSwingThemes(inputs, measures, conditions, {}, pinpoint::athleteLaneParallelFor(&pool));
            ms = u.elapsed();
            landed.release();
        }, pinpoint::WorkPriority::Low);
        landed.acquire();
        laneMs.push_back(ms);
    }
    std::sort(oldMs.begin(), oldMs.end());
    std::sort(laneMs.begin(), laneMs.end());
    std::printf("%-40s BENCH themes (%d sessions): before %lld ms (QThreadPool %d threads + the caller), after %lld ms (the one "
                "pool, %d workers, its own lane)\n",
                qPrintable(name), int(inputs.size()), (long long)oldMs[1], old.maxThreadCount(), (long long)laneMs[1], pool.threadCount());
}

// THE CONSISTENCY MARKS HOME SHOWS (refactor 3.7-f's parity): each area item's and each listed work-on's mark, by
// "<condition>|<measure>", as the result view QML reads (VariabilityResultView::toMap). Old and new builds dump the same way.
QVariantMap marksDump(const WorkOnsController &c)
{
    QVariantMap out;
    for (const QVariant &a : c.areaItems())
        for (const QVariant &i : a.toMap().value(QStringLiteral("items")).toList()) {
            const QVariantMap m = i.toMap();
            const QString key = m.value(QStringLiteral("conditionId")).toString() + QLatin1Char('|') + m.value(QStringLiteral("measureId")).toString();
            out.insert(key, m.value(QStringLiteral("consistency")).value<pinpoint::variability::VariabilityResultView>().toMap());
        }
    for (const QVariant &v : c.items()) {
        const QString id = v.toMap().value(QStringLiteral("id")).toString();
        out.insert(QStringLiteral("detail:") + id,
                   c.faultDetail(id).value(QStringLiteral("consistency")).value<pinpoint::variability::VariabilityResultView>().toMap());
    }
    return out;
}

// THE SCOPED VIEWS HOME SHOWS (refactor 3.7-f's parity, DEC-195): under the first club group, the first club and, under every
// club, the first direction the golfer's shots show — Your swing's summary, the areas (the scoped records' words), the
// filter's line and note — then back to every club. Old and new builds dump the same way.
QVariantMap scopedDump(WorkOnsController &c, int timeoutMs)
{
    QVariantMap out;
    QStringList keys;
    const QVariantList scopes = c.clubScopes();
    if (!scopes.isEmpty()) {
        const QVariantMap g = scopes.first().toMap();
        keys << g.value(QStringLiteral("value")).toString();
        const QVariantList clubs = g.value(QStringLiteral("clubs")).toList();
        if (!clubs.isEmpty()) keys << clubs.first().toMap().value(QStringLiteral("value")).toString();
    }
    const auto take = [&c, timeoutMs](const QString &label, QVariantMap &into) {
        c.waitForIdle(timeoutMs);
        QVariantMap v{
            { QStringLiteral("focus"),    c.focusItem() },
            { QStringLiteral("doWell"),   c.doWellItems() },
            { QStringLiteral("next"),     c.nextItems() },
            { QStringLiteral("together"), c.togetherItems() },
            { QStringLiteral("togetherNote"), c.togetherNote() },
            { QStringLiteral("subtitle"), c.summarySubtitle() },
            { QStringLiteral("areas"),    [&c]() {
                  // Each mark as the result view's map (a gadget does not travel through the canonical writer).
                  const auto plain = [](QVariantMap m) {
                      m[QStringLiteral("consistency")] = m.value(QStringLiteral("consistency")).value<pinpoint::variability::VariabilityResultView>().toMap();
                      return m;
                  };
                  QVariantList areas;
                  for (const QVariant &a : c.areaItems()) {
                      QVariantMap am = plain(a.toMap());
                      QVariantList items;
                      for (const QVariant &i : am.value(QStringLiteral("items")).toList()) items << plain(i.toMap());
                      am[QStringLiteral("items")] = items;
                      areas << am;
                  }
                  return areas;
              }() },
            { QStringLiteral("line"),     c.shotFilterLine() },
            { QStringLiteral("tooFew"),   c.outcomeTooFewNote() },
            { QStringLiteral("shown"),    c.clubScope() + QLatin1Char('|') + c.shotFilter() },
        };
        into.insert(label, v);
    };
    for (const QString &k : std::as_const(keys)) {
        if (k.isEmpty()) continue;
        c.setClubScope(k);
        take(QStringLiteral("club:") + k, out);
    }
    c.setClubScope(QStringLiteral("all"));
    c.waitForIdle(timeoutMs);
    const QVariantList directions = c.shotFilterOptions().value(QStringLiteral("direction")).toList();
    QString dir;
    for (const QVariant &d : directions) {
        const QString v = d.toMap().value(QStringLiteral("value")).toString();
        if (v != QLatin1String("any") && d.toMap().value(QStringLiteral("count")).toInt() > 0) { dir = v; break; }
    }
    if (!dir.isEmpty()) {
        c.setShotFilter(QStringLiteral("direction:") + dir);
        take(QStringLiteral("filter:direction:") + dir, out);
        c.setShotFilter(QStringLiteral("all"));
        c.waitForIdle(timeoutMs);
    }
    return out;
}

// ── --engine new: the catch-up service and the new owners, headless, over a scratch library ───────────────────────
class NewEngine
{
public:
    NewEngine()
    {
        m_store.setOsTrashForTest(false);   // a scratch library: nothing goes to the real Trash
        m_recordings = std::make_unique<pinpoint::SwingStoreRecordingRepository>(m_store);
        m_reader     = std::make_shared<pinpoint::SwingStoreRecordingReader>(m_store);
        m_firing     = std::make_shared<pinpoint::SwingStoreFiringReader>(m_store, *m_recordings);
        m_receiver   = std::make_unique<pinpoint::WorkReceiver>([this](const pinpoint::WorkAddress &a) {
            const pinpoint::SessionId id = pinpoint::SessionId::fromString(a.owner);
            for (auto &it : m_athletes)
                if (pinpoint::Session *s = it.second->loadedSession(id)) return pinpoint::diagnosticsWorkTarget(s, a);
            return pinpoint::diagnosticsWorkTarget(nullptr, a);
        });
    }
    ~NewEngine() { m_athletes.clear(); }

    // The athlete of a folder, made once (no record: the folder's name, no cohort facts). ITS ID IS THE ONE ITS SESSIONS'
    // OWN FILES NAME (refactor 5.3c; REVIEW-5.3 R2): each session folder's session.json (FILE-SESS-01) read through the
    // session repository, its owner (SessionStore::openFolder, then the index entry's athleteId). Since the 22 Nov
    // migrations that is the athlete.json id; an id made from the folder's name matches none of them, so the catch-up
    // took every session for another athlete's and the dump was empty. Only a folder none of whose sessions has a
    // session file (a library not migrated) keeps the id from the folder's name. Null, with `why`, when the session
    // files name more than one athlete: whose sessions they are is not this tool's to choose. `from` says which.
    pinpoint::Athlete *athleteOf(const QString &dir, QString *from = nullptr, QString *why = nullptr)
    {
        using namespace pinpoint;
        const QString name = QFileInfo(dir).fileName();
        QSet<QString> named;
        for (const QString &s : sessionDirs(dir)) {
            const SessionId sid = m_store.openFolder(s);   // reads only (SessionRepository::openFolder)
            if (sid.isNull()) continue;
            if (const std::optional<SessionIndexEntry> e = m_store.open(sid); e && e->hasSessionFile && !e->athlete.isNull())
                named.insert(e->athlete.toString());
        }
        if (named.size() > 1) {
            QStringList ids = named.values();
            ids.sort();
            if (why)
                *why = QStringLiteral("its sessions' session.json files name %1 athletes (%2): refused, whose they are "
                                      "is not the tool's to choose").arg(ids.size()).arg(ids.join(QStringLiteral(", ")));
            return nullptr;
        }
        const AthleteId id = named.size() == 1
            ? AthleteId::fromString(*named.cbegin())
            : AthleteId::fromString(QStringLiteral("00000000-0000-4000-8000-%1")
                  .arg(QString::fromLatin1(QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha1).toHex().left(12))));
        if (from)
            *from = named.size() == 1 ? QStringLiteral("session.json") : QStringLiteral("the folder's name (no session file)");
        auto it = m_athletes.find(id.toString());
        if (it != m_athletes.end()) return it->second.get();
        auto a = std::make_unique<Athlete>(&m_caches, &m_store, id);
        AthleteRecord rec;
        rec.name = name;
        a->setStored(rec, dir, SessionOwner{ id, name, QFileInfo(dir).absolutePath() }, Athlete::Source::Library, false);
        a->setShotDoors({ m_recordings.get(), m_reader });
        DiagnosticsDoors d;
        d.receiver    = m_receiver.get();
        d.ruleData    = RuleData::installed();
        d.reader      = m_firing;
        d.synchronous = true;    // §4.4 step 11 (R-HL-2)
        d.persist     = true;    // a scratch copy (the tool refuses any other)
        d.rebuild     = true;    // every shot fired again, the ratchet replayed from Cold
        d.attach      = &SessionDiagnostics::attach;
        a->setDiagnosticsDoors(d);
        Athlete *raw = a.get();
        m_athletes.emplace(id.toString(), std::move(a));
        return raw;
    }
    pinpoint::AthleteCallables callables()
    {
        pinpoint::AthleteCallables c;
        c.listAthletes = [this] {
            QList<pinpoint::AthleteId> out;
            for (auto &it : m_athletes) out << it.second->id();
            return out;
        };
        c.loadAthlete = [this](const pinpoint::AthleteId &id) -> pinpoint::Athlete * {
            const auto it = m_athletes.find(id.toString());
            return it == m_athletes.end() ? nullptr : it->second.get();
        };
        return c;
    }

private:
    pinpoint::SessionStore                                      m_store;
    std::unique_ptr<pinpoint::SwingStoreRecordingRepository>    m_recordings;
    std::shared_ptr<pinpoint::SwingStoreRecordingReader>        m_reader;
    std::shared_ptr<pinpoint::SwingStoreFiringReader>           m_firing;
    std::unique_ptr<pinpoint::WorkReceiver>                     m_receiver;
    pinpoint::AthleteCacheRepository                            m_caches;     // tendencies.json, swing_themes.json (3.7-f)
    std::map<QString, std::unique_ptr<pinpoint::Athlete>>       m_athletes;   // destroyed before the receiver
};

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // The packs and norms: this process's one RuleData, over the files the PINPOINT_CORE_* seams name
    // (refactor step 3.2). Made here, on the main thread, before anything grades.
    pinpoint::installProcessRuleData();
    PpMessageLog::instance()->setEchoToStderr(true);

    QString     outPath;
    QString     engine = QStringLiteral("new");   // the default since refactor 5.3
    bool        fresh = false;
    int         timeoutS = 1800;
    bool        benchThemes = false;   // J-53: time Home's swing summary on the old fan-out and on the owner's lane
    QStringList dirs;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--help") || a == QLatin1String("-h")) { printUsage(stdout); return 0; }
        if (a == QLatin1String("--fresh")) { fresh = true; continue; }
        if (a == QLatin1String("--bench-themes")) { benchThemes = true; continue; }
        if (a == QLatin1String("--engine") && i + 1 < argc) { engine = QString::fromLocal8Bit(argv[++i]); continue; }
        if (a == QLatin1String("--out") && i + 1 < argc) { outPath = QString::fromLocal8Bit(argv[++i]); continue; }
        if (a == QLatin1String("--timeout") && i + 1 < argc) { timeoutS = QString::fromLocal8Bit(argv[++i]).toInt(); continue; }
        if (a.startsWith(QLatin1String("--"))) {
            std::fprintf(stderr, "athlete_dump: unknown option %s\n", argv[i]);
            printUsage(stderr);
            return 2;
        }
        dirs << QDir(a).absolutePath();
    }
    if (dirs.isEmpty() || outPath.isEmpty() || timeoutS <= 0
        || (engine != QLatin1String("old") && engine != QLatin1String("new"))) {
        printUsage(stderr);
        return 2;
    }
    if (const QStringList missing = parity::missingContent(); !missing.isEmpty()) {
        std::fprintf(stderr, "athlete_dump: every content file is needed; unset or unreadable: %s\n",
                     qPrintable(missing.join(QLatin1Char(' '))));
        return 2;
    }
    for (const QString &dir : std::as_const(dirs)) {
        if (!QFileInfo(dir).isDir()) {
            std::fprintf(stderr, "athlete_dump: no such folder %s\n", qPrintable(dir));
            return 2;
        }
        if (!onScratch(dir)) {
            std::fprintf(stderr, "athlete_dump: %s is not on a scratch copy (no 'scratch' in its real path) — "
                                 "the catch-up writes into it; copy the athlete under /mnt/swingdata/scratch/ first\n",
                         qPrintable(dir));
            return 2;
        }
    }

    int failures = 0;
    QVariantList athletes;
    std::sort(dirs.begin(), dirs.end(), [](const QString &a, const QString &b) {
        return QFileInfo(a).fileName() < QFileInfo(b).fileName();
    });
    for (const QString &dir : std::as_const(dirs)) {
        const QString name = QFileInfo(dir).fileName();
        const QStringList sessions = sessionDirs(dir);

        // WHOSE THE SESSIONS ARE (refactor 5.3c): the id their session.json files name, through the session repository.
        // Refused here, before --fresh deletes anything, when they name more than one athlete.
        QString idFrom, idWhy;
        if (NewEngine probe; !probe.athleteOf(dir, &idFrom, &idWhy)) {
            std::fprintf(stderr, "athlete_dump: %s: %s\n", qPrintable(name), qPrintable(idWhy));
            std::printf("%-40s REFUSED  %s\n", qPrintable(name), qPrintable(idWhy));
            ++failures;
            continue;
        }

        if (fresh) {
            int removed = 0;
            bool ok = removeIfThere(WorkOnsController::tendenciesPath(dir), &removed);
            for (const QString &s : sessions) ok = removeIfThere(WorkOnsController::recordPath(s), &removed) && ok;
            if (!ok) return 1;
            std::printf("%-40s fresh: %d cached file(s) removed\n", qPrintable(name), removed);
        }

        bool settled = false;
        int sessionsFound = 0, sessionCount = 0;
        bool tendenciesReady = false;
        QVariantMap doWell;
        QVariantMap marks;
        QVariantMap scoped;
        QVariantList athleteWorkOns;
        QVariantMap newTendencies;
        QVariant themes;
        if (engine == QLatin1String("old")) {
            // The catch-up as main.cpp wires it (3.7-e): the service, over the headless owners.
            NewEngine ng;
            pinpoint::Athlete *a = ng.athleteOf(dir);   // known (checked above, before anything was written)
            pinpoint::AthleteCatchUp catchUp(ng.callables());
            WorkOnsController c;
            c.setSynchronous(true);
            QObject::connect(&catchUp, &pinpoint::AthleteCatchUp::sessionCaughtUp, &c,
                             [&c, a](const pinpoint::SessionId &id, const QString &) {
                                 for (const pinpoint::SessionIndexEntry &e : a->index())
                                     if (e.id() == id) { c.sessionCaughtUp(e.identity.folderName()); break; }
                             });
            QObject::connect(&catchUp, &pinpoint::AthleteCatchUp::finished, &c, [&c] { c.catchUpDone(); });
            // Folders to ids at the edge, as main.cpp's door does (DEC-90); a folder the index lacks is answered at once.
            c.setCatchUp({ [&catchUp, &c, a](const QString &, const QList<WorkOnsController::CatchUpAsk> &asks) {
                               QList<pinpoint::SessionId> ids;
                               QSet<pinpoint::SessionId> baselineOnly;
                               for (const WorkOnsController::CatchUpAsk &k : asks) {
                                   pinpoint::SessionId id;
                                   for (const pinpoint::SessionIndexEntry &e : a->index())
                                       if (QDir::cleanPath(e.folder) == QDir::cleanPath(k.sessionDir)) { id = e.id(); break; }
                                   if (id.isNull()) { c.sessionCaughtUp(QFileInfo(k.sessionDir).fileName()); continue; }
                                   ids << id;
                                   if (k.baselineOnly) baselineOnly.insert(id);
                               }
                               if (ids.isEmpty()) { c.catchUpDone(); return; }
                               catchUp.runSessions(a->id(), ids, baselineOnly);
                           },
                           [&catchUp](bool on) { catchUp.setPaused(on); }, [&catchUp] { catchUp.cancel(); },
                           [a] { return pinpoint::summaryCohortFacts(a->record().dob, a->record().sex); } });
            // The athlete level Home reads (refactor 3.7-f): this folder's Athlete, as main.cpp's door hands the current one.
            c.setAthleteDoor([a](const QString &) -> pinpoint::AthleteLevel * { return pinpoint::AthleteDiagnostics::of(a); });
            c.setAthleteDir(dir);
            settled = c.waitForIdle(timeoutS * 1000);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);   // the released sessions go
            sessionsFound   = c.sessionsFound();
            sessionCount    = c.sessionCount();
            tendenciesReady = c.tendenciesReady();
            doWell = QVariantMap{ { QStringLiteral("themes"), c.doWellBeforePraise() },
                                  { QStringLiteral("praised"), c.doWellPraised() } };
            marks = marksDump(c);
            if (benchThemes) benchThemesOf(name, a);
            scoped = scopedDump(c, timeoutS * 1000);
            bool tok = false;
            const QJsonObject stored = QJsonDocument::fromVariant(parity::readJsonFile(WorkOnsController::themesPath(dir))).object();
            const pinpoint::analysis::SwingThemes st = pinpoint::analysis::swingThemesFromJson(stored, &tok);
            if (tok) themes = pinpoint::analysis::toJson(st).toVariantMap();
            // THE GUI-THREAD SUMMARY READ (DUTIES-f A2's gate, REVIEW-f R1-m9 a; J-57). Every session's work_ons.json read
            // again by the owner, as a switch to this athlete or an out-of-process write makes Home do: (1) synchronously, on
            // this thread (the round-1 shape, the reference); (2) as the app does it now, readAhead() on the owner's lane of a
            // pool — this thread's part is the plan (ask) and the apply, each timed by the owner.
            {
                pinpoint::AthleteDiagnostics *ad = pinpoint::AthleteDiagnostics::of(a);
                ad->rereadAll();
                QElapsedTimer t;
                t.start();
                const size_t n = ad->summaries().size();
                const qint64 syncMs = t.elapsed();
                static pinpoint::WorkPool benchPool(2);   // outlives every owner that used it (an owner cancels its lane on it)
                pinpoint::WorkReceiver receiver([a](const pinpoint::WorkAddress &w) {
                    return pinpoint::isAthleteWorkOwner(w.owner) ? pinpoint::athleteDiagnosticsWorkTarget(a, w)
                                                                 : pinpoint::diagnosticsWorkTarget(nullptr, w);
                });
                const pinpoint::DiagnosticsDoors saved = a->diagnosticsDoors();
                pinpoint::DiagnosticsDoors lane = saved;
                lane.pool        = &benchPool;
                lane.receiver    = &receiver;
                lane.synchronous = false;
                a->setDiagnosticsDoors(lane);
                ad->rereadAll();
                const quint64 before = ad->counts().summariesReadAhead;
                QElapsedTimer wall;
                wall.start();
                ad->readAhead();
                while ((ad->summariesReading() || ad->stale()) && wall.elapsed() < 60000)
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
                const qint64 wallMs = wall.elapsed();
                std::printf("%-40s summary read: %zu session(s); synchronous on this thread %lld ms; on the lane %llu read, "
                            "this thread's part: ask %lld ms + apply %lld ms (landed after %lld ms)%s\n",
                            qPrintable(name), n, (long long)syncMs,
                            (unsigned long long)(ad->counts().summariesReadAhead - before), (long long)ad->counts().readAheadAskMs,
                            (long long)ad->counts().readAheadApplyMs, (long long)wallMs, ad->stale() ? " NOT LANDED" : "");
                a->setDiagnosticsDoors(saved);
            }
        } else {
            NewEngine ng;
            pinpoint::Athlete *a = ng.athleteOf(dir);   // known (checked above, before anything was written)
            pinpoint::AthleteCatchUp catchUp(ng.callables());
            QElapsedTimer clock;
            clock.start();
            catchUp.run(a->id());   // synchronous doors: every session in hand, one at a time, inside this call
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);   // the released sessions go
            const pinpoint::AthleteCatchUp::Report &r = catchUp.report();
            settled = !catchUp.running() && r.blocked == 0;
            tendenciesReady = true;   // computed from the summaries when read (DEC-60)
            pinpoint::AthleteDiagnostics *ad = pinpoint::AthleteDiagnostics::of(a);
            sessionsFound = r.sessions - r.skipped;
            sessionCount  = int(ad->records().size());
            athleteWorkOns = athleteWorkOnsDump(ad->workOns());
            newTendencies = pinpoint::analysis::tendencySetToJson(ad->tendencies()).toVariantMap();
            QThreadPool *pool = QThreadPool::globalInstance();
            const pinpoint::analysis::ThemeParallelFor parallel = [pool](int n, const std::function<void(int)> &body) {
                std::atomic<int> next{ 0 };
                const auto drain = [&]() { for (int i = next.fetch_add(1); i < n; i = next.fetch_add(1)) body(i); };
                const int helpers = std::max(0, std::min(n - 1, pool->maxThreadCount() - 1));
                QSemaphore done;
                for (int h = 0; h < helpers; ++h) pool->start([&]() { drain(); done.release(); });
                drain();
                done.acquire(helpers);
            };
            themes = pinpoint::analysis::toJson(ad->themes(parallel)).toVariantMap();
            std::printf("%-40s catch-up: %d session(s), %d written, %d skipped, %d blocked, %d released, %lld ms; "
                        "athlete level: %llu summary read(s), %llu recompute(s)\n",
                        qPrintable(name), r.sessions, r.written, r.skipped, r.blocked, r.released, (long long)clock.elapsed(),
                        (unsigned long long)ad->counts().summaryReads, (unsigned long long)ad->counts().recomputes);
            for (const QString &w : r.why) std::printf("%-40s   %s\n", "", qPrintable(w));
        }   // destroyed here: its pools drain before the files are read
        if (!settled || (engine == QLatin1String("old") && !tendenciesReady)) ++failures;
        std::printf("%-40s %s  sessions %d/%d  tendencies %s  (athlete id from %s)\n", qPrintable(name),
                    settled ? "SETTLED" : "TIMEOUT", sessionCount, sessionsFound,
                    tendenciesReady ? "ready" : engine == QLatin1String("new") ? "not built (new engine: F1)" : "NOT READY",
                    qPrintable(idFrom));
        // AN EMPTY DUMP NEVER PASSES (refactor 5.3c; REVIEW-5.3 R2): a folder holding sessions that leaves none counted
        // is a failure, said in words. Until 5.3c a migrated library dumped "sessionCount 0" and exited 0.
        if (!sessions.isEmpty() && sessionCount == 0) {
            std::fprintf(stderr, "athlete_dump: %s holds %d session folder(s) but 0 sessions were counted: the dump has "
                                 "nothing of them (are they this athlete's? the log names each session refused)\n",
                         qPrintable(name), int(sessions.size()));
            std::printf("%-40s FAILED  %d session folder(s), 0 sessions counted\n", qPrintable(name), int(sessions.size()));
            ++failures;
        }

        parity::PathTokens tokens;
        parity::addPathToken(tokens, dir, QStringLiteral("<athlete>"));

        QVariantMap athlete{
            { QStringLiteral("athlete"),         name },
            { QStringLiteral("settled"),         settled },
            { QStringLiteral("sessionsFound"),   sessionsFound },
            { QStringLiteral("sessionCount"),    sessionCount },
            { QStringLiteral("tendenciesReady"), tendenciesReady },
            { QStringLiteral("engine"),          engine },
        };
        athlete.insert(QStringLiteral("themes"), themes);
        if (engine == QLatin1String("old")) {
            athlete.insert(QStringLiteral("doWell"), doWell);
            athlete.insert(QStringLiteral("marks"), marks);
            athlete.insert(QStringLiteral("scoped"), scoped);
            athlete.insert(QStringLiteral("athleteWorkOns"), athleteWorkOnsFromFiles(sessions));
        } else {
            athlete.insert(QStringLiteral("athleteWorkOns"), athleteWorkOns);
        }
        QString why;
        QVariantMap tendencies = engine == QLatin1String("new")
                                     ? QVariantMap()
                                     : parity::readJsonFile(WorkOnsController::tendenciesPath(dir), &why).toMap();
        if (engine == QLatin1String("new")) {
            // tendencies.json's own keys (rule, schema) as the old engine's file holds them; no fingerprint
            newTendencies.remove(QStringLiteral("fingerprint"));
        }
        if (engine == QLatin1String("new")) tendencies = newTendencies;
        if (tendencies.isEmpty()) {
            athlete.insert(QStringLiteral("tendencies"), QVariant());
            athlete.insert(QStringLiteral("tendenciesWhy"), why.isEmpty() ? QStringLiteral("empty") : why);
            athlete.insert(QStringLiteral("clubDistances"), QVariantList());
        } else {
            athlete.insert(QStringLiteral("clubDistances"), tendencies.take(QStringLiteral("clubDistances")));
            athlete.insert(QStringLiteral("tendencies"), tendencies);
        }

        QVariantList sessionList;
        for (const QString &s : sessions) {
            parity::PathTokens st = tokens;
            parity::addPathToken(st, s, QStringLiteral("<session>"));
            QVariantMap entry{ { QStringLiteral("session"), QFileInfo(s).fileName() },
                               { QStringLiteral("boardAnnotations"), boardAnnotations(s) } };
            QString w;
            const QVariant rec = parity::readJsonFile(WorkOnsController::recordPath(s), &w);
            entry.insert(QStringLiteral("workOns"), parity::scrub(rec, st));
            if (!rec.isValid()) entry.insert(QStringLiteral("workOnsWhy"), w);
            // The canonical reader (parity_json.h), by the session files' own name: the dump's bytes must not
            // depend on which JSON reader took the file in.
            const QVariant base = parity::readJsonFile(pinpoint::SessionFiles::clubBaselineFile(QDir(s).path()), &w);
            entry.insert(QStringLiteral("clubBaseline"), parity::scrub(base, st));
            if (!base.isValid()) entry.insert(QStringLiteral("clubBaselineWhy"), w);
            sessionList.append(entry);
        }
        athlete.insert(QStringLiteral("sessions"), sessionList);
        athletes.append(parity::scrub(athlete, tokens));
    }

    const QVariantMap root{
        { QStringLiteral("tool"),        QStringLiteral("athlete_dump") },
        { QStringLiteral("dumpVersion"), 1 },
        { QStringLiteral("content"),     parity::contentHashes() },
        { QStringLiteral("athletes"),    athletes },
    };
    QString why;
    if (!parity::writeFile(outPath, parity::toCanonicalJson(root), &why)) {
        std::fprintf(stderr, "athlete_dump: could not write %s (%s)\n", qPrintable(outPath), qPrintable(why));
        return 1;
    }
    std::printf("dump: %d athlete(s) -> %s\n", int(athletes.size()), qPrintable(outPath));
    return failures == 0 ? 0 : 1;
}
