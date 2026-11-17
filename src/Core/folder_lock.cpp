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

#include "folder_lock.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLockFile>
#include <QMutex>
#include <QThread>

namespace pinpoint {

namespace {

// Which thread of this process holds each folder's lock file (lockOrJoin's question). Only the object that
// took the file is here; joined objects are not.
QMutex &registryMutex()
{
    static QMutex m;
    return m;
}
QHash<QString, Qt::HANDLE> &registry()
{
    static QHash<QString, Qt::HANDLE> r;
    return r;
}

} // namespace

QString FolderLock::fileName() { return QStringLiteral(".pinpoint-lock"); }

FolderLock::FolderLock(const QString &folder, int staleMs)
    : m_folder(QDir::cleanPath(folder))
    , m_lock(std::make_unique<QLockFile>(QDir(m_folder).filePath(fileName())))
{
    m_lock->setStaleLockTime(staleMs);
}

FolderLock::~FolderLock() { unlock(); }

QString FolderLock::lockPath() const { return QDir(m_folder).filePath(fileName()); }

bool FolderLock::isHeld() const { return m_joined || m_lock->isLocked(); }

// The folder as one key however it was spelled: its real path when it is there (symlinks and `//` resolved).
QString FolderLock::registryKey() const
{
    const QFileInfo fi(m_folder);
    const QString   canonical = fi.canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(fi.absoluteFilePath()) : canonical;
}

void FolderLock::unlock()
{
    if (m_joined) {   // the hold belongs to the object it joined
        m_joined = false;
        return;
    }
    if (m_lock->isLocked()) {
        {
            // By the key taken at acquisition (REVIEW-1 of 2.8b): recomputed now, a folder that vanished during
            // the hold (a dropped share) resolves to another spelling (/mnt is a symlink on the Mac), the entry
            // would stay, and this thread's next lockOrJoin would join a hold that no longer exists.
            const QMutexLocker guard(&registryMutex());
            const auto it = registry().constFind(m_key);
            if (it != registry().constEnd() && it.value() == QThread::currentThreadId())
                registry().remove(m_key);
        }
        m_key.clear();
        m_lock->unlock();
    }
}

FolderLock::Result FolderLock::lockOrJoin(int waitMs)
{
    m_holder.clear();
    m_error.clear();
    if (isHeld())
        return Result::Acquired;
    {
        const QMutexLocker guard(&registryMutex());
        const auto it = registry().constFind(registryKey());
        if (it != registry().constEnd() && it.value() == QThread::currentThreadId()) {
            m_joined = true;
            return Result::Acquired;
        }
    }
    return lock(waitMs);
}

FolderLock::Result FolderLock::lock(int waitMs)
{
    m_holder.clear();
    m_error.clear();
    if (isHeld())
        return Result::Acquired;

    // Never make the folder: a lock must not bring a trashed folder back.
    if (m_folder.isEmpty() || !QFileInfo(m_folder).isDir()) {
        m_error = QStringLiteral("Could not lock %1: the folder is not there.").arg(m_folder);
        return Result::Failed;
    }

    // QLockFile takes a lock older than the stale time over (on this machine, also one whose process
    // has died) and otherwise retries until the wait runs out.
    if (m_lock->tryLock(waitMs)) {
        m_key = registryKey();
        const QMutexLocker guard(&registryMutex());
        registry().insert(m_key, QThread::currentThreadId());
        return Result::Acquired;
    }

    switch (m_lock->error()) {
    case QLockFile::LockFailedError: {
        qint64 pid = 0;
        QString host, app;
        if (m_lock->getLockInfo(&pid, &host, &app))
            m_holder = QStringLiteral("pid %1, %2 on %3").arg(pid).arg(app.isEmpty() ? QStringLiteral("?") : app,
                                                                 host.isEmpty() ? QStringLiteral("?") : host);
        m_error = m_holder.isEmpty()
                      ? QStringLiteral("%1 is in use by another PinPoint writer.").arg(m_folder)
                      : QStringLiteral("%1 is in use by another PinPoint writer (%2).").arg(m_folder, m_holder);
        return Result::Locked;
    }
    case QLockFile::PermissionError:
        m_error = QStringLiteral("Could not lock %1: the folder is not writable.").arg(m_folder);
        return Result::Failed;
    case QLockFile::NoError:
    case QLockFile::UnknownError:
    default:
        m_error = QStringLiteral("Could not lock %1.").arg(m_folder);
        return Result::Failed;
    }
}

} // namespace pinpoint
