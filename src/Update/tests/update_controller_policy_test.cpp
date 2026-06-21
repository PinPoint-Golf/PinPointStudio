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

// UpdateController policy tests: the state-machine relay, the QML state-string
// mapping (the contract PpUpdateBanner.qml switches on), the relaunch session-
// safety guard, and skip/auto-check wiring — all driven by a FakeUpdateBackend
// injected through the controller's dependency-injection constructor. No network,
// no subprocess, no platform engine. Real AppSettings + SessionController are used
// (both are lightweight and expose the exact API the controller touches); QSettings
// is isolated via QStandardPaths test mode in main().

#include "update_controller.h"
#include "update_backend_factory.h"
#include "fake_update_backend.h"

#include "app_settings.h"
#include "session_controller.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStringList>

using State = pp::update::State;

// The production UpdateController constructor delegates to makeUpdateBackend(); this
// suite only exercises the dependency-injection constructor, so we stub the factory
// rather than link the real one (which would drag in the Linux backend + its
// network/subprocess chain). Never called by these tests.
UpdateBackend *makeUpdateBackend(AppSettings *, SessionController *, QObject *)
{
    return nullptr;
}

namespace {

// Build a controller around a fake with the given capability, returning both. The
// fake is heap-allocated and reparented to (owned by) the controller.
FakeUpdateBackend *makeFake(State initial, bool supported, bool owns = true)
{
    auto *fake = new FakeUpdateBackend();
    fake->capInitial   = initial;
    fake->capSupported = supported;
    fake->capOwns      = owns;
    return fake;
}

} // namespace

// ── initial-state passthrough ─────────────────────────────────────────────────

TEST(UpdateControllerPolicy, InitialStatePassthrough)
{
    {
        auto *fake = makeFake(State::DevBuild, false);
        UpdateController ctrl(fake, nullptr, nullptr);
        EXPECT_EQ(ctrl.state(), QStringLiteral("devbuild"));
        EXPECT_FALSE(ctrl.supported());
    }
    {
        auto *fake = makeFake(State::Unsupported, false);
        UpdateController ctrl(fake, nullptr, nullptr);
        EXPECT_EQ(ctrl.state(), QStringLiteral("unsupported"));
        EXPECT_FALSE(ctrl.supported());
    }
    {
        auto *fake = makeFake(State::Idle, true);
        UpdateController ctrl(fake, nullptr, nullptr);
        EXPECT_EQ(ctrl.state(), QStringLiteral("idle"));
        EXPECT_TRUE(ctrl.supported());
    }
}

// ── QML state-string mapping (the PpUpdateBanner.qml contract) ─────────────────

TEST(UpdateControllerPolicy, StateStringMapping)
{
    auto *fake = makeFake(State::Idle, true);
    UpdateController ctrl(fake, nullptr, nullptr);

    const QList<QPair<State, QString>> table = {
        {State::Checking,        QStringLiteral("checking")},
        {State::UpToDate,        QStringLiteral("uptodate")},
        {State::UpdateAvailable, QStringLiteral("available")},
        {State::Downloading,     QStringLiteral("downloading")},
        {State::Verifying,       QStringLiteral("verifying")},
        {State::ReadyToRelaunch, QStringLiteral("ready")},
        {State::Error,           QStringLiteral("error")},
        {State::Idle,            QStringLiteral("idle")},
        {State::DevBuild,        QStringLiteral("devbuild")},
        {State::Unsupported,     QStringLiteral("unsupported")},
    };
    for (const auto &row : table) {
        fake->driveState(row.first);
        EXPECT_EQ(ctrl.state(), row.second) << "state index " << int(row.first);
    }
}

// ── offer wiring + emission order ──────────────────────────────────────────────

TEST(UpdateControllerPolicy, OfferWiringAndOrder)
{
    auto *fake = makeFake(State::Checking, true);
    UpdateController ctrl(fake, nullptr, nullptr);

    QStringList order;
    QObject::connect(&ctrl, &UpdateController::latestVersionChanged,
                     [&] { order << QStringLiteral("latestVersionChanged"); });
    QObject::connect(&ctrl, &UpdateController::stateChanged,
                     [&] { order << QStringLiteral("stateChanged"); });
    QObject::connect(&ctrl, &UpdateController::updateOffered,
                     [&](const QString &) { order << QStringLiteral("updateOffered"); });

    QSignalSpy offerSpy(&ctrl, &UpdateController::updateOffered);

    fake->driveOffer(QStringLiteral("v0.1-alpha4"), QStringLiteral("the notes"));

    EXPECT_EQ(ctrl.latestVersion(), QStringLiteral("v0.1-alpha4"));
    EXPECT_EQ(ctrl.releaseNotes(), QStringLiteral("the notes"));
    EXPECT_EQ(ctrl.state(), QStringLiteral("available"));
    EXPECT_TRUE(ctrl.available());

    // Exactly the pre-refactor order: latestVersionChanged → stateChanged → updateOffered.
    ASSERT_EQ(order.size(), 3);
    EXPECT_EQ(order[0], QStringLiteral("latestVersionChanged"));
    EXPECT_EQ(order[1], QStringLiteral("stateChanged"));
    EXPECT_EQ(order[2], QStringLiteral("updateOffered"));

    ASSERT_EQ(offerSpy.count(), 1);
    EXPECT_EQ(offerSpy.at(0).at(0).toString(), QStringLiteral("v0.1-alpha4"));
}

// ── progress passthrough ───────────────────────────────────────────────────────

TEST(UpdateControllerPolicy, ProgressPassthrough)
{
    auto *fake = makeFake(State::Downloading, true);
    UpdateController ctrl(fake, nullptr, nullptr);

    QSignalSpy progSpy(&ctrl, &UpdateController::progressChanged);
    fake->driveProgress(0.5);
    EXPECT_DOUBLE_EQ(ctrl.progress(), 0.5);
    EXPECT_GE(progSpy.count(), 1);
}

// ── status passthrough ─────────────────────────────────────────────────────────

TEST(UpdateControllerPolicy, StatusPassthrough)
{
    auto *fake = makeFake(State::Verifying, true);
    UpdateController ctrl(fake, nullptr, nullptr);

    QSignalSpy statusSpy(&ctrl, &UpdateController::statusMessageChanged);
    fake->driveStatus(QStringLiteral("Verifying signature…"));
    EXPECT_EQ(ctrl.statusMessage(), QStringLiteral("Verifying signature…"));
    EXPECT_GE(statusSpy.count(), 1);
}

// ── relaunch session-safety guard (the core controller policy) ─────────────────

TEST(UpdateControllerPolicy, RelaunchSessionGuard)
{
    auto *fake = makeFake(State::Idle, true);
    AppSettings settings;
    SessionController session;
    UpdateController ctrl(fake, &settings, &session);

    fake->driveState(State::ReadyToRelaunch);
    ASSERT_EQ(ctrl.state(), QStringLiteral("ready"));

    // Session live → relaunch is refused and the guard message is shown.
    session.start(0);
    ASSERT_TRUE(session.running());
    ctrl.relaunch();
    EXPECT_EQ(fake->relaunchCount, 0);
    EXPECT_FALSE(ctrl.statusMessage().isEmpty());

    // Session ended → relaunch is allowed.
    session.endSession();
    ASSERT_FALSE(session.running());
    ctrl.relaunch();
    EXPECT_EQ(fake->relaunchCount, 1);
}

TEST(UpdateControllerPolicy, RelaunchOnlyWhenReady)
{
    auto *fake = makeFake(State::Idle, true);
    SessionController session;   // never started → not running
    UpdateController ctrl(fake, nullptr, &session);

    // Not in ReadyToRelaunch → relaunch is a no-op even with no live session.
    fake->driveState(State::UpToDate);
    ctrl.relaunch();
    EXPECT_EQ(fake->relaunchCount, 0);
}

// ── download / installOnNextLaunch delegation ──────────────────────────────────

TEST(UpdateControllerPolicy, DownloadAndInstallDelegation)
{
    auto *fake = makeFake(State::UpdateAvailable, true);
    UpdateController ctrl(fake, nullptr, nullptr);

    ctrl.download();
    EXPECT_EQ(fake->downloadCount, 1);

    fake->driveState(State::ReadyToRelaunch);
    ctrl.installOnNextLaunch();
    EXPECT_EQ(fake->installCount, 1);
}

// ── checkNow is gated on supported() ───────────────────────────────────────────

TEST(UpdateControllerPolicy, CheckNowGatedOnSupported)
{
    {
        auto *fake = makeFake(State::Idle, true);
        UpdateController ctrl(fake, nullptr, nullptr);
        ctrl.checkNow();
        EXPECT_EQ(fake->checkNowCount, 1);
    }
    {
        auto *fake = makeFake(State::Unsupported, false);
        UpdateController ctrl(fake, nullptr, nullptr);
        ctrl.checkNow();
        EXPECT_EQ(fake->checkNowCount, 0);   // unsupported → never delegates
    }
}

// ── skipVersion persistence + empty guard ──────────────────────────────────────

TEST(UpdateControllerPolicy, SkipVersionPersists)
{
    auto *fake = makeFake(State::Checking, true);
    AppSettings settings;
    UpdateController ctrl(fake, &settings, nullptr);

    fake->driveOffer(QStringLiteral("v0.1-alpha4"), QString());
    ctrl.skipVersion();
    EXPECT_EQ(settings.skippedUpdateVersion(), QStringLiteral("v0.1-alpha4"));
}

TEST(UpdateControllerPolicy, SkipVersionNoOpWhenNoOffer)
{
    auto *fake = makeFake(State::Idle, true);
    AppSettings settings;
    settings.setSkippedUpdateVersion(QStringLiteral("sentinel"));
    UpdateController ctrl(fake, &settings, nullptr);

    // No offer → latestVersion empty → skipVersion must not overwrite.
    ctrl.skipVersion();
    EXPECT_EQ(settings.skippedUpdateVersion(), QStringLiteral("sentinel"));
}

// ── setAutomaticChecks wiring ──────────────────────────────────────────────────

TEST(UpdateControllerPolicy, AutomaticChecksWiring)
{
    auto *fake = makeFake(State::Idle, true);
    AppSettings settings;
    settings.setCheckForUpdates(true);
    UpdateController ctrl(fake, &settings, nullptr);

    const int before = fake->autoCount;
    settings.setCheckForUpdates(false);
    EXPECT_GT(fake->autoCount, before);
    EXPECT_FALSE(fake->lastAuto);
}

// ── shutdown delegation ────────────────────────────────────────────────────────

TEST(UpdateControllerPolicy, ShutdownDelegation)
{
    auto *fake = makeFake(State::Idle, true);
    UpdateController ctrl(fake, nullptr, nullptr);

    ctrl.shutdownUpdater();
    ctrl.shutdownUpdater();
    EXPECT_EQ(fake->shutdownCount, 2);   // idempotent at engine level; counts here
}

// ── error state carries the message ────────────────────────────────────────────

TEST(UpdateControllerPolicy, ErrorCarriesMessage)
{
    auto *fake = makeFake(State::Checking, true);
    UpdateController ctrl(fake, nullptr, nullptr);

    QSignalSpy stateSpy(&ctrl, &UpdateController::stateChanged);
    fake->driveState(State::Error, QStringLiteral("boom"));
    EXPECT_EQ(ctrl.state(), QStringLiteral("error"));
    EXPECT_EQ(ctrl.errorString(), QStringLiteral("boom"));
    EXPECT_GE(stateSpy.count(), 1);
}

int main(int argc, char **argv)
{
    // Isolate QSettings (AppSettings uses a fixed org/app) to a throwaway test path
    // so the suite never reads or writes the developer's real settings file.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
