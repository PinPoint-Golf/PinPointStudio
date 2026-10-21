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

// report_smtp_test — sending a report over SMTP (docs/design/session_report_design.md §8.3, option B):
//
//   the message   smtp_message.cpp, byte by byte: CRLF only, short lines, 7-bit, the em-dash subject and a
//                 non-ASCII name as encoded-words that decode back, the RFC 2231 file name, the attachment
//                 round-tripping exactly, no Bcc header, injected CR/LF gone, dot-stuffing.
//   the client    smtp_client.cpp against a fake server in this process on 127.0.0.1 (a random port), one
//                 script per case: STARTTLS + AUTH PLAIN, AUTH LOGIN, implicit TLS, an open relay, a login
//                 refused with no encryption, no STARTTLS offered, 535, Gmail's 534 5.7.9, SIZE, a refused
//                 recipient, a silent server, a refused connection, an untrusted certificate.
//   the mailer    report_mailer.cpp's sending half: the settings truth table, Gmail's spaced app password,
//                 a changed login forgetting its password, PINPOINT_MAIL_DRYRUN, a real send to the fake
//                 open relay.
//
// The fake server's TLS is a checked-in, test-only CA and certificate (data/smtp/, see its README),
// trusted through SmtpConfig::extraCaCertificates. Nothing here touches the real network, the keychain
// (PINPOINT_MAIL_CREDENTIALS=memory, and the platform half below counts any call) or the real settings
// file (QSettings::setPath to a scratch folder). Silent: nothing here makes a sound.

#include "report/smtp_message.h"
#include "report/smtp_client.h"
#include "report/mail_credentials.h"
#include "report/report_mailer.h"
#include "../../Core/pp_settings.h"
#include "../../Core/PpMessageLog.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QTimer>

#include <cstdio>
#include <functional>
#include <memory>

using namespace pinpoint::report;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

// The platform halves are the test's. compose is never used here; the password store's must never be
// reached at all (memory store), and counts if it is.
static int g_platformStoreCalls = 0;
namespace pinpoint::report {
QString platformCompose(const QStringList &, const QString &, const QString &, const QString &, const QString &) { return {}; }
bool    platformStoreMailPassword(const QString &, const QString &, QString *) { ++g_platformStoreCalls; return false; }
QString platformReadMailPassword(const QString &) { ++g_platformStoreCalls; return {}; }
bool    platformHasMailPassword(const QString &) { ++g_platformStoreCalls; return false; }
bool    platformForgetMailPassword(const QString &) { ++g_platformStoreCalls; return true; }
}

// The app log, drained after every send (it keeps the last 500 lines), to show what it never holds.
static int g_logSeq = -1;
static QStringList g_log;
static void drainLog()
{
    for (const PpMessageLog::Entry &e : PpMessageLog::instance()->fetchSince(g_logSeq)) g_log << e.message;
}

// ── Reading a message back ──────────────────────────────────────────────────────────────────────

static QByteArray unfold(const QByteArray &headerBlock)
{
    QByteArray h = headerBlock;
    h.replace("\r\n ", " ");
    h.replace("\r\n\t", " ");
    return h;
}

static QHash<QByteArray, QByteArray> headers(const QByteArray &mime, QList<QByteArray> *order = nullptr)
{
    QHash<QByteArray, QByteArray> out;
    const QByteArray block = unfold(mime.left(mime.indexOf("\r\n\r\n")));
    for (const QByteArray &line : block.split('\n')) {
        const QByteArray l = line.endsWith('\r') ? line.chopped(1) : line;
        const qsizetype c = l.indexOf(':');
        if (c <= 0) continue;
        const QByteArray name = l.left(c).trimmed().toLower();
        out.insert(name, l.mid(c + 1).trimmed());
        if (order) order->append(name);
    }
    return out;
}

// Adjacent encoded-words' bytes joined (the white space between them is not text, RFC 2047 §6.2).
static QString decodeWords(const QByteArray &value)
{
    static const QRegularExpression word(QStringLiteral("=\\?UTF-8\\?B\\?([A-Za-z0-9+/=]*)\\?="));
    const QString v = QString::fromLatin1(value);
    QByteArray bytes;
    QString out;
    qsizetype pos = 0;
    bool lastWasWord = false;
    auto it = word.globalMatch(v);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString between = v.mid(pos, m.capturedStart() - pos);
        if (!(lastWasWord && between.trimmed().isEmpty())) {
            out += QString::fromUtf8(bytes);
            bytes.clear();
            out += between;
        }
        bytes += QByteArray::fromBase64(m.captured(1).toLatin1());
        pos = m.capturedEnd();
        lastWasWord = true;
    }
    out += QString::fromUtf8(bytes);
    out += v.mid(pos);
    return out;
}

static QByteArray decodeQP(const QByteArray &qp)
{
    QByteArray s = qp;
    s.replace("=\r\n", "");
    QByteArray out;
    for (int i = 0; i < s.size(); ++i) {
        if (s.at(i) == '=' && i + 2 < s.size()) {
            bool ok = false;
            const int v = s.mid(i + 1, 2).toInt(&ok, 16);
            if (ok) { out += char(v); i += 2; continue; }
        }
        out += s.at(i);
    }
    return out;
}

struct Parts { QByteArray textHeaders, text, attHeaders, attachment; bool ok = false; };
static Parts splitParts(const QByteArray &mime)
{
    Parts p;
    const QRegularExpression re(QStringLiteral("boundary=\"([^\"]+)\""));
    const QRegularExpressionMatch m = re.match(QString::fromLatin1(unfold(mime.left(mime.indexOf("\r\n\r\n")))));
    if (!m.hasMatch()) return p;
    const QByteArray b = "--" + m.captured(1).toLatin1();
    const qsizetype p1 = mime.indexOf(b + "\r\n");
    const qsizetype p2 = mime.indexOf("\r\n" + b + "\r\n", p1 + 1);
    const qsizetype p3 = mime.indexOf("\r\n" + b + "--\r\n", p2 + 1);
    if (p1 < 0 || p2 < 0 || p3 < 0 || !mime.endsWith(b + "--\r\n")) return p;
    const QByteArray part1 = mime.mid(p1 + b.size() + 2, p2 - (p1 + b.size() + 2) + 2);   // its own CRLF ends it
    const QByteArray part2 = mime.mid(p2 + 2 + b.size() + 2, p3 - (p2 + 2 + b.size() + 2) + 2);
    const qsizetype h1 = part1.indexOf("\r\n\r\n"), h2 = part2.indexOf("\r\n\r\n");
    p.textHeaders = part1.left(h1);
    p.text = part1.mid(h1 + 4);
    p.attHeaders = unfold(part2.left(h2));
    p.attachment = part2.mid(h2 + 4);
    p.ok = true;
    return p;
}

static bool crlfOnly(const QByteArray &b)
{
    for (int i = 0; i < b.size(); ++i) {
        if (b.at(i) == '\n' && (i == 0 || b.at(i - 1) != '\r')) return false;
        if (b.at(i) == '\r' && (i + 1 >= b.size() || b.at(i + 1) != '\n')) return false;
    }
    return true;
}

static int longestLine(const QByteArray &b)
{
    int longest = 0;
    for (const QByteArray &l : b.split('\n')) longest = qMax(longest, int(l.size()) - (l.endsWith('\r') ? 1 : 0));
    return longest;
}

static bool sevenBit(const QByteArray &b)
{
    for (const char c : b) if (uchar(c) > 127) return false;
    return true;
}

// ── The fake server ─────────────────────────────────────────────────────────────────────────────

struct Script {
    bool        implicitTls = false;
    bool        offerStartTls = true;
    QStringList auth{ QStringLiteral("PLAIN"), QStringLiteral("LOGIN") };
    qint64      size = 0;                                  // 0 = no SIZE
    QByteArray  authReply = "235 2.7.0 Accepted";
    QStringList rejectRcpt;
    bool        silent = false;                            // accept, then never say a word
    QByteArray  finalReply = "250 2.0.0 OK 1729532400 q123-queued";
};

struct Seen {
    int         connections = 0;
    QStringList commands;          // verbs and arguments as received (the AUTH payloads too: test only)
    bool        sawAuth = false, authEncrypted = false, sawMail = false, sawData = false, tls = false;
    QString     authUser, authPass;
    QStringList rcpts;
    QByteArray  data;              // un-stuffed, without the terminator
};

class FakeSmtp : public QTcpServer
{
public:
    Script script;
    Seen   seen;
    QSslCertificate cert;
    QSslKey key;

    void reset(const Script &s) { script = s; seen = Seen{}; }

protected:
    struct Conn { QByteArray buf; enum Mode { Cmd, LoginUser, LoginPass, Data } mode = Cmd; };

    void incomingConnection(qintptr fd) override
    {
        auto *s = new QSslSocket(this);
        if (!s->setSocketDescriptor(fd)) { delete s; return; }
        ++seen.connections;
        s->setLocalCertificate(cert);
        s->setPrivateKey(key);
        s->setPeerVerifyMode(QSslSocket::VerifyNone);
        auto conn = std::make_shared<Conn>();
        QObject::connect(s, &QSslSocket::readyRead, s, [this, s, conn]() { onRead(s, *conn); });
        QObject::connect(s, &QSslSocket::encrypted, s, [this, s]() {
            seen.tls = true;
            if (script.implicitTls) s->write("220 fake.test ESMTP ready\r\n");
        });
        QObject::connect(s, &QSslSocket::disconnected, s, &QObject::deleteLater);
        if (script.silent) return;
        if (script.implicitTls) s->startServerEncryption();
        else                    s->write("220 fake.test ESMTP ready\r\n");
    }

    void reply(QSslSocket *s, const QByteArray &r) { s->write(r + "\r\n"); }

    void onRead(QSslSocket *s, Conn &c)
    {
        c.buf += s->readAll();
        for (;;) {
            if (c.mode == Conn::Data) {
                qsizetype end = -1;
                if (c.buf.startsWith(".\r\n")) end = 0;
                else { const qsizetype i = c.buf.indexOf("\r\n.\r\n"); if (i >= 0) end = i + 2; }
                if (end < 0) return;
                QByteArray raw = c.buf.left(end);
                c.buf.remove(0, end + 3);
                // Un-stuff: a line's leading ".." was ".".
                QByteArray out;
                bool lineStart = true;
                for (int i = 0; i < raw.size(); ++i) {
                    if (lineStart && raw.at(i) == '.' && i + 1 < raw.size() && raw.at(i + 1) == '.') { ++i; }
                    out += raw.at(i);
                    lineStart = raw.at(i) == '\n';
                }
                seen.data = out;
                c.mode = Conn::Cmd;
                reply(s, script.finalReply);
                continue;
            }
            const qsizetype nl = c.buf.indexOf("\r\n");
            if (nl < 0) return;
            const QByteArray line = c.buf.left(nl);
            c.buf.remove(0, nl + 2);
            const QByteArray up = line.toUpper();
            if (c.mode == Conn::LoginUser) {
                seen.authUser = QString::fromUtf8(QByteArray::fromBase64(line));
                c.mode = Conn::LoginPass;
                reply(s, "334 UGFzc3dvcmQ6");
                continue;
            }
            if (c.mode == Conn::LoginPass) {
                seen.authPass = QString::fromUtf8(QByteArray::fromBase64(line));
                c.mode = Conn::Cmd;
                reply(s, script.authReply);
                continue;
            }
            seen.commands << QString::fromUtf8(line);
            if (up.startsWith("EHLO") || up.startsWith("HELO")) {
                QList<QByteArray> caps{ "fake.test greets you" };
                if (script.offerStartTls && !s->isEncrypted()) caps << "STARTTLS";
                if (!script.auth.isEmpty()) caps << "AUTH " + script.auth.join(QLatin1Char(' ')).toLatin1();
                if (script.size > 0) caps << "SIZE " + QByteArray::number(script.size);
                caps << "8BITMIME";
                for (int i = 0; i < caps.size(); ++i)
                    s->write("250" + QByteArray(i + 1 < caps.size() ? "-" : " ") + caps[i] + "\r\n");
            } else if (up == "STARTTLS") {
                reply(s, "220 2.0.0 Ready to start TLS");
                s->flush();
                s->startServerEncryption();
            } else if (up.startsWith("AUTH PLAIN")) {
                seen.sawAuth = true;
                seen.authEncrypted = s->isEncrypted();
                const QList<QByteArray> parts = QByteArray::fromBase64(line.mid(11)).split('\0');
                if (parts.size() == 3) { seen.authUser = QString::fromUtf8(parts[1]); seen.authPass = QString::fromUtf8(parts[2]); }
                reply(s, script.authReply);
            } else if (up.startsWith("AUTH LOGIN")) {
                seen.sawAuth = true;
                seen.authEncrypted = s->isEncrypted();
                c.mode = Conn::LoginUser;
                reply(s, "334 VXNlcm5hbWU6");
            } else if (up.startsWith("MAIL FROM:")) {
                seen.sawMail = true;
                reply(s, "250 2.1.0 OK");
            } else if (up.startsWith("RCPT TO:")) {
                const QString addr = QString::fromUtf8(line.mid(8)).remove(QLatin1Char('<')).remove(QLatin1Char('>')).trimmed();
                seen.rcpts << addr;
                if (script.rejectRcpt.contains(addr))
                    reply(s, "550-5.1.1 The email account that you tried to reach does not exist.\r\n550 5.1.1 Try again.");
                else
                    reply(s, "250 2.1.5 OK");
            } else if (up == "DATA") {
                seen.sawData = true;
                c.mode = Conn::Data;
                reply(s, "354 Go ahead");
            } else if (up == "QUIT") {
                reply(s, "221 2.0.0 closing connection");
                s->disconnectFromHost();
                return;
            } else {
                reply(s, "502 5.5.1 Unrecognized command.");
            }
        }
    }
};

struct Outcome {
    bool done = false, ok = false;
    SmtpError code = SmtpError::None;
    QString reply, rejected;
    qint64 lastSent = 0, lastTotal = 0;
    int progressCount = 0;
};

static void settle(int ms = 50)
{
    QEventLoop l;
    QTimer::singleShot(ms, &l, &QEventLoop::quit);
    l.exec();
}

static Outcome runClient(const std::function<void(SmtpClient &)> &start, int guardMs = 15000)
{
    SmtpClient c;
    Outcome o;
    QEventLoop loop;
    QObject::connect(&c, &SmtpClient::progress, [&](qint64 s, qint64 t) { o.lastSent = s; o.lastTotal = t; ++o.progressCount; });
    QObject::connect(&c, &SmtpClient::finished, [&](bool ok, SmtpError code, const QString &r) {
        o.done = true; o.ok = ok; o.code = code; o.reply = r; loop.quit();
    });
    QTimer::singleShot(guardMs, &loop, &QEventLoop::quit);
    start(c);
    if (!o.done) loop.exec();
    o.rejected = c.rejectedRecipient();
    settle();   // the server sees QUIT and closes
    drainLog();
    return o;
}

static Outcome run(const SmtpConfig &cfg, const MailMessage &m, int guardMs = 15000)
{
    return runClient([&](SmtpClient &c) { c.send(cfg, m); }, guardMs);
}

static QByteArray readFile(const QString &p)
{
    QFile f(p);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static MailMessage sampleMessage(const QByteArray &attachment)
{
    MailMessage m;
    m.fromName = QStringLiteral("José Coach");
    m.fromAddress = QStringLiteral("coach@example.com");
    m.to = { QStringLiteral("nick@example.com"), QStringLiteral("jess@example.org") };
    m.bcc = { QStringLiteral("coach@example.com") };
    m.replyTo = QStringLiteral("studio@example.com");
    m.subject = QString::fromUtf8("Your session report — 20 October 2026");
    m.body = QString::fromUtf8("Hi Nick,\n\nHere is your report from today's session.\n.\n..two dots\nFrom here on, a line\n"
                               "with a trailing space \n")
             + QString(40, QChar(0x00e9)) + QString(120, QLatin1Char('x')) + QStringLiteral("\n\nJosé");
    m.attachmentName = QString::fromUtf8("José_session-report.pdf");
    m.attachment = attachment;
    return m;
}

int main(int argc, char **argv)
{
    qputenv("PINPOINT_MAIL_CREDENTIALS", "memory");
    qputenv("PINPOINT_MAIL_DRYRUN", "0");
    QCoreApplication app(argc, argv);
    // The fake server's TLS needs server mode; OpenSSL's backend does it in memory, where Secure
    // Transport's would make a temporary keychain. Chosen before any socket exists.
    if (QSslSocket::availableBackends().contains(QStringLiteral("openssl")))
        QSslSocket::setActiveBackend(QStringLiteral("openssl"));

    QTemporaryDir cfgDir, files;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, cfgDir.path());
    const QString ini = QFileInfo(ppSettings().fileName()).absoluteFilePath();
    check(ini.startsWith(QFileInfo(cfgDir.path()).absoluteFilePath()), "settings point at a scratch folder");

    QByteArray attachment(300 * 1024 + 7, '\0');
    QRandomGenerator rng(20261021);
    for (char &c : attachment) c = char(rng.bounded(256));
    for (int i = 0; i < 256; ++i) attachment[i] = char(i);   // every byte value at least once

    // ── The message ──
    std::printf("the message\n");
    {
        const MailMessage m = sampleMessage(attachment);
        const QDateTime when(QDate(2026, 10, 20), QTime(18, 40, 5), QTimeZone::fromSecondsAheadOfUtc(3600));
        const QByteArray mime = buildMime(m, when, QStringLiteral("<abc-123@example.com>"));
        check(crlfOnly(mime), "CRLF line endings only");
        check(longestLine(mime) <= 78, "no line over 78 (998 is the limit)");
        check(sevenBit(mime), "7-bit throughout");
        check(mime.endsWith("\r\n"), "ends in CRLF");

        QList<QByteArray> order;
        const auto h = headers(mime, &order);
        check(h.value("date") == "Tue, 20 Oct 2026 18:40:05 +0100", "Date is RFC 5322");
        check(decodeWords(h.value("subject")) == m.subject, "em-dash subject decodes back");
        check(h.value("subject").startsWith("=?UTF-8?B?"), "...as encoded-words");
        {
            bool short75 = true;
            static const QRegularExpression w(QStringLiteral("=\\?UTF-8\\?B\\?[^?]*\\?="));
            for (auto it = w.globalMatch(QString::fromLatin1(mime.left(mime.indexOf("\r\n\r\n")))); it.hasNext();)
                if (it.next().capturedLength() > 75) short75 = false;
            check(short75, "every encoded-word is at most 75 characters");
        }
        check(decodeWords(h.value("from")) == QString::fromUtf8("José Coach <coach@example.com>"), "From: name encoded, address plain");
        check(h.value("to") == "nick@example.com, jess@example.org", "To lists both");
        check(h.value("reply-to") == "studio@example.com", "Reply-To when it differs");
        check(h.value("message-id") == "<abc-123@example.com>", "Message-ID as given");
        check(h.value("mime-version") == "1.0", "MIME-Version");
        check(!h.contains("bcc") && !mime.contains("Bcc:") && !mime.contains("bcc:"), "no Bcc header anywhere");
        check(h.value("content-type").startsWith("multipart/mixed;"), "multipart/mixed");

        const Parts p = splitParts(mime);
        check(p.ok, "two parts, closed by the final delimiter");
        check(p.textHeaders.contains("text/plain; charset=UTF-8") && p.textHeaders.contains("quoted-printable"),
              "text part: UTF-8 quoted-printable");
        QString expected = m.body;
        expected.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
        expected += QStringLiteral("\r\n");
        check(QString::fromUtf8(decodeQP(p.text)) == expected, "body decodes back exactly");
        check(!p.text.contains("\r\n.") && !p.text.startsWith('.'), "no body line starts with a dot");
        check(!p.text.contains("\r\nFrom "), "no body line starts with \"From \"");
        check(p.attHeaders.contains("application/pdf;"), "attachment's type");
        check(p.attHeaders.contains("filename*=UTF-8''Jos%C3%A9_session-report.pdf"), "RFC 2231 file name");
        check(p.attHeaders.contains("Content-Disposition: attachment;"), "disposition: attachment");
        {
            const QRegularExpression nm(QStringLiteral("name=\"([^\"]+)\""));
            const auto mm = nm.match(QString::fromLatin1(p.attHeaders));
            check(mm.hasMatch() && decodeWords(mm.captured(1).toLatin1()) == m.attachmentName, "Content-Type name= decodes back");
        }
        bool cols76 = true;
        const QList<QByteArray> b64lines = p.attachment.split('\n');
        for (int i = 0; i < b64lines.size() - 2; ++i) if (b64lines[i].chopped(1).size() != 76) cols76 = false;
        check(cols76, "base64 in 76-column lines");
        QByteArray b64 = p.attachment;
        b64.replace("\r\n", "");
        check(QByteArray::fromBase64(b64) == attachment, "attachment round-trips byte-exact");

        // Reply-To left out when it is the From address.
        MailMessage same = m;
        same.replyTo = QStringLiteral("Coach@Example.com");
        check(!headers(buildMime(same, when, QStringLiteral("<x@example.com>"))).contains("reply-to"),
              "no Reply-To when it is the From address");

        // Header injection.
        MailMessage evil = m;
        evil.subject = QStringLiteral("Hello\r\nBcc: evil@example.com");
        evil.fromName = QStringLiteral("Evil\nX-Injected: 1");
        evil.replyTo = QStringLiteral("a@example.com\r\nX-Two: 2");
        const QByteArray em = buildMime(evil, when, QStringLiteral("<y@example.com>"));
        const QByteArray eh = em.left(em.indexOf("\r\n\r\n"));
        check(!eh.contains("\r\nBcc:") && !eh.contains("\r\nX-Injected:") && !eh.contains("\r\nX-Two:"),
              "CR/LF in header inputs cannot start a header");
        check(decodeWords(headers(em).value("subject")) == QStringLiteral("Hello Bcc: evil@example.com"),
              "...the subject keeps its words on one line");

        // A long ASCII subject folds at its spaces and unfolds back.
        MailMessage longOne = m;
        longOne.subject = QStringLiteral("A rather long subject line ").repeated(8).trimmed();
        const QByteArray lm = buildMime(longOne, when, QStringLiteral("<z@example.com>"));
        check(longestLine(lm) <= 78 && headers(lm).value("subject") == longOne.subject.toLatin1(),
              "long ASCII subject folded and unfolds back");

        // No attachment: one text part.
        MailMessage plain;
        plain.fromAddress = QStringLiteral("coach@example.com");
        plain.to = { QStringLiteral("coach@example.com") };
        plain.subject = QStringLiteral("Test");
        plain.body = QStringLiteral("Just words.");
        const QByteArray pm = buildMime(plain, when, QStringLiteral("<t@example.com>"));
        check(headers(pm).value("content-type") == "text/plain; charset=UTF-8" && pm.endsWith("Just words.\r\n"),
              "no attachment: a single text part");

        check(dotStuff(".a\r\nb\r\n..c\r\n.\r\n") == "..a\r\nb\r\n...c\r\n..\r\n", "dotStuff doubles every leading dot");
        check(isPlausibleAddress(QStringLiteral("nick.henn+golf@example.co.uk")), "plausible: a real-looking address");
        check(isPlausibleAddress(QStringLiteral("o'brien@example.ie")), "plausible: an apostrophe");
        for (const char *bad : { "nick", "nick@", "@example.com", "nick@example", "nick@@example.com", "nick @example.com",
                                 "<nick@example.com>", "\"nick\"@example.com", "nick@example.com\r\nBcc: x@y.com",
                                 "nick@.example.com", "nick@example..com", "nick@example.c", "josé@example.com" }) {
            const std::string label = std::string("not plausible: ") + QString::fromUtf8(bad).replace(QStringLiteral("\r\n"), QStringLiteral("\\r\\n")).toStdString();
            check(!isPlausibleAddress(QString::fromUtf8(bad)), label.c_str());
        }
        check(SmtpClient::ehloName(QStringLiteral("Mark's MacBook Pro.local")) == QLatin1String("MarksMacBookPro.local"),
              "EHLO name sanitised");
        check(SmtpClient::ehloName(QStringLiteral("…")) == QLatin1String("localhost"), "EHLO name falls back to localhost");
    }

    // ── The client against the fake server ──
    std::printf("the client (TLS backend: %s)\n", qPrintable(QSslSocket::activeBackend()));
    const QString dataDir = QStringLiteral(PP_SMTP_TEST_DATA);
    const QList<QSslCertificate> ca = QSslCertificate::fromData(readFile(dataDir + QStringLiteral("/ca.pem")), QSsl::Pem);
    FakeSmtp server;
    server.cert = QSslCertificate(readFile(dataDir + QStringLiteral("/server.pem")), QSsl::Pem);
    server.key = QSslKey(readFile(dataDir + QStringLiteral("/server.key")), QSsl::Rsa, QSsl::Pem);
    check(!ca.isEmpty() && !server.cert.isNull() && !server.key.isNull(), "test CA, certificate and key load");
    check(server.listen(QHostAddress::LocalHost, 0), "fake server listening on 127.0.0.1");

    SmtpConfig base;
    base.host = QStringLiteral("127.0.0.1");
    base.port = server.serverPort();
    base.security = SmtpConfig::StartTls;
    base.username = QStringLiteral("coach@example.com");
    base.password = QStringLiteral("abcdefghijklmnop");
    base.replyTimeoutMs = 5000;
    base.dataTimeoutMs = 5000;
    base.extraCaCertificates = ca;
    const MailMessage msg = sampleMessage(attachment);

    {
        server.reset(Script{});
        const Outcome o = run(base, msg);
        check(o.ok && o.code == SmtpError::None, "STARTTLS + AUTH PLAIN: sent");
        if (!o.ok) std::printf("      (%d: %s)\n", int(o.code), qPrintable(o.reply));
        check(o.reply.contains(QStringLiteral("q123-queued")), "...the final 250 (queue id) is the reply");
        check(server.seen.tls && server.seen.authEncrypted, "...AUTH went over TLS");
        check(server.seen.authUser == base.username && server.seen.authPass == base.password, "...PLAIN carried the login");
        check(server.seen.commands.size() >= 2 && server.seen.commands.at(0).startsWith(QStringLiteral("EHLO "))
              && server.seen.commands.at(1) == QLatin1String("STARTTLS"), "...EHLO then STARTTLS");
        check(server.seen.commands.filter(QRegularExpression(QStringLiteral("^EHLO "))).size() == 2, "...EHLO again after TLS");
        check(server.seen.rcpts == QStringList({ QStringLiteral("nick@example.com"), QStringLiteral("jess@example.org"),
                                                 QStringLiteral("coach@example.com") }), "...RCPT TO each to and the bcc");
        const Parts p = splitParts(server.seen.data);
        QByteArray b64 = p.attachment;
        b64.replace("\r\n", "");
        check(p.ok && QByteArray::fromBase64(b64) == attachment, "...the server got the attachment byte-exact");
        check(!headers(server.seen.data).contains("bcc"), "...and no Bcc header");
        check(o.progressCount > 0 && o.lastSent == o.lastTotal && o.lastTotal > qint64(attachment.size()),
              "...progress reached the whole message");
    }
    {
        // Exact bytes, with lines that need stuffing.
        server.reset(Script{});
        const QByteArray raw = "From: coach@example.com\r\nTo: nick@example.com\r\nSubject: dots\r\n\r\n.\r\n..two\r\n.three\r\nend\r\n";
        const Outcome o = runClient([&](SmtpClient &c) {
            c.sendMime(base, QStringLiteral("coach@example.com"), { QStringLiteral("nick@example.com") }, raw);
        });
        check(o.ok && server.seen.data == raw, "the server receives the exact message after un-stuffing");
    }
    {
        Script s;
        s.auth = { QStringLiteral("LOGIN") };
        server.reset(s);
        const Outcome o = run(base, msg);
        check(o.ok && server.seen.commands.contains(QStringLiteral("AUTH LOGIN")), "AUTH LOGIN when PLAIN is not offered");
        check(server.seen.authUser == base.username && server.seen.authPass == base.password && server.seen.authEncrypted,
              "...user name and password, over TLS");
    }
    {
        Script s;
        s.implicitTls = true;
        s.offerStartTls = false;
        server.reset(s);
        SmtpConfig c = base;
        c.security = SmtpConfig::Tls;
        const Outcome o = run(c, msg);
        check(o.ok && server.seen.tls && server.seen.authEncrypted, "implicit TLS (465 style): sent");
        check(!server.seen.commands.contains(QStringLiteral("STARTTLS")), "...without STARTTLS");
    }
    {
        Script s;
        s.offerStartTls = false;
        s.auth.clear();
        server.reset(s);
        SmtpConfig c = base;
        c.security = SmtpConfig::None;
        c.username.clear();
        c.password.clear();
        const Outcome o = run(c, msg);
        check(o.ok && !server.seen.sawAuth && !server.seen.tls && server.seen.sawData, "open relay: no login, no TLS, sent");
    }
    {
        server.reset(Script{});
        SmtpConfig c = base;
        c.security = SmtpConfig::None;
        const Outcome o = run(c, msg);
        check(!o.ok && o.code == SmtpError::BadConfig && server.seen.connections == 0,
              "a login with security none: refused before connecting");
    }
    {
        Script s;
        s.offerStartTls = false;
        server.reset(s);
        const Outcome o = run(base, msg);
        check(!o.ok && o.code == SmtpError::NoEncryption, "no STARTTLS offered: NoEncryption");
        check(!server.seen.sawAuth && !server.seen.sawMail && server.seen.commands.filter(QStringLiteral("AUTH")).isEmpty(),
              "...and the server never saw AUTH or MAIL");
    }
    {
        Script s;
        s.authReply = "535-5.7.8 Username and Password not accepted. For more information, go to\r\n"
                      "535 5.7.8  https://support.google.com/mail/?p=BadCredentials q123";
        server.reset(s);
        const Outcome o = run(base, msg);
        check(!o.ok && o.code == SmtpError::AuthRejected && !server.seen.sawMail, "535: AuthRejected");
        check(o.reply.contains(QStringLiteral("not accepted")) && o.reply.contains(QStringLiteral("BadCredentials")),
              "...both reply lines read");
        check(sendErrorWords(o.code, QStringLiteral("gmail"), base.host, base.port, o.reply, {}, 0)
                  == QString::fromUtf8("Gmail didn't accept that app password. Make a new one at "
                                       "myaccount.google.com/apppasswords and paste it in Settings → Studio."),
              "...Gmail's words");
        check(sendErrorWords(o.code, QStringLiteral("other"), base.host, base.port, o.reply, {}, 0)
                  == QStringLiteral("Your mail server didn't accept the user name or password."), "...other's words");
    }
    {
        Script s;
        s.authReply = "534-5.7.9 Application-specific password required. Learn more at\r\n"
                      "534 5.7.9  https://support.google.com/mail/?p=InvalidSecondFactor q123";
        server.reset(s);
        const Outcome o = run(base, msg);
        check(!o.ok && o.code == SmtpError::AppPasswordRequired, "534 5.7.9: AppPasswordRequired");
        check(sendErrorWords(o.code, QStringLiteral("gmail"), base.host, base.port, o.reply, {}, 0)
                  .startsWith(QStringLiteral("Gmail needs an app password here")), "...Gmail's words");
    }
    {
        Script s;
        s.size = 10000;
        server.reset(s);
        const Outcome o = run(base, msg);
        check(!o.ok && o.code == SmtpError::TooBig && !server.seen.sawMail && !server.seen.sawData,
              "SIZE too small: TooBig before MAIL FROM, nothing uploaded");
        check(sendErrorWords(o.code, QStringLiteral("gmail"), base.host, base.port, o.reply, {}, 7759462)
                  == QStringLiteral("The report is too big for your mail server (7.4 MB)."), "...in MB");
    }
    {
        Script s;
        s.rejectRcpt = { QStringLiteral("jess@example.org") };
        server.reset(s);
        const Outcome o = run(base, msg);
        check(!o.ok && o.code == SmtpError::RecipientRejected && o.rejected == QLatin1String("jess@example.org")
              && o.reply.contains(QStringLiteral("jess@example.org")) && !server.seen.sawData,
              "RCPT 550: RecipientRejected, naming the address, no DATA");
        check(sendErrorWords(o.code, {}, base.host, base.port, o.reply, o.rejected, 0)
                  == QStringLiteral("The mail server refused the address jess@example.org."), "...in words");
    }
    {
        Script s;
        s.silent = true;
        server.reset(s);
        SmtpConfig c = base;
        c.replyTimeoutMs = 300;
        const Outcome o = run(c, msg);
        check(!o.ok && o.code == SmtpError::Timeout, "a silent server: Timeout");
    }
    {
        QTcpServer gone;
        gone.listen(QHostAddress::LocalHost, 0);
        const quint16 port = gone.serverPort();
        gone.close();
        SmtpConfig c = base;
        c.port = port;
        const Outcome o = run(c, msg);
        check(!o.ok && o.code == SmtpError::Connect, "connection refused: Connect");
        check(sendErrorWords(o.code, {}, QStringLiteral("smtp.example.com"), 587, o.reply, {}, 0)
                  == QString::fromUtf8("Couldn't reach smtp.example.com. Check the network — some networks block port 587."),
              "...in words");
    }
    {
        Script s;
        s.implicitTls = true;
        server.reset(s);
        SmtpConfig c = base;
        c.security = SmtpConfig::Tls;
        c.extraCaCertificates.clear();
        const Outcome o = run(c, msg);
        check(!o.ok && o.code == SmtpError::Tls && !server.seen.sawAuth, "an untrusted certificate: Tls, nothing sent");
    }

    // ── The mailer ──
    std::printf("the mailer\n");
    {
        QSettings s = ppSettings();
        s.clear();
    }
    {
        ReportMailer mailer;
        check(mailer.provider().isEmpty() && !mailer.sendConfigured() && mailer.copyToSelf(), "fresh: not set up, copy to self on");

        mailer.setProvider(QStringLiteral("gmail"));
        check(mailer.host() == QLatin1String("smtp.gmail.com") && mailer.port() == 587 && mailer.security() == QLatin1String("starttls"),
              "gmail preset");
        check(!mailer.sendConfigured(), "gmail, no login: not configured");
        QVariantMap r = mailer.setPassword(QStringLiteral("abcd efgh"));
        check(!r.value(QStringLiteral("ok")).toBool(), "a password before the user name: refused");
        mailer.setUsername(QStringLiteral("coach@gmail.com"));
        check(!mailer.sendConfigured(), "gmail, login, no password: not configured");
        r = mailer.setPassword(QStringLiteral(" abcd efgh\tijkl mnop "));
        check(r.value(QStringLiteral("ok")).toBool() && mailer.hasPassword(), "password saved");
        check(readMailPassword(mailAccount(QStringLiteral("coach@gmail.com"), QStringLiteral("smtp.gmail.com")))
                  == QLatin1String("abcdefghijklmnop"), "gmail: every space stripped from the app password");
        check(mailer.sendConfigured(), "gmail, login, password: configured");
        check(!readFile(ini).contains("abcdefghijklmnop"), "the password is not in the INI");

        mailer.setUsername(QStringLiteral("other@gmail.com"));
        check(!mailer.hasPassword() && !mailer.sendConfigured(), "a new user name forgets the password");
        check(readMailPassword(mailAccount(QStringLiteral("coach@gmail.com"), QStringLiteral("smtp.gmail.com"))).isEmpty(),
              "...the old one is gone from the store");
        mailer.setPassword(QStringLiteral("abcdefghijklmnop"));
        mailer.setProvider(QStringLiteral("icloud"));
        check(mailer.host() == QLatin1String("smtp.mail.me.com") && !mailer.hasPassword(), "icloud preset: another server, password forgotten");
        mailer.setProvider(QStringLiteral("microsoft"));
        check(mailer.host() == QLatin1String("smtp.office365.com") && mailer.port() == 587, "microsoft preset");
        mailer.setProvider(QStringLiteral("other"));
        check(mailer.host() == QLatin1String("smtp.office365.com"), "other keeps the fields as they are");
        mailer.setPassword(QStringLiteral("  pass word  "));
        check(readMailPassword(mailAccount(mailer.username(), mailer.host())) == QLatin1String("pass word"),
              "other: only the ends trimmed");
        mailer.setServer(QStringLiteral("smtp.office365.com"), 465, QStringLiteral("tls"));
        check(mailer.hasPassword() && mailer.sendConfigured(), "same server, new port: the password stays");
        mailer.setProvider(QString());
        check(!mailer.sendConfigured(), "provider \"\": not set up");
        mailer.setProvider(QStringLiteral("other"));
        mailer.setServer(QStringLiteral("relay.example.net"), 25, QStringLiteral("none"));
        check(!mailer.sendConfigured(), "none with a login and no password: not configured");
        mailer.setUsername(QString());
        check(mailer.sendConfigured(), "none with no login (open relay): configured");
        mailer.setServer(QStringLiteral("relay.example.net"), 587, QStringLiteral("starttls"));
        check(!mailer.sendConfigured(), "starttls with no login: not configured");
        mailer.setServer(QString(), 587, QStringLiteral("none"));
        check(!mailer.sendConfigured(), "no host: not configured");
        mailer.setServer(QStringLiteral("relay.example.net"), 0, QStringLiteral("none"));
        check(!mailer.sendConfigured(), "no port: not configured");

        mailer.setCopyToSelf(false);
        ReportMailer reread;
        check(!reread.copyToSelf() && reread.provider() == QLatin1String("other") && reread.security() == QLatin1String("none"),
              "settings read back by a new mailer");

        mailer.forgetSending();
        check(mailer.provider().isEmpty() && mailer.host().isEmpty() && !mailer.hasPassword() && !mailer.sendConfigured()
                  && mailer.copyToSelf(), "forgetSending clears it all");
        QSettings s = ppSettings();
        check(s.childGroups().isEmpty() || !s.childGroups().contains(QStringLiteral("ReportSending")), "...including the INI group");
    }
    {
        // Sending: against the fake server as an open relay.
        Script relay;
        relay.offerStartTls = false;
        relay.auth.clear();
        server.reset(relay);
        const QString pdf = files.filePath(QStringLiteral("2026-10-20_Nick-Henn_session-report.pdf"));
        { QFile f(pdf); f.open(QIODevice::WriteOnly); f.write(attachment); }
        const QString dir = files.filePath(QStringLiteral("session"));

        ReportMailer mailer;
        QVariantMap r = mailer.send(dir, { QStringLiteral("nick@example.com") }, QStringLiteral("coach@example.com"),
                                    QStringLiteral("Coach"), QStringLiteral("S"), QStringLiteral("B"), pdf);
        check(!r.value(QStringLiteral("ok")).toBool() && mailer.lastAction() == QLatin1String("send:failed"),
              "not set up: refused at once");

        mailer.setProvider(QStringLiteral("other"));
        mailer.setServer(QStringLiteral("127.0.0.1"), server.serverPort(), QStringLiteral("none"));
        check(mailer.sendConfigured(), "open relay on the fake server: configured");

        QString gotDir, gotMethod, gotError;
        bool gotOk = false;
        int finishedCount = 0;
        QObject::connect(&mailer, &ReportMailer::sendFinished, [&](const QString &d, bool ok, const QString &m, const QString &e) {
            gotDir = d; gotOk = ok; gotMethod = m; gotError = e; ++finishedCount;
        });
        bool testOk = false;
        qint64 testAt = 0;
        int testCount = 0;
        QObject::connect(&mailer, &ReportMailer::testFinished, [&](bool ok, const QString &, qint64 at) {
            testOk = ok; testAt = at; ++testCount;
        });
        const auto waitFor = [](const std::function<bool()> &done) {
            QElapsedTimer t;
            t.start();
            while (!done() && t.elapsed() < 10000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        };

        r = mailer.send(dir, { QStringLiteral("not an address") }, QStringLiteral("coach@example.com"), {}, {}, {}, pdf);
        check(!r.value(QStringLiteral("ok")).toBool() && r.value(QStringLiteral("error")).toString().contains(QStringLiteral("not an address")),
              "a bad address: refused, named");
        r = mailer.send(dir, { QStringLiteral("nick@example.com") }, QStringLiteral("coach@example.com"), {}, {}, {}, pdf + QStringLiteral(".gone"));
        check(!r.value(QStringLiteral("ok")).toBool(), "no file: refused");
        r = mailer.send(dir, { QStringLiteral("nick@example.com") }, QString(), {}, {}, {}, pdf);
        check(!r.value(QStringLiteral("ok")).toBool(), "no From address (no login, no reply-to): refused");

        // Dry run: logged, no socket, finished on the next turn.
        qputenv("PINPOINT_MAIL_DRYRUN", "1");
        r = mailer.send(dir, { QStringLiteral("nick@example.com") }, QStringLiteral("coach@example.com"),
                        QStringLiteral("Coach"), QString::fromUtf8("Your session report — 20 October 2026"), QStringLiteral("Hi"), pdf);
        check(r.value(QStringLiteral("ok")).toBool() && finishedCount == 0 && mailer.sending(), "dry run: accepted, not yet finished");
        waitFor([&] { return finishedCount > 0; });
        check(finishedCount == 1 && gotOk && gotMethod == QLatin1String("dry-run") && gotDir == QDir::cleanPath(dir),
              "dry run: sendFinished with method dry-run");
        check(server.seen.connections == 0, "dry run: no socket opened");
        check(mailer.lastAction() == QLatin1String("send:dry-run") && !mailer.sending(), "dry run: lastAction send:dry-run");
        r = mailer.sendTest(QStringLiteral("Coach"), QStringLiteral("coach@example.com"));
        waitFor([&] { return testCount > 0; });
        check(r.value(QStringLiteral("ok")).toBool() && testOk && testAt > 0 && mailer.lastAction() == QLatin1String("test:dry-run")
              && server.seen.connections == 0, "dry run: sendTest finishes, no socket");
        qputenv("PINPOINT_MAIL_DRYRUN", "0");

        // For real, to the fake relay: From the reply-to (no login), a Bcc copy to it.
        r = mailer.send(dir, { QStringLiteral("nick@example.com") }, QStringLiteral("coach@example.com"),
                        QStringLiteral("Coach"), QString::fromUtf8("Your session report — 20 October 2026"), QStringLiteral("Hi Nick"), pdf);
        check(r.value(QStringLiteral("ok")).toBool() && mailer.sending(), "send: accepted, sending");
        const QVariantMap again = mailer.send(dir, { QStringLiteral("nick@example.com") }, QStringLiteral("coach@example.com"),
                                              {}, {}, {}, pdf);
        check(!again.value(QStringLiteral("ok")).toBool(), "a second send while sending: refused");
        waitFor([&] { return finishedCount > 1; });
        check(finishedCount == 2 && gotOk && gotMethod == QLatin1String("smtp") && gotError.isEmpty(), "send: sendFinished smtp ok");
        check(mailer.lastAction() == QLatin1String("send:smtp") && !mailer.sending() && mailer.sendProgress() == 1.0,
              "send: lastAction send:smtp, progress 1");
        check(server.seen.rcpts == QStringList({ QStringLiteral("nick@example.com"), QStringLiteral("coach@example.com") }),
              "send: to the golfer, Bcc the coach");
        const auto h = headers(server.seen.data);
        check(h.value("from") == "Coach <coach@example.com>" && !h.contains("reply-to") && !h.contains("bcc"),
              "send: From the coach, no Reply-To (same), no Bcc header");
        const Parts p = splitParts(server.seen.data);
        QByteArray b64 = p.attachment;
        b64.replace("\r\n", "");
        check(QByteArray::fromBase64(b64) == attachment && p.attHeaders.contains("filename=\"2026-10-20_Nick-Henn_session-report.pdf\""),
              "send: the PDF, under its own name");

        // A server refusal comes back as words.
        Script refuse = relay;
        refuse.rejectRcpt = { QStringLiteral("nick@example.com") };
        server.reset(refuse);
        mailer.send(dir, { QStringLiteral("nick@example.com") }, QStringLiteral("coach@example.com"), {}, {}, {}, pdf);
        waitFor([&] { return finishedCount > 2; });
        check(!gotOk && gotMethod == QLatin1String("smtp")
                  && gotError == QStringLiteral("The mail server refused the address nick@example.com.")
                  && mailer.lastAction() == QLatin1String("send:failed"), "a refused recipient: sendFinished with words");
    }

    check(g_platformStoreCalls == 0, "the platform password store was never reached");

    // What the log says, and what it never says.
    drainLog();
    const QStringList smtpLines = g_log.filter(QStringLiteral("[SMTP]"));
    check(!smtpLines.filter(QStringLiteral("> EHLO")).isEmpty() && !smtpLines.filter(QStringLiteral("< 250")).isEmpty()
              && !smtpLines.filter(QStringLiteral("AUTH PLAIN (withheld)")).isEmpty(),
          "the log has each command's verb and each reply");
    const QStringList secrets{
        QStringLiteral("abcdefghijklmnop"), QStringLiteral("pass word"),
        QString::fromLatin1(QByteArray("\0coach@example.com\0abcdefghijklmnop", 35).toBase64()),   // AUTH PLAIN
        QString::fromLatin1(QByteArray("coach@example.com").toBase64()),                             // AUTH LOGIN user
        QString::fromLatin1(QByteArray("abcdefghijklmnop").toBase64()),                              // AUTH LOGIN password
    };
    bool clean = true;
    for (const QString &line : g_log)
        for (const QString &secret : secrets)
            if (line.contains(secret)) clean = false;
    check(clean, "the log never holds the password or the AUTH payloads");
    check(smtpLines.filter(QStringLiteral("nick@example.com")).isEmpty() && smtpLines.filter(QStringLiteral("Hi Nick")).isEmpty(),
          "the [SMTP] lines hold no recipient address and no message text");

    std::printf("%s (%d failed)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
