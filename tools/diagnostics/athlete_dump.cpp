// athlete_dump — the athlete-level PARITY DUMP (refactor Step G, build/refactor/step-G/BRIEF.md §2 item 3):
// run the app's own work-ons catch-up, headlessly, over one or more athlete folders of a SCRATCH library,
// and write what it leaves behind — <athlete>/tendencies.json, every session's work_ons.json and
// club_baseline.json, and the per-club distances — as one canonical JSON file (parity_json.h: keys sorted,
// numbers "%.17g", no timestamps or cache fingerprints, folders as "<athlete>"/"<session>"), so two runs
// diff byte for byte.
//
// THE CATCH-UP IS THE APP'S. WorkOnsController is the object main.cpp binds to the current athlete; with
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
// --engine old|new (refactor step 3.6; build/refactor/step-3/3.6/NOTES.md, the athlete parity):
//   old (the default) — the above: WorkOnsController, what Home shows, with the praise rule and Stage 0.5's follow-up
//       wired through it; since 3.7-e its records are the new owners' (its private model is gone), so "old" names Home's
//       reductions from the ledgers (tendencies, themes), not an older engine. Its dump adds `doWell` { themes, praised }:
//       the themes' "what you do well" condition ids before and after the praise rule.
//   new — THE NEW OWNERS: the catch-up service (src/App/athlete_catch_up.h) over the real session store, Athlete,
//       Session and SessionDiagnostics (headless, every shot fired again: DiagnosticsDoors::rebuild; each session's club
//       baseline frozen from its predecessors' summaries when it has none or an older rule's), oldest session first,
//       each released after; then AthleteDiagnostics (src/DomainModel/athlete_diagnostics.h), which reads the summaries
//       only: its tendencies (with the distances, from the club windows), its themes and its athlete work-ons (the
//       summaries' compact per-shot facts: DEC-192 as Mark amended it, 16 Nov "b"). The athlete is the folder's own.
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
#include <QThreadPool>
#include <atomic>
#include <map>
#include <memory>

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
        "usage: athlete_dump [--engine old|new] [--fresh] [--timeout <seconds>] --out <out.json> <athlete-dir>...\n"
        "\n"
        "Runs the app's work-ons catch-up (WorkOnsController, headless) over each athlete folder of a SCRATCH\n"
        "library and writes the PARITY DUMP: tendencies.json, the per-club distances, and every session's\n"
        "work_ons.json and club_baseline.json, as canonical JSON (keys sorted, %%.17g numbers, no timestamps\n"
        "or fingerprints, folders as <athlete>/<session>), so two runs diff byte for byte.\n"
        "WRITES INTO THE FOLDERS: refuses any whose real path has no 'scratch' component.\n"
        "\n"
        "  --out <file>       where the dump goes (required)\n"
        "  --fresh            delete each session's work_ons.json and the athlete's tendencies.json first, so\n"
        "                     the catch-up re-derives them with this build (for a parity run: regrade_ledger\n"
        "                     --dump each session first, then this with --fresh)\n"
        "  --timeout <s>      how long one athlete's catch-up may take (default 1800)\n"
        "  --engine old|new   old (default): WorkOnsController, what Home shows; new: the catch-up service and the\n"
        "                     new owners (SessionDiagnostics, AthleteDiagnostics), every shot fired again (refactor 3.6)\n"
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

    // The athlete of a folder (no record: an id from the folder's name, no cohort facts), made once.
    pinpoint::Athlete *athleteOf(const QString &dir)
    {
        using namespace pinpoint;
        const QString name = QFileInfo(dir).fileName();
        const AthleteId id = AthleteId::fromString(QStringLiteral("00000000-0000-4000-8000-%1")
            .arg(QString::fromLatin1(QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha1).toHex().left(12))));
        auto it = m_athletes.find(id.toString());
        if (it != m_athletes.end()) return it->second.get();
        auto a = std::make_unique<Athlete>(nullptr, &m_store, id);
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
    QString     engine = QStringLiteral("old");
    bool        fresh = false;
    int         timeoutS = 1800;
    QStringList dirs;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--help") || a == QLatin1String("-h")) { printUsage(stdout); return 0; }
        if (a == QLatin1String("--fresh")) { fresh = true; continue; }
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
        QVariantList athleteWorkOns;
        QVariantMap newTendencies;
        QVariant themes;
        if (engine == QLatin1String("old")) {
            // The catch-up as main.cpp wires it (3.7-e): the service, over the headless owners.
            NewEngine ng;
            pinpoint::Athlete *a = ng.athleteOf(dir);
            pinpoint::AthleteCatchUp catchUp(ng.callables());
            WorkOnsController c;
            c.setSynchronous(true);
            QObject::connect(&catchUp, &pinpoint::AthleteCatchUp::sessionCaughtUp, &c,
                             [&c, a](const pinpoint::SessionId &id, const QString &) {
                                 for (const pinpoint::SessionIndexEntry &e : a->index())
                                     if (e.id() == id) { c.sessionCaughtUp(e.folder); break; }
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
                                   if (id.isNull()) { c.sessionCaughtUp(k.sessionDir); continue; }
                                   ids << id;
                                   if (k.baselineOnly) baselineOnly.insert(id);
                               }
                               if (ids.isEmpty()) { c.catchUpDone(); return; }
                               catchUp.runSessions(a->id(), ids, baselineOnly);
                           },
                           [&catchUp](bool on) { catchUp.setPaused(on); }, [&catchUp] { catchUp.cancel(); },
                           [a] { return pinpoint::summaryCohortFacts(a->record().dob, a->record().sex); } });
            c.setAthleteDir(dir);
            settled = c.waitForIdle(timeoutS * 1000);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);   // the released sessions go
            sessionsFound   = c.sessionsFound();
            sessionCount    = c.sessionCount();
            tendenciesReady = c.tendenciesReady();
            doWell = QVariantMap{ { QStringLiteral("themes"), c.doWellBeforePraise() },
                                  { QStringLiteral("praised"), c.doWellPraised() } };
            bool tok = false;
            const QJsonObject stored = QJsonDocument::fromVariant(parity::readJsonFile(WorkOnsController::themesPath(dir))).object();
            const pinpoint::analysis::SwingThemes st = pinpoint::analysis::swingThemesFromJson(stored, &tok);
            if (tok) themes = pinpoint::analysis::toJson(st).toVariantMap();
        } else {
            NewEngine ng;
            pinpoint::Athlete *a = ng.athleteOf(dir);
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
        std::printf("%-40s %s  sessions %d/%d  tendencies %s\n", qPrintable(name),
                    settled ? "SETTLED" : "TIMEOUT", sessionCount, sessionsFound,
                    tendenciesReady ? "ready" : engine == QLatin1String("new") ? "not built (new engine: F1)" : "NOT READY");

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
