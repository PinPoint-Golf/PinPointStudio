/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

// pps_convert_library — convert every JSON-era swing under one or more roots to swing.ppsw.
//
//   pps_convert_library [--dry-run] [--keep-json] [--keep-going] [--csv report.csv] ROOT [ROOT ...]
//
// A ROOT may be a library root, an athlete dir, a session dir or a single swing dir: every
// directory beneath it holding a swing document is a swing. Dot-directories (.pinpoint-trash)
// are not entered — a trashed swing is the user's to restore, not ours to rewrite.
//
// Each swing goes through SwingDocWriter::convertToPpsw — the SAME function the in-app
// "Convert library" action uses — which proves the .ppsw against the original (a fresh reader,
// semantic equality of everything but the added summary) BEFORE the JSON is deleted. A swing
// that fails keeps its JSON untouched, and the run STOPS there unless --keep-going: a failure in
// a conversion is something to understand before it is repeated 170 times.
//
// Resumable and idempotent: an already-converted swing reports "already" and is left alone.
// Exit status: 0 when nothing failed.

#include "swing_doc.h"
#include "swing_store.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

using namespace pinpoint;

namespace {

QTextStream &out()
{
    static QTextStream s(stdout);
    return s;
}

QString csvField(QString s)
{
    s.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + s + QLatin1Char('"');
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("Convert JSON-era swings to swing.ppsw, verified."));
    cli.addPositionalArgument(QStringLiteral("root"), QStringLiteral("Library, athlete, session or swing dir"),
                              QStringLiteral("ROOT [ROOT ...]"));
    QCommandLineOption optDry(QStringLiteral("dry-run"), QStringLiteral("List what would be converted; write nothing"));
    QCommandLineOption optKeep(QStringLiteral("keep-json"), QStringLiteral("Verify, but leave swing.json in place"));
    QCommandLineOption optGoOn(QStringLiteral("keep-going"), QStringLiteral("Do not stop at the first failure"));
    QCommandLineOption optCsv(QStringLiteral("csv"), QStringLiteral("Per-swing report"), QStringLiteral("file"));
    cli.addOptions({ optDry, optKeep, optGoOn, optCsv });
    cli.addHelpOption();
    cli.process(app);

    const QStringList roots = cli.positionalArguments();
    if (roots.isEmpty()) {
        cli.showHelp(2);
    }
    const bool dryRun = cli.isSet(optDry);

    QFile csv;
    QTextStream csvOut;
    if (cli.isSet(optCsv)) {
        csv.setFileName(cli.value(optCsv));
        if (!csv.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            out() << "cannot write " << csv.fileName() << "\n";
            return 2;
        }
        csvOut.setDevice(&csv);
        csvOut << "swing_dir,outcome,json_bytes,ppsw_bytes,ms,error\n";
    }

    int converted = 0, already = 0, failed = 0, pending = 0;
    qint64 jsonTotal = 0, ppswTotal = 0;
    QElapsedTimer total;
    total.start();

    for (const QString &rootArg : roots) {
        const QString root = QDir::cleanPath(rootArg);
        if (!QFileInfo(root).isDir()) {
            out() << root << ": no such directory\n";
            ++failed;
            continue;
        }
        const QStringList dirs = SwingStore::swingDirsUnder(root);
        out() << root << ": " << dirs.size() << " swings\n";
        out().flush();

        for (const QString &dir : dirs) {
            const SwingStore::DocInfo info = SwingStore::info(dir);
            const bool hasJson = QFileInfo::exists(SwingStore::jsonPath(dir));
            if (dryRun) {
                const QString what = hasJson ? QStringLiteral("would convert") : QStringLiteral("already");
                out() << "  " << what << "  " << dir << "\n";
                (hasJson ? pending : already)++;
                if (csvOut.device())
                    csvOut << csvField(dir) << ',' << (hasJson ? "pending" : "already") << ','
                           << (hasJson ? QFileInfo(SwingStore::jsonPath(dir)).size() : 0) << ','
                           << (info.format == SwingStore::Format::Ppsw ? info.size : 0) << ",0,\n";
                continue;
            }

            QElapsedTimer t;
            t.start();
            const SwingStore::ConvertResult r =
                SwingDocWriter::convertToPpsw(dir, /*deleteJson=*/!cli.isSet(optKeep));
            const qint64 ms = t.elapsed();

            QString outcome;
            switch (r.outcome) {
                case SwingStore::ConvertResult::Outcome::Converted:
                    outcome = QStringLiteral("converted");
                    ++converted;
                    jsonTotal += r.jsonBytes;
                    ppswTotal += r.ppswBytes;
                    break;
                case SwingStore::ConvertResult::Outcome::AlreadyPpsw:
                    outcome = QStringLiteral("already");
                    ++already;
                    break;
                case SwingStore::ConvertResult::Outcome::NoDocument:
                    outcome = QStringLiteral("nodoc");
                    break;
                case SwingStore::ConvertResult::Outcome::Failed:
                    outcome = QStringLiteral("FAILED");
                    ++failed;
                    break;
            }
            out() << "  " << outcome << "  " << dir;
            if (r.outcome == SwingStore::ConvertResult::Outcome::Converted)
                out() << "  " << r.jsonBytes << " -> " << r.ppswBytes << " bytes, " << ms << " ms";
            if (!r.error.isEmpty())
                out() << "  — " << r.error;
            out() << "\n";
            out().flush();
            if (csvOut.device()) {
                csvOut << csvField(dir) << ',' << outcome << ',' << r.jsonBytes << ',' << r.ppswBytes
                       << ',' << ms << ',' << csvField(r.error) << "\n";
                csvOut.flush();
            }
            if (r.outcome == SwingStore::ConvertResult::Outcome::Failed && !cli.isSet(optGoOn)) {
                out() << "STOPPED at the first failure (its swing.json is untouched). "
                         "--keep-going to continue past it.\n";
                return 1;
            }
        }
    }

    out() << "\n";
    if (dryRun) {
        out() << pending << " to convert, " << already << " already swing.ppsw\n";
    } else {
        out() << converted << " converted, " << already << " already swing.ppsw, " << failed << " failed"
              << " in " << total.elapsed() / 1000 << " s\n";
        if (converted > 0)
            out() << "  JSON " << jsonTotal << " bytes -> .ppsw " << ppswTotal << " bytes ("
                  << QString::number(double(jsonTotal) / double(qMax<qint64>(1, ppswTotal)), 'f', 1)
                  << "x)\n";
    }
    return failed == 0 ? 0 : 1;
}
