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

// THE SESSION SERIES' PURE HALF (refactor 5.5a): one swing document → one ShotSeries, a ledger row → the Charts'
// ChartShotFindings, the board's annotations on a series, and the conditions a snapshot names — moved, not rewritten,
// from the Charts panel's old loader (SessionSeriesModel, a test double since 5.5a: src/Gui/tests/folder_series.h), so the session's series (session_series.h)
// and the tests share one copy. Pure: no file, no QObject.

#include "session_series_extract.h"

#include "../../Vocabulary/Analysis/swing_analysis.h"              // Phase::Impact
#include "../../Vocabulary/Analysis/uncertainty_json.h"            // copyPhaseSampleSigma
#include "../../Coaching/sequence_checks.h"             // the KS checks off the ledger's signal rows
#include "../../Vocabulary/Club/club_vocabulary.h"                 // swingDocDeclaredClub
#include "../../Core/pp_debug.h"
#include "../../Diagnostics/engine/condition_reading.h" // condition → { metricKey, phase }
#include "../../Diagnostics/packs/pack_provider.h"      // condition names and groups
#include "../../Export/swing_paths.h"                   // swingNumberOf: the swing number, the one rule
#include "../../Kinematics/kinematic_sequence_json.h"   // retimeKinematicSequence
#include "session_wrist.h"                               // wristResemblanceOf

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>

namespace pinpoint::charts {

namespace {

// A number, or NaN. QJsonValue::toDouble() answers 0 for a null, and a missing reading must
// never become a zero one (CONTRACT rule 6: missing is "-", never 0).
double num(const QJsonValue &v)
{
    return v.isDouble() ? v.toDouble() : kNaN;
}

bool sameNumber(double a, double b)
{
    return (std::isnan(a) && std::isnan(b)) || a == b;
}

bool sameDiagnostics(const ChartShotFindings &a, const ChartShotFindings &b)
{
    if (a.club != b.club || a.dataWarning != b.dataWarning
        || a.fired != b.fired || a.clean != b.clean || a.values.size() != b.values.size())
        return false;
    if (a.drivenBy.size() != b.drivenBy.size()) return false;
    if (a.sequenceChecks != b.sequenceChecks) return false;
    if (a.wrist != b.wrist) return false;
    for (auto it = a.drivenBy.constBegin(); it != a.drivenBy.constEnd(); ++it) {
        const auto o = b.drivenBy.constFind(it.key());
        if (o == b.drivenBy.constEnd() || o->measureId != it->measureId || o->metricKey != it->metricKey
            || o->phase != it->phase || o->reducer != it->reducer)
            return false;
    }
    for (auto it = a.values.constBegin(); it != a.values.constEnd(); ++it) {
        const auto o = b.values.constFind(it.key());
        if (o == b.values.constEnd()) return false;
        const ChartShotFindings::Value &x = it.value(), &y = o.value();
        if (!sameNumber(x.value, y.value) || !sameNumber(x.z, y.z) || !sameNumber(x.lo, y.lo)
            || !sameNumber(x.hi, y.hi) || x.dir != y.dir || !sameNumber(x.confidence, y.confidence))
            return false;
    }
    return true;
}

// One shot's ledger row as the panel reads it. Fired and clean rows carry their graded value; a
// not-assessable row carries nothing, because there is no value to carry (the fixture's rule,
// build_fixture.py, which the mocks were drawn from).
// The reading a condition is drawn at: the one `measureId` names when the condition reads it,
// else the condition's first reading with a curve key, else its first reading (found == false
// when it has none — a latent condition, or one the pack no longer holds).
pinpoint::analysis::ConditionReading readingFor(const pinpoint::analysis::CharacteristicPack *pack,
                                                const QString &conditionId, const QString &measureId)
{
    using pinpoint::analysis::ConditionReading;
    if (!pack) return ConditionReading{};
    const std::vector<ConditionReading> all = pinpoint::analysis::conditionReadings(*pack, conditionId);
    if (!measureId.isEmpty())
        for (const ConditionReading &r : all)
            if (r.measureId == measureId) return r;
    for (const ConditionReading &r : all)
        if (r.hasKey()) return r;
    return all.empty() ? ConditionReading{} : all.front();
}

ChartShotFindings diagnosticsOf(const pinpoint::analysis::ShotRecord &s,
                              const pinpoint::analysis::CharacteristicPack *pack)
{
    using pinpoint::analysis::ShotState;
    ChartShotFindings d;
    d.club        = s.club;
    d.dataWarning = s.dataWarning;
    for (const pinpoint::analysis::ConditionRow &r : s.rows)
        if (r.state == ShotState::NotAssessable && !r.notJudgedSubject.isEmpty()) {
            const pinpoint::analysis::Condition *c = pack ? pack->condition(r.conditionId) : nullptr;
            QStringList &l = d.notJudgedBy[r.notJudgedSubject];
            const QString name = (c && !c->label.isEmpty()) ? c->label : r.conditionId;
            if (!l.contains(name)) l << name;
        }
    for (const pinpoint::analysis::ConditionRow &r : s.rows) {
        if (r.state == ShotState::Fired)      d.fired << r.conditionId;
        else if (r.state == ShotState::Clean) d.clean << r.conditionId;
        else continue;
        ChartShotFindings::Value v;
        v.value = r.value;
        v.z     = r.z;
        // ⚠ The row's corridorLo/Hi, which design §7 already records are NOT the corridor the
        // norm draws (only the stored band the row was graded against). Carried as the ledger
        // has them; the panel must not draw them as the corridor.
        v.lo    = r.corridorLo;
        v.hi    = r.corridorHi;
        v.dir   = r.direction;
        v.confidence = std::isfinite(r.confidence) ? double(r.confidence) : kNaN;
        d.values.insert(r.conditionId, v);
        const pinpoint::analysis::ConditionReading cr = readingFor(pack, r.conditionId, r.drivingMeasureId);
        ChartShotFindings::Reading rd;
        rd.measureId = r.drivingMeasureId;
        rd.metricKey = cr.metricKey;
        rd.phase     = cr.found ? cr.phase : -1;
        rd.reducer   = cr.found ? int(cr.reducer) : -1;
        rd.contextId = r.contextId;
        if (pack && !r.drivingMeasureId.isEmpty())
            if (const pinpoint::analysis::Measure *ms = pack->measure(r.drivingMeasureId)) {
                using pinpoint::analysis::ReducerKind;
                rd.shape        = int(ms->shape);
                rd.sameQuantity = ms->kind != pinpoint::analysis::MeasureKind::Composed
                               && (ms->reducer.kind == ReducerKind::At
                                   || (ms->reducer.kind == ReducerKind::Extremum && !ms->reducer.anchor));
            }
        d.gradedBy.insert(r.conditionId, rd);
        if (r.state == ShotState::Fired) d.drivenBy.insert(r.conditionId, rd);
    }
    d.fired.sort();
    d.clean.sort();
    d.sequenceChecks = pinpoint::analysis::sequenceChecksToVariant(pinpoint::analysis::sequenceChecksOf(s));
    d.wrist          = s.wrist;   // the wrist checkpoint profile, part of the findings (schema 16; DEC-123)
    return d;
}

qint64 stringBytes(const QString &s)
{
    // QArrayData header (~16 B) plus UTF-16 payload; a shared string is counted where it is held.
    return s.isEmpty() ? 0 : 16 + qint64(s.capacity()) * 2;
}

} // namespace

qint64 seriesRelUs(const QJsonValue &v, qint64 originUs)
{
    // The one rule (windowTimeUs), against the document's analysis origin — what the replay's map
    // (analysisDetailFor) is built with, and it has to be: the panel seeks shotReplay with these
    // numbers. A stamped document's origin is 0 (read as written); an older one's is clock.t0_us.
    return qint64(pinpoint::analysis::windowTimeUs(int64_t(v.toDouble()), originUs));
}

int folderShotId(const QString &swingDir)
{
    // The swing number (ID-SWING-NUMBER, DEC-91), read by SwingPaths — the one reader of a swing folder's
    // name (step 2.7b, lint A3), as the diagnostics model reads it (step 2.7a).
    const int n = pinpoint::SwingPaths::swingNumberOf(QDir::cleanPath(swingDir).section(QLatin1Char('/'), -1));
    return n > 0 ? n : -1;
}

std::shared_ptr<ShotSeries> extractShot(const QJsonObject &root,
                                                           const QString &swingDir, QString *whyNot)
{
    const auto fail = [whyNot](const QString &why) {
        if (whyNot) *whyNot = why;
        return std::shared_ptr<ShotSeries>();
    };
    if (root.isEmpty())
        return fail(QStringLiteral("the swing document could not be read"));
    const QJsonValue anv = root.value(QStringLiteral("analysis"));
    if (!anv.isObject())
        return fail(QStringLiteral("not analysed: the swing carries no analysis"));
    const QJsonObject an = anv.toObject();
    const QJsonArray metrics = an.value(QStringLiteral("metrics")).toArray();
    // GATED ON DATA (CONTRACT rule 7). A launch-monitor-only swing carries lm.* metrics and one
    // phase and nothing else; it is a shot with readings, so it is in, and every figure that
    // needs a curve or a P-position finds it has none.
    if (metrics.isEmpty())
        return fail(QStringLiteral("not analysed: the analysis produced no metrics"));

    // The analysis origin (documentAnalysisOriginUs, the one place a document without
    // analysis.timeBase is guessed) — every time below goes through relUs against it.
    const qint64 t0 = qint64(pinpoint::analysis::documentAnalysisOriginUs(root));

    auto shot = std::make_shared<ShotSeries>();
    shot->swingDir = swingDir;
    shot->shotId   = folderShotId(swingDir);
    // The club the SWING declares, before the board's correction (club_vocabulary.h, the one rule: review.club, else the
    // device club, else capture.club.name) — and empty when it declares none, never a stub (R-CLUB-4). The board's
    // correction is applied below (annotateForBoard), so the shot's club is its effective one (refactor 6.2).
    shot->club     = pinpoint::swingDocDeclaredClub(root);
    // The golfer's handedness as the swing records it (the document's `athlete.handedness`, as SwingSummary reads it),
    // so a presenter that names a side glosses it (refactor 6.3b, G-02).
    shot->handedness = root.value(QStringLiteral("athlete")).toObject().value(QStringLiteral("handedness")).toString();

    // ── analysis.metrics ────────────────────────────────────────────────────────────────
    // The field-by-field twin of DiskReplaySource::load's series loop (and so of ShotProcessor's
    // live map): the same keys, the same re-timing, the same "absent means not characterised"
    // rule for σ and validity. Two differences, both towards honesty: a non-number value is NaN
    // rather than 0, and an all-ones `valid` is stored empty, which the types define as the same.
    shot->metrics.reserve(metrics.size());
    for (const QJsonValue &mv : metrics) {
        const QJsonObject m = mv.toObject();
        MetricSeries s;
        s.key = m.value(QStringLiteral("key")).toString();
        if (s.key.isEmpty()) continue;
        if (shot->metrics.contains(s.key)) {
            ppWarn() << "[Charts] duplicate metric" << s.key << "in" << swingDir << "- kept the first";
            continue;
        }
        s.label = m.value(QStringLiteral("label")).toString();
        s.unit  = m.value(QStringLiteral("unit")).toString();

        const QJsonArray ts = m.value(QStringLiteral("t_us")).toArray();
        const QJsonArray vs = m.value(QStringLiteral("value")).toArray();
        const qsizetype n = std::min(ts.size(), vs.size());
        if (ts.size() != vs.size())
            ppWarn() << "[Charts]" << s.key << "in" << swingDir << "has" << ts.size() << "times and"
                     << vs.size() << "values - kept" << n;
        s.tUs.reserve(size_t(n));
        s.value.reserve(size_t(n));
        for (qsizetype i = 0; i < n; ++i) {
            s.tUs.push_back(seriesRelUs(ts.at(i), t0));
            s.value.push_back(num(vs.at(i)));
        }
        // Per-sample validity (design §5.1): index-parallel, 0 = bridged. ABSENT means every
        // sample is valid, which is how every swing analysed before the field existed reads.
        if (m.contains(QStringLiteral("valid"))) {
            const QJsonArray vd = m.value(QStringLiteral("valid")).toArray();
            const qsizetype nv = std::min(vd.size(), n);
            s.valid.reserve(size_t(nv));
            bool allValid = nv == n;
            for (qsizetype i = 0; i < nv; ++i) {
                const uint8_t ok = vd.at(i).toInt() != 0 ? 1 : 0;
                allValid = allValid && ok;
                s.valid.push_back(ok);
            }
            if (allValid) { s.valid.clear(); s.valid.shrink_to_fit(); }
        }
        if (m.contains(QStringLiteral("sigma")))
            s.sigma = num(m.value(QStringLiteral("sigma")));
        if (m.contains(QStringLiteral("sigmaKind")))
            s.sigmaKind = m.value(QStringLiteral("sigmaKind")).toInt(-1);
        s.lineAngle = m.value(QStringLiteral("lineAngle")).toBool();
        s.routeId   = m.value(QStringLiteral("routeId")).toString();

        const QJsonArray ps = m.value(QStringLiteral("phaseSamples")).toArray();
        s.phaseSamples.reserve(size_t(ps.size()));
        for (const QJsonValue &pv : ps) {
            const QJsonObject p = pv.toObject();
            PhaseSample x;
            x.phase = p.value(QStringLiteral("phase")).toInt(-1);
            x.tUs   = seriesRelUs(p.value(QStringLiteral("t_us")), t0);
            x.value = num(p.value(QStringLiteral("value")));
            x.band  = p.value(QStringLiteral("band")).toString();
            // The reload path's own σ rule, by calling it rather than restating it: present only
            // when the producer characterised one.
            QVariantMap sig;
            pinpoint::analysis::copyPhaseSampleSigma(p, sig);
            const QVariant sv = sig.value(QStringLiteral("sigma"));
            x.sigma = (sv.isValid() && !sv.isNull()) ? sv.toDouble() : kNaN;
            s.phaseSamples.push_back(std::move(x));
        }

        shot->metricOrder << s.key;
        shot->metrics.insert(s.key, std::move(s));
    }

    // ── analysis.phases, and impact ─────────────────────────────────────────────────────
    const int kImpact = int(pinpoint::analysis::Phase::Impact);
    const QJsonArray phases = an.value(QStringLiteral("phases")).toArray();
    shot->phases.reserve(size_t(phases.size()));
    for (const QJsonValue &pv : phases) {
        const QJsonObject p = pv.toObject();
        PhaseMark mk;
        mk.phase = p.value(QStringLiteral("phase")).toInt(-1);
        mk.tUs   = seriesRelUs(p.value(QStringLiteral("t_us")), t0);
        mk.conf  = num(p.value(QStringLiteral("conf")));
        if (mk.phase == kImpact && shot->impactUs < 0) shot->impactUs = mk.tUs;   // the replay's m_impactUs
        shot->phases.push_back(mk);
    }

    // ── analysis.kinematicSequence — the reload's one re-timing, so the strip and the panel
    //    read the same instants (kinematic_sequence_json.h).
    if (an.contains(QStringLiteral("kinematicSequence"))) {
        shot->kinematicSequence = pinpoint::analysis::retimeKinematicSequence(
            an.value(QStringLiteral("kinematicSequence")).toObject(),
            [t0](const QJsonValue &v) { return seriesRelUs(v, t0); });
        // The sequence's impact is the same instant on the same clock; it stands in only when the
        // phase ladder has none. Never the replay's thumbnail/midpoint fallback: that is a place
        // to park a playhead, not an impact, and impactUs −1 is the honest "none".
        if (shot->impactUs < 0 && shot->kinematicSequence.contains(QStringLiteral("impactUs")))
            shot->impactUs = shot->kinematicSequence.value(QStringLiteral("impactUs")).toLongLong();
    }
    // ── analysis.gates — why readings were refused (gate_record.h); impact-relative, kept as stored.
    if (an.contains(QStringLiteral("gatesVersion"))) {
        shot->gatesVersion = an.value(QStringLiteral("gatesVersion")).toInt();
        shot->gates        = an.value(QStringLiteral("gates")).toArray().toVariantList();
    }

    // ── the wrist resemblance index: the analysis' own (analysis.score.resemblance, kept by loadAnalysis: the score object
    //    is inline in the root chunk). The checkpoint profile itself is the findings' (diagnostics.wrist, schema 16; DEC-123):
    //    nothing is graded here.
    shot->wristResemblance = wristResemblanceOf(an);
    return shot;
}

qint64 approxShotBytes(const ShotSeries &s)
{
    qint64 b = qint64(sizeof(ShotSeries));
    b += stringBytes(s.swingDir) + stringBytes(s.club);
    b += qint64(s.phases.capacity() * sizeof(PhaseMark));
    // QHash: a span slot plus a node (key + value) per entry; the keys are shared with metricOrder.
    b += qint64(s.metrics.capacity()) * qint64(sizeof(QString) + sizeof(MetricSeries) + 8);
    for (const MetricSeries &m : s.metrics) {
        b += stringBytes(m.key) + stringBytes(m.label) + stringBytes(m.unit);
        b += qint64(m.tUs.capacity() * sizeof(qint64) + m.value.capacity() * sizeof(double)
                    + m.valid.capacity() * sizeof(uint8_t)
                    + m.phaseSamples.capacity() * sizeof(PhaseSample));
        for (const PhaseSample &p : m.phaseSamples) b += stringBytes(p.band);
    }
    b += qint64(s.metricOrder.size()) * qint64(sizeof(QString));
    // The sequence is a nested QVariantMap; its compact JSON text is a fair proxy, doubled for
    // the QVariant/QMap node overhead around each value.
    if (!s.kinematicSequence.isEmpty())
        b += 2 * qint64(QJsonDocument(QJsonObject::fromVariantMap(s.kinematicSequence))
                            .toJson(QJsonDocument::Compact).size());
    const ChartShotFindings &d = s.diagnostics;
    // the wrist checkpoint profile (≤ 5 DOFs × 8 cells × 3 styles, small)
    b += qint64(d.wrist.rows.size()) * qint64(sizeof(pinpoint::analysis::WristProfileRow) + 3 * 8 * 16);
    b += stringBytes(d.club);
    b += qint64(d.fired.size() + d.clean.size()) * qint64(sizeof(QString));
    b += qint64(d.values.capacity()) * qint64(sizeof(QString) + sizeof(ChartShotFindings::Value) + 8);
    return b;
}

void seriesAnnotateForBoard(ShotSeries &shot, const pinpoint::analysis::BoardAnnotation &a)
{
    using namespace pinpoint::analysis;
    if (!a.any()) return;
    shot.club = annotatedClub(shot.club, a);
    if (!a.excluded) return;
    for (auto m = shot.metrics.begin(); m != shot.metrics.end();)
        m = isLaunchMonitorKey(m.key()) ? shot.metrics.erase(m) : std::next(m);
    shot.metricOrder.removeIf([](const QString &k) { return isLaunchMonitorKey(k); });
}

void seriesLinkConditions(SessionSnapshot &snapshot, const pinpoint::analysis::CharacteristicPack *pack)
{
    SessionSnapshot *snap = &snapshot;
    // Every condition that fired on some shot, named and grouped by the shipped catalogue, and
    // linked to the reading it grades (condition_reading.h, design §6.4). A condition with several
    // readings (early extension: pelvis thrust OR spine-angle loss) is linked to the one the
    // session's fired rows name most often as their driving measure — ties to the pack's
    // detectedBy order — and, where no row names one, to its first reading with a curve key. Each
    // shot's own driving reading stays on the shot (ChartShotFindings::drivenBy).
    QHash<QString, QHash<QString, int>> named;          // condition → driving measure → fired rows
    for (const auto &shot : snap->shots)
        for (auto it = shot->diagnostics.drivenBy.constBegin(); it != shot->diagnostics.drivenBy.constEnd(); ++it)
            if (!it->measureId.isEmpty()) ++named[it.key()][it->measureId];
    for (const auto &shot : snap->shots) {
        for (const QString &cid : shot->diagnostics.fired) {
            if (snap->conditions.contains(cid)) continue;
            ConditionInfo ci;
            const pinpoint::analysis::Condition *c = pack ? pack->condition(cid) : nullptr;
            ci.name  = (c && !c->label.isEmpty()) ? c->label : cid;
            ci.group = c ? pinpoint::analysis::conditionGroupName(c->group) : QString();
            QString chosen;
            if (pack) {
                const QHash<QString, int> counts = named.value(cid);
                int best = 0;
                for (const pinpoint::analysis::ConditionReading &r : pinpoint::analysis::conditionReadings(*pack, cid)) {
                    const int n = counts.value(r.measureId);
                    if (n > best) { best = n; chosen = r.measureId; }   // strict: the earlier wins a tie
                }
            }
            const pinpoint::analysis::ConditionReading r = readingFor(pack, cid, chosen);
            ci.metricKey = r.metricKey;
            ci.phase     = r.found ? r.phase : -1;
            ci.reducer   = r.found ? int(r.reducer) : -1;
            snap->conditions.insert(cid, ci);
        }
    }
}

ChartShotFindings seriesDiagnosticsOf(const pinpoint::analysis::ShotRecord &s, const pinpoint::analysis::CharacteristicPack *pack)
{
    return diagnosticsOf(s, pack);
}

bool seriesSameDiagnostics(const ChartShotFindings &a, const ChartShotFindings &b) { return sameDiagnostics(a, b); }

} // namespace pinpoint::charts
