// swing_ref_fit.cpp — P-position fit for the swing-reference model (Phase A).
//
// Faithful C++ port of fit_proto.py fit_swing(): bounded coordinate descent with a
// golden-section line search per coordinate and reach-feasibility floors. See
// swing_ref_fit.h for the contract.

#include "swing_ref_fit.h"

#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace pinpoint::swingref {

namespace {

// Fit parameters (the five fitted DoF). hubY / ballOffsetX / lie / lean / xSign /
// projection are all fixed and live in SwingRefFitInput.
struct Params {
    double hubX = 0.0, hubZ = 0.0, arm = 0.0, Lc = 0.0, dth = 0.0;
};

constexpr int    kSweeps      = 4;
constexpr int    kGoldenIters = 24;
constexpr double kMargin      = 0.01;   // reach-feasibility margin (m)

// golden section constant, (sqrt(5)-1)/2.
const double kInvPhi = (std::sqrt(5.0) - 1.0) / 2.0;

// P-index → (segment 0/1/2, localS), matching globalSForP + segmentForGlobalS in
// src/Gui/viz/swing_ref_overlay_math.h (P1=0.00 P2=0.33 P3=0.66 P4=1.00 P5=1.40
// P6=1.80 P7=2.00 P8=3.00; global s split at the segment boundaries). Returns false
// for p outside [1,8]. Note P4/P7 land on the NEXT segment's s=0 (Downswing/
// FollowThrough), which is the same pose as the previous segment's s=1 (the model's
// segment joins are C⁰/C¹), so this matches the prototype's seg_local() exactly.
bool pToSegment(int p, int& segOut, double& localSOut)
{
    double g;
    switch (p) {
    case 1: g = 0.00; break;
    case 2: g = 0.33; break;
    case 3: g = 0.66; break;
    case 4: g = 1.00; break;
    case 5: g = 1.40; break;
    case 6: g = 1.80; break;
    case 7: g = 2.00; break;
    case 8: g = 3.00; break;
    default: return false;
    }
    if (g < 1.0)      { segOut = 0; localSOut = g; }
    else if (g < 2.0) { segOut = 1; localSOut = g - 1.0; }
    else              { segOut = 2; localSOut = g - 2.0; }
    return true;
}

// Anchor position weight Wp (P1/P4/P7 = 3, else 1) and clubhead weight Wh (P1=6,
// P4=0.5, P7=0, else 0.3) — fit_proto.py P_WEIGHT / H_WEIGHT.
double weightP(int p) { return (p == 1 || p == 4 || p == 7) ? 3.0 : 1.0; }
double weightH(int p)
{
    switch (p) {
    case 1: return 6.0;
    case 7: return 0.0;
    case 4: return 0.5;
    default: return 0.3;
    }
}

// Build the reference model for a parameter vector (hubY / ballOffsetX / lie / lean
// / handedness fixed from the input; plane offset = current dth). The model always
// uses the ball-contact IK anchor (solveAnchors, unconditional).
std::unique_ptr<SwingReferenceModel> buildModel(const Params& p, const SwingRefFitInput& in)
{
    GolferAnthro a = in.seedAnthro;
    a.hub       = QVector3D(float(p.hubX), in.seedAnthro.hub.y(), float(p.hubZ));
    a.armLength = p.arm;
    // a.rightHanded unchanged

    ClubSpec c        = in.seedClub;
    c.length          = p.Lc;
    c.ballOffsetX     = in.ballOffsetX;

    RefConfig cfg              = in.cfg;
    cfg.backswingPlaneOffsetDeg = p.dth;

    return makeSwingReferenceModel(a, c, cfg);
}

// Orthographic projection (OrthographicProjection convention).
QPointF project(const SwingRefFitInput& in, const QVector3D& w, double xs)
{
    return QPointF(in.originPx.x() + xs * in.pxPerM * double(w.x()),
                   in.originPx.y() -      in.pxPerM * double(w.z()));
}

// Weighted SSE of projected reference grip/head vs measured px over the anchors.
double objective(const Params& p, const SwingRefFitInput& in, double xs)
{
    const std::unique_ptr<SwingReferenceModel> m = buildModel(p, in);
    double sse = 0.0;
    for (const SwingRefFitAnchor& anc : in.anchors) {
        int seg; double ls;
        if (!pToSegment(anc.p, seg, ls))
            continue;
        const ShaftPose pose = m->evaluate(static_cast<Segment>(seg), ls);

        const QPointF bpx = project(in, pose.butt, xs);
        const double du = bpx.x() - anc.gripPx.x(), dv = bpx.y() - anc.gripPx.y();
        sse += weightP(anc.p) * double(anc.conf) * (du * du + dv * dv);

        const QPointF hpx = project(in, pose.clubhead(p.Lc), xs);
        const double hu = hpx.x() - anc.headPx.x(), hv = hpx.y() - anc.headPx.y();
        sse += weightH(anc.p) * double(anc.conf) * (hu * hu + hv * hv);
    }
    return sse;
}

// |C| at the P1 plane — the reach (arm + Lc) the ball anchor needs. Closed-form
// fsp = atan2(hubZ, hypot(hubY, hubX − ballOffsetX)) + dth, then hub projected onto
// the plane normal (mirror-aware, matching the model's planeBasis).
double reachNeeded(const Params& p, const SwingRefFitInput& in)
{
    const double sgn   = in.seedAnthro.rightHanded ? 1.0 : -1.0;
    const double hubY  = double(in.seedAnthro.hub.y());
    const double dx    = p.hubX - in.ballOffsetX;
    const double fsp   = qRadiansToDegrees(std::atan2(p.hubZ, std::hypot(hubY, dx)));
    const double theta = qDegreesToRadians(fsp + p.dth);
    const QVector3D n(0.0f, float(sgn * std::sin(theta)), float(std::cos(theta)));
    const QVector3D hub(float(p.hubX), in.seedAnthro.hub.y(), float(p.hubZ));
    const QVector3D C = hub - float(QVector3D::dotProduct(hub, n)) * n;
    return double(C.length());
}

// Golden-section minimiser of f on [lo,hi], fixed iteration count, midpoint of the
// final bracket. Matches fit_proto.py golden().
template <typename F>
double golden(F&& f, double lo, double hi, int iters = kGoldenIters)
{
    double a = lo, b = hi;
    double c = b - kInvPhi * (b - a);
    double d = a + kInvPhi * (b - a);
    double fc = f(c), fd = f(d);
    for (int i = 0; i < iters; ++i) {
        if (fc < fd) {
            b = d; d = c; fd = fc;
            c = b - kInvPhi * (b - a);
            fc = f(c);
        } else {
            a = c; c = d; fc = fd;
            d = a + kInvPhi * (b - a);
            fd = f(d);
        }
    }
    return (a + b) / 2.0;
}

} // namespace

SwingRefFitResult fitSwingReference(const SwingRefFitInput& in)
{
    SwingRefFitResult out;
    // Seed passthrough (also the degrade return value — seed unchanged).
    out.anthro         = in.seedAnthro;
    out.clubLengthM    = in.seedClub.length;
    out.planeOffsetDeg = in.cfg.backswingPlaneOffsetDeg;

    // Anchors in ascending p (deterministic FP accumulation order); drop out-of-range.
    std::vector<SwingRefFitAnchor> anchors;
    anchors.reserve(in.anchors.size());
    for (const SwingRefFitAnchor& a : in.anchors) {
        int seg; double ls;
        if (pToSegment(a.p, seg, ls))
            anchors.push_back(a);
    }
    std::stable_sort(anchors.begin(), anchors.end(),
                     [](const SwingRefFitAnchor& a, const SwingRefFitAnchor& b) { return a.p < b.p; });

    // Distinct-P count (the acceptance gate).
    int distinctP = 0;
    int lastP = -1;
    for (const SwingRefFitAnchor& a : anchors) {
        if (a.p != lastP) { ++distinctP; lastP = a.p; }
    }
    out.anchorsUsed = distinctP;

    if (distinctP < 4 || !(in.pxPerM > 0.0)) {
        out.fitted = false;   // degrade: seed returned unchanged
        return out;
    }

    // Work on the sorted copy from here.
    SwingRefFitInput work = in;
    work.anchors = anchors;
    const double xs = work.xSign >= 0.0 ? 1.0 : -1.0;   // normalise like OrthographicProjection

    Params p;
    p.hubX = double(in.seedAnthro.hub.x());
    p.hubZ = double(in.seedAnthro.hub.z());
    p.arm  = in.seedAnthro.armLength;
    p.Lc   = in.seedClub.length;
    p.dth  = in.cfg.backswingPlaneOffsetDeg;

    struct Bound { double lo, hi; };
    const Bound bHubX{ p.hubX - 0.35, p.hubX + 0.35 };
    const Bound bHubZ{ p.hubZ - 0.70, p.hubZ + 0.15 };
    const Bound bArm { 0.40, 0.90 };
    const Bound bLc  { in.seedClub.length * 0.70, in.seedClub.length * 1.25 };
    const Bound bDth { -10.0, 14.0 };

    // Σ conf·(Wp+Wh) — the weighted RMS denominator.
    double nW = 0.0;
    for (const SwingRefFitAnchor& a : work.anchors)
        nW += (weightP(a.p) + weightH(a.p)) * double(a.conf);
    const double rms0Sse = objective(p, work, xs);
    out.rmsBeforePx = nW > 0.0 ? std::sqrt(rms0Sse / nW) : -1.0;

    // Lift arm to keep the ball anchor reachable after a hub/plane move.
    auto liftArmIfNeeded = [&](Params& q) {
        const double need = reachNeeded(q, work) - q.Lc + kMargin;
        if (q.arm < need)
            q.arm = std::min(need, bArm.hi);
    };

    const std::array<int, 5> order{ 0, 1, 2, 3, 4 };   // arm, Lc, hubZ, hubX, dth
    for (int sweep = 0; sweep < kSweeps; ++sweep) {
        for (int key : order) {
            double lo = 0.0, hi = 0.0;
            switch (key) {
            case 0: lo = bArm.lo;  hi = bArm.hi;  break;
            case 1: lo = bLc.lo;   hi = bLc.hi;   break;
            case 2: lo = bHubZ.lo; hi = bHubZ.hi; break;
            case 3: lo = bHubX.lo; hi = bHubX.hi; break;
            default: lo = bDth.lo; hi = bDth.hi;  break;
            }
            if (key == 0) {                    // arm: floor arm >= |C| - Lc
                lo = std::max(lo, reachNeeded(p, work) - p.Lc + kMargin);
                lo = std::min(lo, hi);
            } else if (key == 1) {             // Lc: floor Lc >= |C| - arm
                lo = std::max(lo, reachNeeded(p, work) - p.arm + kMargin);
                lo = std::min(lo, hi);
            }

            auto f1 = [&](double x) {
                Params q = p;
                switch (key) {
                case 0: q.arm  = x; break;
                case 1: q.Lc   = x; break;
                case 2: q.hubZ = x; break;
                case 3: q.hubX = x; break;
                default: q.dth = x; break;
                }
                if (key == 2 || key == 3 || key == 4)   // hub/plane moves: keep reach feasible
                    liftArmIfNeeded(q);
                return objective(q, work, xs);
            };

            const double best = golden(f1, lo, hi);
            switch (key) {
            case 0: p.arm  = best; break;
            case 1: p.Lc   = best; break;
            case 2: p.hubZ = best; break;
            case 3: p.hubX = best; break;
            default: p.dth = best; break;
            }
            liftArmIfNeeded(p);
        }
    }

    const double rms1Sse = objective(p, work, xs);
    out.rmsAfterPx = nW > 0.0 ? std::sqrt(rms1Sse / nW) : -1.0;

    out.anthro.hub       = QVector3D(float(p.hubX), in.seedAnthro.hub.y(), float(p.hubZ));
    out.anthro.armLength = p.arm;
    // out.anthro.rightHanded already == seed
    out.clubLengthM      = p.Lc;
    out.planeOffsetDeg   = p.dth;
    out.fitted           = true;
    return out;
}

} // namespace pinpoint::swingref
