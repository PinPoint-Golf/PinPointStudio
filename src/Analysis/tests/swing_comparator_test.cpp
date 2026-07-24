// Standalone test for the WP2a SwingComparator2D (swing_comparator.h).
//
//   cmake -S src/Analysis/tests -B build/analyzer-tests -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.0/gcc_64
//   cmake --build build/analyzer-tests && ctest --test-dir build/analyzer-tests --output-on-failure
//
// The fixture SYNTHESISES a measured shaft track by projecting the idealised
// reference itself through a synthetic camera (T3's approach), on the exact s-grid
// the comparator queries — so a faithful measurement reproduces the reference and
// every delta is 0. Then it injects known perturbations and checks recovery:
//
//   A. Self-comparison ≡ 0 (face-on AND DTL).
//   B. Injected +3° plane rotation (steeper) recovered on the DTL (plane-pitch)
//      view with ≈3° magnitude and POSITIVE sign (pins "positive = steep").
//   C. Time-warp (nonlinear monotone clock) → s-domain deltas unchanged.
//   D. Masked spans absent from the emitted MetricSeries (kept, weight 0, in the
//      rich DiagnosticSeries); DTL run ⇒ refShaftDelta metric empty.
//   E. PhaseMap monotonicity + missing-P Segmentation fallback + non-monotone drop.
//   F. β-proxy against synthetic keypoints (self ≡ 0; injected wrist rotation).
//   G. DTL diagnostics produce correct values while carrying weight 0 by default.

#include "../swing_comparator.h"
#include "../camera_projection.h"
#include "../swing_analysis.h"
#include "../../Models/swing_reference.h"

#include <opencv2/core.hpp>
#include <opencv2/calib3d.hpp>

#include <QPointF>
#include <QQuaternion>
#include <QVector3D>

#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

using namespace pinpoint::swingref;
namespace an = pinpoint::analysis;

static int g_fail = 0;

#define CHECK(label, cond)                                                             \
    do {                                                                               \
        const bool ok = (cond);                                                        \
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);                       \
        if (!ok) ++g_fail;                                                             \
    } while (0)

#define CHECK_LT(label, got, lim)                                                      \
    do {                                                                               \
        const double g = (got), l = (lim);                                             \
        const bool ok = g < l;                                                         \
        std::printf("  [%s] %-42s %10.6f < %.6f\n", ok ? "PASS" : "FAIL", label, g, l); \
        if (!ok) ++g_fail;                                                             \
    } while (0)

#define CHECK_NEAR(label, got, want, tol)                                              \
    do {                                                                               \
        const double g = (got), w = (want);                                            \
        const bool ok = std::abs(g - w) <= (tol);                                      \
        std::printf("  [%s] %-34s got %10.5f  want %10.5f (tol %.4f)\n",                \
                    ok ? "PASS" : "FAIL", label, g, w, double(tol));                   \
        if (!ok) ++g_fail;                                                             \
    } while (0)

// ── Synthetic camera (OpenCV convention: +Z forward, +X right, +Y down) ──────
struct GtCamera { CameraIntrinsics K; QVector3D rvec, tvec; };

static GtCamera makeCamera(const QVector3D& C, const QVector3D& target,
                           const QVector3D& worldUp, double f, int W, int H)
{
    const QVector3D z = (target - C).normalized();
    const QVector3D x = QVector3D::crossProduct(z, worldUp).normalized();
    const QVector3D y = QVector3D::crossProduct(z, x);
    cv::Matx33d R(x.x(), x.y(), x.z(), y.x(), y.y(), y.z(), z.x(), z.y(), z.z());
    const cv::Vec3d Cv(C.x(), C.y(), C.z());
    const cv::Vec3d t = -(R * Cv);
    cv::Vec3d r; cv::Rodrigues(R, r);
    GtCamera cam;
    cam.K.fx = f; cam.K.fy = f; cam.K.cx = 0.5 * W; cam.K.cy = 0.5 * H;
    cam.K.width = W; cam.K.height = H;
    cam.rvec = QVector3D(float(r[0]), float(r[1]), float(r[2]));
    cam.tvec = QVector3D(float(t[0]), float(t[1]), float(t[2]));
    return cam;
}

static constexpr int    W = 1440, H = 1080;
static constexpr double kClubLen = 1.14;
static constexpr int    kN = 60;   // samples per segment (comparator + fixture MUST agree)

static QVector3D rotAboutX(const QVector3D& v, double deg)
{
    return QQuaternion::fromAxisAndAngle(1, 0, 0, float(deg)).rotatedVector(v);
}

// GLOBAL-s grid the comparator walks: seg 0 → i=0..N, segs 1,2 → i=1..N.
static std::vector<double> gridGlobalS(int N)
{
    std::vector<double> g;
    for (int seg = 0; seg < 3; ++seg)
        for (int i = (seg == 0 ? 0 : 1); i <= N; ++i)
            g.push_back(double(seg) + double(i) / double(N));
    return g;
}

// P1..P8 times. linear ⇒ t ∝ global s; warp ⇒ a monotone nonlinear reclock.
static int64_t pTime(int p, bool warp)
{
    const double gs = PhaseMap::globalSForP(p);      // [0,3]
    const double base = 1'000'000.0, scale = 1'000'000.0;
    const double u = warp ? std::pow(gs / 3.0, 1.6) * 3.0 : gs;
    return int64_t(base + scale * u);
}

struct Synth { an::ShaftTrack2D shaft; an::PoseTrack2D pose; };

// Build a measured track by projecting the reference on the comparator's grid.
//   offsetFn : radians added to the measured image angle at a GLOBAL s (signal).
//   rotXdeg  : rigid plane rotation about the target line (−ve = steeper).
//   wristFn  : if set, also emits pose keypoints (shoulders=projected hub, lead
//              wrist=wristFn(projected butt, projected hub)).
static Synth buildMeasured(const SwingReferenceModel& model, const CameraProjection& proj,
                           bool warp, const std::function<double(double)>& offsetFn,
                           double rotXdeg, const QVector3D& hub3d,
                           const std::function<QPointF(QPointF, QPointF)>* wristFn)
{
    Synth s;
    s.shaft.camera = pinpoint::SourceId(1);
    s.shaft.valid = true;
    s.shaft.frameWidth = W; s.shaft.frameHeight = H;
    for (int p = 1; p <= 8; ++p) {
        an::ShaftPosition pos; pos.p = p; pos.t_us = pTime(p, warp);
        const double gsp = PhaseMap::globalSForP(p);
        const auto spp = PhaseMap::segmentForGlobalS(gsp);
        ShaftPose rp = model.evaluate(spp.first, spp.second);
        if (rotXdeg != 0.0) { rp.butt = rotAboutX(rp.butt, rotXdeg); rp.dir = rotAboutX(rp.dir, rotXdeg).normalized(); }
        const ProjectedShaftLine pl = proj.projectShaftLine(rp, kClubLen);
        if (pl.valid) { pos.gripPx = pl.butt2d; pos.headPx = pl.head2d; pos.thetaRad = pl.angleRad; }
        s.shaft.positions.push_back(pos);
    }
    // PhaseMap over the P-times (positions only) → the exact grid times.
    const PhaseMap pm(s.shaft, an::Segmentation{});

    double prevUnwrapped = 0.0; bool first = true;
    for (const double gS : gridGlobalS(kN)) {
        const auto t = pm.toTime(gS);
        if (!t) continue;
        const auto sp = PhaseMap::segmentForGlobalS(gS);
        ShaftPose ref = model.evaluate(sp.first, sp.second);
        if (rotXdeg != 0.0) { ref.butt = rotAboutX(ref.butt, rotXdeg); ref.dir = rotAboutX(ref.dir, rotXdeg).normalized(); }
        const ProjectedShaftLine line = proj.projectShaftLine(ref, kClubLen);
        if (!line.valid) continue;

        double raw = line.angleRad;
        if (offsetFn) raw += offsetFn(gS);
        const double unwrapped = first ? raw : prevUnwrapped + std::remainder(raw - prevUnwrapped, 2.0 * M_PI);
        prevUnwrapped = unwrapped; first = false;

        an::ShaftSample2D smp;
        smp.t_us = *t; smp.gripPx = line.butt2d; smp.headPx = line.head2d;
        smp.thetaRad = unwrapped; smp.conf = 1.0f;
        s.shaft.samples.push_back(smp);

        if (wristFn) {
            const auto hubPx = proj.imagePoint(hub3d);
            an::PoseFrame2D f; f.t_us = *t;
            const QPointF hp = hubPx ? *hubPx : QPointF(0, 0);
            const QPointF wp = (*wristFn)(line.butt2d, hp);
            f.kp[5] = QPointF(hp.x() / W, hp.y() / H); f.conf[5] = 1.0f;   // L shoulder
            f.kp[6] = QPointF(hp.x() / W, hp.y() / H); f.conf[6] = 1.0f;   // R shoulder
            f.kp[9] = QPointF(wp.x() / W, wp.y() / H); f.conf[9] = 1.0f;   // L wrist (lead, RH)
            s.pose.frames.push_back(f);
        }
    }
    s.pose.camera = pinpoint::SourceId(1);
    return s;
}

// Peak |value| over a diagnostic series (all samples).
static double peakAbs(const DiagnosticSeries& d, double* atS = nullptr)
{
    double m = 0.0; double s = -1;
    for (const auto& x : d.samples) if (std::abs(x.value) > m) { m = std::abs(x.value); s = x.s; }
    if (atS) *atS = s;
    return m;
}
static const DiagnosticSeries* findSeries(const ComparisonResult& r, const char* id)
{
    for (const auto& d : r.diagnostics) if (d.id == QString::fromUtf8(id)) return &d;
    return nullptr;
}
static const an::MetricSeries* findMetric(const ComparisonResult& r, const char* key)
{
    for (const auto& m : r.metrics) if (m.key == QString::fromUtf8(key)) return &m;
    return nullptr;
}

int main()
{
    std::printf("=== swing_comparator.h ===\n\n");

    // Reference model + the anthro/club that built it (β-proxy needs the hub).
    GolferAnthro anthro;
    anthro.hub = QVector3D(0.0f, -0.55f, 1.35f);
    anthro.armLength = 0.62; anthro.rightHanded = true;
    ClubSpec club; club.length = kClubLen; club.lieDeg = 56.0;
    const auto model = makeSwingReferenceModel(anthro, club, RefConfig{});

    const GtCamera faceOn = makeCamera({0.0f, 3.0f, 1.0f}, {0.0f, 0.0f, 0.85f}, {0.0f, 0.0f, 1.0f}, 1500.0, W, H);
    const GtCamera dtl    = makeCamera({-3.0f, 0.0f, 1.2f}, {1.0f, 0.0f, 0.4f}, {0.0f, 0.0f, 1.0f}, 1450.0, W, H);
    const CalibratedProjection projF(faceOn.K, faceOn.rvec, faceOn.tvec);
    const CalibratedProjection projD(dtl.K, dtl.rvec, dtl.tvec);

    const auto cmp = makeSwingComparator(ComparatorTier::TwoD);
    CHECK("factory returns 2D comparator", bool(cmp) && cmp->tier() == QStringLiteral("2D"));
    CHECK("factory returns null for 3D (Phase B)", !makeSwingComparator(ComparatorTier::ThreeD));

    ComparatorConfig cfg;  cfg.sGridPerSegment = kN;

    // ── A. Self-comparison ≡ 0 (both cameras) ────────────────────────────────
    std::printf("\n-- A. self-comparison ≡ 0 --\n");
    {
        const Synth sf = buildMeasured(*model, projF, false, nullptr, 0.0, anthro.hub, nullptr);
        const ComparisonResult rf = cmp->compare(*model, projF, sf.shaft, sf.pose, an::Segmentation{},
                                                  kClubLen, kViewFaceOn, cfg);
        CHECK("face-on result valid", rf.valid);
        const DiagnosticSeries* d = findSeries(rf, "shaftAngleDelta");
        CHECK("face-on shaftAngleDelta present", d && !d->samples.empty());
        CHECK_LT("face-on self |delta| max (deg)", d ? peakAbs(*d) : 9, 1e-6);

        const Synth sd = buildMeasured(*model, projD, false, nullptr, 0.0, anthro.hub, nullptr);
        const ComparisonResult rd = cmp->compare(*model, projD, sd.shaft, sd.pose, an::Segmentation{},
                                                  kClubLen, kViewDtl, cfg);
        const DiagnosticSeries* dd = findSeries(rd, "shaftAngleDelta");
        CHECK_LT("DTL self |delta| max (deg)", dd ? peakAbs(*dd) : 9, 1e-6);
    }

    // ── B. Injected +3° steeper plane recovered on DTL, positive sign ────────
    std::printf("\n-- B. injected steeper plane (DTL) → positive ≈3° --\n");
    {
        // Rotate the shaft about the target line by −3° ⇒ plane inclination θ→θ+3°
        // (steeper). Viewed down-the-line (≈along X) this is an in-image rotation.
        const Synth sd = buildMeasured(*model, projD, false, nullptr, -3.0, anthro.hub, nullptr);
        const ComparisonResult rd = cmp->compare(*model, projD, sd.shaft, sd.pose, an::Segmentation{},
                                                  kClubLen, kViewDtl, cfg);
        const DiagnosticSeries* dd = findSeries(rd, "shaftAngleDelta");
        // Sample at impact (global s≈2.0): shaft ⊥ target line ⇒ a plane rotation
        // about the target line maps ~1:1 to the down-the-line image angle. The
        // global peak lands at foreshortened phases (P2/P4/P6, shaft ∥ axis) where
        // small 3D changes swing the image angle — not a fair recovery point.
        double best = 1e9, impact = 0.0;
        if (dd) for (const auto& x : dd->samples) if (std::abs(x.s - 2.0) < best) { best = std::abs(x.s - 2.0); impact = x.value; }
        std::printf("  [DIAG] DTL impact-phase delta = %+.4f deg\n", impact);
        CHECK_NEAR("DTL injected steepen magnitude (deg)", std::abs(impact), 3.0, 0.9);
        CHECK("DTL injected steepen POSITIVE (positive = steep)", impact > 0.0);
    }

    // ── C. Time-warp invariance (s-domain) ───────────────────────────────────
    std::printf("\n-- C. time-warp invariance (s-domain deltas unchanged) --\n");
    {
        auto off = [](double gS) { return 0.05 * gS; };   // s-dependent signal (rad)
        const Synth lin = buildMeasured(*model, projF, false, off, 0.0, anthro.hub, nullptr);
        const Synth wrp = buildMeasured(*model, projF, true,  off, 0.0, anthro.hub, nullptr);
        const ComparisonResult rl = cmp->compare(*model, projF, lin.shaft, lin.pose, an::Segmentation{},
                                                  kClubLen, kViewFaceOn, cfg);
        const ComparisonResult rw = cmp->compare(*model, projF, wrp.shaft, wrp.pose, an::Segmentation{},
                                                  kClubLen, kViewFaceOn, cfg);
        const DiagnosticSeries* dl = findSeries(rl, "shaftAngleDelta");
        const DiagnosticSeries* dw = findSeries(rw, "shaftAngleDelta");
        bool sizeOk = dl && dw && dl->samples.size() == dw->samples.size();
        CHECK("warp: same sample count", sizeOk);
        double maxDiff = 0.0, sigMag = 0.0;
        if (sizeOk)
            for (std::size_t i = 0; i < dl->samples.size(); ++i) {
                maxDiff = std::max(maxDiff, std::abs(dl->samples[i].value - dw->samples[i].value));
                sigMag  = std::max(sigMag, std::abs(dl->samples[i].value));
            }
        CHECK_LT("warp: |delta(linear) − delta(warp)| max (deg)", maxDiff, 1e-6);
        CHECK("warp: signal is non-trivial (deg)", sigMag > 1.0);
    }

    // ── D. Masked spans absent from the emitted MetricSeries ─────────────────
    std::printf("\n-- D. masked spans absent from output series --\n");
    {
        auto off = [](double gS) { return 0.05 * gS; };
        const Synth sf = buildMeasured(*model, projF, false, off, 0.0, anthro.hub, nullptr);
        ComparatorConfig mc = cfg;
        mc.maskOverride = defaultMaskTable();
        mc.maskOverride.push_back({QStringLiteral("shaftAngleDelta"), kViewFaceOn, 0.40, 0.60, 0.0});
        const ComparisonResult rf = cmp->compare(*model, projF, sf.shaft, sf.pose, an::Segmentation{},
                                                  kClubLen, kViewFaceOn, mc);
        const DiagnosticSeries* d = findSeries(rf, "shaftAngleDelta");
        const an::MetricSeries* m = findMetric(rf, "refShaftDelta");
        CHECK("diagnostic keeps masked samples", d && !d->samples.empty());
        bool diagHasMaskedWeight0 = false;
        if (d) for (const auto& x : d->samples) if (x.s >= 0.40 && x.s <= 0.60) { diagHasMaskedWeight0 = (x.weight == 0.0); break; }
        CHECK("masked diagnostic sample carries weight 0", diagHasMaskedWeight0);
        // The masked window must be absent from the metric.
        PhaseMap pm(sf.shaft, an::Segmentation{});
        bool metricInWindow = false;
        if (m) for (int64_t t : m->t_us) { auto gs = pm.toGlobalS(t); if (gs && *gs >= 0.40 && *gs <= 0.60) metricInWindow = true; }
        CHECK("metric excludes masked window", m && !metricInWindow);
        CHECK("metric sparser than diagnostic", m && d && m->t_us.size() < d->samples.size());
        CHECK("metric never emits a zero-weight gap as a value",
              m && m->t_us.size() == m->value.size());

        // DTL run ⇒ refShaftDelta fully masked ⇒ metric empty.
        const Synth sd = buildMeasured(*model, projD, false, off, 0.0, anthro.hub, nullptr);
        const ComparisonResult rd = cmp->compare(*model, projD, sd.shaft, sd.pose, an::Segmentation{},
                                                  kClubLen, kViewDtl, cfg);
        const an::MetricSeries* md = findMetric(rd, "refShaftDelta");
        CHECK("DTL refShaftDelta metric empty (all masked)", md && md->t_us.empty());
    }

    // ── E. PhaseMap monotonicity + missing-P fallback + non-monotone drop ────
    std::printf("\n-- E. PhaseMap monotonicity + fallback --\n");
    {
        an::ShaftTrack2D shaft; shaft.frameWidth = W; shaft.frameHeight = H;
        // positions carry only P1,P4,P7,P8; Segmentation supplies P2,P3,P5,P6.
        for (int p : {1, 4, 7, 8}) { an::ShaftPosition pos; pos.p = p; pos.t_us = pTime(p, false); shaft.positions.push_back(pos); }
        an::Segmentation seg;
        auto addEv = [&](an::Phase ph, int p) { an::PhaseEvent e; e.phase = ph; e.t_us = pTime(p, false); seg.events.push_back(e); };
        addEv(an::Phase::ShaftParallelBack, 2);
        addEv(an::Phase::MidBackswing, 3);
        addEv(an::Phase::ArmParallelDown, 5);
        addEv(an::Phase::Delivery, 6);
        const PhaseMap pm(shaft, seg);
        CHECK("phasemap valid", pm.valid());
        CHECK("phasemap assembled all 8 anchors", pm.anchors().size() == 8);
        bool mono = true;
        for (std::size_t i = 1; i < pm.anchors().size(); ++i)
            if (!(pm.anchors()[i].t_us > pm.anchors()[i - 1].t_us && pm.anchors()[i].s > pm.anchors()[i - 1].s)) mono = false;
        CHECK("phasemap strictly monotone (s and t)", mono);
        CHECK("timeForP(6) uses Segmentation fallback", pm.timeForP(6) && *pm.timeForP(6) == pTime(6, false));
        // Round-trip monotone: increasing s → increasing t.
        auto t1 = pm.toTime(0.5), t2 = pm.toTime(1.5), t3 = pm.toTime(2.5);
        CHECK("toTime monotone across segments", t1 && t2 && t3 && *t1 < *t2 && *t2 < *t3);

        // Non-monotone interior P dropped.
        an::ShaftTrack2D bad; bad.frameWidth = W; bad.frameHeight = H;
        for (int p : {1, 2, 3, 4}) {
            an::ShaftPosition pos; pos.p = p;
            pos.t_us = (p == 3) ? pTime(1, false) - 5 : pTime(p, false);   // P3 out of order (before P1)
            bad.positions.push_back(pos);
        }
        const PhaseMap pmb(bad, an::Segmentation{});
        bool p3absent = true;
        for (const auto& a : pmb.anchors()) if (a.p == 3) p3absent = false;
        CHECK("non-monotone interior P dropped", p3absent);
        bool monoB = true;
        for (std::size_t i = 1; i < pmb.anchors().size(); ++i) if (pmb.anchors()[i].t_us <= pmb.anchors()[i - 1].t_us) monoB = false;
        CHECK("phasemap stays monotone after drop", monoB);
    }

    // ── F. β-proxy vs synthetic keypoints ────────────────────────────────────
    std::printf("\n-- F. β-proxy (lag) vs synthetic keypoints --\n");
    {
        ComparatorConfig bc = cfg; bc.anthro = anthro; bc.anthroValid = true;
        // Self: lead wrist = projected butt ⇒ measured arm line == reference arm line.
        std::function<QPointF(QPointF, QPointF)> wristSelf = [](QPointF butt, QPointF /*hub*/) { return butt; };
        const Synth ss = buildMeasured(*model, projF, false, nullptr, 0.0, anthro.hub, &wristSelf);
        const ComparisonResult rs = cmp->compare(*model, projF, ss.shaft, ss.pose, an::Segmentation{},
                                                  kClubLen, kViewFaceOn, bc);
        const DiagnosticSeries* dl = findSeries(rs, "lagDelta");
        CHECK("β-proxy series present", dl && !dl->samples.empty());
        CHECK_LT("β-proxy self |lagDelta| max (deg)", dl ? peakAbs(*dl) : 9, 1e-3);

        // Injected: rotate the wrist about the hub by +12° ⇒ known arm-line offset.
        const double dArm = 12.0;
        std::function<QPointF(QPointF, QPointF)> wristRot = [dArm](QPointF butt, QPointF hub) {
            const double a = dArm * M_PI / 180.0;
            const double dx = butt.x() - hub.x(), dy = butt.y() - hub.y();
            return QPointF(hub.x() + dx * std::cos(a) - dy * std::sin(a),
                           hub.y() + dx * std::sin(a) + dy * std::cos(a));
        };
        const Synth si = buildMeasured(*model, projF, false, nullptr, 0.0, anthro.hub, &wristRot);
        const ComparisonResult ri = cmp->compare(*model, projF, si.shaft, si.pose, an::Segmentation{},
                                                  kClubLen, kViewFaceOn, bc);
        const DiagnosticSeries* di = findSeries(ri, "lagDelta");
        CHECK_NEAR("β-proxy injected |lagDelta| (deg)", di ? peakAbs(*di) : 0.0, dArm, 0.5);
        CHECK("β-proxy lagRetention valid", ri.summary.lagRetentionValid);
    }

    // ── G. DTL diagnostics correct while weight 0 by default ─────────────────
    std::printf("\n-- G. DTL diagnostics dark (weight 0) but correct --\n");
    {
        const Synth sd = buildMeasured(*model, projD, false, nullptr, 0.0, anthro.hub, nullptr);
        const ComparisonResult rd = cmp->compare(*model, projD, sd.shaft, sd.pose, an::Segmentation{},
                                                  kClubLen, kViewDtl, cfg);
        const DiagnosticSeries* dd = findSeries(rd, "shaftAngleDelta");
        bool allWeightZero = dd && !dd->samples.empty();
        if (dd) for (const auto& x : dd->samples) if (x.weight != 0.0) allWeightZero = false;
        CHECK("DTL shaftAngleDelta present with values", dd && !dd->samples.empty());
        CHECK("DTL shaftAngleDelta weight 0 everywhere (dark)", allWeightZero);
        CHECK_LT("DTL self values correct (≡0)", dd ? peakAbs(*dd) : 9, 1e-6);
        // p4LaidOff scalar computed (self ⇒ ≈0), summary carries it regardless of mask.
        CHECK("DTL p4LaidOff computed", rd.summary.p4LaidOffValid);
        CHECK_LT("DTL p4LaidOff self (deg)", std::abs(rd.summary.p4LaidOffDeg), 1e-3);
        CHECK("DTL butt deviation computed", rd.summary.buttDeviationValid);
        CHECK_LT("DTL butt deviation P3 self (px)", std::abs(rd.summary.buttDeviationP3Px), 1e-3);
    }

    // ── Scorecard sanity on a face-on run with pose ──────────────────────────
    std::printf("\n-- H. scorecard fields populated --\n");
    {
        ComparatorConfig bc = cfg; bc.anthro = anthro; bc.anthroValid = true;
        std::function<QPointF(QPointF, QPointF)> wristSelf = [](QPointF butt, QPointF) { return butt; };
        const Synth ss = buildMeasured(*model, projF, false, nullptr, 0.0, anthro.hub, &wristSelf);
        const ComparisonResult rs = cmp->compare(*model, projF, ss.shaft, ss.pose, an::Segmentation{},
                                                  kClubLen, kViewFaceOn, bc);
        CHECK("tempo ratio valid", rs.summary.tempoValid && rs.summary.tempoRatio > 0.0);
        CHECK("transition time positive", rs.summary.transitionTimeUs > 0);
        CHECK("lean delta P7 valid", rs.summary.leanDeltaP7Valid);
        CHECK("backswing segment stat populated", rs.summary.backswing.n > 0);
        CHECK("exit segment stat populated", rs.summary.exit.n > 0);
    }

    // ── I. ComparatorConfig::fromOverrides (T2 tuning plumbing) ──────────────
    // "swingref.samplesPerSegment" is deliberately the SAME dotted key
    // RefConfig::samplesPerSegment consumes (src/Models/swing_reference.h /
    // tuning_overrides_test.cpp) — one tuning knob feeding both the model's own
    // keyframe sampling density and the comparator's s-grid resolution. Asserted
    // here (not tuning_overrides_test.cpp) because ComparatorConfig::fromOverrides
    // lives in swing_comparator.cpp, which pulls in CameraProjection::projectShaftLine
    // (camera_projection.cpp, OpenCV calib3d) — a dependency this file already
    // carries but the OpenCV-free core tuning suite deliberately does not.
    std::printf("\n-- I. ComparatorConfig::fromOverrides (swingref.samplesPerSegment, reused key) --\n");
    {
        const ComparatorConfig cdef = ComparatorConfig::fromOverrides(QVariantMap{});
        CHECK("empty map -> ComparatorConfig.sGridPerSegment frozen default 200", cdef.sGridPerSegment == 200);

        QVariantMap ov; ov["swingref.samplesPerSegment"] = 48;
        const ComparatorConfig cc = ComparatorConfig::fromOverrides(ov);
        CHECK("swingref.samplesPerSegment override reaches ComparatorConfig::fromOverrides",
              cc.sGridPerSegment == 48);
    }

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
