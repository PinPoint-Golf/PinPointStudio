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

#include "smtp_client.h"

#include "../../Core/pp_debug.h"

#include <QDateTime>
#include <QHostAddress>
#include <QHostInfo>
#include <QRegularExpression>
#include <QSslCipher>
#include <QSslConfiguration>
#include <QSslSocket>

#include <algorithm>

namespace pinpoint::report {

namespace {

constexpr qint64 kChunk     = 64 * 1024;    // handed to the socket at a time
constexpr qint64 kHighWater = 256 * 1024;   // no more while this much is still unwritten
constexpr int    kQuitWaitMs = 5000;        // the message is already accepted; QUIT's 221 is a courtesy

void log(const QString &s) { ppInfo() << qPrintable(QStringLiteral("[SMTP] ") + s); }
void warn(const QString &s) { ppWarn() << qPrintable(QStringLiteral("[SMTP] ") + s); }

// "…@gmail.com": enough to read a log by, without a golfer's address in it.
QString masked(const QString &address)
{
    const qsizetype at = address.lastIndexOf(QLatin1Char('@'));
    return at < 0 ? QStringLiteral("…") : QStringLiteral("…") + address.mid(at);
}

const char *securityName(SmtpConfig::Security s)
{
    switch (s) {
    case SmtpConfig::StartTls: return "starttls";
    case SmtpConfig::Tls:      return "tls";
    case SmtpConfig::None:     return "none";
    }
    return "?";
}

} // namespace

SmtpClient::SmtpClient(QObject *parent) : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &SmtpClient::onTimeout);
}

SmtpClient::~SmtpClient()
{
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        delete m_socket;
    }
}

QString SmtpClient::ehloName(const QString &raw)
{
    QString out;
    for (const QChar c : raw)
        if ((c.unicode() < 0x80 && c.isLetterOrNumber()) || c == QLatin1Char('-') || c == QLatin1Char('.')) out += c;
    while (out.startsWith(QLatin1Char('.')) || out.startsWith(QLatin1Char('-'))) out.remove(0, 1);
    while (out.endsWith(QLatin1Char('.')) || out.endsWith(QLatin1Char('-'))) out.chop(1);
    return out.isEmpty() ? QStringLiteral("localhost") : out;
}

SmtpError SmtpClient::classifyReply(int code, const QString &text)
{
    if (code == 534 && text.contains(QStringLiteral("5.7.9"))) return SmtpError::AppPasswordRequired;
    if (code == 535 || code == 534) return SmtpError::AuthRejected;
    if (code == 552 || text.contains(QStringLiteral("5.3.4"))) return SmtpError::TooBig;
    return SmtpError::ServerRejected;
}

void SmtpClient::send(const SmtpConfig &config, const MailMessage &message)
{
    QStringList rcpts = message.to;
    rcpts += message.bcc;
    const QString from = message.fromAddress.trimmed();
    const bool anyTo = std::any_of(message.to.cbegin(), message.to.cend(),
                                   [](const QString &a) { return !a.trimmed().isEmpty(); });
    // Built only when it can go: an empty To would otherwise build a message for nobody.
    const QByteArray mime = anyTo && isPlausibleAddress(from)
                                ? buildMime(message, QDateTime::currentDateTime(), makeMessageId(from)) : QByteArray();
    sendMime(config, from, anyTo ? rcpts : QStringList(), mime);
}

void SmtpClient::sendMime(const SmtpConfig &config, const QString &fromAddress, const QStringList &recipients,
                          const QByteArray &mime)
{
    if (m_busy) {
        // Not this send's result to give: the one in progress keeps its own.
        QTimer::singleShot(0, this, [this]() {
            emit finished(false, SmtpError::BadConfig, QStringLiteral("A message is already being sent."));
        });
        return;
    }
    m_busy = true;
    m_cfg = config;
    m_cfg.host = config.host.trimmed();
    m_state = State::Idle;
    m_rejected.clear();
    m_sslErrors.clear();
    m_queueReply.clear();
    m_lineBuf.clear();
    m_replyLines.clear();
    m_capStartTls = m_capSize = false;
    m_capSizeLimit = 0;
    m_capAuth.clear();
    m_queued = m_written = 0;
    m_messageBytes = mime.size();

    // Every recipient once, in the order given.
    m_rcpts.clear();
    QSet<QString> seen;
    for (const QString &a : recipients) {
        const QString t = a.trimmed();
        if (t.isEmpty() || seen.contains(t.toLower())) continue;
        seen.insert(t.toLower());
        m_rcpts << t;
    }
    m_rcptIndex = 0;
    m_from = fromAddress.trimmed();

    if (m_cfg.host.isEmpty() || m_cfg.port == 0)
        return finishLater(false, SmtpError::BadConfig, QStringLiteral("No mail server is set."));
    if (!isPlausibleAddress(m_from))
        return finishLater(false, SmtpError::BadConfig, QStringLiteral("The From address is not an email address."));
    if (m_rcpts.isEmpty())
        return finishLater(false, SmtpError::BadConfig, QStringLiteral("There is no one to send it to."));
    for (const QString &r : m_rcpts)
        if (!isPlausibleAddress(r)) {
            m_rejected = r;
            return finishLater(false, SmtpError::BadConfig, QStringLiteral("Not an email address: %1").arg(r));
        }
    if (mime.isEmpty())
        return finishLater(false, SmtpError::BadConfig, QStringLiteral("There is no message."));
    if (m_cfg.security == SmtpConfig::None && !m_cfg.username.isEmpty())
        return finishLater(false, SmtpError::BadConfig,
                           QStringLiteral("A login is never sent without encryption; choose STARTTLS or TLS."));
    if (!m_cfg.username.isEmpty() && m_cfg.password.isEmpty())
        return finishLater(false, SmtpError::BadConfig, QStringLiteral("There is no password for this login."));

    m_data = dotStuff(mime);
    if (!m_data.endsWith("\r\n")) m_data += "\r\n";
    m_data += ".\r\n";

    auto *s = new QSslSocket(this);
    m_socket = s;
    QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
    if (!m_cfg.extraCaCertificates.isEmpty()) tls.addCaCertificates(m_cfg.extraCaCertificates);
    tls.setPeerVerifyMode(QSslSocket::VerifyPeer);
    tls.setProtocol(QSsl::TlsV1_2OrLater);
    s->setSslConfiguration(tls);
    s->setPeerVerifyName(m_cfg.host);

    connect(s, &QSslSocket::connected, this, &SmtpClient::onConnected);
    connect(s, &QSslSocket::encrypted, this, &SmtpClient::onEncrypted);
    connect(s, &QSslSocket::readyRead, this, &SmtpClient::onReadyRead);
    connect(s, &QSslSocket::bytesWritten, this, &SmtpClient::onBytesWritten);
    connect(s, &QSslSocket::errorOccurred, this, &SmtpClient::onSocketError);
    connect(s, &QSslSocket::disconnected, this, &SmtpClient::onDisconnected);
    connect(s, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &errors) {
        QStringList words;
        for (const QSslError &e : errors) words << e.errorString();
        m_sslErrors = words.join(QStringLiteral("; "));
        warn(QStringLiteral("certificate refused: %1").arg(m_sslErrors));
        // Never ignored: the handshake fails and errorOccurred says so.
    });

    log(QStringLiteral("connecting to %1:%2 (%3, %4, message %5 bytes, %6 recipient(s))")
            .arg(m_cfg.host).arg(m_cfg.port).arg(QLatin1String(securityName(m_cfg.security)))
            .arg(m_cfg.username.isEmpty() ? QStringLiteral("no login") : QStringLiteral("login"))
            .arg(m_messageBytes).arg(m_rcpts.size()));
    m_state = State::Connecting;
    arm(m_cfg.replyTimeoutMs);
    if (m_cfg.security == SmtpConfig::Tls) s->connectToHostEncrypted(m_cfg.host, m_cfg.port);
    else                                   s->connectToHost(m_cfg.host, m_cfg.port);
}

void SmtpClient::abort()
{
    if (m_busy) fail(SmtpError::Disconnected, QStringLiteral("cancelled"));
}

void SmtpClient::arm(int ms) { m_timer.start(qMax(1, ms)); }

void SmtpClient::command(const QByteArray &line, const QByteArray &logAs)
{
    if (!m_socket) return;
    log(QStringLiteral("> ") + QString::fromUtf8(logAs.isEmpty() ? line : logAs));
    m_socket->write(line + "\r\n");
    arm(m_cfg.replyTimeoutMs);
}

void SmtpClient::onConnected()
{
    log(QStringLiteral("connected to %1").arg(m_socket ? m_socket->peerAddress().toString() : QString()));
    if (m_cfg.security != SmtpConfig::Tls && m_state == State::Connecting) m_state = State::Greeting;
    // With implicit TLS the greeting comes once the handshake is done (onEncrypted).
}

void SmtpClient::onEncrypted()
{
    if (!m_socket) return;
    log(QStringLiteral("encrypted (%1)").arg(m_socket->sessionCipher().name()));
    if (m_state == State::Connecting) {          // Security::Tls: now the 220
        m_state = State::Greeting;
        arm(m_cfg.replyTimeoutMs);
    } else if (m_state == State::Handshake) {    // after STARTTLS: start again, as RFC 3207 §4.2 says
        m_capStartTls = m_capSize = false;
        m_capSizeLimit = 0;
        m_capAuth.clear();
        sendEhlo();
    }
}

void SmtpClient::onReadyRead()
{
    if (!m_socket) return;
    m_lineBuf += m_socket->readAll();
    for (;;) {
        const qsizetype nl = m_lineBuf.indexOf('\n');
        if (nl < 0) break;
        QByteArray line = m_lineBuf.left(nl);
        m_lineBuf.remove(0, nl + 1);
        if (line.endsWith('\r')) line.chop(1);
        bool okCode = line.size() >= 3;
        for (int i = 0; okCode && i < 3; ++i) okCode = line.at(i) >= '0' && line.at(i) <= '9';
        if (!okCode) {
            fail(SmtpError::ServerRejected, QStringLiteral("An unreadable reply: %1").arg(QString::fromUtf8(line.left(80))));
            return;
        }
        const int code = line.left(3).toInt();
        const bool more = line.size() > 3 && line.at(3) == '-';
        m_replyLines << QString::fromUtf8(line.mid(4)).trimmed();
        if (more) continue;
        const QStringList lines = m_replyLines;
        m_replyLines.clear();
        handleReply(code, lines);
        if (!m_socket || !m_busy) return;   // finished inside: nothing more to read for this send
    }
}

void SmtpClient::sendEhlo()
{
    m_state = State::Ehlo;
    command("EHLO " + ehloName(QHostInfo::localHostName()).toLatin1());
}

void SmtpClient::sendMailFrom()
{
    m_state = State::MailFrom;
    QByteArray line = "MAIL FROM:<" + m_from.toLatin1() + '>';
    if (m_capSize) line += " SIZE=" + QByteArray::number(m_data.size());
    command(line, "MAIL FROM:<" + masked(m_from).toUtf8() + '>' + (m_capSize ? " SIZE=…" : ""));
}

void SmtpClient::sendRcpt()
{
    m_state = State::Rcpt;
    const QString &r = m_rcpts.at(m_rcptIndex);
    command("RCPT TO:<" + r.toLatin1() + '>',
            QStringLiteral("RCPT TO:<%1> (%2 of %3)").arg(masked(r)).arg(m_rcptIndex + 1).arg(m_rcpts.size()).toUtf8());
}

void SmtpClient::afterEhlo()
{
    const bool encrypted = m_socket && m_socket->isEncrypted();
    if (m_cfg.security == SmtpConfig::StartTls && !encrypted) {
        if (!m_capStartTls) {
            fail(SmtpError::NoEncryption, QStringLiteral("%1 does not offer STARTTLS").arg(m_cfg.host));
            return;
        }
        m_state = State::StartTls;
        command("STARTTLS");
        return;
    }
    if (m_capSize && m_capSizeLimit > 0 && m_data.size() > m_capSizeLimit) {
        fail(SmtpError::TooBig, QStringLiteral("The server takes at most %1 bytes; this message is %2.")
                                    .arg(m_capSizeLimit).arg(m_data.size()));
        return;
    }
    if (m_cfg.username.isEmpty()) { sendMailFrom(); return; }

    if (!encrypted) {   // unreachable by construction; kept so no future path can send a login in the clear
        fail(SmtpError::NoEncryption, QStringLiteral("not encrypted"));
        return;
    }
    if (m_capAuth.contains(QStringLiteral("PLAIN"))) {
        QByteArray plain;
        plain += '\0';
        plain += m_cfg.username.toUtf8();
        plain += '\0';
        plain += m_cfg.password.toUtf8();
        m_state = State::AuthPlain;
        command("AUTH PLAIN " + plain.toBase64(), "AUTH PLAIN (withheld)");
    } else if (m_capAuth.contains(QStringLiteral("LOGIN"))) {
        m_state = State::AuthLogin;
        command("AUTH LOGIN");
    } else {
        fail(SmtpError::ServerRejected,
             QStringLiteral("The server offers no way to sign in with a password (AUTH PLAIN or LOGIN)."));
    }
}

void SmtpClient::handleReply(int code, const QStringList &lines)
{
    const QString text = lines.join(QLatin1Char(' ')).simplified();
    const QString reply = QString::number(code) + QLatin1Char(' ') + text;
    // EHLO's capability list is long and repeats itself; its first line and the count are enough.
    if (m_state == State::Ehlo && lines.size() > 1)
        log(QStringLiteral("< %1 %2 (+%3 capabilities: %4)").arg(code).arg(lines.first()).arg(lines.size() - 1)
                .arg(lines.mid(1).join(QStringLiteral(", "))));
    else
        log(QStringLiteral("< ") + reply);

    switch (m_state) {
    case State::Greeting:
        if (code == 220) sendEhlo();
        else fail(classifyReply(code, text), reply);
        return;
    case State::Ehlo:
        if (code != 250) { fail(classifyReply(code, text), reply); return; }
        for (int i = 1; i < lines.size(); ++i) {
            const QString cap = lines.at(i).trimmed();
            const QStringList words = cap.split(QRegularExpression(QStringLiteral("[\\s=]+")), Qt::SkipEmptyParts);
            if (words.isEmpty()) continue;
            const QString key = words.first().toUpper();
            if (key == QLatin1String("STARTTLS")) m_capStartTls = true;
            else if (key == QLatin1String("SIZE")) {
                m_capSize = true;
                m_capSizeLimit = words.size() > 1 ? words.at(1).toLongLong() : 0;
            } else if (key == QLatin1String("AUTH")) {
                for (int w = 1; w < words.size(); ++w) m_capAuth.insert(words.at(w).toUpper());
            }
        }
        afterEhlo();
        return;
    case State::StartTls:
        if (code != 220) { fail(SmtpError::NoEncryption, reply); return; }
        m_state = State::Handshake;
        arm(m_cfg.replyTimeoutMs);
        m_socket->startClientEncryption();
        return;
    case State::AuthPlain:
    case State::AuthLoginPass:
        if (code == 235) { sendMailFrom(); return; }
        fail(code == 535 || code == 534 ? classifyReply(code, text) : SmtpError::AuthRejected, reply);
        return;
    case State::AuthLogin:
        if (code != 334) { fail(SmtpError::AuthRejected, reply); return; }
        m_state = State::AuthLoginUser;
        command(m_cfg.username.toUtf8().toBase64(), "(user name withheld)");
        return;
    case State::AuthLoginUser:
        if (code != 334) { fail(code == 535 || code == 534 ? classifyReply(code, text) : SmtpError::AuthRejected, reply); return; }
        m_state = State::AuthLoginPass;
        command(m_cfg.password.toUtf8().toBase64(), "(password withheld)");
        return;
    case State::MailFrom:
        if (code != 250) { fail(classifyReply(code, text), reply); return; }
        sendRcpt();
        return;
    case State::Rcpt:
        if (code != 250 && code != 251) {
            m_rejected = m_rcpts.at(m_rcptIndex);
            fail(SmtpError::RecipientRejected, m_rejected + QStringLiteral(": ") + reply);
            return;
        }
        if (++m_rcptIndex < m_rcpts.size()) { sendRcpt(); return; }
        m_state = State::Data;
        command("DATA");
        return;
    case State::Data:
        if (code != 354) { fail(classifyReply(code, text), reply); return; }
        m_state = State::Body;
        log(QStringLiteral("> (message, %1 bytes)").arg(m_data.size()));
        emit progress(0, m_data.size());
        arm(m_cfg.dataTimeoutMs);
        pump();
        return;
    case State::Body:
        if (code != 250) { fail(classifyReply(code, text), reply); return; }
        m_queueReply = reply;
        emit progress(m_data.size(), m_data.size());
        m_state = State::Quit;
        command("QUIT");
        arm(qMin(kQuitWaitMs, m_cfg.replyTimeoutMs));
        return;
    case State::Quit:
        finish(true, SmtpError::None, m_queueReply);
        return;
    case State::Idle:
    case State::Connecting:
    case State::Handshake:
        fail(SmtpError::ServerRejected, QStringLiteral("An unexpected reply: ") + reply);
        return;
    }
}

void SmtpClient::pump()
{
    if (!m_socket || m_state != State::Body) return;
    while (m_queued < m_data.size() && m_socket->bytesToWrite() < kHighWater) {
        const qint64 n = qMin(kChunk, m_data.size() - m_queued);
        const qint64 w = m_socket->write(m_data.constData() + m_queued, n);
        if (w <= 0) break;
        m_queued += w;
    }
}

void SmtpClient::onBytesWritten(qint64 n)
{
    if (m_state != State::Body) return;
    m_written = qMin<qint64>(m_written + n, m_data.size());
    emit progress(m_written, m_data.size());
    arm(m_cfg.dataTimeoutMs);   // a stall, not the whole upload, is what times out
    pump();
}

void SmtpClient::onSocketError()
{
    if (!m_socket || !m_busy) return;
    const QAbstractSocket::SocketError e = m_socket->error();
    const QString why = m_sslErrors.isEmpty() ? m_socket->errorString() : m_sslErrors;
    if (m_state == State::Quit) { finish(true, SmtpError::None, m_queueReply); return; }
    switch (e) {
    case QAbstractSocket::HostNotFoundError:
        fail(SmtpError::Resolve, why);
        return;
    case QAbstractSocket::ConnectionRefusedError:
        fail(SmtpError::Connect, why);
        return;
    case QAbstractSocket::SslHandshakeFailedError:
    case QAbstractSocket::SslInternalError:
    case QAbstractSocket::SslInvalidUserDataError:
        fail(SmtpError::Tls, why);
        return;
    case QAbstractSocket::RemoteHostClosedError:
        if (!m_sslErrors.isEmpty() || m_state == State::Handshake
            || (m_cfg.security == SmtpConfig::Tls && m_state == State::Connecting))
            fail(SmtpError::Tls, why);
        else
            fail(SmtpError::Disconnected, why);
        return;
    case QAbstractSocket::SocketTimeoutError:
        fail(SmtpError::Timeout, why);
        return;
    default:
        fail(m_state == State::Connecting ? SmtpError::Connect : SmtpError::Disconnected, why);
        return;
    }
}

void SmtpClient::onDisconnected()
{
    if (!m_busy) return;
    if (m_state == State::Quit) finish(true, SmtpError::None, m_queueReply);
    else fail(SmtpError::Disconnected, QStringLiteral("The server closed the connection."));
}

void SmtpClient::onTimeout()
{
    if (!m_busy) return;
    if (m_state == State::Quit) { finish(true, SmtpError::None, m_queueReply); return; }
    const int ms = (m_state == State::Body) ? m_cfg.dataTimeoutMs : m_cfg.replyTimeoutMs;
    fail(SmtpError::Timeout, QStringLiteral("No reply in %1 s (%2).").arg(ms / 1000.0, 0, 'f', 1).arg(stateName()));
}

QString SmtpClient::stateName() const
{
    switch (m_state) {
    case State::Idle:          return QStringLiteral("idle");
    case State::Connecting:    return QStringLiteral("connecting");
    case State::Greeting:      return QStringLiteral("waiting for the greeting");
    case State::Ehlo:          return QStringLiteral("EHLO");
    case State::StartTls:      return QStringLiteral("STARTTLS");
    case State::Handshake:     return QStringLiteral("TLS handshake");
    case State::AuthPlain:
    case State::AuthLogin:
    case State::AuthLoginUser:
    case State::AuthLoginPass: return QStringLiteral("AUTH");
    case State::MailFrom:      return QStringLiteral("MAIL FROM");
    case State::Rcpt:          return QStringLiteral("RCPT TO");
    case State::Data:          return QStringLiteral("DATA");
    case State::Body:          return QStringLiteral("sending the message");
    case State::Quit:          return QStringLiteral("QUIT");
    }
    return {};
}

void SmtpClient::fail(SmtpError code, const QString &reply) { finish(false, code, reply); }

void SmtpClient::finishLater(bool ok, SmtpError code, const QString &reply)
{
    warn(QStringLiteral("not sent: %1").arg(m_rejected.isEmpty() ? reply : QString(reply).replace(m_rejected, masked(m_rejected))));
    m_busy = false;
    m_cfg.password.fill(QLatin1Char(' '));
    m_cfg.password.clear();
    QTimer::singleShot(0, this, [this, ok, code, reply]() { emit finished(ok, code, reply); });
}

void SmtpClient::finish(bool ok, SmtpError code, const QString &reply)
{
    if (!m_busy) return;
    m_busy = false;
    m_timer.stop();
    if (QSslSocket *s = m_socket.data()) {
        m_socket = nullptr;
        s->disconnect(this);
        s->abort();
        s->deleteLater();
    }
    m_cfg.password.fill(QLatin1Char(' '));
    m_cfg.password.clear();
    m_data.clear();
    m_state = State::Idle;
    if (ok) log(QStringLiteral("accepted: %1").arg(reply));
    else    warn(QStringLiteral("failed (%1): %2").arg(int(code))
                     .arg(m_rejected.isEmpty() ? reply : QString(reply).replace(m_rejected, masked(m_rejected))));
    emit finished(ok, code, reply);
}

} // namespace pinpoint::report
