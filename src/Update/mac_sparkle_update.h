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

class AppSettings;
class SessionController;

// Thin Qt/Cocoa shim over Sparkle 2 (the macOS analogue of WinSparkle) — the macOS
// in-app update engine. Sparkle owns the appcast fetch, version compare, download,
// EdDSA signature + Developer-ID Team-ID verification, progress UI, and the in-place
// swap-and-relaunch; this class only *configures* it (`SUFeedURL` + `SUPublicEDKey`
// come from the bundle's Info.plist), wires the relaunch session-safety hook to the
// single-active-session guard, and exposes a manual "check now". Authoritative
// design: docs/design/macos_update.md.
//
// Deliberately NOT a state machine (unlike the Linux UpdateController): Sparkle is the
// engine and brings its own native UI, exactly as WinSparkle does on Windows. The
// shared `updateController` QML façade delegates to this on macOS.
//
// The header is free of <Sparkle/Sparkle.h> and Objective-C so it can be included
// from plain C++ (update_controller.cpp); the Cocoa/Sparkle objects live behind the
// opaque `void *` handles below and are defined only in the .mm (compiled when
// HAVE_SPARKLE — i.e. APPLE with the fetched framework). Sparkle's delegate callbacks
// run on the main thread, so unlike WinSparkle there is no atomic mirror of the
// session state — the guard touches SessionController directly (design §5.1).
class MacSparkleUpdater : public QObject
{
    Q_OBJECT
public:
    explicit MacSparkleUpdater(QObject *parent = nullptr);
    ~MacSparkleUpdater() override;

    // True iff this is a packaged, in-place-updatable build: -DPINPOINT_INSTALLED was
    // baked in (only the release-packaged target defines it) AND the bundle is not
    // running from a read-only / App-Translocated path (from which Sparkle cannot swap
    // in place — design §8). A dev build run from the build tree has neither, so the
    // updater stays inert there (the macOS analogue of the Linux `$APPIMAGE`-unset and
    // the Windows `unins000.exe` checks).
    static bool isInstalledBuild();

    // Configure Sparkle (it reads SUFeedURL + SUPublicEDKey from Info.plist), set the
    // automatic-check pref + interval, install the session-safety + error delegate, and
    // schedule -startUpdater once the event loop / main window is up (Sparkle needs a
    // live UI). Returns true iff initialisation will proceed — i.e. a VALID pinned
    // EdDSA key is configured. Returns false (and does NOT arm Sparkle) when
    // SUPublicEDKey is still the placeholder, so an unsigned/untrusted feed can never
    // drive an update (mirrors the Linux/Windows gates refusing until the real key
    // lands, P2 / design §6).
    bool configureAndInit(AppSettings *settings, SessionController *session);

    // Manual, user-initiated check — shows Sparkle's UI whether or not an update is
    // found (and ignores any prior "Skip this version"). Backs the Settings action.
    void checkNow();

    // Mirror the "Check for updates automatically" pref into Sparkle.
    void setAutomaticChecks(bool enabled);

    // Release the updater controller — call on app shutdown (before Qt teardown).
    // Idempotent.
    void shutdown();

private:
    // Deferred -startUpdater target (scheduled by configureAndInit once the loop is up).
    void doInit();
    // Main-thread reaction to SessionController::runningChanged: when a session ends,
    // fire any relaunch the postpone hook is holding (design §5.1, §9).
    void onSessionRunningChanged();

    AppSettings       *m_settings = nullptr;
    SessionController *m_session  = nullptr;

    bool m_initialised = false;

    // Opaque Objective-C handles (defined in the .mm) — keeps Sparkle/Cocoa out of the
    // header. m_updaterController: SPUStandardUpdaterController*; m_delegate: the
    // SPUUpdaterDelegate that owns the postponed-relaunch block. Both retained for the
    // app lifetime and released in shutdown().
    void *m_updaterController = nullptr;
    void *m_delegate          = nullptr;
};
