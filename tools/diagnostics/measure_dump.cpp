// measure_dump — every pack measure's reading, per shot, as a CSV (posture_metrics_design.md §9).
//
// regrade_ledger writes ledgers, and a ledger holds condition ROWS: what fired, not the numbers that
// fired it. The posture validation (V1 trunk rise vs DTL bend, V2 the composites' rho, V3 norms for
// the part measures, V4 firing tables, V5 library-vs-corpus repeatability) needs the numbers — every
// measure's value, its z against its norm, its grade or why it has none, and for a composite each
// part's contribution. This reads them with the SAME per-shot construction the session diagnostics
// panel uses (measure_dump_core.h says exactly which), so a number here is the number the app graded.
//
//   measure_dump --out <file.csv> [--measures id1,id2,…|--prefix m_] [--policy standard] <dir>...
//
// <dir> is a session directory (its swing_* children are read) or a single swing directory. Every
// alternative of a selected composite's parts is dumped as a plain measure too. One row per
// (session, swing, measure); a composite adds one row per part ("<id>/<part>").
//
// READ-ONLY on the session directories: it writes no phase-grid or summary sidecar and no ledger, and
// refuses an --out inside one of them.
//
// Content comes from the environment, exactly as regrade_ledger takes it:
//   export PINPOINT_CORE_PACK=src/Resources/diagnostics/core.json   (and NORMS, CONTEXTS, SCREENS,
//                                                                    DRILLS, REFERENCES likewise)
//
// NOT A TEST and not registered with ctest; measure_dump_test drives the same core.

#include "measure_dump_core.h"

#include "Diagnostics/packs/pack_provider.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <cmath>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    measure_dump::Options opt;
    QString     outPath;
    QStringList dirs;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        auto next = [&]() { return i + 1 < argc ? QString::fromLocal8Bit(argv[++i]) : QString(); };
        if (a == QLatin1String("--out"))      { outPath = next(); continue; }
        if (a == QLatin1String("--measures")) { opt.measures = next().split(QLatin1Char(','), Qt::SkipEmptyParts); continue; }
        if (a == QLatin1String("--prefix"))   { opt.prefix = next(); continue; }
        if (a == QLatin1String("--policy"))   { opt.policy = next(); continue; }
        if (a.startsWith(QLatin1String("--"))) {
            std::fprintf(stderr, "measure_dump: unknown option %s\n", argv[i]);
            return 2;
        }
        dirs << a;
    }
    if (outPath.isEmpty() || dirs.isEmpty()) {
        std::fprintf(stderr, "usage: measure_dump --out <file.csv> [--measures id1,id2,...|--prefix m_] "
                             "[--policy standard] <session-or-swing-dir>...\n");
        return 2;
    }
    if (qEnvironmentVariableIsEmpty("PINPOINT_CORE_PACK")) {
        std::fprintf(stderr, "measure_dump: PINPOINT_CORE_PACK is unset - export PINPOINT_CORE_{PACK,NORMS,"
                             "CONTEXTS,SCREENS,DRILLS,REFERENCES} at src/Resources/diagnostics/*.json so the "
                             "dump grades against the checkout, not an empty pack.\n");
        return 2;
    }

    // Read-only on the library: the output may not land inside any directory being read.
    const QString outAbs = QFileInfo(outPath).absoluteFilePath();
    for (const QString &d : dirs) {
        const QString abs = QDir(d).absolutePath() + QLatin1Char('/');
        if (outAbs.startsWith(abs)) {
            std::fprintf(stderr, "measure_dump: --out %s is inside %s - refusing to write into a session\n",
                         qPrintable(outAbs), qPrintable(abs));
            return 2;
        }
    }

    const auto packProv = pinpoint::analysis::makeCharacteristicPackProvider();
    const auto norms    = pinpoint::analysis::sharedNormProvider();
    const pinpoint::analysis::CharacteristicPack &pack = packProv->pack();
    const QStringList ids = measure_dump::selectMeasures(pack, opt);
    if (ids.isEmpty()) {
        std::fprintf(stderr, "measure_dump: no measures selected\n");
        return 2;
    }
    const pinpoint::analysis::GradePolicy policy = pinpoint::analysis::gradePolicyByName(opt.policy);

    QSaveFile out(outPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        std::fprintf(stderr, "measure_dump: cannot write %s\n", qPrintable(outPath));
        return 2;
    }
    out.write(measure_dump::csvColumns().join(QLatin1Char(',')).toUtf8() + '\n');

    int failures = 0;
    long total = 0;
    for (const QString &raw : dirs) {
        const QString dir = QDir(raw).absolutePath();
        if (!QDir(dir).exists()) {
            std::printf("%-48s SKIP  no such directory\n", qPrintable(QFileInfo(dir).fileName()));
            ++failures;
            continue;
        }
        const QStringList swings = measure_dump::swingDirsOf(dir);
        // A single swing directory reads under its parent as the session.
        const QString sessionDir = (swings.size() == 1 && swings.first() == dir)
                                       ? QFileInfo(dir).absolutePath() : dir;
        long rows = 0, valued = 0;
        for (const QString &sw : swings) {
            const std::vector<measure_dump::Row> rs =
                measure_dump::readSwing(sessionDir, sw, pack, norms, ids, policy);
            for (const measure_dump::Row &r : rs) {
                out.write(measure_dump::csvLine(r).toUtf8() + '\n');
                ++rows;
                if (std::isfinite(r.value)) ++valued;
            }
        }
        total += rows;
        std::printf("%-48s swings %3lld  rows %6ld  with value %6ld\n", qPrintable(QFileInfo(dir).fileName()),
                    static_cast<long long>(swings.size()), rows, valued);
    }
    if (!out.commit()) {
        std::fprintf(stderr, "measure_dump: failed to write %s\n", qPrintable(outPath));
        return 2;
    }
    std::printf("measure_dump: %ld rows, %lld measures -> %s\n", total, static_cast<long long>(ids.size()),
                qPrintable(outAbs));
    return failures == 0 ? 0 : 1;
}
