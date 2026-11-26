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

// backfill_analysis_context — THE STEP-1 BACKFILL (refactor step 1, D4; Mark, 14 Nov 2026, DEC-34).
//
// Every swing captured before step 1.5 has no `analysisContext` block, so its analysis reads the
// LEGACY context: height 0 (unknown) and the men's club set, because the old capture never wrote
// either (H-02). This one-off migration (R-REC-1, R-PERS-7) records, in each such document, the
// context its capture would have recorded: the legacy context with the athlete's standing height
// and the club set ON THE SWING'S DATE, taken from the athlete's record in the app's settings.
// After it (and the library re-analysis that follows), the 3-D skeleton of every camera swing is
// scaled by the golfer's height and a women's or junior set reaches the low point.
//
//   backfill_analysis_context --library <root> --settings <PinPointStudio.ini>
//                             [--athlete <folder> ...] [--report <file.md>]
//                             [--write --backup-dir <dir> [--allow-in-progress]]
//
// A DRY RUN BY DEFAULT: it reads every swing document and the settings file, asks the one door
// (RecordingRepository::backfillAnalysisContext with PersistOptions::dryRun) what it would write,
// and writes the report — nothing in the library is written. `--write` writes, and only with
// `--backup-dir` (a folder outside the library, e.g. on the share): each document is copied there,
// as <backup>/<athlete>/<session>/<swing>/swing.ppsw, before its rewrite, and a swing whose copy
// fails is not written. Mark approves the real run after reading the dry-run report (DEC-34).
//
// GUARDS (review 1.7 r1): an --athlete must be a plain folder name (no `..`, no separator); the
// --report path must not be inside the library; --write refuses while PinPoint Studio runs on this
// machine (pgrep -x PinPointStudio / tasklist), and prints a hard warning when it cannot tell — and
// always warns that no app on ANY machine may have the library open (the share is not locked; the
// folder lock is step 2's: each write takes the session's lock). A session whose session.json says it is in
// progress (being recorded here or on another computer) is REFUSED by --write, its swings not written (D11:
// DEC-158), unless --allow-in-progress — for one a crash left open; a dry run reads it as any other. A JSON-era swing.json that is written becomes swing.ppsw (SwingStore::save
// removes the JSON and its sidecar): the report counts them, and the backup holds the original.
//
// What each swing gets (the store's rule, backfilledAnalysisContext in Stores/recording_header.h):
//   heightM   the document's own athlete.heightM when it has one, else the record's
//   clubSet   the record's sex and date of birth on the session's date (else the capture's)
//   club      the captured club as the legacy context reads it; its standard length again for that
//             club set and height
// What it never touches: a document that already has the block (captured since step 1.5, or an
// earlier backfill: a recorded input is never regenerated), an archived session's stub (restoring
// replaces it; backfill after the restore), a document a newer build saved (read-only), and a
// document whose record adds nothing (no height, the men's set): writing it would only freeze
// "unknown". Nothing else in a document changes; the analysis is NOT re-run (that is the library
// re-analysis, step 1's §5.2 step 4), so no number moves until then.
//
// The athlete's record is matched by the document's athlete.uuid, else its athlete.name, else the
// athlete folder's name (SwingPaths::sanitise of the record's name). The record's height is stored
// in feet (AthleteController::currentHeightM: heightValue × 0.3048).
//
// Exit status: 0 when nothing failed; 1 when a swing failed; 2 on a usage error or a refusal.

#include "../../src/Stores/swing_doc.h"
#include "../../src/Stores/swing_paths.h"
#include "../../src/Stores/swing_store.h"
#include "../../src/Stores/swing_store_repository.h"   // the one door to the swing files
#include "../../src/Stores/session_store.h"            // what names each swing's ShotRef (step 2.4)
#include "../../src/Stores/recording_header.h"         // legacyAnalysisContext, backfilledAnalysisContext
#include "real_path.h"                // realPath: links resolved, junctions too on Windows (#243)

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QProcess>
#include <QSettings>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>

using namespace pinpoint;

namespace {

QTextStream &out()
{
    static QTextStream s(stdout);
    return s;
}

// One athlete's record, as the app's settings hold it (AthleteController, group "athletes/<uuid>").
struct AthleteRecord {
    QString uuid, name, sex, dob, handedness;
    double  heightFt = 0.0;
    double  heightM() const { return heightFt > 0.0 ? heightFt * 0.3048 : 0.0; }   // currentHeightM()
};

QVector<AthleteRecord> readRecords(const QString &iniPath)
{
    QVector<AthleteRecord> out;
    QSettings s(iniPath, QSettings::IniFormat);   // read only: nothing is set, so nothing is synced back
    s.beginGroup(QStringLiteral("athletes"));
    for (const QString &uuid : s.childGroups()) {
        s.beginGroup(uuid);
        AthleteRecord r;
        r.uuid       = uuid;
        r.name       = s.value(QStringLiteral("name")).toString();
        r.sex        = s.value(QStringLiteral("sex")).toString();
        r.dob        = s.value(QStringLiteral("dob")).toString();
        r.handedness = s.value(QStringLiteral("handedness")).toString();
        r.heightFt   = s.value(QStringLiteral("heightValue"), 0.0).toDouble();
        s.endGroup();
        if (!r.name.isEmpty())
            out.append(r);
    }
    return out;
}

const char *setName(ClubSet s)
{
    switch (s) {
    case ClubSet::Women:  return "women";
    case ClubSet::Junior: return "junior";
    default:              return "men";
    }
}

QString mm(double v) { return v > 0.0 ? QString::number(v, 'f', 0) : QStringLiteral("none"); }
QString metres(double v) { return v > 0.0 ? QString::number(v, 'f', 3) + QStringLiteral(" m") : QStringLiteral("unknown"); }

// What one session would get, folded over its swings.
struct SessionTally {
    int swings = 0, change = 0, nothing = 0, recorded = 0, archived = 0, readOnly = 0, noDoc = 0, noRecord = 0, failed = 0;
    int jsonEra = 0;   // of `change`: JSON-era documents, which the write converts to swing.ppsw
    int heightFromDoc = 0;
    std::set<QString> heights, sets, matches;
    std::map<QString, std::pair<double, double>> standard;   // club → standard length before → after
    QStringList notes;                                        // per-swing exceptions
};

struct Totals {
    int swings = 0, change = 0, nothing = 0, recorded = 0, archived = 0, readOnly = 0, noDoc = 0, noRecord = 0, failed = 0;
    int jsonEra = 0;
    void add(const SessionTally &t)
    {
        swings += t.swings; change += t.change; nothing += t.nothing; recorded += t.recorded;
        archived += t.archived; readOnly += t.readOnly; noDoc += t.noDoc; noRecord += t.noRecord; failed += t.failed; jsonEra += t.jsonEra;
    }
};

const AthleteRecord *matchRecord(const QVector<AthleteRecord> &recs, const QJsonObject &root,
                                 const QString &athleteFolder, QString *how)
{
    const QJsonObject ath = root.value(QStringLiteral("athlete")).toObject();
    const QString uuid = ath.value(QStringLiteral("uuid")).toString();
    const QString name = ath.value(QStringLiteral("name")).toString();
    for (const AthleteRecord &r : recs)
        if (!uuid.isEmpty() && r.uuid == uuid) { *how = QStringLiteral("uuid"); return &r; }
    for (const AthleteRecord &r : recs)
        if (!name.isEmpty() && r.name == name) { *how = QStringLiteral("name"); return &r; }
    for (const AthleteRecord &r : recs)
        if (SwingPaths::sanitise(r.name) == athleteFolder) { *how = QStringLiteral("folder"); return &r; }
    return nullptr;
}

// Copies the swing's document into the backup tree before a real write. False = not copied.
bool backUp(const QString &swingDir, const QString &backupRoot, const QString &rel, QString *err)
{
    const SwingStore::DocInfo d = SwingStore::info(swingDir);
    if (!d.exists()) { *err = QStringLiteral("no document to back up"); return false; }
    const QString destDir = QDir(backupRoot).filePath(rel);
    if (!QDir().mkpath(destDir)) { *err = QStringLiteral("cannot create %1").arg(destDir); return false; }
    const QString dest = QDir(destDir).filePath(QFileInfo(d.path).fileName());
    if (QFile::exists(dest)) { *err = QStringLiteral("a backup is already there (%1): not overwritten").arg(dest); return false; }
    if (!QFile::copy(d.path, dest)) { *err = QStringLiteral("cannot copy to %1").arg(dest); return false; }
    if (QFileInfo(dest).size() != QFileInfo(d.path).size()) { *err = QStringLiteral("the copy's size differs"); return false; }
    return true;
}

// Is `path` (made absolute; it need not exist yet) the folder `root` or inside it? Real paths (realPath),
// so a `..`, a symlink or a Windows junction cannot hide it.
bool insideOf(const QString &path, const QString &root)
{
    const QFileInfo fi(path);
    QString real = pinpoint::migrate::realPath(fi.exists() ? fi.absoluteFilePath() : fi.absolutePath());
    if (!fi.exists() && !real.isEmpty()) real += QLatin1Char('/') + fi.fileName();
    const QString r = pinpoint::migrate::realPath(root);
    if (real.isEmpty() || r.isEmpty()) return false;
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    return real.compare(r, cs) == 0 || real.startsWith(r + QLatin1Char('/'), cs);
}

// Is PinPoint Studio running on THIS machine? 1 = yes, 0 = no, -1 = could not tell. The tool takes
// no folder lock (that is step 2's), so a write while the app runs could race the app's own writes.
int appRunning()
{
    QProcess pr;
#ifdef Q_OS_WIN
    pr.start(QStringLiteral("tasklist"), { QStringLiteral("/FI"), QStringLiteral("IMAGENAME eq PinPointStudio.exe"),
                                           QStringLiteral("/NH") });
#else
    pr.start(QStringLiteral("pgrep"), { QStringLiteral("-x"), QStringLiteral("PinPointStudio") });
#endif
    if (!pr.waitForStarted(5000) || !pr.waitForFinished(10000) || pr.exitStatus() != QProcess::NormalExit)
        return -1;
#ifdef Q_OS_WIN
    if (pr.exitCode() != 0) return -1;
    return QString::fromLocal8Bit(pr.readAllStandardOutput()).contains(QStringLiteral("PinPointStudio.exe"),
                                                                        Qt::CaseInsensitive) ? 1 : 0;
#else
    if (pr.exitCode() == 0) return 1;   // pgrep: 0 = a match
    if (pr.exitCode() == 1) return 0;   // 1 = none
    return -1;
#endif
}

void visitSwing(RecordingRepository &repo, const QVector<AthleteRecord> &recs, const QString &athleteFolder,
                const QString &sessionName, const QString &swingDir, bool write, const QString &backupRoot,
                SessionTally &t)
{
    ++t.swings;
    const QString swing = QDir(swingDir).dirName();
    if (!SwingStore::hasDocument(swingDir)) {
        ++t.noDoc;
        t.notes << swing + QStringLiteral(": no swing document (never analysed)");
        return;
    }
    QString err;
    const QJsonObject root = SwingStore::load(swingDir, &err);
    if (root.isEmpty()) {
        ++t.failed;
        t.notes << swing + QStringLiteral(": cannot read its document: ") + err;
        return;
    }
    if (root.contains(QStringLiteral("analysisContext"))) { ++t.recorded; return; }
    if (root.contains(QStringLiteral("archive"))) {
        ++t.archived;
        t.notes << swing + QStringLiteral(": archived stub, left alone (backfill after a restore)");
        return;
    }
    QString how;
    const AthleteRecord *rec = matchRecord(recs, root, athleteFolder, &how);
    if (!rec) {
        ++t.noRecord;
        t.notes << swing + QStringLiteral(": no athlete record matches (uuid, name or folder)");
        return;
    }
    t.matches.insert(how + QStringLiteral(" → ") + rec->name);

    // THE TOOL'S EDGE (DEC-90): the folder it was given becomes the shot's ShotRef here, once, over
    // a session reader of its own (a legacy session: D3's id from the athlete who owns its folder, by its athlete.json, else the
    // athlete the swing records; J-39; confirmed by Mark, 15 Nov 2026).
    const ShotRef shot = repo.shotOfFolder(swingDir);
    if (shot.isNull()) {
        ++t.failed;
        t.notes << swing + QStringLiteral(": NOT written, its session cannot be named (no session file, no athlete)");
        return;
    }
    AthleteRecordFacts facts;
    facts.heightM = rec->heightM();
    facts.sex     = rec->sex;
    facts.dobIso  = rec->dob;
    const recording::AnalysisContext before = legacyAnalysisContext(root);
    const recording::AnalysisContext after  = backfilledAnalysisContext(root, facts.heightM, facts.sex, facts.dobIso);

    PersistOptions opts;
    opts.dryRun = !write;
    if (write) {
        // Ask first (a dry run of the same door), so only a swing that WILL be written is backed up.
        PersistOptions ask;
        ask.dryRun = true;
        const WriteResult w = repo.backfillAnalysisContext(shot, facts, ask);
        if (w.status == WriteResult::Status::Written) {
            QString berr;
            if (!backUp(swingDir, backupRoot, athleteFolder + QLatin1Char('/') + sessionName + QLatin1Char('/') + swing, &berr)) {
                ++t.failed;
                t.notes << swing + QStringLiteral(": NOT written, the backup failed: ") + berr;
                return;
            }
        }
    }
    const bool jsonEraDoc = SwingStore::info(swingDir).format == SwingStore::Format::Json;
    const WriteResult w = repo.backfillAnalysisContext(shot, facts, opts);
    switch (w.status) {
    case WriteResult::Status::Written:
        ++t.change;
        // A JSON-era swing.json is rewritten as swing.ppsw (SwingStore::save removes the JSON and its
        // sidecar); the backup holds the original. Counted (from the document read before the write)
        // so the report says how many.
        if (jsonEraDoc)
            ++t.jsonEra;
        break;
    case WriteResult::Status::Unchanged: ++t.nothing; return;
    case WriteResult::Status::ReadOnly:
        ++t.readOnly;
        t.notes << swing + QStringLiteral(": read-only (saved by a newer build): ") + w.error;
        return;
    case WriteResult::Status::NoDocument:
        ++t.noDoc;
        t.notes << swing + QStringLiteral(": no document: ") + w.error;
        return;
    case WriteResult::Status::Failed:
        ++t.failed;
        t.notes << swing + QStringLiteral(": FAILED: ") + w.error;
        return;
    case WriteResult::Status::Locked:   // another writer holds the session folder (J-22): not written
        ++t.failed;
        t.notes << swing + QStringLiteral(": NOT written, the session is locked: ") + w.error;
        return;
    }
    const double docHeight = root.value(QStringLiteral("athlete")).toObject().value(QStringLiteral("heightM")).toDouble(0.0);
    if (docHeight > 0.0) ++t.heightFromDoc;
    t.heights.insert(metres(before.heightM) + QStringLiteral(" → ") + metres(after.heightM));
    t.sets.insert(QString::fromLatin1(setName(before.clubSet)) + QStringLiteral(" → ") + QString::fromLatin1(setName(after.clubSet)));
    const QString club = after.club.name.isEmpty() ? QStringLiteral("(undeclared)") : after.club.name;
    if (before.club.standardLengthMm != after.club.standardLengthMm || !t.standard.count(club))
        t.standard[club] = { before.club.standardLengthMm, after.club.standardLengthMm };
}

QString join(const std::set<QString> &s)
{
    QStringList l;
    for (const QString &v : s) l << v;
    return l.isEmpty() ? QStringLiteral("—") : l.join(QStringLiteral("; "));
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral("The step-1 analysis-context backfill (D4). A dry run unless --write."));
    p.addHelpOption();
    const QCommandLineOption libOpt(QStringLiteral("library"), QStringLiteral("the library root"), QStringLiteral("dir"));
    const QCommandLineOption iniOpt(QStringLiteral("settings"), QStringLiteral("the app's PinPointStudio.ini (read only)"), QStringLiteral("file"));
    const QCommandLineOption athOpt(QStringLiteral("athlete"), QStringLiteral("only this athlete folder (repeatable)"), QStringLiteral("folder"));
    const QCommandLineOption repOpt(QStringLiteral("report"), QStringLiteral("write the Markdown report here"), QStringLiteral("file"));
    const QCommandLineOption writeOpt(QStringLiteral("write"), QStringLiteral("WRITE the documents (needs --backup-dir)"));
    const QCommandLineOption bakOpt(QStringLiteral("backup-dir"), QStringLiteral("copy each document here before its rewrite"), QStringLiteral("dir"));
    const QCommandLineOption liveOpt(QStringLiteral("allow-in-progress"),
                                     QStringLiteral("--write also a session whose session.json says it is in progress (one a crash left open)"));
    p.addOptions({ libOpt, iniOpt, athOpt, repOpt, writeOpt, bakOpt, liveOpt });
    p.process(app);

    const QString library = QDir::cleanPath(p.value(libOpt));
    const QString ini = p.value(iniOpt);
    const bool write = p.isSet(writeOpt);
    const QString backup = p.isSet(bakOpt) ? QDir::cleanPath(p.value(bakOpt)) : QString();
    if (library.isEmpty() || !QDir(library).exists() || ini.isEmpty() || !QFileInfo::exists(ini)) {
        out() << "usage: backfill_analysis_context --library <root> --settings <ini> [--athlete <folder>] "
                 "[--report <md>] [--write --backup-dir <dir> [--allow-in-progress]]\n";
        return 2;
    }
    if (write) {
        const QString bakReal = backup.isEmpty() ? QString() : pinpoint::migrate::realPath(backup);
        if (bakReal.isEmpty() || !QFileInfo(bakReal).isDir()) {
            out() << "refused: --write needs --backup-dir, an existing folder (on the share)\n";
            return 2;
        }
        if (insideOf(bakReal, library)) {
            out() << "refused: the backup folder is inside the library\n";
            return 2;
        }
    }

    // An --athlete is one child folder of the library, by name: never a path. A `..` or a separator
    // would reach outside --library, and its backup path outside --backup-dir.
    for (const QString &a : p.values(athOpt)) {
        if (a.isEmpty() || a.contains(QLatin1String("..")) || a.contains(QLatin1Char('/'))
            || a.contains(QLatin1Char('\\')) || a == QLatin1String(".")) {
            out() << "refused: --athlete \"" << a << "\" is not a plain athlete folder name\n";
            return 2;
        }
    }
    // The report is the tool's own output: never written into the library.
    if (p.isSet(repOpt) && insideOf(p.value(repOpt), library)) {
        out() << "refused: the report path is inside the library\n";
        return 2;
    }
    if (write) {
        const int running = appRunning();
        if (running == 1) {
            out() << "refused: PinPoint Studio is running on this machine; close it before --write "
                     "(the tool takes no folder lock)\n";
            return 2;
        }
        if (running < 0)
            QTextStream(stderr) << "WARNING: could not tell whether PinPoint Studio is running on this machine. "
                                   "Close it before this write: the tool takes no folder lock.\n";
        QTextStream(stderr) << "WARNING: no PinPoint Studio on ANY machine may have this library open during the "
                               "write (the share is not locked).\n";
    }

    const QVector<AthleteRecord> recs = readRecords(ini);
    SessionStore                  sessions;         // names each swing's ShotRef (step 2.4)
    SwingStoreRecordingRepository repo(sessions);   // the composition root's one door

    QStringList athletes = p.values(athOpt);
    if (athletes.isEmpty())
        for (const QString &d : QDir(library).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
            athletes << d;

    QString md;
    QTextStream r(&md);
    r << "# Analysis-context backfill — " << (write ? "WRITE" : "dry run") << "\n\n"
      << "- When: " << QDateTime::currentDateTimeUtc().toString(Qt::ISODate) << "\n"
      << "- Library: `" << library << "`\n"
      << "- Athlete records: `" << ini << "` (read only)\n"
      << "- Rule: the legacy context, with the record's height (the document's own when it has one) and "
         "its club set on the swing's date; the standard length again for both. Nothing else changes; "
         "nothing is re-analysed.\n\n"
      << "## Athlete records\n\n| Name | uuid | Height (record) | Sex | Date of birth | Cannot fill |\n|---|---|---|---|---|---|\n";
    for (const AthleteRecord &a : recs) {
        QStringList cannot;
        if (!(a.heightM() > 0.0)) cannot << QStringLiteral("height (none on the record)");
        else if (std::abs(a.heightFt - std::round(a.heightFt)) < 1e-9)
            cannot << QStringLiteral("height is a whole number of feet (%1 ft): check it").arg(a.heightFt);
        if (a.sex.isEmpty()) cannot << QStringLiteral("sex (none: the men's set)");
        if (a.dob.isEmpty()) cannot << QStringLiteral("date of birth (none: a junior set cannot be decided)");
        r << "| " << a.name << " | `" << a.uuid << "` | " << QString::number(a.heightFt) << " ft = "
          << metres(a.heightM()) << " | " << (a.sex.isEmpty() ? QStringLiteral("—") : a.sex) << " | "
          << (a.dob.isEmpty() ? QStringLiteral("—") : a.dob) << " | "
          << (cannot.isEmpty() ? QStringLiteral("—") : cannot.join(QStringLiteral("; "))) << " |\n";
    }

    Totals total;
    QString body;
    QTextStream b(&body);
    for (const QString &athlete : athletes) {
        const QString athleteDir = QDir(library).filePath(athlete);
        QStringList sessions = QDir(athleteDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        std::vector<std::pair<QString, SessionTally>> rows;
        for (const QString &s : sessions) {
            const QStringList swings = SwingDocReader::findSwingDirs(QDir(athleteDir).filePath(s));
            if (swings.isEmpty()) continue;
            SessionTally t;
            // A session in progress is never written by a tool (D11: DEC-158); a dry run reads it as any other.
            const QString sessionDir = QDir(athleteDir).filePath(s);
            if (write && !p.isSet(liveOpt) && SessionStore::fileSaysInProgress(sessionDir)) {
                t.swings = int(swings.size());
                t.failed = int(swings.size());
                t.notes << QStringLiteral("REFUSED: ") + SessionStore::inProgressRefusal(sessionDir);
                out() << athlete << "/" << s << ": REFUSED — " << SessionStore::inProgressRefusal(sessionDir) << "\n";
                total.add(t);
                rows.emplace_back(s, std::move(t));
                continue;
            }
            for (const QString &sw : swings)
                visitSwing(repo, recs, athlete, s, sw, write, backup, t);
            out() << athlete << "/" << s << ": " << t.swings << " swings, " << t.change
                  << (write ? " written" : " would change") << ", " << t.failed << " failed\n";
            total.add(t);
            rows.emplace_back(s, std::move(t));
        }
        if (rows.empty()) continue;
        b << "\n## " << athlete << "\n\n| Session | Swings | " << (write ? "Written" : "Would change")
          << " | of them JSON-era (converted by the write) | Nothing to add | Already recorded | Height | Club set | Standard length (club: before → after, mm) | Record matched by |\n"
             "|---|---|---|---|---|---|---|---|---|---|\n";
        for (const auto &[s, t] : rows) {
            QStringList stds;
            for (const auto &[club, v] : t.standard)
                stds << QStringLiteral("%1: %2 → %3").arg(club, mm(v.first), mm(v.second));
            b << "| " << s << " | " << t.swings << " | " << t.change << " | " << t.jsonEra << " | " << t.nothing << " | " << t.recorded
              << " | " << join(t.heights) << (t.heightFromDoc ? QStringLiteral(" (%1 from the document)").arg(t.heightFromDoc) : QString())
              << " | " << join(t.sets) << " | " << (stds.isEmpty() ? QStringLiteral("—") : stds.join(QStringLiteral("; ")))
              << " | " << join(t.matches) << " |\n";
        }
        bool any = false;
        for (const auto &[s, t] : rows)
            if (!t.notes.isEmpty()) {
                if (!any) { b << "\nCannot fill, or left alone:\n\n"; any = true; }
                for (const QString &n : t.notes) b << "- " << s << "/" << n << "\n";
            }
    }

    r << "\n## Totals\n\n| Swings | " << (write ? "Written" : "Would change") << " | of them JSON-era (converted by the write) | Nothing to add | Already recorded | "
         "Archived stub | Read-only | No document | No record | Failed |\n|---|---|---|---|---|---|---|---|---|---|\n"
      << "| " << total.swings << " | " << total.change << " | " << total.jsonEra << " | " << total.nothing << " | " << total.recorded << " | "
      << total.archived << " | " << total.readOnly << " | " << total.noDoc << " | " << total.noRecord << " | " << total.failed << " |\n";
    r.flush();
    b.flush();
    md += body;

    if (p.isSet(repOpt)) {
        QFile f(p.value(repOpt));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            out() << "cannot write the report to " << p.value(repOpt) << "\n";
            return 1;
        }
        f.write(md.toUtf8());
    }
    out() << "\n" << total.swings << " swings: " << total.change << (write ? " written" : " would change")
          << " (" << total.jsonEra << " JSON-era, converted by the write), "
          << total.nothing << " nothing to add, " << total.recorded << " already recorded, " << total.archived
          << " archived, " << total.readOnly << " read-only, " << total.noDoc << " no document, " << total.noRecord
          << " no record, " << total.failed
          << " failed\n";
    out().flush();
    return total.failed == 0 ? 0 : 1;
}
