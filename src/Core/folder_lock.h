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

// FolderLock — the advisory lock on one session or athlete folder (R-PERS-6; refactor step 2 §4.9, D11).
// Platform ring: Qt only, so every ring may use it (src/Gui/tests/architecture_lint_test.cpp).
//
// The lock is a file, `<folder>/.pinpoint-lock` (QLockFile: it holds the pid, the application and the
// host). It is ADVISORY: it stops another PinPoint writer — the app on the other machine, a tool —
// from writing the same folder at the same moment; it does not stop anything else. The archive never
// copies it (dot-files are skipped).
//
// The rules (§4.9):
//   - holds are SHORT. A repository takes the lock for one write and lets it go; a tool holds it for
//     its work on one folder. lock() waits up to kWaitMs (5 s) and then answers Locked, which the
//     caller reports — never a silent skip;
//   - a lock is STALE after kStaleMs (60 s). The Mac and GOLFSIMPC share one library, and QLockFile
//     cannot ask the other machine whether its process is alive, so across machines only AGE frees a
//     lock. That is why every hold must be short. (On one machine a lock left by a process that has
//     died is freed at once.)
//   - lock() never makes the folder: a folder that is not there (trashed, never made) fails, it is not
//     re-created around a lone lock file.
//
// JOINING THIS THREAD'S OWN HOLD (step 2.8b; J-47). QLockFile is not re-entrant: a second lock object on a
// folder whose lock this very thread already holds would wait for itself and answer Locked. lock() keeps
// that (a second object is a second writer, on any thread). lockOrJoin() is for a write that may run
// INSIDE another hold of the same folder on the same thread — the swing door's per-write lock, called by
// the archiver while it holds the session's lock around one swing: when this thread already holds the
// folder's lock (taken by lock() or lockOrJoin() on another object), it joins that hold — Acquired, no
// file touched, and its unlock() lets nothing go; otherwise it is lock(). Another thread's hold is never
// joined: it waits and answers Locked as lock() does, so the archiver's hold still shuts out the app's own
// review edits on other threads (H-10). Holds nest like scopes: the joined object lets go first.
//
// The repositories take it from step 2.2 on; the swing door's writes (RecordingRepository) from 2.8b.
//
//   cmake --build build/tests --target folder_lock_test -j 6
//   ctest --test-dir build/tests -R '^folder_lock_test$' --output-on-failure

#include <QString>

#include <memory>

class QLockFile;

namespace pinpoint {

class FolderLock
{
public:
    enum class Result {
        Acquired,   // this object holds the lock
        Locked,     // another holder has it and did not let go within the wait (or it is not stale yet)
        Failed      // no lock could be made at all: no such folder, or it is not writable
    };

    static constexpr int kWaitMs  = 5 * 1000;    // how long lock() waits for another holder
    static constexpr int kStaleMs = 60 * 1000;   // a lock older than this is taken over

    // ".pinpoint-lock"
    static QString fileName();

    explicit FolderLock(const QString &folder, int staleMs = kStaleMs);
    ~FolderLock();   // lets go if held
    FolderLock(const FolderLock &) = delete;
    FolderLock &operator=(const FolderLock &) = delete;

    // Take the lock, waiting up to `waitMs` for another holder. Holding it already answers Acquired.
    Result lock(int waitMs = kWaitMs);
    // lock(), except that a hold of this folder that THIS THREAD already has (through another object) is
    // joined instead of waited for (see the header). joined() says which happened.
    Result lockOrJoin(int waitMs = kWaitMs);
    void unlock();
    bool isHeld() const;
    bool joined() const { return m_joined; }

    const QString &folder() const { return m_folder; }
    QString lockPath() const;

    // After Locked: who holds it ("pid 4242, PinPointStudio on GOLFSIMPC"), when the file says.
    QString holder() const { return m_holder; }
    // After Locked or Failed: a sentence naming the folder, for the caller to report.
    QString errorString() const { return m_error; }

private:
    QString registryKey() const;

    QString                    m_folder;
    std::unique_ptr<QLockFile> m_lock;
    bool                       m_joined = false;   // lockOrJoin() joined this thread's hold
    QString                    m_key;              // the registry key taken when the file was acquired; the
                                                   // entry is removed by it (never recomputed: a folder that
                                                   // vanished mid-hold resolves to another spelling)
    QString                    m_holder;
    QString                    m_error;
};

} // namespace pinpoint
