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

#include "../../Vocabulary/Metrics/metric_catalogue.h"
#include "metric_catalog_types.h"   // the typed summary QML reads (refactor 5.8a, lint A6)

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

// MetricCatalog — the QML façade over pinpoint::analysis::MetricCatalogue (design §7), the same
// shape as ChartMetrics / SwingDataSource: a QML_ELEMENT declared `MetricCatalog { id: mc }` whose
// only job is to marshal registry value types into typed values (refactor 5.8a: Q_GADGETs and string lists, no
// QVariant shapes; metric_catalog_types.h). The catalogue is assembled once
// in the constructor (makeMetricCatalogue) and read-only thereafter. Its one caller today is
// session setup (and the Home rig card), through setupSummary().

class MetricCatalog : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QStringList groups READ groups CONSTANT)   // distinct groups, manifest order
    Q_PROPERTY(QStringList types  READ types  CONSTANT)   // ["summary","pointInTime",…]

public:
    explicit MetricCatalog(QObject *parent = nullptr);
    ~MetricCatalog() override;

    QStringList groups() const;
    QStringList types() const;

    // What a session set up like this WILL record, per metric group, before any swing — the closing
    // page of session setup (session_wizard_refactor_design.md §4.14; summariseSetup in
    // Vocabulary/Metrics/setup_capability.h). The setup is its five facts (the setup facts' faceOn, dtl, imuRoles, hackMotion
    // and launchMonitor: CapRows.summaryOf, setup_capability_rows.js, passes them from the facts object), where a role
    // name is the PERSISTED spelling ("pelvis", "thorax", "leadForearm", "leadHand", "leadUpperArm") and the caller
    // passes only roles that are assigned, enabled and calibrated. One Capability per group, manifest order:
    // { group, measured, estimated, unavailable, planned, derived, measuredKeys, estimatedKeys,
    //   unavailableKeys, plannedKeys, derivedKeys, estimatedBecause, unavailableBecause, upgrade,
    //   deviceGaps, deviceGapIds, gateNotes }.
    Q_INVOKABLE QList<pinpoint::catalogui::Capability> setupSummary(bool faceOn, bool dtl, const QStringList &imuRoles,
                                                                    bool hackMotion, bool launchMonitor) const;

private:
    pinpoint::analysis::MetricCatalogue m_catalogue;
};
