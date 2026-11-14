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

// THE ONE SERIALISER of the `analysis` blocks the pipeline itself produces (their types are in
// swing_analysis.h): `score`, `metrics[]`, `versions`, `filter`, `assessment`, `timings` and
// `bindings`. (`path` is in Export/analysis_path_json.h: it spells the `origin` key, which no
// analysis code may — swing_doc_test's provenance guard, the sibling of CT-I37.) Refactor step 1.3a (information_model.md §8 "One serialiser per analysis
// block"): each writer sits beside its typed reader; swing_doc_analysis.cpp composes the document.
//
// Readers: score, metrics, versions and filter have one. `assessment` (re-derived on every
// analysis, read by score.py), `timings` (wall-clock telemetry) and `bindings` (read on
// re-analysis into the job's ImuSegmentBinding, a different type, by the Recording's loader) have
// a writer only: nothing reads them back as SwingAnalysis.
//
// TIMES go through windowTimeUs (swing_analysis.h): `t0Us` 0 = as written. Only `metrics` carries
// instants; the other blocks hold durations, versions and names.

#include "swing_analysis.h"
#include "uncertainty_json.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>

namespace pinpoint::analysis {

// ── analysis.score ────────────────────────────────────────────────────────────────────────────

// The score breakdown (schema /3+, design §B.0/§B.7). Always carries kind + overall; resemblance
// fields only for the Wrist estimand; the uncertainty interval only once computed (WP-4). Pre-/3
// docs stored a bare int here — every reader takes the overall through overallScoreFromAnalysis(),
// which reads both forms. `metrics` (the audit trail) is not persisted.
inline QJsonObject scoreToJson(const ScoreBreakdown &s)
{
    QJsonObject o;
    o[QStringLiteral("kind")] = (s.kind == ScoreKind::Resemblance)
                                    ? QStringLiteral("resemblance") : QStringLiteral("adherence");
    o[QStringLiteral("overall")] = s.overall;
    if (s.kind == ScoreKind::Resemblance) {
        QJsonObject res;
        for (auto it = s.resemblance.constBegin(); it != s.resemblance.constEnd(); ++it)
            res[it.key()] = it.value();
        o[QStringLiteral("resemblance")] = res;
        o[QStringLiteral("pattern")]     = s.patternLabel;
        o[QStringLiteral("blended")]     = s.blended;
    }
    if (s.interval.valid())
        o[QStringLiteral("interval")] = QJsonObject{
            { QStringLiteral("halfWidth"), s.interval.halfWidth },
            { QStringLiteral("lo"),        s.interval.lo },
            { QStringLiteral("hi"),        s.interval.hi } };
    // Adherence contribution maps (§B.0). Additive and OMITTED WHEN EMPTY, so a
    // resemblance score's doc is byte-identical to before this key existed. A score
    // the athlete can take apart is worth more than one they can only read.
    auto emitBuckets = [&o](const char *name, const QHash<QString,int> &h) {
        if (h.isEmpty()) return;
        QJsonObject b;
        for (auto it = h.constBegin(); it != h.constEnd(); ++it)
            b[it.key()] = it.value();
        o[QString::fromLatin1(name)] = b;
    };
    emitBuckets("perRegion", s.perRegion);
    emitBuckets("perPhase",  s.perPhase);
    return o;
}

// Reads the /3 object; a pre-/3 bare int reads as an adherence score with that overall.
inline ScoreBreakdown scoreFromJson(const QJsonValue &v)
{
    ScoreBreakdown s;
    if (v.isDouble()) { s.overall = v.toInt(); return s; }
    const QJsonObject o = v.toObject();
    s.kind    = o.value(QStringLiteral("kind")).toString() == QLatin1String("resemblance")
                    ? ScoreKind::Resemblance : ScoreKind::Adherence;
    s.overall = o.value(QStringLiteral("overall")).toInt();
    const auto buckets = [&o](const char *name) {
        QHash<QString, int> h;
        const QJsonObject b = o.value(QString::fromLatin1(name)).toObject();
        for (auto it = b.constBegin(); it != b.constEnd(); ++it) h.insert(it.key(), it.value().toInt());
        return h;
    };
    if (s.kind == ScoreKind::Resemblance) {
        s.resemblance  = buckets("resemblance");
        s.patternLabel = o.value(QStringLiteral("pattern")).toString();
        s.blended      = o.value(QStringLiteral("blended")).toBool();
    }
    const QJsonObject iv = o.value(QStringLiteral("interval")).toObject();
    if (!iv.isEmpty()) {
        s.interval.halfWidth = iv.value(QStringLiteral("halfWidth")).toInt(-1);
        s.interval.lo        = iv.value(QStringLiteral("lo")).toInt(-1);
        s.interval.hi        = iv.value(QStringLiteral("hi")).toInt(-1);
    }
    s.perRegion = buckets("perRegion");
    s.perPhase  = buckets("perPhase");
    return s;
}

// ── analysis.metrics[] ────────────────────────────────────────────────────────────────────────

// One metric series. Every optional field is emitted ONLY when its producer set it, so every
// series that predates the field serialises exactly as before (the optional-absence contract).
// `bandLo` / `bandHi` and `flexPositive` are not persisted.
inline QJsonObject metricSeriesToJson(const MetricSeries &m, int64_t t0Us)
{
    const auto rel = [t0Us](int64_t v) -> qint64 { return qint64(windowTimeUs(v, t0Us)); };
    QJsonArray ts, vs, samples;
    for (const int64_t t : m.t_us) ts.append(rel(t));
    for (const double v : m.value) vs.append(v);
    for (const PhaseSample &ps : m.phaseSamples) {
        QJsonObject po{ { QStringLiteral("phase"), int(ps.phase) },
                        { QStringLiteral("t_us"),  rel(ps.t_us) },
                        { QStringLiteral("value"), ps.value },
                        { QStringLiteral("band"),  ps.band } };
        insertPhaseSampleSigma(po, ps);   // per-instant σ — only when set
        samples.append(po);
    }
    QJsonObject mo{ { QStringLiteral("key"),   m.key },
                    { QStringLiteral("label"), m.label },
                    { QStringLiteral("unit"),  m.unit },
                    { QStringLiteral("t_us"),  ts },
                    { QStringLiteral("value"), vs },
                    { QStringLiteral("phaseSamples"), samples } };
    // 1σ measurement uncertainty — emitted ONLY when the producer actually
    // propagated an error budget. Absent means "not characterised", never
    // "zero", so every metric that predates this field serialises exactly as
    // before (the optional-absence contract at the serialization layer).
    if (m.sigma)
        mo.insert(QStringLiteral("sigma"), *m.sigma);
    if (m.sigmaKind > 0)
        mo.insert(QStringLiteral("sigmaKind"), int(m.sigmaKind));
    // Per-sample validity, parallel to t_us (design §5.1): 0 marks a sample the grid
    // BRIDGED across a gated or absent run, so a reader knows which part of the curve
    // was measured and which was drawn between measurements. Same optional-absence
    // contract as `sigma` directly above — emitted ONLY when at least one sample is
    // actually invalid, so every metric with nothing to mark serialises byte-identically
    // to before the field existed. The QML bridge (shot_processor.cpp toAnalysisDetail)
    // and the reload (disk_replay_source.cpp) carry the same key on the same rule.
    if (std::find(m.valid.begin(), m.valid.end(), uint8_t(0)) != m.valid.end()) {
        QJsonArray vd;
        for (const uint8_t f : m.valid) vd.append(int(f));
        mo.insert(QStringLiteral("valid"), vd);
    }
    // The producer route and its quality (MetricSeries::routeId), same optional-absence
    // contract: written only when the producer stated a route, so every other series
    // serialises byte-identically. The phase grid (measure_sample.cpp) is the reader.
    if (!m.routeId.isEmpty())
        mo.insert(QStringLiteral("routeId"), m.routeId);
    if (m.routeDirect)
        mo.insert(QStringLiteral("routeDirect"), true);
    // Drawn but not judged (MetricSeries::withheld), same contract: only when set.
    if (!m.withheld.isEmpty())
        mo.insert(QStringLiteral("withheld"), m.withheld);
    // An angle of a line (MetricSeries::lineAngle), same contract: only when set.
    if (m.lineAngle)
        mo.insert(QStringLiteral("lineAngle"), true);
    return mo;
}

inline MetricSeries metricSeriesFromJson(const QJsonObject &mo, int64_t t0Us = 0)
{
    MetricSeries m;
    m.key   = mo.value(QStringLiteral("key")).toString();
    m.label = mo.value(QStringLiteral("label")).toString();
    m.unit  = mo.value(QStringLiteral("unit")).toString();
    for (const QJsonValue &v : mo.value(QStringLiteral("t_us")).toArray())
        m.t_us.push_back(windowTimeUs(int64_t(v.toDouble()), t0Us));
    for (const QJsonValue &v : mo.value(QStringLiteral("value")).toArray())
        m.value.push_back(v.toDouble());
    for (const QJsonValue &v : mo.value(QStringLiteral("phaseSamples")).toArray()) {
        const QJsonObject po = v.toObject();
        PhaseSample ps;
        ps.phase = Phase(po.value(QStringLiteral("phase")).toInt());
        ps.t_us  = windowTimeUs(int64_t(po.value(QStringLiteral("t_us")).toDouble()), t0Us);
        ps.value = po.value(QStringLiteral("value")).toDouble();
        ps.band  = po.value(QStringLiteral("band")).toString();
        readPhaseSampleSigma(po, ps);
        m.phaseSamples.push_back(ps);
    }
    if (mo.contains(QStringLiteral("sigma"))) m.sigma = mo.value(QStringLiteral("sigma")).toDouble();
    m.sigmaKind = uint8_t(mo.value(QStringLiteral("sigmaKind")).toInt(0));
    for (const QJsonValue &v : mo.value(QStringLiteral("valid")).toArray())
        m.valid.push_back(uint8_t(v.toInt()));
    m.routeId     = mo.value(QStringLiteral("routeId")).toString();
    m.routeDirect = mo.value(QStringLiteral("routeDirect")).toBool();
    m.withheld    = mo.value(QStringLiteral("withheld")).toString();
    m.lineAngle   = mo.value(QStringLiteral("lineAngle")).toBool();
    return m;
}

inline QJsonArray metricsToJson(const std::vector<MetricSeries> &series, int64_t t0Us)
{
    QJsonArray out;
    for (const MetricSeries &m : series) out.append(metricSeriesToJson(m, t0Us));
    return out;
}

inline std::vector<MetricSeries> metricsFromJson(const QJsonArray &a, int64_t t0Us = 0)
{
    std::vector<MetricSeries> out;
    for (const QJsonValue &v : a) out.push_back(metricSeriesFromJson(v.toObject(), t0Us));
    return out;
}

// ── analysis.versions ─────────────────────────────────────────────────────────────────────────

// Producer versions (analysis_versions.h): what produced pose / ball / shaft. Re-analysis reuses a
// recorded stage output when these match the code that would run now. Written only when stamped();
// absent on swings analysed before 2026-09-09. The later stages ONLY when they ran — a
// single-camera swing's block is byte-identical to before they existed.
inline QJsonObject analysisVersionsToJson(const AnalysisVersions &vs)
{
    QJsonObject v{
        { QStringLiteral("pose"),  QJsonObject{ { QStringLiteral("code"),  vs.pose },
                                                { QStringLiteral("model"), vs.poseModel },
                                                { QStringLiteral("scope"), vs.poseScope } } },
        { QStringLiteral("ball"),  QJsonObject{ { QStringLiteral("code"),  vs.ball } } },
        { QStringLiteral("shaft"), QJsonObject{ { QStringLiteral("code"),  vs.shaft } } },
        { QStringLiteral("impact"), QJsonObject{ { QStringLiteral("code"), vs.impact } } } };
    if (vs.poseDtl > 0)
        v.insert(QStringLiteral("poseDtl"),  QJsonObject{ { QStringLiteral("code"),  vs.poseDtl },
                                                          { QStringLiteral("model"), vs.poseDtlModel } });
    const auto code = [&v](const char *k, int c) {
        if (c > 0) v.insert(QString::fromLatin1(k), QJsonObject{ { QStringLiteral("code"), c } });
    };
    code("shaftDtl", vs.shaftDtl);
    code("shaftFusion", vs.shaftFusion);
    code("skeleton3d", vs.skeleton3d);
    code("addressMarks", vs.addressMarks);
    code("dtlPosture", vs.dtlPosture);
    code("shaftDirection", vs.shaftDirection);
    code("skeleton3dBody", vs.skeleton3dBody);
    code("hipInternalRotation", vs.hipInternalRotation);
    code("shoulderTiltVsBend", vs.shoulderTiltVsBend);
    code("clubSlotTop", vs.clubSlotTop);
    return v;
}

inline AnalysisVersions analysisVersionsFromJson(const QJsonObject &v)
{
    AnalysisVersions vs;
    const auto code = [&v](const char *k) {
        return v.value(QString::fromLatin1(k)).toObject().value(QStringLiteral("code")).toInt(0);
    };
    const QJsonObject pose = v.value(QStringLiteral("pose")).toObject();
    vs.pose      = pose.value(QStringLiteral("code")).toInt(0);
    vs.poseModel = pose.value(QStringLiteral("model")).toString();
    vs.poseScope = pose.value(QStringLiteral("scope")).toString();
    vs.ball      = code("ball");
    vs.shaft     = code("shaft");
    vs.impact    = code("impact");
    vs.poseDtl      = code("poseDtl");
    vs.poseDtlModel = v.value(QStringLiteral("poseDtl")).toObject().value(QStringLiteral("model")).toString();
    vs.shaftDtl            = code("shaftDtl");
    vs.shaftFusion         = code("shaftFusion");
    vs.skeleton3d          = code("skeleton3d");
    vs.addressMarks        = code("addressMarks");
    vs.dtlPosture          = code("dtlPosture");
    vs.shaftDirection      = code("shaftDirection");
    vs.skeleton3dBody      = code("skeleton3dBody");
    vs.hipInternalRotation = code("hipInternalRotation");
    vs.shoulderTiltVsBend  = code("shoulderTiltVsBend");
    vs.clubSlotTop         = code("clubSlotTop");
    return vs;
}

// ── analysis.filter ───────────────────────────────────────────────────────────────────────────

// The orientation-filter quality block (SwingLab filter.refuse — §5.3.1). Only present when
// offline re-fusion drove the orientation (filterImpactStepDeg >= 0), giving filter.* an IMU-only
// objective (score.py filter.*).
inline QJsonObject filterToJson(double impactStepDeg)
{
    return QJsonObject{ { QStringLiteral("impactStepDeg"), impactStepDeg } };
}

inline double filterFromJson(const QJsonObject &f)
{
    return f.value(QStringLiteral("impactStepDeg")).toDouble(-1.0);
}

// ── analysis.assessment (writer only) ─────────────────────────────────────────────────────────

// The AI-COACH feedback feed (design §B.0): lead-wrist faults/strengths, decoupled from the
// headline resemblance score. Written whenever the assessment stage ran (assessmentScore >= 0) —
// live capture, re-analysis and SwingLab alike (#110) — and the SwingLab known-groups diagnosis
// input. score.py reads `findings[]` for fault recall / FP-on-clean. scoreV2 is retained as
// telemetry only — it is NOT a score (its confidence-coupled central term is removed in WP-4).
inline QJsonObject assessmentToJson(const std::vector<PpWristFinding> &findingsIn, int scoreV2)
{
    QJsonArray findings;
    for (const PpWristFinding &f : findingsIn) {
        QJsonArray dofs, positions;
        for (const PpJointDof d : f.dofs)      dofs.append(int(d));
        for (const PpSwingPosition p : f.positions) positions.append(int(p));
        findings.append(QJsonObject{
            { QStringLiteral("id"),            f.id },
            { QStringLiteral("name"),          f.name },
            { QStringLiteral("category"),      f.category },
            { QStringLiteral("severity"),      QString::fromLatin1(severityName(f.severity)) },
            { QStringLiteral("magnitudeDeg"),  f.magnitudeDeg },
            { QStringLiteral("weight"),        f.weight },
            { QStringLiteral("confidence"),    double(f.confidence) },
            { QStringLiteral("lowConfidence"), f.lowConfidence },
            { QStringLiteral("dofs"),          dofs },
            { QStringLiteral("positions"),     positions } });
    }
    return QJsonObject{
        { QStringLiteral("scoreV2"),  scoreV2 },
        { QStringLiteral("findings"), findings } };
}

// ── analysis.timings (writer only) ────────────────────────────────────────────────────────────

// Per-stage analyzer wall times (plan §2 telemetry): how long each heavy stage took, so every
// live shot self-reports the < 20 s budget. Written only when measured (totalMs >= 0); a stage
// that did not run stays -1.
inline QJsonObject timingsToJson(const AnalysisTimings &t)
{
    QJsonObject timings{
        { QStringLiteral("poseMs"),  t.poseMs },
        { QStringLiteral("ballMs"),  t.ballMs },
        { QStringLiteral("shaftMs"), t.shaftMs },
        { QStringLiteral("impactMs"), t.impactMs },
        { QStringLiteral("totalMs"), t.totalMs } };
    // The DTL pose pass (DtlPoseStage, smoothing included) — measured since the
    // stage existed and never written; -1 = it did not run.
    timings.insert(QStringLiteral("poseDtlMs"), t.poseDtlMs);
    // Where each camera's pose pass went (pose_inference_performance_plan.md
    // step 0): session build / decode / preprocess / Run() / heatmap decode /
    // wall total / frames. Only for a camera actually posed this run — a reused
    // or loaded track has no split, and a zero split would read as "free".
    QJsonObject pose;
    if (t.poseFaceOn.measured())
        pose.insert(QStringLiteral("faceOn"), t.poseFaceOn.toJson());
    if (t.poseDtl.measured())
        pose.insert(QStringLiteral("dtl"), t.poseDtl.toJson());
    if (!pose.isEmpty())
        timings.insert(QStringLiteral("pose"), pose);
    // Every stage as it ran (analysis_dag_design.md step A): wall ms, start/end offset
    // from the start of the analysis, the pool thread, and a skip's reason — the timeline
    // tools/analysis/analysis_dag.py draws. 0.01 ms is below anything a stage measures
    // and keeps a 39-stage list near 4 kB.
    if (!t.stages.empty()) {
        const auto r2 = [](double v) { return double(qRound64(v * 100.0)) / 100.0; };
        QJsonArray stages;
        for (const StageTiming &s : t.stages) {
            QJsonObject so{
                { QStringLiteral("name"),    s.name },
                { QStringLiteral("ran"),     s.ran },
                { QStringLiteral("ms"),      r2(s.ms) },
                { QStringLiteral("startMs"), r2(s.startMs) },
                { QStringLiteral("endMs"),   r2(s.endMs) },
                { QStringLiteral("thread"),  s.thread } };
            if (!s.source.isEmpty())
                so.insert(QStringLiteral("source"), s.source);
            if (!s.skipReason.isEmpty())
                so.insert(QStringLiteral("skipReason"), s.skipReason);
            stages.append(so);
        }
        timings.insert(QStringLiteral("stages"), stages);
    }
    return timings;
}

// ── analysis.bindings (writer only) ───────────────────────────────────────────────────────────

// The IMU-binding snapshot (SwingLab): the per-device calibration (A/M) keyed by device serial, so
// the offline runner can re-fuse a recorded swing with the exact anatomical transforms the app
// used. Written only when non-empty.
inline QJsonArray bindingsToJson(const std::vector<BindingRecord> &bindings)
{
    QJsonArray binds;
    for (const BindingRecord &b : bindings) {
        QJsonObject bo{
            { QStringLiteral("serial"),   b.serial },
            { QStringLiteral("role"),     int(b.role) },
            { QStringLiteral("roleName"), segmentRoleName(b.role) },
            { QStringLiteral("alignA"), QJsonArray{ b.alignA.scalar(), b.alignA.x(),
                                                    b.alignA.y(), b.alignA.z() } },
            { QStringLiteral("mountM"), QJsonArray{ b.mountM.scalar(), b.mountM.x(),
                                                    b.mountM.y(), b.mountM.z() } },
            // Calibration status at shot time (additive — SwingLab corpus
            // provenance). calibAgeSec -1 / empty calibratedAt = never.
            { QStringLiteral("calibrated"),           b.calibrated },
            { QStringLiteral("anatCalibrated"),       b.anatCalibrated },
            { QStringLiteral("mountDeviationDeg"),    b.mountDeviationDeg },
            { QStringLiteral("mountGravityErrorDeg"), b.mountGravityErrorDeg },
            { QStringLiteral("calibratedAt"),         b.calibratedAtUtc },
            { QStringLiteral("calibAgeSec"),          b.calibAgeSec },
            // ⚠ WHICH INSTRUMENT, and it is load-bearing on re-analysis rather
            // than merely informative. This array takes precedence over the
            // per-stream fallback when a swing is re-read, so without this key a
            // re-analysed HackMotion swing would skip the raw-quaternion conjugate
            // and invert every wrist sign. See BindingRecord::hackMotion.
            { QStringLiteral("hackMotion"),           b.hackMotion } };
        // The trunk stand-tall solve, when it set A/M (trunk design §6.8): calibrationSource
        // + trunkSolve, both absent otherwise (trunk_calib_provenance.h).
        writeTrunkCalibProvenance(bo, b.trunkCalib);
        binds.append(bo);
    }
    return binds;
}

} // namespace pinpoint::analysis
