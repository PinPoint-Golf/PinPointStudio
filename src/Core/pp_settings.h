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

#include <QSettings>

// Returns a QSettings instance pointing to the single canonical PinPoint
// settings file.  All modules must use this instead of QSettings() so that
// every key lands in the same file regardless of whether QCoreApplication
// organisation/name have been set.
inline QSettings ppSettings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope,
                     QStringLiteral("Pinpoint"), QStringLiteral("Pinpoint"));
}
