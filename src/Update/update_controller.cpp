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

#include "update_controller.h"

#include "update_backend.h"
#include "update_backend_factory.h"
#include "app_settings.h"         // checkForUpdates() / skippedUpdateVersion persistence
#include "session_controller.h"   // running() for the relaunch session-safety guard
#include "version.h"              // PINPOINT_VERSION_STRING (src/Core)

UpdateController::UpdateController(AppSettings *settings, SessionController *session,
                                  QObject *parent)
    : UpdateController(makeUpdateBackend(settings, session, nullptr),
                       settings, session, parent)
{
}

UpdateController::UpdateController(UpdateBackend *backend, AppSettings *settings,
                                  SessionController *session, QObject *parent)
    : QObject(parent), m_settings(settings), m_session(session), m_backend(backend)
{
    wireBackend();
}

void UpdateController::wireBackend()
{
    if (!m_backend)
        return;
    m_backend->setParent(this);   // lifetime tied to the controller

    // Seed the QML-facing mirror from the backend's initial capability report —
    // this replaces the old constructor's per-platform #ifdef ladder.
    m_state     = m_backend->initialState();
    m_supported = m_backend->supported();

    connect(m_backend, &UpdateBackend::stateChanged,  this, &UpdateController::onBackendState);
    connect(m_backend, &UpdateBackend::updateOffered, this, &UpdateController::onBackendOffer);
    connect(m_backend, &UpdateBackend::progress,      this, &UpdateController::setProgress);
    connect(m_backend, &UpdateBackend::status,        this, &UpdateController::setStatus);

    // One unconditional wire (was duplicated per platform branch): keep the engine's
    // automatic-check pref in sync with the existing toggle.
    if (m_settings)
        connect(m_settings, &AppSettings::checkForUpdatesChanged, this, [this] {
            if (m_backend)
                m_backend->setAutomaticChecks(m_settings->checkForUpdates());
        });
}

UpdateController::~UpdateController() = default;

QString UpdateController::state() const
{
    switch (m_state) {
    case State::Unsupported:     return QStringLiteral("unsupported");
    case State::DevBuild:        return QStringLiteral("devbuild");
    case State::Idle:            return QStringLiteral("idle");
    case State::Checking:        return QStringLiteral("checking");
    case State::UpToDate:        return QStringLiteral("uptodate");
    case State::UpdateAvailable: return QStringLiteral("available");
    case State::Downloading:     return QStringLiteral("downloading");
    case State::Verifying:       return QStringLiteral("verifying");
    case State::ReadyToRelaunch: return QStringLiteral("ready");
    case State::Error:           return QStringLiteral("error");
    }
    return QStringLiteral("idle");
}

QString UpdateController::currentVersion() const
{
    return QStringLiteral(PINPOINT_VERSION_STRING);
}

void UpdateController::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged();
}

void UpdateController::setStatus(const QString &msg)
{
    if (m_statusMessage == msg)
        return;
    m_statusMessage = msg;
    emit statusMessageChanged();
}

void UpdateController::setProgress(double p)
{
    if (qFuzzyCompare(m_progress, p))
        return;
    m_progress = p;
    emit progressChanged();
}

// ── backend relay ──────────────────────────────────────────────────────────────

void UpdateController::onBackendState(pp::update::State s, const QString &error)
{
    // errorString is meaningful only in the Error state; carrying the "" of every
    // other transition simply clears any stale message (matches the old behaviour
    // of clearing on checkNow/download).
    m_errorString = error;
    setState(s);
}

void UpdateController::onBackendOffer(const pp::update::OfferInfo &info)
{
    // Reproduce the exact pre-refactor emission order so the launch banner repaints
    // once: latestVersionChanged → stateChanged(available) → updateOffered(version).
    m_latestVersion = info.version;
    m_releaseNotes  = info.releaseNotes;
    emit latestVersionChanged();
    setState(State::UpdateAvailable);
    emit updateOffered(info.version);
}

// ── actions (delegated to the backend) ──────────────────────────────────────────

void UpdateController::checkNow()
{
    if (m_supported && m_backend)
        m_backend->checkNow();
}

void UpdateController::download()
{
    if (m_backend)
        m_backend->download();   // the backend guards on its own state
}

void UpdateController::relaunch()
{
    if (m_state != State::ReadyToRelaunch)
        return;
    // Never relaunch out from under a live session (design §5.2). The QML button is
    // disabled in that case; this is the hard guard — the single piece of session-
    // safety POLICY that stays in the controller, above any platform backend.
    if (m_session && m_session->running()) {
        setStatus(tr("End the session to install the update."));
        return;
    }
    if (m_backend)
        m_backend->relaunch();
}

void UpdateController::installOnNextLaunch()
{
    if (m_backend)
        m_backend->installOnNextLaunch();   // the backend guards on its own state
}

void UpdateController::skipVersion()
{
    if (m_settings && !m_latestVersion.isEmpty())
        m_settings->setSkippedUpdateVersion(m_latestVersion);
}

void UpdateController::shutdownUpdater()
{
    if (m_backend)
        m_backend->shutdown();
}
