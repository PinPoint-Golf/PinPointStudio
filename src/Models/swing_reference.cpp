// swing_reference.cpp — GeometricReferenceModel + anchor solves + interpolation.
//
// Geometry (fixed, from the WP1 exemplar):
//   * planeBasis(θ): the swing plane contains the target line (X) through the ball,
//     inclined θ to the ground and rising toward the golfer (−Y). n = (0,sinθ,cosθ),
//     e2 = up-plane toward the golfer = (0,−cosθ,sinθ), e1 = X.
//   * Pivot C = hub projected onto the instantaneous plane (which passes through the
//     ball/origin): C = hub − (hub·n) n.
//   * Arm: armDir = R(n, sgn·α) · (−e2); hands = C + armLength·armDir.
//   * Shaft: interior arm–shaft angle β via a (180−β) rotation about n:
//     shaftDir = R(n, sgn·(180−β)) · armDir.
//   sgn = +1 RH, −1 LH gives the y→−y mirror.
//
// Anchor solve (closed form, BALL-CONTACT-FIRST): the exemplar's two-unknown (α,β)
// solve is realised as a two-circle IK in the swing plane at inclination θ. With
// C = hub projected onto the plane, find the hand position h that satisfies BOTH
// |h| = L_c (clubhead lands exactly on the ball, since shaftDir = −h/|h| carries the
// clubhead from the hands through the origin) AND |h − C| = armLength (the lead arm
// reaches the hands). Then read α off the arm direction C→h and β off the interior
// arm–shaft angle. Ball contact is now EXACT (not the old arm-length residual), at the
// cost of the shaft no longer sitting at the static lie/lean to the degree the geometry
// forbids — ball contact has priority. Branch selection preserves the original
// conventions (P1: β nearest 143° with the butt on the golfer's −Y side; P7: hands
// forward of the ball) and is mirror-aware so the LH golfer stays an exact y→−y mirror.
// Infeasible reach (d ≥ arm+L_c) or degenerate reach (d ≤ |L_c−arm|) fall back to a
// single clamped candidate on the origin→C line. No Newton iteration is required.

#include "swing_reference.h"

#include "../Analysis/analysis_tuning.h"   // pinpoint::analysis::tuning::apply

#include <algorithm>
#include <array>
#include <cmath>

namespace pinpoint::swingref {

namespace {

// Signed rotation angle (deg) that carries a onto b about axis, right-hand rule.
double signedAngleDeg(const QVector3D& a, const QVector3D& b, const QVector3D& axis)
{
    const QVector3D an = a.normalized(), bn = b.normalized(), ax = axis.normalized();
    const double dotv = QVector3D::dotProduct(an, bn);
    const double det  = QVector3D::dotProduct(QVector3D::crossProduct(an, bn), ax);
    return qRadiansToDegrees(std::atan2(det, dotv));
}

} // namespace

// ---------------------------------------------------------------------------
// MonotoneCubic (Fritsch–Carlson, 1980)
// ---------------------------------------------------------------------------
MonotoneCubic::MonotoneCubic(const Track& keys)
{
    // Sort by s (deterministic; input tables are already ordered but do not assume it).
    Track k = keys;
    std::sort(k.begin(), k.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    const int n = int(k.size());
    m_s.resize(n);
    m_v.resize(n);
    m_m.assign(n, 0.0);
    for (int i = 0; i < n; ++i) { m_s[i] = k[i].first; m_v[i] = k[i].second; }
    if (n < 2) return;

    // Secant slopes.
    std::vector<double> d(n - 1, 0.0);
    for (int i = 0; i < n - 1; ++i) {
        const double h = m_s[i + 1] - m_s[i];
        d[i] = (h != 0.0) ? (m_v[i + 1] - m_v[i]) / h : 0.0;
    }

    // Initial tangents = average of adjacent secants; endpoints = one-sided secant.
    m_m[0] = d[0];
    m_m[n - 1] = d[n - 2];
    for (int i = 1; i < n - 1; ++i) m_m[i] = 0.5 * (d[i - 1] + d[i]);

    // Fritsch–Carlson monotonicity limiter.
    for (int i = 0; i < n - 1; ++i) {
        if (d[i] == 0.0) {                    // flat segment → kill both tangents
            m_m[i] = 0.0;
            m_m[i + 1] = 0.0;
            continue;
        }
        const double a = m_m[i] / d[i];
        const double b = m_m[i + 1] / d[i];
        const double s = a * a + b * b;
        if (s > 9.0) {                        // project back onto the circle of radius 3
            const double t = 3.0 / std::sqrt(s);
            m_m[i]     = t * a * d[i];
            m_m[i + 1] = t * b * d[i];
        }
    }
}

double MonotoneCubic::eval(double s) const
{
    const int n = int(m_s.size());
    if (n == 0) return 0.0;
    if (n == 1) return m_v[0];
    if (s <= m_s.front()) return m_v.front();   // clamp
    if (s >= m_s.back())  return m_v.back();     // clamp

    // Locate interval [i, i+1] containing s.
    int i = int(std::upper_bound(m_s.begin(), m_s.end(), s) - m_s.begin()) - 1;
    i = std::clamp(i, 0, n - 2);

    const double h = m_s[i + 1] - m_s[i];
    const double t = (h != 0.0) ? (s - m_s[i]) / h : 0.0;
    const double t2 = t * t, t3 = t2 * t;

    // Hermite basis.
    const double h00 =  2 * t3 - 3 * t2 + 1;
    const double h10 =      t3 - 2 * t2 + t;
    const double h01 = -2 * t3 + 3 * t2;
    const double h11 =      t3 -     t2;
    return h00 * m_v[i] + h10 * h * m_m[i] + h01 * m_v[i + 1] + h11 * h * m_m[i + 1];
}

// ---------------------------------------------------------------------------
// GeometricReferenceModel
// ---------------------------------------------------------------------------
namespace {

class GeometricReferenceModel final : public SwingReferenceModel {
public:
    GeometricReferenceModel(GolferAnthro a, ClubSpec c, RefConfig cfg)
        : m_a(std::move(a)), m_c(std::move(c)), m_cfg(std::move(cfg))
    {
        solveAnchors();   // overwrites P1 + P7 endpoints in the working tables
        buildTracks();
    }

    double fspInclinationDeg() const override
    {
        const double dx    = double(m_a.hub.x()) - m_c.ballOffsetX;
        const double horiz  = std::hypot(double(m_a.hub.y()), dx);
        return qRadiansToDegrees(std::atan2(double(m_a.hub.z()), horiz));
    }

    ShaftPose evaluate(Segment seg, double s) const override
    {
        const double sgn   = m_a.rightHanded ? 1.0 : -1.0;
        const double theta = planeAngleDeg(seg, s);
        const Basis  b     = planeBasis(theta, sgn);
        const QVector3D C  = m_a.hub - float(QVector3D::dotProduct(m_a.hub, b.n)) * b.n;

        const double alpha = track(seg, TrackId::Alpha).eval(s);
        const QQuaternion Ra = QQuaternion::fromAxisAndAngle(b.n, float(sgn * alpha));
        const QVector3D armDir = Ra.rotatedVector(-b.e2).normalized();
        const QVector3D hands  = C + float(m_a.armLength) * armDir;

        const double beta = track(seg, TrackId::Beta).eval(s);
        const QQuaternion Rs = QQuaternion::fromAxisAndAngle(b.n, float(sgn * (180.0 - beta)));
        const QVector3D shaftDir = Rs.rotatedVector(armDir).normalized();

        return { hands, shaftDir, s, seg };
    }

    std::vector<ShaftPose> sample() const override
    {
        std::vector<ShaftPose> out;
        const int m = std::max(2, m_cfg.samplesPerSegment);
        out.reserve(size_t(3 * m));
        const std::array<Segment, 3> order{ Segment::Backswing, Segment::Downswing,
                                            Segment::FollowThrough };
        for (Segment seg : order) {
            for (int i = 0; i < m; ++i) {
                const double s = double(i) / double(m - 1);
                out.push_back(evaluate(seg, s));
            }
        }
        return out;
    }

private:
    struct Basis { QVector3D e1, e2, n; };
    enum class TrackId { Alpha, Beta };

    // Plane basis, mirror-aware. RH (sgn=+1): n=(0,sinθ,cosθ), e2=(0,−cosθ,sinθ),
    // rising toward the golfer on −Y. LH (sgn=−1) flips the y-components so the whole
    // frame is the y→−y mirror; paired with the sgn·angle rotations in evaluate() this
    // makes a left-handed golfer an exact mirror of the right-handed one (test 5).
    static Basis planeBasis(double thetaDeg, double sgn)
    {
        const double t = qDegreesToRadians(thetaDeg);
        return { QVector3D(1, 0, 0),
                 QVector3D(0, float(-sgn * std::cos(t)), float(std::sin(t))),
                 QVector3D(0, float( sgn * std::sin(t)), float(std::cos(t))) };
    }

    double planeAngleDeg(Segment seg, double s) const
    {
        const double fsp = fspInclinationDeg();
        if (seg == Segment::Backswing) return fsp + m_cfg.backswingPlaneOffsetDeg;
        if (seg == Segment::Downswing) return fsp + m_cfg.backswingPlaneOffsetDeg * m_blendDown.eval(s);
        return fsp;   // FollowThrough on the FSP throughout (Kwon planar region)
    }

    // Read α (or β) that reproduces a target arm / shaft direction under evaluate()'s
    // rotation convention. sgn·α = signed(−e2 → armDir about n); β = 180 − sgn·signed(armDir → shaftDir about n).
    static double alphaFor(const Basis& b, double sgn, const QVector3D& armDir)
    {
        return sgn * signedAngleDeg(-b.e2, armDir, b.n);
    }
    static double betaFor(const Basis& b, double sgn, const QVector3D& armDir, const QVector3D& shaftDir)
    {
        return 180.0 - sgn * signedAngleDeg(armDir, shaftDir, b.n);
    }

    // Two-circle IK in the plane at inclination `theta`: return the in-plane hand
    // candidate(s) h (WORLD coords) satisfying |h| = L_c (clubhead on the ball) and
    // |h − C| = armLength (arm reaches the hands), C = hub projected onto the plane.
    // `Cout` returns C. Feasible reach → the two circle-circle intersections;
    // infeasible (d ≥ arm+L_c) or degenerate (d ≤ |L_c−arm|) reach → one clamped
    // candidate on the origin→C line; d < 1e-9 → h = −L_c·e2. Ports ref_model.py
    // `_ik_hands`. (In-plane math only — no rotation representation involved, so the
    // quaternions-only rule is untouched; the candidates feed alphaFor/betaFor which
    // read α/β for evaluate()'s quaternion reconstruction.)
    std::vector<QVector3D> ikHands(double theta, double sgn, QVector3D& Cout) const
    {
        const Basis b = planeBasis(theta, sgn);
        const QVector3D C = m_a.hub - float(QVector3D::dotProduct(m_a.hub, b.n)) * b.n;
        Cout = C;
        const double c1 = QVector3D::dotProduct(C, b.e1);
        const double c2 = QVector3D::dotProduct(C, b.e2);
        const double d  = std::hypot(c1, c2);
        const double arm = m_a.armLength, Lc = m_c.length;
        auto planePt = [&](double h1, double h2) {
            return float(h1) * b.e1 + float(h2) * b.e2;
        };
        if (d < 1e-9)
            return { planePt(0.0, -Lc) };                        // h = −L_c·e2
        const double ux = c1 / d, uy = c2 / d;
        if (d >= arm + Lc) {                                     // too far: stretch toward origin
            const double t = (d - arm) / d;
            return { planePt(c1 * t, c2 * t) };
        }
        if (d <= std::abs(Lc - arm)) {                          // degenerate: clamp
            const double r = (Lc > arm) ? Lc : std::max(d - arm, 1e-6);
            return { planePt(ux * r, uy * r) };
        }
        // Feasible: intersect the circles |h| = L_c and |h − C| = arm.
        const double a  = (Lc * Lc - arm * arm + d * d) / (2.0 * d);   // along origin→C
        const double k  = std::sqrt(std::max(Lc * Lc - a * a, 0.0));
        const double px = ux * a, py = uy * a;
        return { planePt(px - uy * k, py + ux * k),
                 planePt(px + uy * k, py - ux * k) };
    }

    // Ball-contact-first anchor solve: place the clubhead EXACTLY on the ball at both
    // ends via the two-circle IK, then read (α,β) off the chosen hand candidate. Ball
    // contact has priority over the static lie/lean (see the file-header note).
    void solveAnchors()
    {
        const double sgn = m_a.rightHanded ? 1.0 : -1.0;

        // ---- P1 (Backswing s=0): clubhead on ball; prefer β≈143 & butt on −Y -------
        {
            const double theta = fspInclinationDeg() + m_cfg.backswingPlaneOffsetDeg;
            const Basis b = planeBasis(theta, sgn);
            QVector3D C;
            const std::vector<QVector3D> cands = ikHands(theta, sgn, C);
            double bestAlpha = 0, bestBeta = 143, bestErr = 1e18;
            for (const QVector3D& h : cands) {
                const float hl = h.length();
                const QVector3D shaftDir = hl > 1e-9f ? (-h / hl) : (-b.e2);
                const QVector3D armDir   = (h - C).normalized();
                const double al = alphaFor(b, sgn, armDir);
                const double be = betaFor(b, sgn, armDir, shaftDir);
                // Prefer the branch with β nearest the tabulated P1 default (≈143°) and
                // the butt on the golfer's −Y (hub) side. Mirror-aware: sgn·h.y < 0 is
                // the hub side for BOTH handednesses (RH sgn=+1 reduces to the old
                // hands.y() < 0 test); the T5 LH-mirror test requires this sgn factor.
                const double penalty = std::abs(be - 143.0)
                                     + (sgn * double(h.y()) < 0.0 ? 0.0 : 90.0);
                if (penalty < bestErr) { bestErr = penalty; bestAlpha = al; bestBeta = be; }
            }
            m_alphaP1 = bestAlpha;
            m_betaP1  = bestBeta;
        }

        // ---- P7 (Downswing s=1 == FollowThrough s=0): clubhead on ball; hands fwd ---
        {
            const double theta = fspInclinationDeg();           // on the FSP
            const Basis b = planeBasis(theta, sgn);
            QVector3D C;
            const std::vector<QVector3D> cands = ikHands(theta, sgn, C);
            double bestAlpha = 0, bestBeta = 168, bestScore = -1e18;
            for (const QVector3D& h : cands) {
                const float hl = h.length();
                const QVector3D shaftDir = hl > 1e-9f ? (-h / hl) : (-b.e2);
                const QVector3D armDir   = (h - C).normalized();
                const double al = alphaFor(b, sgn, armDir);
                const double be = betaFor(b, sgn, armDir, shaftDir);
                // Prefer hands forward of the ball (world +X) — forward lean at impact.
                // h.x is mirror-invariant, so the branch choice is handedness-agnostic.
                const double score = double(h.x());
                if (score > bestScore) { bestScore = score; bestAlpha = al; bestBeta = be; }
            }
            m_alphaP7 = bestAlpha;
            m_betaP7  = bestBeta;
        }
    }

    void buildTracks()
    {
        // Working copies with anchored endpoints written in.
        Track ab = m_cfg.alphaBack, bb = m_cfg.betaBack;
        Track ad = m_cfg.alphaDown, bd = m_cfg.betaDown;
        Track af = m_cfg.alphaFollow, bf = m_cfg.betaFollow;

        ab.front().second = m_alphaP1;  bb.front().second = m_betaP1;   // P1
        ad.back().second  = m_alphaP7;  bd.back().second  = m_betaP7;   // P7 (down end)
        af.front().second = m_alphaP7;  bf.front().second = m_betaP7;   // P7 (follow start) — shared join

        // C¹ across the P7 join (test 7): the pose (α,β,plane) is identical on both
        // sides, so the clubhead velocity direction matches iff the (α',β') parameter
        // tangents match. The downswing's final interval already carries the tangent
        // from the last authored interior knot (P6) up to the anchored P7 value; we
        // reproduce that same one-sided slope at the start of the follow-through by
        // inserting a short collinear knot, so Fritsch–Carlson yields the identical
        // endpoint tangent there. (Plane rate is already zero on both sides: Δθ_bs is
        // fully blended out by s≥0.75 of the downswing and the follow-through is on the
        // FSP throughout.)
        const double sJoin = 0.10;
        auto joinSlope = [](const Track& down) {
            const auto& kEnd  = down.back();
            const auto& kPrev = down[down.size() - 2];
            const double ds = kEnd.first - kPrev.first;
            return ds != 0.0 ? (kEnd.second - kPrev.second) / ds : 0.0;
        };
        const double mA = joinSlope(ad);
        const double mB = joinSlope(bd);
        af.insert(af.begin() + 1, { sJoin, m_alphaP7 + mA * sJoin });
        bf.insert(bf.begin() + 1, { sJoin, m_betaP7  + mB * sJoin });

        m_alphaBack = MonotoneCubic(ab);  m_betaBack = MonotoneCubic(bb);
        m_alphaDown = MonotoneCubic(ad);  m_betaDown = MonotoneCubic(bd);
        m_alphaFollow = MonotoneCubic(af); m_betaFollow = MonotoneCubic(bf);
        m_blendDown = MonotoneCubic(m_cfg.thetaBlendDown);
    }

    const MonotoneCubic& track(Segment seg, TrackId id) const
    {
        switch (seg) {
        case Segment::Backswing:     return id == TrackId::Alpha ? m_alphaBack   : m_betaBack;
        case Segment::Downswing:     return id == TrackId::Alpha ? m_alphaDown   : m_betaDown;
        case Segment::FollowThrough: return id == TrackId::Alpha ? m_alphaFollow : m_betaFollow;
        }
        return m_alphaBack;
    }

    GolferAnthro m_a; ClubSpec m_c; RefConfig m_cfg;
    double m_alphaP1 = 0, m_betaP1 = 143, m_alphaP7 = 0, m_betaP7 = 168;
    MonotoneCubic m_alphaBack, m_betaBack, m_alphaDown, m_betaDown, m_alphaFollow, m_betaFollow;
    MonotoneCubic m_blendDown{ Track{ {0, 1}, {1, 0} } };
};

} // namespace

// ---------------------------------------------------------------------------
// Free functions
// ---------------------------------------------------------------------------
LieLean lieLeanDefaultsFor(const QString& clubName)
{
    // Endpoints: driver ≈56°/0°, wedge ≈64°/8°. Irons interpolate on a nominal
    // number index (driver 0 … PW 9, wedges beyond). Deterministic string parse.
    const QString n = clubName.trimmed().toLower();

    auto lerp = [](double a, double b, double t) { return a + (b - a) * std::clamp(t, 0.0, 1.0); };

    // Named clubs.
    if (n.contains("driver") || n == "1w" || n == "dr")   return { 56.0, 0.0 };
    if (n.contains("wedge") || n.endsWith("w") || n.contains("sw")
        || n.contains("pw") || n.contains("gw") || n.contains("lw")) {
        // Wedges cluster near the steep/high-lean end.
        return { 64.0, 8.0 };
    }
    if (n.contains("hybrid") || n.endsWith("h"))          return { 58.0, 2.0 };
    if (n.contains("wood"))                                return { 57.0, 1.0 };

    // Numbered irons "3i".."9i" or bare "7" → interpolate between 3-iron and PW.
    // 3-iron ≈ 60.5°/4°, 9-iron ≈ 63.5°/7.5°.
    QString digits;
    for (QChar ch : n) if (ch.isDigit()) digits.append(ch);
    bool ok = false;
    const int num = digits.toInt(&ok);
    if (ok && num >= 1 && num <= 9) {
        const double t = std::clamp((num - 3) / 6.0, 0.0, 1.0);   // 3→0, 9→1
        return { lerp(60.5, 63.5, t), lerp(4.0, 7.5, t) };
    }

    // Fallback: a mid-iron.
    return { 62.5, 5.0 };
}

std::unique_ptr<SwingReferenceModel>
makeSwingReferenceModel(GolferAnthro anthro, ClubSpec club, RefConfig cfg)
{
    return std::make_unique<GeometricReferenceModel>(std::move(anthro), std::move(club), std::move(cfg));
}

RefConfig RefConfig::fromOverrides(const QVariantMap &ov)
{
    RefConfig c;   // struct defaults are already seeded from tuned::swingref::
    pinpoint::analysis::tuning::apply(ov, "swingref.enabled",             c.enabled);
    pinpoint::analysis::tuning::apply(ov, "swingref.fit.enabled",         c.fitEnabled);
    pinpoint::analysis::tuning::apply(ov, "swingref.planeOffsetDeg",      c.backswingPlaneOffsetDeg);
    pinpoint::analysis::tuning::apply(ov, "swingref.samplesPerSegment",   c.samplesPerSegment);
    pinpoint::analysis::tuning::apply(ov, "swingref.referenceTempoRatio", c.referenceTempoRatio);
    pinpoint::analysis::tuning::apply(ov, "swingref.proj.nominalFovDeg",  c.nominalFovDeg);
    pinpoint::analysis::tuning::apply(ov, "swingref.proj.residualWarnPx", c.residualWarnPx);
    return c;
}

} // namespace pinpoint::swingref
