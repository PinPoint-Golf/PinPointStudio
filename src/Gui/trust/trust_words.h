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

// TRUSTWORDS — the gate behind a reading, for a QML surface that holds a shot's records (the detail
// map's `gates`) but no presenter of its own: the launch monitor board, the camera tiles, the carousel
// badge, the 3-D panel (trust stage 6 session 2). One call, the same answer trust_readings.h gives the
// presenters, so a tile and the Trust panel never word a refusal differently.

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace pinpoint::trust {

class TrustWords : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit TrustWords(QObject *parent = nullptr) : QObject(parent) {}

    // trust_readings.h gateFor: { subject, gate, effect, words, title, sentence } or {}.
    Q_INVOKABLE QVariantMap gateFor(const QVariant &gates, const QString &key) const;
    // A gate's short words for a claim, by id ("" when unknown).
    Q_INVOKABLE QString words(const QString &gateId) const;
};

} // namespace pinpoint::trust
