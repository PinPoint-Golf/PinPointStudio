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

#include "report_mailer.h"

// THE WINDOWS HAND-OFF: Simple MAPI's MAPISendMailW, which the default mail program answers (Outlook,
// Thunderbird, eM Client, Windows' own when set). MAPI_DIALOG opens the message for the coach to read and
// send from their own account — never a silent send (§8.3, option A).
//
// MAPI32.DLL is loaded at run time (LoadLibrary / GetProcAddress): no link-time dependency, and a machine
// with no mail program gets a plain-words reason instead of a missing-DLL failure at start-up.
// MAPISendMailW is Windows 8+; it takes UTF-16 throughout, so names, subject and body need no code-page
// conversion. Declared here from the documented API (MapiMessageW / MapiRecipDescW / MapiFileDescW, MAPI.h)
// rather than including MAPI.h, whose W structs are absent from older SDK headers.
//
// ⚠ WRITTEN ON THE MAC, NOT YET COMPILED OR RUN ON WINDOWS — check it on the cabin PC.
//
// Reply-to: Simple MAPI has no reply-to field; the coach's own mail profile is the sender, so replies go
// to them already.

#include <QDir>
#include <QFileInfo>
#include <QVector>

#include <deque>
#include <string>

#include <windows.h>

namespace {

typedef struct {
    ULONG  ulReserved;
    ULONG  ulRecipClass;
    PWSTR  lpszName;
    PWSTR  lpszAddress;
    ULONG  ulEIDSize;
    LPVOID lpEntryID;
} PpMapiRecipDescW;

typedef struct {
    ULONG  ulReserved;
    ULONG  flFlags;
    ULONG  nPosition;
    PWSTR  lpszPathName;
    PWSTR  lpszFileName;
    LPVOID lpFileType;
} PpMapiFileDescW;

typedef struct {
    ULONG             ulReserved;
    PWSTR             lpszSubject;
    PWSTR             lpszNoteText;
    PWSTR             lpszMessageType;
    PWSTR             lpszDateReceived;
    PWSTR             lpszConversationID;
    ULONG             flFlags;
    PpMapiRecipDescW *lpOriginator;
    ULONG             nRecipCount;
    PpMapiRecipDescW *lpRecips;
    ULONG             nFileCount;
    PpMapiFileDescW  *lpFiles;
} PpMapiMessageW;

typedef ULONG (WINAPI *PpMapiSendMailW)(ULONG_PTR lhSession, ULONG_PTR ulUIParam, PpMapiMessageW *lpMessage,
                                        ULONG flFlags, ULONG ulReserved);

constexpr ULONG kMapiTo           = 1;           // MAPI_TO
constexpr ULONG kMapiLogonUi      = 0x00000001;  // MAPI_LOGON_UI
constexpr ULONG kMapiDialog       = 0x00000008;  // MAPI_DIALOG
constexpr ULONG kSuccess          = 0;           // SUCCESS_SUCCESS
constexpr ULONG kUserAbort        = 1;           // MAPI_USER_ABORT
constexpr ULONG kLoginFailure     = 3;           // MAPI_E_LOGIN_FAILURE
constexpr ULONG kNotSupported     = 26;          // MAPI_E_NOT_SUPPORTED

// A QString's UTF-16, kept alive in a deque the MAPI structs point into.
PWSTR wide(std::deque<std::wstring> &keep, const QString &s)
{
    keep.push_back(s.toStdWString());
    return const_cast<PWSTR>(keep.back().c_str());
}

} // namespace

namespace pinpoint::report {

QString platformCompose(const QStringList &to, const QString &replyTo, const QString &subject,
                        const QString &body, const QString &filePath)
{
    Q_UNUSED(replyTo);
    HMODULE mapi = ::LoadLibraryW(L"MAPI32.DLL");
    if (!mapi)
        return QStringLiteral("No mail program is set up on this PC. Use Copy address and Show in Explorer "
                              "to attach the report by hand.");
    auto send = reinterpret_cast<PpMapiSendMailW>(::GetProcAddress(mapi, "MAPISendMailW"));
    if (!send) {
        ::FreeLibrary(mapi);
        return QStringLiteral("This PC's mail program cannot be handed a report. Use Copy address and Show "
                              "in Explorer to attach it by hand.");
    }

    // A deque: the structs keep raw pointers into these strings, and a deque never moves an element
    // it already holds when another is added.
    std::deque<std::wstring> keep;

    QVector<PpMapiRecipDescW> recips;
    recips.reserve(to.size());
    for (const QString &a : to) {
        PpMapiRecipDescW r = {};
        r.ulRecipClass = kMapiTo;
        r.lpszName     = wide(keep, a);
        r.lpszAddress  = wide(keep, QStringLiteral("SMTP:") + a);
        recips.append(r);
    }

    PpMapiFileDescW file = {};
    file.nPosition    = ULONG(-1);                   // not placed in the body text
    file.lpszPathName = wide(keep, QDir::toNativeSeparators(filePath));
    file.lpszFileName = wide(keep, QFileInfo(filePath).fileName());

    PpMapiMessageW msg = {};
    msg.lpszSubject  = wide(keep, subject);
    msg.lpszNoteText = wide(keep, body);
    msg.nRecipCount  = ULONG(recips.size());
    msg.lpRecips     = recips.data();
    msg.nFileCount   = 1;
    msg.lpFiles      = &file;

    // ulUIParam 0: the dialog is parented to no window (the app's own is a Qt Quick window MAPI does not
    // know about); MAPI_DIALOG keeps it modal to the mail program itself.
    const ULONG rc = send(0, 0, &msg, kMapiDialog | kMapiLogonUi, 0);
    ::FreeLibrary(mapi);

    switch (rc) {
    case kSuccess:
    case kUserAbort:            // the coach closed the message: the hand-off itself worked
        return {};
    case kLoginFailure:
        return QStringLiteral("The mail program could not sign in to an account. Use Copy address and Show "
                              "in Explorer to attach the report by hand.");
    case kNotSupported:
        return QStringLiteral("The default mail program does not accept reports handed to it. Use Copy "
                              "address and Show in Explorer to attach it by hand.");
    default:
        return QStringLiteral("The mail program could not open a new message (MAPI error %1). Use Copy "
                              "address and Show in Explorer to attach the report by hand.").arg(rc);
    }
}

} // namespace pinpoint::report
