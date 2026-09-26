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

#include "swing_rig_driver.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMetaObject>
#include <QThreadPool>

#include <algorithm>
#include <cmath>

#include "../../Analysis/skeleton3d/skeleton3d_json.h"
#include "../../Analysis/skeleton3d/skeleton3d_rig.h"
#include "../../Export/swing_store.h"

namespace sk = pinpoint::skeleton3d;

namespace {

constexpr int kPhaseImpact = 5;   // pinpoint::analysis::Phase::Impact

QQuaternion toQt(const sk::Q &q) { return QQuaternion(float(q.w), float(q.x), float(q.y), float(q.z)); }

// World (the fit's) → the scene: about Z by −stanceYaw, then (x, y, z) → (x, z, −y).
struct SceneFrame {
    sk::Q rot;
    sk::V3 origin;
    QVector3D point(const sk::V3 &p) const
    {
        const sk::V3 v = rot.rotate(p - origin);
        return { float(v.x), float(v.y), float(v.z) };
    }
    QVector3D dir(const sk::V3 &d) const
    {
        const sk::V3 v = rot.rotate(d);
        return { float(v.x), float(v.y), float(v.z) };
    }
};

} // namespace

struct SwingRigDriver::Prepared {
    bool dtl = false;
    double clubLengthM = 0.95;
    std::vector<qint64> t;
    std::vector<std::array<QQuaternion, sk::Rig::N>> local;   // root: scene frame
    std::vector<QVector3D> rootPos;
    std::vector<std::array<uint8_t, sk::Rig::N>> tier;
    std::vector<QVector3D> shaftButt;
    std::vector<QQuaternion> shaftRot;
    std::vector<int> shaftTier;
    std::array<QVector3D, sk::Rig::N> offset {};
    std::array<double, sk::Rig::N> meshScale {};
    QVector3D ball;
    bool ballValid = false;
    qint64 impactUs = -1;
    QVector3D centre;
};

SwingRigDriver::SwingRigDriver(QObject *parent)
    : QObject(parent), m_local(size_t(sk::Rig::N)), m_tier(size_t(sk::Rig::N), 0)
{
}

SwingRigDriver::~SwingRigDriver() = default;

void SwingRigDriver::setSwingDir(const QString &dir)
{
    if (dir == m_swingDir) return;
    m_swingDir = dir;
    emit swingDirChanged();
    startLoad();
}

void SwingRigDriver::setPositionUs(qint64 us)
{
    if (us == m_positionUs) return;
    m_positionUs = us;
    evaluate();
}

bool SwingRigDriver::available() const { return m_track && !m_track->t.empty(); }
bool SwingRigDriver::twoViews() const { return m_track && m_track->dtl; }
int SwingRigDriver::jointCount() const { return sk::Rig::N; }
double SwingRigDriver::clubLengthM() const { return m_track ? m_track->clubLengthM : 0.95; }
qint64 SwingRigDriver::startUs() const { return available() ? m_track->t.front() : 0; }
qint64 SwingRigDriver::endUs() const { return available() ? m_track->t.back() : 0; }

QString SwingRigDriver::frameTierText() const
{
    if (!available()) return m_reason;
    switch (m_frameTier) {
    case sk::TierMeasured:    return m_track->dtl ? tr("seen by both cameras") : tr("measured");
    case sk::TierConstrained: return tr("constrained by the anatomy");
    case sk::TierInferred:    return tr("inferred");
    default:                  return tr("partly unseen");
    }
}

int SwingRigDriver::jointIndex(const QString &name) const
{
    for (int j = 0; j < sk::Rig::N; ++j)
        if (name == QLatin1String(sk::ybot::kJoints[j].name)) return j;
    return -1;
}

QQuaternion SwingRigDriver::localRotation(int joint, int) const
{
    if (joint < 0 || joint >= sk::Rig::N) return {};
    if (!available()) {
        const auto &q = sk::ybot::kJoints[joint].restQ;
        return QQuaternion(float(q[0]), float(q[1]), float(q[2]), float(q[3]));
    }
    return m_local[size_t(joint)];
}

QVector3D SwingRigDriver::offset(int joint, int) const
{
    if (joint < 0 || joint >= sk::Rig::N) return {};
    if (!available()) {
        const auto &t = sk::ybot::kJoints[joint].restT;
        return { float(t[0]), float(t[1]), float(t[2]) };
    }
    return m_track->offset[size_t(joint)];
}

double SwingRigDriver::meshScale(int joint, int) const
{
    if (joint < 0 || joint >= sk::Rig::N || !available()) return 1.0;
    return m_track->meshScale[size_t(joint)];
}

int SwingRigDriver::tier(int joint, int) const
{
    if (joint < 0 || joint >= sk::Rig::N || !available()) return 0;
    return m_tier[size_t(joint)];
}

bool SwingRigDriver::loadNow(const QString &dir)
{
    m_swingDir = dir;
    ++m_generation;
    QString why;
    auto p = prepare(dir, &why);
    m_reason = why;
    adopt(p, m_generation);
    return available();
}

void SwingRigDriver::startLoad()
{
    const quint64 gen = ++m_generation;
    m_track.reset();
    m_loading = !m_swingDir.isEmpty();
    m_reason = m_swingDir.isEmpty() ? tr("no swing selected") : tr("loading…");
    emit loadedChanged();
    ++m_revision;
    emit revisionChanged();
    if (m_swingDir.isEmpty()) return;
    // The document is tens of megabytes: parse it off the GUI thread and adopt the result there.
    QPointer<SwingRigDriver> self(this);
    const QString dir = m_swingDir;
    QThreadPool::globalInstance()->start([self, dir, gen]() {
        QString why;
        auto p = prepare(dir, &why);
        QMetaObject::invokeMethod(self, [self, p, gen, why]() {
            if (!self) return;
            self->m_reason = why;
            self->adopt(p, gen);
        }, Qt::QueuedConnection);
    });
}

void SwingRigDriver::adopt(std::shared_ptr<const Prepared> p, quint64 generation)
{
    if (generation != m_generation) return;    // a newer swing was asked for meanwhile
    m_track = std::move(p);
    m_loading = false;
    if (m_track) {
        m_ball = m_track->ball;
        m_centre = m_track->centre;
    }
    emit loadedChanged();
    evaluate();
}

std::shared_ptr<const SwingRigDriver::Prepared> SwingRigDriver::prepare(const QString &dir, QString *reason)
{
    QString err;
    const QJsonObject root = pinpoint::SwingStore::load(dir, &err);
    if (root.isEmpty()) {
        *reason = err.isEmpty() ? QObject::tr("no swing document") : err;
        return {};
    }
    const QJsonObject an = root.value(QStringLiteral("analysis")).toObject();
    const sk::Skeleton3DTrack trk = sk::skeleton3dFromJson(an.value(QStringLiteral("skeleton3d")).toObject());
    if (!trk.valid) {
        *reason = trk.reason.isEmpty() ? QObject::tr("no 3-D skeleton on this shot") : trk.reason;
        return {};
    }
    auto P = std::make_shared<Prepared>();
    P->dtl = trk.dtl;
    P->clubLengthM = trk.clubLengthM > 0.5 ? trk.clubLengthM : 0.95;

    const sk::Rig &R = sk::rig();
    SceneFrame S;
    S.rot = (sk::Q::axisAngle({ 1, 0, 0 }, -sk::kPi / 2.0)
             * sk::Q::axisAngle({ 0, 0, 1 }, -trk.stanceYawDeg * sk::kDeg)).normalized();
    S.origin = trk.displayOrigin;

    for (int j = 0; j < sk::Rig::N; ++j) {
        const int g = sk::jointGroup(j);
        const double s = g >= 0 ? trk.lengths[size_t(g)] : trk.lengths[sk::GSpine];
        const sk::V3 o = R.restT[j] * s;
        P->offset[size_t(j)] = { float(o.x), float(o.y), float(o.z) };
        P->meshScale[size_t(j)] = s;
    }

    // Impact, window-relative, for the ball.
    const qint64 t0 = qint64(root.value(QStringLiteral("clock")).toObject().value(QStringLiteral("t0_us")).toDouble());
    for (const QJsonValue &pv : an.value(QStringLiteral("phases")).toArray()) {
        const QJsonObject po = pv.toObject();
        if (po.value(QStringLiteral("phase")).toInt() != kPhaseImpact) continue;
        const qint64 raw = qint64(po.value(QStringLiteral("t_us")).toDouble());
        P->impactUs = raw >= t0 ? raw - t0 : raw;
    }
    if (trk.ballValid) {
        P->ball = S.point(trk.ball);
        P->ballValid = true;
    }

    const std::array<double, sk::GroupCount> &scale = trk.lengths;
    sk::Pose pose;
    for (const sk::Skeleton3DFrame &f : trk.frames) {
        if (int(f.dof.size()) != R.dofCount()) continue;
        sk::forwardKinematics(R, f.dof.data(), scale.data(), pose);
        std::array<QQuaternion, sk::Rig::N> loc;
        for (int j = 0; j < sk::Rig::N; ++j) loc[size_t(j)] = toQt(pose.local[j]);
        loc[0] = toQt((S.rot * pose.local[0]).normalized());
        P->t.push_back(f.t_us);
        P->local.push_back(loc);
        P->rootPos.push_back(S.point(pose.pos[0]));
        P->tier.push_back(f.tier);
        const sk::V3 butt = f.grip - f.shaftDir * 0.04;
        P->shaftButt.push_back(S.point(butt));
        P->shaftRot.push_back(QQuaternion::rotationTo(QVector3D(0, 1, 0), S.dir(f.shaftDir).normalized()));
        P->shaftTier.push_back(f.shaftTier);
        if (P->t.size() == 1)
            P->centre = S.point((pose.pos[sk::ybot::LeftUpLeg] + pose.pos[sk::ybot::RightUpLeg]) * 0.5);
    }
    if (P->t.size() < 2) {
        *reason = QObject::tr("the 3-D skeleton has fewer than two frames");
        return {};
    }
    reason->clear();
    return P;
}

void SwingRigDriver::evaluate()
{
    if (available()) {
        const Prepared &P = *m_track;
        const qint64 t = std::clamp(m_positionUs, P.t.front(), P.t.back());
        auto hi = std::upper_bound(P.t.begin(), P.t.end(), t);
        size_t b = hi == P.t.end() ? P.t.size() - 1 : size_t(hi - P.t.begin());
        size_t a = b > 0 ? b - 1 : 0;
        float w = 0.f;
        if (b != a && P.t[b] > P.t[a]) w = float(double(t - P.t[a]) / double(P.t[b] - P.t[a]));
        w = std::clamp(w, 0.f, 1.f);
        const size_t near = w < 0.5f ? a : b;
        for (int j = 0; j < sk::Rig::N; ++j) {
            m_local[size_t(j)] = QQuaternion::slerp(P.local[a][size_t(j)], P.local[b][size_t(j)], w);
            m_tier[size_t(j)] = P.tier[near][size_t(j)];
        }
        m_rootPos = P.rootPos[a] * (1.f - w) + P.rootPos[b] * w;
        m_shaftButt = P.shaftButt[a] * (1.f - w) + P.shaftButt[b] * w;
        m_shaftRot = QQuaternion::slerp(P.shaftRot[a], P.shaftRot[b], w);
        m_shaftTier = P.shaftTier[near];
        m_ballVisible = P.ballValid && (P.impactUs < 0 || t <= P.impactUs + 5000);
        // The frame's tier: the weakest of the major joints (an absent one makes it "partly unseen").
        using namespace sk::ybot;
        int ft = sk::TierMeasured;
        for (int j : { Hips, Spine2, Head, LeftArm, RightArm, LeftForeArm, RightForeArm, LeftHand, RightHand,
                       LeftLeg, RightLeg, LeftFoot, RightFoot })
            ft = std::min(ft, m_tier[size_t(j)]);
        m_frameTier = ft;
    }
    ++m_revision;
    emit revisionChanged();
}
