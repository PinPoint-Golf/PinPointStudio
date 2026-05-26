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
#include <QQuaternion>

// ArmBoneController — drives Y-bot GLB arm bones from IMU quaternion data.
//
// setup(sceneRoot) is called once when RuntimeLoader finishes loading ybot.glb.
// It traverses the scene tree via QObject::children() (not QML's children list,
// which is a separate QQmlListProperty not populated for RuntimeLoader nodes),
// caches pointers to the six arm bones, and reads rest-pose rotations.
//
// apply() / resetInactive() are called by the QML Timer at ~60 Hz.

class ArmBoneController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool ready    READ ready    NOTIFY readyChanged)
    Q_PROPERTY(bool attempted READ attempted NOTIFY attemptedChanged)

public:
    explicit ArmBoneController(QObject *parent = nullptr);

    bool ready()    const { return m_ready; }
    bool attempted() const { return m_attempted; }

    // Call once after RuntimeLoader reports Success; sceneRoot = loader.scene.
    Q_INVOKABLE void setup(QObject *sceneRoot);

    // Write the RuntimeLoader status integer to stderr — callable from QML so
    // every status change is visible even before setup() is reached.
    Q_INVOKABLE void logStatus(int status);

    // Drive the active (lead) arm.
    // rightHanded=true  → Left arm driven  (lead arm for right-handed golfer)
    // rightHanded=false → Right arm driven (lead arm for left-handed golfer)
    // hand/forearm/upperArm are world-space quaternions from the IMUs.
    Q_INVOKABLE void apply(bool rightHanded,
                           const QQuaternion &hand,
                           const QQuaternion &forearm,
                           const QQuaternion &upperArm);

    // Restore the inactive arm to its GLB rest-pose rotations.
    Q_INVOKABLE void resetInactive(bool rightHanded);

signals:
    void readyChanged();
    void attemptedChanged();

private:
    QObject *findBone(QObject *node, const QString &name);
    void     setBoneRotation(QObject *bone, const QQuaternion &q);
    void     dumpNodeNames(QObject *node, int depth = 0);

    struct ArmBones {
        QObject    *arm      = nullptr;
        QObject    *forearm  = nullptr;
        QObject    *hand     = nullptr;
        QQuaternion armRest          { 1, 0, 0, 0 };
        QQuaternion forearmRest      { 1, 0, 0, 0 };
        QQuaternion handRest         { 1, 0, 0, 0 };
        QQuaternion shoulderWorldRot { 1, 0, 0, 0 };
    };

    ArmBones m_left;
    ArmBones m_right;
    bool     m_ready     = false;
    bool     m_attempted = false;  // prevent repeated setup() calls on miss
};
