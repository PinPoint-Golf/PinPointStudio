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

// Compiled as Objective-C++ with ARC (-fobjc-arc, set in CMakeLists) — the two
// long-lived Cocoa objects are stored in the C++ object's `void *` handles via
// __bridge_retained / __bridge_transfer so ARC's ownership is explicit at the C++
// boundary; the postponed-relaunch block is held by an ARC `copy` property on the
// delegate.

#include "mac_sparkle_update.h"

#include "app_settings.h"        // checkForUpdates()
#include "session_controller.h"  // running() / runningChanged — relaunch session guard

#include <QCoreApplication>
#include <QString>
#include <QTimer>

#include "pp_debug.h"

#ifdef HAVE_SPARKLE
#  import <Sparkle/Sparkle.h>
#endif

#import <Foundation/Foundation.h>
#include <unistd.h>   // access(W_OK)

namespace {

// SUPublicEDKey placeholder (CMake bakes "PLACEHOLDER" into Info.plist while the
// offline release key has not been generated — design §6 / S1·P2). While the pinned
// key is the placeholder, configureAndInit() refuses to arm Sparkle at all, so an
// unsigned or untrusted feed can never drive an update.
NSString *const kKeyPlaceholder = @"PLACEHOLDER";

// Daily background check (Sparkle floors this at 3600s).
constexpr double kCheckIntervalSeconds = 24 * 60 * 60;

} // namespace

#ifdef HAVE_SPARKLE
// ── SPUUpdaterDelegate: session-safety relaunch gate + error logging ──────────
// Sparkle's delegate callbacks run on the main thread (it is a Cocoa UI framework),
// so this reads SessionController directly — no atomic mirror, no queued invoke
// (design §5.1, the macOS simplification over WinSparkle's off-thread can_shutdown).
@interface PPSparkleDelegate : NSObject <SPUUpdaterDelegate>
@property (nonatomic, assign) SessionController *session;        // C++ ptr, not owned
@property (nonatomic, copy)   void (^pendingInstall)(void);      // held while a session runs
@end

@implementation PPSparkleDelegate

// Sparkle is ready to relaunch-and-install. While a session is live, retain the
// install block and defer (the user finishes the session first) — the macOS mirror of
// the Linux "End the session to install" guard and the Windows can_shutdown gate. The
// block is fired from MacSparkleUpdater::onSessionRunningChanged when the session ends.
- (BOOL)updater:(SPUUpdater *)updater
        shouldPostponeRelaunchForUpdate:(SUAppcastItem *)item
        untilInvokingBlock:(void (^)(void))installHandler
{
    Q_UNUSED(updater);
    Q_UNUSED(item);
    if (self.session && self.session->running()) {
        self.pendingInstall = installHandler;   // ARC copy property retains the block
        ppInfo() << "[Sparkle] update staged — postponing relaunch until the session ends";
        return YES;
    }
    return NO;   // no live session → let Sparkle relaunch + install now
}

// Invoke any held relaunch once it is safe (session ended). No-op if nothing pending.
- (void)firePendingInstallIfSafe
{
    if (self.pendingInstall && !(self.session && self.session->running())) {
        void (^blk)(void) = self.pendingInstall;
        self.pendingInstall = nil;
        ppInfo() << "[Sparkle] session ended — performing the postponed update relaunch";
        blk();
    }
}

- (void)updater:(SPUUpdater *)updater didAbortWithError:(NSError *)error
{
    Q_UNUSED(updater);
    ppWarn() << "[Sparkle] update aborted:"
             << QString::fromNSString(error.localizedDescription);
}

- (void)updater:(SPUUpdater *)updater
        didFailToDownloadUpdate:(SUAppcastItem *)item
        error:(NSError *)error
{
    Q_UNUSED(updater);
    Q_UNUSED(item);
    ppWarn() << "[Sparkle] update download failed:"
             << QString::fromNSString(error.localizedDescription);
}

@end
#endif // HAVE_SPARKLE

MacSparkleUpdater::MacSparkleUpdater(QObject *parent) : QObject(parent) {}

MacSparkleUpdater::~MacSparkleUpdater() { shutdown(); }

bool MacSparkleUpdater::isInstalledBuild()
{
#ifndef PINPOINT_INSTALLED
    // Plain build-tree run → no trusted feed, nothing to update in place.
    return false;
#else
    @autoreleasepool {
        NSString *path = [[NSBundle mainBundle] bundlePath];
        if (path == nil)
            return false;
        // App Translocation runs a quarantined/un-notarized bundle from a randomized
        // read-only path, from which Sparkle cannot swap in place (design §8).
        if ([path rangeOfString:@"/AppTranslocation/"].location != NSNotFound)
            return false;
        // The bundle's parent must be writable for the in-place swap (e.g. /Applications,
        // not a mounted read-only DMG). Belt-and-braces alongside notarization.
        NSString *parent = [path stringByDeletingLastPathComponent];
        if (access(parent.fileSystemRepresentation, W_OK) != 0)
            return false;
        return true;
    }
#endif
}

bool MacSparkleUpdater::configureAndInit(AppSettings *settings, SessionController *session)
{
#ifdef HAVE_SPARKLE
    m_settings = settings;
    m_session  = session;

    @autoreleasepool {
        // Refuse to arm the updater until a real pinned EdDSA key exists (design §6).
        // SUPublicEDKey is baked into Info.plist from the committed .pub at configure
        // time; while it is the placeholder, an untrusted feed could otherwise drive an
        // update — never allow that.
        NSString *pinned =
            [[NSBundle mainBundle] objectForInfoDictionaryKey:@"SUPublicEDKey"];
        if (pinned.length == 0 || [pinned isEqualToString:kKeyPlaceholder]) {
            ppInfo() << "[Sparkle] no pinned EdDSA key yet — updater inert (awaiting P2)";
            return false;
        }

        PPSparkleDelegate *delegate = [[PPSparkleDelegate alloc] init];
        delegate.session = m_session;

        // startingUpdater:NO — defer the actual -startUpdater to doInit(), once the main
        // window is up (Sparkle requires a live UI). Reads SUFeedURL + SUPublicEDKey
        // from Info.plist; the delegate is the session-safety + error hook.
        SPUStandardUpdaterController *uc =
            [[SPUStandardUpdaterController alloc] initWithStartingUpdater:NO
                                                         updaterDelegate:delegate
                                                      userDriverDelegate:nil];
        uc.updater.automaticallyChecksForUpdates =
            (m_settings && m_settings->checkForUpdates());
        uc.updater.updateCheckInterval = kCheckIntervalSeconds;

        // Retain across the C++ boundary; released in shutdown().
        m_delegate          = (__bridge_retained void *)delegate;
        m_updaterController = (__bridge_retained void *)uc;
    }

    // Fire any postponed relaunch when a live session ends (design §5.1). Main-thread
    // signal → main-thread slot; the guard touches SessionController directly.
    if (m_session)
        connect(m_session, &SessionController::runningChanged,
                this, &MacSparkleUpdater::onSessionRunningChanged);

    // Sparkle needs the main window up before -startUpdater. The controller is created
    // before the QML engine loads, so defer briefly — past startup and device
    // bring-up, exactly like the Windows 3s deferral and the Linux 4s launch check.
    QTimer::singleShot(3000, this, &MacSparkleUpdater::doInit);
    return true;
#else
    Q_UNUSED(settings);
    Q_UNUSED(session);
    return false;
#endif
}

void MacSparkleUpdater::doInit()
{
#ifdef HAVE_SPARKLE
    if (m_initialised || m_updaterController == nullptr)
        return;
    SPUStandardUpdaterController *uc =
        (__bridge SPUStandardUpdaterController *)m_updaterController;
    [uc startUpdater];   // non-blocking; an automatic check (if enabled) runs now
    m_initialised = true;
    ppInfo() << "[Sparkle] initialised — automatic checks"
             << ((m_settings && m_settings->checkForUpdates()) ? "on" : "off");
#endif
}

void MacSparkleUpdater::checkNow()
{
#ifdef HAVE_SPARKLE
    if (m_updaterController == nullptr)
        return;
    if (!m_initialised)
        doInit();
    SPUStandardUpdaterController *uc =
        (__bridge SPUStandardUpdaterController *)m_updaterController;
    [uc checkForUpdates:nil];
#endif
}

void MacSparkleUpdater::setAutomaticChecks(bool enabled)
{
#ifdef HAVE_SPARKLE
    if (m_updaterController == nullptr)
        return;
    SPUStandardUpdaterController *uc =
        (__bridge SPUStandardUpdaterController *)m_updaterController;
    uc.updater.automaticallyChecksForUpdates = enabled;
#else
    Q_UNUSED(enabled);
#endif
}

void MacSparkleUpdater::onSessionRunningChanged()
{
#ifdef HAVE_SPARKLE
    if (m_delegate == nullptr)
        return;
    PPSparkleDelegate *delegate = (__bridge PPSparkleDelegate *)m_delegate;
    [delegate firePendingInstallIfSafe];
#endif
}

void MacSparkleUpdater::shutdown()
{
#ifdef HAVE_SPARKLE
    // Transfer ownership back to ARC and let the objects deallocate.
    if (m_updaterController) {
        SPUStandardUpdaterController *uc =
            (__bridge_transfer SPUStandardUpdaterController *)m_updaterController;
        (void)uc;
        m_updaterController = nullptr;
    }
    if (m_delegate) {
        PPSparkleDelegate *delegate = (__bridge_transfer PPSparkleDelegate *)m_delegate;
        (void)delegate;
        m_delegate = nullptr;
    }
    m_initialised = false;
#endif
}
