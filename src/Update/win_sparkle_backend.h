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

#include "update_backend.h"

class AppSettings;
class SessionController;
class WinSparkleUpdater;

// The Windows in-app update backend: a thin UpdateBackend over WinSparkleUpdater
// (which is itself a shim over the WinSparkle DLL). WinSparkle owns the appcast
// fetch, version compare, download, EdDSA verification, progress UI, and installer
// launch + session-safety callbacks — so this backend does NOT drive a state
// machine (ownsStateMachine() == false) and its state never leaves Idle/DevBuild/
// Unsupported. checkNow() pops WinSparkle's own UI. Compiled only when
// HAVE_WINSPARKLE. Authoritative design: docs/design/windows_update.md.
class WinSparkleBackend : public UpdateBackend
{
    Q_OBJECT
public:
    explicit WinSparkleBackend(AppSettings *settings = nullptr,
                               SessionController *session = nullptr,
                               QObject *parent = nullptr);
    ~WinSparkleBackend() override;

    pp::update::State initialState() const override { return m_state; }
    bool supported()       const override { return m_supported; }
    bool ownsStateMachine() const override { return false; }

    void checkNow()              override;
    void shutdown()              override;
    void setAutomaticChecks(bool enabled) override;

private:
    WinSparkleUpdater *m_engine = nullptr;
    pp::update::State  m_state = pp::update::State::Unsupported;
    bool               m_supported = false;
};
