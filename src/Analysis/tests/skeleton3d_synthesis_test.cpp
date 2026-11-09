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

// skeleton3d_synthesis_test — the recipe of swing_3d_session_synthesis_design.md §4 on hand-built
// shots, and forwardChain against forwardKinematics.

#include <cstdio>
#include <cstring>

#include "../../Skeleton3d/skeleton3d_synthesis.h"

using namespace pinpoint::skeleton3d;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static int dofIndex(const char *name)
{
    const Rig &R = rig();
    for (int k = 0; k < R.dofCount(); ++k)
        if (std::strcmp(R.dofs[size_t(k)].name, name) == 0) return k;
    return -1;
}

using Keys = std::array<int64_t, 7>;
static const Keys kBase { 100000, 350000, 510000, 800000, 970000, 1035000, 1090000 };

struct Spec {
    Keys p = kBase;
    double elbow = 1.0;          // radians of elbow flexion by the end of the run after impact
    double rootX = 0.0;          // where this shot stood along the world X
    V3 afterImpact { 0, 0, -1 };   // the shaft's direction once clear of impact
    bool plane = false;
    double incl = 60.0;
    QString club = QStringLiteral("7 IRON");
    int version = 17;
    QString id = QStringLiteral("swing");
};

// u runs 0…6 across P1…P7, and 6…7 over the fixed run after impact: the elbow is linear in u, so a
// correct time alignment reads the same angle at the same P-position whatever the shot's tempo.
static SynthSwing makeSwing(const Spec &c)
{
    const Rig &R = rig();
    const int elbow = dofIndex("lElbow.flex");
    SynthSwing s;
    s.id = c.id;
    s.club = c.club;
    s.pUs = c.p;
    s.hasPlane = c.plane;
    s.planeNormal = { 0, 0, 1 };
    s.planeInclDeg = c.incl;
    Skeleton3DTrack &t = s.track;
    t.valid = true;
    t.dtl = true;
    t.stageVersion = c.version;
    t.lengths.fill(1.0);
    t.clubLengthM = 0.95;
    t.gripOffsetLocal = { 0.01, 0.0, 0.0 };
    t.displayOrigin = { 0, 2, 0 };
    t.ballValid = true;
    t.ball = { 0.2, 2, 0 };
    for (int64_t us = c.p[0]; us <= c.p[6] + 200000; us += 2000) {
        double u;
        if (us >= c.p[6]) {
            u = 6.0 + double(us - c.p[6]) / double(kSynthAfterImpactUs);
        } else {
            size_t k = 0;
            while (k + 1 < 6 && us >= c.p[k + 1]) ++k;
            u = double(k) + double(us - c.p[k]) / double(c.p[k + 1] - c.p[k]);
        }
        Skeleton3DFrame f;
        f.t_us = us;
        f.dof.assign(size_t(R.dofCount()), 0.0);
        f.dof[0] = c.rootX;
        f.dof[1] = 2.0;
        f.dof[2] = 1.0;
        f.dof[size_t(elbow)] = c.elbow * u / 7.0;
        const double a = std::clamp(double(us - c.p[6]) / 5000.0, 0.0, 1.0);
        f.shaftDir = (V3 { 0, 0, -1 } * (1.0 - a) + c.afterImpact * a).unit();
        f.grip = { 0, 2, 1 };
        f.tier.fill(3);
        f.shaftTier = 3;
        t.frames.push_back(std::move(f));
    }
    return s;
}

static std::vector<SynthSwing> many(int n, Spec c)
{
    std::vector<SynthSwing> v;
    for (int i = 0; i < n; ++i) {
        c.id = QStringLiteral("swing_%1").arg(i);
        v.push_back(makeSwing(c));
    }
    return v;
}

static const QString kClub = QStringLiteral("7 IRON");
static const int kAtP4 = 3 * kSynthSamplesPerSegment, kAtP7 = 6 * kSynthSamplesPerSegment;

int main()
{
    std::printf("=== skeleton3d_synthesis ===\n");
    const Rig &R = rig();
    const int elbow = dofIndex("lElbow.flex");

    // ── forwardChain is forwardKinematics below the root, to the bit ──
    {
        std::vector<double> th(size_t(R.dofCount()));
        for (int k = 0; k < R.dofCount(); ++k) th[size_t(k)] = 0.03 * double(k % 11) - 0.1;
        std::array<double, GroupCount> sc;
        sc.fill(1.07);
        Pose a, b;
        forwardKinematics(R, th.data(), sc.data(), a);
        b.pos[0] = a.pos[0]; b.rot[0] = a.rot[0]; b.local[0] = a.local[0];
        forwardChain(R, th.data(), sc.data(), b);
        bool same = true;
        for (int j = 0; j < Rig::N; ++j) {
            same = same && a.pos[j].x == b.pos[j].x && a.pos[j].y == b.pos[j].y && a.pos[j].z == b.pos[j].z;
            same = same && a.rot[j].w == b.rot[j].w && a.rot[j].x == b.rot[j].x && a.rot[j].y == b.rot[j].y && a.rot[j].z == b.rot[j].z;
        }
        check(same, "forwardChain matches forwardKinematics exactly");
    }

    // ── identical shots return that shot ──
    {
        const SynthResult r = synthesiseSession(many(3, Spec {}), kClub, true);
        check(r.valid && r.nShots == 3, "three identical shots synthesise");
        check(std::fabs(r.frames[size_t(kAtP4)].dof[size_t(elbow)] - 3.0 / 7.0) < 1e-9, "…and the elbow at P4 is that shot's");
        check(r.weights[0] == 1.0 && r.weights[2] == 1.0, "…with equal weights");
        const V3 root = r.frames[size_t(kAtP4)].rootPos;
        check(std::fabs(root.x) < 1e-9 && std::fabs(root.y - 1.0) < 1e-9 && std::fabs(root.z) < 1e-9, "…placed in the scene frame");
        check(r.pUs[0] == 0 && r.pUs[3] == 700000 && r.pUs[6] == 990000 && r.pUs[7] == 1080000, "…with canonical P-times from zero, P8 at impact + 90 ms");
        check(r.frames.size() == size_t(7 * kSynthSamplesPerSegment + 1) && r.frames.back().t_us == r.pUs[7], "…and a grid that ends at P8");
    }

    // ── tempo ──
    {
        std::vector<SynthSwing> v;
        for (const double f : { 1.0, 1.3, 0.8 }) {
            Spec c;
            for (size_t k = 0; k < 7; ++k) c.p[k] = 100000 + int64_t(double(kBase[k] - 100000) * f);
            v.push_back(makeSwing(c));
        }
        const SynthResult r = synthesiseSession(v, kClub, true);
        check(r.valid && std::fabs(r.frames[size_t(kAtP4)].dof[size_t(elbow)] - 3.0 / 7.0) < 1e-3, "shots at different tempos agree at P4");
        check(std::fabs(r.frames[size_t(kAtP7)].dof[size_t(elbow)] - 6.0 / 7.0) < 1e-3, "…and at impact");
    }

    // ── where the ball was teed is not an unusual swing ──
    {
        std::vector<SynthSwing> v;
        for (const double x : { 0.0, 0.0, 0.0, 0.15, -0.21 }) {
            Spec c;
            c.rootX = x;
            v.push_back(makeSwing(c));
        }
        const SynthResult r = synthesiseSession(v, kClub, true);
        bool equal = true;
        for (const double w : r.weights) equal = equal && std::fabs(w - r.weights[0]) < 1e-6;
        check(r.valid && equal, "a shot stood elsewhere gets its twin's weight");
    }

    // ── a wild shot counts for less ──
    {
        std::vector<SynthSwing> v;
        for (const double e : { 1.0, 1.02, 0.98, 1.01, 0.99, 2.5 }) {
            Spec c;
            c.elbow = e;
            v.push_back(makeSwing(c));
        }
        const SynthResult r = synthesiseSession(v, kClub, true);
        std::printf("      wild weight %.3f, usual %.3f\n", r.weights[5], r.weights[0]);
        check(r.valid && r.weights[5] < 0.5 * r.weights[0], "a wild shot is down-weighted");
        const double plain = (5.0 + 2.5) / 6.0 * 6.0 / 7.0;
        check(r.frames[size_t(kAtP7)].dof[size_t(elbow)] < plain - 0.05, "…and pulls the figure less than a plain mean would");
    }

    // ── the club hangs off the averaged lead hand ──
    for (const bool leadLeft : { true, false }) {
        const SynthResult r = synthesiseSession(many(3, Spec {}), kClub, leadLeft);
        const int hand = leadLeft ? ybot::LeftHand : ybot::RightHand;
        double worst = 0;
        Pose p;
        for (const SynthFrame &f : r.frames) {
            synthPose(r, f, p);
            worst = std::max(worst, (f.grip - (p.pos[hand] + p.rot[hand].rotate({ 0.01, 0, 0 }))).norm());
        }
        check(worst < 1e-9, leadLeft ? "the grip is on the left hand for a left-hand lead" : "the grip is on the right hand for a right-hand lead");
    }

    // ── after impact, only shots with a confirmed plane ──
    {
        std::vector<SynthSwing> v;
        for (int i = 0; i < 6; ++i) {
            Spec c;
            c.plane = i < 3;
            c.afterImpact = i < 3 ? V3 { 1, 0, 0 } : V3 { 0, 1, 0 };
            v.push_back(makeSwing(c));
        }
        const SynthResult r = synthesiseSession(v, kClub, true);
        const V3 before = r.frames[size_t(kAtP4)].shaftDir, last = r.frames.back().shaftDir;
        std::printf("      after impact, last frame: (%.3f, %.3f, %.3f)\n", last.x, last.y, last.z);
        check(r.valid && !r.clubAfterImpactUncertain, "three plane shots are enough to trust after impact");
        check(std::fabs(before.y + 1.0) < 1e-6, "before impact every shot counts, in the scene frame");
        check(std::fabs(last.x - 1.0) < 1e-6, "after impact the club is the plane shots' alone");
    }

    // ── who counts ──
    {
        check(!synthesiseSession(many(2, Spec {}), kClub, true).valid, "two usable shots are refused");
        check(!synthesiseSession(many(2, Spec {}), kClub, true).reason.isEmpty(), "…with a reason");
        std::vector<SynthSwing> v = many(3, Spec {});
        Spec other; other.club = QStringLiteral("DRIVER"); other.id = QStringLiteral("driver");
        v.push_back(makeSwing(other));
        SynthSwing noP = makeSwing(Spec {}); noP.id = QStringLiteral("noP"); noP.pUs[3] = -1;
        v.push_back(noP);
        Spec old; old.version = 12; old.id = QStringLiteral("old");
        v.push_back(makeSwing(old));
        SynthSwing face = makeSwing(Spec {}); face.id = QStringLiteral("faceOn"); face.track.dtl = false;
        v.push_back(face);
        const SynthResult r = synthesiseSession(v, kClub, true);
        check(r.valid && r.nShots == 3, "another club, a missing P-time, an older fit and no DTL are all left out");
    }

    // ── planes ──
    {
        auto withPlanes = [](int nPlane) {
            std::vector<SynthSwing> v;
            for (int i = 0; i < 6; ++i) {
                Spec c;
                c.plane = i < nPlane;
                c.incl = 58.0 + double(i);
                v.push_back(makeSwing(c));
            }
            return synthesiseSession(v, kClub, true);
        };
        const SynthResult two = withPlanes(2), four = withPlanes(4), five = withPlanes(5);
        check(two.clubAfterImpactUncertain && !two.planeValid, "two plane shots: the club after impact is uncertain, no plane");
        check(!four.clubAfterImpactUncertain && !four.planeValid, "four: the club is trusted, still no plane");
        check(five.planeValid && five.nPlaneShots == 5, "five: a plane");
        check(std::fabs(five.planeInclDeg - 60.0) < 1e-9, "…at the median of the shots' angles");
    }

    std::printf("=== %s (%d failure%s) ===\n", g_fail ? "FAILED" : "PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
