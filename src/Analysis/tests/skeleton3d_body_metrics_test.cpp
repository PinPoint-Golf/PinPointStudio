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

// The readings from the 3-D body (skeleton3d_body_metrics.h, skeleton3d_body_series.h — what
// Skeleton3dBodyStage publishes) on synthetic joint sets in the fit's world frame (+X face-on
// image-right = the target for a right-hander, +Y the face-on view ray, +Z up; the golfer faces −Y):
//   §1 shoulder plane: a known inclination, any heading, + = trail higher; a left-hander
//   §2 shoulder aim: open −, closed +, for both hands; σ = line σ (the yaw term is the series')
//   §3 knee flexion: a known bend, the lead/trail side by hand
//   §4 side bend: + toward the trail; a pelvis turn does not leak in; a left-hander
//   §5 thigh turn: the hinge plane's turn from address, lead-relative; straight legs unmeasured
//   §6 tier gating: an inferred joint gives no reading
//   §7 σ against Monte Carlo (first order, 4000 draws)
//   §8 the per-shot assembly: route, σ Propagated, graded (series_reduce.h reduceAt) == stamped
//   §9 the camera-assumed record; calibrated; uncertainty off
//   §10 precedence: the lead knee replaces / stays; the side bend only without face-on; the substitute record
//   §11 / §11b the failed route's records come off a substituted reading; a gap keeps its real reason
//   §12 the trail knee from the fit (W5): a known angle, both hands; uncalibrated keep / fill (Mark, 13 Nov);
//       calibrated replace / keep / fill; positions lost
//   §13 the 3-D side bend as the trunk sensors' fallback (W5), in both stage orders
//
//   ctest --test-dir build/tests -R '^skeleton3d_body_metrics_test$' --output-on-failure

#include "../../Body/skeleton3d_body_series.h"
#include "../../Body/trunk_sensor_series.h"   // applyTrunkSensorPrecedence: the 3-D shoulder plane as the fallback
#include "../../Metrics/series_reduce.h"
#include "../../Metrics/gate_catalogue.h"   // primaryRecord, notJudgedClause: what a reader says for a gap

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace pinpoint::analysis;
namespace b3 = pinpoint::analysis::body3d;
namespace sk = pinpoint::skeleton3d;
namespace gates = pinpoint::gates;
using namespace sk::ybot;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
static constexpr double kD = 3.14159265358979323846 / 180.0;

static const int64_t kDt = 8333;   // 120 fps

static sk::FitResult makeFit(int n, float sigma = 0.01f)
{
    sk::FitResult fr;
    fr.valid = true; fr.dtlUsed = true;
    for (int i = 0; i < n; ++i) {
        fr.t_us.push_back(1000000 + int64_t(i) * kDt);
        std::array<sk::V3, sk::Rig::N> j {};
        fr.joints.push_back(j);
        std::array<uint8_t, sk::Rig::N> tr; tr.fill(sk::TierMeasured);
        fr.tier.push_back(tr);
        std::array<float, sk::Rig::N> sg; sg.fill(sigma);
        fr.sigmaM.push_back(sg);
    }
    return fr;
}
static sk::V3 rotZ(const sk::V3 &v, double deg)
{
    const double c = std::cos(deg * kD), s = std::sin(deg * kD);
    return { c * v.x - s * v.y, s * v.x + c * v.y, v.z };
}
// A line of width w centred on C whose trail→lead direction has ground heading `headingDeg` and
// whose TRAIL end sits `trailUpDeg` above the lead end in the line's own vertical plane.
static void setLine(sk::FitResult &fr, size_t t, int lead, int trail, sk::V3 C, double w, double headingDeg,
                    double trailUpDeg)
{
    const double h = headingDeg * kD, e = trailUpDeg * kD;
    const sk::V3 d { std::cos(e) * std::cos(h), std::cos(e) * std::sin(h), -std::sin(e) };
    fr.joints[t][size_t(lead)]  = C + d * (0.5 * w);
    fr.joints[t][size_t(trail)] = C - d * (0.5 * w);
}
// A leg from hip H: the thigh forward-down, the shin back-down, bent `flexDeg` in total, in the plane
// facing the golfer's front (−Y) turned by `yawDeg` about +Z.
static void setLeg(sk::FitResult &fr, size_t t, bool left, sk::V3 H, double flexDeg, double yawDeg)
{
    const b3::Side S = b3::sideJoints(left);
    const sk::V3 f = rotZ({ 0, -1, 0 }, yawDeg);
    const double a = 0.6 * flexDeg * kD, bb = 0.4 * flexDeg * kD;
    const sk::V3 K = H + (f * std::sin(a) + sk::V3{ 0, 0, -std::cos(a) }) * 0.42;
    const sk::V3 A = K + (f * -std::sin(bb) + sk::V3{ 0, 0, -std::cos(bb) }) * 0.42;
    fr.joints[t][size_t(S.hip)] = H;
    fr.joints[t][size_t(S.knee)] = K;
    fr.joints[t][size_t(S.ankle)] = A;
}
// A whole standing body at frame t. Right-hander: lead = left, at +X.
struct Pose {
    double pelvisYaw = 0, hipTrailUp = 0;        // hips: heading of trail→lead (0 = +X), trail-end tilt
    double xFactor = 0, shoulderTrailUp = -10;   // shoulders: heading relative to the hips, tilt
    double leadFlex = 20, trailFlex = 18, leadLegYaw = 0, trailLegYaw = 0;
};
static void setBody(sk::FitResult &fr, size_t t, const Pose &p, bool leftHanded = false)
{
    const double m = leftHanded ? -1.0 : 1.0;     // mirror X for a left-hander
    const auto mir = [&](sk::V3 v) { return sk::V3{ m * v.x, v.y, v.z }; };
    const int lh = leftHanded ? RightUpLeg : LeftUpLeg, th = leftHanded ? LeftUpLeg : RightUpLeg;
    const int ls = leftHanded ? RightArm : LeftArm,     ts = leftHanded ? LeftArm : RightArm;
    // Build the right-hander, then mirror.
    sk::FitResult tmp = makeFit(1);
    setLine(tmp, 0, LeftUpLeg, RightUpLeg, { 0, 1.0, 0.95 }, 0.20, p.pelvisYaw, p.hipTrailUp);
    setLine(tmp, 0, LeftArm, RightArm, { 0, 1.0, 1.45 }, 0.36, p.pelvisYaw + p.xFactor, p.shoulderTrailUp);
    const sk::V3 hipL = tmp.joints[0][LeftUpLeg], hipR = tmp.joints[0][RightUpLeg];
    setLeg(tmp, 0, true,  hipL, p.leadFlex, p.leadLegYaw);
    setLeg(tmp, 0, false, hipR, p.trailFlex, p.trailLegYaw);
    fr.joints[t][size_t(lh)] = mir(tmp.joints[0][LeftUpLeg]);
    fr.joints[t][size_t(th)] = mir(tmp.joints[0][RightUpLeg]);
    fr.joints[t][size_t(ls)] = mir(tmp.joints[0][LeftArm]);
    fr.joints[t][size_t(ts)] = mir(tmp.joints[0][RightArm]);
    for (int j : { LeftLeg, LeftFoot }) fr.joints[t][size_t(leftHanded ? j + 5 : j)] = mir(tmp.joints[0][j]);
    for (int j : { RightLeg, RightFoot }) fr.joints[t][size_t(leftHanded ? j - 5 : j)] = mir(tmp.joints[0][j]);
}

// What the grader reads at a phase (measure_sample.cpp): the curve's windowed median, else the
// labelled reading.
struct Graded { bool fromCurve = false; double value = std::nan(""); };
static Graded grade(const MetricSeries &m, Phase ph, int64_t ladderUs)
{
    SeriesView view;
    view.t = m.t_us.data(); view.v = m.value.data(); view.n = m.t_us.size();
    view.valid = m.valid.size() == m.t_us.size() ? m.valid.data() : nullptr;
    Graded g;
    const Reduced r = reduceAt(view, ladderUs, ReduceConfig{});
    if (r.ok) { g.fromCurve = true; g.value = r.value; return g; }
    for (const PhaseSample &p : m.phaseSamples) if (p.phase == ph) g.value = p.value;
    return g;
}
static const PhaseSample *sampleAt(const MetricSeries &m, Phase ph)
{
    for (const PhaseSample &p : m.phaseSamples) if (p.phase == ph) return &p;
    return nullptr;
}
static const MetricSeries *find(const std::vector<MetricSeries> &v, const char *k)
{
    for (const MetricSeries &m : v) if (m.key == QLatin1String(k)) return &m;
    return nullptr;
}
static int count(const std::vector<MetricSeries> &v, const char *k)
{
    int n = 0;
    for (const MetricSeries &m : v) n += m.key == QLatin1String(k) ? 1 : 0;
    return n;
}

int main()
{
    std::printf("skeleton3d_body_metrics_test\n");
    char buf[256];

    // ── §1 shoulder plane ────────────────────────────────────────────────────────────────────────
    {
        sk::FitResult fr = makeFit(3);
        setLine(fr, 0, LeftArm, RightArm, { 0, 1, 1.45 }, 0.36, 0.0, 45.0);
        setLine(fr, 1, LeftArm, RightArm, { 0, 1, 1.45 }, 0.36, 75.0, 45.0);    // turned 75°: same plane
        setLine(fr, 2, LeftArm, RightArm, { 0, 1, 1.45 }, 0.36, 0.0, -8.0);     // lead higher
        check(near(b3::shoulderPlaneDeg(fr, 0, true).deg, 45.0, 1e-9), "§1 trail shoulder 45° up ⇒ +45");
        check(near(b3::shoulderPlaneDeg(fr, 1, true).deg, 45.0, 1e-9), "§1 …and the same 45 turned 75° (its own vertical plane)");
        check(near(b3::shoulderPlaneDeg(fr, 2, true).deg, -8.0, 1e-9), "§1 lead shoulder higher ⇒ negative");
        check(near(b3::shoulderPlaneDeg(fr, 0, true).sigma, std::hypot(0.01, 0.01) / 0.36 / kD, 1e-6),
              "§1 σ = √(σ_l² + σ_t²) / |line|");
        // A left-hander: the mirror image — lead = right shoulder, at −X.
        sk::FitResult lf = makeFit(1);
        Pose p; p.shoulderTrailUp = 40.0; p.xFactor = 80.0;
        setBody(lf, 0, p, /*leftHanded*/ true);
        check(near(b3::shoulderPlaneDeg(lf, 0, false).deg, 40.0, 1e-9), "§1 left-hander, trail shoulder 40° up ⇒ +40");
        check(near(b3::shoulderPlaneDeg(lf, 0, true).deg, -40.0, 1e-9),
              "§1 …read with the right-hander's sides it would be −40 (the side choice matters)");
    }

    // ── §2 shoulder aim ───────────────────────────────────────────────────────────────────────────
    {
        sk::FitResult fr = makeFit(3);
        setLine(fr, 0, LeftArm, RightArm, { 0, 1, 1.45 }, 0.36, 5.0, -10.0);     // lead end toward +Y
        setLine(fr, 1, LeftArm, RightArm, { 0, 1, 1.45 }, 0.36, -7.0, -10.0);    // lead end toward −Y
        setLine(fr, 2, LeftArm, RightArm, { 0, 1, 1.45 }, 0.36, 0.0, -10.0);
        check(near(b3::shoulderAimDeg(fr, 0, true).deg, -5.0, 1e-9),
              "§2 right-hander, the lead end toward the golfer's side (left of target) ⇒ OPEN −5");
        check(near(b3::shoulderAimDeg(fr, 1, true).deg, 7.0, 1e-9), "§2 …aimed right of target ⇒ CLOSED +7");
        check(near(b3::shoulderAimDeg(fr, 2, true).deg, 0.0, 1e-9), "§2 …square ⇒ 0, whatever the tilt");
        check(near(b3::shoulderAimDeg(fr, 0, true).sigma, std::hypot(0.01, 0.01) / (0.36 * std::cos(10 * kD)) / kD, 1e-6),
              "§2 σ = the line's σ over its ground length (the yaw term is the series')");
        // Left-hander: target −X. Open = aimed right of target from behind = the lead end toward +Y.
        sk::FitResult lf = makeFit(2);
        Pose open; open.xFactor = 5.0;     // right-hander's open, mirrored
        Pose closed; closed.xFactor = -7.0;
        setBody(lf, 0, open, true);
        setBody(lf, 1, closed, true);
        check(lf.joints[0][RightArm].y > lf.joints[0][LeftArm].y, "§2 left-hander set-up: the lead (right) shoulder end toward +Y");
        check(near(b3::shoulderAimDeg(lf, 0, false).deg, -5.0, 1e-9), "§2 left-hander OPEN ⇒ −5 (mirrored, same sign)");
        check(near(b3::shoulderAimDeg(lf, 1, false).deg, 7.0, 1e-9), "§2 left-hander CLOSED ⇒ +7");
    }

    // ── §3 knee flexion ───────────────────────────────────────────────────────────────────────────
    {
        sk::FitResult fr = makeFit(1);
        Pose p; p.leadFlex = 25.0; p.trailFlex = 40.0;
        setBody(fr, 0, p);
        check(near(b3::kneeFlexDeg(fr, 0, true).deg, 25.0, 1e-9), "§3 right-hander, lead (left) knee 25°");
        check(near(b3::kneeFlexDeg(fr, 0, false).deg, 40.0, 1e-9), "§3 …trail (right) knee 40°");
        sk::FitResult lf = makeFit(1);
        setBody(lf, 0, p, true);
        check(near(b3::kneeFlexDeg(lf, 0, false).deg, 25.0, 1e-9), "§3 left-hander, lead (RIGHT) knee 25°");
        check(near(b3::kneeFlexDeg(lf, 0, true).deg, 40.0, 1e-9), "§3 …trail (LEFT) knee 40°");
        Pose st; st.leadFlex = 0.0;
        sk::FitResult s1 = makeFit(1);
        setBody(s1, 0, st);
        check(near(b3::kneeFlexDeg(s1, 0, true).deg, 0.0, 1e-6), "§3 a straight leg reads 0");
    }

    // ── §4 side bend ──────────────────────────────────────────────────────────────────────────────
    {
        sk::FitResult fr = makeFit(4);
        Pose p; p.hipTrailUp = 0.0; p.shoulderTrailUp = -20.0;           // the trail shoulder dropped 20°
        setBody(fr, 0, p);
        check(near(b3::sideBendDeg(fr, 0, true).deg, 20.0, 1e-9), "§4 the trail shoulder dropped 20° under level hips ⇒ +20 (toward trail)");
        Pose q = p; q.pelvisYaw = 40.0;                                    // the whole trunk turned 40°
        setBody(fr, 1, q);
        check(near(b3::sideBendDeg(fr, 1, true).deg, 20.0, 1e-9), "§4 …turned 40° with the pelvis: still +20 — the turn does not leak in");
        Pose r = p; r.hipTrailUp = 6.0; r.shoulderTrailUp = 6.0;           // tilted together
        setBody(fr, 2, r);
        check(near(b3::sideBendDeg(fr, 2, true).deg, 0.0, 1e-9), "§4 shoulders and hips tilted together ⇒ 0 (a lean, not a bend)");
        Pose l = p; l.shoulderTrailUp = 8.0;
        setBody(fr, 3, l);
        check(near(b3::sideBendDeg(fr, 3, true).deg, -8.0, 1e-9), "§4 the trail shoulder up ⇒ negative (toward lead)");
        // With an X-factor the shoulder line is read in the PELVIS's frontal plane, not its own.
        sk::FitResult xf = makeFit(1);
        Pose x = p; x.pelvisYaw = 30.0; x.xFactor = 30.0;
        setBody(xf, 0, x);
        const double want = std::atan2(0.36 * std::sin(20 * kD), 0.36 * std::cos(20 * kD) * std::cos(30 * kD)) / kD;
        std::snprintf(buf, sizeof buf, "§4 a 30° X-factor: the shoulder tilt in the pelvis's plane, %.3f°", want);
        check(near(b3::sideBendDeg(xf, 0, true).deg, want, 1e-9), buf);
        sk::FitResult lf = makeFit(1);
        setBody(lf, 0, q, true);
        check(near(b3::sideBendDeg(lf, 0, false).deg, 20.0, 1e-9), "§4 left-hander, the same posture mirrored ⇒ +20");
    }

    // ── §5 thigh turn ─────────────────────────────────────────────────────────────────────────────
    {
        const int n = 60;
        sk::FitResult fr = makeFit(n), lf = makeFit(n);
        for (int i = 0; i < n; ++i) {
            Pose p;
            p.leadFlex = 22.0; p.trailFlex = (i == 50) ? 5.0 : 20.0;     // frame 50: the trail leg near straight
            p.leadLegYaw  = i < 30 ? 0.0 : 20.0;                           // the lead thigh turned +20° about +Z
            p.trailLegYaw = i < 30 ? 0.0 : -15.0;
            setBody(fr, size_t(i), p);
            setBody(lf, size_t(i), p, true);   // the mirror image: the same motion for a left-hander
        }
        const int64_t addr = fr.t_us[10];
        const b3::ThighTurn lead = b3::thighTurn(fr, true, true, addr), trail = b3::thighTurn(fr, false, true, addr);
        check(lead.ok && near(lead.turnDeg[5], 0.0, 1e-9) && near(lead.turnDeg[40], 20.0, 1e-9),
              "§5 right-hander lead thigh: 0 at address, +20 turned toward the target (+Z, the lead-relative sign)");
        check(trail.ok && near(trail.turnDeg[40], -15.0, 1e-9), "§5 …the trail thigh turned back −15");
        check(!std::isfinite(trail.turnDeg[50]) && std::isfinite(lead.turnDeg[50]),
              "§5 a leg bent 5°, under kThighTurnMinKneeFlexDeg (12°), is unmeasured; the other is read");
        const b3::ThighTurn llead = b3::thighTurn(lf, false, false, lf.t_us[10]);
        check(llead.ok && near(llead.turnDeg[40], 20.0, 1e-9), "§5 left-hander, the mirrored motion ⇒ the same +20");
        check(std::isfinite(lead.sigmaDeg[40]) && lead.sigmaDeg[40] > lead.sigmaDeg[5],
              "§5 σ grows with the turn (the camera-scale term frac·Δ)");
        // No address reference ⇒ no turn at all.
        const b3::ThighTurn none = b3::thighTurn(fr, true, true, fr.t_us.back() + 1000000);
        check(!none.ok, "§5 no frame near address ⇒ no reference, no turn");
    }

    // ── §6 tier gating ────────────────────────────────────────────────────────────────────────────
    {
        sk::FitResult fr = makeFit(2);
        Pose p; setBody(fr, 0, p); setBody(fr, 1, p);
        fr.tier[1][LeftLeg] = sk::TierInferred;
        fr.tier[1][RightArm] = sk::TierInferred;
        check(b3::kneeFlexDeg(fr, 0, true).ok() && !b3::kneeFlexDeg(fr, 1, true).ok(),
              "§6 an INFERRED knee gives no lead knee bend");
        check(b3::kneeFlexDeg(fr, 1, false).ok(), "§6 …while the other knee still reads");
        check(!b3::shoulderPlaneDeg(fr, 1, true).ok() && !b3::shoulderAimDeg(fr, 1, true).ok()
                  && !b3::sideBendDeg(fr, 1, true).ok(),
              "§6 an inferred shoulder gives no plane, aim or side bend");
        fr.tier[0][LeftArm] = sk::TierConstrained;
        check(b3::shoulderPlaneDeg(fr, 0, true).ok(), "§6 a CONSTRAINED joint is read");
    }

    // ── §7 σ against Monte Carlo ──────────────────────────────────────────────────────────────────
    {
        sk::FitResult base = makeFit(1, 0.012f);
        Pose p; p.pelvisYaw = 25.0; p.xFactor = 20.0; p.shoulderTrailUp = -25.0; p.hipTrailUp = 4.0;
        p.leadFlex = 30.0; p.leadLegYaw = 10.0;
        setBody(base, 0, p);
        std::mt19937 rng(20261112);
        std::normal_distribution<double> N(0.0, 0.012);
        const int draws = 4000;
        double s1 = 0, s2 = 0, k1 = 0, k2 = 0, b1 = 0, b2 = 0, h1 = 0, h2 = 0, a1 = 0, a2 = 0;
        const b3::Reading pl0 = b3::shoulderPlaneDeg(base, 0, true), kn0 = b3::kneeFlexDeg(base, 0, true),
                          sb0 = b3::sideBendDeg(base, 0, true), am0 = b3::shoulderAimDeg(base, 0, true);
        const b3::Heading th0 = b3::thighHeading(base, 0, true);
        for (int d = 0; d < draws; ++d) {
            sk::FitResult f = base;
            for (auto &j : f.joints[0]) { j.x += N(rng); j.y += N(rng); j.z += N(rng); }
            const double pl = b3::shoulderPlaneDeg(f, 0, true).deg - pl0.deg;
            const double kn = b3::kneeFlexDeg(f, 0, true).deg - kn0.deg;
            const double sb = b3::sideBendDeg(f, 0, true).deg - sb0.deg;
            const double am = b3::shoulderAimDeg(f, 0, true).deg - am0.deg;
            const double th = b3::wrapRadPi(b3::thighHeading(f, 0, true).rad - th0.rad) / kD;
            s1 += pl; s2 += pl * pl; k1 += kn; k2 += kn * kn; b1 += sb; b2 += sb * sb; a1 += am; a2 += am * am;
            h1 += th; h2 += th * th;
        }
        const auto sd = [&](double x1, double x2) { return std::sqrt(x2 / draws - (x1 / draws) * (x1 / draws)); };
        const auto agree = [&](const char *what, double mc, double prop, double tol) {
            std::snprintf(buf, sizeof buf, "§7 %s: propagated σ %.3f° vs Monte Carlo %.3f° (within %.0f%%)", what, prop, mc, tol * 100);
            check(std::fabs(prop - mc) <= tol * mc, buf);
        };
        agree("shoulder plane", sd(s1, s2), pl0.sigma, 0.10);
        agree("shoulder aim", sd(a1, a2), am0.sigma, 0.10);
        agree("knee flexion", sd(k1, k2), kn0.sigma, 0.10);
        agree("side bend", sd(b1, b2), sb0.sigma, 0.15);
        agree("thigh heading", sd(h1, h2), th0.sigma / kD, 0.15);
    }

    // ── §8 the per-shot assembly ──────────────────────────────────────────────────────────────────
    // A swing: address frames 0–20, the trunk turning to a 45° shoulder plane at the top (frame 60),
    // the lead knee bending through impact (frame 90) to P8 (frame 100). The ladder instants sit
    // BETWEEN frames, and the lead knee is unread on two frames inside impact's window (inferred), so
    // the windowed median is not any one frame.
    const int n = 120;
    sk::FitResult fr = makeFit(n);
    for (int i = 0; i < n; ++i) {
        Pose p;
        const double f = std::clamp((i - 20) / 40.0, 0.0, 1.0);           // address → top
        const double g = std::clamp((i - 60) / 30.0, 0.0, 1.0);           // top → impact
        p.pelvisYaw = -45.0 * f + 45.0 * g;
        p.xFactor = -40.0 * f + 40.0 * g;
        p.shoulderTrailUp = -10.0 + 55.0 * f - 75.0 * g;                  // 45° at the top, −30 at impact
        p.leadFlex = 20.0 + 0.15 * i;
        p.leadLegYaw = 10.0 * g;
        setBody(fr, size_t(i), p);
        // A slight open aim at address.
        if (i <= 20) setLine(fr, size_t(i), LeftArm, RightArm, fr.joints[size_t(i)][LeftArm] * 0.5 + fr.joints[size_t(i)][RightArm] * 0.5,
                             0.36, 3.0 + 0.1 * (i % 3), -10.0);
    }
    fr.tier[89][LeftLeg] = sk::TierInferred;
    fr.tier[91][LeftLeg] = sk::TierInferred;
    std::vector<PhaseEvent> ladder;
    const auto ev = [&](Phase ph, int64_t t) { PhaseEvent e; e.phase = ph; e.t_us = t; ladder.push_back(e); };
    ev(Phase::Address, fr.t_us[10] + 3000);
    ev(Phase::Top, fr.t_us[60] - 2000);
    ev(Phase::Impact, fr.t_us[90] + 1000);
    ev(Phase::ShaftParallelThrough, fr.t_us[100]);
    Skeleton3dBodyInput in;
    in.fit = &fr; in.phases = ladder;
    Skeleton3dBodyOutput out = buildSkeleton3dBody(in);
    {
        check(out.shoulderPlane && out.shoulderAim && out.leadKnee && out.trailKnee && out.sideBend && out.thighLead
                  && out.thighTrail,
              "§8 all seven series built");
        for (const std::optional<MetricSeries> *s : { &out.shoulderPlane, &out.shoulderAim, &out.leadKnee, &out.trailKnee,
                                                      &out.sideBend, &out.thighLead, &out.thighTrail }) {
            if (!*s) continue;
            const MetricSeries &m = **s;
            std::snprintf(buf, sizeof buf, "§8 %s: route faceOn+dtl, not Direct, σ Propagated", qPrintable(m.key));
            check(m.routeId == QStringLiteral("faceOn+dtl") && !m.routeDirect && m.sigma && *m.sigma > 0
                      && m.sigmaKind == uint8_t(SigmaKind::Propagated), buf);
            for (const PhaseSample &ps : m.phaseSamples) {
                const int64_t lad = ps.t_us;
                const Graded g = grade(m, ps.phase, lad);
                std::snprintf(buf, sizeof buf, "§8 %s at phase %d: graded %.5f (%s) == stamped %.5f, σ %.3f Propagated",
                         qPrintable(m.key), int(ps.phase), g.value, g.fromCurve ? "curve" : "fallback", ps.value,
                         ps.sigma ? *ps.sigma : -1.0);
                check(std::isfinite(g.value) && near(g.value, ps.value, 1e-9) && ps.sigma && *ps.sigma > 0
                          && ps.sigmaKind == uint8_t(SigmaKind::Propagated), buf);
            }
        }
        const PhaseSample *top = sampleAt(*out.shoulderPlane, Phase::Top);
        // Frames 58–61 lie within ±15 ms of the top (2 ms before frame 60): 42.25, 43.625, 45, 42.5.
        check(top && near(top->value, 0.5 * (42.5 + 43.625), 1e-9) && top->t_us == ladder[1].t_us,
              "§8 the shoulder plane at the top = the median of the four frames in its window (43.06°), "
              "stamped at the ladder's instant");
        check(out.shoulderPlane->phaseSamples.size() == 1, "§8 …and read only at the top");
        const PhaseSample *aim = sampleAt(*out.shoulderAim, Phase::Address);
        check(out.shoulderAim->t_us.empty() && aim && aim->value < 0 && near(aim->value, -3.1, 0.11),
              "§8 shoulderLineYaw: one reading at address, no curve, open (≈ −3°)");
        const double lineSigma = std::hypot(0.01, 0.01) / (0.36 * std::cos(10 * kD)) / kD;
        check(aim && aim->sigma && near(*aim->sigma, std::hypot(lineSigma, pinpoint::tuned::dtlCamera::kDtlYawSigmaUncalDeg), 1e-6),
              "§8 …σ = the line σ ⊕ σ_yaw (uncalibrated)");
        const PhaseSample *imp = sampleAt(*out.leadKnee, Phase::Impact);
        check(imp && !out.leadKnee->valid.empty() && out.leadKnee->valid[89] == 0 && out.leadKnee->valid[91] == 0,
              "§8 the lead knee's unread frames inside impact's window are BRIDGED (valid 0)");
        check(imp && near(imp->value, 20.0 + 0.15 * 90, 0.2), "§8 …and the reading there is the median of the read ones");
        check(out.leadKneeAtImpact, "§8 the 3-D lead knee reads through Impact → P8");
        check(sampleAt(*out.leadKnee, Phase::ShaftParallelThrough) && sampleAt(*out.leadKnee, Phase::Address)
                  && sampleAt(*out.leadKnee, Phase::Top), "§8 …read at address, the top, impact and P8");
        const PhaseSample *ti = sampleAt(*out.thighLead, Phase::Impact);
        check(ti && near(ti->value, 10.0, 0.5), "§8 the lead thigh turned +10° by impact (research series)");
        check(out.thighLead->key == QStringLiteral("thighTurn3dLead") && out.thighTrail->key == QStringLiteral("thighTurn3dTrail"),
              "§8 the thigh turns are keyed thighTurn3dLead / thighTurn3dTrail");
        // A position whose window holds no read frame has NO reading (nothing for the grader to fall back to).
        sk::FitResult holes = fr;
        for (int i = 58; i <= 62; ++i) holes.tier[size_t(i)][LeftArm] = sk::TierInferred;
        Skeleton3dBodyInput hin = in; hin.fit = &holes;
        const Skeleton3dBodyOutput ho = buildSkeleton3dBody(hin);
        check(ho.shoulderPlane && !sampleAt(*ho.shoulderPlane, Phase::Top),
              "§8 no read frame within ±15 ms of the top ⇒ no top reading at all");
    }

    // ── §9 the record ─────────────────────────────────────────────────────────────────────────────
    {
        check(out.gates.size() == 1 && out.gates[0].gate == QStringLiteral("body3d.cameraAssumed")
                  && out.gates[0].effect == GateEffect::Downgraded && out.gates[0].subject == QStringLiteral("shoulderLineYaw")
                  && out.gates[0].covers == QStringList{ QStringLiteral("shoulderLineYaw") }
                  && near(out.gates[0].evidence.value(QStringLiteral("sigmaYawDeg")).toDouble(), pinpoint::tuned::dtlCamera::kDtlYawSigmaUncalDeg, 1e-12),
              "§9 ONE record, body3d.cameraAssumed, Downgraded, covering shoulderLineYaw alone (σ_yaw uncalibrated)");
        Skeleton3dBodyInput cal = in; cal.calibrated = true;
        const Skeleton3dBodyOutput co = buildSkeleton3dBody(cal);
        const double lineSigma = std::hypot(0.01, 0.01) / (0.36 * std::cos(10 * kD)) / kD;
        check(co.gates.empty() && co.shoulderAim && near(*co.shoulderAim->phaseSamples[0].sigma, std::hypot(lineSigma, 1.0), 1e-6),
              "§9 calibrated: no record, σ_yaw 1°");
        Skeleton3dBodyInput noAddr = in;
        noAddr.phases.erase(noAddr.phases.begin());
        const Skeleton3dBodyOutput na = buildSkeleton3dBody(noAddr);
        check(!na.shoulderAim && na.gates.empty() && !na.thighLead,
              "§9 no Address on the ladder ⇒ no aim, so no record (and no thigh reference)");
        Skeleton3dBodyInput off = in; off.withSigma = false;
        const Skeleton3dBodyOutput oo = buildSkeleton3dBody(off);
        check(oo.shoulderPlane && !oo.shoulderPlane->sigma && !oo.shoulderPlane->phaseSamples[0].sigma
                  && oo.shoulderAim && !oo.shoulderAim->phaseSamples[0].sigma,
              "§9 uncertainty off ⇒ no σ written");
    }

    // ── §10 precedence ────────────────────────────────────────────────────────────────────────────
    {
        MetricSeries dtlKnee; dtlKnee.key = QStringLiteral("leadKneeFlexion");
        dtlKnee.t_us = { fr.t_us[0] }; dtlKnee.value = { 1.0 };
        MetricSeries foBend; foBend.key = QStringLiteral("spineSideBend");
        foBend.t_us = { fr.t_us[0] }; foBend.value = { 2.0 };
        MetricSeries dtlTrail; dtlTrail.key = QStringLiteral("trailKneeFlexion");
        dtlTrail.t_us = { fr.t_us[0] }; dtlTrail.value = { 3.0 };
        {
            std::vector<MetricSeries> s{ dtlKnee, foBend, dtlTrail };
            Skeleton3dBodyOutput o = buildSkeleton3dBody(in);
            const Skeleton3dBodyMerge mg = mergeSkeleton3dBody(s, o);
            const MetricSeries *k = find(s, "leadKneeFlexion");
            check(mg.leadKneeReplaced && count(s, "leadKneeFlexion") == 1 && k && k->routeId == QStringLiteral("faceOn+dtl")
                      && k->withheld.isEmpty(),
                  "§10 the 3-D lead knee reads through impact ⇒ it REPLACES the down-the-line one");
            const MetricSeries *sb = find(s, "spineSideBend");
            check(count(s, "spineSideBend") == 1 && sb && sb->routeId.isEmpty() && sb->value.front() == 2.0,
                  "§10 a face-on side bend exists ⇒ it stays the answer, the 3-D one is not added");
            check(find(s, "shoulderPlaneAngle3d") && find(s, "shoulderLineYaw") && find(s, "thighTurn3dLead")
                      && find(s, "thighTurn3dTrail"),
                  "§10 the shoulder readings and the thigh turns are published");
            check(!mg.trailKneeReplaced && mg.trailKneeKeptDtl && mg.trailKneeKeptUncalibrated
                      && count(s, "trailKneeFlexion") == 1 && find(s, "trailKneeFlexion")->routeId.isEmpty()
                      && find(s, "trailKneeFlexion")->value.front() == 3.0,
                  "§10 the cameras uncalibrated ⇒ the down-the-line trail knee stays, though the 3-D one reads "
                  "at address and the top (Mark's rule, 13 Nov)");
            check(mg.gates.empty(),
                  "§10 …and no body3d.fromFit: a 3-D knee REPLACING a down-the-line one is the better rung, "
                  "not a fallback, and the face-on side bend stood");
        }
        {
            sk::FitResult blind = fr;
            for (int i = 88; i <= 101; ++i) blind.tier[size_t(i)][LeftLeg] = sk::TierInferred;
            Skeleton3dBodyInput bi = in; bi.fit = &blind;
            Skeleton3dBodyOutput o = buildSkeleton3dBody(bi);
            check(o.leadKnee && !o.leadKneeAtImpact, "§10 a 3-D lead knee blind through Impact → P8 does not read there");
            std::vector<MetricSeries> s{ dtlKnee, dtlTrail };
            const Skeleton3dBodyMerge mg = mergeSkeleton3dBody(s, o);
            const MetricSeries *k = find(s, "leadKneeFlexion");
            check(mg.leadKneeKeptDtl && count(s, "leadKneeFlexion") == 1 && k && k->withheld.isEmpty() && k->routeId.isEmpty(),
                  "§10 …so the down-the-line one stays, as it is (not withheld), and the 3-D one is not published beside it");
            check(find(s, "spineSideBend") && find(s, "spineSideBend")->routeId == QStringLiteral("faceOn+dtl"),
                  "§10 no face-on side bend ⇒ the 3-D one is published");
            check(mg.gates.size() == 1 && mg.gates[0].gate == QStringLiteral("body3d.fromFit")
                      && mg.gates[0].subject == QStringLiteral("spineSideBend") && mg.gates[0].effect == GateEffect::Downgraded
                      && mg.gates[0].covers == QStringList{ QStringLiteral("spineSideBend") }
                      && mg.gates[0].evidence.value(QStringLiteral("replaces")).toString() == QStringLiteral("faceOn")
                      && mg.gates[0].evidence.value(QStringLiteral("route")).toString() == QStringLiteral("faceOn+dtl")
                      && isRouteSubstitution(mg.gates[0]),
                  "§10 …with ONE body3d.fromFit, Downgraded, on the side bend alone (replaces faceOn) — the lead knee "
                  "stayed down the line, so it has none");
            std::vector<MetricSeries> none;
            Skeleton3dBodyOutput o2 = buildSkeleton3dBody(bi);
            const Skeleton3dBodyMerge m2 = mergeSkeleton3dBody(none, o2);
            check(count(none, "leadKneeFlexion") == 1 && find(none, "leadKneeFlexion")->routeId == QStringLiteral("faceOn+dtl"),
                  "§10 no down-the-line series at all ⇒ the 3-D lead knee is published whatever it covers");
            bool kneeRec = false;
            for (const GateRecord &g : m2.gates)
                if (g.subject == QStringLiteral("leadKneeFlexion") && g.gate == QStringLiteral("body3d.fromFit")
                    && g.evidence.value(QStringLiteral("replaces")).toString() == QStringLiteral("dtl")) kneeRec = true;
            bool trailRec = false;
            for (const GateRecord &g : m2.gates)
                if (g.subject == QStringLiteral("trailKneeFlexion") && g.gate == QStringLiteral("body3d.fromFit")
                    && g.evidence.value(QStringLiteral("replaces")).toString() == QStringLiteral("dtl")) trailRec = true;
            check(kneeRec && trailRec && m2.gates.size() == 3,
                  "§10 …and it carries body3d.fromFit (replaces dtl), beside the trail knee's and the side bend's");
        }
        // THE CHEST SENSOR'S SHOULDER PLANE (W4, 13 Nov 2026). BodyRotationStage runs first and may already
        // have written it (routeId thoraxImu): the 3-D one is still published beside it, so that it is there
        // when applyTrunkSensorPrecedence withdraws the sensor's. Whichever came first, the sensor's is the
        // answer when it is trusted, and the 3-D one when the chest is worn face-in.
        {
            MetricSeries chest; chest.key = QStringLiteral("shoulderPlaneAngle3d"); chest.routeId = QStringLiteral("thoraxImu");
            chest.t_us = { fr.t_us[0], fr.t_us.back() }; chest.value = { 30.0, 30.0 };
            for (const PhaseEvent &e : in.phases) chest.phaseSamples.push_back({ e.phase, e.t_us, 30.0, QString() });
            const auto routes = [](const std::vector<MetricSeries> &sv) {
                QStringList r;
                for (const MetricSeries &m : sv) if (m.key == QLatin1String("shoulderPlaneAngle3d")) r << m.routeId;
                return r;
            };
            {
                std::vector<MetricSeries> s{ chest };
                Skeleton3dBodyOutput o = buildSkeleton3dBody(in);
                const Skeleton3dBodyMerge mg = mergeSkeleton3dBody(s, o);
                check(mg.published.contains(QStringLiteral("shoulderPlaneAngle3d"))
                          && routes(s) == QStringList({ QStringLiteral("thoraxImu"), QStringLiteral("faceOn+dtl") }),
                      "§10 a chest sensor's shoulder plane already written ⇒ the 3-D one is published BESIDE it");
                std::vector<MetricSeries> s2 = s;
                std::vector<GateRecord> g2;
                applyTrunkSensorPrecedence(s2, g2, in.phases);
                check(routes(s2) == QStringList({ QStringLiteral("thoraxImu") }),
                      "§10 …and the precedence makes the sensor's the answer, the 3-D one erased");
                std::vector<GateRecord> g3{ GateRecord{ QStringLiteral("body.thoraxFaceIn"), QStringLiteral("thoraxRotation"),
                                                        GateEffect::Downgraded, {}, {}, {},
                                                        QStringList{ QStringLiteral("shoulderPlaneAngle3d") } } };
                applyTrunkSensorPrecedence(s, g3, in.phases);
                check(routes(s) == QStringList({ QStringLiteral("faceOn+dtl") }),
                      "§10 …a chest worn face-in: the 3-D shoulder plane is the fallback, the sensor's withdrawn");
            }
            {
                std::vector<MetricSeries> s;
                Skeleton3dBodyOutput o = buildSkeleton3dBody(in);
                mergeSkeleton3dBody(s, o);
                s.push_back(chest);   // the sensor's arriving after the 3-D stage
                std::vector<GateRecord> g;
                applyTrunkSensorPrecedence(s, g, in.phases);
                check(routes(s) == QStringList({ QStringLiteral("thoraxImu") }), "§10 …and the same when the sensor's arrives last");
            }
            {
                MetricSeries other; other.key = QStringLiteral("shoulderPlaneAngle3d"); other.routeId = QStringLiteral("faceOn+dtl");
                other.t_us = { fr.t_us[0] }; other.value = { 1.0 };
                std::vector<MetricSeries> s{ other };
                Skeleton3dBodyOutput o = buildSkeleton3dBody(in);
                const Skeleton3dBodyMerge mg = mergeSkeleton3dBody(s, o);
                check(!mg.published.contains(QStringLiteral("shoulderPlaneAngle3d")) && routes(s).size() == 1,
                      "§10 …while a 3-D shoulder plane already present still stops another");
            }
        }
    }

    // ── §11 the failed route's records come off the substituted reading (route_substitution.h) ────
    {
        const auto rec = [](const char *id, const char *subject, GateEffect e, QStringList covers) {
            return GateRecord{ QString::fromLatin1(id), QString::fromLatin1(subject), e, {}, {}, {}, std::move(covers) };
        };
        const QString SB = QStringLiteral("spineSideBend"), LK = QStringLiteral("leadKneeFlexion");
        std::vector<GateRecord> g{
            rec("body.upperAddress", "secondaryAxisTilt", GateEffect::Refused, { SB, QStringLiteral("thoraxLateralDrift") }),
            rec("posture.address", "pelvisThrust", GateEffect::Refused, { QStringLiteral("headRise"), LK }),
            rec("body.sideBendLines", "spineSideBend", GateEffect::Masked, {}),          // subject, nothing else ⇒ dropped
            rec("body.fakeBound", "spineSideBend", GateEffect::Bounded, { QStringLiteral("trunkRise") }),   // subject ⇒ next cover
            rec("body.rotation", "pelvisRotation", GateEffect::Refused, { QStringLiteral("xFactor") }),     // unrelated: untouched
            rec("body.someDowngrade", "spineSideBend", GateEffect::Downgraded, {}),     // a downgrade: untouched
            rec("body3d.cameraAssumed", "shoulderLineYaw", GateEffect::Downgraded, { QStringLiteral("shoulderLineYaw") }),
            body3dFromFitRecord(SB, "faceOn"),
            body3dFromFitRecord(LK, "dtl"),
        };
        MetricSeries sb3; sb3.key = SB; sb3.routeId = QStringLiteral("faceOn+dtl");
        MetricSeries lk3; lk3.key = LK; lk3.routeId = QStringLiteral("faceOn+dtl");
        std::vector<GateRecord> unchanged = g;
        // No 3-D series ⇒ nothing moves (the marker needs its reading present, on its route).
        MetricSeries sbFo; sbFo.key = SB;   // a face-on side bend (no routeId)
        check(reconcileSubstitutedReadings(unchanged, std::vector<MetricSeries>{ sbFo }) == 0 && unchanged.size() == g.size(),
              "§11 the substitute's series absent, or on another route ⇒ nothing is touched");
        const int n = reconcileSubstitutedReadings(g, std::vector<MetricSeries>{ sb3, lk3 });
        const auto byId = [&](const char *id) -> const GateRecord * {
            for (const GateRecord &r : g) if (r.gate == QLatin1String(id)) return &r;
            return nullptr;
        };
        std::snprintf(buf, sizeof buf, "§11 %d (record, reading) pairs cleaned", n);
        check(n == 4, buf);
        check(byId("body.upperAddress") && byId("body.upperAddress")->covers == QStringList{ QStringLiteral("thoraxLateralDrift") }
                  && byId("body.upperAddress")->subject == QStringLiteral("secondaryAxisTilt"),
              "§11 the face-on cascade no longer covers the side bend; it still refuses the rest");
        check(byId("posture.address") && byId("posture.address")->covers == QStringList{ QStringLiteral("headRise") },
              "§11 the down-the-line cascade no longer covers the lead knee");
        check(!byId("body.sideBendLines"), "§11 a record whose ONLY reading was the substituted one is dropped");
        check(byId("body.fakeBound") && byId("body.fakeBound")->subject == QStringLiteral("trunkRise")
                  && byId("body.fakeBound")->covers.isEmpty(),
              "§11 a record whose SUBJECT was it takes its next cover as subject");
        check(byId("body.rotation") && byId("body.rotation")->covers == QStringList{ QStringLiteral("xFactor") }
                  && byId("body.someDowngrade") && byId("body3d.cameraAssumed"),
              "§11 unrelated records, downgrades and the 3-D producer's own records are untouched");
        const auto replacedRecords = [&](const QString &k) {
            for (const GateRecord &r : g)
                if (r.gate == QStringLiteral("body3d.fromFit") && r.subject == k)
                    return r.evidence.value(QStringLiteral("replacedRecords")).toStringList();
            return QStringList();
        };
        check(replacedRecords(SB) == (QStringList{ QStringLiteral("body.upperAddress"), QStringLiteral("body.sideBendLines"),
                                             QStringLiteral("body.fakeBound") })
                  && replacedRecords(LK) == QStringList{ QStringLiteral("posture.address") },
              "§11 the failed route's reasons are kept, on the substitute record's replacedRecords");
        // What now speaks for each 3-D reading: only honest records.
        for (const QString &k : { SB, LK }) {
            bool honest = true;
            for (const GateRecord *r : recordsFor(g, k))
                if (r->effect != GateEffect::Downgraded) honest = false;
            std::snprintf(buf, sizeof buf, "§11 %s: every record that speaks for it is a Downgraded one "
                          "(body3d.fromFit and the like), none a refusal", qPrintable(k));
            check(honest && !recordsFor(g, k).empty(), buf);
        }
    }

    // ── §11b a gap the substitute did not fill keeps the failed route's REAL reason ─────────────
    // The down-the-line posture refused outright (posture.address); the fit reads the lead knee, but
    // not through impact. m_leadKneeFlex (the peak over P7–P8) has no value there. After the clean-up
    // no refusal speaks for the reading, so the reason must come from the substitute's replacedRecords
    // — the record LiveMeasureSource::gateCause falls back to — and never "phase not segmented".
    {
        sk::FitResult blind = fr;
        for (int i = 86; i <= 104; ++i) blind.tier[size_t(i)][LeftLeg] = sk::TierInferred;
        Skeleton3dBodyInput bi = in; bi.fit = &blind;
        Skeleton3dBodyOutput o = buildSkeleton3dBody(bi);
        std::vector<MetricSeries> s;                                  // DtlPosture refused: no lead knee
        const Skeleton3dBodyMerge mg = mergeSkeleton3dBody(s, o);
        const QString LK = QStringLiteral("leadKneeFlexion");
        std::vector<GateRecord> g{ GateRecord{ QStringLiteral("posture.address"), QStringLiteral("pelvisThrust"),
                                               GateEffect::Refused, {}, {}, {}, { QStringLiteral("headRise"), LK } } };
        g.insert(g.end(), mg.gates.begin(), mg.gates.end());
        reconcileSubstitutedReadings(g, s);
        const MetricSeries *k = find(s, "leadKneeFlexion");
        const Graded atImpact = k ? grade(*k, Phase::Impact, ladder[2].t_us) : Graded{};
        check(k && k->routeId == QStringLiteral("faceOn+dtl") && !atImpact.fromCurve && !std::isfinite(atImpact.value)
                  && !sampleAt(*k, Phase::Impact),
              "§11b the substitute lead knee is published but has NO reading at impact (the graded phase)");
        check(!gates::primaryRecord(g, LK), "§11b …no refusal speaks for the reading any more (the clean-up ran)");
        const QString id = substitutedGapGate(g, LK);
        const QString clause = gates::notJudgedClause(LK, id);
        std::snprintf(buf, sizeof buf, "§11b …the gap is explained by %s: \"%s\", not \"the phase this reads at was not segmented\"",
                      qPrintable(id), qPrintable(clause));
        const gates::GateEntry *pe = gates::gateEntry(QStringLiteral("posture.address"));
        check(id == QStringLiteral("posture.address") && pe && clause.contains(pe->shortText)
                  && !clause.contains(QStringLiteral("not segmented")), buf);
        // The array survives a save and reload (gateRecordsToJson / FromJson): a QVariantList of strings.
        const std::vector<GateRecord> back = gateRecordsFromJson(gateRecordsToJson(g));
        check(substitutedGapGate(back, LK) == QStringLiteral("posture.address"),
              "§11b …and replacedRecords survives serialisation, so a reloaded swing gives the same reason");
    }

    // ── §12 the trail knee from the 3-D fit (W5, 13 Nov 2026) ────────────────────────────────────
    // A swing whose TRAIL knee is known at every frame: 15° + 0.2°/frame (address frame 10 → 17°, the top
    // frame 60 → 27°, impact frame 90 → 33°), while the lead knee holds 20°. The trail leg is the RIGHT
    // one for a right-hander, the LEFT one for a left-hander; the lead knee must not be read for it.
    {
        const auto trailTruth = [](int i) { return 15.0 + 0.2 * i; };
        const int m = 120;
        std::vector<PhaseEvent> lad;
        const auto at = [&](Phase ph, int64_t t) { PhaseEvent e; e.phase = ph; e.t_us = t; lad.push_back(e); };
        sk::FitResult proto = makeFit(m);
        at(Phase::Address, proto.t_us[10]);
        at(Phase::ShaftParallelBack, proto.t_us[30]);
        at(Phase::MidBackswing, proto.t_us[45]);
        at(Phase::Top, proto.t_us[60]);
        at(Phase::ArmParallelDown, proto.t_us[72]);
        at(Phase::Delivery, proto.t_us[80]);
        at(Phase::Impact, proto.t_us[90]);
        at(Phase::ShaftParallelThrough, proto.t_us[100]);
        const auto swingFor = [&](bool leftHanded) {
            sk::FitResult f = makeFit(m);
            for (int i = 0; i < m; ++i) {
                Pose p;
                p.leadFlex = 20.0;
                p.trailFlex = trailTruth(i);
                p.pelvisYaw = -30.0 * std::clamp((i - 20) / 40.0, 0.0, 1.0);
                p.trailLegYaw = p.pelvisYaw;
                setBody(f, size_t(i), p, leftHanded);
            }
            return f;
        };
        for (bool lh : { false, true }) {
            const QString who = QString::fromLatin1(lh ? "left-hander" : "right-hander");
            const sk::FitResult f = swingFor(lh);
            Skeleton3dBodyInput ti; ti.fit = &f; ti.phases = lad; ti.leftHanded = lh;
            const Skeleton3dBodyOutput o = buildSkeleton3dBody(ti);
            check(o.trailKnee.has_value() && o.trailKnee->key == QStringLiteral("trailKneeFlexion")
                      && o.trailKnee->routeId == QStringLiteral("faceOn+dtl") && !o.trailKnee->routeDirect
                      && o.trailKnee->sigmaKind == uint8_t(SigmaKind::Propagated),
                  qPrintable(QStringLiteral("§12 %1: a 3-D trailKneeFlexion, route faceOn+dtl, Estimated, σ Propagated").arg(who)));
            if (!o.trailKnee) continue;
            bool every = o.trailKnee->t_us.size() == size_t(m);
            for (int i = 0; i < m && every; ++i) every = near(o.trailKnee->value[size_t(i)], trailTruth(i), 1e-9);
            check(every, qPrintable(QStringLiteral("§12 %1: the curve is the TRAIL knee's known bend at every frame "
                                                   "(+ = more bend, 0 = straight)").arg(who)));
            const PhaseSample *a = sampleAt(*o.trailKnee, Phase::Address), *t = sampleAt(*o.trailKnee, Phase::Top),
                              *im = sampleAt(*o.trailKnee, Phase::Impact);
            check(a && t && im && o.trailKnee->phaseSamples.size() == 3,
                  qPrintable(QStringLiteral("§12 %1: stamped at address, the top and impact — the down-the-line "
                                            "series' positions — and nowhere else").arg(who)));
            // Frames within ±15 ms of a frame instant: that frame and one either side, so the median is it.
            check(a && near(a->value, 17.0, 1e-9) && t && near(t->value, 27.0, 1e-9) && im && near(im->value, 33.0, 1e-9),
                  qPrintable(QStringLiteral("§12 %1: address 17°, top 27°, impact 33°").arg(who)));
            const b3::Reading k60 = b3::kneeFlexDeg(f, 60, /*leftLeg*/ lh);
            check(t && t->sigma && near(*t->sigma, k60.sigma, 0.05) && *t->sigma > 0,
                  qPrintable(QStringLiteral("§12 %1: σ at the top is the knee angle's gradient σ (%2°), as the lead knee's")
                                 .arg(who).arg(k60.sigma, 0, 'f', 3)));
            check(o.leadKnee && sampleAt(*o.leadKnee, Phase::Top) && near(sampleAt(*o.leadKnee, Phase::Top)->value, 20.0, 1e-9),
                  qPrintable(QStringLiteral("§12 %1: …and the lead knee still reads the LEAD leg (20°): the legs are not swapped").arg(who)));
            check(o.trailKneeAtAddressAndTop, qPrintable(QStringLiteral("§12 %1: it reads at address and the top").arg(who)));
            // Graded == stamped, and m_trailKneeFlex (P1 → P4) off the curve is the truth's +10°.
            SeriesView view;
            view.t = o.trailKnee->t_us.data(); view.v = o.trailKnee->value.data(); view.n = o.trailKnee->t_us.size();
            view.valid = o.trailKnee->valid.size() == o.trailKnee->t_us.size() ? o.trailKnee->valid.data() : nullptr;
            const Reduced d = reduceDelta(view, lad[0].t_us, lad[3].t_us, ReduceConfig{});
            check(d.ok && near(d.value, 10.0, 1e-9),
                  qPrintable(QStringLiteral("§12 %1: m_trailKneeFlex (P1 → P4) off the curve = +10°").arg(who)));
        }

        const sk::FitResult rh = swingFor(false);
        // The replace rule is for CALIBRATED cameras (Mark, 13 Nov); uncalibrated, the down-the-line
        // series stays wherever it exists (below).
        Skeleton3dBodyInput ti; ti.fit = &rh; ti.phases = lad; ti.calibrated = true;
        Skeleton3dBodyInput tu = ti; tu.calibrated = false;
        // A down-the-line trail knee that reads at every position, on its own (camera) grid.
        MetricSeries dtl; dtl.key = QStringLiteral("trailKneeFlexion"); dtl.label = QStringLiteral("Trail knee flexion");
        for (int i = 0; i < m; ++i) { dtl.t_us.push_back(rh.t_us[size_t(i)] + 1000); dtl.value.push_back(trailTruth(i) + 2.0); }
        for (const PhaseEvent &e : lad)
            if (e.phase == Phase::Address || e.phase == Phase::Top || e.phase == Phase::Impact)
                dtl.phaseSamples.push_back({ e.phase, e.t_us, 0.0, QString() });
        // UNCALIBRATED (every swing today): the down-the-line series is the answer wherever it exists,
        // even when the 3-D one reads at P1 and P4; with none, the 3-D one fills, with its record.
        {
            Skeleton3dBodyOutput o = buildSkeleton3dBody(tu);
            check(o.trailKnee && o.trailKneeAtAddressAndTop && !o.calibrated,
                  "§12 uncalibrated: the 3-D trail knee reads at address and the top");
            std::vector<MetricSeries> s{ dtl };
            const Skeleton3dBodyMerge mg = mergeSkeleton3dBody(s, o, tu.phases);
            const MetricSeries *k = find(s, "trailKneeFlexion");
            check(!mg.trailKneeReplaced && mg.trailKneeKeptDtl && mg.trailKneeKeptUncalibrated && count(s, "trailKneeFlexion") == 1
                      && k && k->routeId.isEmpty() && k->value.front() == dtl.value.front()
                      && !mg.published.contains(QStringLiteral("trailKneeFlexion")),
                  "§12 …yet the down-the-line one STAYS, as produced, and the 3-D one is not published beside it");
            check(mg.trailKneeLost.empty() && mg.trailKneeGained.empty()
                      && std::none_of(mg.gates.begin(), mg.gates.end(),
                                      [](const GateRecord &g) { return g.subject == QStringLiteral("trailKneeFlexion"); }),
                  "§12 …with no record and no positions counted");
            Skeleton3dBodyOutput o2 = buildSkeleton3dBody(tu);
            std::vector<MetricSeries> none;
            const Skeleton3dBodyMerge m2 = mergeSkeleton3dBody(none, o2, tu.phases);
            const MetricSeries *f = find(none, "trailKneeFlexion");
            const GateRecord *rec = nullptr;
            for (const GateRecord &g : m2.gates) if (g.subject == QStringLiteral("trailKneeFlexion")) rec = &g;
            check(f && f->routeId == QStringLiteral("faceOn+dtl") && rec && rec->gate == QStringLiteral("body3d.fromFit")
                      && rec->evidence.value(QStringLiteral("replaces")).toString() == QStringLiteral("dtl")
                      && !m2.trailKneeKeptDtl && !m2.trailKneeReplaced,
                  "§12 uncalibrated, no down-the-line series ⇒ the 3-D trail knee FILLS, with body3d.fromFit (replaces dtl)");
        }
        // CALIBRATED. Replace: valid at P1 and P4.
        {
            Skeleton3dBodyOutput o = buildSkeleton3dBody(ti);
            std::vector<MetricSeries> s{ dtl };
            const Skeleton3dBodyMerge mg = mergeSkeleton3dBody(s, o, lad);
            const MetricSeries *k = find(s, "trailKneeFlexion");
            check(mg.trailKneeReplaced && !mg.trailKneeKeptDtl && count(s, "trailKneeFlexion") == 1 && k
                      && k->routeId == QStringLiteral("faceOn+dtl"),
                  "§12 calibrated: the 3-D trail knee reads at address AND the top ⇒ it REPLACES the down-the-line one");
            check(mg.trailKneeLost.empty() && mg.trailKneeGained.empty(),
                  "§12 …and with both reading every position, no position is lost or gained");
            check(std::none_of(mg.gates.begin(), mg.gates.end(),
                               [](const GateRecord &g) { return g.subject == QStringLiteral("trailKneeFlexion"); }),
                  "§12 …with no body3d.fromFit on it: the better rung replacing, not standing in");
        }
        // Keep: the 3-D trail knee unread at the top (its knee inferred over frames 55–65).
        {
            sk::FitResult blind = rh;
            for (int i = 55; i <= 65; ++i) blind.tier[size_t(i)][RightLeg] = sk::TierInferred;
            Skeleton3dBodyInput bi = ti; bi.fit = &blind;
            Skeleton3dBodyOutput o = buildSkeleton3dBody(bi);
            check(o.trailKnee && !o.trailKneeAtAddressAndTop && !sampleAt(*o.trailKnee, Phase::Top),
                  "§12 calibrated: a 3-D trail knee blind at the top has no top reading");
            std::vector<MetricSeries> s{ dtl };
            const Skeleton3dBodyMerge mg = mergeSkeleton3dBody(s, o, lad);
            const MetricSeries *k = find(s, "trailKneeFlexion");
            check(mg.trailKneeKeptDtl && !mg.trailKneeKeptUncalibrated && !mg.trailKneeReplaced && count(s, "trailKneeFlexion") == 1 && k
                      && k->routeId.isEmpty() && k->value.front() == dtl.value.front() && k->withheld.isEmpty(),
                  "§12 …so the down-the-line one STAYS, as its producer made it, and the 3-D one is not published "
                  "beside it (P1 → P4 on one route)");
            check(!mg.published.contains(QStringLiteral("trailKneeFlexion"))
                      && std::none_of(mg.gates.begin(), mg.gates.end(),
                                      [](const GateRecord &g) { return g.subject == QStringLiteral("trailKneeFlexion"); }),
                  "§12 …and no record for it");
            // …and the same when it is blind at ADDRESS instead.
            sk::FitResult blindA = rh;
            for (int i = 5; i <= 15; ++i) blindA.tier[size_t(i)][RightFoot] = sk::TierInferred;
            Skeleton3dBodyInput ai = ti; ai.fit = &blindA;
            Skeleton3dBodyOutput oa = buildSkeleton3dBody(ai);
            const bool builtBlindAtAddress = oa.trailKnee.has_value() && !oa.trailKneeAtAddressAndTop;
            std::vector<MetricSeries> sa{ dtl };
            const Skeleton3dBodyMerge ma = mergeSkeleton3dBody(sa, oa, lad);
            check(builtBlindAtAddress && ma.trailKneeKeptDtl && !oa.trailKnee && find(sa, "trailKneeFlexion")
                      && find(sa, "trailKneeFlexion")->routeId.isEmpty(),
                  "§12 …and when it is blind at ADDRESS (the ankle inferred) the down-the-line one stays too");
        }
        // Fill: no down-the-line series at all.
        {
            sk::FitResult blind = rh;
            for (int i = 55; i <= 65; ++i) blind.tier[size_t(i)][RightLeg] = sk::TierInferred;
            Skeleton3dBodyInput bi = ti; bi.fit = &blind;
            Skeleton3dBodyOutput o = buildSkeleton3dBody(bi);
            std::vector<MetricSeries> s;
            const Skeleton3dBodyMerge mg = mergeSkeleton3dBody(s, o, lad);
            const MetricSeries *k = find(s, "trailKneeFlexion");
            check(k && k->routeId == QStringLiteral("faceOn+dtl") && mg.published.contains(QStringLiteral("trailKneeFlexion"))
                      && !mg.trailKneeReplaced && !mg.trailKneeKeptDtl,
                  "§12 no down-the-line series ⇒ the 3-D trail knee FILLS, whatever it covers (here blind at the top)");
            const GateRecord *rec = nullptr;
            for (const GateRecord &g : mg.gates) if (g.subject == QStringLiteral("trailKneeFlexion")) rec = &g;
            check(rec && rec->gate == QStringLiteral("body3d.fromFit") && rec->effect == GateEffect::Downgraded
                      && rec->covers == QStringList{ QStringLiteral("trailKneeFlexion") }
                      && rec->evidence.value(QStringLiteral("replaces")).toString() == QStringLiteral("dtl")
                      && rec->evidence.value(QStringLiteral("route")).toString() == QStringLiteral("faceOn+dtl")
                      && isRouteSubstitution(*rec),
                  "§12 …with body3d.fromFit (Downgraded, replaces dtl), as the lead knee does");
        }
        // The positions between: valid at P1 and P4, blind at P6 (Delivery, frame 80). The 3-D series still
        // replaces (the rule is P1 and P4), and the swap is reported: P6 is LOST — the down-the-line series
        // read it — so m_trailKneeFlex_p6 (P1 → P6) is not assessed on this swing.
        {
            sk::FitResult hole = rh;
            for (int i = 76; i <= 84; ++i) hole.tier[size_t(i)][RightUpLeg] = sk::TierInferred;
            Skeleton3dBodyInput hi = ti; hi.fit = &hole;
            Skeleton3dBodyOutput o = buildSkeleton3dBody(hi);
            std::vector<MetricSeries> s{ dtl };
            const Skeleton3dBodyMerge mg = mergeSkeleton3dBody(s, o, lad);
            check(mg.trailKneeReplaced && mg.trailKneeLost == std::vector<Phase>{ Phase::Delivery } && mg.trailKneeGained.empty(),
                  "§12 calibrated: valid at P1 and P4, blind at P6 ⇒ it replaces, and P6 is reported LOST (the only one)");
            const MetricSeries *k = find(s, "trailKneeFlexion");
            SeriesView view;
            if (k) {
                view.t = k->t_us.data(); view.v = k->value.data(); view.n = k->t_us.size();
                view.valid = k->valid.size() == k->t_us.size() ? k->valid.data() : nullptr;
            }
            check(k && !reduceDelta(view, lad[0].t_us, lad[5].t_us, ReduceConfig{}).ok
                      && reduceDelta(view, lad[0].t_us, lad[3].t_us, ReduceConfig{}).ok
                      && reduceDelta(view, lad[0].t_us, lad[6].t_us, ReduceConfig{}).ok,
                  "§12 …so P1 → P6 is not assessable while P1 → P4 and P1 → P7 are (both ends on the fit)");
            // The reverse: a down-the-line series blind at P5 that the fit reads is a position GAINED.
            MetricSeries dg = dtl;
            dg.valid.assign(dg.t_us.size(), 1);
            for (int i = 68; i <= 76; ++i) dg.valid[size_t(i)] = 0;
            Skeleton3dBodyOutput o2 = buildSkeleton3dBody(ti);
            std::vector<MetricSeries> s2{ dg };
            const Skeleton3dBodyMerge m2 = mergeSkeleton3dBody(s2, o2, lad);
            check(m2.trailKneeReplaced && m2.trailKneeLost.empty() && m2.trailKneeGained == std::vector<Phase>{ Phase::ArmParallelDown },
                  "§12 …and a position the down-the-line series missed and the fit reads is reported GAINED (P5)");
        }
    }

    // ── §13 the 3-D side bend as the trunk sensors' fallback (W5, 13 Nov 2026) ────────────────────
    // Before W5 the 3-D side bend stepped aside for ANY spineSideBend, the sensors' included; when the
    // precedence then withdrew the sensors' (worn face-in), a swing with no face-on side bend had none.
    // Now it is published beside the sensors' and the precedence decides — whichever stage ran first —
    // while the face-on series stays the answer wherever it read.
    {
        MetricSeries sensor; sensor.key = QStringLiteral("spineSideBend"); sensor.routeId = QStringLiteral("trunkImus");
        sensor.routeDirect = true;
        sensor.t_us = { fr.t_us[0], fr.t_us.back() }; sensor.value = { 12.0, 12.0 };
        for (const PhaseEvent &e : in.phases) sensor.phaseSamples.push_back({ e.phase, e.t_us, 12.0, QString() });
        MetricSeries faceOn; faceOn.key = QStringLiteral("spineSideBend");
        faceOn.t_us = { fr.t_us[0], fr.t_us.back() }; faceOn.value = { 4.0, 4.0 };
        for (const PhaseEvent &e : in.phases) faceOn.phaseSamples.push_back({ e.phase, e.t_us, 4.0, QString() });
        const GateRecord faceInRec{ QStringLiteral("body.thoraxFaceIn"), QStringLiteral("thoraxRotation"), GateEffect::Downgraded,
                                    {}, {}, {}, QStringList{ QStringLiteral("spineForwardBend"), QStringLiteral("spineSideBend") } };
        const auto routes = [](const std::vector<MetricSeries> &sv) {
            QStringList r;
            for (const MetricSeries &m : sv) if (m.key == QLatin1String("spineSideBend")) r << m.routeId;
            return r;
        };
        const auto fromFitOn = [](const std::vector<GateRecord> &g) {
            return std::count_if(g.begin(), g.end(), [](const GateRecord &r) {
                return r.gate == QStringLiteral("body3d.fromFit") && r.covers.contains(QStringLiteral("spineSideBend"));
            });
        };
        // The two stage orders: the sensors' series already there when the 3-D stage merges (the pipeline's
        // order), or arriving after it.
        for (bool sensorFirst : { true, false }) {
            const QString order = QString::fromLatin1(sensorFirst ? "sensor first" : "sensor last");
            const auto run = [&](std::vector<MetricSeries> start, bool withFaceIn, std::vector<GateRecord> &g) {
                std::vector<MetricSeries> s = std::move(start);
                if (sensorFirst) s.push_back(sensor);
                Skeleton3dBodyOutput o = buildSkeleton3dBody(in);
                const Skeleton3dBodyMerge mg = mergeSkeleton3dBody(s, o, in.phases);
                g.insert(g.end(), mg.gates.begin(), mg.gates.end());
                if (!sensorFirst) s.push_back(sensor);
                if (withFaceIn) g.push_back(faceInRec);
                applyTrunkSensorPrecedence(s, g, in.phases);
                return s;
            };
            {
                std::vector<GateRecord> g;
                const std::vector<MetricSeries> s = run({}, /*faceIn*/ true, g);
                check(routes(s) == QStringList{ QStringLiteral("faceOn+dtl") } && fromFitOn(g) == 1,
                      qPrintable(QStringLiteral("§13 %1: sensors worn face-in, no face-on side bend ⇒ the 3-D side bend "
                                                "FILLS (with its body3d.fromFit), the sensors' withdrawn").arg(order)));
            }
            {
                std::vector<GateRecord> g;
                const std::vector<MetricSeries> s = run({}, /*faceIn*/ false, g);
                check(routes(s) == QStringList{ QStringLiteral("trunkImus") } && fromFitOn(g) == 0,
                      qPrintable(QStringLiteral("§13 %1: sensors trusted ⇒ the sensors' side bend is the answer, the 3-D one "
                                                "and its body3d.fromFit erased").arg(order)));
            }
            {
                std::vector<GateRecord> g;
                const std::vector<MetricSeries> s = run({ faceOn }, /*faceIn*/ true, g);
                const MetricSeries *sb = find(s, "spineSideBend");
                check(routes(s) == QStringList{ QString() } && sb && sb->value.front() == 4.0 && fromFitOn(g) == 0,
                      qPrintable(QStringLiteral("§13 %1: a face-on side bend present, sensors face-in ⇒ the FACE-ON one "
                                                "stays the answer; no 3-D one was added").arg(order)));
            }
            {
                std::vector<GateRecord> g;
                const std::vector<MetricSeries> s = run({ faceOn }, /*faceIn*/ false, g);
                check(routes(s) == QStringList{ QStringLiteral("trunkImus") } && fromFitOn(g) == 0,
                      qPrintable(QStringLiteral("§13 %1: a face-on side bend present, sensors trusted ⇒ the sensors' wins, "
                                                "as before").arg(order)));
            }
        }
        // The sensors' reading missing a position the 3-D one reads (fewer positions) ⇒ the 3-D one stays.
        {
            MetricSeries thin = sensor;
            thin.phaseSamples.erase(std::remove_if(thin.phaseSamples.begin(), thin.phaseSamples.end(),
                                                   [](const PhaseSample &p) { return p.phase == Phase::Top; }),
                                    thin.phaseSamples.end());
            thin.t_us = { fr.t_us[0] }; thin.value = { 12.0 };
            std::vector<MetricSeries> s{ thin };
            Skeleton3dBodyOutput o = buildSkeleton3dBody(in);
            std::vector<GateRecord> g = mergeSkeleton3dBody(s, o, in.phases).gates;
            applyTrunkSensorPrecedence(s, g, in.phases);
            check(routes(s) == QStringList{ QStringLiteral("faceOn+dtl") } && fromFitOn(g) == 1,
                  "§13 the sensors' reading lacks the top, which the 3-D one reads ⇒ the 3-D side bend is the answer");
        }
        // Another 3-D side bend already present still stops a second.
        {
            MetricSeries other; other.key = QStringLiteral("spineSideBend"); other.routeId = QStringLiteral("faceOn+dtl");
            other.t_us = { fr.t_us[0] }; other.value = { 1.0 };
            std::vector<MetricSeries> s{ other };
            Skeleton3dBodyOutput o = buildSkeleton3dBody(in);
            const Skeleton3dBodyMerge mg = mergeSkeleton3dBody(s, o, in.phases);
            check(!mg.published.contains(QStringLiteral("spineSideBend")) && routes(s).size() == 1,
                  "§13 …while a 3-D side bend already present still stops another");
        }
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
