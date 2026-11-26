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

// The credential store (src/Secrets/secret_store.h) and its first client, the report-sending password
// (src/Secrets/mail_credentials.h).
//
//   the names        the report-sending password's names on all three platforms are the ones its own files
//                    used before the store existed, typed out here: a password a coach saved is still found.
//   an old password  written under exactly those old names into the stand-in store, then read back, changed
//                    and removed through the new store.
//   read once        a secret read or written is held: later reads and "has" never ask the store again;
//                    "has" on one not held asks for existence only, never the secret; an empty read and a
//                    failed write are not held.
//   the test mode    PINPOINT_SECRETS=memory, PINPOINT_MAIL_CREDENTIALS=memory and PINPOINT_MAIL_DRYRUN=1
//                    each keep secrets in memory: a round trip, services kept apart, and the platform's
//                    store never reached.
//
// NOTHING HERE REACHES A REAL STORE: no platform file is linked. The platform half below is the test's own,
// a map in this process that stands in for the keychain, Credential Manager or settings file, and counts
// every call. Per-user stores point at a scratch folder (IsolatedStores). Silent.

#include "../secret_store.h"
#include "../mail_credentials.h"

#include <QHash>
#include <QStringList>

#include <cstdio>
#include "../../Core/tests/isolated_stores.h"   // pinpoint::test::IsolatedStores (#187)

using namespace pinpoint::secrets;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

// ── The stand-in store: the platform half ───────────────────────────────────────────────────────
// Keyed by every platform's name at once, so an item is found only when all three match what the old
// files used.

static QString itemKey(const QString &macService, const QString &macAccount, const QString &winTarget,
                       const QString &winUser, const QString &linuxSecretKey, const QString &linuxAccountKey,
                       const QString &account)
{
    return QStringList{ macService, macAccount, winTarget, winUser, linuxSecretKey, linuxAccountKey, account }
        .join(QChar(0x1f));
}
static QString itemKey(const NativeName &n)
{
    return itemKey(n.macService, n.macAccount, n.windowsTarget, n.windowsUser, n.settingsSecretKey,
                   n.settingsAccountKey, n.account);
}

static QHash<QString, QString> g_items;
static int  g_writes = 0, g_reads = 0, g_has = 0, g_removes = 0;
static bool g_failWrites = false;
static int  calls() { return g_writes + g_reads + g_has + g_removes; }

namespace pinpoint::secrets {
bool platformWriteSecret(const NativeName &n, const QString &secret, QString *error)
{
    ++g_writes;
    if (g_failWrites) {
        if (error) *error = QStringLiteral("refused by the test");
        return false;
    }
    g_items.insert(itemKey(n), secret);
    return true;
}
QString platformReadSecret(const NativeName &n) { ++g_reads; return g_items.value(itemKey(n)); }
bool    platformHasSecret(const NativeName &n)  { ++g_has; return g_items.contains(itemKey(n)); }
bool    platformRemoveSecret(const NativeName &n) { ++g_removes; g_items.remove(itemKey(n)); return true; }
}

// The report-sending password's item exactly as the old files wrote it, for `account` (src/Gui/report/
// mail_credentials_mac.mm, _win.cpp and _linux.cpp at af141e14, typed out here on purpose).
static QString oldMailItem(const QString &account)
{
    return itemKey(QStringLiteral("PinPoint Studio report sending"), account,
                   QStringLiteral("PinPointStudio/ReportSending/") + account, account,
                   QStringLiteral("ReportSending/password"), QStringLiteral("ReportSending/passwordAccount"),
                   account);
}

static void memoryModeOff()
{
    qunsetenv("PINPOINT_SECRETS");
    qunsetenv("PINPOINT_MAIL_CREDENTIALS");
    qunsetenv("PINPOINT_MAIL_DRYRUN");
}

int main()
{
    pinpoint::test::IsolatedStores stores;   // #187: every per-user store points at a scratch folder, first
    namespace R = pinpoint::report;
    const QString account = QStringLiteral("coach@gmail.com@smtp.gmail.com");

    std::printf("the names\n");
    {
        const NativeName n = nativeName(R::mailPasswordService(), account);
        check(n.macService == QLatin1String("PinPoint Studio report sending") && n.macAccount == account,
              "macOS: service \"PinPoint Studio report sending\", account username@host (the old item)");
        check(n.windowsTarget == QLatin1String("PinPointStudio/ReportSending/coach@gmail.com@smtp.gmail.com")
                  && n.windowsUser == account,
              "Windows: target PinPointStudio/ReportSending/<username@host>, user name the account (the old credential)");
        check(n.settingsSecretKey == QLatin1String("ReportSending/password")
                  && n.settingsAccountKey == QLatin1String("ReportSending/passwordAccount") && n.account == account,
              "Linux: ReportSending/password and ReportSending/passwordAccount (the old keys)");
        check(R::mailAccount(QStringLiteral(" coach@gmail.com "), QStringLiteral(" SMTP.Gmail.com ")) == account,
              "mailAccount: the user name trimmed, the host trimmed and lower-cased, as before");
        check(R::mailAccount(QString(), QStringLiteral("smtp.gmail.com")).isEmpty()
                  && R::mailAccount(QStringLiteral("coach"), QString()).isEmpty(),
              "mailAccount: empty when either half is");
    }

    std::printf("an old password\n");
    memoryModeOff();
    check(!R::mailCredentialsInMemory() && !secretsInMemory(), "no switch set: the platform half (the stand-in) is used");
    {
        g_items.insert(oldMailItem(account), QStringLiteral("abcdefghijklmnop"));
        check(R::readMailPassword(R::mailAccount(QStringLiteral("coach@gmail.com"), QStringLiteral("smtp.gmail.com")))
                  == QLatin1String("abcdefghijklmnop"),
              "a password saved under the old names is read back through the new store");
        check(R::hasMailPassword(account), "...and the mailer sees it as saved");

        QString err;
        check(R::storeMailPassword(account, QStringLiteral("qrstuvwxyzabcdef"), &err) && err.isEmpty(), "a changed password is saved");
        check(g_items.value(oldMailItem(account)) == QLatin1String("qrstuvwxyzabcdef") && g_items.size() == 1,
              "...in the same item, under the old names: no second copy");
        check(R::forgetMailPassword(account) && !g_items.contains(oldMailItem(account)), "forgetting it removes that item");
        check(!R::hasMailPassword(account) && R::readMailPassword(account).isEmpty(), "...and it is gone");
    }

    std::printf("read once\n");
    {
        const Service svc{ QStringLiteral("TestService"), QStringLiteral("PinPoint Studio test service") };
        const QString acct = QStringLiteral("azure");
        g_items.insert(itemKey(nativeName(svc, acct)), QStringLiteral("key-1"));
        const int reads0 = g_reads, has0 = g_has;
        check(readSecret(svc, acct) == QLatin1String("key-1"), "the first read asks the store");
        check(readSecret(svc, acct) == QLatin1String("key-1") && readSecret(svc, acct) == QLatin1String("key-1")
                  && hasSecret(svc, acct),
              "...later reads and has are answered from memory");
        check(g_reads == reads0 + 1 && g_has == has0, "...so the store was read exactly once");

        check(writeSecret(svc, acct, QStringLiteral("key-2")) && readSecret(svc, acct) == QLatin1String("key-2")
                  && g_reads == reads0 + 1,
              "a write is held: the next read is the new secret, with no read of the store");

        const QString other = QStringLiteral("gemini");
        g_items.insert(itemKey(nativeName(svc, other)), QStringLiteral("g-key"));
        const int r1 = g_reads, h1 = g_has;
        check(hasSecret(svc, other) && g_has == h1 + 1 && g_reads == r1,
              "has on a secret not held asks whether it exists, and never reads it");

        const QString later = QStringLiteral("later");
        check(readSecret(svc, later).isEmpty(), "a secret that is not there reads empty");
        g_items.insert(itemKey(nativeName(svc, later)), QStringLiteral("now-there"));
        check(readSecret(svc, later) == QLatin1String("now-there"), "...and an empty read is not held: the next read asks again");

        check(removeSecret(svc, acct) && !hasSecret(svc, acct) && readSecret(svc, acct).isEmpty(),
              "remove forgets the held secret too");

        g_failWrites = true;
        QString err;
        const QString failing = QStringLiteral("failing");
        check(!writeSecret(svc, failing, QStringLiteral("x"), &err) && err == QLatin1String("refused by the test"),
              "a refused write fails with the store's words");
        g_failWrites = false;
        const int r2 = g_reads;
        check(readSecret(svc, failing).isEmpty() && g_reads == r2 + 1, "...and is not held: the next read asks the store");

        const int w = g_writes;
        check(writeSecret(svc, other, QString()) && !g_items.contains(itemKey(nativeName(svc, other))) && g_writes == w,
              "writing an empty secret removes it");
        err.clear();
        check(!writeSecret(svc, QString(), QStringLiteral("x"), &err) && !err.isEmpty(), "an empty account is refused, with words");
        check(readSecret(svc, QString()).isEmpty() && !hasSecret(svc, QString()) && removeSecret(svc, QString()),
              "...and has nothing to read, has or remove");
        err.clear();
        check(!R::storeMailPassword(QString(), QStringLiteral("x"), &err)
                  && err == QLatin1String("There is no login to keep a password for."),
              "the mail password keeps its own words for no login");
    }

    std::printf("the test mode\n");
    {
        const Service a{ QStringLiteral("ServiceA"), QStringLiteral("A") };
        const Service b{ QStringLiteral("ServiceB"), QStringLiteral("B") };
        const QString acct = QStringLiteral("same-account");
        // Held from the platform's store before the switch: memory mode must not answer from it.
        check(writeSecret(a, QStringLiteral("held"), QStringLiteral("platform-secret")), "(a secret held from the platform's store)");
        const int before = calls();

        qputenv("PINPOINT_SECRETS", "memory");
        check(secretsInMemory(), "PINPOINT_SECRETS=memory: in memory");
        check(readSecret(a, QStringLiteral("held")).isEmpty(), "memory mode never answers from what the platform's store held");
        check(writeSecret(a, acct, QStringLiteral("secret-a")) && writeSecret(b, acct, QStringLiteral("secret-b")),
              "write two services, one account");
        check(readSecret(a, acct) == QLatin1String("secret-a") && readSecret(b, acct) == QLatin1String("secret-b"),
              "...each reads back its own");
        check(hasSecret(a, acct) && removeSecret(a, acct) && !hasSecret(a, acct) && hasSecret(b, acct),
              "remove takes one service's, and leaves the other's");
        check(writeSecret(b, acct, QString()) && !hasSecret(b, acct), "an empty write removes it here too");

        memoryModeOff();
        qputenv("PINPOINT_MAIL_CREDENTIALS", "memory");
        check(R::mailCredentialsInMemory() && R::storeMailPassword(account, QStringLiteral("pass word"))
                  && R::readMailPassword(account) == QLatin1String("pass word") && R::hasMailPassword(account),
              "PINPOINT_MAIL_CREDENTIALS=memory: the mail password round trip");

        memoryModeOff();
        qputenv("PINPOINT_MAIL_DRYRUN", "1");
        check(R::mailCredentialsInMemory() && R::readMailPassword(account) == QLatin1String("pass word")
                  && R::forgetMailPassword(account) && !R::hasMailPassword(account),
              "PINPOINT_MAIL_DRYRUN=1: in memory too (the probes' switch)");
        qputenv("PINPOINT_MAIL_DRYRUN", "0");
        qputenv("PINPOINT_MAIL_CREDENTIALS", "keychain");
        qputenv("PINPOINT_SECRETS", "");
        check(!secretsInMemory(), "any other value: not in memory");

        check(calls() == before, "in memory, the platform's store was never reached");
    }

    std::printf(g_fail ? "\nFAILED: %d\n" : "\nall passed\n", g_fail);
    return g_fail ? 1 : 0;
}
