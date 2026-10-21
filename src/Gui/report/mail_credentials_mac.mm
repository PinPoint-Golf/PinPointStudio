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

// THE MAC STORE: a generic-password item in the LOGIN keychain — service "PinPoint Studio report
// sending", account "username@host" — through Security.framework's SecItem calls. Keychain Access shows
// it under that name, so a coach can find it and delete it there too.
//
// NOT the data-protection keychain (kSecUseDataProtectionKeychain): that one needs a keychain-access-group
// entitlement from a signing team, and the dev bundle is signed with a self-signed "PinPoint Dev" identity
// that has no team. The login keychain's access list is the app's own code signature, which that identity
// keeps stable across rebuilds, so the app reads its item back without a prompt.
//
// hasPassword asks whether the item exists without asking for its data, which never prompts.
// Core Foundation only, no Objective-C objects, so it needs no ARC.

#include <QByteArray>

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

namespace pinpoint::report {

namespace {

const QString kService = QStringLiteral("PinPoint Studio report sending");

QString statusWords(OSStatus status)
{
    QString why;
    if (CFStringRef s = SecCopyErrorMessageString(status, nullptr)) {
        why = QString::fromCFString(s);
        CFRelease(s);
    }
    return why.isEmpty() ? QStringLiteral("error %1").arg(int(status)) : why;
}

// kSecClass, kSecAttrService and kSecAttrAccount — the item's identity. The caller adds to it and
// releases it.
CFMutableDictionaryRef itemQuery(const QString &account)
{
    CFMutableDictionaryRef q = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                                         &kCFTypeDictionaryValueCallBacks);
    CFStringRef service = kService.toCFString();
    CFStringRef acct = account.toCFString();
    CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(q, kSecAttrService, service);
    CFDictionarySetValue(q, kSecAttrAccount, acct);
    CFRelease(service);
    CFRelease(acct);
    return q;
}

} // namespace

bool platformStoreMailPassword(const QString &account, const QString &password, QString *error)
{
    CFDataRef secret = password.toUtf8().toCFData();

    // An item already there is updated in place; otherwise one is added.
    CFMutableDictionaryRef query = itemQuery(account);
    CFMutableDictionaryRef change = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                                              &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(change, kSecValueData, secret);
    OSStatus status = SecItemUpdate(query, change);
    CFRelease(change);

    if (status == errSecItemNotFound) {
        CFStringRef label = kService.toCFString();
        CFDictionarySetValue(query, kSecValueData, secret);
        CFDictionarySetValue(query, kSecAttrLabel, label);
        status = SecItemAdd(query, nullptr);
        CFRelease(label);
    }
    CFRelease(query);
    CFRelease(secret);

    if (status != errSecSuccess) {
        if (error) *error = QStringLiteral("The password could not be saved in the keychain (%1).").arg(statusWords(status));
        return false;
    }
    return true;
}

QString platformReadMailPassword(const QString &account)
{
    CFMutableDictionaryRef query = itemQuery(account);
    CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef result = nullptr;
    const OSStatus status = SecItemCopyMatching(query, &result);
    CFRelease(query);
    if (status != errSecSuccess || !result) return {};
    QString pw;
    if (CFGetTypeID(result) == CFDataGetTypeID()) pw = QString::fromUtf8(QByteArray::fromCFData(CFDataRef(result)));
    CFRelease(result);
    return pw;
}

bool platformHasMailPassword(const QString &account)
{
    CFMutableDictionaryRef query = itemQuery(account);
    CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
    const OSStatus status = SecItemCopyMatching(query, nullptr);
    CFRelease(query);
    return status == errSecSuccess;
}

bool platformForgetMailPassword(const QString &account)
{
    CFMutableDictionaryRef query = itemQuery(account);
    const OSStatus status = SecItemDelete(query);
    CFRelease(query);
    return status == errSecSuccess || status == errSecItemNotFound;
}

} // namespace pinpoint::report
