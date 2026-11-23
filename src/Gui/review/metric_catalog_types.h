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

// METRIC CATALOG'S TYPED RESULT (refactor 5.8a, lint A6): what MetricCatalog::setupSummary hands QML, one Q_GADGET
// per catalogue group, named as the old map's keys (pinpoint::analysis::CapabilityGroup, Metrics/setup_capability.h,
// field for field). Every map carried every key, so a gadget reads the same. moc reaches this header through
// metric_catalog.cpp's #include of its moc file.

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

namespace pinpoint::catalogui {

// A gate the setup's routes leave some of the group's readings on (CapabilityGroup::GateNote).
struct GateNote
{
    Q_GADGET
    Q_PROPERTY(QString     gate         MEMBER gate         FINAL)
    Q_PROPERTY(QString     words        MEMBER words        FINAL)
    Q_PROPERTY(QStringList subjects     MEMBER subjects     FINAL)
    Q_PROPERTY(QStringList subjectWords MEMBER subjectWords FINAL)
public:
    QString     gate, words;
    QStringList subjects, subjectWords;
    friend bool operator==(const GateNote &, const GateNote &) = default;
};

// What a session set up like this will record for one metric group.
struct Capability
{
    Q_GADGET
    Q_PROPERTY(QString     group              MEMBER group              FINAL)
    Q_PROPERTY(int         measured           MEMBER measured           FINAL)
    Q_PROPERTY(int         estimated          MEMBER estimated          FINAL)
    Q_PROPERTY(int         unavailable        MEMBER unavailable        FINAL)
    Q_PROPERTY(int         planned            MEMBER planned            FINAL)
    Q_PROPERTY(int         derived            MEMBER derived            FINAL)
    Q_PROPERTY(QStringList measuredKeys       MEMBER measuredKeys       FINAL)
    Q_PROPERTY(QStringList estimatedKeys      MEMBER estimatedKeys      FINAL)
    Q_PROPERTY(QStringList unavailableKeys    MEMBER unavailableKeys    FINAL)
    Q_PROPERTY(QStringList plannedKeys        MEMBER plannedKeys        FINAL)
    Q_PROPERTY(QStringList derivedKeys        MEMBER derivedKeys        FINAL)
    Q_PROPERTY(QString     estimatedBecause   MEMBER estimatedBecause   FINAL)
    Q_PROPERTY(QString     unavailableBecause MEMBER unavailableBecause FINAL)
    Q_PROPERTY(QString     upgrade            MEMBER upgrade            FINAL)
    Q_PROPERTY(QStringList deviceGaps         MEMBER deviceGaps         FINAL)
    Q_PROPERTY(QStringList deviceGapIds       MEMBER deviceGapIds       FINAL)
    Q_PROPERTY(QList<pinpoint::catalogui::GateNote> gateNotes MEMBER gateNotes FINAL)
public:
    QString     group;
    int         measured = 0, estimated = 0, unavailable = 0, planned = 0, derived = 0;
    QStringList measuredKeys, estimatedKeys, unavailableKeys, plannedKeys, derivedKeys;
    QString     estimatedBecause, unavailableBecause, upgrade;
    QStringList deviceGaps, deviceGapIds;
    QList<GateNote> gateNotes;
    friend bool operator==(const Capability &, const Capability &) = default;
};

} // namespace pinpoint::catalogui
