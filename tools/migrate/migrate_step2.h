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

#pragma once

// migrate_step2 — THE ENGINE OF pps_migrate_step2, the step-2 data migrations M1–M4 (refactor step 2.8a;
// build/refactor/step-2/BRIEF.md §5.1; R-PERS-7). The command line, the guards it adds and the whole
// story are in pps_migrate_step2.cpp's header; this file is what the tool and migrate_step2_test share.
//
//   M1  the athlete record: this machine's settings (SET-01) → <athlete>/athlete.json (FILE-ATH-01)
//   M2  every existing session's SessionId and session.json (FILE-SESS-01; D3, J-12, J-19, J-39)
//   M3  the golfer's input out of diagnostics.json (H-04): session_input.json + the shots' review
//   M4  the Wrist chart's reference ★ (SET-17) → a ShotRef on the athlete's record (D5)
//
// ONE PLAN, TWO MODES. run() reads the library and the settings and builds the plan (what each migration
// would write, and why not where it would not); a dry run stops there and writes nothing anywhere — the
// caller writes the report. A write run (Options::write) carries the plan out, M1 → M2 → M3 → M4, after
// its backups, and re-reads every write. The same code decides both, so the dry-run report is what the
// write does.

#include "../../src/Vocabulary/Identity/identity.h"

#include <QDateTime>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace pinpoint::migrate {

inline constexpr const char *kToolName    = "pps_migrate_step2";
inline constexpr const char *kToolVersion = "1";
// The other machine's export (M1's appendix) and the decisions file Mark's picks are written into.
inline constexpr const char *kExportSchema    = "pinpoint.migrate-step2.records/1";
inline constexpr const char *kDecisionsSchema = "pinpoint.migrate-step2.decisions/1";

struct Options {
    QString library;               // the library root (the folder holding the athlete folders)
    QString settingsFile;          // this machine's PinPointStudio.ini; empty = no settings (M1, M4 see none)
    QString otherMachineExport;    // the other machine's records (its run's --export-records); optional
    QString decisionsFile;         // Mark's picks (M1 fields and ids, M2 types, M3 restores); optional
    QString machine;               // this machine's name in the report and the export (default: host name)

    bool    write = false;         // false: DRY RUN — nothing is written anywhere but the report
    QString backupDir;             // --write: where every file is copied before it changes (required)
    bool    approved = false;      // --i-have-marks-approval: a library outside the scratch roots
    bool    allowInProgress = false;   // --allow-in-progress: skip InProgress sessions instead of refusing
    // A write may touch a library (and its backups) only under one of these, unless approved. The tool
    // passes the share's scratch folder; the test passes its own temporary folder.
    QStringList scratchRoots;
    // The files the caller will write (the report, its appendices): refused when inside an athlete or session
    // folder, before anything is written.
    QStringList outputs;
    // The target machine's rig folder (FILE-APP-07; the tool's --rig-dir, by default this machine's, as the app finds
    // it). A write run refuses when its rig.json has taken the club-length group from settings: the Rig keeps the
    // learned lengths since then, and a re-keyed settings map would be copied over them at the next launch (the
    // Product Owner, 27 Nov 2026). A write run with a settings file and no rig folder refuses too. A dry run reads none.
    QString rigFolder;
    // Tests: whether the app is running on this machine (the default asks the OS).
    std::function<bool()> appRunning;
    // Tests: called after M3 wrote a session's new homes, before its check (a test can make the check fail).
    std::function<void(const QString &sessionFolder)> afterNewHomesForTest;
};

// ── What M1 found and decided, per athlete folder ────────────────────────────────────────────────────

struct RecordSource {
    QString     origin;            // "this machine (<name>)", "<other machine>", "library"
    AthleteId   id;
    QString     settingsKey;       // this machine's settings group (SET-01), when from settings
    QJsonObject record;            // AthleteRecord::toMap as JSON (SET-01's keys)
};

struct FieldDifference {
    QString    key;                // SET-01's key ("heightValue", "clubs", …)
    QJsonValue thisMachine;        // Undefined: not held there
    QJsonValue otherMachine;
    QJsonValue library;
    bool       decided = false;
    QJsonValue chosen;             // the value written, when decided (or when only one source differs)
};

struct AthletePlan {
    enum class Outcome {
        WriteNew,            // athlete.json written from the record (folder made when missing)
        Update,              // the library's athlete.json rewritten with decided values
        AlreadyInLibrary,    // athlete.json holds it; only this machine's settings copy goes (or nothing)
        NothingHere,         // no record on this machine and none in the library (the other machine's only)
        NoRecord,            // a folder with sessions that no record names (E-02's orphans)
        Blocked              // needs Mark's decision (or a by-hand fix); nothing is written for it
    };
    QString     token;             // the athlete folder's name
    QString     folder;            // its path
    bool        folderExists = false;
    int         sessions = 0;
    Outcome     outcome = Outcome::NothingHere;
    QString     why;               // Blocked / NoRecord: what is needed
    AthleteId   id;                // the id M1 settles (null: none)
    bool        idProvisional = false;   // ids differ between machines and are not decided yet
    QJsonObject record;            // the record athlete.json holds after M1
    QList<RecordSource>    thisMachine, otherMachine;
    std::optional<RecordSource> library;
    bool        libraryReadOnly = false;  // a newer or unreadable athlete.json: never written
    QList<FieldDifference> differences;
    QStringList settingsGroupsToRemove;   // this machine's SET-01 groups M1 removes after the read-back
    QMap<QString, int> headerIds;      // swing headers' athlete.uuid → swings
    QMap<QString, int> headerNames;    // swing headers' athlete.name → swings
    QStringList notes;
};

// ── M2, per session ──────────────────────────────────────────────────────────────────────────────────

struct SessionPlan {
    enum class Outcome {
        Write,               // a backfilled session.json written (in the library, and the archive mirror)
        CorrectStart,        // an app-backfilled session.json (J-19): its start corrected from capture times
        HasFile,             // a session.json the app wrote at Start: nothing to do
        AlreadyMigrated,     // this tool's session.json, as it would write it now
        Skipped              // not written (why says it): InProgress, unreadable, newer, no owner, …
    };
    QString   athleteToken;
    QString   folder;
    QString   name;                // the folder's name
    SessionId id;
    AthleteId owner;
    bool      ownerFromHeaders = false;   // no athlete record owns the folder: the swings' athlete.uuid
    bool      ownerProvisional = false;   // M1 has not settled the owner's id yet
    Outcome   outcome = Outcome::Skipped;
    QString   why;
    int       swings = 0;
    QDateTime startUtc, endUtc;    // from the swings' capture times (end: the latest window's end)
    bool      timesFromFolderDate = false;   // no swing: the start is the folder name's day (flagged)
    QString   type;                // the type written ("" none)
    QString   nameType;            // the folder name's type token, by name
    QMap<QString, int> shotTypes;  // capture.sessionType by name → swings ("" = not recorded)
    QStringList swingUuids;        // the athlete.uuid its swings record (J-39: the owner's id is used instead)
    bool      typeDisagreement = false;   // the shots disagree among themselves, or with the name
    bool      typeNeedsDecision = false;  // the shots disagree among themselves: Mark picks
    QDateTime previousStartUtc;    // CorrectStart: the app's backfilled start
    bool      archived = false;
    QString   mirrorFolder;        // the archive mirror's copy of the session, when archived
    bool      inProgress = false;
    QDate     nameDay;             // the folder name's date
};

struct DayOrderChange {
    QString     athleteToken;
    QDate       day;
    QStringList byName;            // today's order (the folder names' counters)
    QStringList byStart;           // the order by start time
};

// ── M3, per session ──────────────────────────────────────────────────────────────────────────────────

struct GolferMove {
    QString key;                   // the older key ("intent.declaredMiss", "shotIntent.7", …)
    QString target;                // "session_input.json declaredMiss", "swing_0007 review.shotIntent", …
    QString value;                 // as a short string, for the report
    enum class Action { Move, AlreadyThere, NewHomeDiffers, Empty, ShotGone } action = Action::Move;
};

struct RecoveredInput {
    QString backupFile;            // diagnostics.json.<tag>
    QString field;                 // "declaredFocus", "declaredMiss", "screenAnswers", "intentRuns",
                                   // "shotIntent:<n>", "labelCorrections:<n>"
    QString value;                 // what the backup holds
    QString live;                  // what the session holds now ("" none)
    bool    restore = false;       // the decisions file asks for it to be restored
    QJsonValue json;               // the backup's value, for the restore
};

struct GolferPlan {
    QString athleteToken;
    QString folder;
    QString name;
    bool    skipped = false;
    QString why;
    QStringList legacyKeys;        // the older keys diagnostics.json holds
    QList<GolferMove> moves;
    bool    writeSessionInput = false;
    QJsonObject sessionInputFields;    // the fields M3 adds to session_input.json
    QMap<int, QJsonObject> reviewEdits;   // swing number → { shotIntent?, labelCorrections? }
    QStringList keysToRemove;      // the older keys removed from diagnostics.json after the check
    QStringList keysKept;          // kept: an entry has no target (a shot whose folder is gone)
    QList<RecoveredInput> recovered;
    bool    restoring = false;     // at least one recovered input will be restored
};

// ── M4 ───────────────────────────────────────────────────────────────────────────────────────────────

struct WristPlan {
    QString settingValue;          // ui/wristReferenceSwingDir as this machine holds it ("" none)
    enum class Outcome { NotSet, NotFound, Write, AlreadyThere, RecordHasOther, Waits } outcome = Outcome::NotSet;
    QString why;
    QString athleteToken;
    ShotRef shot;
};

// ── The whole run ────────────────────────────────────────────────────────────────────────────────────

struct WriteLog {
    QStringList written;           // every file written or created (absolute)
    QStringList removed;           // every file removed (tendencies.json)
    QStringList settingsChanges;   // "removed athletes/<uuid>", "currentAthleteUuid <old> → <new>", …
    QStringList failures;          // what failed, and what was left as it was
    QString     backupRoot;        // where the backups of this run are
};

struct Result {
    bool        refused = false;   // a guard refused the run before anything (the reason in `refusal`)
    bool        outputRefused = false;   // the refusal was of an output path: no report may be written there
    QString     refusal;
    Options     options;
    QDateTime   at;
    QString     machine;
    QString     libraryInSettings;     // SET-03 as the settings file holds it
    QString     otherMachine;          // the other export's machine name ("" none)
    QStringList locksHeld;             // .pinpoint-lock files found in the library
    QStringList inProgress;            // sessions whose session.json says inProgress
    QStringList notAthleteFolders;     // library folders with no session and no record (not migrated)
    QList<AthletePlan>    athletes;
    QList<SessionPlan>    sessions;
    QList<DayOrderChange> orderChanges;
    QList<GolferPlan>     golfer;
    QStringList           tendenciesToDrop;   // athlete folders whose tendencies.json M3 drops
    WristPlan             wrist;
    QStringList           otherIdReferences;  // this machine's settings naming an id M1 changes
    QStringList           filesRead;          // every file the run opened, sorted (for the read proof)
    WriteLog              log;                // a write run's record
    QStringList           warnings;

    int  writesPlanned() const;   // how many files a write run would create, change or remove
    bool failed() const { return refused || !log.failures.isEmpty(); }
};

// Build the plan and, when options.write, carry it out. Never throws; a refusal is in Result::refused.
Result run(const Options &options);

// The report (Markdown) — what the dry run shows Mark, and a write run's record.
QString reportMarkdown(const Result &result);

// M1's appendix: this machine's records, as the other machine's run reads them (--other-machine-export).
QJsonObject exportRecords(const Result &result);

// The settings file's athlete records, read from a private copy (a dry run never opens the real file with
// QSettings). Exposed for the test.
QList<RecordSource> readSettingsRecords(const QString &iniPath, const QString &origin, QString *librarySetting = nullptr,
                                        QString *wristSetting = nullptr, QString *currentAthlete = nullptr,
                                        QStringList *priorKeys = nullptr, QString *error = nullptr);

// The real path: symlinks resolved (a path not there yet, through its nearest existing folder).
QString resolvedPath(const QString &path);
// Is `path`, RESOLVED, inside one of `roots`, resolved? Only the real paths are compared: a symlink under a root that
// points elsewhere is not under it.
bool isUnder(const QString &path, const QStringList &roots);

// May the run put a file or folder (a backup, the report, an appendix) at `path`? Never inside an athlete or
// session folder of the library, nor directly in the library's root. (The share's own library root holds
// scratch/ beside the athlete folders, and §5.1 puts the backups there: that is allowed.)
bool outputAllowed(const Result &result, const QString &path);

} // namespace pinpoint::migrate
