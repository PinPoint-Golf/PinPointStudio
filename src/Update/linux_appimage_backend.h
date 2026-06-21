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

#include <QString>

class AppSettings;
class SessionController;
class AppImageUpdater;
class QNetworkAccessManager;
class QNetworkReply;
template <typename T> class QFutureWatcher;

// The Linux in-app update engine: the only backend that drives the rich PinPoint
// state machine (the macOS/Windows backends are thin façades over Sparkle/WinSparkle,
// which own their own UI). Absorbs the full flow that used to live inline in
// UpdateController. Authoritative design: docs/design/linux_update.md.
//
// Flow (the controller relays every transition to QML):
//   Idle ─checkNow─▶ Checking ─┬ no update ─▶ UpToDate
//                              ├ offered ───▶ UpdateAvailable ─download─▶ Downloading
//                              └ error ─────▶ Error
//   Downloading ─done─▶ Verifying ─┬ sig ok ─▶ ReadyToRelaunch ─relaunch─▶ (replaced)
//                                  └ sig bad ▶ Error (download discarded)
//
// Appcast = GitHub Releases JSON; transport = appimageupdatetool zsync delta
// (AppImageUpdater); authenticity = pinned GPG signature over the detached
// *.AppImage.sig (verifySignatureBlocking, off the GUI thread). Arch selection is
// data (PlatformTarget), so an aarch64 AppImage is offered by the same code once
// published. The relaunch session-safety guard lives in the controller; relaunch()
// here is reached only after it passes.
class LinuxAppImageBackend : public UpdateBackend
{
    Q_OBJECT
public:
    explicit LinuxAppImageBackend(AppSettings *settings = nullptr,
                                  SessionController *session = nullptr,
                                  QObject *parent = nullptr);
    ~LinuxAppImageBackend() override;

    pp::update::State initialState() const override { return m_state; }
    bool supported()       const override { return m_supported; }
    bool ownsStateMachine() const override { return true; }

    void checkNow()            override;
    void download()            override;
    void relaunch()            override;
    void installOnNextLaunch() override;
    void shutdown()            override;
    // Linux has no dynamic re-arm: the passive launch check is one-shot at startup
    // (matching the pre-refactor behaviour), so this is intentionally a no-op.

private:
    using State = pp::update::State;

    void setState(State s);
    void fail(const QString &error);

    void onFeedReply(QNetworkReply *reply);
    void startVerify(const QString &assembledPath);
    // Worker-thread (QtConcurrent): ephemeral-keyring gpg --verify of the assembled
    // AppImage against the downloaded detached sig, asserting the signing fingerprint
    // equals the pinned constant. Returns true iff trusted. Plain QString args (no
    // captured this) so it is thread-safe.
    static bool verifySignatureBlocking(const QString &appImagePath,
                                        const QString &detachedSigPath);

    AppSettings           *m_settings = nullptr;   // checkForUpdates() launch gate
    QNetworkAccessManager *m_net = nullptr;
    AppImageUpdater       *m_updater = nullptr;
    QFutureWatcher<bool>  *m_verifyWatcher = nullptr;

    State   m_state = State::Idle;
    bool    m_supported = false;
    QString m_latestVersion;       // offered version (for logging on stage)
    QString m_appImageAssetName;   // expected *-<arch>.AppImage on the release
    QString m_sigAssetUrl;         // detached *.AppImage.sig download URL
    QString m_assembledPath;       // verified-pending working copy
    QString m_sigLocalPath;        // downloaded sig file
    QString m_errorString;
};
