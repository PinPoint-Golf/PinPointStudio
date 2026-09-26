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

#include "swing_hull_geometry.h"

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuaternion>
#include <QVector3D>

#include <cmath>
#include <cstring>
#include <vector>

#include "../../Analysis/skeleton3d/skeleton3d_rig.h"

namespace sk = pinpoint::skeleton3d;

SwingHullGeometry::SwingHullGeometry(QQuick3DObject *parent) : QQuick3DGeometry(parent) {}

void SwingHullGeometry::setSource(const QString &s)
{
    if (s == m_source) return;
    m_source = s;
    emit sourceChanged();
    load();
}

void SwingHullGeometry::load()
{
    m_ready = false;
    m_error.clear();
    QString path = m_source;
    if (path.startsWith(QLatin1String("qrc:"))) path = path.mid(3);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("cannot open %1").arg(path);
        emit loaded();
        return;
    }
    const QByteArray b = f.readAll();
    auto u32 = [&b](int off) { quint32 v; std::memcpy(&v, b.constData() + off, 4); return v; };
    if (b.size() < 20 || u32(0) != 0x46546C67u) { m_error = QStringLiteral("not a GLB"); emit loaded(); return; }
    int off = 12;
    QJsonObject js;
    QByteArray bin;
    while (off + 8 <= b.size()) {
        const quint32 len = u32(off), type = u32(off + 4);
        const QByteArray chunk = b.mid(off + 8, int(len));
        if (type == 0x4E4F534Au) js = QJsonDocument::fromJson(chunk).object();
        else if (type == 0x004E4942u) bin = chunk;
        off += 8 + int(len);
    }
    const QJsonArray acc = js.value(QStringLiteral("accessors")).toArray();
    const QJsonArray bvs = js.value(QStringLiteral("bufferViews")).toArray();
    if (acc.size() < 5 || bin.isEmpty()) { m_error = QStringLiteral("hull.glb: unexpected layout"); emit loaded(); return; }
    auto view = [&](int i, int &count) -> const char * {
        const QJsonObject a = acc[i].toObject();
        count = a.value(QStringLiteral("count")).toInt();
        const QJsonObject bv = bvs[a.value(QStringLiteral("bufferView")).toInt()].toObject();
        return bin.constData() + bv.value(QStringLiteral("byteOffset")).toInt();
    };
    int nV = 0, nN = 0, nJ = 0, nW = 0, nI = 0;
    const auto *pos = reinterpret_cast<const float *>(view(0, nV));
    const auto *nrm = reinterpret_cast<const float *>(view(1, nN));
    const auto *jnt = reinterpret_cast<const quint16 *>(view(2, nJ));
    const auto *wgt = reinterpret_cast<const float *>(view(3, nW));
    const auto *idx = reinterpret_cast<const quint32 *>(view(4, nI));
    if (nV <= 0 || nN != nV || nJ != nV || nW != nV || nI <= 0) {
        m_error = QStringLiteral("hull.glb: attribute counts disagree");
        emit loaded();
        return;
    }

    // Interleaved: position (3f) · normal (3f) · joints (4i) · weights (4f) = 56 bytes.
    constexpr int kStride = 56;
    QByteArray vbuf(nV * kStride, Qt::Uninitialized);
    QVector3D lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
    for (int i = 0; i < nV; ++i) {
        char *d = vbuf.data() + i * kStride;
        std::memcpy(d, pos + 3 * i, 12);
        std::memcpy(d + 12, nrm + 3 * i, 12);
        const qint32 j4[4] = { jnt[4 * i], jnt[4 * i + 1], jnt[4 * i + 2], jnt[4 * i + 3] };
        std::memcpy(d + 24, j4, 16);
        std::memcpy(d + 40, wgt + 4 * i, 16);
        const QVector3D p(pos[3 * i], pos[3 * i + 1], pos[3 * i + 2]);
        lo = QVector3D(std::min(lo.x(), p.x()), std::min(lo.y(), p.y()), std::min(lo.z(), p.z()));
        hi = QVector3D(std::max(hi.x(), p.x()), std::max(hi.y(), p.y()), std::max(hi.z(), p.z()));
    }
    clear();
    setVertexData(vbuf);
    setIndexData(QByteArray(reinterpret_cast<const char *>(idx), nI * 4));
    setStride(kStride);
    // The skinned figure travels a metre or more from its bind pose: bounds wide enough never to
    // be culled while it swings.
    setBounds(lo - QVector3D(2, 2, 2), hi + QVector3D(2, 2, 2));
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 12, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::JointSemantic, 24, QQuick3DGeometry::Attribute::I32Type);
    addAttribute(QQuick3DGeometry::Attribute::WeightSemantic, 40, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0, QQuick3DGeometry::Attribute::U32Type);
    update();
    m_vertexCount = nV;

    // Inverse bind poses: the rig at θ = 0 (neutral arms), RIG frame, unit scale.
    const sk::Rig &R = sk::rig();
    std::vector<double> th(size_t(R.dofCount()), 0.0);
    const sk::V3 hipRest = R.rigToWorld.rotate(R.restT[sk::ybot::Hips]);
    th[0] = hipRest.x; th[1] = hipRest.y; th[2] = hipRest.z;
    std::array<double, sk::GroupCount> sc; sc.fill(1.0);
    sk::Pose pose;
    sk::forwardKinematics(R, th.data(), sc.data(), pose);
    const sk::Q toRig = R.rigToWorld.conj();
    const QJsonObject bindJoints = js.value(QStringLiteral("asset")).toObject()
                                       .value(QStringLiteral("extras")).toObject()
                                       .value(QStringLiteral("bindJoints")).toObject();
    m_invBind.clear();
    double worst = 0;
    for (int j = 0; j < sk::Rig::N; ++j) {
        const sk::V3 p = toRig.rotate(pose.pos[j]);
        const sk::Q q = (toRig * pose.rot[j]).normalized();
        QMatrix4x4 m;
        m.translate(float(p.x), float(p.y), float(p.z));
        m.rotate(QQuaternion(float(q.w), float(q.x), float(q.y), float(q.z)));
        m_invBind.append(m.inverted());
        const QJsonArray bj = bindJoints.value(QLatin1String(sk::ybot::kJoints[j].name)).toArray();
        if (bj.size() == 3)
            worst = std::max(worst, std::hypot(std::hypot(p.x - bj[0].toDouble(), p.y - bj[1].toDouble()),
                                               p.z - bj[2].toDouble()));
        else
            worst = std::max(worst, 1.0);   // a joint the file does not know: flag it
    }
    m_bindMismatch = worst;
    m_ready = worst < 0.005;
    if (!m_ready) m_error = QStringLiteral("hull.glb was built for a different rig (bind joints off by %1 m) — "
                                          "rerun tools/generate_swing3d_hull.py").arg(worst, 0, 'f', 4);
    emit loaded();
}
