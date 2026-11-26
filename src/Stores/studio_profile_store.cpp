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

#include "studio_profile_store.h"

#include "../Core/atomic_write.h"
#include "../Core/pp_debug.h"
#include "../Core/pp_settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLatin1String>
#include <QStandardPaths>
#include <QtGlobal>

namespace {

constexpr auto kGroup = "Studio";

// SET-20's key names (information_model.md SET-20), as every build since the studio profile's first has written them.
const char *keyName(StudioKey key)
{
    switch (key) {
    case StudioKey::Name:          return "name";
    case StudioKey::Logo:          return "logo";
    case StudioKey::Accent:        return "accent";
    case StudioKey::Address:       return "address";
    case StudioKey::Phone:         return "phone";
    case StudioKey::Email:         return "email";
    case StudioKey::Website:       return "website";
    case StudioKey::Booking:       return "booking";
    case StudioKey::PracticeSteps: return "practiceSteps";
    case StudioKey::EmailSubject:  return "emailSubject";
    case StudioKey::EmailBody:     return "emailBody";
    case StudioKey::CoachName:     return "coachName";
    case StudioKey::CoachTitle:    return "coachTitle";
    case StudioKey::CoachEmail:    return "coachEmail";
    case StudioKey::CoachPhone:    return "coachPhone";
    case StudioKey::CoachPhoto:    return "coachPhoto";
    }
    Q_UNREACHABLE_RETURN("");
}

} // namespace

QString StudioProfileStore::studioDir()
{
    const QString env = qEnvironmentVariable("PINPOINT_STUDIO_DIR");
    if (!env.isEmpty()) return QDir::cleanPath(env);
    return QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                           + QStringLiteral("/studio"));
}

// ── the keys ────────────────────────────────────────────────────────────────────────────────────

QString StudioProfileStore::value(StudioKey key) const
{
    QSettings s = ppSettings();
    s.beginGroup(QLatin1String(kGroup));
    const QString v = s.value(QLatin1String(keyName(key))).toString();
    s.endGroup();
    return v;
}

void StudioProfileStore::store(StudioKey key, const QString &value)
{
    QSettings s = ppSettings();
    s.beginGroup(QLatin1String(kGroup));
    if (value.isEmpty()) s.remove(QLatin1String(keyName(key)));
    else                 s.setValue(QLatin1String(keyName(key)), value);
    s.endGroup();
    s.sync();
}

// ── the folder ──────────────────────────────────────────────────────────────────────────────────

bool StudioProfileStore::copyExists(const QString &path) const
{
    return !path.isEmpty() && QFileInfo::exists(path);
}

QByteArray StudioProfileStore::copyBytes(const QString &path) const
{
    QFile f(path);
    if (path.isEmpty() || !f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

std::optional<QByteArray> StudioProfileStore::readPicked(const QString &localPath) const
{
    const QFileInfo fi(localPath);
    if (!fi.isFile() || !fi.isReadable()) return std::nullopt;
    QFile in(localPath);
    if (!in.open(QIODevice::ReadOnly)) return std::nullopt;
    return in.readAll();
}

QString StudioProfileStore::writeCopy(const QString &baseName, const QString &ext, const QByteArray &bytes)
{
    const QString dir = studioDir();
    if (!QDir().mkpath(dir)) {
        ppWarn() << "StudioProfile: cannot create" << dir;
        return {};
    }
    const QString dest = dir + QLatin1Char('/') + baseName + QLatin1Char('.') + ext;
    if (const pinpoint::AtomicWriteResult w = pinpoint::atomicWrite(dest, bytes); !w.ok) {
        ppWarn() << "StudioProfile: cannot write" << dest << w.detail;
        return {};
    }
    // The earlier copy under another extension (logo.png → logo.svg) goes, so the folder holds one.
    for (const char *other : { "png", "jpg", "svg" }) {
        const QString p = dir + QLatin1Char('/') + baseName + QLatin1Char('.') + QLatin1String(other);
        if (p != dest) QFile::remove(p);
    }
    return dest;
}

void StudioProfileStore::removeCopy(const QString &path)
{
    if (!path.isEmpty()) QFile::remove(path);
}
