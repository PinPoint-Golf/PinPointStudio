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

// The GSPro listener, driven by a real client socket on loopback.
//
// ⚠ THIS IS THE HALF THE LIBRARY CANNOT TEST. libgspro owns no socket by
// construction, so its own suite proves the protocol against byte strings and
// stops there; what a launch monitor actually experiences — that something is
// listening, that the reply comes back promptly, that a shot becomes a reading,
// that a dead client does not take the listener with it — is this class's, and
// only a socket can show it.
//
// ⚠ EVERY TEST BINDS PORT 0, NEVER 921. The default port is very often already
// held (by GSPro itself, on the machine where both would run), and a suite that
// fought a real listener for it would fail for a reason that has nothing to do
// with the code. Port 0 asks the kernel for a free one; boundPort() reports it.
//
// No launch monitor, no network beyond loopback, no fixtures on disk.

#include "gspro_monitor.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTcpSocket>

#include <gspro/gspro.h>

using pinpoint::lm::GsProMonitor;
using pinpoint::lm::Kind;
using pinpoint::lm::LaunchMonitorReading;
using pinpoint::lm::State;

namespace {

const char *kShot =
    "{\"DeviceID\":\"TestMonitor\",\"Units\":\"Yards\",\"ShotNumber\":21,"
    "\"BallData\":{\"Speed\":147.5,\"SpinAxis\":-13.2,\"TotalSpin\":3250.0,"
    "\"HLA\":2.3,\"VLA\":13.7,\"CarryDistance\":256.5},"
    "\"ClubData\":{\"Speed\":100.1,\"AngleOfAttack\":-2.2,\"FaceToTarget\":1.1,"
    "\"Path\":-0.7},"
    "\"ShotDataOptions\":{\"ContainsBallData\":true,\"ContainsClubData\":true,"
    "\"LaunchMonitorIsReady\":true,\"IsHeartBeat\":false}}";

const char *kHeartbeat =
    "{\"DeviceID\":\"TestMonitor\",\"Units\":\"Yards\",\"ShotNumber\":0,"
    "\"ShotDataOptions\":{\"ContainsBallData\":false,\"ContainsClubData\":false,"
    "\"LaunchMonitorIsReady\":true,\"IsHeartBeat\":true}}";

// Pump the event loop until `done` or the budget runs out. Returns what it
// found, so a case asserts on the outcome rather than on the wait.
template <typename Fn>
bool waitFor(Fn done, int budgetMs = 3000)
{
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < budgetMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return done();
}

// A launch monitor, as far as the listener can tell.
class FakeClient
{
public:
    bool connectTo(quint16 port)
    {
        m_sock.connectToHost(QHostAddress::LocalHost, port);
        return m_sock.waitForConnected(3000);
    }
    void send(const char *json) { m_sock.write(json); m_sock.flush(); }
    void sendBytes(const QByteArray &b) { m_sock.write(b); m_sock.flush(); }
    void close() { m_sock.abort(); }

    // Every complete JSON object the listener has sent us, framed the way a real
    // client must frame them: there is no delimiter on the wire, so a reply may
    // arrive split or two may arrive together. The library's own framer is used
    // here from the OUTSIDE, which is exactly what a client has to do.
    QList<int> codes()
    {
        m_buf += m_sock.readAll();
        QList<int> out;
        for (;;) {
            size_t start = 0, end = 0;
            const auto st = gsp_frame_find(reinterpret_cast<const uint8_t *>(m_buf.constData()),
                                           size_t(m_buf.size()), &start, &end);
            if (st != GSP_OK)
                break;
            gsp_response r;
            if (gsp_response_decode(reinterpret_cast<const uint8_t *>(m_buf.constData()) + start,
                                    end - start, &r) == GSP_OK)
                out.append(int(r.code));
            m_buf.remove(0, int(end));
        }
        m_seen += out;
        return m_seen;
    }

    QTcpSocket &socket() { return m_sock; }

private:
    QTcpSocket m_sock;
    QByteArray m_buf;
    QList<int> m_seen;
};

// A listener on an ephemeral port, started and stopped with the case.
class Listener
{
public:
    Listener() { m.setSourcePath(QStringLiteral("127.0.0.1:0")); m.start(); }
    ~Listener() { m.stop(); }
    GsProMonitor m;
};

} // namespace

TEST(GsProMonitor, ListensAndReportsWaitingUntilSomethingConnects)
{
    Listener l;
    ASSERT_EQ(l.m.state(), State::Waiting) << l.m.errorText().toStdString();
    EXPECT_GT(l.m.boundPort(), 0);
    EXPECT_EQ(l.m.connectionCount(), 0);
    EXPECT_EQ(l.m.kind(), Kind::GsPro);
    EXPECT_TRUE(l.m.sourceDescription().contains(QStringLiteral("waiting")));
}

TEST(GsProMonitor, AShotBecomesAReadingAndIsAcknowledged)
{
    Listener l;
    QSignalSpy spy(&l.m, &GsProMonitor::readingAvailable);

    FakeClient client;
    ASSERT_TRUE(client.connectTo(l.m.boundPort()));
    ASSERT_TRUE(waitFor([&] { return l.m.connectionCount() == 1; }));

    client.send(kShot);
    ASSERT_TRUE(waitFor([&] { return spy.count() == 1; }));

    const auto reading = spy.at(0).at(0).value<LaunchMonitorReading>();
    EXPECT_EQ(reading.deviceShotId.toStdString(), "21");
    EXPECT_EQ(reading.deviceKind.toStdString(), "gspro")
        << "the connector labels its own readings; a second connector must not "
           "inherit the first one's token";
    ASSERT_TRUE(reading.ballSpeed.has_value());
    EXPECT_DOUBLE_EQ(*reading.ballSpeed, 147.5);
    EXPECT_GT(reading.readAtMs, 0);
    EXPECT_FALSE(reading.sourcePath.isEmpty());

    // ⚠ AND THE DEVICE HEARD BACK. A launch monitor that gets no acknowledgement
    // re-sends the shot after two seconds, so the same strike arrives twice —
    // which reads as a golfer hitting two balls, not as a bug.
    ASSERT_TRUE(waitFor([&] { return client.codes().size() >= 1; }));
    EXPECT_EQ(client.codes().first(), 200);

    EXPECT_EQ(l.m.state(), State::Ready);
    EXPECT_EQ(l.m.lastDeviceId().toStdString(), "TestMonitor");
}

TEST(GsProMonitor, AHeartbeatIsAnsweredButIsNotAShot)
{
    // ⚠ Every client sends these and GSPro answers all of them with the same 200 —
    // so the acknowledgement and the reading are two different decisions. A
    // connector that conflated them would attribute an empty reading to a swing
    // every few seconds, and one that answered nothing would look dead.
    Listener l;
    QSignalSpy spy(&l.m, &GsProMonitor::readingAvailable);

    FakeClient client;
    ASSERT_TRUE(client.connectTo(l.m.boundPort()));
    client.send(kHeartbeat);

    ASSERT_TRUE(waitFor([&] { return client.codes().size() >= 1; }));
    EXPECT_EQ(client.codes().first(), 200) << "answered, as GSPro answers it";
    EXPECT_EQ(spy.count(), 0) << "and not a reading";
}

TEST(GsProMonitor, AShotSplitAcrossThreeWritesIsStillOneReading)
{
    // TCP preserves nothing: a message arrives in whatever pieces the network
    // chose. The library's framer handles it — this asserts the CONNECTOR hands
    // the bytes over as they came rather than trying to help.
    Listener l;
    QSignalSpy spy(&l.m, &GsProMonitor::readingAvailable);

    FakeClient client;
    ASSERT_TRUE(client.connectTo(l.m.boundPort()));
    const QByteArray whole(kShot);
    const int third = whole.size() / 3;
    client.sendBytes(whole.left(third));
    QCoreApplication::processEvents();
    EXPECT_EQ(spy.count(), 0) << "no reading from part of a message";
    client.sendBytes(whole.mid(third, third));
    QCoreApplication::processEvents();
    EXPECT_EQ(spy.count(), 0);
    client.sendBytes(whole.mid(2 * third));

    ASSERT_TRUE(waitFor([&] { return spy.count() == 1; }));
    const auto reading = spy.at(0).at(0).value<LaunchMonitorReading>();
    EXPECT_DOUBLE_EQ(*reading.ballSpeed, 147.5);
}

TEST(GsProMonitor, TwoShotsInOneWriteBecomeTwoReadings)
{
    // The other half of the same property: a client may write two objects back to
    // back with nothing between them, and both are shots.
    Listener l;
    QSignalSpy spy(&l.m, &GsProMonitor::readingAvailable);

    FakeClient client;
    ASSERT_TRUE(client.connectTo(l.m.boundPort()));
    client.sendBytes(QByteArray(kShot) + QByteArray(kShot));

    ASSERT_TRUE(waitFor([&] { return spy.count() == 2; }));
    EXPECT_EQ(spy.count(), 2);
}

TEST(GsProMonitor, TwoClientsAtOnceAreServedIndependently)
{
    // An ordinary studio: a putting device beside a full-swing one.
    Listener l;
    QSignalSpy spy(&l.m, &GsProMonitor::readingAvailable);

    FakeClient a, b;
    ASSERT_TRUE(a.connectTo(l.m.boundPort()));
    ASSERT_TRUE(b.connectTo(l.m.boundPort()));
    ASSERT_TRUE(waitFor([&] { return l.m.connectionCount() == 2; }));

    a.send(kShot);
    b.send(kShot);
    ASSERT_TRUE(waitFor([&] { return spy.count() == 2; }));
    EXPECT_TRUE(waitFor([&] { return a.codes().size() >= 1 && b.codes().size() >= 1; }));
    EXPECT_EQ(a.codes().first(), 200);
    EXPECT_EQ(b.codes().first(), 200);
}

TEST(GsProMonitor, AClientThatVanishesMidMessageLeavesTheListenerHealthy)
{
    Listener l;
    QSignalSpy spy(&l.m, &GsProMonitor::readingAvailable);

    {
        FakeClient client;
        ASSERT_TRUE(client.connectTo(l.m.boundPort()));
        ASSERT_TRUE(waitFor([&] { return l.m.connectionCount() == 1; }));
        const QByteArray whole(kShot);
        client.sendBytes(whole.left(whole.size() / 2));
        QCoreApplication::processEvents();
        client.close();
    }
    ASSERT_TRUE(waitFor([&] { return l.m.connectionCount() == 0; }));
    EXPECT_EQ(spy.count(), 0) << "half a message is not a shot";
    EXPECT_NE(l.m.state(), State::Error) << "a client hanging up is not our failure";

    // And the listener still works: the next device connects and is served.
    FakeClient second;
    ASSERT_TRUE(second.connectTo(l.m.boundPort()));
    second.send(kShot);
    EXPECT_TRUE(waitFor([&] { return spy.count() == 1; }));
}

TEST(GsProMonitor, ABindConflictIsReportedRatherThanSwallowed)
{
    // ⚠ THE FAILURE A USER WILL ACTUALLY HIT, because 921 is GSPro's own port. A
    // listener that silently is not listening is indistinguishable from a device
    // nobody switched on, and the user would go looking at the device.
    Listener first;
    ASSERT_EQ(first.m.state(), State::Waiting);

    GsProMonitor second;
    second.setSourcePath(QStringLiteral("127.0.0.1:%1").arg(first.m.boundPort()));
    second.start();

    EXPECT_EQ(second.state(), State::Error);
    EXPECT_FALSE(second.errorText().isEmpty()) << "with the platform's own reason";
    EXPECT_EQ(second.boundPort(), 0);
    second.stop();

    // The listener that owns the port is unharmed.
    EXPECT_EQ(first.m.state(), State::Waiting);
    FakeClient client;
    EXPECT_TRUE(client.connectTo(first.m.boundPort()));
}

TEST(GsProMonitor, TheClubIsSentToTheDeviceRatherThanReadFromIt)
{
    // Club selection flows OUT. A device switches to putting mode because PinPoint
    // said the putter is in play — the shot message has no club field at all.
    Listener l;
    FakeClient client;
    ASSERT_TRUE(client.connectTo(l.m.boundPort()));
    ASSERT_TRUE(waitFor([&] { return l.m.connectionCount() == 1; }));

    l.m.setPlayerClub(QStringLiteral("PT"), /*leftHanded=*/false);
    ASSERT_TRUE(waitFor([&] { return !client.codes().isEmpty(); }));
    EXPECT_TRUE(client.codes().contains(201))
        << "the player-information message is how the club reaches the device";

    // A session start is what an [OSP]-style client arms on: no 202, no shots.
    l.m.setSessionActive(true);
    EXPECT_TRUE(waitFor([&] { return client.codes().contains(202); }));
}

TEST(GsProMonitor, GarbageEarnsARefusalAndKeepsTheConnection)
{
    // A hostile or confused client can at worst occupy its own connection being
    // told 501. Turning the panel red for it would say the listener had failed.
    Listener l;
    FakeClient client;
    ASSERT_TRUE(client.connectTo(l.m.boundPort()));
    client.sendBytes(QByteArray("this is not JSON at all"));

    ASSERT_TRUE(waitFor([&] { return !client.codes().isEmpty(); }));
    EXPECT_EQ(client.codes().first(), 501);
    EXPECT_NE(l.m.state(), State::Error);

    // And a good shot on the same connection afterwards is still served.
    QSignalSpy spy(&l.m, &GsProMonitor::readingAvailable);
    client.send(kShot);
    EXPECT_TRUE(waitFor([&] { return spy.count() == 1; }));
}

TEST(GsProMonitor, StopClosesEverythingAndStartRebinds)
{
    Listener l;
    const quint16 port = l.m.boundPort();
    FakeClient client;
    ASSERT_TRUE(client.connectTo(port));
    ASSERT_TRUE(waitFor([&] { return l.m.connectionCount() == 1; }));

    l.m.stop();
    EXPECT_EQ(l.m.state(), State::Disabled);
    EXPECT_EQ(l.m.connectionCount(), 0);
    EXPECT_EQ(l.m.boundPort(), 0);

    // start() is idempotent and re-binds — the controller calls it again whenever
    // the setting changes.
    l.m.start();
    EXPECT_EQ(l.m.state(), State::Waiting);
    EXPECT_GT(l.m.boundPort(), 0);
    l.m.start();
    EXPECT_EQ(l.m.state(), State::Waiting) << "start() twice is not an error";
}

TEST(GsProMonitor, ANonsenseAddressIsRefusedWithAReason)
{
    GsProMonitor m;
    m.setSourcePath(QStringLiteral("not-an-address:921"));
    EXPECT_EQ(m.state(), State::Error);
    EXPECT_FALSE(m.errorText().isEmpty());
}

TEST(GsProMonitor, ABarePortMeansThePortOnEveryInterface)
{
    // The settings field is text a user types. A bare port is the common case and
    // must not be read as a host name.
    GsProMonitor m;
    m.setSourcePath(QStringLiteral("0"));      // 0 → the kernel picks
    m.start();
    ASSERT_EQ(m.state(), State::Waiting) << m.errorText().toStdString();
    EXPECT_GT(m.boundPort(), 0);
    EXPECT_TRUE(m.sourceDescription().contains(QStringLiteral("all interfaces")));
    m.stop();
}

TEST(GsProMonitor, EnumeratesEachDeviceAsItConnectsAndIdentifiesItself)
{
    // ⚠ THE QUESTION A FOLDER CANNOT ASK. The GCQuad path is configured and that
    // is the end of it; here a device CONNECTS, says who it is, and can go away
    // again — so "which launch monitors are on the link right now" has a changing
    // answer, and the settings panel shows it beside the cameras and the IMUs.
    Listener l;
    QSignalSpy changed(&l.m, &GsProMonitor::clientsChanged);
    EXPECT_TRUE(l.m.clients().isEmpty());

    FakeClient a;
    ASSERT_TRUE(a.connectTo(l.m.boundPort()));
    ASSERT_TRUE(waitFor([&] { return l.m.clients().size() == 1; }));

    // ⚠ CONNECTED IS NOT IDENTIFIED. The protocol has no handshake — a client may
    // sit there for minutes before it says anything — so a socket with no name is
    // an ordinary state the panel has to draw, not a fault.
    {
        const auto clients = l.m.clients();
        EXPECT_FALSE(clients.at(0).identified);
        EXPECT_TRUE(clients.at(0).deviceId.isEmpty());
        EXPECT_FALSE(clients.at(0).peer.isEmpty()) << "the socket's address is known at once";
        EXPECT_EQ(clients.at(0).shots, 0);
        EXPECT_GT(clients.at(0).connectedAtMs, 0);
        EXPECT_EQ(clients.at(0).lastMessageAtMs, 0) << "it has never spoken";
    }

    a.send(kShot);
    ASSERT_TRUE(waitFor([&] { return l.m.clients().value(0).identified; }));
    {
        const auto clients = l.m.clients();
        ASSERT_EQ(clients.size(), 1);
        EXPECT_EQ(clients.at(0).deviceId.toStdString(), "TestMonitor");
        EXPECT_EQ(clients.at(0).shots, 1);
        EXPECT_EQ(clients.at(0).messages, 1);
        EXPECT_GT(clients.at(0).lastMessageAtMs, 0);
    }

    // A second device, and the two are told apart.
    FakeClient b;
    ASSERT_TRUE(b.connectTo(l.m.boundPort()));
    ASSERT_TRUE(waitFor([&] { return l.m.clients().size() == 2; }));
    b.send(kHeartbeat);
    ASSERT_TRUE(waitFor([&] {
        const auto c = l.m.clients();
        return c.size() == 2 && c.at(0).identified && c.at(1).identified;
    }));
    {
        const auto clients = l.m.clients();
        EXPECT_NE(clients.at(0).conn, clients.at(1).conn);
        // ⚠ A HEARTBEAT COUNTS AS A MESSAGE AND NOT AS A SHOT, which is what makes
        // "1 shot" on the panel mean a ball was struck.
        const auto &second = clients.at(1);
        EXPECT_EQ(second.messages, 1);
        EXPECT_EQ(second.shots, 0);
    }

    // And it disappears when it goes away.
    b.close();
    ASSERT_TRUE(waitFor([&] { return l.m.clients().size() == 1; }));
    EXPECT_EQ(l.m.clients().at(0).deviceId.toStdString(), "TestMonitor");
    EXPECT_GT(changed.count(), 0) << "the panel is told rather than having to poll";
}

TEST(GsProMonitor, TheDeviceListIsEmptyWhenNothingIsListening)
{
    // A connector that has not been started must not report devices from a
    // previous run, and must not crash when asked.
    GsProMonitor m;
    EXPECT_TRUE(m.clients().isEmpty());
    EXPECT_EQ(m.connectionCount(), 0);
    EXPECT_EQ(m.boundPort(), 0);
}

TEST(GsProMonitor, StoppingHandsThePortBackToWhoeverWantsIt)
{
    // ⚠ THE WHOLE POINT OF THE ENABLE SWITCH. A user who wants to run GSPro itself
    // has to give it port 921, and the panel's switch is how — so "stopped" has to
    // mean the port is genuinely released, not merely that we stopped reading it.
    // Nothing else in the suite would notice a listener that lingered.
    quint16 port = 0;
    {
        GsProMonitor first;
        first.setSourcePath(QStringLiteral("127.0.0.1:0"));
        first.start();
        ASSERT_EQ(first.state(), State::Waiting) << first.errorText().toStdString();
        port = first.boundPort();
        ASSERT_GT(port, 0);

        // While it is up, nobody else can have the port.
        GsProMonitor rival;
        rival.setSourcePath(QStringLiteral("127.0.0.1:%1").arg(port));
        rival.start();
        EXPECT_EQ(rival.state(), State::Error);
        rival.stop();

        first.stop();
    }

    // And once it is down, they can.
    GsProMonitor successor;
    successor.setSourcePath(QStringLiteral("127.0.0.1:%1").arg(port));
    successor.start();
    EXPECT_EQ(successor.state(), State::Waiting)
        << "the port was not released: " << successor.errorText().toStdString();
    successor.stop();
}

TEST(GsProSourcePath, TurnsTheTwoSettingsIntoOneAddress)
{
    using pinpoint::lm::gsProSourcePath;

    EXPECT_EQ(gsProSourcePath(921, QStringLiteral("any")).toStdString(), "0.0.0.0:921");
    EXPECT_EQ(gsProSourcePath(921, QStringLiteral("loopback")).toStdString(), "127.0.0.1:921");
    EXPECT_EQ(gsProSourcePath(9210, QStringLiteral("any")).toStdString(), "0.0.0.0:9210");

    // ⚠ ANYTHING THAT IS NOT "loopback" MEANS ALL INTERFACES. A value from a newer
    // version, an empty setting or a typo must not silently produce the
    // restrictive binding — which on macOS is also the one that cannot bind 921.
    EXPECT_EQ(gsProSourcePath(921, QString()).toStdString(), "0.0.0.0:921");
    EXPECT_EQ(gsProSourcePath(921, QStringLiteral("LOOPBACK")).toStdString(), "0.0.0.0:921");
    EXPECT_EQ(gsProSourcePath(921, QStringLiteral("wifi-only")).toStdString(), "0.0.0.0:921");

    // Clamped rather than refused: a listener on a sane port beats one that would
    // not start because a digit was typed twice.
    EXPECT_EQ(gsProSourcePath(0, QStringLiteral("any")).toStdString(), "0.0.0.0:1");
    EXPECT_EQ(gsProSourcePath(999999, QStringLiteral("any")).toStdString(), "0.0.0.0:65535");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
