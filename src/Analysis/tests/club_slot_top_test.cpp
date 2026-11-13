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

// THE CLUB'S CLIMB PLACED IN THE SLOT (club_slot_top.h; plane stack D3, 13 Nov 2026), on synthetic parts:
//   §1 a known address plane, P3 plane and shoulder plane give the slot value and its σ (first order through
//      the ratio), on one phase sample at P3, route dtl;
//   §2 the reading IS the grader's: the document the pipeline would write, through the diagnostics' own phase
//      grid (buildPhaseGrid + reduceOverGridReading), reads each part exactly as the derivation read it — the
//      fitted curve at a ladder instant BETWEEN samples (interpolated) next to a bridged stretch, the shoulder
//      plane off its labelled address sample — and the numerator is the grader's own delta P1 → P3
//      (m_shaftPlaneBackswing's reducer);
//   §3 not produced, with the reason named: a part missing (noAddressPlane, noP3Plane, noShoulderPlane), the
//      slot too narrow or upside down (narrow, with the width), and no record at all on a swing with no
//      down-the-line plane; the minimum width is a knob;
//   §4 σ: a noise-floor part keeps the result a noise floor; a part with no σ gives no σ; an assumed input
//      makes it AssumedInput;
//   §5 trust: the parts' kept-value records (Downgraded, Withheld) cover the reading, Masked ones do not; a
//      withheld part withholds the reading; with a part missing, its own refusal (club.trackRefused) covers
//      the reading and is the reason a reader is given, ahead of club.slotUnread — through the written
//      document and the measure source; the record's sentences;
//   §6 idempotent: a second call leaves one reading and one record.

#include "../../Club/club_slot_top.h"
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

// P1 and P3 fall BETWEEN the curve's 5-ms samples, so the fitted curve is interpolated at both.
constexpr int64_t kAddr = 1'002'500, kP3 = 1'452'500, kTop = 1'700'000, kImp = 2'000'000;

std::vector<PhaseEvent> ladder(bool withP3 = true)
{
    std::vector<PhaseEvent> l{ { Phase::Address, kAddr }, { Phase::Top, kTop }, { Phase::Impact, kImp } };
    if (withP3) l.insert(l.begin() + 1, PhaseEvent{ Phase::MidBackswing, kP3 });
    return l;
}

// swingPlaneCurve every 5 ms from 0.8 s to 2.1 s, series σ only (as ShaftFusionStage publishes it).
MetricSeries planeCurve(double (*f)(int64_t), double sigma = 0.27, uint8_t kind = uint8_t(SigmaKind::Propagated))
{
    MetricSeries m;
    m.key = QStringLiteral("swingPlaneCurve");
    m.label = QStringLiteral("Swing plane curve");
    m.unit = QStringLiteral("°");
    for (int64_t t = 800'000; t <= 2'100'000; t += 5'000) { m.t_us.push_back(t); m.value.push_back(f(t)); }
    if (sigma > 0.0) { m.sigma = sigma; m.sigmaKind = kind; }
    return m;
}
// shoulderPlane: one phase sample at address, its σ on the sample and the series (ShaftFusionStage).
MetricSeries shoulderPlane(double deg, double sigma = 0.10, uint8_t kind = uint8_t(SigmaKind::Propagated))
{
    MetricSeries m;
    m.key = QStringLiteral("shoulderPlane");
    m.label = QStringLiteral("Shoulder plane at address");
    m.unit = QStringLiteral("°");
    PhaseSample ps{ Phase::Address, kAddr, deg, QString() };
    if (sigma > 0.0) { ps.sigma = sigma; ps.sigmaKind = kind; m.sigma = sigma; m.sigmaKind = kind; }
    m.phaseSamples.push_back(ps);
    return m;
}
// Flat at 58° through address, rising linearly to 63° by 1.4 s and flat there through P3: a known climb of 5°.
double flatThenClimb(int64_t t)
{
    if (t <= 1'100'000) return 58.0;
    if (t >= 1'400'000) return 63.0;
    return 58.0 + 5.0 * double(t - 1'100'000) / 300'000.0;
}
// A curved plane: the grader interpolates between samples, so the expected parts are not round numbers.
double wavy(int64_t t) { return 57.0 + 8.0 * std::sin(double(t - 800'000) * 2.1e-6) + 0.3 * std::cos(double(t) * 7e-5); }

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
Reducer delta(Phase from, Phase to)
{
    Reducer r;
    r.kind = ReducerKind::Delta;
    r.anchor = from;
    r.window = { from, to };
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
int count(const std::vector<GateRecord> &v, const char *id)
{
    int n = 0;
    for (const GateRecord &g : v) n += g.gate == QLatin1String(id);
    return n;
}

} // namespace

int main()
{
    std::printf("club_slot_top_test\n");
    namespace cs = club_slot;
    const QString K = cs::readingKey();

    // ── §1: a known slot ─────────────────────────────────────────────────────────────────────────────
    std::printf("-- 1. set at 58°, 63° at P3, shoulder plane 68°: half way up a 10° slot --\n");
    {
        std::vector<MetricSeries> s{ planeCurve(flatThenClimb), shoulderPlane(68.0) };
        std::vector<GateRecord> g;
        const cs::Outcome o = deriveClubSlotTop(s, g, ladder());
        const MetricSeries *r = find(s, K);
        check(o.produced && r && near(o.widthDeg, 10.0) && near(o.value, 0.5, 1e-12),
              "(63 − 58) / (68 − 58) = 0.5: the middle of the slot");
        check(r && r->t_us.empty() && r->value.empty() && r->phaseSamples.size() == 1
                  && r->phaseSamples[0].phase == Phase::MidBackswing && r->phaseSamples[0].t_us == kP3
                  && near(r->phaseSamples[0].value, 0.5, 1e-12) && r->unit == QStringLiteral("ratio")
                  && r->routeId == QStringLiteral("dtl") && r->routeDirect,
              "…one phase sample at P3's ladder instant, no curve, unit ratio, route dtl (Direct)");
        // σ² = (σ3² + s²σsh² + (1 − s)²σ1²) / w² with σ3 = σ1 = 0.27 (the curve's series σ), σsh = 0.10.
        const double want = std::sqrt(0.27 * 0.27 + 0.25 * 0.01 + 0.25 * 0.27 * 0.27) / 10.0;
        check(r && r->phaseSamples[0].sigma && near(*r->phaseSamples[0].sigma, want, 1e-12)
                  && r->phaseSamples[0].sigmaKind == uint8_t(SigmaKind::Propagated) && r->sigma && near(*r->sigma, want, 1e-12),
              "σ first order through the ratio: √(0.27² + 0.5²·0.10² + 0.5²·0.27²) / 10 = 0.0306, Propagated");
        std::printf("    slot %.4f ± %.4f\n", o.value, o.sigma);
        check(g.empty() && !o.cameraAssumed, "…and no record (the down-the-line camera calibrated)");

        // D6 round 2 (Mark, 13 Nov 2026): with the down-the-line camera uncalibrated the reading stands, judged,
        // with the Downgraded club.slotCameraAssumed — subject and covers clubSlotTop. Idempotent.
        {
            std::vector<MetricSeries> su{ planeCurve(flatThenClimb), shoulderPlane(68.0) };
            std::vector<GateRecord> gu;
            const cs::Outcome ou = deriveClubSlotTop(su, gu, ladder(), pinpoint::tuned::clubSlot::kMinWidthDeg, false);
            const cs::Outcome ou2 = deriveClubSlotTop(su, gu, ladder(), pinpoint::tuned::clubSlot::kMinWidthDeg, false);
            check(ou.produced && ou.cameraAssumed && ou2.produced && near(ou2.value, 0.5, 1e-12) && gu.size() == 1
                      && gu[0].gate == QStringLiteral("club.slotCameraAssumed") && gu[0].effect == GateEffect::Downgraded
                      && gu[0].subject == K && gu[0].covers == QStringList{ K }
                      && near(gu[0].evidence.value(QStringLiteral("widthDeg")).toDouble(), 10.0, 1e-9),
                  "uncalibrated: the slot reads 0.5 with one Downgraded club.slotCameraAssumed on it (twice run, one record)");
            // Not produced: no ⚠ (there is nothing to judge), only the refusal.
            std::vector<MetricSeries> sn{ planeCurve(flatThenClimb) };
            std::vector<GateRecord> gn;
            const cs::Outcome on = deriveClubSlotTop(sn, gn, ladder(), pinpoint::tuned::clubSlot::kMinWidthDeg, false);
            check(!on.produced && !on.cameraAssumed
                      && std::none_of(gn.begin(), gn.end(), [](const GateRecord &r) { return r.gate == QStringLiteral("club.slotCameraAssumed"); }),
                  "…and none on a slot that was not read");
        }

        struct Row { double p3, want; const char *what; };
        const Row rows[] = { { 70.0, 1.2, "70° at P3 reads 1.2: above the slot" },
                             { 56.0, -0.2, "56° at P3 reads −0.2: under the slot" },
                             { 58.0, 0.0, "58° at P3 reads 0: on the plane it was set on" },
                             { 68.0, 1.0, "68° at P3 reads 1: on the shoulder plane" } };
        for (const Row &row : rows) {
            static double p3;
            p3 = row.p3;
            MetricSeries c = planeCurve([](int64_t t) { return t < 1'200'000 ? 58.0 : p3; });
            std::vector<MetricSeries> s2{ c, shoulderPlane(68.0) };
            std::vector<GateRecord> g2;
            const cs::Outcome o2 = deriveClubSlotTop(s2, g2, ladder());
            check(o2.produced && near(o2.value, row.want, 1e-12), row.what);
        }
        // σ at the edges: s = 0 takes only the climb's two ends, s = 1 the P3 end and the shoulder.
        {
            MetricSeries c = planeCurve([](int64_t t) { return t < 1'200'000 ? 58.0 : 68.0; });
            std::vector<MetricSeries> s3{ c, shoulderPlane(68.0) };
            std::vector<GateRecord> g3;
            const cs::Outcome o3 = deriveClubSlotTop(s3, g3, ladder());
            check(o3.produced && near(o3.sigma, std::hypot(0.27, 0.10) / 10.0, 1e-12),
                  "…at 1, σ = √(σ_P3² + σ_shoulder²) / w (the address end cancels)");
        }
    }

    // ── §2: the grader's own reading ─────────────────────────────────────────────────────────────────
    std::printf("-- 2. the reading is the grader's --\n");
    {
        MetricSeries c = planeCurve(wavy);
        // A bridged stretch away from P1 and P3 (the curve's validity mask): read by nothing.
        c.valid.assign(c.t_us.size(), 1);
        for (size_t i = 0; i < c.t_us.size(); ++i)
            if (c.t_us[i] >= 1'550'000 && c.t_us[i] <= 1'650'000) { c.valid[i] = 0; c.value[i] = 99.0; }
        const double sh = wavy(kAddr) + 11.7;
        std::vector<MetricSeries> s{ c, shoulderPlane(sh) };
        std::vector<GateRecord> g;
        const cs::Outcome o = deriveClubSlotTop(s, g, ladder());
        const SwingPhaseGrid grid = buildPhaseGrid(document(s, ladder(), g));
        const QString C = QStringLiteral("swingPlaneCurve"), S = QStringLiteral("shoulderPlane");
        const auto g1 = reduceOverGridReading(grid, C, at(Phase::Address));
        const auto g3 = reduceOverGridReading(grid, C, at(Phase::MidBackswing));
        const auto gs = reduceOverGridReading(grid, S, at(Phase::Address));
        const auto gd = reduceOverGridReading(grid, C, delta(Phase::Address, Phase::MidBackswing));
        const auto gk = reduceOverGridReading(grid, K, at(Phase::MidBackswing));
        std::printf("    graded P1 %.6f, P3 %.6f, shoulder %.6f, climb %.6f, slot %.6f (σ %.5f)\n", g1 ? g1->value : NAN,
                    g3 ? g3->value : NAN, gs ? gs->value : NAN, gd ? gd->value : NAN, gk ? gk->value : NAN, gk ? gk->sigma : NAN);
        check(o.produced && g1 && g3 && gs && near(g1->value, o.p1.value, 1e-12) && near(g3->value, o.p3.value, 1e-12)
                  && near(gs->value, o.shoulder.value, 1e-12),
              "each part sampled exactly as the grader samples it (the curve at the instant, the shoulder's label)");
        check(g1 && std::fabs(g1->value - wavy(1'000'000)) > 1e-6 && std::fabs(g1->value - wavy(1'005'000)) > 1e-6,
              "…the curve interpolated between its samples, not a neighbour's value");
        check(gd && near(gd->value, o.p3.value - o.p1.value, 1e-12),
              "…the numerator is the grader's delta P1 → P3 (m_shaftPlaneBackswing's reducer)");
        check(gk && g1 && g3 && gs && near(gk->value, (g3->value - g1->value) / (gs->value - g1->value), 1e-12),
              "the graded reading = (graded P3 − graded P1) / (graded shoulder − graded P1)");
        check(gk && near(gk->sigma, o.sigma, 1e-12) && gk->sigmaSrc == SigmaSource::Reading,
              "…with the derived σ, read as a per-reading σ");
    }

    // ── §3: not produced, and why ────────────────────────────────────────────────────────────────────
    std::printf("-- 3. not produced: the reason named --\n");
    {
        const auto refused = [&](std::vector<MetricSeries> s, const std::vector<PhaseEvent> &l, const char *code,
                                 const char *lbl, double minW = tuned::clubSlot::kMinWidthDeg) {
            std::vector<GateRecord> g;
            const cs::Outcome o = deriveClubSlotTop(s, g, l, minW);
            const GateRecord *r = gate(g, "club.slotUnread");
            check(!o.produced && !find(s, K) && o.code == QLatin1String(code) && r && r->effect == GateEffect::Refused
                      && r->subject == K && r->covers == QStringList{ K }
                      && r->evidence.value(QStringLiteral("code")).toString() == QLatin1String(code) && g.size() == 1,
                  lbl);
            return g;
        };
        refused({ planeCurve(flatThenClimb) }, ladder(), "noShoulderPlane",
                "no shoulder plane (no down-the-line ball): club.slotUnread noShoulderPlane");
        refused({ shoulderPlane(68.0) }, ladder(), "noAddressPlane",
                "a shoulder plane but no swing plane: noAddressPlane");
        refused({ planeCurve(flatThenClimb), shoulderPlane(68.0) }, ladder(false), "noP3Plane",
                "no P3 on the ladder: noP3Plane (no nearest-sample guess)");
        {
            MetricSeries c = planeCurve(flatThenClimb);
            c.valid.assign(c.t_us.size(), 1);
            for (size_t i = 0; i < c.t_us.size(); ++i)
                if (std::llabs(c.t_us[i] - kP3) <= 40'000) c.valid[i] = 0;
            refused({ c, shoulderPlane(68.0) }, ladder(), "noP3Plane", "the curve bridged at P3: noP3Plane");
        }
        {
            MetricSeries c = planeCurve(flatThenClimb);
            c.valid.assign(c.t_us.size(), 1);
            for (size_t i = 0; i < c.t_us.size(); ++i)
                if (std::llabs(c.t_us[i] - kAddr) <= 40'000) c.valid[i] = 0;
            refused({ c, shoulderPlane(68.0) }, ladder(), "noAddressPlane", "the curve bridged at address: noAddressPlane");
        }
        // Narrow: a 3° slot (under 4°), and a shoulder plane BELOW the address plane.
        const std::vector<GateRecord> gn = refused({ planeCurve(flatThenClimb), shoulderPlane(61.0) }, ladder(), "narrow",
                                                   "a 3° slot: narrow");
        check(!gn.empty() && near(gn[0].evidence.value(QStringLiteral("widthDeg")).toDouble(), 3.0)
                  && near(gn[0].evidence.value(QStringLiteral("minWidthDeg")).toDouble(), 4.0),
              "…with its width (3°) and the minimum (4°) as evidence");
        refused({ planeCurve(flatThenClimb), shoulderPlane(55.0) }, ladder(), "narrow",
                "a shoulder plane under the address plane (−3°): narrow, never a negative slot");
        refused({ planeCurve(flatThenClimb), shoulderPlane(58.0) }, ladder(), "narrow", "a zero-width slot: narrow, never a division by 0");
        {
            std::vector<MetricSeries> s{ planeCurve(flatThenClimb), shoulderPlane(61.0) };
            std::vector<GateRecord> g;
            const cs::Outcome o = deriveClubSlotTop(s, g, ladder(), 2.5);
            check(o.produced && near(o.value, 5.0 / 3.0, 1e-12) && g.empty(),
                  "club.slotMinWidthDeg 2.5: the 3° slot is read (5/3), no record — the minimum is a knob");
        }
        {
            std::vector<MetricSeries> s;
            std::vector<GateRecord> g{ GateRecord{ QStringLiteral("test.elsewhere"), QStringLiteral("pelvisSway"),
                                                   GateEffect::Refused, {}, {}, {}, {} } };
            const cs::Outcome o = deriveClubSlotTop(s, g, ladder());
            check(!o.produced && o.code.isEmpty() && g.size() == 1 && g[0].covers.isEmpty() && s.empty(),
                  "no down-the-line plane at all (a face-on swing): no reading, no record, nothing touched");
        }
    }

    // ── §4: σ ────────────────────────────────────────────────────────────────────────────────────────
    std::printf("-- 4. σ: noise floors, none, assumed --\n");
    {
        std::vector<MetricSeries> s{ planeCurve(flatThenClimb, 0.27, 0), shoulderPlane(68.0) };
        std::vector<GateRecord> g;
        const cs::Outcome o = deriveClubSlotTop(s, g, ladder());
        const MetricSeries *r = find(s, K);
        check(o.produced && r && r->sigma && r->sigmaKind == 0 && !r->phaseSamples[0].sigma,
              "a noise-floor curve σ (no kind): the slot's σ is a series σ with no kind, none on the phase sample");
        std::vector<MetricSeries> s2{ planeCurve(flatThenClimb, 0.0), shoulderPlane(68.0) };
        std::vector<GateRecord> g2;
        deriveClubSlotTop(s2, g2, ladder());
        const MetricSeries *r2 = find(s2, K);
        check(r2 && !r2->sigma && !r2->phaseSamples[0].sigma, "a curve with no σ: no σ (not characterised, never 0)");
        std::vector<MetricSeries> s3{ planeCurve(flatThenClimb), shoulderPlane(68.0, 0.10, uint8_t(SigmaKind::AssumedInput)) };
        std::vector<GateRecord> g3;
        deriveClubSlotTop(s3, g3, ladder());
        const MetricSeries *r3 = find(s3, K);
        check(r3 && r3->phaseSamples[0].sigma && r3->phaseSamples[0].sigmaKind == uint8_t(SigmaKind::AssumedInput),
              "an assumed-input shoulder σ makes the slot's σ AssumedInput");
    }

    // ── §5: trust ────────────────────────────────────────────────────────────────────────────────────
    std::printf("-- 5. records: inherited from the parts --\n");
    {
        const QString C = QStringLiteral("swingPlaneCurve"), S = QStringLiteral("shoulderPlane");
        std::vector<MetricSeries> s{ planeCurve(flatThenClimb), shoulderPlane(68.0) };
        std::vector<GateRecord> g{ GateRecord{ QStringLiteral("test.curveDowngraded"), C, GateEffect::Downgraded, {}, {}, {}, { C } },
                                   GateRecord{ QStringLiteral("test.shoulderCovered"), QStringLiteral("track.clubDtl"),
                                               GateEffect::Downgraded, {}, {}, {}, { S } },
                                   GateRecord{ QStringLiteral("test.curveMask"), C, GateEffect::Masked, {}, {}, {}, { C } },
                                   GateRecord{ QStringLiteral("test.elsewhere"), QStringLiteral("pelvisSway"), GateEffect::Downgraded,
                                               {}, {}, {}, { QStringLiteral("pelvisSway") } } };
        const cs::Outcome o = deriveClubSlotTop(s, g, ladder());
        const auto covers = [&](const char *id) { const GateRecord *r = gate(g, id); return r && r->covers.contains(K); };
        check(o.produced && o.recordsExtended == 2 && covers("test.curveDowngraded") && covers("test.shoulderCovered"),
              "the parts' Downgraded records (subject or covers) also cover the slot");
        check(!covers("test.curveMask") && !covers("test.elsewhere"), "…a Masked record and an unrelated one do not");
        // §6 idempotent.
        deriveClubSlotTop(s, g, ladder());
        int n = 0;
        for (const MetricSeries &m : s) n += m.key == K;
        check(n == 1 && gate(g, "test.curveDowngraded")->covers.count(K) == 1, "a second call: one reading, covered once");

        // A withheld part withholds the reading.
        std::vector<MetricSeries> sw{ planeCurve(flatThenClimb), shoulderPlane(68.0) };
        sw[1].withheld = QStringLiteral("test reason");
        std::vector<GateRecord> gw;
        deriveClubSlotTop(sw, gw, ladder());
        check(find(sw, K) && find(sw, K)->withheld == QStringLiteral("test reason"), "a withheld part withholds the slot");
    }
    {
        // The club track refused (club.trackRefused covers both parts on a two-camera swing): no curve and no
        // shoulder plane, so no slot record of its own; the refusal covers the slot and is its reason.
        const QString C = QStringLiteral("swingPlaneCurve"), S = QStringLiteral("shoulderPlane");
        std::vector<MetricSeries> s;
        std::vector<GateRecord> g{ GateRecord{ QStringLiteral("club.trackRefused"), QStringLiteral("track.club"), GateEffect::Refused,
                                               { { QStringLiteral("code"), QStringLiteral("coverage") } }, {}, {},
                                               { C, QStringLiteral("handPlaneCurve"), S } } };
        const cs::Outcome o = deriveClubSlotTop(s, g, ladder());
        check(!o.produced && o.recordsExplaining == 1 && g.size() == 1 && g[0].covers.contains(K),
              "club.trackRefused: it covers the slot, and no club.slotUnread is added");
        const GateRecord *pr = gates::primaryRecord(g, K);
        check(pr && pr->gate == QStringLiteral("club.trackRefused"), "…primaryRecord(the slot) is club.trackRefused");

        // A curve that stands but a shoulder plane refused by a record: that record first, then club.slotUnread.
        std::vector<MetricSeries> s2{ planeCurve(flatThenClimb) };
        std::vector<GateRecord> g2{ GateRecord{ QStringLiteral("test.shoulderRefused"), S, GateEffect::Refused, {}, {}, {}, { S } },
                                    GateRecord{ QStringLiteral("test.curveRefusedStale"), C, GateEffect::Refused, {}, {}, {}, { C } } };
        const cs::Outcome o2 = deriveClubSlotTop(s2, g2, ladder());
        const GateRecord *pr2 = gates::primaryRecord(g2, K);
        check(!o2.produced && gate(g2, "test.shoulderRefused")->covers.contains(K) && !gate(g2, "test.curveRefusedStale")->covers.contains(K)
                  && count(g2, "club.slotUnread") == 1 && pr2 && pr2->gate == QStringLiteral("test.shoulderRefused"),
              "a refused shoulder plane names the slot first; a record on the part that IS there does not");
        deriveClubSlotTop(s2, g2, ladder());
        check(count(g2, "club.slotUnread") == 1 && gate(g2, "test.shoulderRefused")->covers.count(K) == 1,
              "…a second call: one club.slotUnread, covered once");

        // THE REASON SHOWN, through the written document and the measure source the engine reads.
        CharacteristicPack pack;
        Measure m;
        m.id = QStringLiteral("m_clubSlotTop");
        m.kind = MeasureKind::Provided;
        m.metricKey = K;
        m.reducer = at(Phase::MidBackswing);
        pack.measures.push_back(m);
        {
            const LiveMeasureSource src(buildPhaseGrid(document(s, ladder(), g)), pack);
            const LiveMeasureSource::GateCause cause = src.gateCause(m.id);
            std::printf("    not judged: gate %s, \"%s\"\n", qPrintable(cause.gate), qPrintable(cause.clause));
            check(!src.value(m.id) && cause.gate == QStringLiteral("club.trackRefused"),
                  "…the measure source names club.trackRefused as the reason");
        }
        {
            std::vector<MetricSeries> s3{ planeCurve(flatThenClimb) };
            std::vector<GateRecord> g3;
            deriveClubSlotTop(s3, g3, ladder());
            const LiveMeasureSource src(buildPhaseGrid(document(s3, ladder(), g3)), pack);
            const LiveMeasureSource::GateCause cause = src.gateCause(m.id);
            std::printf("    not judged: gate %s, \"%s\"\n", qPrintable(cause.gate), qPrintable(cause.clause));
            check(!src.value(m.id) && cause.gate == QStringLiteral("club.slotUnread"),
                  "no down-the-line ball (no shoulder plane, no record): club.slotUnread is the reason");
        }

        // The sentences the Trust panel shows.
        const gates::GateEntry *e = gates::gateEntry(QStringLiteral("club.slotUnread"));
        const QString tn = e ? gates::evidenceText(*e, { { QStringLiteral("code"), QStringLiteral("narrow") },
                                                         { QStringLiteral("widthDeg"), 3.0 },
                                                         { QStringLiteral("minWidthDeg"), 4.0 } })
                             : QString();
        const QString ts = e ? gates::evidenceText(*e, { { QStringLiteral("code"), QStringLiteral("noShoulderPlane") } }) : QString();
        std::printf("    \"%s\"\n    \"%s\"\n", qPrintable(tn), qPrintable(ts));
        check(e && e->effects.size() == 1 && e->effects[0] == GateEffect::Refused && tn.contains(QStringLiteral("3"))
                  && tn.contains(QStringLiteral("4")) && ts.startsWith(QStringLiteral("No shoulder plane at address")),
              "club.slotUnread is a refusal; its sentences name the width and the missing part");
    }

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
