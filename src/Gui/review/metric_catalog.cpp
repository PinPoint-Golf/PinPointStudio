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

#include "metric_catalog.h"

#include "../../Vocabulary/Metrics/setup_capability.h"

using namespace pinpoint::analysis;

MetricCatalog::MetricCatalog(QObject *parent)
    : QObject(parent)
    , m_catalogue(makeMetricCatalogue())
{
}

MetricCatalog::~MetricCatalog() = default;

QStringList MetricCatalog::groups() const
{
    QStringList out;
    for (const MetricDescriptor *d : m_catalogue.all())
        if (!out.contains(d->group))
            out.append(d->group);
    return out;
}

QStringList MetricCatalog::types() const
{
    return { metricTypeName(MetricType::Summary), metricTypeName(MetricType::PointInTime),
             metricTypeName(MetricType::TimeSeries), metricTypeName(MetricType::Sequence) };
}

QList<pinpoint::catalogui::Capability> MetricCatalog::setupSummary(bool faceOn, bool dtl, const QStringList &imuRoles,
                                                                   bool hackMotion, bool launchMonitor) const
{
    SetupFacts f;
    f.faceOnCamera  = faceOn;
    f.dtlCamera     = dtl;
    f.hackMotion    = hackMotion;
    f.launchMonitor = launchMonitor;
    for (const QString &name : imuRoles) {
        // The persisted spelling ("leadHand"), not segmentRoleName()'s ("LeadHand") — the shared
        // inverse in segment_role.h, so an unknown name is Unknown and dropped, never a guess.
        const SegmentRole r = segmentRoleForPlacementRole(name);
        if (r != SegmentRole::Unknown)
            f.imuRoles.push_back(r);
    }

    QList<pinpoint::catalogui::Capability> out;
    for (const CapabilityGroup &g : summariseSetup(m_catalogue, f)) {
        pinpoint::catalogui::Capability c;
        c.group              = g.group;
        c.measured           = g.measured;
        c.estimated          = g.estimated;
        c.unavailable        = g.unavailable;
        c.planned            = g.planned;
        c.derived            = g.derived;
        c.measuredKeys       = g.measuredKeys;
        c.estimatedKeys      = g.estimatedKeys;
        c.unavailableKeys    = g.unavailableKeys;
        c.plannedKeys        = g.plannedKeys;
        c.derivedKeys        = g.derivedKeys;
        c.estimatedBecause   = g.estimatedBecause;
        c.unavailableBecause = g.unavailableBecause;
        c.upgrade            = g.upgrade;
        c.deviceGaps         = g.deviceGaps;
        c.deviceGapIds       = g.deviceGapIds;
        for (const CapabilityGroup::GateNote &n : g.gateNotes) {
            pinpoint::catalogui::GateNote gn;
            gn.gate         = n.gate;
            gn.words        = n.words;
            gn.subjects     = n.subjects;
            gn.subjectWords = n.subjectWords;
            c.gateNotes.append(gn);
        }
        out.append(c);
    }
    return out;
}

// moc for the typed summary's gadgets (metric_catalog_types.h has no .cpp of its own)
#include "moc_metric_catalog_types.cpp"
