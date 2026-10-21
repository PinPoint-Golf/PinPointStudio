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

#include "smtp_message.h"

#include <QLocale>
#include <QUuid>

namespace pinpoint::report {

namespace {

constexpr int kFoldAt = 76;   // RFC 2047 §2's line length for encoded-words, used for every header here

bool isAsciiPrintable(const QString &s)
{
    for (const QChar c : s)
        if (c.unicode() < 0x20 || c.unicode() > 0x7e) return false;
    return true;
}

// An address as it goes into a header or an SMTP command: no whitespace, no controls.
QString addressSafe(const QString &a)
{
    QString out;
    for (const QChar c : a)
        if (!c.isSpace() && c.unicode() >= 0x20 && c.unicode() != 0x7f) out += c;
    return out;
}

// Encoded-words for `text`, the first fitting in what is left of the first line, the rest 75 each, one to
// a line. A character's UTF-8 bytes never straddle two words (RFC 2047 §5(3)).
QByteArray encodedWords(const QString &text, int usedOnFirstLine)
{
    const auto bytesFor = [](int wordLen) { return qMax(3, ((wordLen - 12) / 4) * 3); };
    QByteArray out;
    QByteArray chunk;
    int limit = bytesFor(qMax(kFoldAt - usedOnFirstLine, 24));
    const auto flush = [&]() {
        if (chunk.isEmpty()) return;
        if (!out.isEmpty()) out += "\r\n ";
        out += "=?UTF-8?B?" + chunk.toBase64() + "?=";
        chunk.clear();
        limit = bytesFor(kFoldAt - 1);
    };
    const QList<uint> ucs = text.toUcs4();
    for (const uint cp : ucs) {
        const char32_t one = char32_t(cp);
        const QByteArray utf8 = QString::fromUcs4(&one, 1).toUtf8();
        if (chunk.size() + utf8.size() > limit) flush();
        chunk += utf8;
    }
    flush();
    return out;
}

// Plain ASCII folded at its spaces so no line passes 76 where a space allows it. Empty when a single
// unbroken run would pass 998, for the caller to encode instead.
QByteArray foldedAscii(const QString &text, int usedOnFirstLine)
{
    const QByteArray a = text.toLatin1();
    if (usedOnFirstLine + a.size() <= kFoldAt) return a;
    QByteArray out;
    int lineLen = usedOnFirstLine;
    const QList<QByteArray> words = a.split(' ');
    for (int i = 0; i < words.size(); ++i) {
        const QByteArray &w = words[i];
        if (w.size() > 990) return {};
        if (i == 0) { out += w; lineLen += int(w.size()); continue; }
        if (lineLen + 1 + w.size() > kFoldAt && lineLen > 1) {
            out += "\r\n " + w;            // the space this word followed becomes the folding white space
            lineLen = 1 + int(w.size());
        } else {
            out += ' ' + w;
            lineLen += 1 + int(w.size());
        }
    }
    return out;
}

// "Name <addr>", the name quoted or encoded as it needs; the address moved to a line of its own when the
// name's last line would run long.
QByteArray mailbox(const QString &name, const QString &address, int used)
{
    const QByteArray addr = addressSafe(address).toLatin1();
    const QString n = headerSafe(name);
    if (n.isEmpty()) return addr;
    QByteArray phrase;
    if (isAsciiPrintable(n) && !n.contains(QStringLiteral("=?"))) {
        static const QString specials = QStringLiteral("()<>[]:;@\\,.\"");
        bool needsQuote = false;
        for (const QChar c : n) if (specials.contains(c)) { needsQuote = true; break; }
        if (needsQuote) {
            QString q = n;
            q.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
            phrase = '"' + q.toLatin1() + '"';
        } else {
            phrase = n.toLatin1();
        }
    } else {
        phrase = encodedWords(n, used);
    }
    const int lastNl = int(phrase.lastIndexOf('\n'));
    const int lastLen = lastNl < 0 ? used + int(phrase.size()) : int(phrase.size()) - lastNl - 1;
    return phrase + (lastLen + 2 + addr.size() + 1 > kFoldAt ? "\r\n <" : " <") + addr + '>';
}

QByteArray addressList(const QStringList &addresses, int used)
{
    QByteArray out;
    int lineLen = used;
    for (const QString &a : addresses) {
        const QByteArray one = addressSafe(a).toLatin1();
        if (one.isEmpty()) continue;
        if (!out.isEmpty()) {
            if (lineLen + 2 + one.size() > kFoldAt) { out += ",\r\n "; lineLen = 1; }
            else                                    { out += ", ";     lineLen += 2; }
        }
        out += one;
        lineLen += int(one.size());
    }
    return out;
}

QByteArray rfc5322Date(const QDateTime &date)
{
    const int off = date.offsetFromUtc();
    const int a = qAbs(off) / 60;
    const QString zone = QStringLiteral("%1%2%3").arg(off < 0 ? QLatin1Char('-') : QLatin1Char('+'))
                             .arg(a / 60, 2, 10, QLatin1Char('0')).arg(a % 60, 2, 10, QLatin1Char('0'));
    return (QLocale::c().toString(date, QStringLiteral("ddd, dd MMM yyyy HH:mm:ss")) + QLatin1Char(' ') + zone).toLatin1();
}

void header(QByteArray &out, const char *name, const QByteArray &value)
{
    out += name;
    out += ": ";
    out += value;
    out += "\r\n";
}

// RFC 2231 attr-chars pass; every other byte is %XX.
QByteArray percent2231(const QByteArray &utf8)
{
    static const QByteArray keep = "!#$&+-.^_`|~";
    QByteArray out;
    for (const char ch : utf8) {
        const uchar b = uchar(ch);
        if ((b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') || (b >= '0' && b <= '9') || keep.contains(char(b)))
            out += char(b);
        else
            out += '%' + QByteArray::number(b, 16).toUpper().rightJustified(2, '0');
    }
    return out;
}

// filename="x.pdf", or filename*=UTF-8''… split into numbered sections of at most 60 characters,
// one to a line, never splitting a %XX.
QByteArray filenameParameter(const QString &name)
{
    if (isAsciiPrintable(name)) {
        QString q = name;
        q.replace(QLatin1Char('"'), QLatin1Char('_')).replace(QLatin1Char('\\'), QLatin1Char('_'));
        return "filename=\"" + q.toLatin1() + '"';
    }
    const QByteArray enc = "UTF-8''" + percent2231(name.toUtf8());
    if (enc.size() <= 60) return "filename*=" + enc;
    QList<QByteArray> parts;
    QByteArray cur;
    for (int i = 0; i < enc.size();) {
        const int step = enc.at(i) == '%' ? 3 : 1;
        if (cur.size() + step > 60) { parts << cur; cur.clear(); }
        cur += enc.mid(i, step);
        i += step;
    }
    if (!cur.isEmpty()) parts << cur;
    QByteArray out;
    for (int i = 0; i < parts.size(); ++i) {
        if (i) out += ";\r\n ";
        out += "filename*" + QByteArray::number(i) + "*=" + parts[i];
    }
    return out;
}

} // namespace

QString headerSafe(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (qsizetype i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        const ushort u = c.unicode();
        if (u == '\r' && i + 1 < s.size() && s.at(i + 1) == QLatin1Char('\n')) continue;   // CRLF is one break
        if (u == '\r' || u == '\n' || u == '\t') out += QLatin1Char(' ');
        else if (u >= 0x20 && u != 0x7f) out += c;
    }
    return out.trimmed();
}

QByteArray encodeHeaderText(const QString &raw, int usedOnFirstLine)
{
    const QString text = headerSafe(raw);
    if (isAsciiPrintable(text) && !text.contains(QStringLiteral("=?"))) {
        const QByteArray folded = foldedAscii(text, usedOnFirstLine);
        if (!folded.isEmpty() || text.isEmpty()) return folded;
    }
    return encodedWords(text, usedOnFirstLine);
}

bool isPlausibleAddress(const QString &a)
{
    if (a.isEmpty() || a.size() > 254) return false;
    const qsizetype at = a.indexOf(QLatin1Char('@'));
    if (at <= 0 || at != a.lastIndexOf(QLatin1Char('@'))) return false;
    static const QString banned = QStringLiteral("<>\"(),;:\\[]`");
    for (const QChar c : a) {
        const ushort u = c.unicode();
        if (u <= 0x20 || u >= 0x7f || banned.contains(c)) return false;   // ASCII only: no SMTPUTF8 here
    }
    const QString local = a.left(at);
    const QString domain = a.mid(at + 1);
    if (local.size() > 64 || local.startsWith(QLatin1Char('.')) || local.endsWith(QLatin1Char('.'))
        || local.contains(QStringLiteral("..")))
        return false;
    const QStringList labels = domain.split(QLatin1Char('.'));
    if (labels.size() < 2 || labels.last().size() < 2) return false;
    for (const QString &l : labels) {
        if (l.isEmpty() || l.size() > 63 || l.startsWith(QLatin1Char('-')) || l.endsWith(QLatin1Char('-'))) return false;
        for (const QChar c : l)
            if (!(c.isLetterOrNumber() || c == QLatin1Char('-'))) return false;
    }
    return true;
}

QString makeMessageId(const QString &fromAddress)
{
    QString domain;
    const qsizetype at = fromAddress.lastIndexOf(QLatin1Char('@'));
    if (at >= 0)
        for (const QChar c : fromAddress.mid(at + 1))
            if (c.isLetterOrNumber() || c == QLatin1Char('.') || c == QLatin1Char('-')) domain += c;
    if (domain.isEmpty() || !domain.contains(QLatin1Char('.'))) domain = QStringLiteral("pinpoint.invalid");
    return QLatin1Char('<') + QUuid::createUuid().toString(QUuid::WithoutBraces) + QLatin1Char('@') + domain + QLatin1Char('>');
}

QByteArray quotedPrintable(const QString &text)
{
    QString t = text;
    t.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
    QStringList lines = t.split(QLatin1Char('\n'));
    if (lines.size() > 1 && lines.last().isEmpty()) lines.removeLast();   // a final newline is not one more line

    static const char hex[] = "0123456789ABCDEF";
    QByteArray out;
    for (const QString &line : lines) {
        const QByteArray b = line.toUtf8();
        int col = 0;
        for (int i = 0; i < b.size(); ++i) {
            const uchar c = uchar(b.at(i));
            const bool last = (i == b.size() - 1);
            bool literal = (c >= 33 && c <= 126 && c != '=') || ((c == ' ' || c == '\t') && !last);
            // A leading '.' (SMTP transparency) and a leading "From " (mbox munging) survive any relay encoded.
            if (i == 0 && (c == '.' || (c == 'F' && b.startsWith("From ")))) literal = false;
            QByteArray token;
            if (literal) token = QByteArray(1, char(c));
            else { token = "="; token += hex[c >> 4]; token += hex[c & 0xf]; }
            if (col + token.size() > kFoldAt - 1) { out += "=\r\n"; col = 0; }   // soft break, line ≤ 76 with its '='
            if (col == 0 && token == ".") token = "=2E";                           // nor after a soft break
            out += token;
            col += int(token.size());
        }
        out += "\r\n";
    }
    return out;
}

QByteArray base64Lines(const QByteArray &bytes)
{
    const QByteArray b64 = bytes.toBase64();
    QByteArray out;
    out.reserve(b64.size() + (b64.size() / 76 + 1) * 2);
    for (qsizetype i = 0; i < b64.size(); i += 76) {
        out += b64.mid(i, 76);
        out += "\r\n";
    }
    return out;
}

QByteArray buildMime(const MailMessage &m, const QDateTime &date, const QString &messageId)
{
    QByteArray out;
    const QString from = addressSafe(m.fromAddress);
    header(out, "Date", rfc5322Date(date));
    header(out, "From", mailbox(m.fromName, from, 6));
    header(out, "To", addressList(m.to, 4));
    const QString replyTo = addressSafe(m.replyTo);
    if (!replyTo.isEmpty() && replyTo.compare(from, Qt::CaseInsensitive) != 0)
        header(out, "Reply-To", replyTo.toLatin1());
    header(out, "Message-ID", addressSafe(messageId).toLatin1());
    header(out, "Subject", encodeHeaderText(m.subject, 9));
    header(out, "MIME-Version", "1.0");

    const QByteArray text = quotedPrintable(m.body);
    if (m.attachmentName.isEmpty() && m.attachment.isEmpty()) {
        header(out, "Content-Type", "text/plain; charset=UTF-8");
        header(out, "Content-Transfer-Encoding", "quoted-printable");
        out += "\r\n";
        out += text;
        return out;
    }

    // "=_" cannot occur in quoted-printable (an '=' is followed by hex or a line break) or in base64.
    const QByteArray boundary = "=_pps_" + QUuid::createUuid().toString(QUuid::Id128).toLatin1();
    header(out, "Content-Type", "multipart/mixed;\r\n boundary=\"" + boundary + '"');
    out += "\r\nThis is a multi-part message in MIME format.\r\n";

    out += "\r\n--" + boundary + "\r\n";
    header(out, "Content-Type", "text/plain; charset=UTF-8");
    header(out, "Content-Transfer-Encoding", "quoted-printable");
    out += "\r\n";
    out += text;   // ends in CRLF, which the next delimiter claims

    QString name = headerSafe(m.attachmentName);
    if (name.isEmpty()) name = QStringLiteral("attachment");
    QByteArray mime = addressSafe(m.attachmentMime).toLatin1();
    if (mime.isEmpty() || !mime.contains('/')) mime = "application/octet-stream";
    QByteArray nameParam;
    if (isAsciiPrintable(name)) {
        QString q = name;
        q.replace(QLatin1Char('"'), QLatin1Char('_')).replace(QLatin1Char('\\'), QLatin1Char('_'));
        nameParam = "name=\"" + q.toLatin1() + '"';
    } else {
        nameParam = "name=\"=?UTF-8?B?" + name.toUtf8().toBase64() + "?=\"";
    }
    out += "--" + boundary + "\r\n";
    header(out, "Content-Type", mime + ";\r\n " + nameParam);
    header(out, "Content-Transfer-Encoding", "base64");
    header(out, "Content-Disposition", "attachment;\r\n " + filenameParameter(name));
    out += "\r\n";
    out += base64Lines(m.attachment);
    out += "--" + boundary + "--\r\n";
    return out;
}

QByteArray dotStuff(const QByteArray &data)
{
    QByteArray out;
    out.reserve(data.size() + data.size() / 1000 + 8);
    bool lineStart = true;
    for (const char c : data) {
        if (lineStart && c == '.') out += '.';
        out += c;
        lineStart = (c == '\n');
    }
    return out;
}

} // namespace pinpoint::report
