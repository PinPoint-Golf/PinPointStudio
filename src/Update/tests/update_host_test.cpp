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

// The updater's injected question and settings (UpdateHost), driven through a fake:
// UpdateController asks the host, at the moment it matters, whether a session runs
// before it lets a relaunch install an update; it follows the host's automatic-check
// setting; and it writes "skip this version" through the host. The suite compiles
// the controller with no Gui source at all, which is the point: the updater no longer
// reaches into src/Gui. No network, no platform engine, no settings file: the backend
// is a FakeUpdateBackend handed in through the controller's injection constructor,
// and the real factory is not linked (the stub below fails the test if called).

#include "update_controller.h"
#include "update_backend_factory.h"
#include "fake_update_backend.h"
#include "fake_update_host.h"

#include <gtest/gtest.h>

#include <QCoreApplication>

using State = pp::update::State;

namespace {
int g_factoryCalls = 0;
}

// update_controller.cpp's production constructor calls makeUpdateBackend(); these
// tests use only the injection constructor, so the real factory (and with it any
// platform engine that could reach the update feed) is never linked.
UpdateBackend *makeUpdateBackend(UpdateHost *, QObject *)
{
    ++g_factoryCalls;
    return nullptr;
}

namespace {

FakeUpdateBackend *readyBackend()
{
    auto *fake = new FakeUpdateBackend();
    fake->capInitial   = State::Idle;
    fake->capSupported = true;
    return fake;
}

} // namespace

// The question decides the relaunch: refused while the host says a session runs,
// with the words the golfer sees today; allowed once it says none does.
TEST(UpdateHost, RelaunchWaitsForTheSessionToEnd)
{
    FakeUpdateHost host;
    auto *fake = readyBackend();
    UpdateController ctrl(fake, &host);
    fake->driveState(State::ReadyToRelaunch);
    ASSERT_EQ(ctrl.state(), QStringLiteral("ready"));

    host.setRunning(true);
    ctrl.relaunch();
    EXPECT_EQ(fake->relaunchCount, 0);
    EXPECT_EQ(ctrl.statusMessage(), QStringLiteral("End the session to install the update."));

    host.setRunning(false);
    ctrl.relaunch();
    EXPECT_EQ(fake->relaunchCount, 1);
    EXPECT_GE(host.runningAsked, 2);
}

// The question is asked when the golfer presses relaunch, not remembered from a
// notice: a session that started without one still holds the update back.
TEST(UpdateHost, RelaunchAsksAtThatMoment)
{
    FakeUpdateHost host;
    auto *fake = readyBackend();
    UpdateController ctrl(fake, &host);
    fake->driveState(State::ReadyToRelaunch);

    host.running = true;   // no sessionRunningChanged
    ctrl.relaunch();
    EXPECT_EQ(fake->relaunchCount, 0);
}

// Not ready: the question is not even asked, and nothing relaunches.
TEST(UpdateHost, NotReadyNeverAsks)
{
    FakeUpdateHost host;
    auto *fake = readyBackend();
    UpdateController ctrl(fake, &host);
    fake->driveState(State::UpToDate);
    ctrl.relaunch();
    EXPECT_EQ(fake->relaunchCount, 0);
    EXPECT_EQ(host.runningAsked, 0);
}

// The automatic-check setting reaches the engine on every change, with the host's
// value, and not at construction (the engine reads it itself when it starts).
TEST(UpdateHost, AutomaticChecksFollowTheSetting)
{
    FakeUpdateHost host;
    host.checks = true;
    auto *fake = readyBackend();
    UpdateController ctrl(fake, &host);
    EXPECT_EQ(fake->autoCount, 0);

    host.setChecks(false);
    EXPECT_EQ(fake->autoCount, 1);
    EXPECT_FALSE(fake->lastAuto);

    host.setChecks(true);
    EXPECT_EQ(fake->autoCount, 2);
    EXPECT_TRUE(fake->lastAuto);
}

// "Skip this version" writes the offered version through the host, once; with no
// offer it writes nothing.
TEST(UpdateHost, SkipWritesTheOfferedVersion)
{
    FakeUpdateHost host;
    auto *fake = readyBackend();
    UpdateController ctrl(fake, &host);

    ctrl.skipVersion();
    EXPECT_EQ(host.skipWrites, 0);

    fake->driveOffer(QStringLiteral("v0.1-alpha9"), QString());
    ctrl.skipVersion();
    EXPECT_EQ(host.skipWrites, 1);
    EXPECT_EQ(host.skipped, QStringLiteral("v0.1-alpha9"));
}

// No host: no session ever blocks, and skip persists nothing, as with the null
// settings and session pointers before.
TEST(UpdateHost, NoHostNeverBlocks)
{
    auto *fake = readyBackend();
    UpdateController ctrl(fake, nullptr);
    fake->driveOffer(QStringLiteral("v2"), QString());
    ctrl.skipVersion();
    fake->driveState(State::ReadyToRelaunch);
    ctrl.relaunch();
    EXPECT_EQ(fake->relaunchCount, 1);
}

// Runs last (gtest keeps declaration order): no test above reached the factory.
TEST(UpdateHost, FactoryNeverCalled)
{
    EXPECT_EQ(g_factoryCalls, 0);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
