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

// The skeleton3d session pool (skeleton3d_pool.h; swing_3d_viz_design.md §13.2 (C)), on persisted
// blocks written by the production writer. Checks:
//   - the golfer's values are the medians over the swings (an outlier swing does not move them);
//   - a DTL camera that moves mid-session starts a new camera epoch, and each swing gets its own
//     epoch's cameras;
//   - fewer than two fitted swings make no pool;
//   - the pool survives its JSON round trip.

#include <QCoreApplication>

#include <cmath>
#include <cstdio>

#include "../skeleton3d/skeleton3d_json.h"
#include "../skeleton3d/skeleton3d_pool.h"

namespace sk = pinpoint::skeleton3d;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static QJsonObject block(double armScale, double club, double dtlX, double sym0)
{
    sk::FitResult r;
    r.valid = true;
    r.dtlUsed = true;
    r.scale.fill(1.0);
    r.scale[sk::GUpperArm] = armScale;
    r.clubLengthM = club;
    r.gripAxisLocal = { 0.0, 1.0, 0.0 };
    r.gripOffsetLocal = { 0.0, 0.08, 0.02 };
    r.trailGripOffsetLocal = { 0.0, 0.08, -0.02 };
    r.symOffsets = { sym0, 0.01, -0.02, 0.005 };
    r.cam.fF = 1000; r.cam.pF = 2 * sk::kDeg;
    r.cam.cD = { dtlX, 2.0, 0.3 }; r.cam.psiD = 180 * sk::kDeg; r.cam.pD = 5 * sk::kDeg; r.cam.fD = 900;
    r.cam.rD = 3 * sk::kDeg;
    return sk::skeleton3dToJson(r, 0, 2);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("=== skeleton3d_pool ===\n");

    // Five swings: the DTL camera moves 0.5 m after the third; swing 2's arm is an outlier.
    std::vector<std::pair<QString, QJsonObject>> sw {
        { "swing_0001", block(1.02, 0.93, 3.00, 0.010) }, { "swing_0002", block(1.40, 0.94, 3.01, 0.012) },
        { "swing_0003", block(1.03, 0.95, 2.99, 0.011) }, { "swing_0004", block(1.01, 0.94, 3.50, 0.010) },
        { "swing_0005", block(1.04, 0.94, 3.51, 0.011) } };
    const sk::SessionPool P = sk::poolSkeletons(sw);
    check(P.valid && P.nSwings == 5, "five fitted swings pool");
    std::printf("      epochs %zu\n", P.epochs.size());
    check(!P.golfer.hasScale && !P.golfer.hasSym && !P.golfer.hasGrip && !std::isfinite(P.golfer.clubToHeadM),
          "by default only the cameras are held (the golfer's values graded worse, §13.7)");
    sk::PoolConfig all;
    all.poolClub = all.poolScale = all.poolSym = all.poolGrip = true;
    const sk::SessionPool A = sk::poolSkeletons(sw, all);
    check(std::fabs(A.golfer.clubToHeadM - 0.90) < 1e-9, "the club pools when asked (grip to head = length − 4 cm)");
    check(A.golfer.hasScale && std::fabs(A.golfer.scale[sk::GUpperArm] - 1.03) < 1e-9,
          "the golfer's values are medians — the outlier swing does not move them");
    check(A.golfer.hasSym && std::fabs(A.golfer.sym[0] - 0.011) < 1e-9, "the shoulder/hip offsets pool when asked");
    check(A.golfer.hasGrip, "…and the grip");
    check(P.epochs.size() == 2 && P.epochs[0].swings.size() == 3 && P.epochs[1].swings.size() == 2,
          "a DTL camera moved 0.5 m starts a new camera epoch");
    sk::SkeletonCalib c1, c4;
    check(sk::calibFor(P, "swing_0001", c1) && sk::calibFor(P, "swing_0004", c4), "each swing finds its calib");
    check(std::fabs(c1.cam[2] - 3.00) < 1e-9 && std::fabs(c4.cam[2] - 3.505) < 1e-9, "…with its own epoch's cameras");
    sk::SkeletonCalib cx;
    check(!sk::calibFor(P, "swing_0099", cx), "a swing not in the pool gets none");
    const sk::SessionPool Q = sk::sessionPoolFromJson(sk::sessionPoolToJson(A));
    sk::SkeletonCalib q4, a4;
    check(Q.valid && sk::calibFor(Q, "swing_0004", q4) && sk::calibFor(A, "swing_0004", a4)
              && std::fabs(q4.cam[2] - a4.cam[2]) < 1e-12 && q4.hasScale
              && std::fabs(q4.scale[sk::GUpperArm] - a4.scale[sk::GUpperArm]) < 1e-12,
          "the pool survives its JSON round trip");
    const sk::SessionPool one = sk::poolSkeletons({ sw.front() });
    check(!one.valid, "one swing makes no pool");

    std::printf("=== %s (%d failure%s) ===\n", g_fail ? "FAILED" : "PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
