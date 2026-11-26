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

#include "app_update_host.h"

#include "app_settings.h"         // checkForUpdates() / skippedUpdateVersion
#include "session_controller.h"   // running() / runningChanged

AppUpdateHost::AppUpdateHost(AppSettings *settings, SessionController *session,
                             QObject *parent)
    : UpdateHost(parent), m_settings(settings), m_session(session)
{
    if (m_session)
        connect(m_session, &SessionController::runningChanged,
                this, &UpdateHost::sessionRunningChanged);
    if (m_settings)
        connect(m_settings, &AppSettings::checkForUpdatesChanged,
                this, &UpdateHost::checkForUpdatesChanged);
}

AppUpdateHost::~AppUpdateHost() = default;

bool AppUpdateHost::sessionRunning() const
{
    return m_session && m_session->running();
}

bool AppUpdateHost::checkForUpdates() const
{
    return m_settings && m_settings->checkForUpdates();
}

void AppUpdateHost::setSkippedUpdateVersion(const QString &version)
{
    if (m_settings)
        m_settings->setSkippedUpdateVersion(version);
}
