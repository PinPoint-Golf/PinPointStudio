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

// THE CREDENTIAL STORE: one place for passwords and keys, kept in the system's secure store and never as
// plain text where the system has one (REQ-PRIV-3). A service and an account in, a secret out: write, read,
// has and remove. Ruled by the Solution Architect, 25 Nov 2026 (K1 (a),
// docs/implementation/platform_e_host_settings.md): one store in src/Secrets (Adapters), generalised from the
// report-sending password's, which is its first client (mail_credentials.h). The API keys (SecretsManager)
// are not a client yet; they move in a later step.
//
//   macOS    a generic-password item in the LOGIN keychain (secret_store_mac.mm)
//   Windows  a generic credential in Credential Manager (secret_store_win.cpp)
//   Linux    the settings file, which is NOT a secure store (secret_store_linux.cpp says why)
//
// THE NAMES ON EACH PLATFORM are made in one portable place, nativeName() below, so a secret saved by an
// older build is found by this one: the report-sending password keeps the exact service, target and keys
// it always had (secret_store_test pins them, typed out).
//
// READ ONCE: a secret read successfully, or written, is held in memory for the rest of the run, so a
// request never waits on the keychain. "Has" asks the system whether the item exists without reading its
// data (on the Mac that never prompts). An empty or failed read is not held: the next read asks again.
//
// THE IN-MEMORY TEST MODE: PINPOINT_SECRETS=memory, PINPOINT_MAIL_CREDENTIALS=memory or PINPOINT_MAIL_DRYRUN=1
// keeps every secret in a map in this process instead, read at every call: tests and probes never read or
// write the real keychain, Credential Manager or settings file. The switch is here (secret_store.cpp, which is
// portable) so a test links that file alone and supplies the platform half itself.

#include <QString>

namespace pinpoint::secrets {

// What a secret is for: one per client.
struct Service {
    QString id;     // short and stable, e.g. "ReportSending": Windows' target and Linux's settings group
    QString name;   // what a person reads, e.g. "PinPoint Studio report sending": the Mac item's service and label
};

// Where a secret sits on each platform. All three are made on every platform so one test pins them all.
struct NativeName {
    QString account;              // the account itself (Linux writes it under settingsAccountKey)
    QString macService;           // kSecAttrService and kSecAttrLabel: Service::name
    QString macAccount;           // kSecAttrAccount: the account
    QString windowsTarget;        // CREDENTIALW::TargetName: "PinPointStudio/<id>/<account>"
    QString windowsUser;          // CREDENTIALW::UserName: the account
    QString settingsSecretKey;    // Linux: "<id>/password", the one secret the service keeps
    QString settingsAccountKey;   // Linux: "<id>/passwordAccount", the account it belongs to
};

NativeName nativeName(const Service &service, const QString &account);

// An empty account is refused (write) or has nothing (read, has); writing an empty secret removes it.
bool    writeSecret(const Service &service, const QString &account, const QString &secret, QString *error = nullptr);
QString readSecret(const Service &service, const QString &account);       // "" when there is none
bool    hasSecret(const Service &service, const QString &account);        // without reading the secret
bool    removeSecret(const Service &service, const QString &account);     // true when none is left (none there counts)

bool    secretsInMemory();

// The platform half, one file each (secret_store_mac.mm, _win.cpp, _linux.cpp); a test supplies its own.
// Handed the names nativeName() made. Same contracts as above.
bool    platformWriteSecret(const NativeName &name, const QString &secret, QString *error);
QString platformReadSecret(const NativeName &name);
bool    platformHasSecret(const NativeName &name);
bool    platformRemoveSecret(const NativeName &name);

} // namespace pinpoint::secrets
