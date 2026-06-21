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

#include "linux_appimage_backend.h"

#include "appimage_update.h"
#include "app_settings.h"         // checkForUpdates() launch-check gate
#include "linux_update_logic.h"   // pure version-compare / asset-select / gpg-parse
#include "platform_target.h"      // assetArchToken() — arch-driven asset selection
#include "version.h"              // PINPOINT_VERSION_STRING (src/Core)

#include <cstdio>      // std::rename — atomic same-dir overwrite for the final swap

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include "pp_debug.h"

namespace {

const QString kReleasesUrl = QStringLiteral(
    "https://api.github.com/repos/PinPoint-Golf/PinPointStudio/releases?per_page=10");

// Pinned release-key fingerprint (design §6) — the public half ships as the
// :/keys/pinpoint_release_pubkey.asc resource. The verify gate accepts an update
// only if its detached signature is VALIDSIG by exactly this key.
//   UID: PinPointStudio <liversedge@gmail.com>  (ed25519, sign-only)
const QString kPinnedKeyFpr =
    QStringLiteral("C15A1C82CE718ED190B7C3C00F677C2EDA4F7BF0");

QString currentVersion()
{
    return QStringLiteral(PINPOINT_VERSION_STRING);
}

} // namespace

LinuxAppImageBackend::LinuxAppImageBackend(AppSettings *settings,
                                           SessionController * /*session*/,
                                           QObject *parent)
    : UpdateBackend(parent), m_settings(settings)
{
    m_net = new QNetworkAccessManager(this);

    if (AppImageUpdater::runningAppImagePath().isEmpty()) {
        // Running from a build tree / extracted dir — in-app update does not apply.
        m_state = State::DevBuild;
        m_supported = false;
    } else {
        m_state = State::Idle;
        m_supported = true;
        // Passive launch check (design surface A) — gated on the user pref. Deferred
        // a few seconds so it never competes with startup/device bring-up. The
        // launch banner reacts to the resulting state (suppressed for skipped
        // versions and during a session — handled in QML).
        if (m_settings && m_settings->checkForUpdates())
            QTimer::singleShot(4000, this, &LinuxAppImageBackend::checkNow);
    }
}

LinuxAppImageBackend::~LinuxAppImageBackend() = default;

void LinuxAppImageBackend::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(s, m_errorString);
}

void LinuxAppImageBackend::fail(const QString &error)
{
    m_errorString = error;
    ppWarn() << "Update error:" << error;
    m_state = State::Error;
    emit stateChanged(State::Error, error);
}

// ── check ────────────────────────────────────────────────────────────────────

void LinuxAppImageBackend::checkNow()
{
    if (!m_supported)
        return;
    if (m_state == State::Checking || m_state == State::Downloading
        || m_state == State::Verifying)
        return;

    m_errorString.clear();
    setState(State::Checking);

    QNetworkRequest req{QUrl(kReleasesUrl)};
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", "PinPointStudio-Updater");  // GitHub requires a UA
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onFeedReply(reply); });
}

void LinuxAppImageBackend::onFeedReply(QNetworkReply *reply)
{
    reply->deleteLater();
    if (m_state != State::Checking)
        return;  // superseded
    if (reply->error() != QNetworkReply::NoError) {
        fail(tr("Could not reach the update server: %1").arg(reply->errorString()));
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isArray()) {
        fail(tr("Unexpected update feed format."));
        return;
    }

    // Find the newest non-draft release that publishes a same-arch AppImage. The
    // repo also carries Windows-only releases (e.g. installer .exe's) and possibly
    // other-arch AppImages — those are skipped, not treated as errors. Prereleases
    // are included for now (alpha/beta — design §7); a Stable/Beta filter on
    // `prerelease` lands with GA. The offered version comes from the AppImage asset
    // filename, never the git tag.
    const QString archToken = pp::update::PlatformTarget::current().assetArchToken();
    const auto pick = pp::update::linux_logic::pickLatestAppImage(doc.array(), archToken);

    if (!pick.found) {
        setState(State::UpToDate);   // no same-arch build published (yet)
        return;
    }
    if (pp::update::linux_logic::compareVersion(pick.version, currentVersion()) <= 0) {
        setState(State::UpToDate);
        return;
    }

    m_appImageAssetName = pick.appImageName;
    m_sigAssetUrl       = pick.sigUrl;
    m_latestVersion     = pick.version;

    // UpdateAvailable is delivered via updateOffered() so the controller can set
    // latestVersion BEFORE flipping state (banner single-repaint order). Set our
    // own state directly here — no separate stateChanged for this transition.
    m_state = State::UpdateAvailable;
    emit updateOffered(pp::update::OfferInfo{pick.version, pick.notes});
    ppInfo() << "Update available:" << pick.version;
}

// ── download ───────────────────────────────────────────────────────────────────

void LinuxAppImageBackend::download()
{
    if (m_state != State::UpdateAvailable && m_state != State::Error)
        return;
    if (!AppImageUpdater::available()) {
        fail(tr("The update helper is unavailable in this build."));
        return;
    }

    m_errorString.clear();
    emit progress(0.0);
    setState(State::Downloading);

    m_updater = new AppImageUpdater(this);
    connect(m_updater, &AppImageUpdater::progress, this, &LinuxAppImageBackend::progress);
    connect(m_updater, &AppImageUpdater::status,   this, &LinuxAppImageBackend::status);
    connect(m_updater, &AppImageUpdater::finished, this,
            [this](bool ok, const QString &path, const QString &err) {
                if (m_updater) { m_updater->deleteLater(); m_updater = nullptr; }
                if (!ok) { fail(err); return; }
                startVerify(path);
            });
    m_updater->start();
}

// ── verify ───────────────────────────────────────────────────────────────────

void LinuxAppImageBackend::startVerify(const QString &assembledPath)
{
    m_assembledPath = assembledPath;
    setState(State::Verifying);
    emit status(tr("Verifying signature…"));

    if (m_sigAssetUrl.isEmpty()) {
        QFile::remove(m_assembledPath);
        fail(tr("Release is not signed — refusing to install."));
        return;
    }

    QNetworkRequest req{QUrl(m_sigAssetUrl)};
    req.setRawHeader("User-Agent", "PinPointStudio-Updater");
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            QFile::remove(m_assembledPath);
            fail(tr("Could not download the update signature."));
            return;
        }
        m_sigLocalPath = m_assembledPath + QStringLiteral(".sig");
        QFile sf(m_sigLocalPath);
        if (!sf.open(QIODevice::WriteOnly) || sf.write(reply->readAll()) < 0) {
            QFile::remove(m_assembledPath);
            fail(tr("Could not write the update signature."));
            return;
        }
        sf.close();

        // Pinned-fingerprint gpg verify off the GUI thread.
        m_verifyWatcher = new QFutureWatcher<bool>(this);
        connect(m_verifyWatcher, &QFutureWatcher<bool>::finished, this, [this] {
            const bool trusted = m_verifyWatcher->result();
            m_verifyWatcher->deleteLater();
            m_verifyWatcher = nullptr;
            QFile::remove(m_sigLocalPath);

            if (!trusted) {
                QFile::remove(m_assembledPath);
                fail(tr("Signature verification failed — update rejected."));
                return;
            }

            // Atomic same-dir swap of the verified file over the live $APPIMAGE
            // (design §9). The working copy is a sibling, so this is a rename(2).
            const QString live = AppImageUpdater::runningAppImagePath();
            QFile::setPermissions(m_assembledPath,
                                  QFileInfo(live).permissions() | QFileDevice::ExeOwner
                                      | QFileDevice::ExeGroup | QFileDevice::ExeOther);
            if (std::rename(QFile::encodeName(m_assembledPath).constData(),
                            QFile::encodeName(live).constData()) != 0) {
                QFile::remove(m_assembledPath);
                fail(tr("Could not replace the application file (location not writable?)."));
                return;
            }
            emit status(tr("Update ready."));
            setState(State::ReadyToRelaunch);
            ppInfo() << "Update verified and staged:" << m_latestVersion;
        });
        m_verifyWatcher->setFuture(
            QtConcurrent::run(&LinuxAppImageBackend::verifySignatureBlocking,
                              m_assembledPath, m_sigLocalPath));
    });
}

bool LinuxAppImageBackend::verifySignatureBlocking(const QString &appImagePath,
                                                   const QString &detachedSigPath)
{
    // Refuse outright until a real release key + pinned fingerprint exist (P2).
    if (pp::update::linux_logic::isPlaceholderFingerprint(kPinnedKeyFpr))
        return false;

    // Import the pinned public key into an EPHEMERAL keyring — never the user's.
    QTemporaryDir gpgHome;
    if (!gpgHome.isValid())
        return false;

    QFile keyRes(QStringLiteral(":/keys/pinpoint_release_pubkey.asc"));
    if (!keyRes.open(QIODevice::ReadOnly))
        return false;
    const QString keyPath = gpgHome.filePath(QStringLiteral("pub.asc"));
    QFile keyOut(keyPath);
    if (!keyOut.open(QIODevice::WriteOnly) || keyOut.write(keyRes.readAll()) < 0)
        return false;
    keyOut.close();

    auto runGpg = [&](const QStringList &args) -> QString {
        QProcess gpg;
        gpg.setProcessChannelMode(QProcess::MergedChannels);
        gpg.start(QStringLiteral("gpg"),
                  QStringList{QStringLiteral("--homedir"), gpgHome.path(),
                              QStringLiteral("--batch"), QStringLiteral("--status-fd"),
                              QStringLiteral("1")} + args);
        if (!gpg.waitForFinished(30000))
            return QString();
        return QString::fromUtf8(gpg.readAll());
    };

    if (!runGpg({QStringLiteral("--import"), keyPath}).contains(QStringLiteral("IMPORT_OK")))
        return false;

    const QString out = runGpg({QStringLiteral("--verify"), detachedSigPath, appImagePath});

    // Require a good, valid signature whose signing key fingerprint equals the pin.
    return pp::update::linux_logic::isTrustedValidSig(out, kPinnedKeyFpr);
}

// ── relaunch / dismiss / shutdown ──────────────────────────────────────────────

void LinuxAppImageBackend::relaunch()
{
    // The controller's session-safety guard has already passed by the time we get
    // here; just guard our own state defensively.
    if (m_state != State::ReadyToRelaunch)
        return;
    const QString live = AppImageUpdater::runningAppImagePath();
    if (!live.isEmpty())
        QProcess::startDetached(live, {});
    // Quit through the normal close path (controllers' aboutToQuit teardown runs).
    QCoreApplication::quit();
}

void LinuxAppImageBackend::installOnNextLaunch()
{
    if (m_state != State::ReadyToRelaunch)
        return;
    // The verified swap already happened — the next launch runs the new build.
    emit status(tr("Update will apply next time you open PinPoint Studio."));
    setState(State::Idle);
}

void LinuxAppImageBackend::shutdown()
{
    // Cancel any in-flight download so the worker/process is gone before teardown.
    if (m_updater)
        m_updater->cancel();
}
