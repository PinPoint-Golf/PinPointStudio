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
//   1. diagnostics.json is read and parsed. One that cannot be is left exactly as it is and the
//      session is SKIPPED: it may hold the golfer's only copy of what they said.
//   2. diagnostics.json  ->  diagnostics.json.<tag>   (default tag pre-regrade; never overwritten:
//      refuses if present)
//   3. THE GOLFER'S HALF IS CARRIED ACROSS. The file is half cache (the rows, the session meta) and
//      half the only copy of what the golfer and coach said: SessionDiagnosticsModel::golferInputKeys()
//      — the focus and declared miss (`intent`), screen answers, shot intent, a run of intent and
//      outcome corrections. When the golfer said anything at all — a value, not a key: the app
//      always writes `intent` and `screens`, empty or not — those keys, as they stood, are written
//      as a seed diagnostics.json (golfer keys, no rows) before the model reads it, so the rebuilt
//      file keeps them (audit H-04: a regrade used to drop them all, and one corpus session lost
//      its declared focus that way). When the golfer said nothing no seed is written.
//      If the model then writes no ledger (no swing_* folders, or every write failed), the
//      original is PUT BACK from the backup and the seed removed: a ledger-less file is not left
//      behind, because prepareClubBaseline reads it as an out-of-date predecessor and the
//      golfer's later sessions would then never freeze their club baseline.
//   4. SessionDiagnosticsModel::activateSession(dir), synchronously, which re-ingests every shot
//      and writes a fresh diagnostics.json after each one
//   5. one line per session on stdout: shots, stage, patterns, coverage, and what was carried —
//      with a WARN, and exit 1, if the rebuilt file does not say the same as the old one did
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
#include "Core/PpMessageLog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStringList>

#include <cstdio>

// SessionDiagnosticsModel is a QML_ELEMENT façade in the global namespace, as the app registers it.

namespace {

// The golfer's half of an envelope, in words: what must read the same before and after the regrade.
QString golferSummary(const QJsonObject &g)
{
    QStringList parts;
    const QJsonObject intent = g.value(QStringLiteral("intent")).toObject();
    const QString focus = intent.value(QStringLiteral("focusConditionId")).toString();
    if (!focus.isEmpty())
        parts << QStringLiteral("focus %1 from row %2").arg(focus).arg(intent.value(QStringLiteral("focusFromShot")).toInt(-1));
    const QString miss = intent.value(QStringLiteral("declaredMiss")).toString();
    if (!miss.isEmpty()) parts << QStringLiteral("miss %1").arg(miss);
    if (const auto n = g.value(QStringLiteral("screens")).toObject().size(); n > 0)
        parts << QStringLiteral("%1 screen answer(s)").arg(n);
    if (const auto n = g.value(QStringLiteral("shotIntent")).toObject().size(); n > 0)
        parts << QStringLiteral("%1 shot intent(s)").arg(n);
    const QJsonObject run = g.value(QStringLiteral("intentFrom")).toObject();
    if (run.value(QStringLiteral("fromShot")).toInt(-1) >= 0 && !run.value(QStringLiteral("shape")).toString().isEmpty())
        parts << QStringLiteral("%1 from shot %2").arg(run.value(QStringLiteral("shape")).toString())
                                                 .arg(run.value(QStringLiteral("fromShot")).toInt());
    if (const auto n = g.value(QStringLiteral("corrections")).toObject().size(); n > 0)
        parts << QStringLiteral("corrections on %1 shot(s)").arg(n);
    return parts.join(QStringLiteral(" · "));
}

// Read and parse a ledger. False, with the reason, when it is there and cannot be.
bool readLedger(const QString &path, QJsonObject *root, QString *why)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { *why = f.errorString(); return false; }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        *why = QStringLiteral("%1 at byte %2").arg(err.errorString()).arg(err.offset);
        return false;
    }
    if (!doc.isObject()) { *why = QStringLiteral("not a JSON object"); return false; }
    *root = doc.object();
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // A headless tool echoes the app log (PpMessageLog.h): the model reports through ppWarn — an
    // unreadable club baseline, a ledger set aside — and nothing else in this process reads it.
    PpMessageLog::instance()->setEchoToStderr(true);

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
        const QString name   = QFileInfo(dir).fileName();
        bool hadLedger = false;
        QJsonObject carried;          // the golfer's half of the old file, as it stood
        bool seeded = false;          // diagnostics.json is, for now, the carried golfer input alone
        if (QFile::exists(ledger)) {
            if (QFile::exists(backup)) {
                std::printf("%-48s SKIP  %s already exists — pick another --tag\n",
                            qPrintable(name), qPrintable(QFileInfo(backup).fileName()));
                ++failures;
                continue;
            }
            QJsonObject old;
            QString why;
            if (!readLedger(ledger, &old, &why)) {
                std::printf("%-48s SKIP  diagnostics.json cannot be read (%s) — left as it is\n",
                            qPrintable(name), qPrintable(why));
                ++failures;
                continue;
            }
            carried = SessionDiagnosticsModel::golferInput(old);
            if (!QFile::rename(ledger, backup)) {
                std::printf("%-48s SKIP  could not set the ledger aside\n", qPrintable(name));
                ++failures;
                continue;
            }
            hadLedger = true;
            if (!golferSummary(carried).isEmpty()) {          // the golfer said something: carry it
                // The seed the model reads: the golfer's keys and no rows, so every shot back-fills and
                // each write carries them. Put back as it was if the seed cannot be written.
                QJsonObject seed = carried;
                if (old.contains(QStringLiteral("schemaVersion")))
                    seed.insert(QStringLiteral("schemaVersion"), old.value(QStringLiteral("schemaVersion")));
                QSaveFile out(ledger);
                const bool written = out.open(QIODevice::WriteOnly)
                                  && out.write(QJsonDocument(seed).toJson(QJsonDocument::Compact)) >= 0
                                  && out.commit();
                if (!written) {
                    const bool restored = QFile::rename(backup, ledger);
                    std::printf("%-48s SKIP  could not carry the golfer's input across (%s)%s\n",
                                qPrintable(name), qPrintable(out.errorString()),
                                restored ? " — ledger put back" : " — the ledger is in the backup");
                    ++failures;
                    continue;
                }
                seeded = true;
            }
        }

        SessionDiagnosticsModel m;
        m.setSynchronous(true);
        m.setCadence(QStringLiteral("everyShot"));
        m.setBaselineAuthority(SessionDiagnosticsModel::BaselineAuthority::Never);   // a tool: no club_baseline.json
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
        // Did the model write a ledger? Its write attempts are counted, not its successes, so the file
        // itself is asked: a seed still standing has no `ledger`.
        QJsonObject now;
        QString why;
        const bool readable = readLedger(ledger, &now, &why);
        const bool modelWrote = m.persistCount() > 0 && readable && now.contains(QStringLiteral("ledger"));
        if (!modelWrote) {
            QString what = QStringLiteral("no diagnostics.json was written (no swing_* directories?)");
            if (hadLedger) {
                // Nothing was regraded: the session keeps the file it had, not a seed and not nothing.
                const bool seedGone = !QFile::exists(ledger)
                                   || (seeded && readable && !now.contains(QStringLiteral("ledger")) && QFile::remove(ledger));
                const bool restored = seedGone && QFile::rename(backup, ledger);
                what += restored ? QStringLiteral(" — the original is put back")
                                 : QStringLiteral(" — could not put the original back; it is %1").arg(QFileInfo(backup).fileName());
                if (!restored) ++failures;
            }
            std::printf("%-48s WARN  %s\n", qPrintable(name), qPrintable(what));
        } else if (seeded) {
            // What the rebuilt file says the golfer said, against what the old one did.
            const QString before = golferSummary(carried);
            const QString after  = golferSummary(SessionDiagnosticsModel::golferInput(now));
            std::printf("%-48s   carried: %s\n", "", qPrintable(before));
            if (after != before) {
                std::printf("%-48s WARN  the golfer's input did not survive: now %s — the old file is %s\n",
                            qPrintable(name), after.isEmpty() ? "nothing" : qPrintable(after),
                            qPrintable(QFileInfo(backup).fileName()));
                ++failures;
            }
        }
    }
    return failures == 0 ? 0 : 1;
}
