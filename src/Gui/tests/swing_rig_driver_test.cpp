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

// SwingRigDriver (src/Gui/swing3d/) against a swing document it did not write: a hand-built
// skeleton3d block, written through the production writer (skeleton3d_json.h) into a temporary
// swing directory. Checks:
//   - no skeleton3d on the shot ⇒ not available, and a reason a person can read;
//   - a zero pose reads back as the rig's rest (+ neutral) rotations;
//   - the playhead interpolates (an elbow at 0° and 90° is at 45° halfway);
//   - the root lands in the SCENE frame (the stance yaw and the axis swap applied);
//   - a joint's tier is the persisted one.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "swing_rig_driver.h"
#include "../../Analysis/skeleton3d/skeleton3d_json.h"

namespace sk = pinpoint::skeleton3d;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static int dof(const char *name)
{
    const sk::Rig &R = sk::rig();
    for (int k = 0; k < R.dofCount(); ++k)
        if (std::strcmp(R.dofs[size_t(k)].name, name) == 0) return k;
    return -1;
}

static bool writeDoc(const QString &dir, const QJsonObject &analysis)
{
    QJsonObject root;
    root[QStringLiteral("clock")] = QJsonObject { { QStringLiteral("t0_us"), 0 } };
    root[QStringLiteral("analysis")] = analysis;
    QFile f(QDir(dir).filePath(QStringLiteral("swing.json")));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return true;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("=== swing_rig_driver ===\n");
    QTemporaryDir tmp;
    const sk::Rig &R = sk::rig();

    // ── no skeleton ──
    {
        const QString d = tmp.filePath(QStringLiteral("none"));
        QDir().mkpath(d);
        writeDoc(d, QJsonObject { { QStringLiteral("phases"), QJsonArray {} } });
        SwingRigDriver drv;
        check(!drv.loadNow(d), "a shot with no skeleton3d is not available");
        std::printf("      reason: %s\n", qPrintable(drv.reason()));
        check(!drv.reason().isEmpty(), "…and says why");
    }

    // ── a three-frame skeleton ──
    sk::FitResult r;
    r.valid = true;
    r.dtlUsed = true;
    r.scale.fill(1.0);
    r.scaleGlobal = 1.0;
    r.stanceYawRad = 30 * sk::kDeg;
    r.displayOrigin = { 0.1, 2.0, -1.0 };
    r.clubLengthM = 0.95;
    // A "ball" one metre along the stance axis from the origin must land on scene +X.
    r.ballValid = true;
    r.ballWorld = { 0.1 + std::cos(30 * sk::kDeg), 2.0 + std::sin(30 * sk::kDeg), -1.0 };
    const int kElbow = dof("lElbow.flex");
    for (int i = 0; i < 3; ++i) {
        r.t_us.push_back(100000 * (i + 1));
        std::vector<double> th(size_t(R.dofCount()), 0.0);
        th[0] = 0.1; th[1] = 2.0; th[2] = 0.0;        // the hips 1 m above the floor at z −1
        th[size_t(kElbow)] = i == 0 ? 0.0 : 90 * sk::kDeg;
        r.theta.push_back(th);
        sk::Pose p;
        std::array<double, sk::GroupCount> sc; sc.fill(1.0);
        sk::forwardKinematics(R, th.data(), sc.data(), p);
        std::array<sk::V3, sk::Rig::N> joints {};
        for (int j = 0; j < sk::Rig::N; ++j) joints[size_t(j)] = p.pos[j];
        r.joints.push_back(joints);
        std::array<uint8_t, sk::Rig::N> tier; tier.fill(sk::TierMeasured);
        tier[sk::ybot::RightHand] = sk::TierInferred;
        r.tier.push_back(tier);
        r.sigmaM.push_back({});
        r.flags.push_back(0);
        r.grip.push_back(p.pos[sk::ybot::LeftHand]);
        r.shaftDir.push_back({ 0, 0, -1 });
        r.shaftTier.push_back(3);
    }
    const QString d = tmp.filePath(QStringLiteral("swing"));
    QDir().mkpath(d);
    writeDoc(d, QJsonObject { { QStringLiteral("skeleton3d"), sk::skeleton3dToJson(r, 0, 1) } });
    SwingRigDriver drv;
    check(drv.loadNow(d), "a shot with a skeleton3d is available");
    check(drv.twoViews(), "…from two views");

    // Frame 1 (t = 100 ms): zero pose ⇒ each non-root joint's local rotation is restQ·N.
    drv.setPositionUs(100000);
    double worst = 0;
    for (int j = 1; j < sk::Rig::N; ++j) {
        const sk::Q q = (R.restQ[j] * R.neutral[j]).normalized();
        const QQuaternion e(float(q.w), float(q.x), float(q.y), float(q.z));
        const QQuaternion g = drv.localRotation(j, drv.revision());
        worst = std::max(worst, 1.0 - std::fabs(double(QQuaternion::dotProduct(e, g))));
    }
    check(worst < 1e-5, "a zero pose reads back as the rest (+ neutral) rotations");

    // Halfway between 0° and 90° of elbow flexion: the forearm's local rotation is 45° from rest.
    drv.setPositionUs(150000);
    {
        const sk::Q q = (R.restQ[sk::ybot::LeftForeArm] * R.neutral[sk::ybot::LeftForeArm]).normalized();
        const QQuaternion rest(float(q.w), float(q.x), float(q.y), float(q.z));
        const QQuaternion g = drv.localRotation(sk::ybot::LeftForeArm, drv.revision());
        const double ang = 2.0 * std::acos(std::min(1.0, std::fabs(double(QQuaternion::dotProduct(rest, g))))) / sk::kDeg;
        std::printf("      elbow at the midpoint: %.2f°\n", ang);
        check(std::fabs(ang - 45.0) < 0.5, "the playhead interpolates (45° halfway from 0° to 90°)");
    }

    // The root in the scene: world (0.1, 2.0, 0) − origin (0.1, 2.0, −1) = (0, 0, 1) world, which is
    // straight UP — scene +Y, whatever the stance yaw.
    {
        const QVector3D p = drv.rootPosition();
        std::printf("      root in the scene: (%.3f, %.3f, %.3f)\n", p.x(), p.y(), p.z());
        check(std::fabs(p.x()) < 1e-4 && std::fabs(p.y() - 1.0f) < 1e-4 && std::fabs(p.z()) < 1e-4,
              "world up is scene +Y and the origin is subtracted");
    }
    {
        const QVector3D b = drv.ballPosition();
        std::printf("      stance-axis point in the scene: (%.3f, %.3f, %.3f)\n", b.x(), b.y(), b.z());
        check(std::fabs(b.x() - 1.0f) < 1e-4 && std::fabs(b.y()) < 1e-4 && std::fabs(b.z()) < 1e-4,
              "the stance axis (trail → lead heel) is scene +X");
    }
    check(drv.tier(sk::ybot::RightHand, drv.revision()) == sk::TierInferred, "a joint's tier is the persisted one");
    check(drv.tier(sk::ybot::Hips, drv.revision()) == sk::TierMeasured, "…for every joint");
    check(drv.shaftTier() == 3, "the shaft's tier reads back");
    check(std::fabs(drv.clubLengthM() - 0.95) < 1e-6, "the club length reads back");

    std::printf("=== %s (%d failure%s) ===\n", g_fail ? "FAILED" : "PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
