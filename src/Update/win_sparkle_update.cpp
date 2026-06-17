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

#include "win_sparkle_update.h"

#include "app_settings.h"        // checkForUpdates()
#include "session_controller.h"  // running() / runningChanged — relaunch session guard
#include "version.h"             // PINPOINT_VERSION_STRING / PINPOINT_VERSION_BUILD_STRING

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

#include "pp_debug.h"

#ifdef HAVE_WINSPARKLE
#  include <winsparkle.h>
#endif

namespace {

// ── Feed (design §4.1) ────────────────────────────────────────────────────────
// GitHub's stable "latest non-prerelease release" redirect — the appcast asset is
// re-resolved to whatever the newest published release carries, so this URL never
// encodes a version. Mirrors the Linux `gh-releases-zsync … latest` decision.
const char *const kAppcastUrl =
    "https://github.com/PinPoint-Golf/PinPointStudio/releases/latest/download/appcast-win.xml";

// ── Pinned EdDSA (Ed25519) public key (design §6) — THE trust anchor ──────────
// WinSparkle verifies each downloaded installer's `sparkle:edSignature` against this
// key before running it. The key is the base64 string in the compiled-in resource
// :/keys/pinpoint_release_win_eddsa.pub (single source of truth, mirroring the Linux
// .asc). PLACEHOLDER until the real release key is generated (P2): while it is the
// placeholder, configureAndInit() refuses to start WinSparkle at all, so an unsigned
// or untrusted feed can never drive an update — the analogue of the Linux all-zero
// pinned fingerprint.
const char *const kPinnedKeyResource = ":/keys/pinpoint_release_win_eddsa.pub";
const char *const kKeyPlaceholderPrefix = "PLACEHOLDER";

const wchar_t *const kCompanyName = L"Mark Liversedge";
const wchar_t *const kAppName     = L"PinPoint Studio";

// Daily background check (WinSparkle floors this at 3600s).
constexpr int kCheckIntervalSeconds = 24 * 60 * 60;

} // namespace

// ── WinSparkle C callbacks (no userdata → file-scope state) ───────────────────
#ifdef HAVE_WINSPARKLE
namespace {
// Mirror of SessionController::running(), written on the GUI thread and read by the
// can-shutdown callback OFF the main thread — hence atomic, and the only state the
// callbacks share with the object.
std::atomic<bool> g_sessionRunning{false};

// Called OFF the main thread before WinSparkle launches the installer. Refuse while
// a session is live (the user finishes it first) — the Windows mirror of the Linux
// "End the session to install" guard. Reads an atomic only; touches no QObject.
int __cdecl canShutdownCb()
{
    return g_sessionRunning.load() ? 0 : 1;
}

// Called OFF the main thread once WinSparkle is ready to install. Quit GRACEFULLY via
// the normal close path (controllers' aboutToQuit teardown runs) — never a hard kill.
// Post to the GUI thread; do not call Qt directly from this thread.
void __cdecl shutdownRequestCb()
{
    QMetaObject::invokeMethod(qApp, [] { QCoreApplication::quit(); },
                              Qt::QueuedConnection);
}

// Called OFF the main thread on an update error. Log on the GUI thread.
void __cdecl errorCb()
{
    QMetaObject::invokeMethod(qApp, [] {
        ppWarn() << "[WinSparkle] update check/download failed";
    }, Qt::QueuedConnection);
}
} // namespace
#endif // HAVE_WINSPARKLE

WinSparkleUpdater::WinSparkleUpdater(QObject *parent) : QObject(parent) {}

WinSparkleUpdater::~WinSparkleUpdater() { shutdown(); }

bool WinSparkleUpdater::isInstalledBuild()
{
    // Inno Setup drops `unins000.exe` next to the installed executable; a build-tree
    // run has none. Specific to our installer and reliable, unlike path heuristics.
    const QString uninst =
        QCoreApplication::applicationDirPath() + QStringLiteral("/unins000.exe");
    return QFileInfo::exists(uninst);
}

bool WinSparkleUpdater::configureAndInit(AppSettings *settings, SessionController *session)
{
#ifdef HAVE_WINSPARKLE
    m_settings = settings;
    m_session  = session;

    // Mirror SessionController::running() into the file-scope atomic the can-shutdown
    // callback reads off-thread. Written only here, on the GUI thread, on every
    // session transition.
    const auto syncRunning = [this] {
        g_sessionRunning.store(m_session && m_session->running());
    };
    syncRunning();
    if (m_session)
        connect(m_session, &SessionController::runningChanged, this, syncRunning);

    // Refuse to arm the updater until a real pinned key exists (P2). Without a valid
    // EdDSA key WinSparkle would accept an unsigned feed — never allow that.
    QByteArray pubKey;
    QFile keyRes(QString::fromLatin1(kPinnedKeyResource));
    if (keyRes.open(QIODevice::ReadOnly))
        pubKey = keyRes.readAll().trimmed();
    if (pubKey.isEmpty() || pubKey.startsWith(kKeyPlaceholderPrefix)) {
        ppInfo() << "[WinSparkle] no pinned EdDSA key yet — updater inert (awaiting P2)";
        return false;
    }
    if (win_sparkle_set_eddsa_public_key(pubKey.constData()) != 1) {
        ppWarn() << "[WinSparkle] pinned EdDSA key rejected — updater disabled";
        return false;
    }

    win_sparkle_set_appcast_url(kAppcastUrl);

    m_companyName  = std::wstring(kCompanyName);
    m_appName      = std::wstring(kAppName);
    m_appVersion   = QStringLiteral(PINPOINT_VERSION_STRING).toStdWString();
    m_buildVersion = QStringLiteral(PINPOINT_VERSION_BUILD_STRING).toStdWString();
    win_sparkle_set_app_details(m_companyName.c_str(), m_appName.c_str(),
                                m_appVersion.c_str());
    // The monotonic integer is the comparison key vs the appcast's `sparkle:version`
    // (design §7); the display string above is shown to the user.
    win_sparkle_set_app_build_version(m_buildVersion.c_str());

    win_sparkle_set_automatic_check_for_updates(
        (m_settings && m_settings->checkForUpdates()) ? 1 : 0);
    win_sparkle_set_update_check_interval(kCheckIntervalSeconds);

    win_sparkle_set_can_shutdown_callback(&canShutdownCb);
    win_sparkle_set_shutdown_request_callback(&shutdownRequestCb);
    win_sparkle_set_error_callback(&errorCb);

    // WinSparkle requires the main window to be up before init(). The controller is
    // constructed before the QML engine loads, so defer init briefly — past startup
    // and device bring-up, like the Linux 4s launch check.
    QTimer::singleShot(3000, this, &WinSparkleUpdater::doInit);
    return true;
#else
    Q_UNUSED(settings);
    Q_UNUSED(session);
    return false;
#endif
}

void WinSparkleUpdater::doInit()
{
#ifdef HAVE_WINSPARKLE
    if (m_initialised)
        return;
    win_sparkle_init();   // non-blocking; an automatic check (if enabled) runs now
    m_initialised = true;
    ppInfo() << "[WinSparkle] initialised —" << QString::fromStdWString(m_appVersion)
             << "(build" << QString::fromStdWString(m_buildVersion) << ")";
#endif
}

void WinSparkleUpdater::checkNow()
{
#ifdef HAVE_WINSPARKLE
    if (!m_initialised)
        doInit();
    win_sparkle_check_update_with_ui();
#endif
}

void WinSparkleUpdater::setAutomaticChecks(bool enabled)
{
#ifdef HAVE_WINSPARKLE
    win_sparkle_set_automatic_check_for_updates(enabled ? 1 : 0);
#else
    Q_UNUSED(enabled);
#endif
}

void WinSparkleUpdater::shutdown()
{
#ifdef HAVE_WINSPARKLE
    if (m_initialised) {
        win_sparkle_cleanup();
        m_initialised = false;
    }
#endif
}
