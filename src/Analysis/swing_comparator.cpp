// swing_comparator.cpp — SwingComparator2D (WP2a). See swing_comparator.h.

#include "swing_comparator.h"
#include "analysis_tuning.h"   // pinpoint::analysis::tuning::apply

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace pinpoint::swingref {

namespace {

using analysis::MetricSeries;
using analysis::PoseFrame2D;
using analysis::PoseTrack2D;
using analysis::Segmentation;
using analysis::ShaftSample2D;
using analysis::ShaftTrack2D;

// ── Sign conventions (pinned by swing_comparator_test.cpp) ───────────────────
// Shaft-angle delta. The PRODUCT convention is "positive = steeper than reference":
// a measured shaft whose swing plane is MORE inclined (more upright) than the
// reference at that phase reads positive. The measured/reference image angles are
// raw atan2(head.y−grip.y, head.x−grip.x) (image px, y-DOWN, grip→head), so the
// raw wrapped difference is (measured − reference). kSteepSign = −1 maps that raw
// difference to the product sign: pinned by swing_comparator_test's injected
// steeper-plane case — a genuine +Δ plane inclination (rotation about the target
// line), read on the down-the-line plane-pitch view, yields a POSITIVE delta at
// impact (raw = −Δ there, so kSteepSign flips it to +Δ).
constexpr double kSteepSign = -1.0;
constexpr double kLagSign   = 1.0;   // β-proxy: measured − reference interior arm↔shaft angle
constexpr double kHubSign   = 1.0;   // hub shift: + = measured mid-shoulder drift in +image-x

constexpr double kTwoPi = 2.0 * M_PI;

inline double wrap(double rad) { return std::remainder(rad, kTwoPi); }   // → (−π,π]
inline double deg(double rad)  { return rad * 180.0 / M_PI; }

analysis::Phase phaseForP(int p)
{
    using P = analysis::Phase;
    switch (p) {
    case 1: return P::Address;
    case 2: return P::ShaftParallelBack;
    case 3: return P::MidBackswing;
    case 4: return P::Top;
    case 5: return P::ArmParallelDown;
    case 6: return P::Delivery;
    case 7: return P::Impact;
    case 8: return P::ShaftParallelThrough;
    default: return P::Address;
    }
}

// Bracket a time-ordered container by t_us. Returns i0<=i1 and interpolation
// fraction a∈[0,1]; false when t is outside [front,back].
template <class Vec, class TimeFn>
bool bracketByTime(const Vec& v, int64_t t, TimeFn tf, int& i0, int& i1, double& a)
{
    const int n = int(v.size());
    if (n == 0) return false;
    if (t < tf(v.front()) || t > tf(v.back())) return false;
    int lo = 0, hi = n - 1;
    while (lo + 1 < hi) {
        const int mid = (lo + hi) / 2;
        if (tf(v[mid]) <= t) lo = mid; else hi = mid;
    }
    i0 = lo; i1 = hi;
    const int64_t t0 = tf(v[i0]), t1 = tf(v[i1]);
    a = (t1 == t0) ? 0.0 : double(t - t0) / double(t1 - t0);
    return true;
}

// Measured shaft image angle (unwrapped, radians) at t, gap when a bracket sample
// is below the confidence floor.
std::optional<double> measuredThetaAt(const ShaftTrack2D& shaft, int64_t t, double confMin)
{
    int i0, i1; double a;
    if (!bracketByTime(shaft.samples, t, [](const ShaftSample2D& s) { return s.t_us; }, i0, i1, a))
        return std::nullopt;
    if (shaft.samples[i0].conf < confMin || shaft.samples[i1].conf < confMin)
        return std::nullopt;
    const double t0 = shaft.samples[i0].thetaRad, t1 = shaft.samples[i1].thetaRad;
    return t0 + a * (t1 - t0);
}

// Measured grip image px at t (image px in-memory, per swing_analysis.h).
std::optional<QPointF> measuredGripAt(const ShaftTrack2D& shaft, int64_t t)
{
    int i0, i1; double a;
    if (!bracketByTime(shaft.samples, t, [](const ShaftSample2D& s) { return s.t_us; }, i0, i1, a))
        return std::nullopt;
    const QPointF g0 = shaft.samples[i0].gripPx, g1 = shaft.samples[i1].gripPx;
    return g0 + (g1 - g0) * a;
}

// Located P-position grip px (Layer B positions[]), if present.
std::optional<QPointF> positionGrip(const ShaftTrack2D& shaft, int p)
{
    for (const auto& pos : shaft.positions)
        if (pos.p == p) return pos.gripPx;
    return std::nullopt;
}

const std::vector<PoseFrame2D>& poseSource(const PoseTrack2D& pose)
{
    return pose.smoothed.empty() ? pose.frames : pose.smoothed;   // never smoothedSynth
}

// Interpolated keypoint at t → (image px, min bracket confidence). Normalised kp
// are scaled to px by (W,H) so image angles are comparable to the shaft θ.
std::optional<std::pair<QPointF, float>>
kpAt(const std::vector<PoseFrame2D>& frames, int64_t t, int idx, double W, double H)
{
    int i0, i1; double a;
    if (!bracketByTime(frames, t, [](const PoseFrame2D& f) { return f.t_us; }, i0, i1, a))
        return std::nullopt;
    const PoseFrame2D& f0 = frames[i0];
    const PoseFrame2D& f1 = frames[i1];
    const QPointF p0(f0.kp[idx].x() * W, f0.kp[idx].y() * H);
    const QPointF p1(f1.kp[idx].x() * W, f1.kp[idx].y() * H);
    const QPointF p = p0 + (p1 - p0) * a;
    return std::make_pair(p, std::min(f0.conf[idx], f1.conf[idx]));
}

// Mid-shoulder image px at t (COCO 5,6), gap when either shoulder is below floor.
std::optional<QPointF>
midShoulderAt(const std::vector<PoseFrame2D>& frames, int64_t t, double W, double H, double confMin)
{
    const auto l = kpAt(frames, t, 5, W, H);
    const auto r = kpAt(frames, t, 6, W, H);
    if (!l || !r) return std::nullopt;
    if (l->second < confMin || r->second < confMin) return std::nullopt;
    return (l->first + r->first) * 0.5;
}

// Lead-arm image line angle (mid-shoulder → lead-wrist), gap on low confidence.
std::optional<double>
leadArmAngleAt(const std::vector<PoseFrame2D>& frames, int64_t t, bool rightHanded,
               double W, double H, double confMin)
{
    const auto mid = midShoulderAt(frames, t, W, H, confMin);
    if (!mid) return std::nullopt;
    const int wristIdx = rightHanded ? 9 : 10;   // COCO 9 left wrist / 10 right wrist
    const auto w = kpAt(frames, t, wristIdx, W, H);
    if (!w || w->second < confMin) return std::nullopt;
    return std::atan2(w->first.y() - mid->y(), w->first.x() - mid->x());
}

double perpComponent(const QPointF& v, const QPointF& lineDir)
{
    // Component of v along the unit normal of lineDir (2D).
    const double len = std::hypot(lineDir.x(), lineDir.y());
    if (len <= 0) return 0.0;
    const QPointF n(-lineDir.y() / len, lineDir.x() / len);
    return v.x() * n.x() + v.y() * n.y();
}

// Segment bucket for a GLOBAL s (0 backswing, 1 downswing, 2 follow-through/exit).
int segBucket(double g)
{
    if (g < 1.0) return 0;
    if (g < 2.0) return 1;
    return 2;
}

// The process-wide default mask (D6). defaultMaskTable() is declared in the header.
const std::vector<MaskEntry> defaultMask_ = defaultMaskTable();

// ── The concrete 2D comparator ──────────────────────────────────────────────
class SwingComparator2D final : public SwingComparator {
public:
    ComparisonResult compare(const SwingReferenceModel& model,
                             const CameraProjection& proj,
                             const ShaftTrack2D& shaft,
                             const PoseTrack2D& pose,
                             const Segmentation& seg,
                             double clubLenM, int view,
                             const ComparatorConfig& cfg) const override;

    QString tier() const override { return QStringLiteral("2D"); }
};

ComparisonResult SwingComparator2D::compare(const SwingReferenceModel& model,
                                            const CameraProjection& proj,
                                            const ShaftTrack2D& shaft,
                                            const PoseTrack2D& pose,
                                            const Segmentation& seg,
                                            double clubLenM, int view,
                                            const ComparatorConfig& cfg) const
{
    ComparisonResult out;

    const std::vector<MaskEntry>& mask =
        cfg.maskOverride.empty() ? defaultMask_ : cfg.maskOverride;

    const PhaseMap pm(shaft, seg);
    if (!pm.valid())
        return out;   // degrade: valid=false, empty

    out.valid = true;
    out.summary.projectionResidualPx = proj.residualPx();

    const int    N = std::max(1, cfg.sGridPerSegment);
    const double W = shaft.frameWidth  > 0 ? double(shaft.frameWidth)  : 1.0;
    const double H = shaft.frameHeight > 0 ? double(shaft.frameHeight) : 1.0;
    const auto&  frames = poseSource(pose);

    // Reusable evaluation of the reference at a GLOBAL s.
    auto refLineAt = [&](double globalS) -> ProjectedShaftLine {
        const auto sp = PhaseMap::segmentForGlobalS(globalS);
        const ShaftPose pose3d = model.evaluate(sp.first, sp.second);
        return proj.projectShaftLine(pose3d, clubLenM);
    };

    // ── (a) shaft-angle delta over the whole s-grid (incl. exit P7→P8) ───────
    DiagnosticSeries shaftDelta;
    shaftDelta.id = QStringLiteral("shaftAngleDelta");
    shaftDelta.view = view;
    shaftDelta.unit = QStringLiteral("deg");

    // ── (b) β-proxy lag/release ──────────────────────────────────────────────
    DiagnosticSeries lagDelta;
    lagDelta.id = QStringLiteral("lagDelta");
    lagDelta.view = view;
    lagDelta.unit = QStringLiteral("deg");

    // ── (d) hub lateral shift (fixed-hub reference ⇒ measured drift) ─────────
    DiagnosticSeries hubShift;
    hubShift.id = QStringLiteral("hubShift");
    hubShift.view = view;
    hubShift.unit = QStringLiteral("cm");

    // Address mid-shoulder for the hub-shift baseline.
    std::optional<QPointF> midShoulder0;
    if (const auto tP1 = pm.timeForP(1))
        midShoulder0 = midShoulderAt(frames, *tP1, W, H, cfg.kpConfMin);

    // Walk the concatenated s-grid: seg 0 covers i=0..N; segs 1,2 i=1..N (drop the
    // duplicated boundary). GLOBAL s ascending ⇒ t ascending ⇒ series ascending.
    for (int segIdx = 0; segIdx < 3; ++segIdx) {
        for (int i = (segIdx == 0 ? 0 : 1); i <= N; ++i) {
            const double localS  = double(i) / double(N);
            const double globalS = double(segIdx) + localS;

            const auto t = pm.toTime(globalS);
            if (!t) continue;

            const ProjectedShaftLine rl = refLineAt(globalS);

            // (a)
            if (rl.valid) {
                if (const auto mth = measuredThetaAt(shaft, *t, cfg.shaftConfMin)) {
                    const double d = kSteepSign * deg(wrap(*mth - rl.angleRad));
                    const double w = maskWeightFor(mask, shaftDelta.id, view, globalS);
                    shaftDelta.samples.push_back({globalS, *t, d, w});
                }
            }

            // (b) β-proxy — needs the reference hub (anthro) + measured keypoints.
            // Reference arm line = projected hub → projected butt (rl.butt2d).
            if (cfg.anthroValid && rl.valid) {
                const auto hubPx = proj.imagePoint(cfg.anthro.hub);
                const auto mth   = measuredThetaAt(shaft, *t, cfg.shaftConfMin);
                const auto marm  = leadArmAngleAt(frames, *t, cfg.anthro.rightHanded, W, H, cfg.kpConfMin);
                if (hubPx && mth && marm) {
                    const double refArm = std::atan2(rl.butt2d.y() - hubPx->y(),
                                                     rl.butt2d.x() - hubPx->x());
                    const double refLag = wrap(rl.angleRad - refArm);
                    const double meaLag = wrap(*mth - *marm);
                    const double d = kLagSign * deg(wrap(meaLag - refLag));
                    const double w = maskWeightFor(mask, lagDelta.id, view, globalS);
                    lagDelta.samples.push_back({globalS, *t, d, w});
                }
            }

            // (d) hub shift
            if (midShoulder0) {
                if (const auto mid = midShoulderAt(frames, *t, W, H, cfg.kpConfMin)) {
                    const double driftPx = mid->x() - midShoulder0->x();
                    const double cm = kHubSign * (driftPx / cfg.pxPerM) * 100.0;
                    const double w = maskWeightFor(mask, hubShift.id, view, globalS);
                    hubShift.samples.push_back({globalS, *t, cm, w});
                }
            }
        }
    }

    out.diagnostics.push_back(shaftDelta);
    if (!lagDelta.samples.empty()) out.diagnostics.push_back(lagDelta);
    if (!hubShift.samples.empty()) out.diagnostics.push_back(hubShift);

    // ── (c/f) scalar deltas at specific P-anchors ────────────────────────────
    auto shaftDeltaAtP = [&](int p, double& outDeg) -> bool {
        const double gS = PhaseMap::globalSForP(p);
        auto tUse = pm.timeForP(p);
        if (!tUse) tUse = pm.toTime(gS);
        if (!tUse) return false;
        const ProjectedShaftLine rl = refLineAt(gS);
        if (!rl.valid) return false;
        const auto mth = measuredThetaAt(shaft, *tUse, cfg.shaftConfMin);
        if (!mth) return false;
        outDeg = kSteepSign * deg(wrap(*mth - rl.angleRad));
        return true;
    };

    out.summary.p4LaidOffValid   = shaftDeltaAtP(4, out.summary.p4LaidOffDeg);
    out.summary.leanDeltaP7Valid = shaftDeltaAtP(7, out.summary.leanDeltaP7Deg);

    // ── (e) phase-domain scalars ─────────────────────────────────────────────
    {
        const auto t1 = pm.timeForP(1), t4 = pm.timeForP(4), t7 = pm.timeForP(7);
        if (t1 && t4 && t7 && *t4 > *t1 && *t7 > *t4) {
            out.summary.tempoRatio = double(*t4 - *t1) / double(*t7 - *t4);
            out.summary.tempoValid = true;
        }
        const auto t5 = pm.timeForP(5);
        if (t4 && t5 && *t5 > *t4)
            out.summary.transitionTimeUs = *t5 - *t4;
    }

    // Lag retention: measured interior arm↔shaft angle at downswing s≈0.75.
    {
        const double gS = 1.75;
        if (const auto t = pm.toTime(gS)) {
            const auto mth  = measuredThetaAt(shaft, *t, cfg.shaftConfMin);
            const auto marm = leadArmAngleAt(frames, *t, cfg.anthro.rightHanded, W, H, cfg.kpConfMin);
            if (mth && marm) {
                out.summary.lagRetentionDeg   = std::abs(deg(wrap(*mth - *marm)));
                out.summary.lagRetentionValid = true;
            }
        }
    }

    // ── (f) DTL butt-point deviation from the ball-line at P3 / P5 ────────────
    {
        const auto bl0 = proj.imagePoint(QVector3D(-0.30f, 0.0f, 0.0f));
        const auto bl1 = proj.imagePoint(QVector3D( 0.30f, 0.0f, 0.0f));
        bool ok = bool(bl0) && bool(bl1);
        if (ok) {
            const QPointF lineDir = *bl1 - *bl0;
            auto buttDevAtP = [&](int p, double& outPx) -> bool {
                const double gS = PhaseMap::globalSForP(p);
                const auto sp = PhaseMap::segmentForGlobalS(gS);
                const auto refGrip = proj.imagePoint(model.evaluate(sp.first, sp.second).butt);
                if (!refGrip) return false;
                std::optional<QPointF> meaGrip = positionGrip(shaft, p);
                if (!meaGrip) {
                    if (const auto t = pm.timeForP(p)) meaGrip = measuredGripAt(shaft, *t);
                    else if (const auto tg = pm.toTime(gS)) meaGrip = measuredGripAt(shaft, *tg);
                }
                if (!meaGrip) return false;
                outPx = perpComponent(*meaGrip - *refGrip, lineDir);
                return true;
            };
            const bool a3 = buttDevAtP(3, out.summary.buttDeviationP3Px);
            const bool a5 = buttDevAtP(5, out.summary.buttDeviationP5Px);
            out.summary.buttDeviationValid = a3 && a5;
        }
    }

    // ── Per-segment stats + plane shift from the shaft-angle delta series ─────
    {
        double sum2[3] = {0, 0, 0}, peak[3] = {0, 0, 0};
        int    cnt[3]  = {0, 0, 0};
        double planeMag = 0.0, planePhase = -1.0; int planeN = 0;
        for (const auto& smp : shaftDelta.samples) {
            if (smp.weight <= 0.0) continue;   // mask-weighted
            const int b = segBucket(smp.s);
            sum2[b] += smp.value * smp.value;
            peak[b] = std::max(peak[b], std::abs(smp.value));
            ++cnt[b];
            if (b == 1) {   // downswing → plane shift
                ++planeN;
                if (std::abs(smp.value) > planeMag) { planeMag = std::abs(smp.value); planePhase = smp.s; }
            }
        }
        auto fill = [](SegmentStat& st, double s2, double pk, int n) {
            st.n = n; st.peakDeg = pk;
            st.rmsDeg = n > 0 ? std::sqrt(s2 / double(n)) : 0.0;
        };
        fill(out.summary.backswing, sum2[0], peak[0], cnt[0]);
        fill(out.summary.downswing, sum2[1], peak[1], cnt[1]);
        fill(out.summary.exit,      sum2[2], peak[2], cnt[2]);
        out.summary.planeShiftMagDeg = planeMag;
        out.summary.planeShiftPhaseS = planePhase;
        out.summary.planeShiftN      = planeN;
    }

    // ── Catalogue-facing MetricSeries (masked samples dropped, sparse) ───────
    auto toMetric = [](const DiagnosticSeries& d, const QString& key,
                       const QString& label, const QString& unit) {
        MetricSeries m;
        m.key = key; m.label = label; m.unit = unit;
        for (const auto& s : d.samples) {
            if (s.weight <= 0.0) continue;   // never emit masked/zero
            m.t_us.push_back(s.t_us);
            m.value.push_back(s.value);
        }
        return m;
    };
    out.metrics.push_back(toMetric(shaftDelta, QStringLiteral("refShaftDelta"),
                                   QStringLiteral("Shaft-angle delta"), QStringLiteral("°")));
    out.metrics.push_back(toMetric(lagDelta, QStringLiteral("refLagDelta"),
                                   QStringLiteral("Lag / release delta"), QStringLiteral("°")));
    out.metrics.push_back(toMetric(hubShift, QStringLiteral("refHubShift"),
                                   QStringLiteral("Hub lateral shift"), QStringLiteral("cm")));

    return out;
}

} // namespace

// ── Mask table (D6) ─────────────────────────────────────────────────────────
std::vector<MaskEntry> defaultMaskTable()
{
    std::vector<MaskEntry> t;
    // Face-on (view 0): plane-pitch descriptives are uninformative → dark. These
    // ids are not emitted for face-on, but the rows document the rule (mask-is-data).
    t.push_back({QStringLiteral("planeOffset"),   kViewFaceOn, 0.0, 3.0, 0.0});
    t.push_back({QStringLiteral("buttDeviation"), kViewFaceOn, 0.0, 3.0, 0.0});
    t.push_back({QStringLiteral("p4LaidOff"),     kViewFaceOn, 0.0, 3.0, 0.0});

    // DTL (view 1): dark by default (never runs live in Phase A). The physically
    // meaningful live mask is the inclination/yaw conflation over backswing
    // s∈[0.25,0.75]; Phase B drops the blanket rows and keeps this one.
    t.push_back({QStringLiteral("shaftAngleDelta"), kViewDtl, 0.25, 0.75, 0.0});   // conflation
    t.push_back({QStringLiteral("shaftAngleDelta"), kViewDtl, 0.00, 3.00, 0.0});
    t.push_back({QStringLiteral("lagDelta"),        kViewDtl, 0.00, 3.00, 0.0});
    t.push_back({QStringLiteral("hubShift"),        kViewDtl, 0.00, 3.00, 0.0});
    t.push_back({QStringLiteral("planeOffset"),     kViewDtl, 0.00, 3.00, 0.0});
    t.push_back({QStringLiteral("buttDeviation"),   kViewDtl, 0.00, 3.00, 0.0});
    t.push_back({QStringLiteral("p4LaidOff"),       kViewDtl, 0.00, 3.00, 0.0});
    return t;
}

double maskWeightFor(const std::vector<MaskEntry>& table, const QString& diagnosticId,
                     int view, double globalS)
{
    constexpr double eps = 1e-9;
    double w = 1.0;
    for (const auto& m : table)
        if (m.view == view && m.diagnosticId == diagnosticId &&
            globalS >= m.sLo - eps && globalS <= m.sHi + eps)
            w = std::min(w, m.weight);
    return w;
}

// ── PhaseMap ────────────────────────────────────────────────────────────────
double PhaseMap::globalSForP(int p)
{
    switch (p) {
    case 1: return 0.00;
    case 2: return 0.33;
    case 3: return 0.66;
    case 4: return 1.00;
    case 5: return 1.40;
    case 6: return 1.80;
    case 7: return 2.00;
    case 8: return 3.00;
    default: return -1.0;
    }
}

std::pair<Segment, double> PhaseMap::segmentForGlobalS(double g)
{
    if (g < 1.0) return {Segment::Backswing, std::max(0.0, g)};
    if (g < 2.0) return {Segment::Downswing, g - 1.0};
    return {Segment::FollowThrough, std::min(1.0, g - 2.0)};
}

PhaseMap::PhaseMap(const ShaftTrack2D& shaft, const Segmentation& seg)
{
    // Collect P1..P8 anchors: positions[] preferred, Segmentation fallback per P.
    std::vector<Anchor> cand;
    for (int p = 1; p <= 8; ++p) {
        std::optional<int64_t> t;
        for (const auto& pos : shaft.positions)
            if (pos.p == p) { t = pos.t_us; break; }
        if (!t) {
            if (const analysis::PhaseEvent* e = seg.eventFor(phaseForP(p)))
                t = e->t_us;
        }
        if (t) cand.push_back({p, globalSForP(p), *t});
    }
    // Keep a strictly-ascending-in-t subsequence (drop non-monotone interior P's).
    for (const Anchor& a : cand)
        if (m_anchors.empty() || a.t_us > m_anchors.back().t_us)
            m_anchors.push_back(a);
}

std::optional<int64_t> PhaseMap::timeForP(int p) const
{
    for (const Anchor& a : m_anchors)
        if (a.p == p) return a.t_us;
    return std::nullopt;
}

std::optional<double> PhaseMap::toGlobalS(int64_t t_us) const
{
    if (!valid()) return std::nullopt;
    if (t_us < m_anchors.front().t_us || t_us > m_anchors.back().t_us) return std::nullopt;
    for (std::size_t i = 0; i + 1 < m_anchors.size(); ++i) {
        const Anchor& a = m_anchors[i];
        const Anchor& b = m_anchors[i + 1];
        if (t_us >= a.t_us && t_us <= b.t_us) {
            const double f = (b.t_us == a.t_us) ? 0.0
                              : double(t_us - a.t_us) / double(b.t_us - a.t_us);
            return a.s + f * (b.s - a.s);
        }
    }
    return m_anchors.back().s;
}

std::optional<int64_t> PhaseMap::toTime(double globalS) const
{
    if (!valid()) return std::nullopt;
    constexpr double eps = 1e-9;
    if (globalS < m_anchors.front().s - eps || globalS > m_anchors.back().s + eps)
        return std::nullopt;
    for (std::size_t i = 0; i + 1 < m_anchors.size(); ++i) {
        const Anchor& a = m_anchors[i];
        const Anchor& b = m_anchors[i + 1];
        if (globalS >= a.s - eps && globalS <= b.s + eps) {
            const double f = (b.s == a.s) ? 0.0 : (globalS - a.s) / (b.s - a.s);
            return a.t_us + int64_t(std::llround(f * double(b.t_us - a.t_us)));
        }
    }
    return m_anchors.back().t_us;
}

// ── Tuning ──────────────────────────────────────────────────────────────────
ComparatorConfig ComparatorConfig::fromOverrides(const QVariantMap &ov)
{
    ComparatorConfig c;   // struct default is already seeded from tuned::swingref::
    pinpoint::analysis::tuning::apply(ov, "swingref.samplesPerSegment", c.sGridPerSegment);
    return c;
}

// ── Factory ─────────────────────────────────────────────────────────────────
std::unique_ptr<SwingComparator> makeSwingComparator(ComparatorTier tier)
{
    if (tier == ComparatorTier::TwoD)
        return std::make_unique<SwingComparator2D>();
    return nullptr;   // ThreeD: Phase B, behind the same ABC
}

} // namespace pinpoint::swingref
