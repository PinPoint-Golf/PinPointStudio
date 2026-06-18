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

class AppSettings;
class SessionController;
class AppImageUpdater;
class WinSparkleUpdater;
class MacSparkleUpdater;
class QNetworkAccessManager;
class QNetworkReply;
template <typename T> class QFutureWatcher;

// In-app updater for the Linux AppImage build — the native analogue of Sparkle
// (macOS) / WinSparkle (Windows). Prompt-then-install: never downloads without a
// click, never relaunches without a click, never replaces the live binary with
// unverified bytes. Authoritative design: docs/design/linux_update.md.
//
// Flow (state machine, design §5.1):
//   Idle ─checkNow─▶ Checking ─┬ no update ─▶ UpToDate
//                              ├ offered ───▶ UpdateAvailable ─download─▶ Downloading
//                              └ error ─────▶ Error
//   Downloading ─done─▶ Verifying ─┬ sig ok ─▶ ReadyToRelaunch ─relaunch─▶ (process replaced)
//                                  └ sig bad ▶ Error (download discarded)
//
// Appcast = GitHub Releases JSON (version + notes); transport = appimageupdatetool
// zsync delta (AppImageUpdater); authenticity = pinned GPG signature over the
// detached *.AppImage.sig asset (verifySignatureBlocking()).
//
// Registered in main.cpp as the QML context property `updateController` on ALL
// platforms (so the shared QML binds uniformly); functional only on a Linux
// AppImage — elsewhere `state` is "unsupported"/"devbuild" and actions are no-ops.
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
    explicit UpdateController(AppSettings *settings = nullptr,
                              SessionController *session = nullptr,
                              QObject *parent = nullptr);
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

    // Query the GitHub Releases feed and decide whether a newer version is offered.
    Q_INVOKABLE void checkNow();
    // Begin the zsync delta download of the offered version (UpdateAvailable only).
    Q_INVOKABLE void download();
    // Relaunch into the verified new build now (ReadyToRelaunch + no live session).
    Q_INVOKABLE void relaunch();
    // Dismiss without relaunching — the verified swap has already happened, so the
    // next launch runs the new build (ReadyToRelaunch only).
    Q_INVOKABLE void installOnNextLaunch();
    // Persist the offered version as "skipped" so the launch banner stops showing it
    // (a newer version overrides the skip). Settings always still shows the offer.
    Q_INVOKABLE void skipVersion();

    // Shut the platform updater down cleanly on app exit (win_sparkle_cleanup on
    // Windows; no-op elsewhere). Call from aboutToQuit, before Qt teardown.
    void shutdownUpdater();

signals:
    void stateChanged();
    void latestVersionChanged();
    void progressChanged();
    void statusMessageChanged();
    // Emitted when a newer version becomes available — drives the launch banner (P3).
    void updateOffered(const QString &version);

private:
    enum class State {
        Unsupported,      // non-Linux (native updater) or compiled-out
        DevBuild,         // Linux, not launched as an AppImage
        Idle,
        Checking,
        UpToDate,
        UpdateAvailable,
        Downloading,
        Verifying,
        ReadyToRelaunch,
        Error
    };

    void setState(State s);
    void setStatus(const QString &msg);
    void setProgress(double p);
    void fail(const QString &error);

    void onFeedReply(QNetworkReply *reply);
    void startVerify(const QString &assembledPath);
    // Runs on a worker thread (QtConcurrent): ephemeral-keyring gpg --verify of the
    // assembled AppImage against the downloaded detached sig, asserting the signing
    // fingerprint equals the pinned constant. Returns true iff trusted.
    static bool verifySignatureBlocking(const QString &appImagePath,
                                        const QString &detachedSigPath);

    AppSettings       *m_settings = nullptr;
    SessionController *m_session  = nullptr;
    QNetworkAccessManager *m_net = nullptr;
    AppImageUpdater       *m_updater = nullptr;
    QFutureWatcher<bool>  *m_verifyWatcher = nullptr;
    WinSparkleUpdater     *m_winSparkle = nullptr;   // Windows engine (façade), else null
    MacSparkleUpdater     *m_macSparkle = nullptr;   // macOS engine (façade), else null

    State   m_state = State::Idle;
    bool    m_supported = false;
    QString m_latestVersion;
    QString m_releaseNotes;
    QString m_appImageAssetName;   // expected *-x86_64.AppImage on the release
    QString m_sigAssetUrl;         // detached *.AppImage.sig download URL
    QString m_assembledPath;       // verified-pending working copy
    QString m_sigLocalPath;        // downloaded sig file
    double  m_progress = 0.0;
    QString m_statusMessage;
    QString m_errorString;
};
