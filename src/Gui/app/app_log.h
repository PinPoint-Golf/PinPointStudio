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

#include <QObject>
#include <QString>

// QML → the app log. QML's console.* goes through the Qt message handler, which is NOT where
// ppInfo()/ppWarn() write (they go straight to PpMessageLog, pp_log_stream.cpp), so a line logged
// from QML with console.log never reached the one log the app keeps. This is the bridge, exposed
// as the "appLog" context property (main.cpp). Each call writes one line, "[<tag>] <text>".
//
// The test harnesses do not link this class: they inject a fake under the same context-property
// name, so its interface is the contract — keep both methods public and void.
class AppLog : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    Q_INVOKABLE void info(const QString &tag, const QString &text);
    Q_INVOKABLE void warn(const QString &tag, const QString &text);
};
