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

#include "athlete_controller.h"

#include "pp_settings.h"
#include <QUuid>
#include <QDateTime>
#include <algorithm>

static constexpr auto kGroup         = "athletes";
static constexpr auto kCurrentUuid   = "currentAthleteUuid";
static constexpr auto kName          = "name";
static constexpr auto kHandedness    = "handedness";
static constexpr auto kHeightValue   = "heightValue";
static constexpr auto kHeightUnit    = "heightUnit";
static constexpr auto kWeightValue   = "weightValue";
static constexpr auto kWeightUnit    = "weightUnit";
static constexpr auto kHandicap      = "handicap";
static constexpr auto kPrimaryClub   = "primaryClub";
static constexpr auto kSpeedTarget   = "speedTarget";
static constexpr auto kNotes         = "notes";
static constexpr auto kCreatedAt     = "createdAt";
static constexpr auto kLastSessionAt = "lastSessionAt";
static constexpr auto kSessionCount  = "sessionCount";


AthleteController::AthleteController(QObject *parent)
    : QObject(parent)
{
    reload();
}

QString AthleteController::computeInitials(const QString &name)
{
    const QStringList words = name.trimmed().split(u' ', Qt::SkipEmptyParts);
    if (words.size() >= 2)
        return QString(words.first()[0]).toUpper() + QString(words.last()[0]).toUpper();
    if (!words.isEmpty() && words.first().length() >= 2)
        return words.first().left(2).toUpper();
    if (!words.isEmpty())
        return words.first().left(1).toUpper();
    return QStringLiteral("?");
}

void AthleteController::reload()
{
    QSettings s = ppSettings();

    // Collect all athlete UUIDs
    s.beginGroup(kGroup);
    const QStringList uuids = s.childGroups();
    s.endGroup();

    QVariantList list;
    list.reserve(uuids.size());

    for (const QString &uuid : uuids) {
        s.beginGroup(kGroup);
        s.beginGroup(uuid);

        QVariantMap m;
        m[QStringLiteral("uuid")]          = uuid;
        m[QStringLiteral("name")]          = s.value(kName,         QString()).toString();
        m[QStringLiteral("handedness")]    = s.value(kHandedness,   QStringLiteral("Right")).toString();
        m[QStringLiteral("heightValue")]   = s.value(kHeightValue,  0.0).toDouble();
        m[QStringLiteral("heightUnit")]    = s.value(kHeightUnit,   QStringLiteral("ft")).toString();
        m[QStringLiteral("weightValue")]   = s.value(kWeightValue,  0.0).toDouble();
        m[QStringLiteral("weightUnit")]    = s.value(kWeightUnit,   QStringLiteral("lb")).toString();
        m[QStringLiteral("handicap")]      = s.value(kHandicap,     -1.0).toDouble();
        m[QStringLiteral("primaryClub")]   = s.value(kPrimaryClub,  QStringLiteral("Driver")).toString();
        m[QStringLiteral("speedTarget")]   = s.value(kSpeedTarget,  0.0).toDouble();
        m[QStringLiteral("notes")]         = s.value(kNotes,        QString()).toString();
        m[QStringLiteral("createdAt")]     = s.value(kCreatedAt,    0LL).toLongLong();
        m[QStringLiteral("lastSessionAt")] = s.value(kLastSessionAt,0LL).toLongLong();
        m[QStringLiteral("sessionCount")]  = s.value(kSessionCount, 0).toInt();
        m[QStringLiteral("initials")]      = computeInitials(m[QStringLiteral("name")].toString());

        s.endGroup(); // uuid
        s.endGroup(); // athletes

        list.append(m);
    }

    // Sort: lastSessionAt desc, createdAt desc as tie-breaker
    std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        const auto ma = a.toMap();
        const auto mb = b.toMap();
        const qint64 aLast = ma[QStringLiteral("lastSessionAt")].toLongLong();
        const qint64 bLast = mb[QStringLiteral("lastSessionAt")].toLongLong();
        if (aLast != bLast) return aLast > bLast;
        return ma[QStringLiteral("createdAt")].toLongLong() > mb[QStringLiteral("createdAt")].toLongLong();
    });

    m_athletes = list;
    emit athletesChanged();

    // Resolve current athlete
    const QString wantedUuid = s.value(kCurrentUuid, QString()).toString();
    bool found = false;
    for (const QVariant &v : std::as_const(m_athletes)) {
        const auto m = v.toMap();
        if (m[QStringLiteral("uuid")].toString() == wantedUuid) {
            m_currentUuid       = wantedUuid;
            m_currentName       = m[QStringLiteral("name")].toString();
            m_currentInitials   = m[QStringLiteral("initials")].toString();
            m_currentHandedness = m[QStringLiteral("handedness")].toString();
            found = true;
            break;
        }
    }
    if (!found) {
        m_currentUuid       = {};
        m_currentName       = {};
        m_currentInitials   = {};
        m_currentHandedness = {};
    }

    emit currentAthleteChanged();
}

QString AthleteController::createAthlete(
    const QString &name,
    const QString &handedness,
    double         heightValue,
    const QString &heightUnit,
    double         weightValue,
    const QString &weightUnit,
    double         handicap,
    const QString &primaryClub,
    double         speedTarget,
    const QString &notes)
{
    if (name.trimmed().isEmpty())
        return {};

    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const qint64  now  = QDateTime::currentSecsSinceEpoch();

    // Convert to base unit before storing (always ft / lb)
    double storedHeight = heightValue;
    if (heightUnit == QLatin1String("cm") && heightValue > 0.0)
        storedHeight = heightValue / 30.48;

    double storedWeight = weightValue;
    if (weightUnit == QLatin1String("kg") && weightValue > 0.0)
        storedWeight = weightValue * 2.20462;

    QSettings s = ppSettings();
    s.beginGroup(kGroup);
    s.beginGroup(uuid);

    s.setValue(kName,          name.trimmed());
    s.setValue(kHandedness,    handedness);
    s.setValue(kHeightValue,   storedHeight);
    s.setValue(kHeightUnit,    heightUnit);
    s.setValue(kWeightValue,   storedWeight);
    s.setValue(kWeightUnit,    weightUnit);
    s.setValue(kHandicap,      handicap);
    s.setValue(kPrimaryClub,   primaryClub);
    s.setValue(kSpeedTarget,   speedTarget);
    s.setValue(kNotes,         notes);
    s.setValue(kCreatedAt,     now);
    s.setValue(kLastSessionAt, 0LL);
    s.setValue(kSessionCount,  0);

    s.endGroup(); // uuid
    s.endGroup(); // athletes

    s.setValue(kCurrentUuid, uuid);
    s.sync();

    reload();
    return uuid;
}

bool AthleteController::updateAthlete(const QString &uuid, const QString &fieldName, const QVariant &value)
{
    QSettings s = ppSettings();

    s.beginGroup(kGroup);
    const bool exists = s.childGroups().contains(uuid);
    s.endGroup();

    if (!exists)
        return false;

    s.beginGroup(kGroup);
    s.beginGroup(uuid);
    s.setValue(fieldName, value);
    s.endGroup();
    s.endGroup();
    s.sync();

    reload();
    return true;
}

bool AthleteController::deleteAthlete(const QString &uuid)
{
    QSettings s = ppSettings();

    s.beginGroup(kGroup);
    s.remove(uuid);
    s.endGroup();

    if (s.value(kCurrentUuid).toString() == uuid)
        s.remove(kCurrentUuid);

    s.sync();
    reload();
    return true;
}

void AthleteController::selectAthlete(const QString &uuid)
{
    QSettings s = ppSettings();
    s.setValue(kCurrentUuid, uuid);
    s.sync();
    reload();
}

void AthleteController::clearCurrentAthlete()
{
    QSettings s = ppSettings();
    s.remove(kCurrentUuid);
    s.sync();
    reload();
}
