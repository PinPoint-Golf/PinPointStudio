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

#include "../../Wrist/reference_bands.h"
#include "../../Wrist/wrist_analysis_adapter.h"
#include "../../Wrist/wrist_assessment_engine.h"
#include "../../Wrist/wrist_dof_metadata.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QVariantList>

#include <cmath>
#include <memory>
#include <optional>
#include <vector>

namespace pinpoint::charts {

namespace {

namespace pa = pinpoint::analysis;

// The display order (WristDiagnosticsModel's kDisplayOrder): lag → face → forearm roll → trail tray →
// the grid-only lead elbow.
const pa::PpJointDof kDisplayOrder[] = {
    pa::PpJointDof::LeadWristRadUln,
    pa::PpJointDof::LeadWristFlexExt,
    pa::PpJointDof::LeadForearmRot,
    pa::PpJointDof::TrailWristFlexExt,
    pa::PpJointDof::LeadElbowFlex,
};

// ONE band provider for the process, built under a lock. NormBandProvider is read-only once made (it
// holds the shared norm set), so every worker may grade through it at once; what is not thread-safe is
// the FIRST build of sharedNormProvider()'s slot, which this lock serialises among our callers and
// warmWristBands() moves onto the GUI thread before any worker exists.
const pa::IReferenceBandProvider &bands()
{
    static QMutex lock;
    static std::unique_ptr<pa::IReferenceBandProvider> provider;
    QMutexLocker lk(&lock);
    if (!provider) provider = pa::makeReferenceBandProvider();
    return *provider;
}

QVariant nul() { return QVariant::fromValue(nullptr); }

QString ragWord(pa::PpRag r)
{
    return r == pa::PpRag::Grey ? QStringLiteral("none") : QString::fromLatin1(pa::ragName(r));
}

// "IMU" for the wrist unit's own reading (hm.*); a lead-wrist DOF read off anything else is the pose's.
QString sourceOf(const pa::DofMetadata &meta, const QString &key)
{
    if (key.startsWith(QLatin1String("hm."))) return QStringLiteral("IMU");
    if (meta.source == QLatin1String("IMU")) return QStringLiteral("pose");
    return meta.source;
}

} // namespace

void warmWristBands() { (void)bands(); }

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

QVariantMap shotWristPayload(const ShotSeries &shot, int archetypeMode, const QVariantMap &resemblance)
{
    // The wrist series only, in the swing's own order (last key per DOF wins, as the adapter's).
    std::vector<pa::MetricSeries> series;
    QHash<int, QString> keyOf;
    for (const QString &k : shot.metricOrder) {
        const std::optional<pa::PpJointDof> dof = pa::dofForMetricKey(k);
        if (!dof) continue;
        const MetricSeries *m = shot.metric(k);
        if (!m || m->tUs.empty()) continue;
        pa::MetricSeries s;
        s.key = k;
        s.t_us.assign(m->tUs.begin(), m->tUs.end());
        s.value = m->value;
        series.push_back(std::move(s));
        keyOf.insert(int(*dof), k);
    }
    if (series.empty()) return {};

    std::vector<pa::PhaseEvent> phases;
    phases.reserve(shot.phases.size());
    for (const PhaseMark &p : shot.phases) {
        if (p.phase < 0) continue;
        pa::PhaseEvent e;
        e.phase = pa::Phase(p.phase);
        e.t_us  = p.tUs;
        e.conf  = std::isfinite(p.conf) ? float(p.conf) : 1.0f;
        phases.push_back(e);
    }

    pa::InMemoryWristAngleSource source = pa::buildWristAngleSource(series, phases);
    // A missing sample is a gap, never a value: the document's null reads NaN here (session_series_model
    // num()), which the sampler would otherwise median into the cell.
    for (int d = 0; d < pa::kNumDof; ++d) {
        const pa::PpJointAngleSeries *s = source.series(pa::PpJointDof(d));
        if (!s) continue;
        bool any = false;
        for (const pa::PpJointAngleSample &x : s->samples) any = any || !std::isfinite(x.valueDeg) || !std::isfinite(x.pitchProxyDeg);
        if (!any) continue;
        pa::PpJointAngleSeries fixed = *s;
        for (pa::PpJointAngleSample &x : fixed.samples) {
            if (!std::isfinite(x.valueDeg)) { x.valueDeg = 0.0; x.available = false; }
            if (!std::isfinite(x.pitchProxyDeg)) x.pitchProxyDeg = 0.0;
        }
        source.setSeries(fixed);
    }

    pa::WristAssessmentConfig cfg;
    cfg.band.archetype = archetypeMode < -1 || archetypeMode > 2 ? -1 : archetypeMode;
    const pa::PpWristAssessmentResult r = pa::WristAssessmentEngine::assess(source, bands(), cfg);
    const pa::PpSwingPositionTimeline tl = source.timeline();

    QVariantList dofs;
    for (pa::PpJointDof dof : kDisplayOrder) {
        const pa::PpDofRow &row = r.row(dof);
        if (!row.present) continue;
        const pa::DofMetadata meta = pa::dofMetadata(dof);
        const QString key = keyOf.value(int(dof));
        QVariantList cells;
        for (int p = 0; p < pa::kNumPos; ++p) {
            const pa::PpRagCell &c = row.cells[size_t(p)];
            const bool ok = c.status == pa::PpCellStatus::Ok && std::isfinite(c.deltaDeg);
            QVariantMap cm{ { QStringLiteral("available"), ok }, { QStringLiteral("rag"), ragWord(c.rag) },
                            { QStringLiteral("banded"), c.banded },
                            { QStringLiteral("bandLo"), c.banded ? QVariant(c.bandLo) : nul() },
                            { QStringLiteral("bandHi"), c.banded ? QVariant(c.bandHi) : nul() } };
            if (ok) cm.insert(QStringLiteral("deltaDeg"), c.deltaDeg);
            cells.append(cm);
        }
        dofs.append(QVariantMap{
            { QStringLiteral("dof"), QString::fromLatin1(pa::dofName(dof)) }, { QStringLiteral("metricKey"), key },
            { QStringLiteral("name"), meta.name }, { QStringLiteral("sub"), meta.sub }, { QStringLiteral("axisName"), meta.axisName },
            { QStringLiteral("source"), sourceOf(meta, key) }, { QStringLiteral("confidence"), double(row.confidence) },
            { QStringLiteral("poleTop"), meta.poleTop }, { QStringLiteral("poleBottom"), meta.poleBottom },
            { QStringLiteral("hasStrip"), meta.hasStrip }, { QStringLiteral("cells"), cells } });
    }
    if (dofs.isEmpty()) return {};

    QVariantList cps;
    const pa::WristCheckpoint *k = pa::wristCheckpoints();
    for (int c = 0; c < pa::kNumPos; ++c) {
        const auto &e = tl.positions[size_t(c)];
        QVariantMap m{ { QStringLiteral("tag"), QStringLiteral("P%1").arg(int(k[c].pos) + 1) },
                       { QStringLiteral("phase"), k[c].phase }, { QStringLiteral("note"), QString::fromLatin1(k[c].note) },
                       { QStringLiteral("available"), e.present } };
        if (e.present) m.insert(QStringLiteral("tUs"), qlonglong(e.t_us));
        cps.append(m);
    }

    return { { QStringLiteral("archetype"), r.archetype }, { QStringLiteral("archetypeName"), wristArchetypeName(r.archetype) },
             { QStringLiteral("archetypeMode"), cfg.band.archetype }, { QStringLiteral("resemblance"), resemblance },
             { QStringLiteral("checkpoints"), cps }, { QStringLiteral("dofs"), dofs },
             { QStringLiteral("engineScore"), r.score.total } };
}

QVariantMap regradeWristPayload(const ShotSeries &shot, const QVariantMap &payload, int archetypeMode)
{
    if (payload.isEmpty()) return payload;
    if (payload.value(QStringLiteral("archetypeMode")).toInt() == archetypeMode) return payload;
    return shotWristPayload(shot, archetypeMode, payload.value(QStringLiteral("resemblance")).toMap());
}

} // namespace pinpoint::charts
