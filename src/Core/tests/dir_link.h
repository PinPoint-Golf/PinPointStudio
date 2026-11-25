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

// pinpoint::test::linkDirectory: a REAL link to a folder, the kind the platform makes for one (#243).
//
//   - macOS and Linux: a symbolic link (QFile::link).
//   - Windows: a directory junction (`mklink /J`). QFile::link makes a .lnk shortcut there, a plain file the file
//     system never follows, so a test built on it tests nothing. A junction is what a user (or a mapped share's
//     tooling) can make without developer mode or admin rights; std::filesystem::create_directory_symlink needs one
//     of those. Both are reparse points that the file system follows on open.
//
// unlinkDirectory removes the link itself, never what it points at. Remove each junction before its temporary
// folder goes, so no recursive delete has to decide whether to walk through it.
// Test-only code (header-only, no library). Include it by relative path, e.g. from src/<Dir>/tests/:
//   #include "../../Core/tests/dir_link.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#ifdef Q_OS_WIN
#include <QProcess>
#endif

namespace pinpoint::test {

// Make `link` a link to the existing folder `target`. True when the link is there and reaches the folder.
inline bool linkDirectory(const QString &target, const QString &link)
{
#ifdef Q_OS_WIN
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);   // mklink's "Junction created" line stays out of the log
    p.start(QStringLiteral("cmd.exe"),
            { QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
              QDir::toNativeSeparators(QFileInfo(link).absoluteFilePath()),
              QDir::toNativeSeparators(QFileInfo(target).absoluteFilePath()) });
    if (!p.waitForFinished(30000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) return false;
#else
    if (!QFile::link(target, link)) return false;
#endif
    return QFileInfo(link).isDir();
}

// Remove the link `link` made by linkDirectory, leaving its target as it is.
inline bool unlinkDirectory(const QString &link)
{
#ifdef Q_OS_WIN
    return QDir().rmdir(link);   // a junction is an empty folder with a reparse point: RemoveDirectory takes the link only
#else
    return QFile::remove(link);
#endif
}

} // namespace pinpoint::test
