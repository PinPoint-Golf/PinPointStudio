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

#include "session_wrist.h"

#include "../../Wrist/wrist_analysis_adapter.h"
#include "../../Wrist/wrist_dof_metadata.h"
#include "../../Vocabulary/Metrics/measurement.h"   // parseStoredKey: a key's source, told one way (refactor 6.1a)

#include <QHash>
#include <QVariantList>

#include <cmath>
#include <optional>
#include <vector>

namespace pinpoint::charts {

namespace {

namespace pa = pinpoint::analysis;

// The display order (the profile's own: Wrist/wrist_profile_assess.cpp): lag → face → forearm roll → trail tray → the
// grid-only lead elbow.
const pa::PpJointDof kDisplayOrder[] = {
    pa::PpJointDof::LeadWristRadUln,
    pa::PpJointDof::LeadWristFlexExt,
    pa::PpJointDof::LeadForearmRot,
    pa::PpJointDof::TrailWristFlexExt,
    pa::PpJointDof::LeadElbowFlex,
};

std::optional<pa::PpJointDof> dofNamed(const QString &name)
{
    for (pa::PpJointDof d : kDisplayOrder)
        if (name == QLatin1String(pa::dofName(d))) return d;
    for (int d = 0; d < pa::kNumDof; ++d)
        if (name == QLatin1String(pa::dofName(pa::PpJointDof(d)))) return pa::PpJointDof(d);
    return std::nullopt;
}

QVariant nul() { return QVariant::fromValue(nullptr); }

// "IMU" for the wrist unit's own reading (hm.*); a lead-wrist DOF read off anything else is the pose's.
QString sourceOf(const pa::DofMetadata &meta, const QString &key)
{
    if (pinpoint::analysis::parseStoredKey(key).instrument == pinpoint::analysis::Instrument::HackMotion)
        return QStringLiteral("IMU");
    if (meta.source == QLatin1String("IMU")) return QStringLiteral("pose");
    return meta.source;
}

// The checkpoints on the shot's own (window) clock: each checkpoint's phase, its first mark (the adapter's rule).
QVariantList checkpointsOf(const ShotSeries &shot)
{
    QVariantList cps;
    const pa::WristCheckpoint *k = pa::wristCheckpoints();
    for (int c = 0; c < pa::kNumPos; ++c) {
        const PhaseMark *at = nullptr;
        for (const PhaseMark &p : shot.phases)
            if (p.phase >= 0 && p.phase == k[c].phase) { at = &p; break; }
        QVariantMap m{ { QStringLiteral("tag"), QStringLiteral("P%1").arg(int(k[c].pos) + 1) },
                       { QStringLiteral("phase"), k[c].phase }, { QStringLiteral("note"), QString::fromLatin1(k[c].note) },
                       { QStringLiteral("available"), at != nullptr } };
        if (at) m.insert(QStringLiteral("tUs"), qlonglong(at->tUs));
        cps.append(m);
    }
    return cps;
}

QVariantMap dofHead(pa::PpJointDof dof, const QString &key, double confidence)
{
    const pa::DofMetadata meta = pa::dofMetadata(dof);
    return { { QStringLiteral("dof"), QString::fromLatin1(pa::dofName(dof)) }, { QStringLiteral("metricKey"), key },
             { QStringLiteral("name"), meta.name }, { QStringLiteral("sub"), meta.sub }, { QStringLiteral("axisName"), meta.axisName },
             { QStringLiteral("source"), sourceOf(meta, key) }, { QStringLiteral("confidence"), confidence },
             { QStringLiteral("poleTop"), meta.poleTop }, { QStringLiteral("poleBottom"), meta.poleBottom },
             { QStringLiteral("hasStrip"), meta.hasStrip } };
}

QVariantMap payloadOf(const ShotSeries &shot, const pa::WristProfile &prof, int archetypeMode, bool unbanded)
{
    const int mode = archetypeMode < -1 || archetypeMode > 2 ? -1 : archetypeMode;
    const int style = mode < 0 ? prof.resolved : mode;
    QVariantList dofs;
    for (const pa::WristProfileRow &r : prof.rows) {
        const std::optional<pa::PpJointDof> dof = dofNamed(r.dof);
        if (!dof) continue;
        QVariantMap d = dofHead(*dof, r.metricKey, r.confidence);
        QVariantList cells;
        for (int p = 0; p < pa::kWristProfilePositions; ++p) {
            const double delta = r.deltaDeg[size_t(p)];
            const bool ok = std::isfinite(delta);
            const pa::WristProfileGrade g = unbanded ? pa::WristProfileGrade{} : r.grades[size_t(style)][size_t(p)];
            QVariantMap cm{ { QStringLiteral("available"), ok }, { QStringLiteral("rag"), g.rag },
                            { QStringLiteral("banded"), g.banded },
                            { QStringLiteral("bandLo"), g.banded ? QVariant(g.lo) : nul() },
                            { QStringLiteral("bandHi"), g.banded ? QVariant(g.hi) : nul() } };
            if (ok) cm.insert(QStringLiteral("deltaDeg"), delta);
            cells.append(cm);
        }
        d.insert(QStringLiteral("cells"), cells);
        dofs.append(d);
    }
    if (dofs.isEmpty()) return {};
    return { { QStringLiteral("archetype"), style }, { QStringLiteral("archetypeName"), wristArchetypeName(style) },
             { QStringLiteral("archetypeMode"), mode }, { QStringLiteral("resemblance"), shot.wristResemblance },
             { QStringLiteral("pending"), false }, { QStringLiteral("pendingWhy"), QString() },
             { QStringLiteral("checkpoints"), checkpointsOf(shot) }, { QStringLiteral("dofs"), dofs } };
}

// Wrist readings, no profile: the DOFs it carries (the rule's own pick: per DOF the last key), no cell measured.
QVariantMap pendingOf(const ShotSeries &shot, int archetypeMode, const QString &why)
{
    const pa::WristInput in = wristInputOfSeries(shot);
    if (!in.hasWristDof()) return {};
    std::vector<pa::MetricSeries> series;
    QHash<int, QString> keyOf;
    for (const pa::WristInput::Series &s : in.series) {
        const std::optional<pa::PpJointDof> dof = pa::dofForMetricKey(s.key);
        if (!dof || s.tUs.empty() || s.value.empty() || pa::isCameraWristDofSeries(s.routeId)) continue;
        pa::MetricSeries m;
        m.key = s.key;
        m.t_us.assign(s.tUs.begin(), s.tUs.end());
        m.value = s.value;
        series.push_back(std::move(m));
        keyOf.insert(int(*dof), s.key);
    }
    const pa::InMemoryWristAngleSource source = pa::buildWristAngleSource(series, {});
    QVariantList dofs;
    for (pa::PpJointDof dof : kDisplayOrder) {
        if (!keyOf.contains(int(dof))) continue;
        const pa::PpJointAngleSeries *s = source.series(dof);
        QVariantMap d = dofHead(dof, keyOf.value(int(dof)), s ? double(s->baseConfidence) : 0.0);
        QVariantList cells;
        for (int p = 0; p < pa::kWristProfilePositions; ++p)
            cells.append(QVariantMap{ { QStringLiteral("available"), false }, { QStringLiteral("rag"), QStringLiteral("none") },
                                      { QStringLiteral("banded"), false }, { QStringLiteral("bandLo"), nul() },
                                      { QStringLiteral("bandHi"), nul() } });
        d.insert(QStringLiteral("cells"), cells);
        dofs.append(d);
    }
    if (dofs.isEmpty()) return {};
    const int mode = archetypeMode < -1 || archetypeMode > 2 ? -1 : archetypeMode;
    const int style = mode < 0 ? 0 : mode;
    return { { QStringLiteral("archetype"), style }, { QStringLiteral("archetypeName"), wristArchetypeName(style) },
             { QStringLiteral("archetypeMode"), mode }, { QStringLiteral("resemblance"), shot.wristResemblance },
             { QStringLiteral("pending"), true },
             { QStringLiteral("pendingWhy"), why.isEmpty() ? QStringLiteral("Diagnostics have not graded the wrist checkpoints yet.") : why },
             { QStringLiteral("checkpoints"), checkpointsOf(shot) }, { QStringLiteral("dofs"), dofs } };
}

} // namespace

QString wristArchetypeName(int archetype)
{
    switch (archetype) {
    case 1:  return QStringLiteral("Bowed");
    case 2:  return QStringLiteral("Cupped");
    default: return QStringLiteral("Neutral");
    }
}

QVariantMap wristResemblanceOf(const QJsonObject &analysis)
{
    const QJsonObject score = analysis.value(QStringLiteral("score")).toObject();
    const QJsonObject res = score.value(QStringLiteral("resemblance")).toObject();
    if (res.isEmpty()) return {};
    QVariantMap out;
    for (const char *k : { "neutral", "bowed", "cupped" }) {
        const QJsonValue v = res.value(QLatin1String(k));
        if (!v.isDouble()) return {};          // a partial index is no index
        out.insert(QLatin1String(k), int(std::lround(v.toDouble())));
    }
    return out;
}

pa::WristInput wristInputOfSeries(const ShotSeries &shot)
{
    pa::WristInput in;
    for (const QString &k : shot.metricOrder) {
        const MetricSeries *m = shot.metric(k);
        if (!m) continue;
        pa::WristInput::Series s;
        s.key = k;
        s.routeId = m->routeId;
        s.tUs.assign(m->tUs.begin(), m->tUs.end());
        s.value = m->value;
        in.series.push_back(std::move(s));
    }
    for (const PhaseMark &p : shot.phases) in.phases.push_back(pa::WristInput::Phase{ p.phase, p.tUs, p.conf });
    return in;
}

QVariantMap shotWristPayload(const ShotSeries &shot, int archetypeMode, const QString &pendingWhy)
{
    if (shot.diagnostics.wrist.present) return payloadOf(shot, shot.diagnostics.wrist, archetypeMode, false);
    return pendingOf(shot, archetypeMode, pendingWhy);
}

QVariantMap referenceWristPayload(const ShotSeries &shot)
{
    if (shot.diagnostics.wrist.present) return payloadOf(shot, shot.diagnostics.wrist, -1, true);
    return payloadOf(shot, pa::wristDeltasOnly(wristInputOfSeries(shot)), -1, true);
}

} // namespace pinpoint::charts
