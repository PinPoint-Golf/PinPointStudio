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

// SessionStore — SessionRepository over the session folders (refactor step 2.2; the interface and its
// rules: src/DomainModel/session_repository.h). Adapters ring.
//
// THE SESSION FILE, `<session>/session.json` (FILE-SESS-01; DEC-149; keys DEC-160: IM-S2-1), written
// whole through atomicWrite (src/Core/atomic_write.h) under the session folder's advisory lock
// (FolderLock, held for that one write only: J-11):
//
//     {
//       "schema":    "pinpoint.session/1",       checked on read; a newer one is read-only
//       "sessionId": "<uuid>",                   v4 at Start; D3's v5 for a session older than step 2
//       "athleteId": "<uuid>",                   absent when no athlete was selected
//       "type":      "wrist",                    by name (DEC-88); absent for none
//       "status":    "inProgress" | "ended",
//       "startedAt": "2026-11-15T10:04:05.123Z", ISO 8601 UTC with milliseconds
//       "endedAt":   "…",                        absent while InProgress, and when the end is not known
//       "origin":    { "kind": "captured" }      written at Start; or { "kind": "backfilled", "tool",
//                                                "version", "at", "read" } — how an inferred start was
//                                                inferred (P1)
//     }
//
// Keys this build does not know are kept on every rewrite.
//
// ⚠ A READ NEVER WRITES. enumerate, todaysSession, open and openFolder only read; a session without a
// file is read from its folder's name, in memory (D3). Only start, resume, end and allocateSwing write.
//
// THE ATHLETE'S FOLDER (until step 2.3): `<libraryRoot>/<sanitise(name), else sanitise(uuid)>`, with
// `<AppDataLocation>/swings` for an empty library — today's derivation, made here only (athleteFolder).
//
// Not thread-safe: one owner thread (the GUI thread in the app).
//
//   cmake --build build/tests --target session_repository_test -j 6
//   ctest --test-dir build/tests -R '^session_repository_test$' --output-on-failure

#include "../DomainModel/session_repository.h"

#include <QHash>
#include <QJsonObject>
#include <QTimeZone>

#include <functional>

namespace pinpoint {

class SessionStore final : public SessionRepository
{
public:
    static constexpr const char *kSchema = "pinpoint.session/1";

    SessionStore() = default;

    SessionStartResult start(const SessionOwner &owner, const QString &type,
                             const QString &namingPattern) override;
    SessionId          todaysSession(const SessionOwner &owner, const QString &type) override;
    WriteResult        resume(const SessionId &id, const QString &type) override;
    WriteResult        end(const SessionId &id, bool discardIfEmpty) override;
    SessionStartResult restartInLibrary(const SessionId &running, const SessionOwner &owner,
                                        const QString &namingPattern, QString *oldEndProblem = nullptr) override;
    QList<SessionIndexEntry>         enumerate(const SessionOwner &owner) override;
    std::optional<SessionIndexEntry> open(const SessionId &id) const override;
    SessionId       openFolder(const QString &folder, const AthleteId &legacyAthlete = {}) override;
    SwingAllocation allocateSwing(const SessionId &id) override;
    QList<ShotRef>  shots(const SessionId &id) const override;
    QString         swingFolder(const ShotRef &shot) const override;

    // Today's athlete-folder derivation (until 2.3's Athlete::folder()).
    static QString athleteFolder(const SessionOwner &owner);
    // The folder name's type token for a type by name (SET-04): "swing" → "Swing", "grf" → "GRF";
    // "" for none (the name then says "session").
    static QString folderTypeLabel(const QString &type);

    // Tests: the clock (UTC) and the zone "today" is read in; how long a write waits for a held lock.
    void setClockForTest(std::function<QDateTime()> nowUtc) { m_now = std::move(nowUtc); }
    void setTimeZoneForTest(const QTimeZone &zone) { m_zone = zone; }
    void setLockWaitForTest(int ms) { m_lockWaitMs = ms; }

private:
    struct Held {
        SessionIndexEntry entry;
        QJsonObject       file;              // the session file as read (unknown keys kept)
        bool              createdHere = false;   // start() made it this run: the empty discard may take it
    };
    enum class FileState { Missing, Read, Unreadable };

    QDateTime nowUtc() const;
    QDate     today() const;
    // Read `folder`'s session file into an entry. Missing / Unreadable leave `out` untouched.
    FileState readFile(const QString &folder, SessionIndexEntry &out, QJsonObject &file) const;
    // The entry for one folder: its file's, or D3's legacy identity. Null identity: not a session.
    SessionIndexEntry readFolder(const QString &folder, const AthleteId &owner, QJsonObject &file,
                                 bool requireSessionName) const;
    // Write `h`'s entry into its folder's session file (merged over h.file), under the folder's lock.
    WriteResult write(Held &h);
    // End every other InProgress session in `athleteDir` (their end time: unknown).
    void endOthersInProgress(const QString &athleteDir, const SessionId &except);
    // Bring an entry read from disk into the identity map, keeping what only the map knows.
    Held &hold(const SessionIndexEntry &entry, const QJsonObject &file);

    QHash<SessionId, Held>      m_held;
    std::function<QDateTime()>  m_now;
    QTimeZone                   m_zone = QTimeZone::systemTimeZone();
    int                         m_lockWaitMs = -1;   // -1: FolderLock::kWaitMs
};

} // namespace pinpoint
