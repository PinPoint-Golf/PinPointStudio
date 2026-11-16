/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

// AthleteStore — AthleteRepository over the library's athlete folders and, until migration M1, this
// machine's settings (refactor step 2.3; the interface and its rules: src/DomainModel/athlete_repository.h).
// Adapters ring.
//
// THE ATHLETE FILE, `<athlete>/athlete.json` (FILE-ATH-01; DEC-05, DEC-151; name: athlete_files.h), written
// whole through atomicWrite (src/Core/atomic_write.h) under the athlete folder's advisory lock (FolderLock,
// held for that one write only: J-11):
//
//     {
//       "schema":    "pinpoint.athlete/1",      checked on read; a newer one is read-only
//       "athleteId": "<uuid>",                  the AthleteId (v4, minted at creation)
//       "record":    { "name": …, "handedness": …, "heightValue": …, … "clubs": { <club id>: {…} },
//                      "wristReference": "<sessionId>/<swing number>" }
//                                               exactly SET-01's keys as stored today (DEC-89: without
//                                               lastSessionAt / sessionCount), the bag included; keys
//                                               this build does not know are kept
//       "migratedFrom": { … }                   M1's (step 2.8), when the migration wrote the file
//     }
//
// There is NO `folder` key (D4: DEC-151): the athlete's folder is the one its athlete.json is found in.
// Top-level keys this build does not know are kept on every rewrite.
//
// WHERE AN ATHLETE IS FOUND
//   - `<library>/<folder>/athlete.json` — every athlete created from step 2.3 on (its folder made at
//     creation: the sanitised name, with -2, -3 … when taken), and every one M1 has moved;
//   - this machine's settings group `athletes/<uuid>` (SET-01) for an athlete M1 has not moved: read from
//     there and written back there (never into an athlete.json: M1 writes that, reconciling the two
//     machines, H-09). Its folder is today's derivation, SessionStore::athleteFolder — library + the
//     name sanitised. An athlete.json with the same id WINS.
//
// ⚠ A READ NEVER WRITES. list() and load() write nothing anywhere.
//
// DELETE (Athlete::remove → trash): the WHOLE folder, sessions and all, to the OS trash; where the volume
// has none (the share), into the library's own trash with the layout housekeeping already purges
// (FILE-LIB-01): `<library>/.pinpoint-trash/<athlete>/<stamp>_<athlete>` — one entry, inside the library
// (SwingPaths::trashPath is never handed an athlete folder: it would treat it as a session and land it
// outside the library, brief §3.6). A settings athlete's record is written into its folder as
// athlete.json first, so the trashed folder carries its record and a restore brings the athlete back.
// Its settings group goes last, only once the folder has moved.
//
// A FOLDER THAT IS NOT THIS ATHLETE'S ALONE IS NEVER MOVED (J-30). A settings athlete's folder follows
// its name, so two athletes can resolve to one folder (look-alike names, a rename onto another's name).
// Then only this athlete's record goes — its settings group, or its athlete.json to the trash — exactly
// the delete before step 2, and the folder and the other athlete's sessions stay. A folder whose
// athlete.json names ANOTHER athlete is refused outright.
//
// Not thread-safe (one owner thread, the GUI thread in the app), except readActivity() and the static
// appendTuningLog(), which keep no state.
//
//   cmake --build build/tests --target athlete_repository_test -j 6
//   ctest --test-dir build/tests -R '^athlete_repository_test$' --output-on-failure

#include "../Core/folder_lock.h"
#include "../DomainModel/athlete_repository.h"

#include <QHash>
#include <QJsonObject>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

namespace pinpoint {

class AthleteStore final : public AthleteRepository
{
public:
    static constexpr const char *kSchema = "pinpoint.athlete/1";

    // `libraryRoot` is read on every call (SET-03, athleteLibraryPath; empty = the app-data fallback,
    // as SessionStore's); `sessions` is the session repository the athletes' index reads through.
    AthleteStore(std::function<QString()> libraryRoot, SessionRepository *sessions);
    ~AthleteStore() override;

    QList<AthleteId>    list() override;
    Athlete            *load(const AthleteId &id) override;
    AthleteCreateResult create(const AthleteRecord &record) override;
    WriteResult         save(Athlete &athlete, const AthleteRecord &record) override;
    AthleteRemoval      trash(const AthleteId &id) override;
    AthleteActivity     readActivity(const SessionOwner &owner) const override;
    // The Athlete's own append (refactor step 3.4b-i): appendTuningLog() on its folder's log (FILE-ATH-04).
    WriteResult         appendTuningLine(const Athlete &athlete, const QByteArray &line) override;

    // THE TUNING LOG's append (FILE-ATH-04, append-only, never rewritten; DEC-43): one line added to
    // `logFile` under its athlete folder's lock, for that one append (brief §4.9). Static, because its
    // caller today is a QML-made model with no repository to hand, and an append needs no identity map.
    static WriteResult appendTuningLog(const QString &logFile, const QByteArray &line,
                                       int lockWaitMs = FolderLock::kWaitMs);

    // The doors every Athlete this store makes hands down to its sessions' shots (refactor step 3.4a): the
    // composition root's RecordingRepository and RecordingReader. Set once, before the athletes are used;
    // the athletes already made get them too.
    void setShotDoors(const ShotDoors &doors);
    // The doors every held Athlete's sessions' diagnostics work through (refactor step 3.5-ii; the parallel run, D14:
    // set by the composition root only with PINPOINT_DIAG_PARALLEL=1, `persist` false), as setShotDoors.
    void setDiagnosticsDoors(const DiagnosticsDoors &doors);
    // A Session any held Athlete has loaded, by id (the receiver's lookup, §4.4 step 6); null when none has.
    Session *loadedSession(const SessionId &id) const;

    // The library folder the athletes are listed from (the app-data fallback when SET-03 is empty).
    QString libraryDir() const;

    // Tests: never use the OS trash (the library's own trash instead), and how long a write waits for
    // a held lock.
    void setOsTrashForTest(bool on) { m_osTrash = on; }
    void setLockWaitForTest(int ms) { m_lockWaitMs = ms; }

private:
    struct Found {
        AthleteId       id;
        QString         folder;
        SessionOwner    owner;
        AthleteRecord   record;
        Athlete::Source source   = Athlete::Source::Library;
        bool            readOnly = false;
        QJsonObject     file;          // athlete.json as read (unknown keys kept)
        QString         settingsKey;   // the settings group's name, for a settings athlete
    };

    QList<Found> scan() const;
    Athlete     *hold(const Found &f);
    SessionOwner ownerFor(const AthleteId &id, const QString &token) const;
    bool         folderTaken(const QString &token) const;
    WriteResult  writeFile(const AthleteId &id, const QString &folder, const AthleteRecord &record);
    WriteResult  writeSettings(const QString &key, const AthleteRecord &before, const AthleteRecord &after);
    // A folder, or one file of an athlete folder (`athleteDir`), to the OS trash, else the library's own.
    bool         trashPath(const QString &path, const QString &athleteDir, QString *where, QString *why) const;
    int          lockWait() const { return m_lockWaitMs >= 0 ? m_lockWaitMs : FolderLock::kWaitMs; }

    std::function<QString()>              m_root;
    SessionRepository                    *m_sessions = nullptr;
    QHash<AthleteId, Athlete *>           m_held;        // the identity map: one Athlete per id (DEC-52)
    std::vector<std::unique_ptr<Athlete>> m_owned;       // every Athlete ever handed out, for the store's life
    QHash<AthleteId, QJsonObject>         m_files;       // each library athlete's athlete.json as last read
    QHash<AthleteId, QString>             m_settingsKey; // each settings athlete's group name
    ShotDoors                             m_shotDoors;
    DiagnosticsDoors                      m_diagnosticsDoors;
    bool                                  m_osTrash    = true;
    int                                   m_lockWaitMs = -1;
};

} // namespace pinpoint
