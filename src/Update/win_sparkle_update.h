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

#include <string>

class AppSettings;
class SessionController;

// Thin Qt/Win32 shim over WinSparkle (the Windows analogue of macOS Sparkle) — the
// Windows in-app update engine. WinSparkle owns the appcast fetch, version compare,
// download, EdDSA signature verification, progress UI, and installer launch; this
// class only *configures* it, wires the session-safety callbacks to the single-active
// -session guard, and exposes a manual "check now". Authoritative design:
// docs/design/windows_update.md.
//
// Deliberately NOT a state machine (unlike the Linux UpdateController): WinSparkle is
// the engine and brings its own native UI, exactly as Sparkle does on macOS. The
// shared `updateController` QML façade delegates to this on Windows.
//
// The header is free of <winsparkle.h> so it can be included cheaply; all WinSparkle
// calls live in the .cpp (compiled only when HAVE_WINSPARKLE — i.e. WIN32 with the
// fetched package). A single instance is expected (WinSparkle's C callbacks carry no
// userdata, so the .cpp keeps a static self-pointer).
class WinSparkleUpdater : public QObject
{
    Q_OBJECT
public:
    explicit WinSparkleUpdater(QObject *parent = nullptr);
    ~WinSparkleUpdater() override;

    // True iff this process was installed by our Inno Setup installer — detected by
    // the `unins000.exe` Inno drops next to the executable. A dev build run from the
    // build tree has no installer to relaunch and no trusted feed, so the updater
    // stays inert there (the Windows analogue of the Linux `$APPIMAGE`-unset check).
    static bool isInstalledBuild();

    // Configure WinSparkle (feed URL, app details, monotonic build version, pinned
    // EdDSA public key, automatic-check pref, session-safety + error callbacks) and
    // schedule win_sparkle_init() once the event loop is running (WinSparkle requires
    // the main window to be up). Returns true iff initialisation will proceed — i.e.
    // a VALID pinned EdDSA key is configured. Returns false (and does NOT init) when
    // the pinned key is still the placeholder, so an unsigned/untrusted feed can never
    // drive an update (mirrors the Linux gate refusing until the real key lands, P2).
    bool configureAndInit(AppSettings *settings, SessionController *session);

    // Manual, user-initiated check — shows WinSparkle's UI whether or not an update is
    // found (and ignores any prior "Skip this version"). Backs the Settings action.
    void checkNow();

    // Mirror the "Check for updates automatically" pref into WinSparkle.
    void setAutomaticChecks(bool enabled);

    // win_sparkle_cleanup() — call on app shutdown (before Qt teardown). Idempotent.
    void shutdown();

private:
    // Deferred init target (scheduled by configureAndInit once the loop is up).
    void doInit();

    AppSettings       *m_settings = nullptr;
    SessionController *m_session  = nullptr;

    bool m_initialised = false;

    // Kept alive for WinSparkle (it takes pointers); harmless even though it copies.
    std::wstring m_companyName;
    std::wstring m_appName;
    std::wstring m_appVersion;
    std::wstring m_buildVersion;
};
