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

// ONE FOLDER, HOWEVER IT IS SPELLED (refactor 6.1a, fix round). Two spellings of a folder name the same folder when
// they agree once cleaned (QDir::cleanPath: a trailing separator, "//", "/./" and "x/.." go; on Windows a backslash is
// a separator) and, on Windows, whatever their case (its file names and drive letters ignore case). Pure path text:
// nothing is read from the disk, so a folder that is not there compares like any other.
//
// The edge that finds a swing's Session from its folder (Athlete::sessionAt, SessionStore::openSwingFolder) asks
// this, so a late launch monitor reading spelled another way (C:\ against c:/, a trailing slash) still reaches its
// Shot rather than being refused.

#include <QDir>
#include <QString>

namespace pinpoint {

#ifdef Q_OS_WIN
inline constexpr Qt::CaseSensitivity kFolderNameCase = Qt::CaseInsensitive;
#else
inline constexpr Qt::CaseSensitivity kFolderNameCase = Qt::CaseSensitive;
#endif

// `cs` is the platform's rule; a test passes the other one to check Windows' rule on any machine.
inline bool sameFolder(const QString &a, const QString &b, Qt::CaseSensitivity cs = kFolderNameCase)
{
    return QDir::cleanPath(a).compare(QDir::cleanPath(b), cs) == 0;
}

} // namespace pinpoint
