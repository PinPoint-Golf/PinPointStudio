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

#include "kinematic_series.h"

#include "det_rng.h"           // deterministic Monte Carlo (uncertainty design principle 7)
#include "shaft_sigma.h"       // σ helpers
#include "shaft_synthesis.h"   // synthDraws

#include <QPointF>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace pinpoint::analysis {

namespace {

constexpr double kPi      = 3.14159265358979323846;
constexpr double kMps2Mph = 2.2369362920544;   // 1 m/s in mph

// COCO body keypoint indices (PoseJoint) for the lead forearm.
constexpr int kLeftElbow = 7, kRightElbow = 8, kLeftWrist = 9, kRightWrist = 10;

// A face-on golf frame spans roughly this vertically — the last-ditch px→metre scale
// when no measured club length is available (keeps mph plausible, never claimed exact).
constexpr double kNominalFrameHeightM = 2.5;
constexpr double kNominalMPerPx       = 1.0 / 900.0;

// Wrap an angle into (−π, π].
double wrapPi(double a)
{
    a = std::fmod(a + kPi, 2.0 * kPi);
    if (a < 0) a += 2.0 * kPi;
    return a - kPi;
}

// Symmetric moving average, window = 2*half+1, edge-clamped.
std::vector<double> movAvg(const std::vector<double> &v, int half)
{
    const int n = int(v.size());
    if (n == 0 || half <= 0) return v;
    std::vector<double> out(size_t(n), 0.0);
    for (int i = 0; i < n; ++i) {
        double sum = 0.0;
        int cnt = 0;
        for (int k = -half; k <= half; ++k) {
            const int j = std::clamp(i + k, 0, n - 1);
            sum += v[size_t(j)];
            ++cnt;
        }
        out[size_t(i)] = sum / cnt;
    }
    return out;
}

int nearestIndex(const std::vector<int64_t> &grid, int64_t t)
{
    if (grid.empty()) return -1;
    auto it = std::lower_bound(grid.begin(), grid.end(), t);
    if (it == grid.begin()) return 0;
    if (it == grid.end())   return int(grid.size()) - 1;
    const int hi = int(it - grid.begin());
    const int lo = hi - 1;
    return (t - grid[size_t(lo)] <= grid[size_t(hi)] - t) ? lo : hi;
}

// Set Address/Top/Impact phase dots on a curve from the phase timeline; when the
// timeline is empty, a lone Impact dot at impactUs.
// A masked sample is never the site of a phase dot (MetricSeries::valid): the dot goes to
// the nearest VALID sample, which for a series masked past its P7 knot is the last sample
// of the club arriving — the number the launch-monitor comparison reads.
void addPhaseDots(MetricSeries &m, const std::vector<PhaseEvent> &phases, int64_t impactUs)
{
    if (m.t_us.empty()) return;
    const auto dot = [&](Phase p, int64_t t) {
        int i = nearestIndex(m.t_us, t);
        if (i < 0) return;
        if (!m.valid.empty() && m.valid[size_t(i)] == 0u) {
            int best = -1; int64_t bd = std::numeric_limits<int64_t>::max();
            for (int j = 0; j < int(m.t_us.size()); ++j) {
                if (m.valid[size_t(j)] == 0u) continue;
                const int64_t d = std::llabs(m.t_us[size_t(j)] - t);
                if (d < bd) { bd = d; best = j; }
            }
            if (best < 0) return;
            i = best;
        }
        m.phaseSamples.push_back({ p, m.t_us[size_t(i)], m.value[size_t(i)], QString() });
    };
    bool any = false;
    for (const Phase p : { Phase::Address, Phase::Top, Phase::Impact }) {
        for (const PhaseEvent &e : phases)
            if (e.phase == p) { dot(p, e.t_us); any = true; break; }
    }
    if (!any && impactUs >= 0) dot(Phase::Impact, impactUs);
}

// px → metre scale from the club-length fusion, falling back to the median visible
// shaft length, then a nominal frame scale. clubLenPx is a grip→head length in px, so
// clubLengthM / clubLenPx maps px → metres.
double resolveMetrePerPx(const ShaftTrack2D &shaft, const std::vector<ShaftSample2D> &track,
                         double clubLengthM)
{
    double lenPx = -1.0;
    if (shaft.lengths.fusedPx > 0.0)        lenPx = shaft.lengths.fusedPx;
    else if (shaft.measuredClubLenPx > 0.0) lenPx = shaft.measuredClubLenPx;
    else {
        std::vector<double> lens;
        lens.reserve(track.size());
        for (const ShaftSample2D &s : track)
            if (s.visibleLenPx > 0.0) lens.push_back(s.visibleLenPx);
        if (!lens.empty()) {
            std::nth_element(lens.begin(), lens.begin() + lens.size() / 2, lens.end());
            lenPx = lens[lens.size() / 2];
        }
    }
    if (lenPx > 0.0 && clubLengthM > 0.0) return clubLengthM / lenPx;
    if (shaft.frameHeight > 0)            return kNominalFrameHeightM / shaft.frameHeight;
    return kNominalMPerPx;
}

// Linear speed (mph) along a smoothed px track sampled on `t` (ascending µs).
std::vector<double> speedMph(const std::vector<int64_t> &t, const std::vector<double> &px,
                             const std::vector<double> &py, double mPerPx)
{
    const int n = int(t.size());
    std::vector<double> sp(size_t(std::max(n, 0)), 0.0);
    if (n < 2) return sp;
    // Smooth positions before differentiating (differentiation amplifies jitter).
    const std::vector<double> sx = movAvg(px, 2), sy = movAvg(py, 2);
    for (int i = 0; i < n; ++i) {
        const int a = std::max(i - 1, 0), b = std::min(i + 1, n - 1);
        const double dt = double(t[size_t(b)] - t[size_t(a)]) / 1e6;   // s
        if (dt <= 0.0) { sp[size_t(i)] = (i > 0) ? sp[size_t(i - 1)] : 0.0; continue; }
        const double dx = sx[size_t(b)] - sx[size_t(a)];
        const double dy = sy[size_t(b)] - sy[size_t(a)];
        sp[size_t(i)] = (std::sqrt(dx * dx + dy * dy) / dt) * mPerPx * kMps2Mph;
    }
    return movAvg(sp, 1);   // light 3-tap smooth of the speed itself
}

// The grip's velocity from the pose wrists (KinematicSeriesInputs::wristVelocity). The grip anchor
// sits on the club below the hands, so it is carried rigidly from the wrists' midpoint W: grip =
// W + d·û + p·n̂ with û = (cos θ, sin θ) along the shaft and n̂ = (−sin θ, cos θ) across it, d and p
// the medians of the anchor's offset from W over the track. Then v_grip = v_W + θ̇·(d·n̂ − p·û):
// the wrists carry the translation, the shaft's own rate the rest. v_W is the central difference
// of the smoothed wrists over ±one pose frame; vx is NaN on a tick the wrists do not bracket, which
// keeps the anchor difference there.
struct WristGrip {
    std::vector<double> vx, vy;   // v_W per track tick (px/s); NaN = unavailable
    double d = 0.0, p = 0.0;      // anchor offset from W along / across the shaft (px)
    bool   ok = false;
};

WristGrip wristGrip(const PoseTrack2D &pose, const std::vector<ShaftSample2D> &track, int frameW, int frameH)
{
    WristGrip g;
    const std::vector<PoseFrame2D> &F = pose.smoothed.empty() ? pose.frames : pose.smoothed;
    if (F.size() < 3 || track.empty() || frameW <= 0 || frameH <= 0) return g;
    constexpr float kConf = 0.15f;
    std::vector<int64_t> t;
    std::vector<double>  x, y;
    for (const PoseFrame2D &f : F) {
        if (f.conf[size_t(kLeftWrist)] < kConf || f.conf[size_t(kRightWrist)] < kConf) continue;
        t.push_back(f.t_us);
        x.push_back(0.5 * (f.kp[size_t(kLeftWrist)].x() + f.kp[size_t(kRightWrist)].x()) * frameW);
        y.push_back(0.5 * (f.kp[size_t(kLeftWrist)].y() + f.kp[size_t(kRightWrist)].y()) * frameH);
    }
    if (t.size() < 3) return g;
    std::vector<int64_t> dt;
    for (size_t i = 1; i < t.size(); ++i) if (t[i] > t[i - 1]) dt.push_back(t[i] - t[i - 1]);
    if (dt.empty()) return g;
    std::nth_element(dt.begin(), dt.begin() + dt.size() / 2, dt.end());
    const int64_t h = dt[dt.size() / 2], maxGap = 3 * h;
    // W at time `at`, linear between the bracketing frames; false outside them or across a gap.
    const auto at = [&](int64_t tt, double &ox, double &oy) {
        const auto it = std::lower_bound(t.begin(), t.end(), tt);
        if (it == t.end()) return false;
        const size_t b = size_t(it - t.begin());
        if (t[b] == tt) { ox = x[b]; oy = y[b]; return true; }
        if (b == 0 || t[b] - t[b - 1] > maxGap) return false;
        const double u = double(tt - t[b - 1]) / double(t[b] - t[b - 1]);
        ox = x[b - 1] + u * (x[b] - x[b - 1]);
        oy = y[b - 1] + u * (y[b] - y[b - 1]);
        return true;
    };
    const double nan = std::numeric_limits<double>::quiet_NaN();
    g.vx.assign(track.size(), nan);
    g.vy.assign(track.size(), nan);
    std::vector<double> ds, ps;
    int have = 0;
    for (size_t i = 0; i < track.size(); ++i) {
        const ShaftSample2D &s = track[i];
        double ax, ay, bx, by, wx, wy;
        if (at(s.t_us - h, ax, ay) && at(s.t_us + h, bx, by)) {
            g.vx[i] = (bx - ax) / (2.0 * double(h) * 1e-6);
            g.vy[i] = (by - ay) / (2.0 * double(h) * 1e-6);
            ++have;
        }
        if (at(s.t_us, wx, wy)) {
            const double rx = s.gripPx.x() - wx, ry = s.gripPx.y() - wy;
            const double c = std::cos(s.thetaRad), sn = std::sin(s.thetaRad);
            ds.push_back(rx * c + ry * sn);
            ps.push_back(-rx * sn + ry * c);
        }
    }
    if (have == 0 || ds.empty()) return g;
    std::nth_element(ds.begin(), ds.begin() + ds.size() / 2, ds.end());
    std::nth_element(ps.begin(), ps.begin() + ps.size() / 2, ps.end());
    g.d  = ds[ds.size() / 2];
    g.p  = ps[ps.size() / 2];
    g.ok = true;
    return g;
}

// The grip velocity (px/s) of tick i: off the wrists where they bracket it, else the ±1-sample
// central difference of the grip anchor.
QPointF gripVelocity(const std::vector<ShaftSample2D> &track, int i, const WristGrip *wg)
{
    const ShaftSample2D &s = track[size_t(i)];
    if (wg && wg->ok && size_t(i) < wg->vx.size() && std::isfinite(wg->vx[size_t(i)])) {
        const double c = std::cos(s.thetaRad), sn = std::sin(s.thetaRad), w = s.thetaDotRadS;
        // v_W + θ̇·(d·n̂ − p·û)
        return QPointF(wg->vx[size_t(i)] + w * (-wg->d * sn - wg->p * c),
                       wg->vy[size_t(i)] + w * ( wg->d * c  - wg->p * sn));
    }
    const int n = int(track.size());
    const int a = std::max(i - 1, 0), b = std::min(i + 1, n - 1);
    const double dt = double(track[size_t(b)].t_us - track[size_t(a)].t_us) / 1e6;
    if (!(dt > 0.0)) return QPointF(0.0, 0.0);
    return QPointF((track[size_t(b)].gripPx.x() - track[size_t(a)].gripPx.x()) / dt,
                   (track[size_t(b)].gripPx.y() - track[size_t(a)].gripPx.y()) / dt);
}

// Composed head speed (mph) on a track: |v_grip + L·θ̇·n̂|, n̂ = (−sin θ, cos θ) the
// image-plane direction of rotation, L the fused club length in px (falls back to the
// measured club length, then the sample's own visible extent). v_grip is the ±1-sample
// central difference of the grip path — the grip is smooth through impact, the head is
// not, and nothing here smooths across the step.
std::vector<double> composedHeadSpeedMph(const std::vector<ShaftSample2D> &track,
                                         const ShaftTrack2D &shaft, double mPerPx,
                                         const WristGrip *wg = nullptr)
{
    const int n = int(track.size());
    std::vector<double> sp(size_t(std::max(n, 0)), 0.0);
    if (n < 2) return sp;
    double lenFixed = -1.0;
    if (shaft.lengths.fusedPx > 0.0)        lenFixed = shaft.lengths.fusedPx;
    else if (shaft.measuredClubLenPx > 0.0) lenFixed = shaft.measuredClubLenPx;
    for (int i = 0; i < n; ++i) {
        const QPointF gv = gripVelocity(track, i, wg);
        const double gvx = gv.x(), gvy = gv.y();
        const ShaftSample2D &s = track[size_t(i)];
        const double L  = lenFixed > 0.0 ? lenFixed : std::max(s.visibleLenPx, 0.0);
        const double vx = gvx - L * s.thetaDotRadS * std::sin(s.thetaRad);
        const double vy = gvy + L * s.thetaDotRadS * std::cos(s.thetaRad);
        sp[size_t(i)] = std::hypot(vx, vy) * mPerPx * kMps2Mph;
    }
    return sp;
}

// Domain mask for the composed clubhead speed: every sample AFTER `boundaryUs` is marked
// invalid (MetricSeries::valid — drawn dashed, skipped by every reducer, never a phase dot).
// The boundary is the P7 knot of the track, where the synthesized rate steps, NOT the
// acoustic impact: the two differ by up to ~3 ms, and a boundary sample on the departing
// side would put the step back inside every window about Impact. Same mechanism as
// metric_channel.h's applyPhaseDomainMask (design §5.1), snapped to the knot instead of the
// nearest grid sample for that reason. No boundary ⇒ the series is left exactly as it was.
void maskAfter(MetricSeries &m, int64_t boundaryUs)
{
    if (m.t_us.empty() || boundaryUs < 0) return;
    bool any = false;
    for (int64_t t : m.t_us) if (t > boundaryUs) { any = true; break; }
    if (!any) return;
    if (m.valid.empty()) m.valid.assign(m.t_us.size(), 1u);
    for (size_t i = 0; i < m.t_us.size(); ++i)
        if (m.t_us[i] > boundaryUs) m.valid[i] = 0u;
}

MetricSeries makeSpeedSeries(const QString &key, const QString &label,
                             const std::vector<int64_t> &t, std::vector<double> v,
                             const std::vector<PhaseEvent> &phases, int64_t impactUs,
                             int64_t maskAfterUs = -1)
{
    MetricSeries m;
    m.key   = key;
    m.label = label;
    m.unit  = QStringLiteral("mph");
    m.t_us  = t;
    m.value = std::move(v);
    maskAfter(m, maskAfterUs);
    addPhaseDots(m, phases, impactUs);
    return m;
}

// ── clubheadPeakLead: how long before the ball the clubhead stopped getting faster ──
//
// The TIME of the clubhead speed's maximum, in ms before `anchorUs`, as a single-sample
// scalar (empty curve + one Impact phaseSample — the shape transitionPlaneDelta and every
// setup metric use, so m_clubheadPeakLead's `at p7` reducer finds it in the phase grid).
//
// WHY A PRODUCER AND NOT A REDUCER. No reducer returns the time of an extremum — `extremum`
// returns its value — and the question "did the club give up speed before the ball" is
// exactly that time. It is read off the composed, domain-masked clubhead speed: the mask
// ends the series at the P7 knot, so the departing side (an iron gives the ball a quarter
// of the club's speed in two frames) can never be the peak, and the search runs from the
// Top tick when the timeline has one — the backswing has its own, smaller peak, and a
// series without a Top is searched whole rather than guessed at.
//
// The anchor is the same boundary the mask uses (the P7 knot, else the impact instant), so
// a peak on the last valid sample reads as 0 ms and never as the ~3 ms knot-to-acoustic
// offset. POSITIVE IS EARLIER: a clubhead that peaked 40 ms before the ball reads 40.
//
// Hand speed deliberately gets no such series. Hands peak ~70 ms before impact in every
// good release (97 of 97 corpus swings) — that is the sling working, not a fault.
std::optional<MetricSeries> peakLeadSeries(const MetricSeries &speed,
                                           const std::vector<PhaseEvent> &phases,
                                           int64_t anchorUs)
{
    if (anchorUs < 0 || speed.t_us.size() < 3) return std::nullopt;
    int64_t fromUs = std::numeric_limits<int64_t>::min();
    for (const PhaseEvent &e : phases)
        if (e.phase == Phase::Top) { fromUs = e.t_us; break; }

    // The searched samples: valid, from Top to the anchor.
    std::vector<size_t> idx;
    for (size_t i = 0; i < speed.t_us.size(); ++i) {
        if (!speed.valid.empty() && speed.valid[i] == 0u) continue;
        if (speed.t_us[i] < fromUs || speed.t_us[i] > anchorUs) continue;
        idx.push_back(i);
    }
    // Fewer than three valid samples between Top and the anchor is a track that never
    // covered the downswing; a peak "found" there would be the mask's edge, not the club's.
    if (idx.size() < 3) return std::nullopt;

    // SPIKES ARE NOT SPEED (2026-09-23). The search runs on a running median of those samples,
    // ±kPeakLeadMedianHalfUs wide: a jump in the synthesized arc differentiates into a 1–3-sample
    // speed spike, and on 16 of 38 corpus swings that spike — not the club — was the maximum, so
    // the lead jumped ~60 or ~160 ms between two encodings of the same frames. See the constant.
    const int64_t half = tuned::kinematics::kPeakLeadMedianHalfUs;
    std::vector<double> med(idx.size());
    for (size_t j = 0; j < idx.size(); ++j) {
        std::vector<double> w;
        for (size_t q = 0; q < idx.size(); ++q)
            if (std::llabs(speed.t_us[idx[q]] - speed.t_us[idx[j]]) <= half)
                w.push_back(speed.value[idx[q]]);
        std::nth_element(w.begin(), w.begin() + w.size() / 2, w.end());
        med[j] = w[w.size() / 2];
    }
    size_t bestJ = 0;
    for (size_t j = 1; j < med.size(); ++j)
        if (med[j] > med[bestJ]) bestJ = j;

    // THE PLATEAU, NOT THE ARGMAX. The composed speed flattens over the last ~60 ms before the
    // ball, so a ±3 mph tracker wobble on a flat top can move the single highest sample by tens of
    // milliseconds — one 9 Sep 2026 swing read 55 ms on the argmax where its speed had merely
    // dipped 69→58→65 mph and recovered. What "the club stopped getting faster" means is the LAST
    // instant the speed was still within a hair of its maximum, so the lead is taken from the last
    // valid sample at or above 97 % of the peak: a club still at full speed into the ball reads 0
    // whatever the wobble did earlier, and a club that genuinely fell away reads the fall.
    const double floor = 0.97 * med[bestJ];
    size_t lastJ = bestJ;
    for (size_t j = bestJ; j < med.size(); ++j)
        if (med[j] >= floor) lastJ = j;
    const size_t last = idx[lastJ];

    MetricSeries m;
    m.key   = QStringLiteral("clubheadPeakLead");
    m.label = QStringLiteral("Speed peak before the ball");
    m.unit  = QStringLiteral("ms");
    m.phaseSamples.push_back({ Phase::Impact, anchorUs,
                               double(anchorUs - speed.t_us[last]) / 1000.0, QString() });
    return m;
}

// ── lag: lead forearm (elbow→wrist) vs shaft (grip→head) ─────────────────────
// Interpolated lead-forearm direction (px) at time `t`; false if no confident cover.
bool forearmDirAt(const PoseTrack2D &pose, int64_t t, int elbowKp, int wristKp,
                  int frameW, int frameH, double &dx, double &dy)
{
    const auto &F = pose.frames;
    if (F.size() < 2) return false;
    const double W = frameW > 0 ? double(frameW) : 1.0;
    const double H = frameH > 0 ? double(frameH) : 1.0;
    size_t hi = F.size();
    for (size_t i = 0; i < F.size(); ++i) if (F[i].t_us >= t) { hi = i; break; }
    const size_t iB = std::min(hi, F.size() - 1);
    const size_t iA = iB > 0 ? iB - 1 : 0;
    const PoseFrame2D &A = F[iA], &B = F[iB];
    const float cA = std::min(A.conf[size_t(elbowKp)], A.conf[size_t(wristKp)]);
    const float cB = std::min(B.conf[size_t(elbowKp)], B.conf[size_t(wristKp)]);
    const auto vecOf = [&](const PoseFrame2D &f) {
        return QPointF((f.kp[size_t(wristKp)].x() - f.kp[size_t(elbowKp)].x()) * W,
                       (f.kp[size_t(wristKp)].y() - f.kp[size_t(elbowKp)].y()) * H);
    };
    constexpr float kConf = 0.15f;
    QPointF v;
    if (cA >= kConf && cB >= kConf && B.t_us > A.t_us) {
        const double f = std::clamp(double(t - A.t_us) / double(B.t_us - A.t_us), 0.0, 1.0);
        v = vecOf(A) * (1.0 - f) + vecOf(B) * f;
    } else if (cB >= kConf) {
        v = vecOf(B);
    } else if (cA >= kConf) {
        v = vecOf(A);
    } else {
        return false;
    }
    dx = v.x();
    dy = v.y();
    return std::hypot(dx, dy) > 1e-6;
}

// Nearest shaft direction angle (grip→head, rad) at time t on the chosen track.
bool shaftAngleAt(const std::vector<ShaftSample2D> &track, int64_t t, double &theta)
{
    if (track.empty()) return false;
    int64_t best = std::numeric_limits<int64_t>::max();
    for (const ShaftSample2D &s : track) {
        const int64_t d = std::llabs(s.t_us - t);
        if (d < best) { best = d; theta = s.thetaRad; }
    }
    return true;
}

MetricSeries buildLagSeries(const std::vector<ShaftSample2D> &track, const ShaftTrack2D &shaft,
                            const PoseTrack2D &pose, int handedness,
                            const std::vector<PhaseEvent> &phases, int64_t impactUs)
{
    MetricSeries m;
    const bool leadLeft = (handedness != 2);
    const int elbowKp = leadLeft ? kLeftElbow : kRightElbow;
    const int wristKp = leadLeft ? kLeftWrist : kRightWrist;
    for (const PoseFrame2D &f : pose.frames) {
        double dx, dy, theta;
        if (!forearmDirAt(pose, f.t_us, elbowKp, wristKp, shaft.frameWidth, shaft.frameHeight, dx, dy))
            continue;
        if (!shaftAngleAt(track, f.t_us, theta))
            continue;
        const double forearm = std::atan2(dy, dx);
        const double lagDeg  = std::fabs(wrapPi(theta - forearm)) * 180.0 / kPi;   // [0,180]
        m.t_us.push_back(f.t_us);
        m.value.push_back(lagDeg);
    }
    if (m.value.size() < 2) { m.t_us.clear(); m.value.clear(); return m; }
    m.value = movAvg(m.value, 1);
    m.key   = QStringLiteral("lagAngle");
    m.label = QStringLiteral("Lag");
    m.unit  = QStringLiteral("°");
    addPhaseDots(m, phases, impactUs);
    return m;
}

// Robust spread (1.4826 × MAD) of a sample; NaN when fewer than 5 values.
double robustSd(std::vector<double> v)
{
    v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return !std::isfinite(x); }), v.end());
    if (v.size() < 5) return std::numeric_limits<double>::quiet_NaN();
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    const double med = v[v.size() / 2];
    for (double &x : v) x = std::abs(x - med);
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return 1.4826 * v[v.size() / 2];
}

// A keypoint's smoother σ (px) at time t, floored; the floor when the smoother left nothing.
double kpSigmaPx(const PoseTrack2D &pose, int64_t t, int kp)
{
    const double floorPx = tuned::uncertainty::kGripSigmaFloorPx;
    if (pose.smoothed.empty() || pose.smoothedAux.size() != pose.smoothed.size()) return floorPx;
    size_t best = 0;
    int64_t bd = std::numeric_limits<int64_t>::max();
    for (size_t i = 0; i < pose.smoothed.size(); ++i) {
        const int64_t d = std::llabs(pose.smoothed[i].t_us - t);
        if (d < bd) { bd = d; best = i; }
    }
    const float s = pose.smoothedAux[best].sigma[size_t(kp)];
    return s > 0.f ? std::max(floorPx, double(s)) : floorPx;
}

// The median camera-frame interval of the measured track (s) — the spacing the grip's jitter
// lives on, whatever cadence the drawn track has.
double cameraPeriodS(const ShaftTrack2D &shaft)
{
    std::vector<int64_t> d;
    for (size_t i = 1; i < shaft.samples.size(); ++i)
        if (shaft.samples[i].t_us > shaft.samples[i - 1].t_us) d.push_back(shaft.samples[i].t_us - shaft.samples[i - 1].t_us);
    if (d.empty()) return 1.0 / 150.0;
    std::nth_element(d.begin(), d.begin() + d.size() / 2, d.end());
    return double(d[d.size() / 2]) * 1e-6;
}

// σ on the kinematic series' phase readings (uncertainty design §6). Clubhead speed: a Monte
// Carlo over the synth posterior's draws (the rotation-rate term, anchors included) ⊕ the club
// length (the composed speed is linear in it) ⊕ the grip velocity. Peak lead: the same draws.
// Hand speed: the grip velocity. Lag: the shaft angle ⊕ the forearm angle.
void addKinematicSigma(std::vector<MetricSeries> &out, const KinematicSeriesInputs &in,
                       const std::vector<ShaftSample2D> &track, double mPerPx, double lengthM,
                       int64_t boundaryUs, const WristGrip *wg)
{
    const ShaftTrack2D &shaft = *in.shaft;
    const UncertaintyConfig &unc = *in.unc;
    const double hCam  = cameraPeriodS(shaft);
    const double sClub = in.clubLengthKnown ? tuned::uncertainty::kClubLenSigmaKnownM
                                            : tuned::uncertainty::kClubLenSigmaDefaultM;
    // The grip velocity's σ (px/s) from a ±1-frame difference of two grip positions.
    const auto gripVelSigma = [&](int64_t t) {
        const double sg = in.pose ? shaftsigma::gripSigmaPx(*in.pose, t) : tuned::uncertainty::kGripSigmaFloorPx;
        return std::sqrt(2.0) * sg / (2.0 * hCam);
    };
    const bool synthTrack = &track == &shaft.synth;
    std::vector<std::vector<ShaftSample2D>> draws;
    if (synthTrack && shaft.synthPost && unc.synthPosterior) {
        DetRng rng(unc.seed);
        draws = synthDraws(shaft.synth, shaft.positions, *shaft.synthPost, unc.mcDraws, rng);
    }
    for (MetricSeries &m : out) {
        if (m.key == QStringLiteral("clubheadSpeed") || m.key == QStringLiteral("handSpeed")) {
            const bool head = m.key == QStringLiteral("clubheadSpeed");
            std::vector<std::vector<double>> drawSp;
            if (head && in.composed)
                for (const auto &d : draws) drawSp.push_back(composedHeadSpeedMph(d, shaft, mPerPx, wg));
            for (PhaseSample &ps : m.phaseSamples) {
                const int i = nearestIndex(m.t_us, ps.t_us);
                if (i < 0) continue;
                double sMc = 0.0;
                if (!drawSp.empty()) {
                    std::vector<double> v;
                    for (const auto &sp : drawSp) if (size_t(i) < sp.size()) v.push_back(sp[size_t(i)]);
                    sMc = robustSd(v);
                } else if (head) {
                    // No posterior: the measured samples' σθ through the rotation term, L·σθ̇.
                    const shaftsigma::SigmaAt sa = shaftsigma::sigmaAt(shaft.samples, ps.t_us);
                    const double L = shaft.lengths.fusedPx > 0.0 ? shaft.lengths.fusedPx : shaft.measuredClubLenPx;
                    if (std::isfinite(sa.sigDeg) && L > 0.0)
                        sMc = L * (std::sqrt(2.0) * sa.sigDeg * kPi / 180.0 / (2.0 * hCam)) * mPerPx * kMps2Mph;
                }
                if (!std::isfinite(sMc)) continue;
                const double sGrip = gripVelSigma(ps.t_us) * mPerPx * kMps2Mph / (head ? 1.0 : std::sqrt(unc.nEff(5.0)));
                const double sLen  = lengthM > 0.0 ? std::abs(ps.value) * sClub / lengthM : 0.0;
                const double sig   = shaftsigma::quad(sMc, sGrip, sLen)
                                     * (head ? tuned::uncertainty::kInflateSpeed : 1.0);
                ps.sigma     = sig;
                ps.sigmaKind = head ? (in.clubLengthKnown ? shaftsigma::kindOf(tuned::uncertainty::kCalSpeed)
                                                          : uint8_t(SigmaKind::AssumedInput))
                                    : uint8_t(SigmaKind::Propagated);
                const shaftsigma::SigmaAt sa = shaftsigma::sigmaAt(track, ps.t_us);
                if (std::isfinite(sa.pGross)) ps.grossRisk = float(sa.pGross);
                if (ps.phase == Phase::Impact) { m.sigma = sig; m.sigmaKind = ps.sigmaKind; }
            }
        } else if (m.key == QStringLiteral("clubheadPeakLead")) {
            if (draws.empty() || !in.composed || m.phaseSamples.empty()) continue;
            std::vector<int64_t> t;
            for (const ShaftSample2D &e : track) t.push_back(e.t_us);
            std::vector<double> v;
            for (const auto &d : draws) {
                const MetricSeries sp = makeSpeedSeries(QStringLiteral("clubheadSpeed"), QString(), t,
                                                        composedHeadSpeedMph(d, shaft, mPerPx, wg),
                                                        in.phases, in.impactUs, boundaryUs);
                if (const std::optional<MetricSeries> lead =
                        peakLeadSeries(sp, in.phases, boundaryUs >= 0 ? boundaryUs : in.impactUs))
                    if (!lead->phaseSamples.empty()) v.push_back(lead->phaseSamples.front().value);
            }
            // The peak lands on a sample, so the draws' spread is quantised: when most draws pick the
            // same sample the robust spread is 0, which would claim an exact time. Floor it at the
            // sample quantum, period/√12 (the 1 Oct sigma sweep: 13 of 90 swings read ±0).
            const double rawSig = robustSd(v);
            if (!std::isfinite(rawSig)) continue;
            const double sig = std::max(rawSig, cameraPeriodS(shaft) * 1e3 * tuned::uncertainty::kCrossingFloorFrac);
            PhaseSample &ps = m.phaseSamples.front();
            ps.sigma = sig;
            ps.sigmaKind = uint8_t(SigmaKind::Propagated);
            // The share of draws in which the peak could not be read at all is its gross risk.
            ps.grossRisk = float(1.0 - double(v.size()) / double(std::max<size_t>(1, draws.size())));
            m.sigma = sig; m.sigmaKind = ps.sigmaKind;
        } else if (m.key == QStringLiteral("lagAngle")) {
            const bool leadLeft = (in.handedness != 2);
            const int elbowKp = leadLeft ? kLeftElbow : kRightElbow;
            const int wristKp = leadLeft ? kLeftWrist : kRightWrist;
            for (PhaseSample &ps : m.phaseSamples) {
                const shaftsigma::SigmaAt sa = shaftsigma::sigmaAt(track, ps.t_us);
                if (!std::isfinite(sa.sigDeg) || !in.pose) continue;
                double dx = 0.0, dy = 0.0;
                if (!forearmDirAt(*in.pose, ps.t_us, elbowKp, wristKp, shaft.frameWidth, shaft.frameHeight, dx, dy))
                    continue;
                const double sPhi = shaftsigma::lineAngleSigmaDeg(kpSigmaPx(*in.pose, ps.t_us, elbowKp),
                                                                  kpSigmaPx(*in.pose, ps.t_us, wristKp),
                                                                  std::hypot(dx, dy));
                if (!std::isfinite(sPhi)) continue;
                ps.sigma     = shaftsigma::quad(sa.sigDeg, sPhi) * tuned::uncertainty::kInflateLag;
                ps.sigmaKind = shaftsigma::kindOf(tuned::uncertainty::kCalLag);
                if (std::isfinite(sa.pGross)) ps.grossRisk = float(sa.pGross);
                if (ps.phase == Phase::Impact) { m.sigma = ps.sigma; m.sigmaKind = ps.sigmaKind; }
            }
        }
    }
}

} // namespace

std::optional<MetricSeries> clubheadPeakLeadFromSpeed(const MetricSeries &speed,
                                                     const std::vector<PhaseEvent> &phases,
                                                     int64_t anchorUs)
{
    return peakLeadSeries(speed, phases, anchorUs);
}

std::vector<MetricSeries> buildKinematicSeries(const KinematicSeriesInputs &in)
{
    std::vector<MetricSeries> out;

    // Speeds + lag need the shaft track. Prefer the dense C¹ synth channel (the display
    // track, smoother for differentiation); fall back to the measured samples.
    if (!in.shaft || !in.shaft->valid)
        return out;
    const ShaftTrack2D &shaft = *in.shaft;
    const std::vector<ShaftSample2D> &track =
        (shaft.synth.size() >= 2) ? shaft.synth : shaft.samples;
    if (track.size() < 2)
        return out;

    // Composed: the fused px span runs from the hands to the head, so it maps to the club
    // length LESS the grip-down (the tracker's own drawing convention), never the full club.
    const double lengthM = in.composed ? std::max(in.clubLengthM - in.gripDownM, 0.0) : in.clubLengthM;
    const double mPerPx  = resolveMetrePerPx(shaft, track, lengthM);
    std::vector<int64_t> t;
    std::vector<double> hx, hy, gx, gy;
    t.reserve(track.size());
    for (const ShaftSample2D &e : track) {
        t.push_back(e.t_us);
        hx.push_back(e.headPx.x());  hy.push_back(e.headPx.y());
        gx.push_back(e.gripPx.x());  gy.push_back(e.gripPx.y());
    }
    // Composed: the tail past the P7 knot (or, without a located P7, past the impact
    // instant) is out of the metric's Address→Impact domain — see maskAfter.
    int64_t boundaryUs = -1;
    if (in.composed) {
        boundaryUs = in.impactUs;
        for (const ShaftPosition &p : shaft.positions)
            if (p.p == 7) { boundaryUs = p.t_us; break; }
        // One impact instant (uncertainty design §4.4, uncertainty.oneImpact): the ladder's.
        if (in.unc && in.unc->enabled && in.unc->oneImpact)
            for (const PhaseEvent &e : in.phases)
                if (e.phase == Phase::Impact) { boundaryUs = e.t_us; break; }
    }
    // The grip's velocity off the pose wrists (wristVelocity), when the pose carries them.
    WristGrip wristG;
    if (in.wristVelocity && in.pose)
        wristG = wristGrip(*in.pose, track, shaft.frameWidth, shaft.frameHeight);
    const WristGrip *wg = wristG.ok ? &wristG : nullptr;
    out.push_back(makeSpeedSeries(QStringLiteral("clubheadSpeed"), QStringLiteral("Clubhead speed"),
                                  t, in.composed ? composedHeadSpeedMph(track, shaft, mPerPx, wg)
                                                 : speedMph(t, hx, hy, mPerPx),
                                  in.phases, in.impactUs, boundaryUs));
    if (std::optional<MetricSeries> lead =
            peakLeadSeries(out.back(), in.phases, boundaryUs >= 0 ? boundaryUs : in.impactUs))
        out.push_back(std::move(*lead));
    std::vector<double> hand = speedMph(t, gx, gy, mPerPx);
    if (wg)
        for (int i = 0; i < int(track.size()); ++i)
            if (std::isfinite(wg->vx[size_t(i)])) {
                const QPointF v = gripVelocity(track, i, wg);
                hand[size_t(i)] = std::hypot(v.x(), v.y()) * mPerPx * kMps2Mph;
            }
    out.push_back(makeSpeedSeries(QStringLiteral("handSpeed"), QStringLiteral("Hand speed"),
                                  t, std::move(hand), in.phases, in.impactUs));

    // Lag additionally needs the pose track; omit it (not fabricate) when absent.
    if (in.pose && in.pose->frames.size() >= 2) {
        MetricSeries lag = buildLagSeries(track, shaft, *in.pose, in.handedness,
                                          in.phases, in.impactUs);
        if (!lag.t_us.empty())
            out.push_back(std::move(lag));
    }

    if (in.unc && in.unc->enabled)
        addKinematicSigma(out, in, track, mPerPx, lengthM, boundaryUs, wg);
    return out;
}

} // namespace pinpoint::analysis
