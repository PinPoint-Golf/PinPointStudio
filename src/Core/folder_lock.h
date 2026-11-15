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
// Nothing calls it yet but its test; the repositories take it from step 2.2 on.
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
    void unlock();
    bool isHeld() const;

    const QString &folder() const { return m_folder; }
    QString lockPath() const;

    // After Locked: who holds it ("pid 4242, PinPointStudio on GOLFSIMPC"), when the file says.
    QString holder() const { return m_holder; }
    // After Locked or Failed: a sentence naming the folder, for the caller to report.
    QString errorString() const { return m_error; }

private:
    QString                    m_folder;
    std::unique_ptr<QLockFile> m_lock;
    QString                    m_holder;
    QString                    m_error;
};

} // namespace pinpoint
