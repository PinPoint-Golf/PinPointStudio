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

// The 3-D swing panel's motion annotations as meshes (swing_annotation_mesh.h;
// docs/design/swing_3d_annotations_design.md §7.1), against a synthetic annotation track —
// every point moving along its own straight line, 10 ms apart. Checks:
//   - a trace grows with the playhead and ends EXACTLY at the anchor interpolated there;
//   - an absent frame breaks the tube into two pieces;
//   - nothing outside Address → Finish, nothing when nothing is playing;
//   - the fan holds ≤ 24 frames on the tiles' alpha ramp;
//   - the lead side follows leadLeft; the trace target overrides a body element's anchor;
//   - the plane quad is two triangles whatever the playhead;
//   - the tubes are LIT tubes: every normal is unit and square to the tube's axis;
//   - the club stabiliser (smoothPath): a clean arc at swing speed passes through untouched, a
//     jittered one comes out closer to the truth, an unseen sample is filled from its neighbours.

#include <QCoreApplication>

#include <cmath>
#include <cstdio>

#include "swing_annotation_mesh.h"

using namespace swing3d;
using AP = SwingAnnotTrack;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static SwingAnnotTrack synthetic(int n)
{
    SwingAnnotTrack T;
    for (int i = 0; i < n; ++i) {
        T.t.push_back(qint64(i) * 10000);
        std::array<QVector3D, AP::PointCount> p;
        std::array<quint8, AP::PointCount> tr;
        for (int k = 0; k < AP::PointCount; ++k) {
            // Point k starts at (k·0.1, 1, 0) and moves 1 cm per frame along x and z.
            p[size_t(k)] = QVector3D(0.1f * k + 0.01f * i, 1.0f, 0.01f * i * (k % 2 ? 1.f : -1.f));
            tr[size_t(k)] = 3;
        }
        T.p.push_back(p);
        T.tier.push_back(tr);
    }
    T.addressUs = 50000;
    T.finishUs = qint64(n - 5) * 10000;
    return T;
}

static float dist(const QVector3D &a, const QVector3D &b) { return (a - b).length(); }

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("=== swing_annotation_geometry ===\n");
    SwingAnnotTrack T = synthetic(120);

    AnnotSpec tr;
    tr.element = QStringLiteral("arms");
    tr.mode = QStringLiteral("trace");
    tr.leadLeft = true;
    tr.color = Qt::cyan;

    // ── the trace follows the playhead ──
    {
        const AnnotMesh a = buildAnnotationMesh(T, tr, 300000);
        const AnnotMesh b = buildAnnotationMesh(T, tr, 600000);
        std::printf("      vertices at 300 ms %d, at 600 ms %d\n", a.vertexCount, b.vertexCount);
        check(a.vertexCount > 0 && b.vertexCount > a.vertexCount, "the trace grows with the playhead");
        const qint64 t = 453700;        // between frames
        const AnnotMesh c = buildAnnotationMesh(T, tr, t);
        const float e = dist(c.traceEnd, T.sample(t, AP::LWrist));
        std::printf("      trace end vs the anchor at 453.7 ms: %.2e m\n", double(e));
        check(e < 1e-5f, "the trace ends at the anchor interpolated to the playhead");
        check(c.pieces == 1, "…in one piece");
        check(c.indexCount == (c.vertexCount / kTubeSides - 1) * kTubeSides * 6, "a tube: 6 sides, 2 triangles per side per segment");
    }
    // ── an absent frame breaks it ──
    {
        SwingAnnotTrack U = T;
        U.tier[30][AP::LWrist] = 0;
        const AnnotMesh m = buildAnnotationMesh(U, tr, 600000);
        check(m.pieces == 2, "an absent frame breaks the trace in two (a gap, not a bridge)");
        U.tier[30][AP::LWrist] = 1;
        const AnnotMesh g = buildAnnotationMesh(U, tr, 600000);
        check(g.pieces == 1, "an inferred frame does not break it");
        bool ghost = false;
        const float *v = reinterpret_cast<const float *>(g.vertices.constData());
        for (int i = 0; i < g.vertexCount; ++i)
            if (std::fabs(v[i * kAnnotFloats + 9] - kInferredAlpha) < 1e-4f) { ghost = true; break; }
        check(ghost, "…it is drawn at 40 %");
    }
    // ── the swing window ──
    {
        check(buildAnnotationMesh(T, tr, 20000).vertexCount == 0, "nothing before Address");
        check(buildAnnotationMesh(T, tr, 1180000).vertexCount == 0, "nothing after Finish");
        check(buildAnnotationMesh(T, tr, -1).vertexCount == 0, "nothing when nothing is playing");
        const AnnotMesh m = buildAnnotationMesh(T, tr, 200000);
        const float *v = reinterpret_cast<const float *>(m.vertices.constData());
        // The first ring sits around the anchor AT ADDRESS (frame 5), not the fit's first frame.
        QVector3D c0;
        for (int k = 0; k < kTubeSides; ++k) c0 += QVector3D(v[k * kAnnotFloats], v[k * kAnnotFloats + 1], v[k * kAnnotFloats + 2]);
        c0 /= float(kTubeSides);
        check(dist(c0, T.at(5, AP::LWrist)) < 1e-5f, "the trace starts at Address");
    }
    // ── lead side and the trace target ──
    {
        AnnotSpec r = tr;
        r.leadLeft = false;
        const AnnotMesh m = buildAnnotationMesh(T, r, 400000);
        check(dist(m.traceEnd, T.sample(400000, AP::RWrist)) < 1e-5f, "a left-handed golfer's lead wrist is the right one");
        AnnotSpec h = tr;
        h.element = QStringLiteral("shoulders");
        h.target = QStringLiteral("head");
        const AnnotMesh hm = buildAnnotationMesh(T, h, 400000);
        check(dist(hm.traceEnd, T.sample(400000, AP::Head)) < 1e-5f, "the trace target overrides the element's anchor (traceHead)");
        AnnotSpec sp = tr;
        sp.element = QStringLiteral("spine");
        const AnnotMesh sm = buildAnnotationMesh(T, sp, 400000);
        check(dist(sm.traceEnd, T.sample(400000, AP::NeckMid)) < 1e-5f, "the spine traces the shoulder midpoint");
        AnnotSpec cl = tr;
        cl.element = QStringLiteral("shaft");
        const AnnotMesh cm = buildAnnotationMesh(T, cl, 400000);
        check(dist(cm.traceEnd, T.sample(400000, AP::ClubHead)) < 1e-5f, "the shaft traces the clubhead");
        cl.element = QStringLiteral("shaftGrip");
        const AnnotMesh gm = buildAnnotationMesh(T, cl, 400000);
        check(dist(gm.traceEnd, T.sample(400000, AP::Grip)) < 1e-5f, "shaftGrip traces the grip");
    }
    // ── frame + fan ──
    {
        AnnotSpec f = tr;
        f.mode = QStringLiteral("frame");
        const AnnotMesh m = buildAnnotationMesh(T, f, 500000);
        check(m.vertexCount == 4 * 2 * kTubeSides, "frame: both arms, two bones each");
        f.element = QStringLiteral("legs");
        check(buildAnnotationMesh(T, f, 500000).vertexCount == 4 * 2 * kTubeSides, "frame: the legs' four bones");
        f.element = QStringLiteral("shaftGrip");
        check(buildAnnotationMesh(T, f, 500000).vertexCount == 0, "frame: the grip end draws nothing of its own");

        AnnotSpec fan = tr;
        fan.mode = QStringLiteral("fan");
        const AnnotMesh fm = buildAnnotationMesh(T, fan, 700000);
        std::printf("      fan: %d frames, alpha %.3f → %.3f\n", fm.fanFrames, double(fm.fanAlphaMin), double(fm.fanAlphaMax));
        check(fm.fanFrames > 0 && fm.fanFrames <= kFanMaxFrames, "the fan holds at most 24 frames");
        check(std::fabs(fm.fanAlphaMin - kFanAlphaLo * kFanAlphaScale) < 1e-4f, "…the oldest at 0.08 × 0.7");
        check(fm.fanAlphaMax <= kFanAlphaHi * kFanAlphaScale + 1e-4f, "…the newest at most 0.55 × 0.7");
        // Lead arm only: two bones per fanned frame + the current frame.
        check(fm.vertexCount == (fm.fanFrames + 1) * 2 * 2 * kTubeSides, "arms fans the lead arm only");
        // A dense track (1 ms frames) is stride-subsampled to ≤ 24.
        SwingAnnotTrack D = synthetic(1200);
        for (size_t i = 0; i < D.t.size(); ++i) D.t[i] = qint64(i) * 1000;
        D.addressUs = 0; D.finishUs = 1199000;
        const AnnotMesh dm = buildAnnotationMesh(D, fan, 900000);
        std::printf("      dense fan: %d frames\n", dm.fanFrames);
        // 241 frames in the window, stride ⌈241/24⌉ = 11 ⇒ 22 (the tiles' own arithmetic).
        check(dm.fanFrames == 22, "a dense track is stride-subsampled to ≤ 24 frames, as the tiles do");
    }
    // ── the plane ──
    {
        SwingAnnotTrack P = T;
        AnnotSpec pl;
        pl.element = QStringLiteral("plane");
        pl.mode = QStringLiteral("frame");
        pl.color = QColor(0, 128, 255, 38);
        check(buildAnnotationMesh(P, pl, 300000).vertexCount == 0, "no plane on file: nothing drawn");
        P.planeValid = true;
        P.planeQuad = { QVector3D(0, 0, 0), QVector3D(1, 0, 0), QVector3D(1, 1, 0), QVector3D(0, 1, 0) };
        const AnnotMesh m = buildAnnotationMesh(P, pl, -1);
        check(m.vertexCount == 4 && m.indexCount == 6, "the plane is one quad, drawn even at rest");
        const float *v = reinterpret_cast<const float *>(m.vertices.constData());
        check(std::fabs(v[9] - 38.f / 255.f) < 1e-3f, "…carrying its translucency in the vertex alpha");
    }

    // ── lit tubes: unit normals, square to the axis ──
    {
        const AnnotMesh m = buildAnnotationMesh(T, tr, 600000);
        const float *v = reinterpret_cast<const float *>(m.vertices.constData());
        double worstLen = 0, worstDot = 0;
        const int rings = m.vertexCount / kTubeSides;
        for (int r = 0; r + 1 < rings; ++r) {
            QVector3D c0, c1;
            for (int k = 0; k < kTubeSides; ++k) {
                const float *a = v + (r * kTubeSides + k) * kAnnotFloats, *b = v + ((r + 1) * kTubeSides + k) * kAnnotFloats;
                c0 += QVector3D(a[0], a[1], a[2]); c1 += QVector3D(b[0], b[1], b[2]);
            }
            const QVector3D axis = (c1 - c0).normalized();
            for (int k = 0; k < kTubeSides; ++k) {
                const float *a = v + (r * kTubeSides + k) * kAnnotFloats;
                const QVector3D n(a[3], a[4], a[5]);
                worstLen = std::max(worstLen, std::fabs(double(n.length()) - 1.0));
                worstDot = std::max(worstDot, std::fabs(double(QVector3D::dotProduct(n, axis))));
            }
        }
        std::printf("      normals: |n|−1 ≤ %.1e, n·axis ≤ %.3f\n", worstLen, worstDot);
        check(worstLen < 1e-4 && worstDot < 0.05, "trace normals are unit and square to the tube (a lit tube)");
    }
    // ── the club stabiliser ──
    {
        // A clubhead on a 1.6 m circle at ~35 m/s (22 rad/s), sampled every 6.7 ms (150 fps).
        std::vector<qint64> t;
        std::vector<QVector3D> clean, noisy;
        std::vector<float> w;
        unsigned seed = 12345;
        auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return float((seed >> 8) & 0xffff) / 65535.f - 0.5f; };
        for (int i = 0; i < 90; ++i) {
            const double tt = i * 6667.0;
            const double a = 22.0 * tt * 1e-6;
            const QVector3D p(float(1.6 * std::cos(a)), float(1.6 * std::sin(a)), 0.f);
            t.push_back(qint64(tt));
            clean.push_back(p);
            noisy.push_back(p + QVector3D(rnd(), rnd(), rnd()) * 0.04f);    // ~2 cm rms, the fit's own
            w.push_back(1.f);
        }
        auto rms = [&](const std::vector<QVector3D> &a) {
            double s2 = 0; int n = 0;
            for (size_t i = 5; i + 5 < a.size(); ++i) { s2 += double((a[i] - clean[i]).lengthSquared()); ++n; }
            return std::sqrt(s2 / n);
        };
        const auto sc = smoothPath(t, clean, w, 20000.0);
        const auto sn = smoothPath(t, noisy, w, 20000.0);
        std::printf("      stabiliser: clean arc moved %.2f mm rms; jitter %.1f → %.1f mm rms\n",
                    rms(sc) * 1000, rms(noisy) * 1000, rms(sn) * 1000);
        // A local quadratic lags a circle by its third derivative: ~5 mm at 35 m/s, beside ~2 cm of jitter.
        check(rms(sc) < 0.006, "a clean arc at swing speed passes the 20 ms stabiliser within 6 mm");
        check(rms(sn) < 0.6 * rms(noisy), "a jittered arc comes out at least 40 % closer to the truth");
        std::vector<float> w2 = w;
        w2[40] = 0.f;
        std::vector<QVector3D> gap = clean;
        gap[40] = QVector3D(9, 9, 9);                        // an unseen sample's garbage
        const auto sg = smoothPath(t, gap, w2, 20000.0);
        check((sg[40] - clean[40]).length() < 0.01f, "an unseen sample is filled from its neighbours, never from itself");
    }

    std::printf("=== %s (%d failure%s) ===\n", g_fail ? "FAILED" : "PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
