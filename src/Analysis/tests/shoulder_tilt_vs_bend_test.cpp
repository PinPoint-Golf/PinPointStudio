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

// THE SHOULDER TILT AGAINST THE FORWARD BEND (shoulder_tilt_vs_bend.h; planned-metrics second pass W7,
// Mark's decision of 13 Nov 2026), on synthetic halves:
//   §1 a known tilt and bend give their difference, + steeper than the posture, for every route pair;
//   §2 the reading IS the grader's: the document the pipeline would write, through the diagnostics'
//      own phase grid (measure_sample.cpp buildPhaseGrid + reduceOverGridReading, the `at` reducer),
//      reads the difference of the two graded halves — value and σ — on a curve whose window holds a
//      bridged sample, a half read off its labelled phase sample, a phase the ladder lacks but the series
//      labels, and a ladder instant between samples;
//   §3 not produced when either half is missing at its phase: no series, no record, no cover;
//   §4 σ: quadrature for the same sensor and for mixed halves; a noise-floor half keeps the result a
//      noise floor; no σ on a half, no σ;
//   §5 trust: the halves' kept-value records (Downgraded, Withheld) cover the reading, the others do
//      not; body.tiltBendMixedSources fires exactly when the instruments differ, with its sentence;
//   §6 a withheld half withholds the difference; a second call leaves one reading and one record;
//   §7 no reading because a half is missing: that half's refusals (Refused, Bounded, Withheld) name the
//      reading too, so the shown "not judged" reason (LiveMeasureSource::gateCause, on the written
//      document) is the half's real refusal, e.g. posture.address — never "metric not produced"
//      (W7 review). A missing half with no such record touches nothing.
//   §8 the backstop (FB, 13 Nov 2026): −22° gets body.tiltBendOutOfRange (Downgraded, on the reading and the
//      3-D tilt), −18° does not; idempotent; the floor is a knob. The cameras' shoulder record on the tilt
//      (body3d.shoulderViewsDisagree) is inherited by the reading.

#include "../../Body/shoulder_tilt_vs_bend.h"
#include "../../Diagnostics/measures/measure_sample.h"
#include "../../Metrics/gate_catalogue.h"
#include "../../Diagnostics/measures/live_measure_source.h"
#include "../../Export/swing_doc.h"
#include "../../Export/swing_store.h"

#include <QDir>
#include <QTemporaryDir>

#include <QJsonArray>
#include <QJsonObject>

#include <cmath>
#include <cstdio>
#include <tuple>
#include <vector>

using namespace pinpoint;
using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static bool near(double a, double b, double tol = 1e-9) { return std::isfinite(a) && std::isfinite(b) && std::fabs(a - b) <= tol; }

namespace {

constexpr int64_t kAddr = 1'000'000, kTop = 1'802'500, kImp = 2'100'000;   // the top falls BETWEEN samples

std::vector<PhaseEvent> ladder(bool withTop = true)
{
    std::vector<PhaseEvent> l{ { Phase::Address, kAddr }, { Phase::Impact, kImp } };
    if (withTop) l.insert(l.begin() + 1, PhaseEvent{ Phase::Top, kTop });
    return l;
}

// A curve every 5 ms from 0.8 s to 2.3 s: `f(t)`; σ per phase sample `phaseSigma` (≤ 0: none).
MetricSeries curve(const char *key, const char *route, double (*f)(int64_t), std::vector<Phase> stampAt,
                   double phaseSigma, uint8_t kind = uint8_t(SigmaKind::Propagated))
{
    MetricSeries m;
    m.key = QString::fromLatin1(key);
    m.label = m.key;
    m.unit = QStringLiteral("°");
    m.routeId = QString::fromLatin1(route);
    m.routeDirect = m.routeId == QLatin1String("thoraxImu") || m.routeId == QLatin1String("trunkImus");
    for (int64_t t = 800'000; t <= 2'300'000; t += 5'000) { m.t_us.push_back(t); m.value.push_back(f(t)); }
    for (Phase p : stampAt) {
        const int64_t t = p == Phase::Address ? kAddr : p == Phase::Top ? kTop : kImp;
        PhaseSample ps{ p, t, f(t), QString() };
        if (phaseSigma > 0.0) { ps.sigma = phaseSigma; ps.sigmaKind = kind; }
        m.phaseSamples.push_back(ps);
    }
    return m;
}
// The chest sensor's tilt: −8° at address rising to ≈ +37° near the top, a wobble so the ±15 ms median
// is not the instant's value.
double tiltF(int64_t t)
{
    const double s = double(t - kAddr) / double(kTop - kAddr);
    return -8.0 + 45.0 * std::clamp(s, 0.0, 1.2) + 0.7 * std::sin(double(t) * 1e-4);
}
// The forward bend: 40° at address, a small wobble.
double bendF(int64_t t) { return 40.0 + 0.4 * std::cos(double(t) * 3e-5); }

// THE DOCUMENT THE PIPELINE WRITES, through the real writer (SwingDocWriter::writeSwingJson →
// serializeAnalysis) and read back through the store, so "graded == produced" rests on the writer's own
// metric shape, not a copy of it. clock.t0_us = 0, so the writer's relative-time shift is the identity.
QJsonObject document(const std::vector<MetricSeries> &series, const std::vector<PhaseEvent> &phases,
                     const std::vector<GateRecord> &gates)
{
    static QTemporaryDir tmp;
    static int n = 0;
    const QString dir = tmp.path() + QStringLiteral("/swing_%1").arg(++n);
    QDir().mkpath(dir);
    SwingAnalysis a;
    a.series = series;
    a.phases = phases;
    a.gates = gates;
    a.gatesVersion = kGatesVersion;
    const QJsonObject manifest{
        { QStringLiteral("schema"), QStringLiteral("pinpoint.swing/1") },
        { QStringLiteral("swing"), QJsonObject{ { QStringLiteral("index"), n }, { QStringLiteral("id"), QStringLiteral("swing_%1").arg(n) } } },
        { QStringLiteral("streams"), QJsonArray{} },
        { QStringLiteral("clock"), QJsonObject{ { QStringLiteral("t0_us"), 0.0 } } } };
    QString err;
    if (!SwingDocWriter::writeSwingJson(dir, manifest, &a, &err)) {
        std::printf("    write failed: %s\n", qPrintable(err));
        return {};
    }
    return SwingStore::load(dir).value(QStringLiteral("analysis")).toObject();
}

Reducer at(Phase p)
{
    Reducer r;
    r.kind = ReducerKind::At;
    r.anchor = p;
    return r;
}

const MetricSeries *find(const std::vector<MetricSeries> &v, const QString &k)
{
    for (const MetricSeries &m : v)
        if (m.key == k) return &m;
    return nullptr;
}
const GateRecord *gate(const std::vector<GateRecord> &v, const char *id)
{
    for (const GateRecord &g : v)
        if (g.gate == QLatin1String(id)) return &g;
    return nullptr;
}

// The sensor's records as body_rotation.cpp leaves them on a worn swing, plus two that must NOT carry
// over: a mask on the tilt (it counts the tilt's frames) and a record on an unrelated reading.
std::vector<GateRecord> sensorRecords()
{
    const QString T = QStringLiteral("shoulderPlaneAngle3d"), B = QStringLiteral("spineForwardBend");
    return { GateRecord{ QStringLiteral("body.chestBendSite"), B, GateEffect::Downgraded, {}, {}, {}, { B, QStringLiteral("secondaryAxisTilt") } },
             GateRecord{ QStringLiteral("body.chestShoulderSite"), T, GateEffect::Downgraded, {}, {}, {}, { T } },
             GateRecord{ QStringLiteral("body.trunkMountYawAssumed"), B, GateEffect::Downgraded, {}, {}, {}, { B, T } },
             GateRecord{ QStringLiteral("body.targetFromAddress"), B, GateEffect::Downgraded, {}, {}, {}, { B } },
             GateRecord{ QStringLiteral("test.tiltMask"), T, GateEffect::Masked, {}, {}, {}, { T } },
             GateRecord{ QStringLiteral("body.pelvisFaceIn"), QStringLiteral("pelvisRotation"), GateEffect::Downgraded, {}, {}, {},
                         { QStringLiteral("hipLineTilt") } } };
}

} // namespace

int main()
{
    std::printf("shoulder_tilt_vs_bend_test\n");
    namespace tb = tilt_vs_bend;
    const QString K = tb::readingKey();

    // ── §1 + §2: the chest sensor for both halves — the value, and the grader's own reading ─────────────
    std::printf("-- 1/2. both halves from the chest sensor: the difference, as the grader reads it --\n");
    {
        MetricSeries tilt = curve("shoulderPlaneAngle3d", "thoraxImu", tiltF, { Phase::Address, Phase::Top }, 7.26);
        // A bridged sample inside the top's window: the grader skips it, and so must the reading.
        tilt.valid.assign(tilt.t_us.size(), 1);
        for (size_t i = 0; i < tilt.t_us.size(); ++i)
            if (tilt.t_us[i] == 1'800'000) { tilt.valid[i] = 0; tilt.value[i] = 99.0; }
        tilt.sigma = 7.2; tilt.sigmaKind = uint8_t(SigmaKind::Propagated);
        MetricSeries bend = curve("spineForwardBend", "trunkImus", bendF, { Phase::Address, Phase::Top }, 3.06);
        std::vector<MetricSeries> s{ tilt, bend };
        std::vector<GateRecord> g = sensorRecords();
        const tb::Outcome o = deriveShoulderTiltVsBend(s, g, ladder());
        const MetricSeries *r = find(s, K);
        check(o.produced && r && r->routeId == QStringLiteral("trunkImus") && r->routeDirect && !o.mixed,
              "produced on the trunkImus rung, Direct, one instrument");
        check(r && r->t_us.empty() && r->value.empty() && r->phaseSamples.size() == 1
                  && r->phaseSamples[0].phase == Phase::Top && r->phaseSamples[0].t_us == kTop && r->unit == QStringLiteral("°"),
              "…one phase sample, at the top's ladder instant, no curve");

        // The grader, on the document the pipeline would write.
        const SwingPhaseGrid grid = buildPhaseGrid(document(s, ladder(), g));
        const auto gt = reduceOverGridReading(grid, QStringLiteral("shoulderPlaneAngle3d"), at(Phase::Top));
        const auto gb = reduceOverGridReading(grid, QStringLiteral("spineForwardBend"), at(Phase::Address));
        const auto gd = reduceOverGridReading(grid, K, at(Phase::Top));
        std::printf("    graded tilt %.6f (σ %.4f), bend %.6f (σ %.4f), difference %.6f (σ %.4f)\n",
                    gt ? gt->value : NAN, gt ? gt->sigma : NAN, gb ? gb->value : NAN, gb ? gb->sigma : NAN,
                    gd ? gd->value : NAN, gd ? gd->sigma : NAN);
        check(gt && gb && gd && near(gd->value, gt->value - gb->value, 1e-9),
              "the graded reading = graded tilt at the top − graded bend at address (the `at` reducer, to 1e-9)");
        check(gt && near(gt->value, o.tilt.value, 1e-12) && gb && near(gb->value, o.bend.value, 1e-12),
              "…each half sampled exactly as the grader samples it");
        check(gt && std::fabs(gt->value - tiltF(kTop)) > 1e-3 && std::fabs(gt->value - 99.0) > 40.0,
              "…a windowed median, not the instant's value, and the bridged 99° left out");
        check(gd && gt && gb && near(gd->sigma, std::hypot(gt->sigma, gb->sigma), 1e-12) && gd->sigmaSrc == SigmaSource::Reading,
              "…its σ = the graded halves' σ in quadrature, a per-reading σ");
        check(r && r->phaseSamples[0].sigma && near(*r->phaseSamples[0].sigma, std::hypot(7.26, 3.06), 1e-12)
                  && r->phaseSamples[0].sigmaKind == uint8_t(SigmaKind::Propagated) && r->sigma
                  && r->sigmaKind == uint8_t(SigmaKind::Propagated),
              "σ same sensor: √(7.26² + 3.06²), Propagated (quadrature; the shared mount-yaw term is not taken jointly)");
        // A known tilt and bend: flat halves, so the median is the value.
        MetricSeries t2 = curve("shoulderPlaneAngle3d", "thoraxImu", [](int64_t) { return 37.0; }, {}, 0.0);
        MetricSeries b2 = curve("spineForwardBend", "trunkImus", [](int64_t) { return 40.0; }, {}, 0.0);
        std::vector<MetricSeries> s2{ t2, b2 };
        std::vector<GateRecord> g2;
        deriveShoulderTiltVsBend(s2, g2, ladder());
        const MetricSeries *r2 = find(s2, K);
        check(r2 && near(r2->phaseSamples[0].value, -3.0), "a 37° tilt on a 40° bend reads −3°: 3° flatter than the posture");
        check(r2 && !r2->sigma && !r2->phaseSamples[0].sigma, "…and with no σ on either half, no σ (not characterised, never 0)");
    }

    // ── §1: the route pairs, and the mixed-sources record ──────────────────────────────────────────────
    std::printf("-- 1/5. the route pairs and body.tiltBendMixedSources --\n");
    {
        struct Row { const char *tiltRoute, *bendRoute, *rung, *tiltFrom, *bendFrom; bool direct, mixed; };
        const Row rows[] = {
            { "thoraxImu",  "trunkImus", "trunkImus",            "", "", true, false },
            { "thoraxImu",  "",          "thoraxImu+dtl",        "thoraxImu", "dtl", true, true },
            { "faceOn+dtl", "",          "faceOn+dtl",           "faceOn+dtl", "dtl", false, true },
            { "faceOn+dtl", "trunkImus", "trunkImus+faceOn+dtl", "faceOn+dtl", "trunkImus", false, true } };
        for (const Row &row : rows) {
            std::vector<MetricSeries> s{ curve("shoulderPlaneAngle3d", row.tiltRoute, [](int64_t) { return 30.0; }, { Phase::Top }, 4.0),
                                         curve("spineForwardBend", row.bendRoute, [](int64_t) { return 36.0; }, { Phase::Address }, 3.0) };
            std::vector<GateRecord> g;
            const tb::Outcome o = deriveShoulderTiltVsBend(s, g, ladder());
            const MetricSeries *r = find(s, K);
            const GateRecord *mx = gate(g, "body.tiltBendMixedSources");
            char lbl[240];
            std::snprintf(lbl, sizeof lbl, "tilt %s + bend %s: rung %s, %s, −6°", row.tiltRoute,
                          *row.bendRoute ? row.bendRoute : "(down the line, no route stated)", row.rung,
                          row.direct ? "Direct" : "not Direct");
            check(o.produced && r && r->routeId == QString::fromLatin1(row.rung) && r->routeDirect == row.direct
                      && near(r->phaseSamples[0].value, -6.0),
                  lbl);
            std::snprintf(lbl, sizeof lbl, "…%s", row.mixed ? "mixed instruments: body.tiltBendMixedSources, Downgraded, on the reading"
                                                            : "one instrument: no mixed-sources record");
            if (!row.mixed) check(!mx && !o.mixed, lbl);
            else
                check(o.mixed && mx && mx->effect == GateEffect::Downgraded && mx->subject == K && mx->covers == QStringList{ K }
                          && mx->evidence.value(QStringLiteral("tiltFrom")).toString() == QString::fromLatin1(row.tiltFrom)
                          && mx->evidence.value(QStringLiteral("bendFrom")).toString() == QString::fromLatin1(row.bendFrom),
                      lbl);
            check(r && r->phaseSamples[0].sigma && near(*r->phaseSamples[0].sigma, 5.0, 1e-12),
                  row.mixed ? "…σ mixed: √(4² + 3²) = 5, the two instruments' errors independent"
                            : "…σ same sensor: √(4² + 3²) = 5");
        }
        // The sentence the Trust panel shows.
        const gates::GateEntry *e = gates::gateEntry(QStringLiteral("body.tiltBendMixedSources"));
        const QString t = e ? gates::evidenceText(*e, { { QStringLiteral("tiltFrom"), QStringLiteral("faceOn+dtl") },
                                                        { QStringLiteral("bendFrom"), QStringLiteral("dtl") } })
                            : QString();
        std::printf("    \"%s\"\n", qPrintable(t));
        check(t == QStringLiteral("Your shoulder tilt was read from the 3-D fit of both cameras and your posture by the down-the-line camera."),
              "the record's sentence names both instruments");
        check(e && e->effects.size() == 1 && e->effects[0] == GateEffect::Downgraded, "…and the record is a downgrade, nothing else");
    }

    // ── §3: not produced when either half is missing ────────────────────────────────────────────────
    std::printf("-- 3. either half missing at its phase: no reading --\n");
    {
        const auto none = [&](std::vector<MetricSeries> s, const std::vector<PhaseEvent> &l, const char *lbl) {
            std::vector<GateRecord> g = sensorRecords();
            const size_t n = s.size(), ng = g.size();
            const tb::Outcome o = deriveShoulderTiltVsBend(s, g, l);
            bool covered = false;
            for (const GateRecord &r : g) covered = covered || r.covers.contains(K);
            check(!o.produced && !find(s, K) && s.size() == n && g.size() == ng && !covered, lbl);
        };
        const MetricSeries tilt = curve("shoulderPlaneAngle3d", "thoraxImu", tiltF, { Phase::Top }, 7.0);
        const MetricSeries bend = curve("spineForwardBend", "trunkImus", bendF, { Phase::Address }, 3.0);
        none({ tilt }, ladder(), "no forward bend at all: nothing written, no record, no cover");
        none({ bend }, ladder(), "no shoulder plane at all: nothing");
        MetricSeries blindAddr = curve("spineForwardBend", "", bendF, {}, 0.0);
        blindAddr.valid.assign(blindAddr.t_us.size(), 1);
        for (size_t i = 0; i < blindAddr.t_us.size(); ++i)
            if (std::llabs(blindAddr.t_us[i] - kAddr) <= 15'000) blindAddr.valid[i] = 0;
        none({ tilt, blindAddr }, ladder(), "a bend with every sample at address bridged and no labelled address: nothing");
        MetricSeries topless = curve("shoulderPlaneAngle3d", "faceOn+dtl", tiltF, {}, 0.0);
        none({ topless, bend }, ladder(false), "no top on the ladder and none labelled: nothing (no nearest-sample guess)");
        none({ tilt, bend }, {}, "no ladder at all: nothing, as the grader reads nothing");
        // …but a half the curve cannot answer and the producer labelled IS read, as the grader reads it.
        MetricSeries labelledOnly = blindAddr;
        labelledOnly.phaseSamples.push_back(PhaseSample{ Phase::Address, kAddr, 41.5, QString() });
        std::vector<MetricSeries> s{ tilt, labelledOnly };
        std::vector<GateRecord> g;
        const tb::Outcome o = deriveShoulderTiltVsBend(s, g, ladder());
        const SwingPhaseGrid grid = buildPhaseGrid(document(s, ladder(), g));
        const auto gd = reduceOverGridReading(grid, K, at(Phase::Top));
        const auto gb = reduceOverGridReading(grid, QStringLiteral("spineForwardBend"), at(Phase::Address));
        check(o.produced && near(o.bend.value, 41.5) && gb && near(gb->value, 41.5) && gd
                  && near(gd->value, o.tilt.value - 41.5, 1e-9),
              "a bend read off its labelled address sample (the curve bridged there): the grader's fallback, and the same difference");
        // A tilt with no top on the ladder but a labelled top: the grader reads that sample; so does this.
        MetricSeries topLabelled = curve("shoulderPlaneAngle3d", "faceOn+dtl", tiltF, { Phase::Top }, 0.0);
        std::vector<MetricSeries> s3{ topLabelled, bend };
        std::vector<GateRecord> g3;
        const tb::Outcome o3 = deriveShoulderTiltVsBend(s3, g3, ladder(false));
        const SwingPhaseGrid grid3 = buildPhaseGrid(document(s3, ladder(false), g3));
        const auto gd3 = reduceOverGridReading(grid3, K, at(Phase::Top));
        const auto gt3 = reduceOverGridReading(grid3, QStringLiteral("shoulderPlaneAngle3d"), at(Phase::Top));
        check(o3.produced && gt3 && near(o3.tilt.value, gt3->value, 1e-12) && gd3 && near(gd3->value, gt3->value - o3.bend.value, 1e-9),
              "no top on the ladder, a labelled top: the curve read at the label's instant, as the grader reads it");
    }

    // ── §4: σ from series and noise floors ──────────────────────────────────────────────────────────────
    std::printf("-- 4. σ: series σ, noise floors --\n");
    {
        // The 3-D tilt with only a series σ (Propagated), the down-the-line bend with a labelled one.
        MetricSeries tilt = curve("shoulderPlaneAngle3d", "faceOn+dtl", [](int64_t) { return 28.0; }, { Phase::Top }, 0.0);
        tilt.sigma = 2.4; tilt.sigmaKind = uint8_t(SigmaKind::Propagated);
        MetricSeries bend = curve("spineForwardBend", "", [](int64_t) { return 34.0; }, { Phase::Address }, 1.8);
        std::vector<MetricSeries> s{ tilt, bend };
        std::vector<GateRecord> g;
        deriveShoulderTiltVsBend(s, g, ladder());
        const SwingPhaseGrid grid = buildPhaseGrid(document(s, ladder(), g));
        const auto gt = reduceOverGridReading(grid, QStringLiteral("shoulderPlaneAngle3d"), at(Phase::Top));
        const auto gb = reduceOverGridReading(grid, QStringLiteral("spineForwardBend"), at(Phase::Address));
        const auto gd = reduceOverGridReading(grid, K, at(Phase::Top));
        check(gt && gb && gd && near(gd->sigma, std::hypot(gt->sigma, gb->sigma), 1e-12) && near(gd->sigma, 3.0, 1e-12)
                  && gt->sigmaSrc == SigmaSource::Series,
              "σ mixed: the 3-D tilt's series σ 2.4 ⊕ the bend's reading σ 1.8 = 3.0, as graded");
        // A noise-floor half (a series σ with no kind): the result is a noise floor, never a full budget.
        MetricSeries noisy = bend;
        noisy.phaseSamples[0].sigma.reset(); noisy.phaseSamples[0].sigmaKind = 0;
        noisy.sigma = 0.5; noisy.sigmaKind = 0;
        std::vector<MetricSeries> s2{ tilt, noisy };
        std::vector<GateRecord> g2;
        deriveShoulderTiltVsBend(s2, g2, ladder());
        const MetricSeries *r2 = find(s2, K);
        const SwingPhaseGrid grid2 = buildPhaseGrid(document(s2, ladder(), g2));
        const auto gd2 = reduceOverGridReading(grid2, K, at(Phase::Top));
        check(r2 && r2->sigma && near(*r2->sigma, std::hypot(2.4, 0.5), 1e-12) && r2->sigmaKind == 0 && !r2->phaseSamples[0].sigma
                  && gd2 && gd2->sigmaSrc == SigmaSource::Noise,
              "a noise-floor half: the difference carries a series σ with no kind, graded as a noise floor");
        // A half assessed with an assumed input keeps that label.
        MetricSeries assumed = bend;
        assumed.phaseSamples[0].sigmaKind = uint8_t(SigmaKind::AssumedInput);
        std::vector<MetricSeries> s3{ tilt, assumed };
        std::vector<GateRecord> g3;
        deriveShoulderTiltVsBend(s3, g3, ladder());
        const MetricSeries *r3 = find(s3, K);
        check(r3 && r3->phaseSamples[0].sigmaKind == uint8_t(SigmaKind::AssumedInput),
              "a half whose σ rests on an assumed input: the difference's σ says so");
        // One half with no σ at all: no σ.
        MetricSeries bare = bend;
        bare.phaseSamples[0].sigma.reset();
        std::vector<MetricSeries> s4{ tilt, bare };
        std::vector<GateRecord> g4;
        deriveShoulderTiltVsBend(s4, g4, ladder());
        const MetricSeries *r4 = find(s4, K);
        check(r4 && !r4->sigma && !r4->phaseSamples[0].sigma, "one half with no σ: the difference has none");
    }

    // ── §5: the halves' records cover the reading ─────────────────────────────────────────────────────
    std::printf("-- 5. the halves' records --\n");
    {
        std::vector<MetricSeries> s{ curve("shoulderPlaneAngle3d", "thoraxImu", tiltF, { Phase::Top }, 7.0),
                                     curve("spineForwardBend", "trunkImus", bendF, { Phase::Address }, 3.0) };
        std::vector<GateRecord> g = sensorRecords();
        g.push_back(GateRecord{ QStringLiteral("test.bendWithheld"), QStringLiteral("spineForwardBend"), GateEffect::Withheld, {}, {}, {}, {} });
        g.push_back(GateRecord{ QStringLiteral("test.tiltRefused"), QStringLiteral("shoulderPlaneAngle3d"), GateEffect::Refused, {}, {}, {}, {} });
        const tb::Outcome o = deriveShoulderTiltVsBend(s, g, ladder());
        const auto covers = [&](const char *id) { const GateRecord *r = gate(g, id); return r && r->covers.contains(K); };
        check(covers("body.chestBendSite") && covers("body.chestShoulderSite") && covers("body.trunkMountYawAssumed")
                  && covers("body.targetFromAddress"),
              "the chest sensor's four Downgraded records (bend site, shoulder site, mount yaw, address front) cover the reading");
        check(covers("test.bendWithheld"), "…a Withheld record on a half too (a half that is not judged takes the difference with it)");
        check(!covers("test.tiltMask") && !covers("test.tiltRefused") && !covers("body.pelvisFaceIn"),
              "…not a mask or a refusal (they count the half's frames), nor a record on another reading");
        check(o.recordsExtended == 5, "…five records extended");
        check(gate(g, "body.chestBendSite")->covers.count(K) == 1, "…each covers it once");
        // The grader sees it: the reading's records are the halves'.
        const std::vector<const GateRecord *> rf = recordsFor(g, K);
        check(rf.size() == 5, "recordsFor(the reading) finds exactly those five");
    }

    // ── §6: withheld halves, idempotence ───────────────────────────────────────────────────────────────
    std::printf("-- 6. a withheld half; a second call --\n");
    {
        MetricSeries tilt = curve("shoulderPlaneAngle3d", "faceOn+dtl", tiltF, { Phase::Top }, 4.0);
        tilt.withheld = QStringLiteral("test: not judged");
        std::vector<MetricSeries> s{ tilt, curve("spineForwardBend", "", bendF, { Phase::Address }, 2.0) };
        std::vector<GateRecord> g = sensorRecords();
        deriveShoulderTiltVsBend(s, g, ladder());
        check(find(s, K) && find(s, K)->withheld == QStringLiteral("test: not judged"), "a withheld half withholds the difference");
        const size_t ns = s.size(), ng = g.size();
        deriveShoulderTiltVsBend(s, g, ladder());
        int readings = 0, mixed = 0;
        for (const MetricSeries &m : s) readings += m.key == K;
        for (const GateRecord &r : g) mixed += r.gate == QLatin1String("body.tiltBendMixedSources");
        check(s.size() == ns && g.size() == ng && readings == 1 && mixed == 1
                  && gate(g, "body.chestShoulderSite")->covers.count(K) == 1,
              "a second call: still one reading, one mixed-sources record, each cover once");
    }

    // ── §7: no reading — the missing half's refusal is the reason shown ───────────────────────────────
    std::printf("-- 7. a missing half: its refusal names the reading --\n");
    {
        const QString B = QStringLiteral("spineForwardBend"), T = QStringLiteral("shoulderPlaneAngle3d");
        const MetricSeries tilt3d = curve("shoulderPlaneAngle3d", "faceOn+dtl", tiltF, { Phase::Top }, 4.0);
        // The down-the-line posture refused at address (dtl_posture.h refuseAll): no bend series at all.
        const GateRecord postureAddress{ QStringLiteral("posture.address"), QStringLiteral("pelvisThrust"), GateEffect::Refused,
                                         { { QStringLiteral("code"), QStringLiteral("noAddressFrame") } }, {}, {},
                                         { QStringLiteral("headRise"), B, QStringLiteral("leadKneeFlexion") } };
        std::vector<MetricSeries> s{ tilt3d };
        std::vector<GateRecord> g{ postureAddress,
                                   GateRecord{ QStringLiteral("test.bendDowngraded"), B, GateEffect::Downgraded, {}, {}, {}, { B } },
                                   GateRecord{ QStringLiteral("test.bendMask"), B, GateEffect::Masked, {}, {}, {}, { B } },
                                   GateRecord{ QStringLiteral("test.tiltRefusedStale"), T, GateEffect::Refused, {}, {}, {}, { T } } };
        const tb::Outcome o = deriveShoulderTiltVsBend(s, g, ladder());
        const auto covers = [&](const char *id) { const GateRecord *r = gate(g, id); return r && r->covers.contains(K); };
        check(!o.produced && !find(s, K) && covers("posture.address") && o.recordsExplaining == 1,
              "refused bend (posture.address): no reading, and the refusal now covers it");
        check(!covers("test.bendDowngraded") && !covers("test.bendMask"),
              "…a Downgraded or Masked record on the missing bend is not a reason for absence, so not extended");
        check(!covers("test.tiltRefusedStale"), "…nor a record on the half that IS there");
        const pinpoint::analysis::GateRecord *pr = gates::primaryRecord(g, K);
        check(pr && pr->gate == QStringLiteral("posture.address"), "…primaryRecord(the reading) is posture.address");
        // Idempotent: a second call does not add the key twice.
        deriveShoulderTiltVsBend(s, g, ladder());
        check(gate(g, "posture.address")->covers.count(K) == 1, "…a second call leaves it covered once");

        // THE REASON SHOWN: the written document, its phase grid, and the measure source the engine reads.
        CharacteristicPack pack;
        Measure m;
        m.id = QStringLiteral("m_shoulderTiltVsBend");
        m.kind = MeasureKind::Provided;
        m.metricKey = K;
        m.reducer = at(Phase::Top);
        pack.measures.push_back(m);
        const LiveMeasureSource src(buildPhaseGrid(document(s, ladder(), g)), pack);
        const auto v = src.value(m.id);
        const LiveMeasureSource::GateCause cause = src.gateCause(m.id);
        std::printf("    not judged: gate %s, subject %s, \"%s\"\n", qPrintable(cause.gate), qPrintable(cause.subject),
                    qPrintable(cause.clause));
        check(!v && cause.gate == QStringLiteral("posture.address") && !cause.clause.isEmpty(),
              "…and the measure source names posture.address as the reason, not \"metric not produced\"");

        // A refused tilt (the 3-D fit had no top), a Bounded and a Withheld record on it: all three name it.
        const MetricSeries bend = curve("spineForwardBend", "", bendF, { Phase::Address }, 0.0);
        std::vector<MetricSeries> s2{ bend };
        std::vector<GateRecord> g2{ GateRecord{ QStringLiteral("test.tiltRefused"), T, GateEffect::Refused, {}, {}, {}, {} },
                                    GateRecord{ QStringLiteral("test.tiltBounded"), QStringLiteral("track.skeleton3d"), GateEffect::Bounded,
                                                {}, {}, {}, { T } },
                                    GateRecord{ QStringLiteral("test.tiltWithheld"), T, GateEffect::Withheld, {}, {}, {}, {} } };
        const tb::Outcome o2 = deriveShoulderTiltVsBend(s2, g2, ladder());
        check(!o2.produced && o2.recordsExplaining == 3
                  && gate(g2, "test.tiltRefused")->covers.contains(K) && gate(g2, "test.tiltBounded")->covers.contains(K)
                  && gate(g2, "test.tiltWithheld")->covers.contains(K),
              "refused tilt: its Refused, Bounded and Withheld records all cover the reading");
        // A tilt present but unread at the top (its window bridged), with its refusal: the same.
        MetricSeries blindTop = curve("shoulderPlaneAngle3d", "faceOn+dtl", tiltF, {}, 0.0);
        blindTop.valid.assign(blindTop.t_us.size(), 1);
        for (size_t i = 0; i < blindTop.t_us.size(); ++i)
            if (std::llabs(blindTop.t_us[i] - kTop) <= 15'000) blindTop.valid[i] = 0;
        std::vector<MetricSeries> s3{ blindTop, bend };
        std::vector<GateRecord> g3{ GateRecord{ QStringLiteral("test.topRefused"), T, GateEffect::Refused, {}, {}, {}, {} } };
        const tb::Outcome o3 = deriveShoulderTiltVsBend(s3, g3, ladder());
        check(!o3.produced && gate(g3, "test.topRefused")->covers.contains(K),
              "a tilt with no reading at the top (bridged): its refusal covers the reading too");

        // A missing half with no record at all: nothing is touched.
        std::vector<MetricSeries> s4{ tilt3d };
        std::vector<GateRecord> g4{ GateRecord{ QStringLiteral("test.tiltDowngraded"), T, GateEffect::Downgraded, {}, {}, {}, { T } },
                                    GateRecord{ QStringLiteral("test.elsewhere"), QStringLiteral("pelvisSway"), GateEffect::Refused, {}, {}, {}, {} } };
        const std::vector<GateRecord> before = g4;
        const tb::Outcome o4 = deriveShoulderTiltVsBend(s4, g4, ladder());
        bool same = g4.size() == before.size();
        for (size_t i = 0; same && i < g4.size(); ++i) same = g4[i].covers == before[i].covers && g4[i].subject == before[i].subject;
        check(!o4.produced && o4.recordsExplaining == 0 && same, "a missing half with no record on it: no record touched");
    }

    // ── §8: the backstop (FB, plane stack, 13 Nov 2026) ─────────────────────────────────────────────────
    // Below −20° (body3d.tiltBendFloorDeg) no turn can make the reading: kept, judged, and Downgraded with
    // body.tiltBendOutOfRange on it and on the 3-D tilt. And the cameras' shoulder record on the tilt is
    // inherited by the reading, as every Downgraded record on a half is.
    std::printf("-- 8. the backstop below −20°, and the cameras' shoulder record --\n");
    {
        const QString T = tb::tiltKey();
        const auto run = [&](double tilt, std::vector<GateRecord> g = {}) {
            std::vector<MetricSeries> s{ curve("shoulderPlaneAngle3d", "faceOn+dtl", [](int64_t) { return 0.0; }, { Phase::Top }, 4.0),
                                         curve("spineForwardBend", "", [](int64_t) { return 36.0; }, { Phase::Address }, 3.0) };
            for (double &v : s[0].value) v = tilt;
            s[0].phaseSamples[0].value = tilt;
            const tb::Outcome o = deriveShoulderTiltVsBend(s, g, ladder());
            return std::make_tuple(o, s, g);
        };
        {
            const auto [o, s, g] = run(36.0 - 22.0);
            const GateRecord *r = gate(g, "body.tiltBendOutOfRange");
            const MetricSeries *m = find(s, K);
            check(o.produced && o.outOfRange && m && near(m->phaseSamples[0].value, -22.0, 1e-9),
                  "−22°: the reading is kept, value −22");
            check(r && r->effect == GateEffect::Downgraded && r->subject == K
                      && r->covers == QStringList({ K, T })
                      && near(r->evidence.value(QStringLiteral("valueDeg")).toDouble(), -22.0, 1e-9)
                      && near(r->evidence.value(QStringLiteral("floorDeg")).toDouble(), -20.0, 1e-9),
                  "…with body.tiltBendOutOfRange: Downgraded, subject the reading, covering it and the 3-D tilt, valueDeg / floorDeg");
            const gates::GateEntry *e = gates::gateEntry(QStringLiteral("body.tiltBendOutOfRange"));
            const QString t = e && r ? gates::evidenceText(*e, r->evidence) : QString();
            std::printf("    \"%s\"\n", qPrintable(t));
            check(e && e->effects == std::vector<GateEffect>{ GateEffect::Downgraded } && t.contains(QStringLiteral("−22°"))
                      && t.contains(QStringLiteral("−20°")) && !t.contains(QLatin1Char('{')),
                  "…catalogued Downgraded only, its sentence filled");
        }
        {
            const auto [o, s, g] = run(36.0 - 18.0);
            check(o.produced && !o.outOfRange && !gate(g, "body.tiltBendOutOfRange"), "−18°: no backstop");
        }
        {
            std::vector<GateRecord> g;
            std::vector<MetricSeries> s{ curve("shoulderPlaneAngle3d", "faceOn+dtl", [](int64_t) { return 14.0; }, { Phase::Top }, 4.0),
                                         curve("spineForwardBend", "", [](int64_t) { return 36.0; }, { Phase::Address }, 3.0) };
            deriveShoulderTiltVsBend(s, g, ladder());
            deriveShoulderTiltVsBend(s, g, ladder());
            int n = 0;
            for (const GateRecord &r : g) n += r.gate == QLatin1String("body.tiltBendOutOfRange");
            check(n == 1, "…a second call leaves one backstop record");
            deriveShoulderTiltVsBend(s, g, ladder(), -25.0);
            check(!gate(g, "body.tiltBendOutOfRange"), "…and a floor of −25° (body3d.tiltBendFloorDeg) takes it away");
        }
        {
            const GateRecord cams{ QStringLiteral("body3d.shoulderViewsDisagree"), T, GateEffect::Downgraded,
                                   { { QStringLiteral("medianDeg"), 12.0 } }, {}, {}, { T, QStringLiteral("shoulderLineYaw") } };
            const auto [o, s, g] = run(30.0, { cams });
            const GateRecord *r = gate(g, "body3d.shoulderViewsDisagree");
            check(o.produced && r && r->covers.contains(K) && o.recordsExtended >= 1,
                  "the cameras' shoulder record on the 3-D tilt covers shoulderTiltVsBend too (inherited)");
        }
    }

    std::printf("\n=== %s (%d failures) ===\n", g_fail ? "FAILURES PRESENT" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
