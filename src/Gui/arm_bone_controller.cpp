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

#include "arm_bone_controller.h"
#include <QVariant>
#include <cstdio>

ArmBoneController::ArmBoneController(QObject *parent) : QObject(parent) {}

// Recursive depth-first search through QObject::children().
// RuntimeLoader sets the GLTF node name on the Qt Quick 3D "name" Q_PROPERTY
// (QQuick3DObject::name), NOT on QObject::objectName — so we query the "name"
// property via the meta-object system.
QObject *ArmBoneController::findBone(QObject *node, const QString &name)
{
    if (!node) return nullptr;
    if (node->property("name").toString() == name) return node;
    for (auto *child : node->children()) {
        auto *found = findBone(child, name);
        if (found) return found;
    }
    return nullptr;
}

void ArmBoneController::setBoneRotation(QObject *bone, const QQuaternion &q)
{
    if (!bone) return;
    bone->setProperty("rotation", QVariant::fromValue(q));
}

void ArmBoneController::logStatus(int status)
{
    // RuntimeLoader::Status enum: Null=0, Loading=1, Success=2, Error=3
    const char *label = (status == 0) ? "Null"
                      : (status == 1) ? "Loading"
                      : (status == 2) ? "Success"
                      : (status == 3) ? "Error"
                      : "Unknown";
    fprintf(stderr, "[ArmViz] RuntimeLoader status=%d (%s)\n", status, label);
    fflush(stderr);
}

void ArmBoneController::dumpNodeNames(QObject *node, int depth)
{
    if (!node) return;
    // Qt Quick 3D name property — this is where RuntimeLoader stores GLTF node names.
    const QString name = node->property("name").toString();
    if (!name.isEmpty())
        fprintf(stderr, "[ArmViz]%*s  %-45s  [%s]\n",
                depth * 2, "",
                name.toLatin1().constData(),
                node->metaObject()->className());
    for (auto *child : node->children())
        dumpNodeNames(child, depth + 1);
}

void ArmBoneController::setup(QObject *sceneRoot)
{
    if (!sceneRoot) {
        fprintf(stderr, "[ArmViz] setup() called with null sceneRoot — ignored\n");
        fflush(stderr);
        return;
    }
    if (m_ready || m_attempted) return;
    m_attempted = true;
    emit attemptedChanged();
    m_ready = false;

    const int totalNodes = sceneRoot->findChildren<QObject *>().count();
    fprintf(stderr, "[ArmViz] setup(): scene root class=%s  total descendants=%d\n",
            sceneRoot->metaObject()->className(), totalNodes);

    fprintf(stderr, "[ArmViz] --- full node name dump ---\n");
    dumpNodeNames(sceneRoot);
    fprintf(stderr, "[ArmViz] --- end dump ---\n");
    fflush(stderr);

    // Helper: find a bone, log result, return pointer.
    auto findAndLog = [this, sceneRoot](const char *boneName) -> QObject * {
        auto *bone = findBone(sceneRoot, QString::fromLatin1(boneName));
        if (bone) {
            const QQuaternion r = bone->property("rotation").value<QQuaternion>();
            fprintf(stderr, "[ArmViz]   found  %-42s  rot=(%+.3f %+.3f %+.3f %+.3f)\n",
                    boneName, r.scalar(), r.x(), r.y(), r.z());
        } else {
            fprintf(stderr, "[ArmViz]   MISS   %s\n", boneName);
        }
        return bone;
    };

    // Find all eight arm bones (both sides).
    QObject *leftShoulder  = findAndLog("mixamorig:LeftShoulder");
    m_left.arm             = findAndLog("mixamorig:LeftArm");
    m_left.forearm         = findAndLog("mixamorig:LeftForeArm");
    m_left.hand            = findAndLog("mixamorig:LeftHand");

    QObject *rightShoulder = findAndLog("mixamorig:RightShoulder");
    m_right.arm            = findAndLog("mixamorig:RightArm");
    m_right.forearm        = findAndLog("mixamorig:RightForeArm");
    m_right.hand           = findAndLog("mixamorig:RightHand");

    // Cache rest-pose local rotations (to restore the inactive arm each tick).
    auto rotOf = [](QObject *obj) -> QQuaternion {
        if (!obj) return QQuaternion(1, 0, 0, 0);
        return obj->property("rotation").value<QQuaternion>();
    };
    // Read shoulder world rotation once at rest pose — used as the fixed parent
    // frame when converting world-space IMU quaternions to arm-bone local space.
    auto sceneRotOf = [](QObject *obj) -> QQuaternion {
        if (!obj) return QQuaternion(1, 0, 0, 0);
        return obj->property("sceneRotation").value<QQuaternion>();
    };

    m_left.armRest          = rotOf(m_left.arm);
    m_left.forearmRest      = rotOf(m_left.forearm);
    m_left.handRest         = rotOf(m_left.hand);
    m_left.shoulderWorldRot = sceneRotOf(leftShoulder);

    m_right.armRest          = rotOf(m_right.arm);
    m_right.forearmRest      = rotOf(m_right.forearm);
    m_right.handRest         = rotOf(m_right.hand);
    m_right.shoulderWorldRot = sceneRotOf(rightShoulder);

    // Log C++ class of first found bone — useful to know the concrete type.
    QObject *firstBone = m_left.arm ? m_left.arm : m_right.arm;
    if (firstBone)
        fprintf(stderr, "[ArmViz] bone C++ class = %s\n",
                firstBone->metaObject()->className());

    const bool leftOk  = m_left.arm  && m_left.forearm  && m_left.hand;
    const bool rightOk = m_right.arm && m_right.forearm && m_right.hand;

    fprintf(stderr, "[ArmViz] Left  shoulder worldRot=(%+.3f %+.3f %+.3f %+.3f)  armsReady=%d\n",
            m_left.shoulderWorldRot.scalar(),  m_left.shoulderWorldRot.x(),
            m_left.shoulderWorldRot.y(),       m_left.shoulderWorldRot.z(), leftOk);
    fprintf(stderr, "[ArmViz] Right shoulder worldRot=(%+.3f %+.3f %+.3f %+.3f)  armsReady=%d\n",
            m_right.shoulderWorldRot.scalar(), m_right.shoulderWorldRot.x(),
            m_right.shoulderWorldRot.y(),      m_right.shoulderWorldRot.z(), rightOk);
    fflush(stderr);

    if (leftOk || rightOk) {
        m_ready = true;
        emit readyChanged();
    }
}

void ArmBoneController::apply(bool rightHanded,
                               const QQuaternion &hand,
                               const QQuaternion &forearm,
                               const QQuaternion &upperArm)
{
    static int s_tick = 0;
    if (++s_tick % 300 == 1) {   // log on first call and then every ~5 s
        ArmBones &side = rightHanded ? m_left : m_right;
        fprintf(stderr,
                "[ArmViz] apply() tick=%-5d rightHanded=%d  arm=%p forearm=%p hand=%p\n"
                "         upperArm=(%+.3f %+.3f %+.3f %+.3f)\n"
                "         forearm =(%+.3f %+.3f %+.3f %+.3f)\n"
                "         hand    =(%+.3f %+.3f %+.3f %+.3f)\n",
                s_tick, (int)rightHanded,
                (void *)side.arm, (void *)side.forearm, (void *)side.hand,
                upperArm.scalar(), upperArm.x(), upperArm.y(), upperArm.z(),
                forearm.scalar(),  forearm.x(),  forearm.y(),  forearm.z(),
                hand.scalar(),     hand.x(),     hand.y(),     hand.z());
        fflush(stderr);
    }

    ArmBones &bones = rightHanded ? m_left : m_right;
    if (!bones.arm || !bones.forearm || !bones.hand) return;

    // Convert world-space IMU quaternions to each bone's local space.
    // Each bone's local rotation = inverse(parentWorldRot) × boneWorldRot.
    // The shoulder's world rotation is constant (we never drive it).
    setBoneRotation(bones.arm,     bones.shoulderWorldRot.inverted() * upperArm);
    setBoneRotation(bones.forearm, upperArm.inverted() * forearm);
    setBoneRotation(bones.hand,    forearm.inverted() * hand);
}

void ArmBoneController::resetInactive(bool rightHanded)
{
    // The inactive arm is the opposite side from the driven one.
    ArmBones &bones = rightHanded ? m_right : m_left;
    setBoneRotation(bones.arm,     bones.armRest);
    setBoneRotation(bones.forearm, bones.forearmRest);
    setBoneRotation(bones.hand,    bones.handRest);
}
