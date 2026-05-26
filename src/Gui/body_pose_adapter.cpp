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

#include "body_pose_adapter.h"
#include <QVariantList>
#include <QVariantMap>
#include <cmath>

static constexpr float kMinScore   = 0.30f;
static constexpr float kSlerpAlpha = 0.20f;  // per 60 Hz tick ≈ 100 ms settling
static constexpr int   kIntervalMs = 1000 / 60;

// COCO keypoint indices
enum KP {
    Nose = 0,
    LShoulder = 5, RShoulder = 6,
    LElbow = 7,    RElbow    = 8,
    LWrist = 9,    RWrist    = 10,
    LHip   = 11,   RHip      = 12,
    LKnee  = 13,   RKnee     = 14,
    LAnkle = 15,   RAnkle    = 16,
};

// ── Constructor ───────────────────────────────────────────────────────────────

BodyPoseAdapter::BodyPoseAdapter(QObject *parent) : QObject(parent)
{
    // Precompute inverse of the constant (rest-pose) parent-chain quaternions
    // taken directly from BodyVizView.qml:
    //   spineNode  rotation: Qt.quaternion(0.9982, -0.0607,  0,      0     )
    //   spine2Node rotation: Qt.quaternion(0.9983,  0.0577,  0,      0     )
    //   (spine1Node is identity — omitted)
    //   leftShoulderNode:    Qt.quaternion(-0.4398, -0.4538, -0.5448, 0.5511)
    //   rightShoulderNode:   Qt.quaternion( 0.4398,  0.4538, -0.5448, 0.5511)
    //   neckNode is identity — omitted
    //
    // For each arm or head node, localRot = inv(bodyLean * constParent) * worldTarget
    //   = inv(constParent) * inv(bodyLean) * worldTarget
    // The inv(constParent) part is pre-baked here; inv(bodyLean) is applied each tick.
    QQuaternion spineRest  ( 0.9982f, -0.0607f,  0.f,      0.f     );
    QQuaternion spine2Rest ( 0.9983f,  0.0577f,  0.f,      0.f     );
    QQuaternion lShoulderR (-0.4398f, -0.4538f, -0.5448f,  0.5511f );
    QQuaternion rShoulderR ( 0.4398f,  0.4538f, -0.5448f,  0.5511f );

    QQuaternion spineChain = (spineRest * spine2Rest).normalized();
    m_leftArmParentInv  = (spineChain * lShoulderR).normalized().conjugated();
    m_rightArmParentInv = (spineChain * rShoulderR).normalized().conjugated();
    m_headParentInv     = spineChain.conjugated();

    m_timer.setInterval(kIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &BodyPoseAdapter::onTimerTick);
}

// ── poseSource wiring ─────────────────────────────────────────────────────────

void BodyPoseAdapter::setPoseSource(QObject *src)
{
    if (m_poseSource == src) return;
    if (m_poseSource)
        disconnect(m_poseSource, nullptr, this, nullptr);

    m_poseSource = src;

    if (m_poseSource) {
        // Only set the dirty flag — no work on the signal thread.
        connect(m_poseSource, SIGNAL(poseKeypointsChanged()),
                this, SLOT(onPoseKeypointsChanged()));
        m_timer.start();
    } else {
        m_timer.stop();
        if (m_active) { m_active = false; emit activeChanged(); }
    }
    emit poseSourceChanged();
}

void BodyPoseAdapter::onPoseKeypointsChanged()
{
    m_dirty = true;
}

// ── Keypoint helpers ──────────────────────────────────────────────────────────

struct KpSet {
    float x[17]{}, y[17]{}, s[17]{};
    bool ok(int i)         const { return s[i] >= kMinScore; }
    bool ok2(int a, int b) const { return ok(a) && ok(b); }
};

static KpSet parseKps(const QVariantList &list)
{
    KpSet kp;
    for (int i = 0; i < 17 && i < list.size(); ++i) {
        const QVariantMap m = list.at(i).toMap();
        kp.x[i] = static_cast<float>(m.value(QStringLiteral("x")).toDouble());
        kp.y[i] = static_cast<float>(m.value(QStringLiteral("y")).toDouble());
        kp.s[i] = static_cast<float>(m.value(QStringLiteral("score")).toDouble());
    }
    return kp;
}

// ── Rotation math ─────────────────────────────────────────────────────────────

// Z-axis rotation quaternion that orients bone +Y toward the world direction A→B.
// Image coords: x increases right, y increases downward.
// World coords: x right, y up (so world Δy = −image Δy).
QQuaternion BodyPoseAdapter::zRot(float ax, float ay, float bx, float by, bool *ok) const
{
    float dx = m_mirroredSource ? (ax - bx) : (bx - ax);
    float dy = (ay - by);   // invert image y → world y
    float len = std::sqrt(dx*dx + dy*dy);
    if (len < 1e-4f) {
        if (ok) *ok = false;
        return QQuaternion(1,0,0,0);
    }
    dx /= len;  dy /= len;
    // angle of (dx, dy) from +Y axis (dy=1, dx=0 → angle=0 → identity)
    float half = std::atan2(dx, dy) * 0.5f;
    if (ok) *ok = true;
    return QQuaternion(std::cos(half), 0.f, 0.f, std::sin(half));
}

QQuaternion BodyPoseAdapter::slerpStep(const QQuaternion &cur,
                                        const QQuaternion &tgt, float alpha)
{
    return QQuaternion::slerp(cur, tgt, alpha).normalized();
}

// ── 60 Hz tick ────────────────────────────────────────────────────────────────

void BodyPoseAdapter::onTimerTick()
{
    if (m_dirty && m_poseSource) {
        m_dirty = false;

        QVariantList raw = m_poseSource->property("poseKeypoints").toList();
        if (raw.size() >= 17) {
            KpSet kp = parseKps(raw);

            // Active requires both hips and both shoulders with sufficient confidence.
            bool nowActive = kp.ok2(LHip, RHip) && kp.ok2(LShoulder, RShoulder);
            if (nowActive != m_active) { m_active = nowActive; emit activeChanged(); }

            if (m_active) {
                // Update per-segment visibility from current keypoint confidence.
                m_leftUpLegVisible    = kp.ok2(LHip,      LKnee);
                m_leftLegVisible      = kp.ok2(LKnee,     LAnkle);
                m_rightUpLegVisible   = kp.ok2(RHip,      RKnee);
                m_rightLegVisible     = kp.ok2(RKnee,     RAnkle);
                m_leftArmVisible      = kp.ok2(LShoulder,  LElbow);
                m_leftForeArmVisible  = kp.ok2(LElbow,     LWrist);
                m_rightArmVisible     = kp.ok2(RShoulder,  RElbow);
                m_rightForeArmVisible = kp.ok2(RElbow,     RWrist);
                m_headVisible         = kp.ok(Nose) && kp.ok2(LShoulder, RShoulder);

                float hipMx  = (kp.x[LHip]      + kp.x[RHip])      * 0.5f;
                float hipMy  = (kp.y[LHip]      + kp.y[RHip])      * 0.5f;
                float shMx   = (kp.x[LShoulder] + kp.x[RShoulder]) * 0.5f;
                float shMy   = (kp.y[LShoulder] + kp.y[RShoulder]) * 0.5f;

                // Body lean: hip-mid → shoulder-mid direction.
                // hipsNode parent is identity, so local == world.
                bool zok;
                QQuaternion hipsW = zRot(hipMx, hipMy, shMx, shMy, &zok);
                if (zok) m_tHips = hipsW;
                QQuaternion invHips = m_tHips.conjugated();

                // ── Legs ──────────────────────────────────────────────────────
                // World quats for thighs are needed to compute the relative shin rotation.
                QQuaternion lThighW = m_tHips;   // fallback: inherit body lean
                QQuaternion rThighW = m_tHips;

                if (kp.ok2(LHip, LKnee)) {
                    lThighW = zRot(kp.x[LHip], kp.y[LHip], kp.x[LKnee], kp.y[LKnee]);
                    m_tLeftUpLeg = invHips * lThighW;
                }
                if (kp.ok2(LKnee, LAnkle)) {
                    QQuaternion shinW = zRot(kp.x[LKnee], kp.y[LKnee], kp.x[LAnkle], kp.y[LAnkle]);
                    m_tLeftLeg = lThighW.conjugated() * shinW;
                }

                if (kp.ok2(RHip, RKnee)) {
                    rThighW = zRot(kp.x[RHip], kp.y[RHip], kp.x[RKnee], kp.y[RKnee]);
                    m_tRightUpLeg = invHips * rThighW;
                }
                if (kp.ok2(RKnee, RAnkle)) {
                    QQuaternion shinW = zRot(kp.x[RKnee], kp.y[RKnee], kp.x[RAnkle], kp.y[RAnkle]);
                    m_tRightLeg = rThighW.conjugated() * shinW;
                }

                // ── Arms ──────────────────────────────────────────────────────
                // localRot = inv(constParent) * inv(bodyLean) * worldTarget
                // Forearm is relative to upper arm (no constant parent needed).
                QQuaternion lArmW = m_tHips;   // fallback
                QQuaternion rArmW = m_tHips;

                if (kp.ok2(LShoulder, LElbow)) {
                    lArmW = zRot(kp.x[LShoulder], kp.y[LShoulder], kp.x[LElbow], kp.y[LElbow]);
                    m_tLeftArm = m_leftArmParentInv * invHips * lArmW;
                }
                if (kp.ok2(LElbow, LWrist)) {
                    QQuaternion faW = zRot(kp.x[LElbow], kp.y[LElbow], kp.x[LWrist], kp.y[LWrist]);
                    m_tLeftForeArm = lArmW.conjugated() * faW;
                }

                if (kp.ok2(RShoulder, RElbow)) {
                    rArmW = zRot(kp.x[RShoulder], kp.y[RShoulder], kp.x[RElbow], kp.y[RElbow]);
                    m_tRightArm = m_rightArmParentInv * invHips * rArmW;
                }
                if (kp.ok2(RElbow, RWrist)) {
                    QQuaternion faW = zRot(kp.x[RElbow], kp.y[RElbow], kp.x[RWrist], kp.y[RWrist]);
                    m_tRightForeArm = rArmW.conjugated() * faW;
                }

                // ── Head ──────────────────────────────────────────────────────
                if (kp.ok2(Nose, LShoulder) && kp.ok2(Nose, RShoulder)) {
                    QQuaternion headW = zRot(shMx, shMy, kp.x[Nose], kp.y[Nose]);
                    m_tHead = m_headParentInv * invHips * headW;
                }
            }
        }
    }

    // Slerp all current rotations toward their targets each tick.
    m_hips         = slerpStep(m_hips,         m_tHips,         kSlerpAlpha);
    m_leftUpLeg    = slerpStep(m_leftUpLeg,    m_tLeftUpLeg,    kSlerpAlpha);
    m_leftLeg      = slerpStep(m_leftLeg,      m_tLeftLeg,      kSlerpAlpha);
    m_rightUpLeg   = slerpStep(m_rightUpLeg,   m_tRightUpLeg,   kSlerpAlpha);
    m_rightLeg     = slerpStep(m_rightLeg,     m_tRightLeg,     kSlerpAlpha);
    m_leftArm      = slerpStep(m_leftArm,      m_tLeftArm,      kSlerpAlpha);
    m_leftForeArm  = slerpStep(m_leftForeArm,  m_tLeftForeArm,  kSlerpAlpha);
    m_rightArm     = slerpStep(m_rightArm,     m_tRightArm,     kSlerpAlpha);
    m_rightForeArm = slerpStep(m_rightForeArm, m_tRightForeArm, kSlerpAlpha);
    m_head         = slerpStep(m_head,         m_tHead,         kSlerpAlpha);

    emit rotationsChanged();
}
