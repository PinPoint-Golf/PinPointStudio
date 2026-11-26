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

#include "update_host.h"

class AppSettings;
class SessionController;

// The app's answer to the updater's questions (UpdateHost, src/Update/update_host.h):
// the session question from SessionController::running(), the settings from the
// app's one AppSettings, the same object QML reads, so the updater and the Settings
// page never disagree. Built by the composition root before the UpdateController
// and destroyed after it; both objects it wraps outlive it. Either may be null: no
// session then never blocks, and no settings means automatic checks off and
// nothing persisted, as before.
class AppUpdateHost final : public UpdateHost
{
    Q_OBJECT
public:
    AppUpdateHost(AppSettings *settings, SessionController *session,
                  QObject *parent = nullptr);
    ~AppUpdateHost() override;

    bool sessionRunning() const override;
    bool checkForUpdates() const override;
    void setSkippedUpdateVersion(const QString &version) override;

private:
    AppSettings       *m_settings = nullptr;
    SessionController *m_session  = nullptr;
};
