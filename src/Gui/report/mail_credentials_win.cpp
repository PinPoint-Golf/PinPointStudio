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

#include "mail_credentials.h"

// THE WINDOWS STORE: Credential Manager (CredWriteW / CredReadW / CredDeleteW), a generic credential with
// target "PinPointStudio/ReportSending/<username@host>", user name the account, persisted for this user on
// this machine (CRED_PERSIST_LOCAL_MACHINE: not roamed to other PCs). Control Panel → Credential Manager →
// Windows Credentials lists it under that target, so a coach can see and remove it there too.
//
// The secret is the password's UTF-8 bytes (at most CRED_MAX_CREDENTIAL_BLOB_SIZE, 2560 — far beyond any
// app password). Advapi32, linked in CMakeLists.txt and named below for MSVC.
//
// ⚠ WRITTEN ON THE MAC, NOT YET COMPILED OR RUN ON WINDOWS — check it on the cabin PC.

#include <QByteArray>

#include <string>

#include <windows.h>
#include <wincred.h>

#if defined(_MSC_VER)
#pragma comment(lib, "advapi32.lib")
#endif

namespace pinpoint::report {

namespace {

std::wstring target(const QString &account)
{
    return (QStringLiteral("PinPointStudio/ReportSending/") + account).toStdWString();
}

QString lastErrorWords(DWORD code)
{
    wchar_t *buf = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, code, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    QString why = (n && buf) ? QString::fromWCharArray(buf, int(n)).trimmed() : QString();
    if (buf) LocalFree(buf);
    return why.isEmpty() ? QStringLiteral("error %1").arg(code) : why;
}

} // namespace

bool platformStoreMailPassword(const QString &account, const QString &password, QString *error)
{
    QByteArray secret = password.toUtf8();
    if (secret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
        if (error) *error = QStringLiteral("That password is too long to keep.");
        return false;
    }
    std::wstring t = target(account);
    std::wstring user = account.toStdWString();

    CREDENTIALW cred = {};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = t.data();
    cred.CredentialBlobSize = DWORD(secret.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(secret.data());
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.UserName = user.data();

    const BOOL ok = CredWriteW(&cred, 0);
    const DWORD code = ok ? 0 : GetLastError();
    secret.fill('\0');
    if (!ok) {
        if (error) *error = QStringLiteral("The password could not be saved in Credential Manager (%1).").arg(lastErrorWords(code));
        return false;
    }
    return true;
}

QString platformReadMailPassword(const QString &account)
{
    const std::wstring t = target(account);
    PCREDENTIALW cred = nullptr;
    if (!CredReadW(t.c_str(), CRED_TYPE_GENERIC, 0, &cred) || !cred) return {};
    QString pw;
    if (cred->CredentialBlob && cred->CredentialBlobSize > 0)
        pw = QString::fromUtf8(reinterpret_cast<const char *>(cred->CredentialBlob), int(cred->CredentialBlobSize));
    CredFree(cred);
    return pw;
}

bool platformHasMailPassword(const QString &account)
{
    // CredReadW is the only lookup by target; it reads the blob too, but asks nothing of the user.
    const std::wstring t = target(account);
    PCREDENTIALW cred = nullptr;
    if (!CredReadW(t.c_str(), CRED_TYPE_GENERIC, 0, &cred) || !cred) return false;
    const bool has = cred->CredentialBlobSize > 0;
    CredFree(cred);
    return has;
}

bool platformForgetMailPassword(const QString &account)
{
    const std::wstring t = target(account);
    if (CredDeleteW(t.c_str(), CRED_TYPE_GENERIC, 0)) return true;
    return GetLastError() == ERROR_NOT_FOUND;
}

} // namespace pinpoint::report
