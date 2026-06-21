/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "linux_update_logic.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

namespace pp::update::linux_logic {

int postfixRank(const QString &p)
{
    if (p.isEmpty())                           return 100;
    const QString s = p.toLower();
    if (s.startsWith(QStringLiteral("rc")))    return 30;
    if (s.startsWith(QStringLiteral("beta")))  return 20;
    if (s.startsWith(QStringLiteral("alpha"))) return 10;
    return 0;
}

int compareVersion(const QString &a, const QString &b)
{
    static const QRegularExpression re(
        QStringLiteral("^v?(\\d+)\\.(\\d+)(?:[-.]?([a-zA-Z]+)(\\d*))?"));
    const auto ma = re.match(a);
    const auto mb = re.match(b);
    if (!ma.hasMatch() || !mb.hasMatch())
        return 0;
    const int amaj = ma.captured(1).toInt(), amin = ma.captured(2).toInt();
    const int bmaj = mb.captured(1).toInt(), bmin = mb.captured(2).toInt();
    if (amaj != bmaj) return amaj < bmaj ? -1 : 1;
    if (amin != bmin) return amin < bmin ? -1 : 1;
    const int ar = postfixRank(ma.captured(3)), br = postfixRank(mb.captured(3));
    if (ar != br) return ar < br ? -1 : 1;
    const int an = ma.captured(4).toInt(), bn = mb.captured(4).toInt();
    if (an != bn) return an < bn ? -1 : 1;
    return 0;
}

QString versionFromAssetName(const QString &name, const QString &archToken)
{
    if (archToken.isEmpty())
        return QString();   // unknown arch → never match (offer nothing)
    // ^PinPointStudio-(.+)-<archToken>\.AppImage$   (archToken is alnum but escape
    // it defensively so a future token with regex metachars cannot misbehave).
    const QRegularExpression re(
        QStringLiteral("^PinPointStudio-(.+)-%1\\.AppImage$")
            .arg(QRegularExpression::escape(archToken)));
    const auto m = re.match(name);
    return m.hasMatch() ? m.captured(1) : QString();
}

FeedPick pickLatestAppImage(const QJsonArray &releases, const QString &archToken)
{
    FeedPick pick;

    for (const QJsonValue &v : releases) {
        const QJsonObject rel = v.toObject();
        if (rel.value(QStringLiteral("draft")).toBool())
            continue;   // unpublished — never offer

        const QJsonArray assets = rel.value(QStringLiteral("assets")).toArray();

        // Locate a same-arch AppImage asset in this release (if any).
        QString appName, appVersion;
        for (const QJsonValue &av : assets) {
            const QString n = av.toObject().value(QStringLiteral("name")).toString();
            const QString ver = versionFromAssetName(n, archToken);
            if (!ver.isEmpty()) {
                appName    = n;
                appVersion = ver;
                break;
            }
        }
        if (appName.isEmpty())
            continue;   // Windows-only / asset-less / other-arch — keep scanning down

        // Match the detached signature to THIS AppImage (<appName>.sig).
        QString sigUrl;
        const QString wantSig = appName + QStringLiteral(".sig");
        for (const QJsonValue &av : assets) {
            const QJsonObject a = av.toObject();
            if (a.value(QStringLiteral("name")).toString() == wantSig) {
                sigUrl = a.value(QStringLiteral("browser_download_url")).toString();
                break;
            }
        }

        pick.appImageName = appName;
        pick.sigUrl       = sigUrl;
        pick.notes        = rel.value(QStringLiteral("body")).toString();
        pick.version      = appVersion;
        pick.found        = true;
        return pick;
    }

    return pick;   // found == false
}

bool isTrustedValidSig(const QString &gpgStatusOutput, const QString &pinnedFpr)
{
    // gpg --status-fd emits: "[GNUPG:] VALIDSIG <40-hex-fpr> <date> ...".
    static const QRegularExpression validRe(
        QStringLiteral("\\[GNUPG:\\] VALIDSIG ([0-9A-Fa-f]{40})"));
    const auto m = validRe.match(gpgStatusOutput);
    if (!m.hasMatch())
        return false;
    return m.captured(1).compare(pinnedFpr, Qt::CaseInsensitive) == 0;
}

bool isPlaceholderFingerprint(const QString &fpr)
{
    return fpr == QStringLiteral("0000000000000000000000000000000000000000");
}

} // namespace pp::update::linux_logic
