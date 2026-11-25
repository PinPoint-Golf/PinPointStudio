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

#include "measure_dump_core.h"
#include "../../src/Export/phase_grid_store.h"   // the phase grid's file door: a tool over bare folders (P-3)

#include "Vocabulary/Club/club_vocabulary.h"
#include "Diagnostics/norms/norm_measure_source.h"
#include "Export/swing_doc.h"
#include "Export/swing_store.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>

#include <cmath>

namespace measure_dump {

using namespace pinpoint::analysis;

// ── CSV ─────────────────────────────────────────────────────────────────────

QStringList csvColumns()
{
    return {
        QStringLiteral("sessionPath"), QStringLiteral("session"), QStringLiteral("swing"),
        QStringLiteral("club"), QStringLiteral("context"), QStringLiteral("faceOn"), QStringLiteral("dtl"),
        QStringLiteral("devices"), QStringLiteral("captureWithheld"),
        QStringLiteral("measure"), QStringLiteral("partMeasure"), QStringLiteral("kind"), QStringLiteral("status"),
        QStringLiteral("value"), QStringLiteral("unit"), QStringLiteral("hasCorridor"), QStringLiteral("z"),
        QStringLiteral("grade"), QStringLiteral("implausible"), QStringLiteral("withheld"),
        QStringLiteral("refusal"), QStringLiteral("refusalText"), QStringLiteral("gate"),
        QStringLiteral("atStart"), QStringLiteral("atEnd"), QStringLiteral("measSigma"),
        QStringLiteral("weight"), QStringLiteral("sign"), QStringLiteral("share"),
        QStringLiteral("weightRead"), QStringLiteral("weightTotal"),
    };
}

QString csvNumber(double v)
{
    if (!std::isfinite(v)) return QString();
    return QString::number(v, 'g', 10);
}

static QString triState(int v) { return v < 0 ? QString() : QString::number(v); }

QStringList rowFields(const Row &r)
{
    return {
        r.sessionPath, r.session, r.swing, r.club, r.contextId, triState(r.faceOn), triState(r.dtl),
        r.devices, triState(r.captureWithheld),
        r.measure, r.partMeasure, r.kind, r.status,
        csvNumber(r.value), r.unit, triState(r.hasCorridor), csvNumber(r.z),
        r.grade, triState(r.implausible), r.withheld,
        r.refusal, r.refusalText, r.gate,
        csvNumber(r.atStart), csvNumber(r.atEnd), csvNumber(r.measSigma),
        csvNumber(r.weight), r.sign == 0 ? QString() : QString::number(r.sign), csvNumber(r.share),
        csvNumber(r.weightRead), csvNumber(r.weightTotal),
    };
}

static QString csvQuote(const QString &s)
{
    if (!s.contains(QLatin1Char(',')) && !s.contains(QLatin1Char('"')) && !s.contains(QLatin1Char('\n'))
        && !s.contains(QLatin1Char('\r')))
        return s;
    QString q = s;
    q.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + q + QLatin1Char('"');
}

QString csvLine(const Row &r)
{
    QStringList f = rowFields(r);
    for (QString &s : f) s = csvQuote(s);
    return f.join(QLatin1Char(','));
}

QString missingKindToken(MissingKind k)
{
    switch (k) {
    case MissingKind::None:                     return QString();
    case MissingKind::UnknownMeasure:           return QStringLiteral("UnknownMeasure");
    case MissingKind::NoMetricBinding:          return QStringLiteral("NoMetricBinding");
    case MissingKind::NoAnalysis:               return QStringLiteral("NoAnalysis");
    case MissingKind::NoLaunchMonitor:          return QStringLiteral("NoLaunchMonitor");
    case MissingKind::LaunchMonitorFieldAbsent: return QStringLiteral("LaunchMonitorFieldAbsent");
    case MissingKind::MetricNotProduced:        return QStringLiteral("MetricNotProduced");
    case MissingKind::PhaseNotSegmented:        return QStringLiteral("PhaseNotSegmented");
    case MissingKind::WindowNotCovered:         return QStringLiteral("WindowNotCovered");
    case MissingKind::CaptureDataIssue:         return QStringLiteral("CaptureDataIssue");
    case MissingKind::SessionScope:             return QStringLiteral("SessionScope");
    case MissingKind::CompositeCoverage:        return QStringLiteral("CompositeCoverage");
    }
    return QStringLiteral("Unknown");
}

// ── Selection ───────────────────────────────────────────────────────────────

QStringList selectMeasures(const CharacteristicPack &pack, const Options &opt)
{
    QStringList out;
    QSet<QString> seen;
    auto add = [&](const QString &id) {
        if (id.isEmpty() || seen.contains(id)) return;
        seen.insert(id);
        out << id;
    };
    if (!opt.measures.isEmpty()) {
        for (const QString &id : opt.measures) add(id.trimmed());
    } else {
        for (const Measure &m : pack.measures) {
            if (m.scope == MeasureScope::Session) continue;
            if (!opt.prefix.isEmpty() && !m.id.startsWith(opt.prefix)) continue;
            add(m.id);
        }
    }
    // A composite's alternatives, every rung, as plain measures.
    const QStringList base = out;
    for (const QString &id : base) {
        const Measure *m = pack.measure(id);
        if (!m || m->kind != MeasureKind::Composite) continue;
        for (const CompositePartSpec &p : m->composite.parts)
            for (const QString &alt : p.measures) add(alt);
    }
    return out;
}

// ── Reading ─────────────────────────────────────────────────────────────────

SwingPhaseGrid readGridReadOnly(const QString &swingDir, QJsonObject *rootOut, const PhaseGridConfig &cfg)
{
    // THE OWNER'S READ (refactor 5.3): the file door's read (PhaseGridStore; P-3) with SidecarPolicy::ParseNoWrite, as the session owner's firing
    // reader takes it (Export/firing_reader_store.cpp; the parallel run's policy): a current sidecar answers, else the
    // document is parsed, and nothing is written. Until 5.3 this was readPhaseGrid() copied line for line minus its write.
    // PhaseGridStore::readForMeasures adds the pack's millisecond spans; here the caller's `cfg` names them.
    pinpoint::SwingStore::LazyDocument parsed(swingDir);
    SwingPhaseGrid grid = pinpoint::PhaseGridStore::read(swingDir, pinpoint::SidecarPolicy::ParseNoWrite, cfg, &parsed);
    if (grid.swingDir.isEmpty()) grid.swingDir = swingDir;
    if (rootOut) *rootOut = parsed.loaded() ? parsed.root() : QJsonObject();
    return grid;
}

QStringList swingDirsOf(const QString &dir)
{
    const QString abs = QDir(dir).absolutePath();
    if (pinpoint::SwingStore::hasDocument(abs)) return { abs };
    const QDir d(abs);
    QStringList out;
    for (const QString &name : d.entryList(QStringList{ QStringLiteral("swing_*") },
                                           QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        out << d.filePath(name);
    return out;
}

namespace {

// (value − μ) / σ of the side the value fell on — the composite's own part rule (norm_measure_source.h).
double sideZ(double value, const Norm &n)
{
    if (!std::isfinite(value)) return kNaN;
    const double s = value < n.mu ? n.sigmaLo : n.sigmaHi;
    return s > 0.0 ? (value - n.mu) / s : kNaN;
}

QString unreadText(const QString &token)
{
    if (token == QLatin1String("notMeasured")) return QStringLiteral("no value on this shot");
    if (token == QLatin1String("noCorridor"))  return QStringLiteral("no norm to grade it against");
    if (token == QLatin1String("implausible")) return QStringLiteral("reading outside what its norm believes");
    if (token == QLatin1String("withheld"))    return QStringLiteral("drawn but not judged by its producer");
    if (token == QLatin1String("nested"))      return QStringLiteral("a part naming a composite");
    if (token == QLatin1String("notLive"))     return QStringLiteral("not graded: the measure is not live (held or planned)");
    return QString();
}

} // namespace

std::vector<Row> readSwing(const QString &sessionDir, const QString &swingDir,
                           const CharacteristicPack &pack,
                           const std::shared_ptr<const INormProvider> &norms,
                           const QStringList &measureIds, const GradePolicy &policy)
{
    std::vector<Row> rows;

    // THE SESSION OWNER'S PER-SHOT READ AND CONSTRUCTION (refactor 5.3): its firing reader's
    // (SwingStoreFiringReader::read — one lazy parse shared by the phase grid and the summary, the pack's millisecond
    // spans added by PhaseGridStore::readForMeasures) with the parallel run's policy, ParseNoWrite, so nothing is written beside the
    // swing; then fireFindings' (LiveMeasureSource over that grid, withSigma at the owner's tuned default, and
    // detectForSwing's NormMeasureSource at the grade policy). The one difference: no athlete, so Cohort{} — the
    // unisex norms — where the owner grades with the golfer's cohort.
    pinpoint::SwingStore::LazyDocument parsed(swingDir);
    SwingPhaseGrid grid = pinpoint::PhaseGridStore::readForMeasures(swingDir, pack.measures, pinpoint::SidecarPolicy::ParseNoWrite, PhaseGridConfig{}, &parsed);

    const LiveMeasureSource values(std::move(grid), pack);   // withSigma: the tuned default, as FiringInputs'
    const QString           contextId = values.contextId();
    const NormMeasureSource graded(values, norms, contextId, policy, &pack, Cohort{});

    // Shot identity: the summary as the firing reader reads it (ParseNoWrite, the same parse); a swing.ppsw carries it.
    Row base;
    base.sessionPath = QDir(sessionDir).absolutePath();
    base.session     = QFileInfo(base.sessionPath).fileName();
    base.swing       = QFileInfo(swingDir).fileName();
    base.club        = values.club();
    base.contextId   = contextId;
    const pinpoint::SwingSummary sum = pinpoint::SwingDocReader::readSwingSummary(swingDir, pinpoint::SidecarPolicy::ParseNoWrite, &parsed);
    QStringList devices;
    QJsonObject root;
    if (sum.ok) {
        devices = sum.devices;
        base.captureWithheld = (sum.dataWarning
                                && (sum.dataWarningDetail.value(QStringLiteral("capture")).toBool()
                                    || sum.dataWarningDetail.value(QStringLiteral("imu")).toBool())) ? 1 : 0;
    } else {
        root = parsed.root();
        if (!root.isEmpty()) devices = pinpoint::swingDeviceTokens(root);
    }
    parsed.release();
    if (sum.ok || !root.isEmpty()) {
        base.faceOn  = devices.contains(QStringLiteral("faceOn")) ? 1 : 0;
        base.dtl     = devices.contains(QStringLiteral("dtl")) ? 1 : 0;
        base.devices = devices.join(QLatin1Char('+'));
    }

    for (const QString &id : measureIds) {
        const Measure *m = pack.measure(id);
        Row r = base;
        r.measure = id;
        if (m) {
            r.kind   = measureKindName(m->kind);
            r.status = measureStatusName(m->status);
            r.unit   = m->unit;
        }

        const std::optional<MeasureReading> rd = graded.read(id);

        if (m && m->kind == MeasureKind::Composite) {
            // ── the composite row ──
            if (rd) {
                r.value       = rd->value;
                r.hasCorridor = rd->hasCorridor ? 1 : 0;
                r.measSigma   = rd->measSigma;
                r.weightRead  = rd->partsWeightRead;
                r.weightTotal = rd->partsWeightTotal;
                if (rd->hasCorridor) {
                    r.grade       = gradeName(rd->grade);
                    r.implausible = rd->implausible ? 1 : 0;
                    if (norms)
                        if (const NormResolution res = norms->resolve(id, contextId, Cohort{}); res.found())
                            r.z = sideZ(rd->value, *res.norm);
                }
                if (rd->compositeShort) {
                    r.refusal     = missingKindToken(MissingKind::CompositeCoverage);
                    r.refusalText = missingReasonText(MissingKind::CompositeCoverage);
                } else if (!rd->hasCorridor) {
                    r.refusal     = QStringLiteral("noCorridor");
                    r.refusalText = QStringLiteral("no composite norm (or no rho) to combine under");
                }
            } else {
                r.refusal = QStringLiteral("noReading");
            }
            rows.push_back(r);
            if (!rd) continue;

            // ── one row per part read, then per part not read ──
            for (const CompositePart &cp : rd->parts) {
                Row p = base;
                p.measure     = id + QLatin1Char('/') + cp.name;
                p.partMeasure = cp.measureId;
                p.kind        = QStringLiteral("part");
                if (const Measure *pm = pack.measure(cp.measureId)) p.status = measureStatusName(pm->status);
                p.value       = cp.value;
                p.unit        = cp.unit;
                p.hasCorridor = 1;
                p.z           = cp.z;
                p.weight      = cp.weight;
                p.sign        = cp.sign;
                p.share       = cp.share;
                p.atStart     = cp.atStart;
                p.atEnd       = cp.atEnd;
                rows.push_back(p);
            }
            for (const CompositeUnread &u : rd->partsUnread) {
                Row p = base;
                p.measure     = id + QLatin1Char('/') + u.name;
                p.partMeasure = u.measureId;
                p.kind        = QStringLiteral("part");
                if (const Measure *pm = pack.measure(u.measureId)) {
                    p.status = measureStatusName(pm->status);
                    p.unit   = pm->unit;
                }
                for (const CompositePartSpec &ps : m->composite.parts)
                    if (ps.name == u.name) p.weight = ps.weight;
                p.refusal     = u.reason;
                p.refusalText = unreadText(u.reason);
                // The value source's own reason for the last rung, when it had no value at all.
                if (u.reason == QLatin1String("notMeasured")) {
                    const MissingKind mk = values.missingKind(u.measureId);
                    if (mk != MissingKind::None) p.refusalText = missingReasonText(mk);
                }
                rows.push_back(p);
            }
            continue;
        }

        // ── a plain measure ──
        if (!rd) {
            const MissingKind mk = values.missingKind(id);
            r.refusal     = missingKindToken(mk);
            r.refusalText = missingReasonText(mk);
            r.gate        = values.gateCause(id).gate;
            rows.push_back(r);
            continue;
        }

        r.value       = rd->value;
        r.measSigma   = rd->measSigma;
        r.withheld    = rd->withheld;
        r.hasCorridor = rd->hasCorridor ? 1 : 0;
        if (rd->hasCorridor) {
            r.grade       = gradeName(rd->grade);
            r.implausible = rd->implausible ? 1 : 0;
            if (norms)
                if (const NormResolution res = norms->resolve(id, contextId, Cohort{}); res.found())
                    r.z = sideZ(rd->value, *res.norm);
        }
        // The endpoints ride on the value source's answer, not the graded reading.
        if (const std::optional<IMeasureValueSource::Value> v = values.value(id)) {
            r.atStart = v->atStart;
            r.atEnd   = v->atEnd;
        }
        if (!rd->withheld.isEmpty()) {
            r.refusal     = QStringLiteral("withheld");
            r.refusalText = rd->withheld;
            r.gate        = values.gateCause(id).gate;
        } else if (rd->implausible) {
            r.refusal     = QStringLiteral("implausible");
            r.refusalText = unreadText(r.refusal);
        } else if (!rd->hasCorridor) {
            r.refusal     = QStringLiteral("noCorridor");
            r.refusalText = unreadText(r.refusal);
        }
        rows.push_back(r);
    }
    return rows;
}

} // namespace measure_dump
