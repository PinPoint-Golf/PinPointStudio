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

#include "mac_sparkle_backend.h"

#include "mac_sparkle_update.h"

MacSparkleBackend::MacSparkleBackend(AppSettings *settings, SessionController *session,
                                     QObject *parent)
    : UpdateBackend(parent)
{
    // Mirrors the old UpdateController macOS branch: supported when installed AND a
    // real pinned EdDSA key is configured; a build-tree / App-Translocated run is
    // inert (DevBuild) — the macOS analogue of the Linux $APPIMAGE-unset and the
    // Windows unins000.exe checks.
    if (MacSparkleUpdater::isInstalledBuild()) {
        m_engine    = new MacSparkleUpdater(this);
        m_supported = m_engine->configureAndInit(settings, session);
        m_state     = m_supported ? pp::update::State::Idle
                                  : pp::update::State::Unsupported;
    } else {
        m_state     = pp::update::State::DevBuild;
        m_supported = false;
    }
}

MacSparkleBackend::~MacSparkleBackend() = default;

void MacSparkleBackend::checkNow()
{
    // Hand off to Sparkle's own check + UI (design §5B); no PinPoint state machine.
    if (m_engine)
        m_engine->checkNow();
}

void MacSparkleBackend::shutdown()
{
    if (m_engine)
        m_engine->shutdown();
}

void MacSparkleBackend::setAutomaticChecks(bool enabled)
{
    if (m_engine)
        m_engine->setAutomaticChecks(enabled);
}
