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

// TEST-ONLY: the trimmed 4 July fixture (data/charts/session_0704.json, made by
// build/charts-build/goldens/trim_fixture.py from the Charts round-1 fixture) read into a
// SessionSnapshot, plus the card read-at table it carries (readat_from_manifest.py's extraction of
// MetricCardSpec::readAt, so the maths test needs no ChartMetrics and no catalogue).
//
// Shape (compact on purpose, ≈ 370 KB): shots[] { ordinal, club, impactUs, phases [[phase, t_us, conf]],
// kinematicSequence { order, nodes }, metricOrder [key], metrics { key: { label, unit, sigma, sigmaKind,
// at [[phase, t_us, value, sigma]], t_us?, value?, valid? } }, fired [id] }, conditions { id: { name,
// group } }, readAt { key: [phase] }, pickedOrdinal.

#include "charts/session_series_types.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <functional>
#include <memory>
#include <vector>

namespace charts_test {

using namespace pinpoint::charts;

inline double num(const QJsonValue &v) { return v.isDouble() ? v.toDouble() : kNaN; }

struct Fixture {
    SessionSnapshot session;
    QHash<QString, std::vector<int>> readAt;
    int picked = 0;
    bool ok = false;

    std::vector<int> readAtOf(const QString &key) const
    {
        auto it = readAt.constFind(key);
        return it == readAt.constEnd() ? std::vector<int>{ 5 } : it.value();   // the catalogue default: impact
    }
};

inline Fixture loadFixture(const QString &path)
{
    Fixture fx;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return fx;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    fx.picked = root.value(QStringLiteral("pickedOrdinal")).toInt();
    const QJsonObject ra = root.value(QStringLiteral("readAt")).toObject();
    for (auto it = ra.begin(); it != ra.end(); ++it) {
        std::vector<int> ph;
        for (const QJsonValue &v : it.value().toArray()) ph.push_back(v.toInt());
        fx.readAt.insert(it.key(), ph);
    }
    const QJsonObject conds = root.value(QStringLiteral("conditions")).toObject();
    for (auto it = conds.begin(); it != conds.end(); ++it) {
        ConditionInfo ci;
        ci.name = it.value().toObject().value(QStringLiteral("name")).toString();
        ci.group = it.value().toObject().value(QStringLiteral("group")).toString();
        fx.session.conditions.insert(it.key(), ci);
    }
    for (const QJsonValue &sv : root.value(QStringLiteral("shots")).toArray()) {
        const QJsonObject so = sv.toObject();
        auto sh = std::make_shared<ShotSeries>();
        sh->ordinal  = so.value(QStringLiteral("ordinal")).toInt();
        sh->shotId   = sh->ordinal;
        sh->club     = so.value(QStringLiteral("club")).toString();
        sh->impactUs = qint64(so.value(QStringLiteral("impactUs")).toDouble());
        for (const QJsonValue &pv : so.value(QStringLiteral("phases")).toArray()) {
            const QJsonArray p = pv.toArray();
            sh->phases.push_back(PhaseMark{ p.at(0).toInt(), qint64(p.at(1).toDouble()), num(p.at(2)) });
        }
        sh->kinematicSequence = so.value(QStringLiteral("kinematicSequence")).toObject().toVariantMap();
        for (const QJsonValue &k : so.value(QStringLiteral("metricOrder")).toArray()) sh->metricOrder << k.toString();
        const QJsonObject ms = so.value(QStringLiteral("metrics")).toObject();
        for (auto it = ms.begin(); it != ms.end(); ++it) {
            const QJsonObject mo = it.value().toObject();
            MetricSeries m;
            m.key   = it.key();
            m.label = mo.value(QStringLiteral("label")).toString();
            m.unit  = mo.value(QStringLiteral("unit")).toString();
            m.sigma = num(mo.value(QStringLiteral("sigma")));
            m.sigmaKind = mo.value(QStringLiteral("sigmaKind")).isDouble() ? mo.value(QStringLiteral("sigmaKind")).toInt() : -1;
            for (const QJsonValue &av : mo.value(QStringLiteral("at")).toArray()) {
                const QJsonArray a = av.toArray();
                PhaseSample ps;
                ps.phase = a.at(0).toInt(); ps.tUs = qint64(a.at(1).toDouble()); ps.value = num(a.at(2)); ps.sigma = num(a.at(3));
                m.phaseSamples.push_back(ps);
            }
            for (const QJsonValue &v : mo.value(QStringLiteral("t_us")).toArray()) m.tUs.push_back(qint64(v.toDouble()));
            for (const QJsonValue &v : mo.value(QStringLiteral("value")).toArray()) m.value.push_back(num(v));
            for (const QJsonValue &v : mo.value(QStringLiteral("valid")).toArray()) m.valid.push_back(uint8_t(v.toInt()));
            sh->metrics.insert(m.key, m);
        }
        for (const QJsonValue &v : so.value(QStringLiteral("fired")).toArray()) sh->diagnostics.fired << v.toString();
        sh->diagnostics.club = sh->club;
        fx.session.shots.push_back(sh);
    }
    fx.ok = !fx.session.shots.empty();
    return fx;
}

} // namespace charts_test
