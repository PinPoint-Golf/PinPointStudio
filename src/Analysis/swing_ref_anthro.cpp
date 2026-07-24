// swing_ref_anthro.cpp — GolferAnthro-from-2D estimation (WP4a, plan §3).
//
// See swing_ref_anthro.h for the full geometry-assumption contract (A1–A6).

#include "swing_ref_anthro.h"
#include "swing_analysis.h"
#include "analysis_tuning.h"   // pinpoint::analysis::tuning::apply

#include <QPointF>
#include <QVector3D>

#include <algorithm>
#include <array>
#include <cmath>

namespace pinpoint::swingref {

namespace {

// COCO body keypoint indices (PoseFrame2D.kp / .conf).
constexpr int kLShoulder = 5, kRShoulder = 6;
constexpr int kLAnkle    = 15, kRAnkle    = 16;

bool isSet(double v) { return !std::isnan(v); }

// Conf-gated mean of one keypoint over the frames whose t_us falls in [t0,t1].
// Returns false when no frame passed the confidence gate for this keypoint.
struct KpMean { QPointF norm; float conf = 0.f; bool ok = false; };

KpMean meanKp(const std::vector<pinpoint::analysis::PoseFrame2D>& fr,
              int kpIdx, qint64 t0, qint64 t1, double confMin)
{
    KpMean out;
    // lower_bound on the ascending t_us grid, then walk the window.
    auto it = std::lower_bound(fr.begin(), fr.end(), t0,
                               [](const pinpoint::analysis::PoseFrame2D& f, qint64 t) { return f.t_us < t; });
    double sx = 0.0, sy = 0.0, sc = 0.0;
    int n = 0;
    for (; it != fr.end() && it->t_us <= t1; ++it) {
        if (it->conf[std::size_t(kpIdx)] < float(confMin))
            continue;
        sx += it->kp[std::size_t(kpIdx)].x();
        sy += it->kp[std::size_t(kpIdx)].y();
        sc += it->conf[std::size_t(kpIdx)];
        ++n;
    }
    if (n == 0)
        return out;
    out.norm = QPointF(sx / n, sy / n);
    out.conf = float(sc / n);
    out.ok = true;
    return out;
}

// Conf-gated mean of two keypoints (e.g. mid-shoulder). ok only when BOTH sides
// produced a value.
KpMean meanMidKp(const std::vector<pinpoint::analysis::PoseFrame2D>& fr,
                 int a, int b, qint64 t0, qint64 t1, double confMin)
{
    const KpMean ma = meanKp(fr, a, t0, t1, confMin);
    const KpMean mb = meanKp(fr, b, t0, t1, confMin);
    KpMean out;
    if (!ma.ok || !mb.ok)
        return out;
    out.norm = QPointF(0.5 * (ma.norm.x() + mb.norm.x()),
                       0.5 * (ma.norm.y() + mb.norm.y()));
    out.conf = 0.5f * (ma.conf + mb.conf);
    out.ok = true;
    return out;
}

// Mean found-ball centre over the window; falls back to the nearest found sample
// when the window itself is empty.
struct BallMean { QPointF norm; bool ok = false; };

BallMean meanBall(const std::vector<pinpoint::analysis::BallSample2D>& fr, qint64 tA, qint64 t0, qint64 t1)
{
    double sx = 0.0, sy = 0.0;
    int n = 0;
    for (const auto& s : fr) {
        if (!s.found) continue;
        if (s.t_us >= t0 && s.t_us <= t1) {
            sx += s.center.x(); sy += s.center.y(); ++n;
        }
    }
    BallMean out;
    if (n > 0) {
        out.norm = QPointF(sx / n, sy / n);
        out.ok = true;
        return out;
    }
    // Nearest found sample overall.
    const pinpoint::analysis::BallSample2D* best = nullptr;
    qint64 bestDt = 0;
    for (const auto& s : fr) {
        if (!s.found) continue;
        const qint64 dt = std::llabs(s.t_us - tA);
        if (!best || dt < bestDt) { best = &s; bestDt = dt; }
    }
    if (best) { out.norm = best->center; out.ok = true; }
    return out;
}

} // namespace

std::optional<AnthroEstimate>
estimateGolferAnthro(const pinpoint::analysis::PoseTrack2D& pose,
                    const pinpoint::analysis::ShaftTrack2D& shaft,
                    const pinpoint::analysis::BallTrack2D& ball,
                    const pinpoint::analysis::Segmentation& seg,
                    double clubLengthM,
                    double lieDeg,
                    int handedness,
                    const AnthroConfig& cfg)
{
    const bool rightHanded = handedness > 0;
    const double depthSign = rightHanded ? -1.0 : 1.0;   // RH body sits at y<0

    const bool manualHub = isSet(cfg.hubX) && isSet(cfg.hubY) && isSet(cfg.hubZ);
    const bool manualArm = isSet(cfg.armLengthM);
    const bool fullyManual = manualHub && manualArm;

    auto manualFallback = [&](float conf) -> std::optional<AnthroEstimate> {
        AnthroEstimate e;
        e.anthro.rightHanded = rightHanded;
        e.anthro.hub = QVector3D(float(cfg.hubX), float(cfg.hubY), float(cfg.hubZ));
        e.anthro.armLength = cfg.armLengthM;
        e.pxPerM = 0.0;
        e.conf = conf;
        e.flags = AnthroFlagManualHub | AnthroFlagManualArm;
        return e;
    };

    // ── Address window ──────────────────────────────────────────────────────
    const pinpoint::analysis::PhaseEvent* addr = seg.eventFor(pinpoint::analysis::Phase::Address);
    if (!addr)
        return fullyManual ? manualFallback(0.5f) : std::nullopt;
    const qint64 tA = addr->t_us;
    const qint64 half = cfg.addrWindowUs / 2;
    const qint64 t0 = tA - half, t1 = tA + half;

    const int W = shaft.frameWidth, H = shaft.frameHeight;
    if (W <= 0 || H <= 0)
        return fullyManual ? manualFallback(0.5f) : std::nullopt;

    const std::vector<pinpoint::analysis::PoseFrame2D>& frames =
        !pose.smoothed.empty() ? pose.smoothed : pose.frames;
    if (frames.empty())
        return fullyManual ? manualFallback(0.5f) : std::nullopt;

    quint32 flags = AnthroFlagNone;

    // ── Required observables ────────────────────────────────────────────────
    // P1 shaft grip + head (image px, already in image space). The head is the
    // measured clubhead-at-address, which — A3: clubhead sits AT the ball —
    // gives the ball's image position and the club's projected image length far
    // more reliably than the ball detector (a spurious static blob otherwise
    // sets both scale and origin catastrophically wrong).
    const pinpoint::analysis::ShaftPosition* p1 = nullptr;
    for (const auto& p : shaft.positions)
        if (p.p == 1) { p1 = &p; break; }
    if (!p1)
        return fullyManual ? manualFallback(0.5f) : std::nullopt;
    const QPointF gripPx = p1->gripPx;
    const QPointF headPx = p1->headPx;
    const float   p1Conf = p1->conf;
    const double  gripHeadPx = std::hypot(headPx.x() - gripPx.x(), headPx.y() - gripPx.y());
    const bool    headValid  = gripHeadPx > 1.0;

    // Detected ball centre (image px) — now OPTIONAL: only the origin fallback
    // when there is no P1 head, plus a spurious-detection cross-check.
    const BallMean bm = meanBall(ball.frames, tA, t0, t1);
    const bool     ballOk = bm.ok;
    const QPointF  ballPx = ballOk ? QPointF(bm.norm.x() * W, bm.norm.y() * H) : QPointF();

    if (!headValid && !ballOk)
        return fullyManual ? manualFallback(0.5f) : std::nullopt;

    // Mid-shoulder (hub projection).
    const KpMean shoulder = meanMidKp(frames, kLShoulder, kRShoulder, t0, t1, cfg.kpConfMin);
    if (!shoulder.ok) {
        if (!manualHub)
            return std::nullopt;   // no hub evidence and no override → refuse
        flags |= AnthroFlagLowShoulder;
    }

    // ── Club image length (px) — prefer the pipeline's own club measurement ──
    // Order: fused club-length estimate (confident) → address-window measured
    // club px → raw grip→head span. This is the "prefer measured over labelled"
    // rule: the club LENGTH label is only a world-scale gauge (see header A4),
    // so the image length must come from measurement, never the label.
    constexpr double kFusedClubConfMin = 0.5;   // trust the fused px only when solidly supported
    double clubImgLenPx = 0.0;
    if (headValid) {
        clubImgLenPx = gripHeadPx;                                  // default: raw grip→head span
        if (shaft.lengths.fusedPx > 0.0 && shaft.lengths.fusedConf >= kFusedClubConfMin) {
            clubImgLenPx = shaft.lengths.fusedPx;
            flags |= AnthroFlagFusedClubLen;
        } else if (shaft.measuredClubLenPx > 0.f) {
            clubImgLenPx = double(shaft.measuredClubLenPx);
            flags |= AnthroFlagFusedClubLen;
        }
    }

    // ── Origin (ball/clubhead-at-address) image reference ────────────────────
    // Primary: along the measured P1 head direction at the club image length.
    // Fallback: the detected ball centre (flagged) when there is no P1 head.
    QPointF originPx;
    if (headValid) {
        const double ux = (headPx.x() - gripPx.x()) / gripHeadPx;
        const double uy = (headPx.y() - gripPx.y()) / gripHeadPx;
        originPx = QPointF(gripPx.x() + clubImgLenPx * ux, gripPx.y() + clubImgLenPx * uy);
        // Cross-check the detected ball against the measured clubhead: a ball
        // far from where the club actually points at address is spurious.
        if (ballOk && std::hypot(ballPx.x() - originPx.x(), ballPx.y() - originPx.y())
                          > 0.5 * clubImgLenPx)
            flags |= AnthroFlagBallInconsistent;
    } else {
        originPx     = ballPx;
        clubImgLenPx = std::hypot(gripPx.x() - ballPx.x(), gripPx.y() - ballPx.y());
        flags |= AnthroFlagOriginFromBall;
    }
    if (clubImgLenPx < 1e-6)
        return fullyManual ? manualFallback(0.5f) : std::nullopt;

    // ── Scale + shaft-depth fixed point (A3/A4) ─────────────────────────────
    // `originPx` is the ball/clubhead image position; the grip→origin image
    // vector IS the projected club, and |grip→origin| == clubImgLenPx.
    const double Lc = clubLengthM;
    const double lieRad = lieDeg * M_PI / 180.0;
    double yb = depthSign * Lc * std::cos(lieRad);      // seed
    double pxPerM = clubImgLenPx / std::max(Lc * std::sin(lieRad), 1e-6);
    bool depthSeedKept = true;
    for (int it = 0; it < 5; ++it) {
        const double inPlane = std::sqrt(std::max(Lc * Lc - yb * yb, 1e-9));
        pxPerM = clubImgLenPx / inPlane;
        const double xb = (gripPx.x() - originPx.x()) / pxPerM;    // world X (m)
        const double zb = (originPx.y() - gripPx.y()) / pxPerM;    // world Z (m), image y down
        const double d2 = Lc * Lc - xb * xb - zb * zb;
        const double ybNew = depthSign * std::sqrt(std::max(d2, 0.0));
        if (std::abs(ybNew - yb) < 1e-6) { yb = ybNew; break; }
        depthSeedKept = false;
        yb = ybNew;
    }
    if (depthSeedKept)
        flags |= AnthroFlagDepthSeed;

    // ── Butt / hub 3D (A5/A6) ───────────────────────────────────────────────
    const QVector3D butt3D(
        float((gripPx.x() - originPx.x()) / pxPerM),
        float(yb),
        float((originPx.y() - gripPx.y()) / pxPerM));

    QVector3D hub;
    if (manualHub) {
        hub = QVector3D(float(cfg.hubX), float(cfg.hubY), float(cfg.hubZ));
        flags |= AnthroFlagManualHub;
    } else {
        const QPointF shPx(shoulder.norm.x() * W, shoulder.norm.y() * H);
        hub = QVector3D(
            float((shPx.x() - originPx.x()) / pxPerM),
            float(yb + depthSign * cfg.hubDepthOffsetM),
            float((originPx.y() - shPx.y()) / pxPerM));
    }

    double armLength;
    if (manualArm) {
        armLength = cfg.armLengthM;
        flags |= AnthroFlagManualArm;
    } else {
        armLength = double((hub - butt3D).length()) + cfg.gripOffsetM;
    }

    // ── Ball offset from stance centre ──────────────────────────────────────
    double ballOffsetX = 0.0;
    const KpMean ankle = meanMidKp(frames, kLAnkle, kRAnkle, t0, t1, cfg.kpConfMin);
    if (ankle.ok) {
        const double ankleMidX = ankle.norm.x() * W;
        ballOffsetX = (originPx.x() - ankleMidX) / pxPerM;
    } else {
        flags |= AnthroFlagNoAnkles;
    }

    // ── Aggregate confidence ────────────────────────────────────────────────
    float conf;
    if (fullyManual) {
        conf = 0.9f;
    } else {
        const float shConf = shoulder.ok ? shoulder.conf : 0.3f;
        conf = std::clamp(std::min(shConf, p1Conf > 0.f ? p1Conf : shConf), 0.f, 1.f);
    }

    AnthroEstimate e;
    e.anthro.hub = hub;
    e.anthro.armLength = armLength;
    e.anthro.rightHanded = rightHanded;
    e.ballOffsetX = ballOffsetX;
    e.pxPerM = pxPerM;
    e.originPx = originPx;
    e.conf = conf;
    e.flags = flags;
    return e;
}

AnthroConfig AnthroConfig::fromOverrides(const QVariantMap &ov)
{
    AnthroConfig c;   // struct defaults are already seeded from tuned::swingref::
    pinpoint::analysis::tuning::apply(ov, "swingref.anthro.addrWindowUs",    c.addrWindowUs);
    pinpoint::analysis::tuning::apply(ov, "swingref.anthro.gripOffsetM",     c.gripOffsetM);
    pinpoint::analysis::tuning::apply(ov, "swingref.anthro.hubDepthOffsetM", c.hubDepthOffsetM);
    pinpoint::analysis::tuning::apply(ov, "swingref.anthro.kpConfMin",       c.kpConfMin);
    pinpoint::analysis::tuning::apply(ov, "swingref.anthro.hubX",           c.hubX);
    pinpoint::analysis::tuning::apply(ov, "swingref.anthro.hubY",           c.hubY);
    pinpoint::analysis::tuning::apply(ov, "swingref.anthro.hubZ",           c.hubZ);
    pinpoint::analysis::tuning::apply(ov, "swingref.anthro.armLengthM",     c.armLengthM);
    return c;
}

} // namespace pinpoint::swingref
