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

#include "update_backend.h"   // pp::update::State / OfferInfo (slot signatures + alias)

class AppSettings;
class SessionController;

// The QML-facing in-app updater façade — registered in main.cpp as the
// `updateController` context property on ALL platforms so the shared QML binds
// uniformly. It owns the platform state, session-safety policy, and one polymorphic
// UpdateBackend (chosen by makeUpdateBackend()): the rich Linux AppImage state
// machine, the macOS Sparkle / Windows WinSparkle façades, or an inert fallback.
//
// This class is deliberately thin: it keeps the QML property surface + the
// State→string mapping (the single source of truth for the strings the update
// banner switches on), relays the backend's transitions, enforces the relaunch
// session-safety guard, and persists "skip this version". Everything platform-
// specific lives behind UpdateBackend. Authoritative design: the three
// docs/design/{linux,windows,macos}_update.md documents.
class UpdateController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state         READ state         NOTIFY stateChanged)
    Q_PROPERTY(bool    available     READ available     NOTIFY stateChanged)
    Q_PROPERTY(bool    supported     READ supported     CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY latestVersionChanged)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString releaseNotes  READ releaseNotes  NOTIFY latestVersionChanged)
    Q_PROPERTY(double  progress      READ progress      NOTIFY progressChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString errorString   READ errorString   NOTIFY stateChanged)

public:
    // Re-export the shared state enum so existing call sites keep using State::...
    using State = pp::update::State;

    // Production constructor: builds the platform backend via makeUpdateBackend().
    explicit UpdateController(AppSettings *settings = nullptr,
                              SessionController *session = nullptr,
                              QObject *parent = nullptr);
    // Dependency-injection constructor (tests / explicit backend). Takes ownership
    // of `backend` (reparents it to this).
    UpdateController(UpdateBackend *backend, AppSettings *settings,
                     SessionController *session, QObject *parent = nullptr);
    ~UpdateController() override;

    QString state() const;
    bool    available() const     { return m_state == State::UpdateAvailable; }
    bool    supported() const     { return m_supported; }
    QString latestVersion() const { return m_latestVersion; }
    QString currentVersion() const;
    QString releaseNotes() const  { return m_releaseNotes; }
    double  progress() const      { return m_progress; }
    QString statusMessage() const { return m_statusMessage; }
    QString errorString() const   { return m_errorString; }

    // Query the feed / native engine for a newer version.
    Q_INVOKABLE void checkNow();
    // Begin the download of the offered version (Linux state machine only).
    Q_INVOKABLE void download();
    // Relaunch into the verified new build now (ReadyToRelaunch + no live session).
    Q_INVOKABLE void relaunch();
    // Dismiss without relaunching — the verified swap already happened, so the next
    // launch runs the new build (Linux state machine only).
    Q_INVOKABLE void installOnNextLaunch();
    // Persist the offered version as "skipped" so the launch banner stops showing it
    // (a newer version overrides the skip). Settings always still shows the offer.
    Q_INVOKABLE void skipVersion();

    // Shut the platform updater down cleanly on app exit (win_sparkle_cleanup on
    // Windows; Sparkle release on macOS; cancel an in-flight Linux download). Call
    // from aboutToQuit, before Qt teardown.
    void shutdownUpdater();

signals:
    void stateChanged();
    void latestVersionChanged();
    void progressChanged();
    void statusMessageChanged();
    // Emitted when a newer version becomes available — drives the launch banner (P3).
    void updateOffered(const QString &version);

private:
    void wireBackend();
    void setState(State s);
    void setStatus(const QString &msg);
    void setProgress(double p);

    // Relay slots for the backend's signals.
    void onBackendState(pp::update::State s, const QString &error);
    void onBackendOffer(const pp::update::OfferInfo &info);

    AppSettings       *m_settings = nullptr;
    SessionController *m_session  = nullptr;
    UpdateBackend     *m_backend  = nullptr;

    State   m_state = State::Idle;
    bool    m_supported = false;
    QString m_latestVersion;
    QString m_releaseNotes;
    double  m_progress = 0.0;
    QString m_statusMessage;
    QString m_errorString;
};
