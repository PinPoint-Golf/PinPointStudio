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

#include "../../Metrics/setup_capability.h"

using namespace pinpoint::analysis;

namespace {

QVariantList toVariantList(const QStringList &l)
{
    QVariantList out;
    out.reserve(l.size());
    for (const QString &s : l)
        out.append(s);
    return out;
}

} // namespace

MetricCatalog::MetricCatalog(QObject *parent)
    : QObject(parent)
    , m_catalogue(makeMetricCatalogue())
{
}

MetricCatalog::~MetricCatalog() = default;

QVariantList MetricCatalog::groups() const
{
    QVariantList out;
    for (const MetricDescriptor *d : m_catalogue.all())
        if (!out.contains(d->group))
            out.append(d->group);
    return out;
}

QVariantList MetricCatalog::types() const
{
    return { metricTypeName(MetricType::Summary), metricTypeName(MetricType::PointInTime),
             metricTypeName(MetricType::TimeSeries), metricTypeName(MetricType::Sequence) };
}

QVariantList MetricCatalog::setupSummary(const QVariantMap &setup) const
{
    SetupFacts f;
    f.faceOnCamera  = setup.value(QStringLiteral("faceOn")).toBool();
    f.dtlCamera     = setup.value(QStringLiteral("dtl")).toBool();
    f.hackMotion    = setup.value(QStringLiteral("hackMotion")).toBool();
    f.launchMonitor = setup.value(QStringLiteral("launchMonitor")).toBool();
    for (const QVariant &v : setup.value(QStringLiteral("imuRoles")).toList()) {
        // The persisted spelling ("leadHand"), not segmentRoleName()'s ("LeadHand") — the shared
        // inverse in segment_role.h, so an unknown name is Unknown and dropped, never a guess.
        const SegmentRole r = segmentRoleForPlacementRole(v.toString());
        if (r != SegmentRole::Unknown)
            f.imuRoles.push_back(r);
    }

    QVariantList out;
    for (const CapabilityGroup &g : summariseSetup(m_catalogue, f)) {
        QVariantMap m;
        m.insert(QStringLiteral("group"),              g.group);
        m.insert(QStringLiteral("measured"),           g.measured);
        m.insert(QStringLiteral("estimated"),          g.estimated);
        m.insert(QStringLiteral("unavailable"),        g.unavailable);
        m.insert(QStringLiteral("planned"),            g.planned);
        m.insert(QStringLiteral("derived"),            g.derived);
        m.insert(QStringLiteral("measuredKeys"),       toVariantList(g.measuredKeys));
        m.insert(QStringLiteral("estimatedKeys"),      toVariantList(g.estimatedKeys));
        m.insert(QStringLiteral("unavailableKeys"),    toVariantList(g.unavailableKeys));
        m.insert(QStringLiteral("plannedKeys"),        toVariantList(g.plannedKeys));
        m.insert(QStringLiteral("derivedKeys"),        toVariantList(g.derivedKeys));
        m.insert(QStringLiteral("estimatedBecause"),   g.estimatedBecause);
        m.insert(QStringLiteral("unavailableBecause"), g.unavailableBecause);
        m.insert(QStringLiteral("upgrade"),            g.upgrade);
        m.insert(QStringLiteral("deviceGaps"),         toVariantList(g.deviceGaps));
        m.insert(QStringLiteral("deviceGapIds"),       toVariantList(g.deviceGapIds));
        QVariantList notes;
        for (const CapabilityGroup::GateNote &n : g.gateNotes)
            notes.append(QVariantMap{ { QStringLiteral("gate"), n.gate }, { QStringLiteral("words"), n.words },
                                      { QStringLiteral("subjects"), toVariantList(n.subjects) },
                                      { QStringLiteral("subjectWords"), toVariantList(n.subjectWords) } });
        m.insert(QStringLiteral("gateNotes"),          notes);
        out.append(m);
    }
    return out;
}
