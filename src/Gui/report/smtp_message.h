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

// THE MESSAGE A REPORT GOES OUT AS (docs/design/session_report_design.md §8.3, option B) — the bytes
// SmtpClient hands the server after DATA. Pure: no network, no settings, no clock (the date and the
// Message-ID are the caller's), so report_smtp_test can read every byte back.
//
// RFC 5322 + MIME, written to be dull: CRLF line endings everywhere and nothing else; no line over 998
// octets (headers are folded to 76 where they can be); 7-bit only, so no server needs 8BITMIME. The body
// is text/plain; charset=UTF-8 in quoted-printable, the attachment base64 in 76-column lines inside a
// multipart/mixed whose boundary starts "=_" — a sequence neither encoding can produce, so the boundary
// can never occur inside a part. With no attachment (the test message) the message is one text part.
//
// NON-ASCII HEADERS — the default subject has an em dash, a coach may be "José" — go as RFC 2047
// encoded-words (=?UTF-8?B?…?=, each at most 75 characters, never splitting a character, folded one to a
// line); the attachment's file name as RFC 2231 (filename*=UTF-8''Jos%C3%A9…) in the disposition and an
// encoded-word in Content-Type's name=, which is what mail programs read in practice.
//
// HEADER INJECTION: every header input has CR, LF and the other control characters taken out before it
// is used, so a subject cannot smuggle in a "Bcc:" line. Addresses are checked with isPlausibleAddress
// by the caller before anything is built. There is NO Bcc header — a Bcc recipient exists only in the
// RCPT TO the client sends.

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>

namespace pinpoint::report {

struct MailMessage {
    QString     fromName;
    QString     fromAddress;
    QStringList to;
    QStringList bcc;                 // RCPT TO only; never written into the message
    QString     replyTo;             // written only when non-empty and not the From address
    QString     subject;
    QString     body;                // plain text; any line ending
    QString     attachmentName;      // empty = no attachment
    QByteArray  attachment;
    QString     attachmentMime = QStringLiteral("application/pdf");
};

// something@something.tld: one '@', no whitespace, no <>, quotes, brackets, commas or control
// characters, a dotted domain with no empty label, at most 254 characters. Not RFC 5321's grammar —
// a gate against typing slips and header injection, which is all a coach's address book needs.
bool isPlausibleAddress(const QString &address);

// The whole message, CRLF throughout, ending in CRLF. `messageId` is "<…@…>" (makeMessageId).
QByteArray buildMime(const MailMessage &m, const QDateTime &date, const QString &messageId);

// "<uuid@domain-of-from>", or "@pinpoint.invalid" when the From address has no domain.
QString makeMessageId(const QString &fromAddress);

// DATA transparency (RFC 5321 §4.5.2): a '.' that starts a line is doubled. The client appends the
// terminating ".\r\n".
QByteArray dotStuff(const QByteArray &data);

// The pieces, for the test and for anyone who needs one header right.
QString    headerSafe(const QString &s);                             // CR/LF/controls out (CR/LF → space)
QByteArray encodeHeaderText(const QString &text, int usedOnFirstLine); // ASCII as is (folded), else encoded-words
QByteArray quotedPrintable(const QString &text);                     // CRLF lines, ≤ 76 columns
QByteArray base64Lines(const QByteArray &bytes);                     // 76 columns, CRLF after each line

} // namespace pinpoint::report
