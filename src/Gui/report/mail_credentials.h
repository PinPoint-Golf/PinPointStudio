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

// THE PASSWORD FOR SENDING REPORTS (docs/design/session_report_design.md §8.3, option B) — the coach's
// mail login's password (an app password for Gmail and iCloud), kept in the system's secure store and
// never in the INI: the login keychain on macOS (mail_credentials_mac.mm), Windows Credential Manager
// (mail_credentials_win.cpp). Linux is not a studio platform and has no store every desktop shares, so
// there it sits in the settings file, and that file says so (mail_credentials_linux.cpp).
//
// `account` is "username@host" — one password per login per server, so a password is never offered to
// a server it was not typed for.
//
// PINPOINT_MAIL_CREDENTIALS=memory, or PINPOINT_MAIL_DRYRUN=1, keeps them in a map in this process instead:
// tests and probes never read or write the real keychain. The switch is here (mail_credentials.cpp, which
// is portable) so a test links that file alone and supplies the platform half itself.

#include <QString>

namespace pinpoint::report {

QString mailAccount(const QString &username, const QString &host);   // "" when either is empty

bool    storeMailPassword(const QString &account, const QString &password, QString *error = nullptr);
QString readMailPassword(const QString &account);                    // "" when there is none
bool    hasMailPassword(const QString &account);                     // without reading the secret
bool    forgetMailPassword(const QString &account);                  // true when none is left (none there counts)

bool    mailCredentialsInMemory();

// The platform half, one file each. Same contracts as above.
bool    platformStoreMailPassword(const QString &account, const QString &password, QString *error);
QString platformReadMailPassword(const QString &account);
bool    platformHasMailPassword(const QString &account);
bool    platformForgetMailPassword(const QString &account);

} // namespace pinpoint::report
