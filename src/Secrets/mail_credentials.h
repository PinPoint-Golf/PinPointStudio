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
// mail login's password (an app password for Gmail and iCloud), kept in the credential store
// (secret_store.h) and never in the INI: the login keychain on macOS, Windows Credential Manager. Linux is
// not a studio platform and has no store every desktop shares, so there it sits in the settings file, and
// that file says so (secret_store_linux.cpp). It is the store's first client.
//
// `account` is "username@host" — one password per login per server, so a password is never offered to
// a server it was not typed for.
//
// Its names on each platform are the ones it had before the store existed (moved from src/Gui/report,
// 26 Nov 2026), so a password a coach saved before is still found: service "ReportSending", shown as
// "PinPoint Studio report sending" (secret_store.h, nativeName(); secret_store_test pins them).
//
// PINPOINT_MAIL_CREDENTIALS=memory, or PINPOINT_MAIL_DRYRUN=1, keeps them in a map in this process instead
// (the store's in-memory test mode, secret_store.cpp): tests and probes never read or write the real keychain.

#include <QString>

namespace pinpoint::secrets { struct Service; }

namespace pinpoint::report {

QString mailAccount(const QString &username, const QString &host);   // "" when either is empty

bool    storeMailPassword(const QString &account, const QString &password, QString *error = nullptr);
QString readMailPassword(const QString &account);                    // "" when there is none
bool    hasMailPassword(const QString &account);                     // without reading the secret
bool    forgetMailPassword(const QString &account);                  // true when none is left (none there counts)

bool    mailCredentialsInMemory();

const pinpoint::secrets::Service &mailPasswordService();             // the store's service for it

} // namespace pinpoint::report
