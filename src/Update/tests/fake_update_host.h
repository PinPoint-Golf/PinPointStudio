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

#include "../update_host.h"

#include <QString>

// A test double for UpdateHost: the session question and the two settings as plain
// fields, with setters that emit the change notices the way the app's host does
// (only on a real change). No AppSettings, no SessionController, no settings file.
class FakeUpdateHost : public UpdateHost
{
    Q_OBJECT
public:
    explicit FakeUpdateHost(QObject *parent = nullptr) : UpdateHost(parent) {}

    bool    running = false;
    bool    checks  = true;
    QString skipped;
    mutable int runningAsked = 0;   // how often the question was asked
    int     skipWrites = 0;

    bool sessionRunning() const override { ++runningAsked; return running; }
    bool checkForUpdates() const override { return checks; }
    void setSkippedUpdateVersion(const QString &v) override { ++skipWrites; skipped = v; }

    void setRunning(bool r)
    {
        if (running == r) return;
        running = r;
        emit sessionRunningChanged();
    }
    void setChecks(bool c)
    {
        if (checks == c) return;
        checks = c;
        emit checkForUpdatesChanged();
    }
};
