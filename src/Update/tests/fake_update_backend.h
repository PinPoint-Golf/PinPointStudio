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

#include "../update_backend.h"

#include <QString>

// A test double for UpdateBackend: it records the actions the controller delegates
// to it and lets a test drive the controller through every state transition by
// emitting the backend signals on command — with NO network, subprocess, or
// platform engine. This is what makes UpdateController's state-machine + session-
// safety policy unit-testable; it is the whole point of the polymorphic seam.
class FakeUpdateBackend : public UpdateBackend
{
    Q_OBJECT
public:
    explicit FakeUpdateBackend(QObject *parent = nullptr) : UpdateBackend(parent) {}

    // --- Capability the controller seeds its mirror from (set before wiring) -----
    pp::update::State capInitial = pp::update::State::Idle;
    bool capSupported = true;
    bool capOwns      = true;

    pp::update::State initialState() const override { return capInitial; }
    bool supported()       const override { return capSupported; }
    bool ownsStateMachine() const override { return capOwns; }

    // --- Recorded actions --------------------------------------------------------
    int  checkNowCount = 0;
    int  downloadCount = 0;
    int  relaunchCount = 0;
    int  installCount  = 0;
    int  shutdownCount = 0;
    int  autoCount     = 0;
    bool lastAuto      = true;

    void checkNow()            override { ++checkNowCount; }
    void download()            override { ++downloadCount; }
    void relaunch()            override { ++relaunchCount; }
    void installOnNextLaunch() override { ++installCount; }
    void shutdown()            override { ++shutdownCount; }
    void setAutomaticChecks(bool e) override { ++autoCount; lastAuto = e; }

    // --- Drivers (emit the backend signals the controller relays) ----------------
    void driveState(pp::update::State s, const QString &err = QString())
    {
        emit stateChanged(s, err);
    }
    void driveOffer(const QString &version, const QString &notes)
    {
        emit updateOffered(pp::update::OfferInfo{version, notes});
    }
    void driveProgress(double p) { emit progress(p); }
    void driveStatus(const QString &m) { emit status(m); }
};
