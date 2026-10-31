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
// in the constructor (makeMetricCatalogue) and read-only thereafter. Its one caller today is
// session setup (and the Home rig card), through setupSummary().

class MetricCatalog : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList groups READ groups CONSTANT)   // distinct groups, manifest order
    Q_PROPERTY(QVariantList types  READ types  CONSTANT)   // ["summary","pointInTime",…]

public:
    explicit MetricCatalog(QObject *parent = nullptr);
    ~MetricCatalog() override;

    QVariantList groups() const;
    QVariantList types() const;

    // What a session set up like this WILL record, per metric group, before any swing — the closing
    // page of session setup (session_wizard_refactor_design.md §4.14; summariseSetup in
    // Metrics/setup_capability.h). setup: { faceOn:bool, dtl:bool, imuRoles:[name…],
    // hackMotion:bool, launchMonitor:bool }, where a role name is the PERSISTED spelling
    // ("pelvis", "thorax", "leadForearm", "leadHand", "leadUpperArm") and the caller passes only
    // roles that are assigned, enabled and calibrated. One map per group, manifest order:
    // { group, measured, estimated, unavailable, planned, derived, measuredKeys, estimatedKeys,
    //   unavailableKeys, plannedKeys, derivedKeys, estimatedBecause, unavailableBecause, upgrade,
    //   deviceGaps, deviceGapIds }.
    Q_INVOKABLE QVariantList setupSummary(const QVariantMap &setup) const;

private:
    pinpoint::analysis::MetricCatalogue m_catalogue;
};
