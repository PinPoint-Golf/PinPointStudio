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

#include "app_settings.h"

#include <QDirIterator>
#include <QStorageInfo>

StorageInfo AppSettings::queryStorageInfo() const
{
    StorageInfo info;
    const QString path = athleteLibraryPath();
    if (path.isEmpty()) return info;

    QStorageInfo si(path);
    if (si.isValid()) {
        info.totalBytes = si.bytesTotal();
        info.freeBytes  = si.bytesAvailable();
        info.volumeName = si.displayName();
    }

    QDirIterator it(path, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    qint64 total = 0;
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    info.sessionBytes = total;

    return info;
}
