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

#include "../../Metrics/metric_catalogue.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// MetricCatalog — the QML façade over pinpoint::analysis::MetricCatalogue (design §7), the same
// shape as ChartMetrics / SwingDataSource: a QML_ELEMENT declared `MetricCatalog { id: mc }` whose
// only job is to marshal registry value types into QVariant shapes. The catalogue is assembled once
// in the constructor (makeMetricCatalogue) and read-only thereafter.
//
// A `shotCtx` QVariantMap parameterises per-shot availability + normative resolution. Shape (all
// optional): { tier:int, sessionType:int, imuRoles:[roleName…], hasFaceOn, hasClubTrack,
// hasBallTrack, archetype, club, shape }. Omit it (or pass {}) for the context-free directory view;
// pass a shot's capability (or the studio's configured capability) to drive availability chips.
// Phases are emitted as Phase ints (the vocabulary QML already compares and TimelineLabels renders).

class MetricCatalog : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList groups READ groups CONSTANT)   // distinct groups, manifest order
    Q_PROPERTY(QVariantList types  READ types  CONSTANT)   // ["summary","pointInTime",…]

public:
    explicit MetricCatalog(QObject *parent = nullptr);

    QVariantList groups() const;
    QVariantList types() const;

    // Directory list. filters: { type?, group?, scored?, availableOnly?, sessionType? }.
    // With availableOnly + a non-empty shotCtx, Unavailable metrics are dropped.
    Q_INVOKABLE QVariantList query(const QVariantMap &filters,
                                   const QVariantMap &shotCtx = {}) const;

    // Full detail for the detail page (description / howToRead / normative corridors / requirement /
    // availability / usedBy). Empty map when the key is unknown.
    Q_INVOKABLE QVariantMap descriptor(const QString &key,
                                       const QVariantMap &shotCtx = {}) const;

    // Availability alone: { state, reason, tier } — e.g. a zone deciding whether to render a metric.
    Q_INVOKABLE QVariantMap availability(const QString &key,
                                         const QVariantMap &shotCtx) const;

private:
    pinpoint::analysis::MetricCatalogue m_catalogue;
};
