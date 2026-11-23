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

// THE AVATAR TRUNK'S TYPED BONES (refactor 5.8a, lint A6): what BodyPoseAdapter::trunkBones hands BodyVizView, the
// parent-local node rotations by node name, as the old map's keys (viz_frame.h TrunkAvatarBones, field for field).
// moc reaches this header through body_pose_adapter.cpp's #include of its moc file.

#include <QMetaType>
#include <QQuaternion>

namespace pinpoint::vizui {

struct TrunkBones
{
    Q_GADGET
    Q_PROPERTY(QQuaternion hips       MEMBER hips       FINAL)
    Q_PROPERTY(QQuaternion spine      MEMBER spine      FINAL)
    Q_PROPERTY(QQuaternion spine1     MEMBER spine1     FINAL)
    Q_PROPERTY(QQuaternion spine2     MEMBER spine2     FINAL)
    Q_PROPERTY(QQuaternion leftUpLeg  MEMBER leftUpLeg  FINAL)
    Q_PROPERTY(QQuaternion leftLeg    MEMBER leftLeg    FINAL)
    Q_PROPERTY(QQuaternion rightUpLeg MEMBER rightUpLeg FINAL)
    Q_PROPERTY(QQuaternion rightLeg   MEMBER rightLeg   FINAL)
public:
    QQuaternion hips, spine, spine1, spine2, leftUpLeg, leftLeg, rightUpLeg, rightLeg;
    friend bool operator==(const TrunkBones &, const TrunkBones &) = default;
};

} // namespace pinpoint::vizui
