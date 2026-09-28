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

#include "swing_annotation_mesh.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace swing3d {

namespace {

using AP = SwingAnnotTrack;

// One bone of an element: its two points, its width against the tiles' upper-arm stroke, and
// how bright (the spine's faint torso sides).
struct Bone {
    int a, b;
    float width = 1.f;
    float alpha = 1.f;
    bool gradient = false;       // the spine: color at a, colorB at b
};

// The tiles' element → bone lists (PpCameraFrame.qml paintBlueprint / _paintElem), on the rig's
// joint centres. Widths are the tiles' stroke widths over the upper arm's 0.049.
std::vector<Bone> bonesFor(const QString &e, bool leadLeft, bool bothArms, bool full)
{
    std::vector<Bone> out;
    if (e == QLatin1String("arms")) {
        auto arm = [&](bool left) {
            out.push_back({ left ? AP::LShoulder : AP::RShoulder, left ? AP::LElbow : AP::RElbow, 1.f });
            out.push_back({ left ? AP::LElbow : AP::RElbow, left ? AP::LWrist : AP::RWrist, 0.65f });
        };
        if (bothArms) { arm(true); arm(false); }
        else arm(leadLeft);
    } else if (e == QLatin1String("shoulders")) {
        out.push_back({ AP::LShoulder, AP::RShoulder, 0.75f });
        out.push_back({ AP::NeckMid, AP::Head, 0.75f });
    } else if (e == QLatin1String("spine")) {
        out.push_back({ AP::NeckMid, AP::PelvisMid, 1.4f, 1.f, true });
        if (full) {
            out.push_back({ AP::LShoulder, AP::LHip, 0.35f, 0.28f });
            out.push_back({ AP::RShoulder, AP::RHip, 0.35f, 0.28f });
        }
    } else if (e == QLatin1String("hips")) {
        out.push_back({ AP::LHip, AP::RHip, 0.75f });
    } else if (e == QLatin1String("legs")) {
        out.push_back({ AP::LHip, AP::LKnee, 1.1f });
        out.push_back({ AP::LKnee, AP::LAnkle, 0.75f });
        out.push_back({ AP::RHip, AP::RKnee, 1.1f });
        out.push_back({ AP::RKnee, AP::RAnkle, 0.75f });
    } else if (e == QLatin1String("shaft")) {
        out.push_back({ AP::ClubButt, AP::ClubHead, 0.6f });
    }
    return out;
}

struct Builder {
    std::vector<float> v;
    std::vector<quint32> idx;

    quint32 vertex(const QVector3D &p, const QVector3D &n, const QColor &c, float alpha)
    {
        const quint32 k = quint32(v.size() / kAnnotFloats);
        v.insert(v.end(), { p.x(), p.y(), p.z(), n.x(), n.y(), n.z(),
                            float(c.redF()), float(c.greenF()), float(c.blueF()), float(c.alphaF()) * alpha });
        return k;
    }

    // A tube through `pts` — one ring per point, carried along by projecting the previous ring's
    // normal onto each new cross-section (no twist, no flips at the top of the swing).
    void tube(const std::vector<QVector3D> &ptsIn, const std::vector<float> &alphaIn, float radius,
              const QColor &cA, const QColor &cB)
    {
        std::vector<QVector3D> pts;
        std::vector<float> al;
        for (size_t i = 0; i < ptsIn.size(); ++i) {
            if (!pts.empty() && (ptsIn[i] - pts.back()).lengthSquared() < 1e-12f) continue;
            pts.push_back(ptsIn[i]);
            al.push_back(alphaIn[i]);
        }
        const size_t n = pts.size();
        if (n < 2) return;
        QVector3D N;
        const quint32 base = quint32(v.size() / kAnnotFloats);
        for (size_t i = 0; i < n; ++i) {
            const QVector3D T = (pts[std::min(i + 1, n - 1)] - pts[i > 0 ? i - 1 : 0]).normalized();
            if (i == 0) {
                N = QVector3D::crossProduct(T, std::fabs(T.y()) < 0.9f ? QVector3D(0, 1, 0) : QVector3D(1, 0, 0)).normalized();
            } else {
                const QVector3D Nn = N - T * QVector3D::dotProduct(T, N);
                if (Nn.lengthSquared() > 1e-10f) N = Nn.normalized();
            }
            const QVector3D B = QVector3D::crossProduct(T, N).normalized();
            const float w = n > 1 ? float(i) / float(n - 1) : 0.f;
            const QColor c = cB.isValid()
                ? QColor::fromRgbF(float(cA.redF() * (1 - w) + cB.redF() * w), float(cA.greenF() * (1 - w) + cB.greenF() * w),
                                   float(cA.blueF() * (1 - w) + cB.blueF() * w), float(cA.alphaF() * (1 - w) + cB.alphaF() * w))
                : cA;
            for (int k = 0; k < kTubeSides; ++k) {
                const float th = float(2.0 * M_PI * k / kTubeSides);
                const QVector3D out = N * std::cos(th) + B * std::sin(th);
                vertex(pts[i] + out * radius, out, c, al[i]);
            }
        }
        for (size_t i = 0; i + 1 < n; ++i)
            for (int k = 0; k < kTubeSides; ++k) {
                const quint32 a = base + quint32(i * kTubeSides + k);
                const quint32 b = base + quint32(i * kTubeSides + (k + 1) % kTubeSides);
                const quint32 c = a + kTubeSides, d = b + kTubeSides;
                idx.insert(idx.end(), { a, b, c, b, d, c });     // counter-clockwise seen from OUTSIDE
            }
    }

    void stick(const QVector3D &a, const QVector3D &b, float alpha, float radius, const QColor &cA, const QColor &cB)
    {
        tube({ a, b }, { alpha, alpha }, radius, cA, cB);
    }

    AnnotMesh finish()
    {
        AnnotMesh m;
        m.vertexCount = int(v.size() / kAnnotFloats);
        m.indexCount = int(idx.size());
        m.vertices = QByteArray(reinterpret_cast<const char *>(v.data()), qsizetype(v.size() * sizeof(float)));
        m.indices = QByteArray(reinterpret_cast<const char *>(idx.data()), qsizetype(idx.size() * sizeof(quint32)));
        if (m.vertexCount > 0) {
            QVector3D lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
            for (size_t i = 0; i < v.size(); i += kAnnotFloats) {
                lo = QVector3D(std::min(lo.x(), v[i]), std::min(lo.y(), v[i + 1]), std::min(lo.z(), v[i + 2]));
                hi = QVector3D(std::max(hi.x(), v[i]), std::max(hi.y(), v[i + 1]), std::max(hi.z(), v[i + 2]));
            }
            m.lo = lo; m.hi = hi;
        }
        return m;
    }
};

float tierAlpha(quint8 tier) { return tier <= 1 ? kInferredAlpha : 1.f; }

// The frame nearest t (its tier is the instant's, as the figure's is).
size_t nearestFrame(const SwingAnnotTrack &T, qint64 t)
{
    auto hi = std::upper_bound(T.t.begin(), T.t.end(), t);
    size_t b = hi == T.t.end() ? T.t.size() - 1 : size_t(hi - T.t.begin());
    size_t a = b > 0 ? b - 1 : 0;
    return (t - T.t[a]) <= (T.t[b] - t) ? a : b;
}

// The element's bones at frame i (or, with i = npos, interpolated at t).
void addBones(Builder &B, const SwingAnnotTrack &T, const std::vector<Bone> &bones, size_t i, qint64 t,
              float alpha, float radius, const AnnotSpec &s)
{
    const bool atT = i == size_t(-1);
    const size_t tf = atT ? nearestFrame(T, t) : i;
    for (const Bone &bn : bones) {
        const quint8 ta = T.tierAt(tf, bn.a), tb = T.tierAt(tf, bn.b);
        if (ta == 0 || tb == 0) continue;
        const QVector3D pa = atT ? T.sample(t, bn.a) : T.at(i, bn.a);
        const QVector3D pb = atT ? T.sample(t, bn.b) : T.at(i, bn.b);
        B.stick(pa, pb, alpha * bn.alpha * tierAlpha(std::min(ta, tb)), radius * bn.width, s.color,
                bn.gradient && s.colorB.isValid() && s.colorB.alpha() > 0 ? s.colorB : QColor());
    }
}

} // namespace

int traceAnchor(const QString &element, const QString &target, bool leadLeft)
{
    if (element == QLatin1String("shaft")) return AP::ClubHead;
    if (element == QLatin1String("shaftGrip")) return AP::Grip;
    QString tt = target;
    if (tt.isEmpty()) {
        if (element == QLatin1String("arms")) tt = QStringLiteral("leadWrist");
        else if (element == QLatin1String("spine")) tt = QStringLiteral("neckMid");
        else if (element == QLatin1String("shoulders")) tt = QStringLiteral("leadShoulder");
        else if (element == QLatin1String("hips")) tt = QStringLiteral("pelvisMid");
        else if (element == QLatin1String("legs")) tt = QStringLiteral("leadAnkle");
        else return -1;
    }
    if (tt == QLatin1String("head")) return AP::Head;
    if (tt == QLatin1String("leadShoulder")) return leadLeft ? AP::LShoulder : AP::RShoulder;
    if (tt == QLatin1String("leadWrist")) return leadLeft ? AP::LWrist : AP::RWrist;
    if (tt == QLatin1String("leadAnkle")) return leadLeft ? AP::LAnkle : AP::RAnkle;
    if (tt == QLatin1String("neckMid")) return AP::NeckMid;
    if (tt == QLatin1String("pelvisMid")) return AP::PelvisMid;
    return -1;
}

AnnotMesh buildAnnotationMesh(const SwingAnnotTrack &T, const AnnotSpec &s, qint64 t)
{
    Builder B;
    if (T.t.size() < 2 || s.mode == QLatin1String("off")) return B.finish();

    if (s.element == QLatin1String("plane")) {
        if (!T.planeValid) return B.finish();
        quint32 q[4];
        const QVector3D n = QVector3D::crossProduct(T.planeQuad[1] - T.planeQuad[0], T.planeQuad[3] - T.planeQuad[0]).normalized();
        for (int k = 0; k < 4; ++k) q[k] = B.vertex(T.planeQuad[size_t(k)], n, s.color, 1.f);
        B.idx.insert(B.idx.end(), { q[0], q[1], q[2], q[0], q[2], q[3] });
        return B.finish();
    }
    if (t < 0 || !T.inWindow(t)) return B.finish();
    t = std::clamp(t, T.t.front(), T.t.back());

    if (s.mode == QLatin1String("trace")) {
        const int anchor = traceAnchor(s.element, s.target, s.leadLeft);
        if (anchor < 0) return B.finish();
        const qint64 start = T.addressUs >= 0 ? T.addressUs : T.t.front();
        AnnotMesh info;
        std::vector<QVector3D> pts;
        std::vector<float> al;
        auto flush = [&]() {
            if (pts.size() >= 2) { B.tube(pts, al, s.radius, s.color, QColor()); ++info.pieces; info.traceEnd = pts.back(); }
            pts.clear(); al.clear();
        };
        for (size_t i = 0; i < T.t.size() && T.t[i] < t; ++i) {
            if (T.t[i] < start) continue;
            const quint8 tr = T.tierAt(i, anchor);
            if (tr == 0) { flush(); continue; }
            pts.push_back(T.at(i, anchor));
            al.push_back(tierAlpha(tr));
        }
        // The last piece reaches the playhead exactly, if the frame it leads to is seen.
        const size_t nf = nearestFrame(T, t);
        if (T.tierAt(nf, anchor) > 0 && t >= start) {
            pts.push_back(T.sample(t, anchor));
            al.push_back(tierAlpha(T.tierAt(nf, anchor)));
        }
        flush();
        AnnotMesh m = B.finish();
        m.pieces = info.pieces;
        m.traceEnd = info.traceEnd;
        return m;
    }

    if (s.mode == QLatin1String("frame")) {
        if (s.element == QLatin1String("shaft")) {
            // The tiles' head trail: the last kHeadTrail heads before the playhead, brightening.
            auto hi = std::upper_bound(T.t.begin(), T.t.end(), t);
            const size_t ci = hi == T.t.begin() ? 0 : size_t(hi - T.t.begin()) - 1;
            const size_t k0 = ci >= size_t(kHeadTrail) ? ci - size_t(kHeadTrail) : 0;
            std::vector<QVector3D> pts;
            std::vector<float> al;
            for (size_t k = k0; k <= ci; ++k) {
                if (T.tierAt(k, AP::ClubHead) == 0) { B.tube(pts, al, s.radius, s.color, QColor()); pts.clear(); al.clear(); continue; }
                pts.push_back(T.at(k, AP::ClubHead));
                al.push_back(0.45f * float(k + 1 - k0) / float(ci - k0 + 1));
            }
            B.tube(pts, al, s.radius, s.color, QColor());
            return B.finish();
        }
        addBones(B, T, bonesFor(s.element, s.leadLeft, true, true), size_t(-1), t, 1.f, s.radius, s);
        return B.finish();
    }

    if (s.mode == QLatin1String("fan")) {
        // PpCameraFrame's FAN pass: frames from t − 240 ms to t, stride-subsampled to ≤ 24, alpha
        // 0.08 → 0.55 (× 0.7) oldest to newest, then the current frame at 0.7.
        const std::vector<Bone> bones = bonesFor(s.element, s.leadLeft, false, false);
        if (bones.empty()) return B.finish();
        auto idxFor = [&](qint64 tt) -> long {
            auto hi = std::upper_bound(T.t.begin(), T.t.end(), tt);
            return long(hi - T.t.begin()) - 1;
        };
        const long eFi = idxFor(t);
        if (eFi < 0) return B.finish();
        long sFi = idxFor(t - kFanWindowUs);
        if (sFi < 0) sFi = 0;
        const long span = std::max(1L, eFi - sFi);
        const long stride = std::max(1L, long(std::ceil(double(eFi - sFi + 1) / kFanMaxFrames)));
        int nFan = 0;
        float aMin = 1.f, aMax = 0.f;
        for (long k = sFi; k < eFi; k += stride) {
            const float a = (kFanAlphaLo + (kFanAlphaHi - kFanAlphaLo) * float(k - sFi) / float(span)) * kFanAlphaScale;
            addBones(B, T, bones, size_t(k), t, a, s.radius, s);
            ++nFan;
            aMin = std::min(aMin, a); aMax = std::max(aMax, a);
        }
        addBones(B, T, bones, size_t(-1), t, kFanAlphaScale, s.radius, s);
        AnnotMesh m = B.finish();
        m.fanFrames = nFan;
        m.fanAlphaMin = nFan ? aMin : 0.f;
        m.fanAlphaMax = nFan ? aMax : 0.f;
        return m;
    }
    return B.finish();
}

std::vector<QVector3D> smoothPath(const std::vector<qint64> &t, const std::vector<QVector3D> &x,
                                  const std::vector<float> &weight, double sigmaUs)
{
    const size_t n = t.size();
    std::vector<QVector3D> out(x);
    if (n < 3 || !(sigmaUs > 0)) return out;
    const double reach = 3.0 * sigmaUs;
    for (size_t i = 0; i < n; ++i) {
        // Weighted least squares of x(τ) = a + bτ + cτ² about t_i; the value is a.
        double S[5] = { 0, 0, 0, 0, 0 };           // Σw τ^k, k = 0..4
        double R[3][3] = { { 0 } };                // Σw τ^k x, k = 0..2, per axis
        for (size_t j = 0; j < n; ++j) {
            const double tau = double(t[j] - t[i]);
            if (std::fabs(tau) > reach || !(weight[j] > 0)) continue;
            const double z = tau / sigmaUs;
            const double w = double(weight[j]) * std::exp(-0.5 * z * z);
            const double tn = tau / sigmaUs;       // scaled, for conditioning
            double p = w;
            for (int k = 0; k < 5; ++k) { S[k] += p; if (k < 3) for (int a = 0; a < 3; ++a) R[k][a] += p * double(x[j][a]); p *= tn; }
        }
        // Solve the 3×3 normal equations (Cramer); too few neighbours ⇒ keep the sample.
        const double M[3][3] = { { S[0], S[1], S[2] }, { S[1], S[2], S[3] }, { S[2], S[3], S[4] } };
        auto det3 = [](const double m[3][3]) {
            return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                 + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
        };
        const double D = det3(M);
        if (std::fabs(D) < 1e-12 * std::max(1.0, S[0] * S[0] * S[0])) continue;
        QVector3D r;
        for (int a = 0; a < 3; ++a) {
            double Ma[3][3];
            for (int row = 0; row < 3; ++row) {
                Ma[row][0] = R[row][a];
                Ma[row][1] = M[row][1];
                Ma[row][2] = M[row][2];
            }
            r[a] = float(det3(Ma) / D);
        }
        out[i] = r;
    }
    return out;
}

} // namespace swing3d
