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
#include <QObject>
#include <QQmlEngine>
#include <QQuaternion>
#include <QTimer>

// BodyPoseAdapter — converts 17 COCO keypoints from a face-on VideoController
// into per-bone parent-local quaternions for BodyVizView's kinematic chain.
//
// Design contract:
//   poseSource   — any QObject exposing poseKeypoints (QVariantList) and
//                  poseKeypointsChanged() signal (VideoController satisfies this).
//   active       — true when both hip keypoints and both shoulder keypoints
//                  meet the confidence threshold; BodyVizView falls back to
//                  rest-pose quaternions when false.
//
// Threading:
//   poseKeypointsChanged() only sets m_dirty = true — no work done on the
//   signal.  The 60 Hz QTimer tick reads property("poseKeypoints") once to
//   get the latest value, so multiple queued signals collapse to one read.

class BodyPoseAdapter : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject* poseSource     READ poseSource     WRITE setPoseSource     NOTIFY poseSourceChanged)
    Q_PROPERTY(bool     mirroredSource READ mirroredSource WRITE setMirroredSource NOTIFY mirroredSourceChanged)
    Q_PROPERTY(bool     active         READ active         NOTIFY activeChanged)

    // Parent-local quaternions — assign directly to the corresponding Node.rotation
    // in BodyVizView.  Identity when the bone is not driven (confidence too low).
    Q_PROPERTY(QQuaternion hipsRotation         READ hipsRotation         NOTIFY rotationsChanged)
    Q_PROPERTY(QQuaternion leftUpLegRotation    READ leftUpLegRotation    NOTIFY rotationsChanged)
    Q_PROPERTY(QQuaternion leftLegRotation      READ leftLegRotation      NOTIFY rotationsChanged)
    Q_PROPERTY(QQuaternion rightUpLegRotation   READ rightUpLegRotation   NOTIFY rotationsChanged)
    Q_PROPERTY(QQuaternion rightLegRotation     READ rightLegRotation     NOTIFY rotationsChanged)
    Q_PROPERTY(QQuaternion leftArmRotation      READ leftArmRotation      NOTIFY rotationsChanged)
    Q_PROPERTY(QQuaternion leftForeArmRotation  READ leftForeArmRotation  NOTIFY rotationsChanged)
    Q_PROPERTY(QQuaternion rightArmRotation     READ rightArmRotation     NOTIFY rotationsChanged)
    Q_PROPERTY(QQuaternion rightForeArmRotation READ rightForeArmRotation NOTIFY rotationsChanged)
    Q_PROPERTY(QQuaternion headRotation         READ headRotation         NOTIFY rotationsChanged)

    // Per-segment visibility — false when the required keypoints lack confidence.
    // Feet are never exposed (no COCO keypoint drives them).
    Q_PROPERTY(bool leftUpLegVisible    READ leftUpLegVisible    NOTIFY rotationsChanged)
    Q_PROPERTY(bool leftLegVisible      READ leftLegVisible      NOTIFY rotationsChanged)
    Q_PROPERTY(bool rightUpLegVisible   READ rightUpLegVisible   NOTIFY rotationsChanged)
    Q_PROPERTY(bool rightLegVisible     READ rightLegVisible     NOTIFY rotationsChanged)
    Q_PROPERTY(bool leftArmVisible      READ leftArmVisible      NOTIFY rotationsChanged)
    Q_PROPERTY(bool leftForeArmVisible  READ leftForeArmVisible  NOTIFY rotationsChanged)
    Q_PROPERTY(bool rightArmVisible     READ rightArmVisible     NOTIFY rotationsChanged)
    Q_PROPERTY(bool rightForeArmVisible READ rightForeArmVisible NOTIFY rotationsChanged)
    Q_PROPERTY(bool headVisible         READ headVisible         NOTIFY rotationsChanged)

public:
    explicit BodyPoseAdapter(QObject *parent = nullptr);

    QObject*    poseSource() const      { return m_poseSource; }
    void        setPoseSource(QObject *src);
    bool        mirroredSource() const  { return m_mirroredSource; }
    void        setMirroredSource(bool m) { if (m_mirroredSource == m) return; m_mirroredSource = m; emit mirroredSourceChanged(); }
    bool        active() const          { return m_active; }

    QQuaternion hipsRotation()          const { return m_hips; }
    QQuaternion leftUpLegRotation()     const { return m_leftUpLeg; }
    QQuaternion leftLegRotation()       const { return m_leftLeg; }
    QQuaternion rightUpLegRotation()    const { return m_rightUpLeg; }
    QQuaternion rightLegRotation()      const { return m_rightLeg; }
    QQuaternion leftArmRotation()       const { return m_leftArm; }
    QQuaternion leftForeArmRotation()   const { return m_leftForeArm; }
    QQuaternion rightArmRotation()      const { return m_rightArm; }
    QQuaternion rightForeArmRotation()  const { return m_rightForeArm; }
    QQuaternion headRotation()          const { return m_head; }

    bool leftUpLegVisible()    const { return m_leftUpLegVisible; }
    bool leftLegVisible()      const { return m_leftLegVisible; }
    bool rightUpLegVisible()   const { return m_rightUpLegVisible; }
    bool rightLegVisible()     const { return m_rightLegVisible; }
    bool leftArmVisible()      const { return m_leftArmVisible; }
    bool leftForeArmVisible()  const { return m_leftForeArmVisible; }
    bool rightArmVisible()     const { return m_rightArmVisible; }
    bool rightForeArmVisible() const { return m_rightForeArmVisible; }
    bool headVisible()         const { return m_headVisible; }

signals:
    void poseSourceChanged();
    void mirroredSourceChanged();
    void activeChanged();
    void rotationsChanged();

private slots:
    void onPoseKeypointsChanged();
    void onTimerTick();

private:
    // Returns Z-axis world quaternion aligning bone +Y with direction A→B in
    // image coords (x right, y down → world x right, y up).  *ok = false if
    // the two points are coincident.
    QQuaternion zRot(float ax, float ay, float bx, float by, bool *ok = nullptr) const;

    static QQuaternion slerpStep(const QQuaternion &cur, const QQuaternion &tgt, float alpha);

    QObject    *m_poseSource     = nullptr;
    QTimer      m_timer;
    bool        m_dirty          = false;
    bool        m_active         = false;
    bool        m_mirroredSource = true;   // default: webcam (mirrored) convention

    // Precomputed inverse of the constant (rest-pose) portion of each bone's
    // parent-chain quaternion.  Derived from BodyVizView kinematic-chain values;
    // see constructor for derivation.
    QQuaternion m_leftArmParentInv;
    QQuaternion m_rightArmParentInv;
    QQuaternion m_headParentInv;

    // Current (slerped) parent-local rotations exposed to QML
    QQuaternion m_hips         { 1,0,0,0 };
    // UpLeg rest pose ≈ 180° around Z so bone-local +Y points world-down
    QQuaternion m_leftUpLeg    { -0.0030f, 0.f, -0.0064f, 1.0000f };
    QQuaternion m_leftLeg      { 1,0,0,0 };
    QQuaternion m_rightUpLeg   {  0.0031f, 0.f, -0.0063f, 1.0000f };
    QQuaternion m_rightLeg     { 1,0,0,0 };
    // Arms pre-seeded to rest-pose so the T-pose is correct before first activation
    QQuaternion m_leftArm      { 0.9948f, -0.0105f, -0.0011f,  0.1012f };
    QQuaternion m_leftForeArm  { 1,0,0,0 };
    QQuaternion m_rightArm     { 0.9948f, -0.0105f,  0.0011f, -0.1011f };
    QQuaternion m_rightForeArm { 1,0,0,0 };
    QQuaternion m_head         { 1,0,0,0 };

    // Target parent-local rotations (latest from keypoints, not yet slerped)
    QQuaternion m_tHips         { 1,0,0,0 };
    QQuaternion m_tLeftUpLeg    { -0.0030f, 0.f, -0.0064f, 1.0000f };
    QQuaternion m_tLeftLeg      { 1,0,0,0 };
    QQuaternion m_tRightUpLeg   {  0.0031f, 0.f, -0.0063f, 1.0000f };
    QQuaternion m_tRightLeg     { 1,0,0,0 };
    QQuaternion m_tLeftArm      { 0.9948f, -0.0105f, -0.0011f,  0.1012f };
    QQuaternion m_tLeftForeArm  { 1,0,0,0 };
    QQuaternion m_tRightArm     { 0.9948f, -0.0105f,  0.0011f, -0.1011f };
    QQuaternion m_tRightForeArm { 1,0,0,0 };
    QQuaternion m_tHead         { 1,0,0,0 };

    // Per-segment keypoint confidence flags (updated each time keypoints are read)
    bool m_leftUpLegVisible    = true;
    bool m_leftLegVisible      = true;
    bool m_rightUpLegVisible   = true;
    bool m_rightLegVisible     = true;
    bool m_leftArmVisible      = true;
    bool m_leftForeArmVisible  = true;
    bool m_rightArmVisible     = true;
    bool m_rightForeArmVisible = true;
    bool m_headVisible         = true;
};
