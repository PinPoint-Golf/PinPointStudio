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
//   - a joint's tier is the persisted one;
//   - MOTION ANNOTATIONS (swing_3d_annotations_design.md §7.1): the annotation anchors are the
//     FK joints in the scene frame; the club's ends are the drawn club's; P-positions read back in
//     time order and window-relative; the Address → Finish window; the match cameras project
//     exactly as the fit's pinhole does and agree with their own Qt camera pose; the fused plane's
//     quad lies in the plane, through the clubhead at impact;
//   - THE END AT P8: frames past P8 are dropped, the figure holds its P8 pose past it, P-positions
//     after it are not offered; a swing without a fused shaft plane is face-on only.

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

#include <QQuaternion>
#include <QVector3D>

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
    check(drv.faceOnOnly(), "two views but no fused shaft plane on file: the swing is face-on only");

    // ── the time domain: a RE-ANALYSED swing is written with the real clock.t0 while its
    // frame times are already window-relative. Subtracting t0 from them again wrote −98 s times
    // the playhead never reached (4 July library, 26 Sept). Written with a real t0, the frames
    // must stay where they were — and the figure must move with the playhead.
    {
        const qint64 t0 = 98113148038;
        const QJsonObject j = sk::skeleton3dToJson(r, t0, 1);
        const qint64 first = qint64(j.value(QStringLiteral("frames")).toArray().first().toObject()
                                        .value(QStringLiteral("t")).toDouble());
        check(first == 100000, "relative frame times survive a writer given the real clock t0");
        const QString d2 = tmp.filePath(QStringLiteral("reanalysed"));
        QDir().mkpath(d2);
        QJsonObject root;
        root[QStringLiteral("clock")] = QJsonObject { { QStringLiteral("t0_us"), double(t0) } };
        root[QStringLiteral("analysis")] = QJsonObject { { QStringLiteral("skeleton3d"), j } };
        QFile f(QDir(d2).filePath(QStringLiteral("swing.json")));
        f.open(QIODevice::WriteOnly);
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        f.close();
        SwingRigDriver d3;
        d3.loadNow(d2);
        d3.setPositionUs(100000);
        const QQuaternion a = d3.localRotation(sk::ybot::LeftForeArm, d3.revision());
        d3.setPositionUs(300000);
        const QQuaternion b = d3.localRotation(sk::ybot::LeftForeArm, d3.revision());
        check(d3.startUs() == 100000 && d3.endUs() == 300000, "…and read back in the playhead's domain");
        check(std::fabs(QQuaternion::dotProduct(a, b)) < 0.99f, "the figure moves as the playhead moves");
    }

    // ── motion annotations ──
    {
        sk::FitResult a = r;
        a.cam.fF = 1000; a.cam.pF = 0; a.cam.rF = 0;
        a.cam.cD = { 3.0, 2.0, 0.2 }; a.cam.psiD = 180 * sk::kDeg; a.cam.pD = 5 * sk::kDeg;
        a.cam.fD = 900; a.cam.rD = 3 * sk::kDeg;
        // A world point 0.5 m right of the face-on axis, 5 m out, at camera height — it must image
        // 100 px right of centre (f·x/z) — carried to the scene as the "ball".
        a.ballWorld = { 0.5, 5.0, 0.0 };
        const QJsonArray phases {
            QJsonObject { { QStringLiteral("phase"), 0 }, { QStringLiteral("t_us"), 150000 } },
            QJsonObject { { QStringLiteral("phase"), 5 }, { QStringLiteral("t_us"), 200000 } },
            QJsonObject { { QStringLiteral("phase"), 14 }, { QStringLiteral("t_us"), 250000 } },   // P8
            QJsonObject { { QStringLiteral("phase"), 7 }, { QStringLiteral("t_us"), 280000 } } };
        const QJsonObject club {
            { QStringLiteral("frameWidth"), 1440 }, { QStringLiteral("frameHeight"), 1080 },
            { QStringLiteral("positions"), QJsonArray {
                  QJsonObject { { QStringLiteral("p"), 7 }, { QStringLiteral("t_us"), 200000 }, { QStringLiteral("source"), 1 } },
                  QJsonObject { { QStringLiteral("p"), 1 }, { QStringLiteral("t_us"), 100000 }, { QStringLiteral("source"), 0 } },
                  QJsonObject { { QStringLiteral("p"), 9 }, { QStringLiteral("t_us"), 290000 }, { QStringLiteral("source"), 0 } } } } };
        // club3d's down plane: a 60° plane, as the camera-level frame gives it.
        const double inc = 60 * sk::kDeg;
        const QJsonObject club3d { { QStringLiteral("planes"), QJsonObject { { QStringLiteral("down"), QJsonObject {
            { QStringLiteral("normal"), QJsonArray { 0.0, -std::sin(inc), std::cos(inc) } },
            { QStringLiteral("inclDeg"), 60.0 }, { QStringLiteral("offered"), true } } } } } };
        const QString da = tmp.filePath(QStringLiteral("annot"));
        QDir().mkpath(da);
        QJsonObject root;
        root[QStringLiteral("clock")] = QJsonObject { { QStringLiteral("t0_us"), 0 } };
        root[QStringLiteral("streams")] = QJsonArray {
            QJsonObject { { QStringLiteral("alias"), QStringLiteral("FaceOn") }, { QStringLiteral("kind"), QStringLiteral("video") },
                          { QStringLiteral("encoded"), QJsonObject { { QStringLiteral("width"), 1440 }, { QStringLiteral("height"), 1080 } } } },
            QJsonObject { { QStringLiteral("alias"), QStringLiteral("DTL") }, { QStringLiteral("kind"), QStringLiteral("video") },
                          { QStringLiteral("encoded"), QJsonObject { { QStringLiteral("width"), 1280 }, { QStringLiteral("height"), 720 } } } } };
        root[QStringLiteral("analysis")] = QJsonObject {
            { QStringLiteral("skeleton3d"), sk::skeleton3dToJson(a, 0, 1) }, { QStringLiteral("phases"), phases },
            { QStringLiteral("club"), club }, { QStringLiteral("club3d"), club3d } };
        QFile f(QDir(da).filePath(QStringLiteral("swing.json")));
        f.open(QIODevice::WriteOnly);
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        f.close();

        SwingRigDriver dv;
        check(dv.loadNow(da), "annotations: the swing loads");
        const SwingAnnotTrack *T = dv.annotTrack();
        check(T && T->t.size() == 3, "…with an annotation track, one entry per fitted frame");

        // The scene frame, rebuilt here from its definition (driver header): about Z by −stanceYaw,
        // then −90° about X, the display origin subtracted first.
        const sk::Q rot = (sk::Q::axisAngle({ 1, 0, 0 }, -sk::kPi / 2.0) * sk::Q::axisAngle({ 0, 0, 1 }, -a.stanceYawRad)).normalized();
        auto scene = [&](const sk::V3 &p) { const sk::V3 v = rot.rotate(p - a.displayOrigin); return QVector3D(float(v.x), float(v.y), float(v.z)); };
        double worstJ = 0, worstC = 0;
        for (size_t i = 0; T && i < T->t.size(); ++i) {
            const auto &J = a.joints[i];
            const std::pair<int, int> m[] = { { SwingAnnotTrack::LWrist, sk::ybot::LeftHand }, { SwingAnnotTrack::RShoulder, sk::ybot::RightArm },
                                              { SwingAnnotTrack::LAnkle, sk::ybot::LeftFoot }, { SwingAnnotTrack::RHip, sk::ybot::RightUpLeg },
                                              { SwingAnnotTrack::Head, sk::ybot::Head } };
            for (const auto &pr : m) worstJ = std::max(worstJ, double((T->p[i][size_t(pr.first)] - scene(J[size_t(pr.second)])).length()));
            const sk::V3 butt = a.grip[i] - a.shaftDir[i] * 0.04, head = butt + a.shaftDir[i] * a.clubLengthM;
            worstC = std::max(worstC, double((T->p[i][SwingAnnotTrack::ClubHead] - scene(head)).length()));
            worstC = std::max(worstC, double((T->p[i][SwingAnnotTrack::Grip] - scene(a.grip[i])).length()));
        }
        std::printf("      anchors vs FK joints: %.2e m; club ends: %.2e m\n", worstJ, worstC);
        check(worstJ < 1e-5, "the anchors are the fitted joints (FK), in the scene frame");
        // The document rounds the grip and shaft direction to 1e-4 (skeleton3d_json.h), so the club's
        // ends agree with the unrounded fit to that — ~0.1 mm over a 0.95 m club.
        check(worstC < 2e-4, "the clubhead and grip are the drawn club's ends");
        check(T && T->tier[0][SwingAnnotTrack::RWrist] == sk::TierInferred, "…with the joint's own tier");

        check(dv.positionCount() == 2, "two P-positions read — the one after P8 is not offered");
        check(dv.positionP(0) == 1 && dv.positionP(1) == 7, "…in time order");
        check(dv.positionTimeUs(1) == 200000 && dv.positionSource(1) == 1, "…with their time and source");
        check((dv.positionHead(1, 0) - T->p[1][SwingAnnotTrack::ClubHead]).length() < 1e-5f, "a P-position's head is the club at that instant");

        dv.setPositionUs(120000);
        check(!dv.inSwingWindow(), "before Address: outside the swing window");
        dv.setPositionUs(200000);
        check(dv.inSwingWindow(), "Address → Finish: inside");
        dv.setPositionUs(-1);
        check(!dv.inSwingWindow() && dv.annotTimeUs() < 0, "nothing playing: no annotation instant");

        // The end at P8: the display ends there, the figure holds its P8 pose past it.
        check(dv.endUs() == 250000, "the 3-D swing ends at P8");
        check(!dv.faceOnOnly(), "two views and a fused shaft plane: every preset is offered");
        dv.setPositionUs(250000);
        const QVector3D atP8 = dv.rootPosition();
        const QQuaternion armP8 = dv.localRotation(sk::ybot::LeftForeArm, dv.revision());
        check(!dv.heldAtEnd(), "at P8 the figure is not yet held");
        dv.setPositionUs(300000);
        check(dv.heldAtEnd() && dv.annotTimeUs() == 250000, "past P8 it is held, and the annotations stop at P8");
        check((dv.rootPosition() - atP8).length() < 1e-6f
              && std::fabs(QQuaternion::dotProduct(armP8, dv.localRotation(sk::ybot::LeftForeArm, dv.revision()))) > 0.999999f,
              "…in its P8 pose");
        dv.setPositionUs(-1);

        // The match cameras.
        check(dv.cameraAvailable(0) && dv.cameraAvailable(1), "both fitted cameras, with their image sizes");
        const QPointF uv = dv.projectScene(0, dv.ballPosition());
        std::printf("      face-on image of the test point: (%.3f, %.3f)\n", uv.x(), uv.y());
        check(std::fabs(uv.x() - 820.0) < 1e-2 && std::fabs(uv.y() - 540.0) < 1e-2, "the face-on camera images a point as the fit's pinhole does");
        const double fov = dv.cameraFovDeg(0);
        check(std::fabs(fov - 2.0 * std::atan(540.0 / 1000.0) / sk::kDeg) < 1e-6, "the face-on field of view is 2·atan(H/2f)");
        // The Qt camera pose sees the same image: a point's camera-local coordinates through the
        // camera's own rotation give the pixel projectScene gives, in both views.
        for (int v = 0; v < 2; ++v) {
            const QVector3D P = dv.ballPosition() + QVector3D(0.2f, 0.9f, -0.1f) * float(v);
            const QVector3D L = dv.cameraRotation(v).conjugated().rotatedVector(P - dv.cameraPosition(v));
            const double f = 0.5 * (v == 0 ? 1080.0 : 720.0) / std::tan(0.5 * dv.cameraFovDeg(v) * sk::kDeg);
            const double u = 0.5 * (v == 0 ? 1440.0 : 1280.0) + f * L.x() / -L.z();
            const double w = 0.5 * (v == 0 ? 1080.0 : 720.0) - f * L.y() / -L.z();
            const QPointF q = dv.projectScene(v, P);
            std::printf("      view %d: Qt camera (%.2f, %.2f) vs pinhole (%.2f, %.2f)\n", v, u, w, q.x(), q.y());
            check(L.z() < 0 && std::hypot(u - q.x(), w - q.y()) < 0.5, v == 0 ? "the face-on match camera sees what the pinhole sees"
                                                                         : "the DTL match camera sees what the pinhole sees");
        }

        // The plane: through the clubhead at impact, spanning the plane.
        check(dv.planeAvailable() && std::fabs(dv.planeInclDeg() - 60.0) < 1e-9, "the offered down plane is read");
        const sk::V3 nw { 0.0, -std::sin(inc), std::cos(inc) };
        const QVector3D n = scene(nw + a.displayOrigin);     // a direction: the origin cancels
        const QVector3D H = T->sample(200000, SwingAnnotTrack::ClubHead);
        double off = 0;
        for (const QVector3D &c : T->planeQuad) off = std::max(off, double(std::fabs(QVector3D::dotProduct(n.normalized(), c - H))));
        std::printf("      plane corners off the plane: %.2e m\n", off);
        check(off < 1e-5, "the quad lies in the plane, through the clubhead at impact");
    }

    std::printf("=== %s (%d failure%s) ===\n", g_fail ? "FAILED" : "PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
