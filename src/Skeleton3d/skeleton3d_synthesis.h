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

// The session's usual swing (docs/design/swing_3d_session_synthesis_design.md §4): one figure made
// from the skeleton3d fits a session already holds. DISPLAY ONLY — nothing reads this but the 3-D
// swing panel, and no metric may be taken from it: averaging softens the fastest parts of a swing.
//
// Everything here is in the 3-D panel's SCENE frame (swing_rig_driver.h): each swing is first turned
// by its own stance yaw and placed about its own display origin, the frame the panel draws in.
//
// Pure: Qt types only through the track reader, no Eigen, no files.

#include <QString>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <vector>

#include "skeleton3d_json.h"
#include "skeleton3d_rig.h"

namespace pinpoint::skeleton3d {

inline constexpr int     kSynthSamplesPerSegment = 40;      // grid samples between each pair of P-positions
inline constexpr int     kSynthSegments = 7;                // P1→P2 … P6→P7, then the fixed run after impact
inline constexpr int64_t kSynthAfterImpactUs = 90000;       // P8 is too erratic to align on (75–412 ms after impact)
inline constexpr int64_t kSynthPlaneBlendUs = 10000;        // shots with no plane fade out over this, after impact
inline constexpr double  kSynthWeightScale = 1.5;           // × the median distance: the Cauchy weight's width
inline constexpr int     kSynthMinShots = 3;                // fewer usable shots: no synthesis (decision 5)
inline constexpr int     kSynthMinPlaneShots = 5;           // fewer shots with a plane: face-on only (decision 2)
inline constexpr int     kSynthMinPlaneAfterImpact = 3;     // fewer: all shots stay in after impact, with a caveat

// One shot of the session, as read from its document.
struct SynthSwing {
    QString id;
    QString club;                                  // review.club, exactly as stored
    Skeleton3DTrack track;
    std::array<int64_t, 7> pUs { -1, -1, -1, -1, -1, -1, -1 };     // P1…P7, window-relative; < 0 = missing
    std::array<int, 7> pSource {};                 // how each was found (club.positions[].source)
    bool hasPlane = false;                         // club3d's fused downswing plane was offered
    V3 planeNormal;                                // …its normal in the scene frame
    double planeInclDeg = 0;
};

struct SynthFrame {
    int64_t t_us = 0;                              // canonical time: 0 at P1
    std::vector<double> dof;                       // the joints' DoFs; the root's six are unused (0)
    V3 rootPos;                                    // scene frame
    Q  rootRot;                                    // scene frame (rig-local → scene, as Pose::local[0])
    V3 grip, shaftDir;                             // scene frame
    std::array<uint8_t, Rig::N> tier {};
    int shaftTier = 0;
};

struct SynthResult {
    bool valid = false;
    QString reason;
    int nShots = 0;                                // the shots used
    int nPlaneShots = 0;                           // …of which have a fused plane
    bool clubAfterImpactUncertain = false;         // too few plane shots: all shots stay in after impact
    bool planeValid = false;
    V3 planeNormal;
    double planeInclDeg = 0;
    std::array<double, GroupCount> lengths {};
    double clubLengthM = 0.95;
    V3 ball;
    bool ballValid = false;
    std::array<int64_t, 8> pUs {};                 // canonical P1…P7, and P8 at the end of the run after impact
    std::array<int, 8> pSource {};
    std::vector<double> weights;                   // per shot used, in the input order
    std::vector<QString> shotIds;
    std::vector<SynthFrame> frames;
};

namespace synth_detail {

inline double medianOf(std::vector<double> &v)
{
    if (v.empty()) return std::numeric_limits<double>::quiet_NaN();
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

struct Scene {
    Q rot;
    V3 origin;
    explicit Scene(const Skeleton3DTrack &t)
        : rot((Q::axisAngle({ 1, 0, 0 }, -kPi / 2.0) * Q::axisAngle({ 0, 0, 1 }, -t.stanceYawDeg * kDeg)).normalized()),
          origin(t.displayOrigin) {}
    V3 point(const V3 &p) const { return rot.rotate(p - origin); }
    V3 dir(const V3 &d) const { return rot.rotate(d); }
};

// One shot on the grid, in the scene frame.
struct Sampled {
    std::vector<double> dof;                       // G × nDof
    std::vector<V3> pos;                           // G × Rig::N
    std::vector<Q> root;
    std::vector<V3> grip, dir;
    std::vector<uint8_t> tier;                     // G × Rig::N
    std::vector<int> shaftTier;
};

inline Sampled sample(const SynthSwing &s, const std::vector<int64_t> &gridUs)
{
    const Rig &R = rig();
    const int G = int(gridUs.size()), nd = R.dofCount();
    const Scene S(s.track);
    const auto &F = s.track.frames;
    Sampled out;
    out.dof.resize(size_t(G * nd));
    out.pos.resize(size_t(G * Rig::N));
    out.root.resize(size_t(G));
    out.grip.resize(size_t(G));
    out.dir.resize(size_t(G));
    out.tier.resize(size_t(G * Rig::N));
    out.shaftTier.resize(size_t(G));
    Pose pose;
    std::vector<double> th(static_cast<size_t>(nd));
    for (int g = 0; g < G; ++g) {
        const int64_t t = std::clamp(gridUs[size_t(g)], F.front().t_us, F.back().t_us);
        const auto hi = std::upper_bound(F.begin(), F.end(), t,
                                         [](int64_t v, const Skeleton3DFrame &f) { return v < f.t_us; });
        const size_t b = hi == F.end() ? F.size() - 1 : size_t(hi - F.begin());
        const size_t a = b > 0 ? b - 1 : 0;
        const double w = (b != a && F[b].t_us > F[a].t_us) ? double(t - F[a].t_us) / double(F[b].t_us - F[a].t_us) : 0.0;
        for (int k = 0; k < nd; ++k) th[size_t(k)] = F[a].dof[size_t(k)] * (1.0 - w) + F[b].dof[size_t(k)] * w;
        forwardKinematics(R, th.data(), s.track.lengths.data(), pose);
        std::copy(th.begin(), th.end(), out.dof.begin() + g * nd);
        for (int j = 0; j < Rig::N; ++j) out.pos[size_t(g * Rig::N + j)] = S.point(pose.pos[j]);
        out.root[size_t(g)] = (S.rot * pose.local[0]).normalized();
        out.grip[size_t(g)] = S.point(F[a].grip * (1.0 - w) + F[b].grip * w);
        out.dir[size_t(g)] = S.dir((F[a].shaftDir * (1.0 - w) + F[b].shaftDir * w).unit());
        const auto &closest = F[w < 0.5 ? a : b];
        std::copy(closest.tier.begin(), closest.tier.end(), out.tier.begin() + g * Rig::N);
        out.shaftTier[size_t(g)] = closest.shaftTier;
    }
    return out;
}

} // namespace synth_detail

// The figure's pose at a synthesised frame, in the scene frame.
inline void synthPose(const SynthResult &r, const SynthFrame &f, Pose &out)
{
    out.pos[0] = f.rootPos;
    out.rot[0] = f.rootRot;
    out.local[0] = f.rootRot;
    forwardChain(rig(), f.dof.data(), r.lengths.data(), out);
}

// The session's usual swing with `club`: the recipe of §4. `leadIsLeft` picks the hand the club
// is held by. Refused (valid false, `reason` set) when fewer than kSynthMinShots shots are usable.
inline SynthResult synthesiseSession(const std::vector<SynthSwing> &all, const QString &club, bool leadIsLeft)
{
    using namespace synth_detail;
    SynthResult R;
    const Rig &rg = rig();
    const int nd = rg.dofCount();

    // ── which shots ──
    std::vector<const SynthSwing *> cand;
    for (const SynthSwing &s : all) {
        if (!s.track.valid || !s.track.dtl || s.track.frames.size() < 2 || s.club != club) continue;
        bool ok = s.pUs[0] >= 0;
        for (size_t k = 1; k < s.pUs.size() && ok; ++k) ok = s.pUs[k] > s.pUs[k - 1];
        if (ok) cand.push_back(&s);
    }
    std::map<int, int> versions;
    for (const SynthSwing *s : cand) ++versions[s->track.stageVersion];
    int version = 0, most = 0;
    for (const auto &[v, n] : versions)          // ascending: a tie goes to the newer fit
        if (n >= most) { most = n; version = v; }
    std::vector<const SynthSwing *> used;
    for (const SynthSwing *s : cand)
        if (s->track.stageVersion == version) used.push_back(s);
    const int N = int(used.size());
    R.nShots = N;
    if (N < kSynthMinShots) {
        R.reason = QStringLiteral("fewer than %1 usable shots with this club").arg(kSynthMinShots);
        return R;
    }

    // ── the common clock: the shots' median P-to-P durations ──
    std::array<double, kSynthSegments> dur {};
    for (size_t k = 0; k + 1 < 7; ++k) {
        std::vector<double> d;
        d.reserve(used.size());
        for (const SynthSwing *s : used) d.push_back(double(s->pUs[k + 1] - s->pUs[k]));
        dur[k] = medianOf(d);
    }
    dur[6] = double(kSynthAfterImpactUs);
    std::array<double, 8> canon {};
    for (size_t k = 0; k < 7; ++k) canon[k + 1] = canon[k] + dur[k];

    const int seg = kSynthSamplesPerSegment;
    const int G = kSynthSegments * seg + 1;
    const int gImpact = 6 * seg;
    std::vector<Sampled> S;
    S.reserve(used.size());
    for (const SynthSwing *s : used) {
        std::array<double, 8> key {};
        for (size_t k = 0; k < 7; ++k) key[k] = double(s->pUs[k]);
        key[7] = key[6] + double(kSynthAfterImpactUs);
        std::vector<int64_t> grid(static_cast<size_t>(G));
        for (int g = 0; g < G; ++g) {
            const int k = std::min(g / seg, kSynthSegments - 1);
            const double f = double(g - k * seg) / double(seg);
            grid[size_t(g)] = int64_t(std::llround(key[size_t(k)] + f * (key[size_t(k + 1)] - key[size_t(k)])));
        }
        S.push_back(sample(*s, grid));
    }

    // ── one weight per shot, from the shape of the swing only ──
    // Where the ball was teed moves the whole figure about 7 cm between shots, and must not read as
    // an unusual swing: each shot's own mean pelvis comes off first.
    using ybot::Hips;
    std::vector<V3> pelvis(used.size());
    for (int i = 0; i < N; ++i) {
        V3 m;
        for (int g = 0; g < G; ++g) m += S[size_t(i)].pos[size_t(g * Rig::N + Hips)];
        pelvis[size_t(i)] = m * (1.0 / double(G));
    }
    std::vector<V3> median(static_cast<size_t>(G * Rig::N));
    {
        std::vector<double> x(used.size()), y(used.size()), z(used.size());
        for (int c = 0; c < G * Rig::N; ++c) {
            for (int i = 0; i < N; ++i) {
                const V3 p = S[size_t(i)].pos[size_t(c)] - pelvis[size_t(i)];
                x[size_t(i)] = p.x; y[size_t(i)] = p.y; z[size_t(i)] = p.z;
            }
            median[size_t(c)] = { medianOf(x), medianOf(y), medianOf(z) };
        }
    }
    std::vector<double> dist(used.size());
    for (int i = 0; i < N; ++i) {
        double sum = 0;
        for (int c = 0; c < G * Rig::N; ++c) {
            const V3 e = S[size_t(i)].pos[size_t(c)] - pelvis[size_t(i)] - median[size_t(c)];
            sum += e.dot(e);
        }
        dist[size_t(i)] = std::sqrt(sum / double(G * Rig::N));
    }
    std::vector<double> distCopy = dist;
    const double width = kSynthWeightScale * medianOf(distCopy);
    std::vector<double> w(used.size(), 1.0);
    if (width > 1e-9)
        for (int i = 0; i < N; ++i) {
            const double r = dist[size_t(i)] / width;
            w[size_t(i)] = 1.0 / (1.0 + r * r);
        }
    const double sumW = std::accumulate(w.begin(), w.end(), 0.0);
    R.weights = w;
    for (const SynthSwing *s : used) R.shotIds.push_back(s->id);

    // ── per-golfer constants ──
    for (int g = 0; g < GroupCount; ++g) {
        std::vector<double> v;
        for (const SynthSwing *s : used) v.push_back(s->track.lengths[size_t(g)]);
        R.lengths[size_t(g)] = medianOf(v);
    }
    {
        std::vector<double> v;
        for (const SynthSwing *s : used) v.push_back(s->track.clubLengthM);
        R.clubLengthM = medianOf(v);
    }
    std::array<double, 3> gripOffset {};
    for (size_t c = 0; c < 3; ++c) {
        std::vector<double> v;
        for (const SynthSwing *s : used) v.push_back(s->track.gripOffsetLocal[c]);
        gripOffset[c] = medianOf(v);
    }
    {
        std::vector<double> x, y, z;
        for (const SynthSwing *s : used)
            if (s->track.ballValid) {
                const V3 b = Scene(s->track).point(s->track.ball);
                x.push_back(b.x); y.push_back(b.y); z.push_back(b.z);
            }
        if (!x.empty()) {
            R.ballValid = true;
            R.ball = { medianOf(x), medianOf(y), medianOf(z) };
        }
    }

    // ── the plane ──
    std::vector<char> plane(used.size(), 0);
    for (int i = 0; i < N; ++i) {
        plane[size_t(i)] = used[size_t(i)]->hasPlane ? 1 : 0;
        R.nPlaneShots += plane[size_t(i)];
    }
    R.clubAfterImpactUncertain = R.nPlaneShots < kSynthMinPlaneAfterImpact;
    if (R.nPlaneShots >= kSynthMinPlaneShots) {
        V3 sum, ref;
        bool haveRef = false;
        std::vector<double> incl;
        for (int i = 0; i < N; ++i) {
            if (!plane[size_t(i)]) continue;
            const V3 n = used[size_t(i)]->planeNormal;
            if (!haveRef) { ref = n; haveRef = true; }
            sum += (n.dot(ref) < 0 ? -n : n) * w[size_t(i)];
            incl.push_back(used[size_t(i)]->planeInclDeg);
        }
        if (sum.norm() > 1e-9) {
            R.planeValid = true;
            R.planeNormal = sum.unit();
            R.planeInclDeg = medianOf(incl);
        }
    }

    // ── P-marks ──
    for (size_t k = 0; k < 8; ++k) R.pUs[k] = int64_t(std::llround(canon[k]));
    for (size_t k = 0; k < 7; ++k) {
        std::map<int, int> votes;
        for (const SynthSwing *s : used) ++votes[s->pSource[k]];
        int bestN = 0;
        for (const auto &[src, n] : votes)
            if (n > bestN) { bestN = n; R.pSource[k] = src; }
    }
    R.pSource[7] = R.pSource[6];

    // ── the figure, frame by frame ──
    const int lead = leadIsLeft ? ybot::LeftHand : ybot::RightHand;
    const V3 gripOff { gripOffset[0], gripOffset[1], gripOffset[2] };
    const int nRootDof = rg.nDof[0];
    R.frames.resize(size_t(G));
    Pose pose;
    std::vector<int> digits(used.size());
    for (int g = 0; g < G; ++g) {
        SynthFrame &f = R.frames[size_t(g)];
        const int k = std::min(g / seg, kSynthSegments - 1);
        const double frac = double(g - k * seg) / double(seg);
        const double tc = canon[size_t(k)] + frac * dur[size_t(k)];
        f.t_us = int64_t(std::llround(tc));
        f.dof.assign(size_t(nd), 0.0);
        for (int d = nRootDof; d < nd; ++d) {
            double sum = 0;
            for (int i = 0; i < N; ++i) sum += w[size_t(i)] * S[size_t(i)].dof[size_t(g * nd + d)];
            f.dof[size_t(d)] = sum / sumW;
        }
        V3 rootPos;
        Q rootSum { 0, 0, 0, 0 };
        const Q ref = S[0].root[size_t(g)];
        for (int i = 0; i < N; ++i) {
            rootPos += S[size_t(i)].pos[size_t(g * Rig::N)] * w[size_t(i)];
            const Q q = S[size_t(i)].root[size_t(g)];
            const double dot = q.w * ref.w + q.x * ref.x + q.y * ref.y + q.z * ref.z;
            const double a = w[size_t(i)] * (dot < 0 ? -1.0 : 1.0);
            rootSum.w += a * q.w; rootSum.x += a * q.x; rootSum.y += a * q.y; rootSum.z += a * q.z;
        }
        f.rootPos = rootPos * (1.0 / sumW);
        f.rootRot = rootSum.normalized();

        for (int j = 0; j < Rig::N; ++j) {
            for (int i = 0; i < N; ++i) digits[size_t(i)] = S[size_t(i)].tier[size_t(g * Rig::N + j)];
            std::nth_element(digits.begin(), digits.begin() + N / 2, digits.end());
            f.tier[size_t(j)] = uint8_t(digits[size_t(N / 2)]);
        }
        for (int i = 0; i < N; ++i) digits[size_t(i)] = S[size_t(i)].shaftTier[size_t(g)];
        std::nth_element(digits.begin(), digits.begin() + N / 2, digits.end());
        f.shaftTier = digits[size_t(N / 2)];

        // The club hangs off the averaged hand: averaging the hand and the grip separately leaves
        // them up to 38 mm apart.
        synthPose(R, f, pose);
        f.grip = pose.pos[lead] + pose.rot[lead].rotate(gripOff);

        // Up to impact every shot counts; after it, only shots with a confirmed plane (the club
        // scatters 37° without one, 17° with).
        const double past = g > gImpact ? tc - canon[6] : 0.0;
        const double others = R.clubAfterImpactUncertain ? 1.0 : std::max(0.0, 1.0 - past / double(kSynthPlaneBlendUs));
        V3 dir;
        for (int i = 0; i < N; ++i)
            dir += S[size_t(i)].dir[size_t(g)] * (w[size_t(i)] * (plane[size_t(i)] ? 1.0 : others));
        f.shaftDir = dir.unit();
    }
    R.valid = true;
    return R;
}

} // namespace pinpoint::skeleton3d
