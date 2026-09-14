// regrade_ledger — re-reduce a session's diagnostics.json under the CURRENT pack, headlessly.
//
// The session diagnostics panel persists per-shot ROWS (diagnostic_ledger.h) and re-reduces the
// verdicts from them on review, so a gate that moves in a later build re-grades an old session
// honestly. What it does NOT do is re-run detection on shots already in the file: activateSession()
// reconciles the ledger against the swing_* directories and back-fills only the shots it has never
// reduced. So when the CONTENT moves — a norm mirrored, a measure retired to noProducer, a signal's
// tail swapped — every ledger already on disk keeps quoting the pack that wrote it, and the only
// way to see what the new pack says about an old session is to set the file aside and let the
// model back-fill every shot again. That is the whole of this tool:
//
//   1. diagnostics.json  ->  diagnostics.json.pre-<tag>   (never overwritten: refuses if present)
//   2. SessionDiagnosticsModel::activateSession(dir), synchronously, which re-ingests every shot
//      and writes a fresh diagnostics.json after each one
//   3. one line per session on stdout: shots, stage, patterns, coverage
//
// It does NOT close the session — closeSession() writes the athlete's fault_profile.json and
// freezes the stage, and a sweep is a re-reading of evidence, not a session ending — and it does
// not touch swing.json, pose, ball or shaft. No pose re-analysis is involved; the reduction reads
// the metrics already in each document.
//
// Content comes from the same environment the test suites use, so the sweep grades against the
// checkout rather than whatever the installed app carries:
//
//   export PINPOINT_CORE_PACK=src/Resources/diagnostics/core.json   (and NORMS, CONTEXTS, SCREENS,
//                                                                    DRILLS, REFERENCES likewise)
//   ./build/tests/Analysis/regrade_ledger --tag pre-headsway-fix /mnt/swingdata/corpus/swings/*/
//
// NOT A TEST, and not registered with ctest — it writes into the swing library.

#include "Gui/diagnostics/session_diagnostics_model.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <cstdio>

// SessionDiagnosticsModel is a QML_ELEMENT façade in the global namespace, as the app registers it.

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QString     tag = QStringLiteral("pre-regrade");
    QStringList dirs;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--tag") && i + 1 < argc) { tag = QString::fromLocal8Bit(argv[++i]); continue; }
        if (a.startsWith(QLatin1String("--"))) {
            std::fprintf(stderr, "regrade_ledger: unknown option %s\n", argv[i]);
            return 2;
        }
        dirs << a;
    }
    if (dirs.isEmpty()) {
        std::fprintf(stderr, "usage: regrade_ledger [--tag <backup-suffix>] <session-dir>...\n");
        return 2;
    }
    if (qEnvironmentVariableIsEmpty("PINPOINT_CORE_PACK")) {
        std::fprintf(stderr, "regrade_ledger: PINPOINT_CORE_PACK is unset — export PINPOINT_CORE_{PACK,NORMS,"
                             "CONTEXTS,SCREENS,DRILLS,REFERENCES} at src/Resources/diagnostics/*.json so the "
                             "sweep grades against the checkout, not an empty pack.\n");
        return 2;
    }

    int failures = 0;
    for (const QString &raw : dirs) {
        const QString dir = QDir(raw).absolutePath();
        if (!QDir(dir).exists()) {
            std::printf("%-48s SKIP  no such directory\n", qPrintable(QFileInfo(dir).fileName()));
            ++failures;
            continue;
        }
        const QString ledger = dir + QStringLiteral("/diagnostics.json");
        const QString backup = ledger + QLatin1Char('.') + tag;
        bool hadLedger = false;
        if (QFile::exists(ledger)) {
            if (QFile::exists(backup)) {
                std::printf("%-48s SKIP  %s already exists — pick another --tag\n",
                            qPrintable(QFileInfo(dir).fileName()), qPrintable(QFileInfo(backup).fileName()));
                ++failures;
                continue;
            }
            if (!QFile::rename(ledger, backup)) {
                std::printf("%-48s SKIP  could not set the ledger aside\n", qPrintable(QFileInfo(dir).fileName()));
                ++failures;
                continue;
            }
            hadLedger = true;
        }

        SessionDiagnosticsModel m;
        m.setSynchronous(true);
        m.setCadence(QStringLiteral("everyShot"));
        m.activateSession(dir);

        std::printf("%-48s %s  shots %2d  stage %-11s patterns %2d  %s\n",
                    qPrintable(QFileInfo(dir).fileName()),
                    hadLedger ? "REGRADED" : "GRADED  ",
                    m.shotCount(), qPrintable(m.stage()), m.patternCount(),
                    qPrintable(m.coverageLine()));
        QStringList patterns;
        for (const QVariant &v : m.cards()) {
            const QVariantMap card = v.toMap();
            const QString id = card.value(QStringLiteral("id"),
                                          card.value(QStringLiteral("conditionId"))).toString();
            if (!id.isEmpty()) patterns << id;
        }
        if (!patterns.isEmpty())
            std::printf("%-48s   patterns: %s\n", "", qPrintable(patterns.join(QLatin1Char(' '))));
        if (!QFile::exists(ledger)) {
            std::printf("%-48s WARN  no diagnostics.json was written (no swing_* directories?)\n",
                        qPrintable(QFileInfo(dir).fileName()));
        }
    }
    return failures == 0 ? 0 : 1;
}
