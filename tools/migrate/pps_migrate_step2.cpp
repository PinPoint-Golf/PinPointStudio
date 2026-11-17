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

// pps_migrate_step2 — THE STEP-2 DATA MIGRATIONS, M1–M4 (refactor step 2.8a; build/refactor/step-2/BRIEF.md §5.1;
// R-PERS-7; docs/developer/information_model.md §11). One tool, run once per machine, after Mark has read its
// dry-run report.
//
//   pps_migrate_step2 --library <root> [--settings <PinPointStudio.ini>] --report <file.md>
//                     [--export-records <file.json>] [--read-list <file.txt>]
//                     [--other-machine-export <file.json>] [--decisions <file.json>] [--machine <name>]
//                     [--write --backup-dir <dir> [--i-have-marks-approval] [--allow-in-progress]]
//
// ⚠ A DRY RUN BY DEFAULT, AND A DRY RUN WRITES NOTHING ANYWHERE BUT ITS REPORT (and the two appendices it is
// asked for). It reads the library, the settings file — from a private copy, so QSettings never opens the real
// one — and the other machine's export, builds the plan, and says what each migration would write and why
// anything would not be. migrate_step2_test hashes a fixture library and its settings before and after a dry
// run to prove it.
//
// `--write` carries the plan out. It needs `--backup-dir` (outside the library), and it refuses:
//   · a library that is not under the share's scratch folder (/mnt/swingdata/scratch/ — on the cabin PC also
//     C:/PinPointStudio/scratch/), and a backup folder or a settings file that is not under it either, unless
//     `--i-have-marks-approval` is given. That flag is for Mark's real run, after he has read the dry run;
//     nobody else passes it;
//   · while PinPoint Studio runs on this machine (pgrep / tasklist) — and no app on ANY machine may have the
//     library open: the share is not locked as a whole;
//   · while any folder lock (.pinpoint-lock, R-PERS-6) is held anywhere in the library;
//   · while any session's session.json says inProgress (D11), unless `--allow-in-progress` (a session a crash
//     left open), which then passes over those sessions.
//   · an archived session's mirror (archived.json's `location`, outside the library) that is not under the scratch
//     folder either, without the approval flag; a lock held in a mirror refuses the write too.
// EVERY PATH IS COMPARED RESOLVED (symlinks followed), for the library, the backups, the settings and each single
// write target: a symlink under scratch that points elsewhere is not under scratch, and a write whose target resolves
// outside the library's real folder is refused (reported; nothing written there).
// Before any write every file it will change or remove (and the settings file) is copied into
// <backup-dir>/step2-backup-<stamp>/ (library/…, settings/…, manifest.json with each copy's SHA-256), and each
// copy is read back; if one copy fails nothing is written. Every write is atomic (atomicWrite) under the
// folder's lock for that one write (J-11), and is read back.
//
// IDEMPOTENT: a second run finds every athlete.json, every session.json it wrote, no older golfer key left and
// the ★ on the record, and writes nothing.
//
// THE MIGRATIONS (each section of the report shows exactly what §5.1's "dry-run report shows" lists):
//
//   M1  THE ATHLETE RECORD (SET-01 → <athlete>/athlete.json, FILE-ATH-01; DEC-04, DEC-05, DEC-89, DEC-151).
//       Each of this machine's records goes to the folder its athlete.json already names, else the folder its
//       name gives (SessionStore's derivation); athlete.json is written (schema pinpoint.athlete/1, the record
//       as AthleteStore writes it, `migratedFrom`: the tool, the machine, the settings groups, any decided
//       fields), read back, and only then is the settings group removed. Reported: each record → its folder;
//       folders with no record (E-02's orphans), with the uuids and names their swings record; a folder whose
//       swings record two uuids, or two athletes' names (reported, NEVER split); records with no folder yet
//       (the folder is made).
//       THE ID IS NEVER SETTLED SILENTLY (J-12: M2 freezes every old session's id from it). The library's
//       athlete.json's id stands, and a decision naming another is refused. A decided `athleteId` is honoured when it
//       is this machine's, the other machine's or a uuid the folder's swings record (J-58); any other is refused,
//       with the reason. With no decision, an athlete whose swings record none of the id M1 would settle is blocked
//       ("needs a decision"), as is one the two machines give different ids.
//       H-09 — TWO MACHINES. `--export-records` writes this machine's records as a JSON appendix (a dry run
//       may write it: it is part of the report). The other machine's run takes that file as
//       `--other-machine-export` and lists, per athlete, every field where the two records differ, and
//       whether the two machines gave the athlete different ids. Nothing differing is merged silently: such
//       an athlete is not written until the decisions file picks (below). Where the library already holds
//       the athlete.json (the second machine's run), this machine's copy is compared with it instead, and its
//       settings group goes once every difference is decided. When the settled id is not this machine's,
//       `currentAthleteUuid` and the club-length prior's keys (SET-02, SET-05) are re-keyed to it.
//   M2  THE SESSION ID AND session.json for every existing session (FILE-SESS-01; DEC-41, DEC-88, DEC-150).
//       A session with no session.json gets one (`origin: backfilled`, this tool, what it read): its id is
//       D3's v5 uuid of the OWNING athlete's id (the one M1 settles, J-39) and the folder's name (J-12), its
//       start the earliest swing's capture time (clock.wallclock: the window start), its end the latest
//       window's end, marked `endInferred: true` (J-18, Mark 15 Nov 2026: the app's rule for a session a
//       crash left open, recording_header.h's captureEndUtcOf), its type the shots' capture.sessionType (else the name's token), status ended. A
//       session.json the app backfilled at a resume (J-19: start from folder times) has its start corrected
//       from the capture times. An archived session's mirror gets the same file. Reported: every session's
//       id, start, end and type; type disagreements (the shots against the name; the shots among themselves,
//       which Mark picks); sessions with no swing (the start is the folder's date, flagged); same-day
//       sessions whose order by name differs from their order by start (parity run B's expected set).
//       Never written: an InProgress session; an unreadable or newer session.json; a session whose folder no
//       athlete record owns (its id comes from its swings in memory, as today), or whose athlete M1 has not
//       settled. Trashed sessions (in .pinpoint-trash) are not listed at all.
//   M3  THE GOLFER'S INPUT OUT OF diagnostics.json (H-04; FILE-SESS-02, FILE-SHOT-02; DEC-13, DEC-160).
//       The older keys (`intent`, `screens`, `intentFrom` → session_input.json; `shotIntent`, `corrections` →
//       each shot's review, through RecordingRepository::editReview: a swing-file rewrite, R-REC-1's one-off).
//       The new homes win field by field (J-33/J-45): a field already there is never overwritten. Then the
//       CHECK: the golfer's input, resolved without the older keys (SessionDiagnosticsFiles::golferInput), must
//       read exactly as it did with them; only then are the keys removed from diagnostics.json (nothing else in
//       it changes). A key one of whose entries has no shot to go to (its folder gone) stays, reported. Each
//       athlete whose input moved has tendencies.json dropped (a cache, rebuilt from the new homes; J-45).
//       Reported: every older key, its new home and what happens to it; the swing files rewritten; and every
//       session whose diagnostics.json.* backups hold golfer input the live file lacks (H-04's 10 Jul focus) —
//       restored only where the decisions file asks.
//   M4  THE WRIST REFERENCE ★ (SET-17 → the athlete record's `wristReference`, a ShotRef; D5, DEC-152). The
//       setting's swing folder → its session's id (M2's) and swing number, on that athlete's athlete.json —
//       never over a ★ the record already holds. Reported: the path → the ShotRef, or "not found". The setting
//       itself is left (the Charts views still carry the path).
//
// THE DECISIONS FILE (Mark's picks; schema pinpoint.migrate-step2.decisions/1):
//   { "schema": "pinpoint.migrate-step2.decisions/1",
//     "athletes": { "<athlete folder>": { "athleteId": "<uuid>",
//                                         "fields": { "<key>": <the value> | { "from": "<machine>" | "library" } } } },
//     "sessionTypes": { "<athlete folder>/<session folder>": "wrist" },
//     "restore": [ { "session": "<athlete folder>/<session folder>", "from": "diagnostics.json.<tag>",
//                    "field": "declaredFocus" | "declaredMiss" | "screenAnswers" | "intentRuns"
//                             | "shotIntent:<n>" | "labelCorrections:<n>" } ] }
// A field's value is absolute (the same file serves both machines' runs); `from` names a machine as the
// reports name it, or the library's athlete.json.
//
// Exit status: 0 — done (a dry run, or a write with no failure; an athlete that needs a decision is reported,
// not a failure); 1 — a write failed somewhere (the report says what, and what was left as it was); 2 — a
// usage error or a refusal.
//
//   cmake --build build/tools-parity-ninja --target pps_migrate_step2 -j 6         (the tools tree)
//   ctest --test-dir build/tests -R '^migrate_step2_test$' --output-on-failure      (its test)

#include "migrate_step2.h"

#include "../../src/Core/atomic_write.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTextStream>

#include <cstdio>

using namespace pinpoint;

namespace {

QTextStream &err()
{
    static QTextStream s(stderr);
    return s;
}

bool writeOut(const QString &path, const QByteArray &bytes, const QString &what)
{
    const AtomicWriteResult w = atomicWrite(path, bytes);
    if (!w.ok) err() << "pps_migrate_step2: the " << what << " " << path << " could not be written: " << w.error << "\n";
    return w.ok;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QLatin1String(migrate::kToolName));
    QCoreApplication::setApplicationVersion(QLatin1String(migrate::kToolVersion));

    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral("The step-2 data migrations M1-M4. A dry run unless --write."));
    p.addHelpOption();
    const QCommandLineOption library(QStringLiteral("library"), QStringLiteral("The library root (holds the athlete folders)."), QStringLiteral("dir"));
    const QCommandLineOption settings(QStringLiteral("settings"), QStringLiteral("This machine's PinPointStudio.ini."), QStringLiteral("file"));
    const QCommandLineOption report(QStringLiteral("report"), QStringLiteral("The report (Markdown). Default: standard output."), QStringLiteral("file"));
    const QCommandLineOption exportTo(QStringLiteral("export-records"), QStringLiteral("Write this machine's records (M1's appendix) for the other machine's run."), QStringLiteral("file"));
    const QCommandLineOption readList(QStringLiteral("read-list"), QStringLiteral("Write the list of every file the run read."), QStringLiteral("file"));
    const QCommandLineOption other(QStringLiteral("other-machine-export"), QStringLiteral("The other machine's --export-records file."), QStringLiteral("file"));
    const QCommandLineOption decisions(QStringLiteral("decisions"), QStringLiteral("Mark's picks (see the tool's header)."), QStringLiteral("file"));
    const QCommandLineOption machine(QStringLiteral("machine"), QStringLiteral("This machine's name in the report (default: the host name)."), QStringLiteral("name"));
    const QCommandLineOption write(QStringLiteral("write"), QStringLiteral("Carry the plan out (default: a dry run that writes nothing)."));
    const QCommandLineOption backup(QStringLiteral("backup-dir"), QStringLiteral("--write: where every file is copied before it changes."), QStringLiteral("dir"));
    const QCommandLineOption approved(QStringLiteral("i-have-marks-approval"), QStringLiteral("--write outside the scratch folder: Mark's real run only."));
    const QCommandLineOption inProgress(QStringLiteral("allow-in-progress"), QStringLiteral("Pass over sessions left in progress instead of refusing."));
    p.addOptions({ library, settings, report, exportTo, readList, other, decisions, machine, write, backup, approved, inProgress });
    p.process(app);

    if (!p.isSet(library)) {
        err() << "pps_migrate_step2: --library is required\n";
        return 2;
    }
    migrate::Options o;
    o.library            = p.value(library);
    o.settingsFile       = p.value(settings);
    o.otherMachineExport = p.value(other);
    o.decisionsFile      = p.value(decisions);
    o.machine            = p.value(machine);
    o.write              = p.isSet(write);
    o.backupDir          = p.value(backup);
    o.approved           = p.isSet(approved);
    o.allowInProgress    = p.isSet(inProgress);
    o.scratchRoots       = { QStringLiteral("/mnt/swingdata/scratch") };
#ifdef Q_OS_WIN
    o.scratchRoots << QStringLiteral("C:/PinPointStudio/scratch");
#endif

    // The report and its appendices never land in an athlete or session folder (nor the library's root): run()
    // refuses them, before anything else is written.
    for (const QCommandLineOption *opt : { &report, &exportTo, &readList })
        if (p.isSet(*opt)) o.outputs << p.value(*opt);

    const migrate::Result r = migrate::run(o);
    if (r.refused && r.outputRefused) {
        err() << "pps_migrate_step2: REFUSED: " << r.refusal << "\n";
        return 2;
    }
    const QString md = migrate::reportMarkdown(r);
    bool ok = true;
    if (p.isSet(report)) ok = writeOut(p.value(report), md.toUtf8(), QStringLiteral("report")) && ok;
    else QTextStream(stdout) << md;
    if (p.isSet(exportTo) && !r.refused)
        ok = writeOut(p.value(exportTo), QJsonDocument(migrate::exportRecords(r)).toJson(QJsonDocument::Indented),
                      QStringLiteral("records export")) && ok;
    if (p.isSet(readList)) ok = writeOut(p.value(readList), (r.filesRead.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8(),
                                         QStringLiteral("read list")) && ok;

    if (r.refused) {
        err() << "pps_migrate_step2: REFUSED: " << r.refusal << "\n";
        return 2;
    }
    err() << "pps_migrate_step2: " << (o.write ? "write" : "dry run") << " done; " << r.writesPlanned()
          << " file(s) " << (o.write ? "planned" : "would be written") << "; " << r.log.failures.size() << " failure(s)\n";
    return (!ok || r.failed()) ? 1 : 0;
}
