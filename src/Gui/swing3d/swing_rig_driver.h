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

// SwingRigDriver — the 3-D swing panel's C++ half (docs/design/swing_3d_viz_design.md §6).
//
// Reads ONE swing's `analysis.skeleton3d` (pinpoint.skeleton3d/1) in C++ — off the GUI thread,
// because the document is tens of megabytes — and answers, for the playhead instant, the rig's
// bone rotations, the root's placement, the club and the ball, in the Qt Quick 3D scene frame.
// QML never sees the track: no QVariantMap crosses the boundary (every QML read of one is a deep
// copy), only a handful of small values per bone.
//
// Scene frame: the golfer's. Origin = the ball at address on the floor (else the mid-heels),
// +X along the stance axis toward the lead heel, +Y up, +Z toward the face-on camera's side of
// the golfer... precisely: world (X face-on right, Y face-on view ray, Z up) is turned about Z
// by −stanceYaw, then mapped (x, y, z) → (x, z, −y), a proper rotation (−90° about X).
//
// Bindings: every per-bone read takes `revision` as an argument — `driver.localRotation(j,
// driver.revision)` — so the binding re-evaluates on each playhead step. (A bare `revision`
// statement in a binding is dropped by the QML compiler.)
//
// Shares NOTHING with the calibration views (BodyVizView / BodyPoseAdapter).

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQuaternion>
#include <QString>
#include <QVector3D>

#include <array>
#include <memory>
#include <vector>

namespace pinpoint::skeleton3d { struct Skeleton3DTrack; }

class SwingRigDriver : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString swingDir   READ swingDir   WRITE setSwingDir   NOTIFY swingDirChanged)
    Q_PROPERTY(qint64  positionUs READ positionUs WRITE setPositionUs NOTIFY revisionChanged)
    Q_PROPERTY(bool    loading    READ loading    NOTIFY loadedChanged)
    Q_PROPERTY(bool    available  READ available  NOTIFY loadedChanged)
    Q_PROPERTY(QString reason     READ reason     NOTIFY loadedChanged)
    Q_PROPERTY(bool    twoViews   READ twoViews   NOTIFY loadedChanged)
    Q_PROPERTY(int     jointCount READ jointCount CONSTANT)
    Q_PROPERTY(double  clubLengthM READ clubLengthM NOTIFY loadedChanged)
    Q_PROPERTY(qint64  startUs    READ startUs    NOTIFY loadedChanged)
    Q_PROPERTY(qint64  endUs      READ endUs      NOTIFY loadedChanged)
    Q_PROPERTY(int     revision   READ revision   NOTIFY revisionChanged)
    // Per frame, updated with revision.
    Q_PROPERTY(QVector3D   rootPosition READ rootPosition NOTIFY revisionChanged)
    Q_PROPERTY(QQuaternion rootRotation READ rootRotation NOTIFY revisionChanged)
    Q_PROPERTY(int         frameTier    READ frameTier    NOTIFY revisionChanged)
    Q_PROPERTY(QString     frameTierText READ frameTierText NOTIFY revisionChanged)
    Q_PROPERTY(QVector3D   shaftButt    READ shaftButt    NOTIFY revisionChanged)
    Q_PROPERTY(QQuaternion shaftRotation READ shaftRotation NOTIFY revisionChanged)
    Q_PROPERTY(int         shaftTier    READ shaftTier    NOTIFY revisionChanged)
    Q_PROPERTY(QVector3D   ballPosition READ ballPosition NOTIFY loadedChanged)
    Q_PROPERTY(bool        ballVisible  READ ballVisible  NOTIFY revisionChanged)
    Q_PROPERTY(QVector3D   centre       READ centre       NOTIFY loadedChanged)   // orbit pivot: mid-hip at address

public:
    explicit SwingRigDriver(QObject *parent = nullptr);
    ~SwingRigDriver() override;

    QString swingDir() const { return m_swingDir; }
    void    setSwingDir(const QString &dir);
    qint64  positionUs() const { return m_positionUs; }
    void    setPositionUs(qint64 us);
    bool    loading() const { return m_loading; }
    bool    available() const;
    QString reason() const { return m_reason; }
    bool    twoViews() const;
    int     jointCount() const;
    double  clubLengthM() const;
    qint64  startUs() const;
    qint64  endUs() const;
    int     revision() const { return m_revision; }

    QVector3D   rootPosition() const { return m_rootPos; }
    QQuaternion rootRotation() const { return m_local[0]; }
    int         frameTier() const { return m_frameTier; }
    QString     frameTierText() const;
    QVector3D   shaftButt() const { return m_shaftButt; }
    QQuaternion shaftRotation() const { return m_shaftRot; }
    int         shaftTier() const { return m_shaftTier; }
    QVector3D   ballPosition() const { return m_ball; }
    bool        ballVisible() const { return m_ballVisible; }
    QVector3D   centre() const { return m_centre; }

    // The ybot joint index for a name ("LeftForeArm"), −1 if unknown.
    Q_INVOKABLE int jointIndex(const QString &name) const;
    // Parent-local rotation of joint j at the playhead (the root's is in the scene frame).
    Q_INVOKABLE QQuaternion localRotation(int joint, int revision) const;
    // Joint j's offset from its parent, scaled to the golfer (constant per swing).
    Q_INVOKABLE QVector3D offset(int joint, int revision) const;
    // Uniform scale for the mesh that joint j carries (its bone's group scale).
    Q_INVOKABLE double meshScale(int joint, int revision) const;
    // 0 absent, 1 inferred, 2 constrained, 3 measured.
    Q_INVOKABLE int tier(int joint, int revision) const;

    // Synchronous load for tests and probes (the property path is asynchronous).
    Q_INVOKABLE bool loadNow(const QString &dir);

signals:
    void swingDirChanged();
    void loadedChanged();
    void revisionChanged();

private:
    struct Prepared;                                  // the track, pre-baked per frame
    void startLoad();
    void adopt(std::shared_ptr<const Prepared> p, quint64 generation);
    void evaluate();
    static std::shared_ptr<const Prepared> prepare(const QString &dir, QString *reason);

    QString m_swingDir;
    qint64  m_positionUs = -1;     // < 0 = nothing playing: rest at address
    bool    m_loading = false;
    QString m_reason;
    int     m_revision = 0;
    quint64 m_generation = 0;
    std::shared_ptr<const Prepared> m_track;

    std::vector<QQuaternion> m_local;
    std::vector<int>         m_tier;
    QVector3D   m_rootPos;
    int         m_frameTier = 0;
    QVector3D   m_shaftButt;
    QQuaternion m_shaftRot;
    int         m_shaftTier = 0;
    QVector3D   m_ball;
    bool        m_ballVisible = false;
    QVector3D   m_centre;
};
