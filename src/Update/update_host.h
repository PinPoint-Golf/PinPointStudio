/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once

#include <QObject>
#include <QString>

// What the updater asks of the app that builds it, handed in rather than reached
// for: the updater lives outside src/Gui and never includes a Gui header (lint A1).
//
//  - The question: is a session running? An update is never installed while one
//    runs: the controller refuses a relaunch ("End the session to install the
//    update."), Sparkle postpones its relaunch until the session ends, and
//    WinSparkle's can-shutdown callback says no. sessionRunningChanged() is how the
//    macOS engine fires a postponed relaunch and the Windows engine refreshes the
//    mirror its off-thread callback reads.
//  - The settings: "Check for updates automatically", read at start-up and followed
//    through checkForUpdatesChanged(); and the version the golfer chose to skip,
//    which the updater only writes (the launch banner reads it).
//
// The app's answer is AppUpdateHost (src/Gui/app/app_update_host.h), over the app's
// one AppSettings and SessionController. The tests hand in a fake. Every caller
// treats a null host as "no session running, automatic checks off", as a null
// settings or session pointer was treated before.
class UpdateHost : public QObject
{
    Q_OBJECT
public:
    explicit UpdateHost(QObject *parent = nullptr) : QObject(parent) {}
    ~UpdateHost() override = default;

    // True while a session runs. Asked on the GUI thread only.
    virtual bool sessionRunning() const = 0;
    // "Check for updates automatically".
    virtual bool checkForUpdates() const = 0;
    // Persist the version the golfer skipped ("skip this version").
    virtual void setSkippedUpdateVersion(const QString &version) = 0;

signals:
    void sessionRunningChanged();
    void checkForUpdatesChanged();
};
