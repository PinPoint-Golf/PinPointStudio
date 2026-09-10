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

#include "gspro_monitor.h"

#include "gspro_reading.h"

#include "pp_debug.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QTcpServer>
#include <QTcpSocket>

namespace pinpoint::lm {

namespace {

// ⚠ MONOTONIC, AND THE ONLY CLOCK THE LIBRARY EVER SEES. A wall clock stepped by
// NTP mid-session produces an ordering that is a fiction — and the library's write
// spacing and idle alarm are both differences of these values. The reading's
// readAtMs is a wall clock separately, because that one is a timestamp a human
// reads rather than an interval anything measures.
qint64 monotonicUs()
{
    static QElapsedTimer clock;
    if (!clock.isValid())
        clock.start();
    return clock.nsecsElapsed() / 1000;
}

// One line for the log, with the peer address and the DeviceID redacted — the
// library's own formatter does the redacting, so this cannot drift from what the
// library considers an identifier.
QString formatEvent(const gsp_event &ev)
{
    char line[512];
    gsp_event_format(&ev, line, sizeof(line), /*include_identifiers=*/false);
    return QString::fromUtf8(line);
}

} // namespace

QString gsProSourcePath(int port, const QString &interfaceKey)
{
    // ⚠ "loopback" IS THE ONLY THING THAT MEANS LOOPBACK. Anything else — an empty
    // setting, a value from a newer version, a typo — means all interfaces, which
    // is the working default rather than the safe-looking one that cannot bind 921
    // on a Mac at all (see the note at the top of this file).
    const QString host = (interfaceKey == QLatin1String("loopback"))
                             ? QStringLiteral("127.0.0.1")
                             : QStringLiteral("0.0.0.0");
    // A port outside the range is clamped rather than refused: the setting is a
    // text field, and a listener on the default port is more useful than one that
    // refused to start because somebody typed a digit too many.
    const int p = qBound(1, port, 65535);
    return QStringLiteral("%1:%2").arg(host).arg(p);
}

GsProMonitor::GsProMonitor(QObject *parent) : LaunchMonitorBase(parent)
{
    // ⚠ SINGLE SHOT, AND RE-ARMED FROM next_due_us AFTER EVERY CALL IN. The
    // protocol has no deadline of its own, so the ordinary answer is NEVER and this
    // timer never runs — an idle GSPro link costs no wakeups at all. Only the write
    // spacing arms it, and only for milliseconds.
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &GsProMonitor::onTimer);
}

GsProMonitor::~GsProMonitor()
{
    stop();
}

QString GsProMonitor::sourceDescription() const
{
    if (!m_running)
        return m_configured.isEmpty() ? QString() : m_configured;

    const quint16 port = boundPort();
    const QString where = (m_address == QHostAddress::Any)
                              ? QStringLiteral("all interfaces")
                              : m_address.toString();
    if (m_sockets.isEmpty())
        return tr("%1 port %2 — waiting for a launch monitor").arg(where).arg(port);
    return tr("%1 port %2 — %3 connected").arg(where).arg(port).arg(m_sockets.size());
}

void GsProMonitor::setSourcePath(const QString &path)
{
    m_configured = path.trimmed();

    // "0.0.0.0:921" | "921" | "192.168.1.5" | "". Parsed leniently because this is
    // a text field in a settings panel: a user who types a bare port means the port,
    // and one who types a bare address means the address on the usual port.
    QHostAddress address = QHostAddress::Any;
    int port = -1;                      // -1 → not specified; 0 → ephemeral

    if (!m_configured.isEmpty()) {
        const int colon = m_configured.lastIndexOf(QLatin1Char(':'));
        QString hostPart = m_configured;
        QString portPart;
        // ⚠ lastIndexOf, and only when what follows is numeric: an IPv6 literal is
        // full of colons and "::1" is a legitimate thing to type.
        if (colon >= 0) {
            bool numeric = false;
            const QString tail = m_configured.mid(colon + 1);
            tail.toUShort(&numeric);
            if (numeric && !tail.isEmpty()) {
                hostPart = m_configured.left(colon);
                portPart = tail;
            }
        }
        if (portPart.isEmpty() && !hostPart.contains(QLatin1Char('.'))
            && !hostPart.contains(QLatin1Char(':'))) {
            bool numeric = false;
            hostPart.toUShort(&numeric);
            if (numeric) {          // a bare port
                portPart = hostPart;
                hostPart.clear();
            }
        }
        if (!portPart.isEmpty())
            port = int(portPart.toUShort());
        if (!hostPart.isEmpty() && !address.setAddress(hostPart)) {
            // Not an address we can bind. Say so rather than silently listening
            // somewhere else — "it is not receiving" has too many causes already.
            setState(State::Error, tr("Not an address: %1").arg(hostPart));
            return;
        }
    }

    const bool changed = (address != m_address) || (port != m_port);
    m_address = address;
    m_port    = port;
    if (changed && m_running) {
        stop();
        start();
    }
}

void GsProMonitor::start()
{
    // Idempotent, as the base requires: the controller calls start() again whenever
    // the configured source changes.
    if (m_running)
        return;

    gsp_server_config cfg = gsp_server_config_default();
    // ⚠ Hold a 200 apart from a 201 rather than letting them share a segment. Six
    // clients parse one read as one message, and PiTrac's receive thread DIES on
    // {200}{201} — the library will space them for us if we ask, and 20 ms costs
    // nothing a golfer could notice.
    cfg.policy.write_spacing_us = 20000;
    // Every device in the studio, plus a connector reconnecting before its old
    // socket has closed. Permissive on purpose.
    cfg.max_connections = 8;

    if (gsp_server_create(&cfg, &m_gs) < GSP_OK || !m_gs) {
        setState(State::Error, tr("Cannot start the GSPro listener"));
        return;
    }

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &GsProMonitor::onNewConnection);

    const quint16 port = (m_port >= 0) ? quint16(m_port) : quint16(GSP_DEFAULT_PORT);
    if (!m_server->listen(m_address, port)) {
        // ⚠ THE ONE FAILURE A USER WILL ACTUALLY HIT, and 921 is why: GSPro Connect
        // itself listens there, on the one machine where somebody would run both.
        // The reason is the platform's own words, and the hint is what turns
        // "Address already in use" into something actionable.
        QString why = m_server->errorString();
        // ⚠ THREE FAILURES, THREE DIFFERENT FIXES, and a host that reported them
        // all as "cannot bind" would send the user to the wrong one every time.
        if (m_server->serverError() == QAbstractSocket::SocketAccessError && port < 1024) {
#if defined(Q_OS_MACOS)
            // Measured on macOS 27: 0.0.0.0:921 binds as an ordinary user and
            // 127.0.0.1:921 does not. So the RESTRICTIVE choice is the one that
            // fails, and the first thing to offer is the permissive one.
            if (m_address != QHostAddress::Any) {
                why += tr(" — on macOS a port below 1024 can be bound on ALL INTERFACES "
                          "but not on one address. Set the interface to \"All\", or "
                          "choose a port above 1024 here and in the launch monitor's app.");
            } else {
                why += tr(" — ports below 1024 need privileges here. Choose a port above "
                          "1024, in this panel and in the launch monitor's app.");
            }
#elif defined(Q_OS_LINUX)
            why += tr(" — ports below 1024 are privileged on Linux. The preferred fix is "
                      "to move the boundary rather than to grant this program anything:\n"
                      "    sudo sysctl net.ipv4.ip_unprivileged_port_start=921\n"
                      "Otherwise choose a port above 1024, here and in the launch "
                      "monitor's app.");
#else
            why += tr(" — ports below 1024 need privileges here. Choose a port above "
                      "1024, in this panel and in the launch monitor's app.");
#endif
        } else if (m_server->serverError() == QAbstractSocket::AddressInUseError
                   && port == quint16(GSP_DEFAULT_PORT)) {
            why += tr(" — GSPro Connect itself listens on %1. Move one of them: GSPro to "
                      "%2 with <OpenAPIUseAltPort>true</OpenAPIUseAltPort>, or PinPoint to "
                      "another port.").arg(port).arg(int(GSP_ALT_PORT));
        } else if (m_server->serverError() == QAbstractSocket::AddressInUseError) {
            why += tr(" — something else on this machine is already listening on %1.")
                       .arg(port);
        }
        ppWarn() << "LaunchMonitor: GSPro listener cannot bind" << port << ":" << why;
        closeServer();
        setState(State::Error, why);
        return;
    }

    m_running = true;
    ppInfo() << "LaunchMonitor: GSPro listener on" << m_address.toString() << "port"
             << m_server->serverPort();
    setState(State::Waiting, QString());
    applyPlayerInfo();
    pump();
}

void GsProMonitor::stop()
{
    if (!m_running && !m_server && !m_gs)
        return;

    m_timer.stop();
    closeServer();

    if (m_gs) {
        // Seals the queues and reports every open connection closed, so one last
        // drain leaves a complete log rather than a session that just stops.
        gsp_server_close(m_gs);
        gsp_event ev[16];
        size_t n;
        while ((n = gsp_server_poll_events(m_gs, ev, 16)) > 0) {
            for (size_t i = 0; i < n; ++i)
                ppDebug() << "LaunchMonitor:" << formatEvent(ev[i]);
        }
        gsp_server_destroy(m_gs);
        m_gs = nullptr;
    }
    m_running = false;
    setState(State::Disabled, QString());
}

void GsProMonitor::closeServer()
{
    for (auto it = m_sockets.begin(); it != m_sockets.end(); ++it) {
        QTcpSocket *s = it.value();
        if (!s)
            continue;
        s->disconnect(this);        // no onDisconnected while we are tearing down
        s->abort();
        s->deleteLater();
    }
    m_sockets.clear();
    m_ids.clear();

    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
}

void GsProMonitor::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *sock = m_server->nextPendingConnection();
        if (!sock)
            break;

        // ⚠ TCP_NODELAY, or Nagle holds a 60-byte reply for up to 40 ms waiting for
        // company — on the one path where a Rapsodo is counting to two seconds
        // before it re-sends the shot as though the first had been lost.
        sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);

        const quint32 conn = m_nextConn++;
        const QString peer = QStringLiteral("%1:%2")
                                 .arg(sock->peerAddress().toString())
                                 .arg(sock->peerPort());

        if (gsp_server_on_connection_opened(m_gs, conn, peer.toUtf8().constData(),
                                            monotonicUs()) < GSP_OK) {
            // At the limit. Nothing we send can tell the client why — the protocol
            // has no message for it — so closing the socket is the whole vocabulary.
            ppWarn() << "LaunchMonitor: GSPro connection refused (at the limit)";
            sock->abort();
            sock->deleteLater();
            continue;
        }

        m_sockets.insert(conn, sock);
        m_ids.insert(sock, conn);
        connect(sock, &QTcpSocket::readyRead,    this, &GsProMonitor::onReadyRead);
        connect(sock, &QTcpSocket::disconnected, this, &GsProMonitor::onDisconnected);
        emit clientsChanged();
    }
    pump();
}

void GsProMonitor::onReadyRead()
{
    auto *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock || !m_gs)
        return;
    const quint32 conn = m_ids.value(sock, 0);
    if (conn == 0)
        return;

    // ⚠ EXACTLY AS IT CAME. Not split on newlines — one client's messages contain
    // them — not parsed first, and not held back waiting for "a whole message",
    // which a host cannot recognise. TCP preserves nothing; the library's framer is
    // what finds the objects, and it is tested on every fragmentation there is.
    const QByteArray data = sock->readAll();
    gsp_status st = gsp_server_on_bytes(m_gs, conn,
                                        reinterpret_cast<const uint8_t *>(data.constData()),
                                        size_t(data.size()), monotonicUs());
    int guard = 0;
    while (st == GSP_ERR_QUEUE_FULL && guard++ < 8) {
        // The write ring filled and the message was NOT consumed — the library is
        // holding it. Drain and offer zero bytes to resume.
        pump();
        st = gsp_server_on_bytes(m_gs, conn, nullptr, 0, monotonicUs());
    }
    pump();
}

void GsProMonitor::onDisconnected()
{
    auto *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock)
        return;
    const quint32 conn = m_ids.take(sock);
    if (conn != 0) {
        m_sockets.remove(conn);
        if (m_gs)
            (void)gsp_server_on_connection_closed(m_gs, conn, GSP_CLOSE_REMOTE_CLOSED,
                                                  monotonicUs());
    }
    sock->deleteLater();
    emit clientsChanged();
    pump();
}

void GsProMonitor::onTimer()
{
    if (!m_gs)
        return;
    gsp_server_tick(m_gs, monotonicUs());
    pump();
}

void GsProMonitor::pump()
{
    if (!m_gs)
        return;

    // ⚠ WRITES BEFORE EVENTS, because a full event ring must cost a log line and
    // never an acknowledgement: a launch monitor that does not hear one re-sends
    // the shot and we get it twice.
    gsp_write_request w[8];
    size_t n;
    while ((n = gsp_server_poll_writes(m_gs, w, 8)) > 0) {
        for (size_t i = 0; i < n; ++i) {
            QTcpSocket *sock = m_sockets.value(w[i].conn, nullptr);
            if (!sock)
                continue;           // it went away between the queue and the drain
            // ONE write PER REQUEST, never concatenated — see write_spacing_us above.
            sock->write(reinterpret_cast<const char *>(w[i].data), qint64(w[i].length));
            sock->flush();
        }
    }

    gsp_event ev[16];
    while ((n = gsp_server_poll_events(m_gs, ev, 16)) > 0) {
        for (size_t i = 0; i < n; ++i) {
            const gsp_event &e = ev[i];
            switch (e.type) {
            case GSP_EV_SHOT: {
                QString why;
                auto reading = readingFromGsProMessage(e.u.message, &why);
                if (!reading) {
                    ppWarn() << "LaunchMonitor: GSPro shot ignored —" << why;
                    break;
                }
                reading->deviceKind = kindKey(kind());
                reading->sourcePath = sourceDescription();
                reading->readAtMs   = QDateTime::currentMSecsSinceEpoch();
                setState(State::Ready, QString());
                emit readingAvailable(*reading);
                emit clientsChanged();   // its shot count moved
                break;
            }
            case GSP_EV_CLIENT_IDENTIFIED:
                // ⚠ THE MOMENT A CONNECTION BECOMES A DEVICE. Until its first
                // message a client is an anonymous socket — the protocol has no
                // handshake and a client may say nothing for minutes — so this is
                // where the panel's list gains a name to show.
                m_lastDeviceId = QString::fromUtf8(e.u.connection.info.device_id);
                setState(State::Ready, QString());
                ppInfo() << "LaunchMonitor:" << formatEvent(e);
                emit clientsChanged();
                break;
            case GSP_EV_CONNECTION_OPENED:
            case GSP_EV_CONNECTION_CLOSED:
                ppInfo() << "LaunchMonitor:" << formatEvent(e);
                if (m_sockets.isEmpty() && state() != State::Error)
                    setState(State::Waiting, QString());
                break;
            case GSP_EV_PROTOCOL_ERROR:
            case GSP_EV_WARNING:
                // ⚠ NOT an Error state. A device sending rubbish on one connection
                // has not broken the listener, and the library has already answered
                // it 501; turning the panel red would say the opposite.
                ppWarn() << "LaunchMonitor:" << formatEvent(e);
                break;
            case GSP_EV_CLOSE_REQUESTED: {
                // The library cannot close anything — it asks, and this is the host
                // acting on it.
                QTcpSocket *sock = m_sockets.value(e.conn, nullptr);
                ppWarn() << "LaunchMonitor:" << formatEvent(e);
                if (sock)
                    sock->disconnectFromHost();
                break;
            }
            default:
                ppDebug() << "LaunchMonitor:" << formatEvent(e);
                break;
            }
        }
    }

    // ⚠ RE-READ AFTER EVERY CALL INCLUDING THE POLLS: a write held back by the
    // spacing above becomes due here and nowhere else.
    const gsp_time_us due = gsp_server_next_due_us(m_gs);
    if (due == GSP_TIME_NEVER) {
        m_timer.stop();
    } else {
        const qint64 ms = (due - monotonicUs() + 999) / 1000;
        m_timer.start(int(ms > 0 ? ms : 0));
    }
}

void GsProMonitor::setPlayerClub(const QString &club, bool leftHanded)
{
    if (club == m_club && leftHanded == m_leftHanded)
        return;
    m_club       = club;
    m_leftHanded = leftHanded;
    m_playerSet  = true;
    applyPlayerInfo();
    pump();
}

void GsProMonitor::applyPlayerInfo()
{
    if (!m_gs || !m_playerSet)
        return;

    gsp_player_info info;
    memset(&info, 0, sizeof(info));
    info.handed = quint8(m_leftHanded ? GSP_HANDED_LEFT : GSP_HANDED_RIGHT);
    // ⚠ An unrecognised code clears the club rather than inventing one: the JSON
    // omits an UNKNOWN member entirely, which is what the protocol asks for, and a
    // device told "the club is nonsense" would have no way to say so.
    info.club = quint8(m_club.isEmpty() ? GSP_CLUB_UNKNOWN
                                        : gsp_club_parse(m_club.toUtf8().constData()));
    // DistanceToTarget has no PinPoint meaning today and is left unset. ⚠ One
    // client arms its device only on a NON-ZERO value, so if a device ever sits
    // there connected and silent, this is the first thing to try.
    info.has_distance = 0;

    (void)gsp_server_set_player(m_gs, &info, monotonicUs());
}

void GsProMonitor::setSessionActive(bool active)
{
    if (active == m_sessionActive)
        return;
    m_sessionActive = active;
    if (!m_gs)
        return;
    (void)gsp_server_set_session_state(m_gs,
                                       active ? GSP_SESSION_ACTIVE : GSP_SESSION_ENDED,
                                       monotonicUs());
    pump();
}

QList<GsProMonitor::Client> GsProMonitor::clients() const
{
    QList<Client> out;
    if (!m_gs)
        return out;

    // ⚠ READ FROM THE LIBRARY, NOT FROM A TALLY OF OUR OWN. It is already counting
    // messages, shots and protocol errors per connection, and it knows which
    // DeviceID arrived first — a second count kept here would be a second thing to
    // get wrong, and the two would disagree exactly when somebody was debugging.
    gsp_conn_id ids[16];
    const size_t n = gsp_server_connection_ids(m_gs, ids, 16);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const gsp_time_us nowUs = monotonicUs();

    for (size_t i = 0; i < n && i < 16; ++i) {
        gsp_connection_info info;
        if (gsp_server_connection_info(m_gs, ids[i], &info) < GSP_OK)
            continue;
        Client c;
        c.conn       = info.conn;
        c.deviceId   = QString::fromUtf8(info.device_id);
        c.peer       = QString::fromUtf8(info.peer);
        c.messages   = int(info.messages);
        c.shots      = int(info.shots);
        c.identified = info.identified != 0;
        // The library's clock is monotonic microseconds; the panel wants wall
        // clock. Converted here, once, rather than storing a second timestamp.
        c.connectedAtMs = nowMs - (nowUs - info.opened_us) / 1000;
        c.lastMessageAtMs = (info.last_message_us == GSP_TIME_UNKNOWN)
                                ? 0
                                : nowMs - (nowUs - info.last_message_us) / 1000;
        out.append(c);
    }
    return out;
}

quint16 GsProMonitor::boundPort() const
{
    return m_server ? m_server->serverPort() : quint16(0);
}

int GsProMonitor::connectionCount() const
{
    return m_sockets.size();
}

} // namespace pinpoint::lm
