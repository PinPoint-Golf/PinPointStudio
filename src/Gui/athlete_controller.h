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

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class AthleteController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool         hasCurrentAthlete READ hasCurrentAthlete NOTIFY currentAthleteChanged)
    Q_PROPERTY(QString      currentName       READ currentName       NOTIFY currentAthleteChanged)
    Q_PROPERTY(QString      currentInitials   READ currentInitials   NOTIFY currentAthleteChanged)
    Q_PROPERTY(QString      currentHandedness READ currentHandedness NOTIFY currentAthleteChanged)
    Q_PROPERTY(QString      currentUuid       READ currentUuid       NOTIFY currentAthleteChanged)
    Q_PROPERTY(QVariantList athletes          READ athletes          NOTIFY athletesChanged)

public:
    explicit AthleteController(QObject *parent = nullptr);

    bool         hasCurrentAthlete() const { return !m_currentUuid.isEmpty(); }
    QString      currentName()       const { return m_currentName; }
    QString      currentInitials()   const { return m_currentInitials; }
    QString      currentHandedness() const { return m_currentHandedness; }
    QString      currentUuid()       const { return m_currentUuid; }
    QVariantList athletes()          const { return m_athletes; }

    Q_INVOKABLE QString createAthlete(
        const QString &name,
        const QString &handedness  = QStringLiteral("Right"),
        double         heightValue = 0.0,
        const QString &heightUnit  = QStringLiteral("ft"),
        double         weightValue = 0.0,
        const QString &weightUnit  = QStringLiteral("lb"),
        double         handicap    = -1.0,
        const QString &primaryClub = QStringLiteral("Driver"),
        double         speedTarget = 0.0,
        const QString &notes       = QString()
    );

    Q_INVOKABLE bool updateAthlete(const QString &uuid, const QString &fieldName, const QVariant &value);
    Q_INVOKABLE bool deleteAthlete(const QString &uuid);
    Q_INVOKABLE void selectAthlete(const QString &uuid);
    Q_INVOKABLE void clearCurrentAthlete();

signals:
    void currentAthleteChanged();
    void athletesChanged();

private:
    void reload();
    static QString computeInitials(const QString &name);

    QVariantList m_athletes;
    QString      m_currentUuid;
    QString      m_currentName;
    QString      m_currentInitials;
    QString      m_currentHandedness;
};
