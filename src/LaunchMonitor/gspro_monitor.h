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

#include "launch_monitor_base.h"

#include <gspro/gspro.h>

#include <QHash>
#include <QHostAddress>
#include <QString>
#include <QTimer>

class QTcpServer;
class QTcpSocket;

namespace pinpoint::lm {

// Listens for launch monitors speaking GSPro Open Connect v1 and turns their shots
// into readings.
//
// WHY ONE CONNECTOR REACHES MANY DEVICES. ⚠ Almost none of them speak this
// protocol themselves — somebody wrote a BRIDGE that speaks it on the device's
// behalf, and libgspro's survey found seventeen: five independent ones for the
// Garmin R10, two for the Rapsodo MLM2PRO, one for a SkyTrak+ via OpenSkyPlus,
// one off a Foresight GC2's serial feed, an SLX proxy, and two DIY monitors that
// are native clients because they were built that way (PiTrac, OpenFlight). All
// of them are CLIENTS of GSPro; PinPoint plays the server they connect to, and
// the bridge does not know or care that it is not GSPro. So this is one connector
// rather than one per device.
//
// ⚠ AND IT DOES NOT REACH UNEEKOR, BUSHNELL OR FORESIGHT. Their connectors are
// closed and speak their vendors' own simulators; none ships an Open Connect
// client. A Uneekor is reachable only through a third-party bridge that watches
// Uneekor VIEW's shot folder — the same shape as the GCQuad connector beside this
// one, which is exactly why that connector still has to exist. Do not let this
// class's existence suggest the GCQuad path is redundant.
//
// THE PROTOCOL LIVES IN libgspro AND THE SOCKET LIVES HERE. The library owns no
// socket, thread, timer, clock or file: it takes bytes and a `now`, and hands back
// replies to write and events to handle. This class is the whole of the other
// side — a QTcpServer, a map from the connection ids WE choose to the sockets they
// name, and a QTimer for the one deadline the library can ask for. Everything
// below the JSON is the library's and is tested there against sixteen real
// clients' byte patterns; everything about sockets is here.
//
// ⚠ setSourcePath() CARRIES AN ADDRESS, NOT A PATH, and the base class already
// anticipated it: "every connector we can foresee is either a watched path or a
// polled endpoint". It accepts "0.0.0.0:921", a bare port ("921"), or a bare
// address ("192.168.1.5"), and an empty string means the defaults — all
// interfaces on 921.
//
// ⚠ ALL INTERFACES BY DEFAULT, NOT LOOPBACK. The vendor documents 127.0.0.1
// because it assumes the client runs on the GSPro PC; the useful case for a
// coaching studio is the opposite — a Rapsodo bridge on a phone, a PiTrac on the
// network. On Windows an inbound firewall rule for the PinPoint executable is
// needed or every device on another machine fails with "connection refused" and
// nothing here can tell the difference between that and a device nobody switched
// on. Loopback stays available as the restrictive setting.
//
// ⚠ PORT 921 IS BELOW 1024, AND WHAT THAT COSTS DEPENDS ON THE PLATFORM AND ON
// THE ADDRESS. Measured on macOS 27 as an ordinary user: 0.0.0.0:921 BINDS, while
// 127.0.0.1:921 and a specific LAN address are refused with EACCES — so the
// permissive all-interfaces bind works and the restrictive loopback-only one does
// not, which is the opposite of the rule most people carry. Windows has no
// privileged-port rule at all. On Linux the boundary is a setting:
//
//     sudo sysctl net.ipv4.ip_unprivileged_port_start=921
//
// which is PinPoint's preferred answer there — better than granting the binary
// CAP_NET_BIND_SERVICE, which travels with it through updates nobody thought
// about, and far better than running an unauthenticated listener as root. Moving
// the port above 1024 on both sides is the fallback that needs no administrator
// anywhere; every client examined has a port setting.
//
// ⚠ AND THE PORT IS OFTEN ALREADY HELD — by GSPro Connect itself, on the one
// machine where somebody would run both. All three failures are reported as Error
// with the platform's own reason and a hint naming the fix for THAT failure: a
// listener that silently is not listening is indistinguishable from a device
// nobody switched on, and the user would go looking at the device.
// The two settings a user edits — a port and "all interfaces" or "this machine
// only" — as the one string setSourcePath() takes.
//
// ⚠ A FREE FUNCTION BECAUSE THE DECISION IS TESTABLE AND THE GATHERING IS NOT.
// The controller reads AppSettings, which needs the app; what it does with the two
// values is four lines that can be got wrong (a bare port read as a host, loopback
// silently becoming all interfaces) and those four lines are checked here. Same
// split as decideStandalone() beside its controller.
QString gsProSourcePath(int port, const QString &interfaceKey);

class GsProMonitor : public LaunchMonitorBase
{
    Q_OBJECT

public:
    explicit GsProMonitor(QObject *parent = nullptr);
    ~GsProMonitor() override;

    Kind    kind() const override { return Kind::GsPro; }
    QString sourceDescription() const override;

    void start() override;
    void stop() override;
    // "0.0.0.0:921" | "921" | "192.168.1.5" | "" (defaults). Applied on the next
    // start(); calling it while running rebinds, which is what the controller does
    // when the setting changes.
    void setSourcePath(const QString &path) override;
    // ⚠ IGNORED, DELIBERATELY. There is nothing to poll: the protocol is
    // event-driven and an idle connection has no deadline of its own. The base
    // declares it for the connectors that do poll.
    void setPollIntervalMs(int ms) override { Q_UNUSED(ms) }

    // ── The other direction ─────────────────────────────────────────────────
    // Club selection flows OUT, not in. The shot message carries no club at all;
    // GSPro tells the launch monitor which club is in play so a device can switch
    // to putting mode, and a client that arms on a non-zero DistanceToTarget or on
    // "GSPro ready" needs to hear it. So PinPoint's own club selection — the thing
    // that resolves a normative corridor — becomes the 201 this sends.
    //
    // `club` is a GSPro club code ("PT", "DR", "I7", …); anything unrecognised
    // clears it rather than inventing one. Safe to call before start() and safe to
    // call repeatedly: an unchanged value sends nothing.
    void setPlayerClub(const QString &club, bool leftHanded);
    // A PinPoint session started or stopped. Sends 202 "GSPro ready" / 203 "GSPro
    // round ended" — an [OSP]-style client does not arm its device until it has
    // seen the 202, so a connector that never sent one would sit there looking
    // connected and receive nothing.
    void setSessionActive(bool active);

    // ── Introspection, for the settings panel and the tests ─────────────────
    quint16 boundPort() const;              // 0 when not listening
    int     connectionCount() const;

    // One connected launch monitor, as the library knows it.
    //
    // ⚠ THIS IS WHY THE GSPro LINK IS ENUMERATED AND THE GCQUAD PATH IS NOT.
    // A folder is configured; a launch monitor CONNECTS, says who it is, and can
    // go away again — so "which devices are on the link right now" is a real
    // question with a changing answer, and the panel shows it beside the cameras
    // and the IMUs rather than showing only what was configured.
    struct Client {
        quint32 conn = 0;       // the id this connector chose for the socket
        QString deviceId;       // what the client called itself; empty until it speaks
        QString peer;           // "192.168.1.40:51022" — ⚠ personal data, design §9.2
        int     messages = 0;   // every well-formed object, heartbeats included
        int     shots    = 0;
        bool    identified = false;
        qint64  connectedAtMs = 0;
        qint64  lastMessageAtMs = 0;   // 0 when it has never spoken
    };
    QList<Client> clients() const;
    // The last DeviceID any client identified itself with, empty until one does.
    // ⚠ Personal data in principle — a user-configured connector lets the user
    // type anything, and one device puts its serial here — so it reaches the log
    // through the library's redacting formatter, never raw.
    QString lastDeviceId() const { return m_lastDeviceId; }

signals:
    // A device connected, identified itself, or went away. The controller
    // re-reads clients() on this; it is deliberately not carried in the signal,
    // because a list that arrives by value invites a stale copy being kept.
    void clientsChanged();

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();
    void onTimer();

private:
    // Drain the library's three queues and act on them: write what it composed,
    // turn shots into readings, follow its state. Called after every call in.
    void pump();
    void closeServer();
    void applyPlayerInfo();

    QTcpServer *m_server = nullptr;
    gsp_server *m_gs     = nullptr;
    QTimer      m_timer;

    // The connection id is OURS to choose — any non-zero value we can map back to
    // a socket. A counter, and the two maps that make it reversible.
    QHash<quint32, QTcpSocket *> m_sockets;
    QHash<QTcpSocket *, quint32> m_ids;
    quint32     m_nextConn = 1;

    QHostAddress m_address  = QHostAddress::Any;
    // ⚠ -1 IS "NOT SPECIFIED", NOT ZERO. Zero is a legitimate value a caller can
    // mean — bind an ephemeral port, which is what every test does and what a
    // second listener beside a real GSPro would want — so it cannot double as the
    // sentinel for "use the protocol default".
    int          m_port     = -1;
    QString      m_configured;        // exactly what the setting said, for the panel
    bool         m_running  = false;

    QString      m_club;
    bool         m_leftHanded = false;
    // ⚠ NOTHING IS ANNOUNCED UNTIL PINPOINT SAYS SOMETHING. Until the controller
    // calls setPlayerClub, this connector knows neither the club nor the golfer's
    // handedness — so it sends no player information at all rather than
    // announcing a right-hander with no club to every device that connects. Same
    // rule as the reading struct's: never invent a value you did not receive.
    bool         m_playerSet  = false;
    bool         m_sessionActive = false;
    QString      m_lastDeviceId;
};

} // namespace pinpoint::lm
