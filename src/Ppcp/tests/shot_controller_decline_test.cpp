/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

// MSG 8.5 (CR-03) — every reason `ShotController` declines a PPCP Shot with,
// driven through the public surface and read off `shotDeclined`.
//
// The four reasons are 8.5's registry values and each is shown to a person at
// the phone, so the assertion is on the exact string: `not_corroborated`,
// `busy`, `review_mode`, `session_ended`.  And the negative half — a Shot this
// host DOES keep is never declined — is asserted beside them, because a decline
// on a swing we record tells the phone to drop footage we are about to ask for.
//
// ⚠ NO BRIDGE.  `commitArbitratedShot()` never touches one, and a Shot here is
// just an id and a `t0` on `tb:host` — which is what the host service hands
// this class after the arbiter issues.

#include "shot_controller.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QVariantMap>

#include <gtest/gtest.h>

#include <cstdio>
#include <memory>
#include <ostream>

// A failed reason reads as `"busy"` rather than sixteen two-byte objects.  In
// the global namespace, where ADL finds it for QString.
void PrintTo(const QString &s, std::ostream *os) { *os << '"' << s.toStdString() << '"'; }

namespace {

using Source = ShotController::Source;

// `t0` as the host service passes it: EventBuffer::nowMicros() on `tb:host`, in
// nanoseconds.
qint64 nowHostNs() { return static_cast<qint64>(pinpoint::EventBuffer::nowMicros()) * 1000; }

// A buffer that is CAPTURING, the way the application has one: started, the
// controller constructed on it, then resumed.
//
// ⚠ THE RESUME IS OURS, AND THE COMMENTS SAY IT SHOULD NOT BE NEEDED.  Both
// `event_buffer.h` (`no_source_paused_`: "registerSource() clears this and
// calls resume() automatically") and ShotController's constructor ("Registering
// the first source auto-resumes the buffer") say the first registerSource()
// resumes a started buffer.  Measured here on 24 Sep 2026: it does not — the
// buffer stays Paused, and the application is Capturing only because main.cpp
// calls `cameraManager.applyCaptureIntent()` straight afterwards.  So the
// fixture does what main.cpp does, and asserts the Paused state first so the
// day registerSource() starts resuming by itself this line says so.
struct Capturing {
    std::unique_ptr<pinpoint::EventBuffer> buffer;
    std::unique_ptr<ShotController>        ctl;

    void build()
    {
        buffer = std::make_unique<pinpoint::EventBuffer>();
        buffer->start();
        ctl = std::make_unique<ShotController>(buffer.get(), nullptr);
        ASSERT_EQ(buffer->state(), pinpoint::BufferState::Paused)
            << "registerSource() now resumes by itself: the note above is stale";
        buffer->resume();   // main.cpp: cameraManager.applyCaptureIntent()
        ctl->reevaluateArmed();
        ASSERT_TRUE(buffer->isCapturing());
        ASSERT_TRUE(ctl->armed());
    }
    ~Capturing() { ctl.reset(); buffer.reset(); }   // the controller's source first
};

QString reasonOf(const QSignalSpy &spy, int i = 0) { return spy.at(i).at(1).toString(); }
QString shotOf(const QSignalSpy &spy, int i = 0)   { return spy.at(i).at(0).toString(); }

}  // namespace

// ── not_corroborated — a detector was listening and heard nothing ─────────

TEST(ShotControllerDecline, AnUncorroboratedShotIsDeclinedNotCorroborated)
{
    Capturing C;
    ASSERT_NO_FATAL_FAILURE(C.build());
    C.ctl->setDetectorAvailable(Source::Acoustic, true);
    QSignalSpy declined(C.ctl.get(), &ShotController::shotDeclined);
    QSignalSpy captured(C.ctl.get(), &ShotController::captureRequested);
    const int failBefore = C.ctl->shotStats().value("corroborateFail").toInt();

    C.ctl->commitArbitratedShot(nowHostNs(), QStringLiteral("S1"));

    ASSERT_EQ(declined.count(), 1);
    EXPECT_EQ(shotOf(declined), QStringLiteral("S1"));
    EXPECT_EQ(reasonOf(declined), QStringLiteral("not_corroborated"));
    EXPECT_EQ(C.ctl->shotStats().value("corroborateFail").toInt(), failBefore + 1);
    EXPECT_EQ(captured.count(), 0) << "footage asked for on a Shot this host refused";
}

// ── busy — corroborated (nothing to refute it with), but the pipeline is ────
//    still on the last swing

TEST(ShotControllerDecline, AShotWhileTheProcessorIsBusyIsDeclinedBusy)
{
    Capturing C;
    ASSERT_NO_FATAL_FAILURE(C.build());
    C.ctl->setProcessorBusy(true);
    ASSERT_FALSE(C.ctl->armed());
    QSignalSpy declined(C.ctl.get(), &ShotController::shotDeclined);
    QSignalSpy captured(C.ctl.get(), &ShotController::captureRequested);

    C.ctl->commitArbitratedShot(nowHostNs(), QStringLiteral("S1"));

    ASSERT_EQ(declined.count(), 1);
    EXPECT_EQ(shotOf(declined), QStringLiteral("S1"));
    EXPECT_EQ(reasonOf(declined), QStringLiteral("busy"));
    EXPECT_EQ(C.ctl->shotStats().value("droppedBusy").toInt(), 1);
    EXPECT_EQ(captured.count(), 0);
}

// ── review_mode — and it WINS over a stopped buffer ────────────────────────
//
// Entering review also stops capture, and the golfer did the former; so
// `disarmReason()` tests review first.  Asserted both ways: with a live buffer,
// and with none at all, where `session_ended` would otherwise be the answer.

TEST(ShotControllerDecline, AShotDuringReviewIsDeclinedReviewModeEvenWithCaptureStopped)
{
    {
        Capturing C;
        ASSERT_NO_FATAL_FAILURE(C.build());
        C.ctl->setReviewActive(true);
        QSignalSpy declined(C.ctl.get(), &ShotController::shotDeclined);
        C.ctl->commitArbitratedShot(nowHostNs(), QStringLiteral("S1"));
        ASSERT_EQ(declined.count(), 1);
        EXPECT_EQ(reasonOf(declined), QStringLiteral("review_mode"));
    }
    {
        ShotController ctl(nullptr, nullptr);
        ctl.setReviewActive(true);
        ctl.setProcessorBusy(true);   // and busy too: review still wins
        QSignalSpy declined(&ctl, &ShotController::shotDeclined);
        ctl.commitArbitratedShot(nowHostNs(), QStringLiteral("S2"));
        ASSERT_EQ(declined.count(), 1);
        EXPECT_EQ(shotOf(declined), QStringLiteral("S2"));
        EXPECT_EQ(reasonOf(declined), QStringLiteral("review_mode"))
            << "review must be named ahead of a stopped buffer";
    }
}

// ── session_ended — this host is not recording ─────────────────────────────

TEST(ShotControllerDecline, AShotWithNoBufferIsDeclinedSessionEnded)
{
    ShotController ctl(nullptr, nullptr);
    ASSERT_FALSE(ctl.armed());
    QSignalSpy declined(&ctl, &ShotController::shotDeclined);
    ctl.commitArbitratedShot(nowHostNs(), QStringLiteral("S1"));
    ASSERT_EQ(declined.count(), 1);
    EXPECT_EQ(shotOf(declined), QStringLiteral("S1"));
    EXPECT_EQ(reasonOf(declined), QStringLiteral("session_ended"));
}

TEST(ShotControllerDecline, AShotOnANeverStartedBufferIsDeclinedSessionEndedNotBusy)
{
    // Busy as well, so the test fails if `disarmReason()` ever asks the
    // processor before the buffer: "still working on the last one" would be a
    // false thing to tell a golfer whose host has stopped recording.
    pinpoint::EventBuffer buffer;
    auto ctl = std::make_unique<ShotController>(&buffer, nullptr);
    ASSERT_FALSE(buffer.isCapturing());
    ctl->setProcessorBusy(true);
    QSignalSpy declined(ctl.get(), &ShotController::shotDeclined);
    ctl->commitArbitratedShot(nowHostNs(), QStringLiteral("S1"));
    ASSERT_EQ(declined.count(), 1);
    EXPECT_EQ(reasonOf(declined), QStringLiteral("session_ended"));
    ctl.reset();
}

// ── No id, no statement ────────────────────────────────────────────────────
//
// 8.5 names a Shot by id; a decline with an empty one is a malformed message,
// not a vaguer decline.  Both refusal paths are covered: uncorroborated, and
// dropped for want of a recording buffer.

TEST(ShotControllerDecline, AShotWithNoIdIsNeverDeclined)
{
    {
        Capturing C;
        ASSERT_NO_FATAL_FAILURE(C.build());
        C.ctl->setDetectorAvailable(Source::Acoustic, true);
        QSignalSpy declined(C.ctl.get(), &ShotController::shotDeclined);
        QSignalSpy refused(C.ctl.get(), &ShotController::shotRefused);
        C.ctl->commitArbitratedShot(nowHostNs(), QString());
        EXPECT_EQ(refused.count(), 1) << "precondition: the Shot WAS refused";
        EXPECT_EQ(declined.count(), 0);
    }
    {
        ShotController ctl(nullptr, nullptr);
        QSignalSpy declined(&ctl, &ShotController::shotDeclined);
        QSignalSpy refused(&ctl, &ShotController::shotRefused);
        ctl.commitArbitratedShot(nowHostNs(), QString());
        EXPECT_EQ(refused.count(), 1) << "precondition: the Shot WAS dropped";
        EXPECT_EQ(declined.count(), 0);
    }
}

// ── The negative half — a Shot this host keeps is asked for, never declined ─

TEST(ShotControllerDecline, ACorroboratedShotWhileArmedIsRequestedAndNeverDeclined)
{
    // With a detector listening AND a detection of its own at t0.
    {
        Capturing C;
        ASSERT_NO_FATAL_FAILURE(C.build());
        C.ctl->setDetectorAvailable(Source::Acoustic, true);
        const qint64 t0Ns = nowHostNs();
        C.ctl->injectDetection(Source::Acoustic, t0Ns / 1000);
        QSignalSpy declined(C.ctl.get(), &ShotController::shotDeclined);
        QSignalSpy captured(C.ctl.get(), &ShotController::captureRequested);
        QSignalSpy detected(C.ctl.get(), &ShotController::shotDetected);

        C.ctl->commitArbitratedShot(t0Ns, QStringLiteral("S1"));

        EXPECT_EQ(declined.count(), 0);
        EXPECT_EQ(detected.count(), 1);
        ASSERT_EQ(captured.count(), 1);
        EXPECT_EQ(captured.at(0).at(0).toString(), QStringLiteral("S1"));
        // Microsecond precision at the local store, nanoseconds restored on the
        // way out: a truncation, never a shift.
        EXPECT_EQ(captured.at(0).at(1).toLongLong(), (t0Ns / 1000) * 1000);
        EXPECT_EQ(C.ctl->shotStats().value("corroboratePass").toInt(), 1);
    }
    // With nothing listening: accepted unweighed (the honest exemption).
    {
        Capturing C;
        ASSERT_NO_FATAL_FAILURE(C.build());
        QSignalSpy declined(C.ctl.get(), &ShotController::shotDeclined);
        QSignalSpy captured(C.ctl.get(), &ShotController::captureRequested);
        C.ctl->commitArbitratedShot(nowHostNs(), QStringLiteral("S2"));
        EXPECT_EQ(declined.count(), 0);
        ASSERT_EQ(captured.count(), 1);
        EXPECT_EQ(captured.at(0).at(0).toString(), QStringLiteral("S2"));
    }
}

int main(int argc, char **argv)
{
    // ⛔ BEFORE ANYTHING ELSE.  `PINPOINT_PPCP_ACCEPT_ALL=1` turns every refused
    // Shot into a recorded one, and `commitArbitratedShot()` reads it ONCE into
    // a function-local static — so a developer shell that exported it for a
    // bench session would make `not_corroborated` untestable for the life of
    // this process, and quietly: the corroboration tests would fail as if the
    // code were wrong.  Cleared here, and the clearing checked.
    qunsetenv("PINPOINT_PPCP_ACCEPT_ALL");
    if (qEnvironmentVariableIsSet("PINPOINT_PPCP_ACCEPT_ALL")) {
        std::fprintf(stderr, "PINPOINT_PPCP_ACCEPT_ALL could not be cleared\n");
        return 1;
    }
    // Qt's signal machinery and QObject timers want an application object.
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
