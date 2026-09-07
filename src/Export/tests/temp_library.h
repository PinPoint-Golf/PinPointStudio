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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#ifndef Q_OS_WIN
#  include <unistd.h>
#endif

// Scratch athlete-library tree for the capture/session suites.
//
// The session and swing folder logic is filesystem logic: what it does depends
// on what is already on disk, whether the volume will accept a write, and what
// other folders happen to sit alongside. None of that can be reached by calling
// the functions with different arguments, so the tests build the tree instead.
//
// Layout mirrors SwingPaths:
//   <root>/<athlete>/<session>_NN/swing_0001/
namespace pinpoint::test {

class TempLibrary {
public:
    TempLibrary() { m_dir.setAutoRemove(true); }

    bool valid() const { return m_dir.isValid(); }
    QString root() const { return m_dir.path(); }

    QString athleteDir(const QString &athleteToken) const
    {
        return root() + QLatin1Char('/') + athleteToken;
    }

    // Create a session folder under an athlete, with `swings` swing_NNNN
    // subfolders already in it — a session from an earlier run of the app.
    QString makeSession(const QString &athleteToken, const QString &sessionId, int swings = 0) const
    {
        const QString dir = athleteDir(athleteToken) + QLatin1Char('/') + sessionId;
        if (!QDir().mkpath(dir))
            return {};
        for (int i = 1; i <= swings; ++i)
            if (!makeSwing(dir, i))
                return {};
        return dir;
    }

    // One swing folder, with a token file in it so a "recoverable" delete can be
    // told apart from a folder that was never there.
    static bool makeSwing(const QString &sessionDir, int index)
    {
        const QString dir = sessionDir
                          + QStringLiteral("/swing_%1").arg(index, 4, 10, QLatin1Char('0'));
        if (!QDir().mkpath(dir))
            return false;
        QFile f(dir + QStringLiteral("/swing.json"));
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write("{}");
        return true;
    }

    static int countSwings(const QString &sessionDir)
    {
        return QDir(sessionDir)
            .entryList({ QStringLiteral("swing_*") }, QDir::Dirs | QDir::NoDotAndDotDot)
            .size();
    }

    static int countSessions(const QString &athleteDir)
    {
        return QDir(athleteDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size();
    }

    static bool exists(const QString &path) { return QFileInfo::exists(path); }

    // Names of the session folders under an athlete, sorted.
    static QStringList sessionNames(const QString &athleteDir)
    {
        return QDir(athleteDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    }

private:
    QTemporaryDir m_dir;
};

// A directory the process cannot write into — the unmounted-share case. Returns
// false where the platform will not honour it (Windows ACLs, or running as
// root), so a test can skip rather than report a false pass.
inline bool makeUnwritable(const QString &path)
{
#ifdef Q_OS_WIN
    Q_UNUSED(path);
    return false;
#else
    if (::geteuid() == 0)
        return false;   // root writes anywhere; the case is unreachable here
    return QFile::setPermissions(path, QFile::ReadOwner | QFile::ExeOwner);
#endif
}

inline void makeWritable(const QString &path)
{
#ifndef Q_OS_WIN
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
#else
    Q_UNUSED(path);
#endif
}

} // namespace pinpoint::test
