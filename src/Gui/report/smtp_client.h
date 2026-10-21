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

#pragma once

// SENDING A REPORT OVER SMTP (docs/design/session_report_design.md §8.3, option B) — one message, from
// the coach's own mail account, over Qt Network's QSslSocket. One send at a time; a state machine on the
// thread that owns it (the GUI thread), fully asynchronous: no waitFor*, so the window keeps drawing
// while a 10 MB report uploads, and `progress` says how far it has gone.
//
// THE ORDER: connect (TLS from the first byte for Security::Tls, port 465) → 220 → EHLO → STARTTLS → TLS
// → EHLO again (Security::StartTls, port 587) → AUTH → MAIL FROM → RCPT TO each to and bcc → DATA → the
// dot-stuffed message in chunks → the final 250, whose text (the server's queue id) is the result's
// serverReply → QUIT.
//
// NEVER IN THE CLEAR. With a user name set, nothing but EHLO and STARTTLS goes over a connection that is
// not encrypted: StartTls against a server that does not offer STARTTLS fails NoEncryption with no AUTH
// sent, and Security::None with a user name is refused before connecting. None is for an ISP's open relay
// only — no login, so no password to give away. Certificates are always verified (the system's CAs plus
// SmtpConfig::extraCaCertificates, which only the test sets); there is no way to ignore a TLS error.
//
// AUTH PLAIN with an initial response when offered, else AUTH LOGIN; none when the user name is empty.
// A server's advertised SIZE is checked before MAIL FROM, so a report too big for it fails TooBig
// without a byte of it uploaded.
//
// LOGGED to the app log, "[SMTP]": the connection, each command's verb and each reply's code and text.
// NEVER the password, the AUTH exchange's payloads, the message, or a recipient's local part.

#include "smtp_message.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QSslCertificate>
#include <QString>
#include <QStringList>
#include <QTimer>

class QSslSocket;

namespace pinpoint::report {

struct SmtpConfig {
    enum Security { StartTls, Tls, None };
    QString  host;
    quint16  port = 587;
    Security security = StartTls;
    QString  username;                       // empty = no AUTH (open relay; Security::None only)
    QString  password;
    int      replyTimeoutMs = 30000;          // each reply, and the connection
    int      dataTimeoutMs  = 180000;         // the upload stalling, and the final 250 after it
    QList<QSslCertificate> extraCaCertificates;   // tests only: the fake server's CA
};

enum class SmtpError {
    None,
    BadConfig,            // refused before connecting: no host, bad address, a login without TLS…
    Resolve,              // host not found
    Connect,              // refused, unreachable
    Timeout,              // no reply in time
    Tls,                  // the handshake or the certificate
    NoEncryption,         // StartTls asked for, not offered: nothing secret was sent
    AuthRejected,         // 535 and kin
    AppPasswordRequired,  // Gmail's 534 5.7.9
    TooBig,               // SIZE, 552, 5.3.4
    RecipientRejected,    // a RCPT TO refused; SmtpClient::rejectedRecipient() says which
    ServerRejected,       // any other refusal
    Disconnected,         // the server went part-way
};

class SmtpClient : public QObject
{
    Q_OBJECT
public:
    explicit SmtpClient(QObject *parent = nullptr);
    ~SmtpClient() override;

    // Starts the send; the result is always `finished`, never a return value — a refusal before connecting
    // arrives on the next event-loop turn, so every caller has one path.
    void send(const SmtpConfig &config, const MailMessage &message);
    // The same for a message already built (buildMime's bytes): `recipients` is every RCPT TO, Bcc
    // included. send() is this after buildMime; the test uses it to compare bytes exactly.
    void sendMime(const SmtpConfig &config, const QString &fromAddress, const QStringList &recipients,
                  const QByteArray &mime);
    // Ends a send in progress: finished(false, Disconnected, "cancelled").
    void abort();

    bool    busy() const { return m_busy; }
    QString rejectedRecipient() const { return m_rejected; }
    qint64  messageBytes() const { return m_messageBytes; }   // the built message, before dot-stuffing

    // 535 → AuthRejected; 534 with 5.7.9 → AppPasswordRequired; 552 or 5.3.4 → TooBig; else ServerRejected.
    static SmtpError classifyReply(int code, const QString &text);
    // The machine's name as EHLO can carry it: letters, digits, '-' and '.'; "localhost" if nothing is left.
    static QString ehloName(const QString &raw);

signals:
    void progress(qint64 sent, qint64 total);
    void finished(bool ok, pinpoint::report::SmtpError code, const QString &serverReply);

private:
    enum class State { Idle, Connecting, Greeting, Ehlo, StartTls, Handshake, AuthPlain, AuthLogin,
                       AuthLoginUser, AuthLoginPass, MailFrom, Rcpt, Data, Body, Quit };

    void onConnected();
    void onEncrypted();
    void onReadyRead();
    void onBytesWritten(qint64 n);
    void onSocketError();
    void onDisconnected();
    void onTimeout();

    void handleReply(int code, const QStringList &lines);
    void afterEhlo();
    void sendEhlo();
    void sendMailFrom();
    void sendRcpt();
    void command(const QByteArray &line, const QByteArray &logAs = {});
    void arm(int ms);
    void pump();
    void fail(SmtpError code, const QString &reply);
    void finish(bool ok, SmtpError code, const QString &reply);
    void finishLater(bool ok, SmtpError code, const QString &reply);
    QString stateName() const;

    QPointer<QSslSocket> m_socket;
    QTimer      m_timer;
    SmtpConfig  m_cfg;
    State       m_state = State::Idle;
    bool        m_busy = false;
    QString     m_from;
    QStringList m_rcpts;
    int         m_rcptIndex = 0;
    QString     m_rejected;
    QByteArray  m_data;              // dot-stuffed, with the terminating ".\r\n"
    qint64      m_messageBytes = 0;
    qint64      m_queued = 0;        // of m_data, handed to the socket
    qint64      m_written = 0;       // of m_data, written by the socket
    QByteArray  m_lineBuf;
    QStringList m_replyLines;
    // EHLO's answer
    bool        m_capStartTls = false;
    bool        m_capSize = false;
    qint64      m_capSizeLimit = 0;
    QSet<QString> m_capAuth;
    QString     m_sslErrors;
    QString     m_queueReply;
};

} // namespace pinpoint::report
