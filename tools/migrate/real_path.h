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

// pinpoint::migrate::realPath: an EXISTING path with every link resolved, the one resolver the migration tools'
// "is this write inside the library / under scratch?" guards compare with (#243).
//
// On Windows QFileInfo::canonicalFilePath does not follow a directory junction: a junction under scratch to the
// live library resolved to its own spelling, so pps_migrate_step2 took it for scratch and wrote through it
// (migrate_step2_test H0-H3 on the studio PC, 25 Nov 2026). There the file system's own final path is used
// (std::filesystem::canonical, which follows symlinks and junctions alike), its \\?\ prefix taken off. Elsewhere
// QFileInfo::canonicalFilePath, as before.
//
// An empty string when the path is not there (or cannot be resolved): the caller decides, never this.

#include <QDir>
#include <QFileInfo>
#include <QString>

#include <filesystem>
#include <system_error>

namespace pinpoint::migrate {

// The file system's final path for `path` (compiled on every platform, used on Windows).
inline QString finalPathOf(const QString &path)
{
    std::error_code ec;
    const std::filesystem::path in(QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()).toStdU16String());
    const std::filesystem::path out = std::filesystem::canonical(in, ec);
    if (ec) return QString();
    QString s = QDir::fromNativeSeparators(QString::fromStdU16String(out.u16string()));
    if (s.startsWith(QStringLiteral("//?/UNC/"))) s = QStringLiteral("//") + s.mid(8);   // \\?\UNC\server\share
    else if (s.startsWith(QStringLiteral("//?/"))) s = s.mid(4);                       // \\?\C:\…
    return s.isEmpty() ? QString() : QDir::cleanPath(s);
}

inline QString realPath(const QString &path)
{
#ifdef Q_OS_WIN
    return finalPathOf(path);
#else
    return QFileInfo(path).canonicalFilePath();
#endif
}

} // namespace pinpoint::migrate
