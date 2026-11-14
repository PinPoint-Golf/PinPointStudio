// athlete_dump — the athlete-level PARITY DUMP (refactor Step G, build/refactor/step-G/BRIEF.md §2 item 3):
// run the app's own work-ons catch-up, headlessly, over one or more athlete folders of a SCRATCH library,
// and write what it leaves behind — <athlete>/tendencies.json, every session's work_ons.json and
// club_baseline.json, and the per-club distances — as one canonical JSON file (parity_json.h: keys sorted,
// numbers "%.17g", no timestamps or cache fingerprints, folders as "<athlete>"/"<session>"), so two runs
// diff byte for byte.
//
// THE CATCH-UP IS THE APP'S. WorkOnsController is the object main.cpp binds to the current athlete; with
// setSynchronous(true) and waitForIdle() it scans, derives each stale session's record through its private
// SessionDiagnosticsModel (activateSession: back-fills and regrades the ledger), freezes any club baseline a
// session lacks, and reduces the tendencies — exactly the work_ons_controller_test's drive. Nothing here
// re-derives anything; the tool only reads the files back.
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

#include "Gui/diagnostics/work_ons_controller.h"
#include "Core/PpMessageLog.h"
#include "parity_json.h"

#include <QCoreApplication>
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
        "usage: athlete_dump [--fresh] [--timeout <seconds>] --out <out.json> <athlete-dir>...\n"
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
        if (d.exists(QStringLiteral("diagnostics.json")) || d.exists(QStringLiteral("work_ons.json"))
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

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    PpMessageLog::instance()->setEchoToStderr(true);

    QString     outPath;
    bool        fresh = false;
    int         timeoutS = 1800;
    QStringList dirs;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--help") || a == QLatin1String("-h")) { printUsage(stdout); return 0; }
        if (a == QLatin1String("--fresh")) { fresh = true; continue; }
        if (a == QLatin1String("--out") && i + 1 < argc) { outPath = QString::fromLocal8Bit(argv[++i]); continue; }
        if (a == QLatin1String("--timeout") && i + 1 < argc) { timeoutS = QString::fromLocal8Bit(argv[++i]).toInt(); continue; }
        if (a.startsWith(QLatin1String("--"))) {
            std::fprintf(stderr, "athlete_dump: unknown option %s\n", argv[i]);
            printUsage(stderr);
            return 2;
        }
        dirs << QDir(a).absolutePath();
    }
    if (dirs.isEmpty() || outPath.isEmpty() || timeoutS <= 0) {
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
        {
            WorkOnsController c;
            c.setSynchronous(true);
            c.setAthleteDir(dir);
            settled = c.waitForIdle(timeoutS * 1000);
            sessionsFound   = c.sessionsFound();
            sessionCount    = c.sessionCount();
            tendenciesReady = c.tendenciesReady();
        }   // destroyed here: its pools drain before the files are read
        if (!settled || !tendenciesReady) ++failures;
        std::printf("%-40s %s  sessions %d/%d  tendencies %s\n", qPrintable(name),
                    settled ? "SETTLED" : "TIMEOUT", sessionCount, sessionsFound, tendenciesReady ? "ready" : "NOT READY");

        parity::PathTokens tokens;
        parity::addPathToken(tokens, dir, QStringLiteral("<athlete>"));

        QVariantMap athlete{
            { QStringLiteral("athlete"),         name },
            { QStringLiteral("settled"),         settled },
            { QStringLiteral("sessionsFound"),   sessionsFound },
            { QStringLiteral("sessionCount"),    sessionCount },
            { QStringLiteral("tendenciesReady"), tendenciesReady },
        };
        QString why;
        QVariantMap tendencies = parity::readJsonFile(WorkOnsController::tendenciesPath(dir), &why).toMap();
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
            QVariantMap entry{ { QStringLiteral("session"), QFileInfo(s).fileName() } };
            QString w;
            const QVariant rec = parity::readJsonFile(WorkOnsController::recordPath(s), &w);
            entry.insert(QStringLiteral("workOns"), parity::scrub(rec, st));
            if (!rec.isValid()) entry.insert(QStringLiteral("workOnsWhy"), w);
            const QVariant base = parity::readJsonFile(QDir(s).filePath(QStringLiteral("club_baseline.json")), &w);
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
