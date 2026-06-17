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

#include "appimage_update.h"
#if defined(Q_OS_WIN)
#  include "win_sparkle_update.h"   // Windows in-app update engine (WinSparkle façade)
#endif
#include "app_settings.h"         // checkForUpdates() / skippedUpdateVersion persistence
#include "session_controller.h"   // running() for the relaunch session-safety guard
#include "version.h"   // PINPOINT_VERSION_STRING (src/Core)

#include <cstdio>      // std::rename — atomic same-dir overwrite for the final swap

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
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

// Rank a version postfix: clean release outranks any prerelease; rc > beta > alpha.
int postfixRank(const QString &p)
{
    if (p.isEmpty())                       return 100;
    const QString s = p.toLower();
    if (s.startsWith(QStringLiteral("rc")))    return 30;
    if (s.startsWith(QStringLiteral("beta")))  return 20;
    if (s.startsWith(QStringLiteral("alpha"))) return 10;
    return 0;
}

// Compare "v<maj>.<min>[-<word><n>]" strings. >0 if a newer than b, <0 older, 0 equal
// or unparseable (treated as "not newer" so we never prompt on garbage).
int compareVersion(const QString &a, const QString &b)
{
    static const QRegularExpression re(
        QStringLiteral("^v?(\\d+)\\.(\\d+)(?:[-.]?([a-zA-Z]+)(\\d*))?"));
    const auto ma = re.match(a);
    const auto mb = re.match(b);
    if (!ma.hasMatch() || !mb.hasMatch())
        return 0;
    const int amaj = ma.captured(1).toInt(), amin = ma.captured(2).toInt();
    const int bmaj = mb.captured(1).toInt(), bmin = mb.captured(2).toInt();
    if (amaj != bmaj) return amaj < bmaj ? -1 : 1;
    if (amin != bmin) return amin < bmin ? -1 : 1;
    const int ar = postfixRank(ma.captured(3)), br = postfixRank(mb.captured(3));
    if (ar != br) return ar < br ? -1 : 1;
    const int an = ma.captured(4).toInt(), bn = mb.captured(4).toInt();
    if (an != bn) return an < bn ? -1 : 1;
    return 0;
}

// The offered version is taken from the AppImage **asset filename**
// (PinPointStudio-<ver>-x86_64.AppImage), which package_appimage.sh derives from
// version.h at build time — NOT from the git tag. GitHub tag names are constrained
// by the release process (e.g. "Version0-1-0-alpha1"); the updater is deliberately
// independent of them. Empty if the name doesn't match the expected pattern.
QString versionFromAssetName(const QString &name)
{
    static const QRegularExpression re(
        QStringLiteral("^PinPointStudio-(.+)-x86_64\\.AppImage$"));
    const auto m = re.match(name);
    return m.hasMatch() ? m.captured(1) : QString();
}

} // namespace

UpdateController::UpdateController(AppSettings *settings, SessionController *session,
                                  QObject *parent)
    : QObject(parent), m_settings(settings), m_session(session)
{
    m_net = new QNetworkAccessManager(this);

#if defined(Q_OS_LINUX)
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
            QTimer::singleShot(4000, this, &UpdateController::checkNow);
    }
#elif defined(Q_OS_WIN) && defined(HAVE_WINSPARKLE)
    // Windows: WinSparkle is the engine and brings its own native UI (design §5). This
    // controller is a thin façade — supported when installed AND a real pinned key is
    // configured; the rich Downloading/Verifying/Ready states stay Linux-only.
    if (WinSparkleUpdater::isInstalledBuild()) {
        m_winSparkle = new WinSparkleUpdater(this);
        m_supported  = m_winSparkle->configureAndInit(m_settings, m_session);
        m_state      = m_supported ? State::Idle : State::Unsupported;
        // Keep WinSparkle's automatic-check pref in sync with the existing toggle.
        if (m_supported && m_settings)
            connect(m_settings, &AppSettings::checkForUpdatesChanged, this, [this] {
                if (m_winSparkle)
                    m_winSparkle->setAutomaticChecks(m_settings->checkForUpdates());
            });
    } else {
        m_state = State::DevBuild;   // build-tree run → inert (analogue of $APPIMAGE unset)
        m_supported = false;
    }
#else
    // macOS → Sparkle handles updates natively; Windows without WinSparkle → inert.
    m_state = State::Unsupported;
    m_supported = false;
#endif
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

void UpdateController::fail(const QString &error)
{
    m_errorString = error;
    ppWarn() << "Update error:" << error;
    setState(State::Error);
}

// ── check ────────────────────────────────────────────────────────────────────

void UpdateController::checkNow()
{
    if (!m_supported)
        return;

#if defined(Q_OS_WIN) && defined(HAVE_WINSPARKLE)
    // Hand off to WinSparkle's own check + UI (design §5B); no PinPoint state machine.
    if (m_winSparkle)
        m_winSparkle->checkNow();
    return;
#endif

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

void UpdateController::onFeedReply(QNetworkReply *reply)
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

    // Find the newest non-draft release that actually publishes a Linux AppImage.
    // The repo also carries Windows-only releases (e.g. installer .exe's) — those are
    // skipped, not treated as errors. Prereleases are included for now (alpha/beta —
    // design §7); a Stable/Beta filter on `prerelease` lands with GA. The offered
    // version comes from the AppImage asset filename, never the git tag.
    QString appImageName, sigUrl, notes, version;
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject rel = v.toObject();
        if (rel.value(QStringLiteral("draft")).toBool())
            continue;
        QString relApp, relSig;
        for (const QJsonValue &av : rel.value(QStringLiteral("assets")).toArray()) {
            const QJsonObject a = av.toObject();
            const QString n = a.value(QStringLiteral("name")).toString();
            const QString u = a.value(QStringLiteral("browser_download_url")).toString();
            if (n.endsWith(QStringLiteral(".AppImage.sig")))     relSig = u;
            else if (n.endsWith(QStringLiteral(".AppImage")))    relApp = n;
        }
        if (relApp.isEmpty())
            continue;   // Windows-only / asset-less release — keep looking down the list
        appImageName = relApp;
        sigUrl       = relSig;
        notes        = rel.value(QStringLiteral("body")).toString();
        version      = versionFromAssetName(relApp);
        break;
    }

    if (appImageName.isEmpty() || version.isEmpty()) {
        setState(State::UpToDate);   // no Linux build published (yet)
        return;
    }
    if (compareVersion(version, currentVersion()) <= 0) {
        setState(State::UpToDate);
        return;
    }

    m_appImageAssetName = appImageName;
    m_sigAssetUrl       = sigUrl;
    m_latestVersion     = version;
    m_releaseNotes      = notes;
    emit latestVersionChanged();
    setState(State::UpdateAvailable);
    emit updateOffered(version);
    ppInfo() << "Update available:" << version;
}

// ── download ───────────────────────────────────────────────────────────────────

void UpdateController::download()
{
    if (m_state != State::UpdateAvailable && m_state != State::Error)
        return;
    if (!AppImageUpdater::available()) {
        fail(tr("The update helper is unavailable in this build."));
        return;
    }

    m_errorString.clear();
    setProgress(0.0);
    setState(State::Downloading);

    m_updater = new AppImageUpdater(this);
    connect(m_updater, &AppImageUpdater::progress, this, &UpdateController::setProgress);
    connect(m_updater, &AppImageUpdater::status, this, &UpdateController::setStatus);
    connect(m_updater, &AppImageUpdater::finished, this,
            [this](bool ok, const QString &path, const QString &err) {
                if (m_updater) { m_updater->deleteLater(); m_updater = nullptr; }
                if (!ok) { fail(err); return; }
                startVerify(path);
            });
    m_updater->start();
}

// ── verify ───────────────────────────────────────────────────────────────────

void UpdateController::startVerify(const QString &assembledPath)
{
    m_assembledPath = assembledPath;
    setState(State::Verifying);
    setStatus(tr("Verifying signature…"));

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
            setStatus(tr("Update ready."));
            setState(State::ReadyToRelaunch);
            ppInfo() << "Update verified and staged:" << m_latestVersion;
        });
        m_verifyWatcher->setFuture(
            QtConcurrent::run(&UpdateController::verifySignatureBlocking,
                              m_assembledPath, m_sigLocalPath));
    });
}

bool UpdateController::verifySignatureBlocking(const QString &appImagePath,
                                               const QString &detachedSigPath)
{
    // Refuse outright until a real release key + pinned fingerprint exist (P2).
    if (kPinnedKeyFpr == QStringLiteral("0000000000000000000000000000000000000000"))
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
    // gpg --status-fd emits: "[GNUPG:] VALIDSIG <fpr> <date> ...".
    static const QRegularExpression validRe(
        QStringLiteral("\\[GNUPG:\\] VALIDSIG ([0-9A-Fa-f]{40})"));
    const auto m = validRe.match(out);
    if (!m.hasMatch())
        return false;
    return m.captured(1).compare(kPinnedKeyFpr, Qt::CaseInsensitive) == 0;
}

// ── relaunch ───────────────────────────────────────────────────────────────────

void UpdateController::relaunch()
{
    if (m_state != State::ReadyToRelaunch)
        return;
    // Never relaunch out from under a live session (design §5.2). The QML button is
    // disabled in that case; this is the hard guard.
    if (m_session && m_session->running()) {
        setStatus(tr("End the session to install the update."));
        return;
    }
    const QString live = AppImageUpdater::runningAppImagePath();
    if (!live.isEmpty())
        QProcess::startDetached(live, {});
    // Quit through the normal close path (controllers' aboutToQuit teardown runs).
    QCoreApplication::quit();
}

void UpdateController::installOnNextLaunch()
{
    if (m_state != State::ReadyToRelaunch)
        return;
    // The verified swap already happened — the next launch runs the new build.
    // Just acknowledge; the badge returns to a neutral state.
    setStatus(tr("Update will apply next time you open PinPoint Studio."));
    setState(State::Idle);
}

void UpdateController::skipVersion()
{
    if (m_settings && !m_latestVersion.isEmpty())
        m_settings->setSkippedUpdateVersion(m_latestVersion);
}

void UpdateController::shutdownUpdater()
{
#if defined(Q_OS_WIN) && defined(HAVE_WINSPARKLE)
    if (m_winSparkle)
        m_winSparkle->shutdown();
#endif
}
