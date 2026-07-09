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

#ifdef HAVE_OPENCV

#include "video_overlay_pose.h"

#include <QBrush>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// "Biomech Blueprint" skeleton — a monochrome cool-cyan instrument overlay.
//
// The 17 MoveNet joints are reduced to a body-only skeleton: the face
// keypoints (eyes/ears) are dropped entirely, and two derived midpoints —
// neck (between the shoulders) and pelvis (between the hips) — anchor a
// graduated set of bones that taper from a thick spine and thighs down to
// thin forearms and shins. Every width and radius is a fraction of the torso
// length, so the look is invariant to how large the golfer appears in frame.
// ---------------------------------------------------------------------------

namespace {

// Monochrome instrument palette (ARGB). The joint-fill alpha is baked into
// kRingFill; every other colour has its alpha set per-stroke from confidence.
constexpr QRgb kBone        = 0xff'74'd0'e6; // bones, joint outlines, head, dots
constexpr QRgb kRingFill    = 0xc8'12'1a'23; // dark-ink joint centre (~200 alpha)
constexpr QRgb kSpineNeck   = 0xff'd6'f2'ff; // spine gradient — neck end
constexpr QRgb kSpinePelvis = 0xff'4a'a6'c4; // spine gradient — pelvis end
constexpr QRgb kTickNeck    = 0xff'd6'f2'ff; // neck-end spine tick + diamonds
constexpr QRgb kTickPelvis  = 0xff'6c'c3'dc; // pelvis-end spine tick

constexpr float kMinScore = 0.25f; // keypoints below this are not drawn

// All widths/radii are fractions of the torso length (neck→pelvis), eyeballed
// from the reference illustration where torsoLen ≈ 81 px. The graduated weight
// (thick spine/thighs → thin forearms/shins) is what reads as anatomy.
constexpr float kSpineW       = 0.068f;
constexpr float kThighW       = 0.054f;
constexpr float kUpperArmW    = 0.049f;
constexpr float kShinW        = 0.037f;
constexpr float kCrossW       = 0.037f; // neck bone + shoulder/hip crossbars
constexpr float kForearmW     = 0.032f;
constexpr float kTorsoSideW   = 0.017f;
constexpr float kRingBigR     = 0.056f; // shoulders + hips
constexpr float kRingR        = 0.049f; // all other joints
constexpr float kRingOutlineW = 0.020f;
constexpr float kHeadR        = 0.123f;
constexpr float kHeadOutlineW = 0.025f;
constexpr float kTickHalf     = 0.080f; // spine end-tick half-length
constexpr float kDiamondHalf  = 0.052f;
constexpr float kCentreDotR   = 0.017f;

constexpr float kTorsoSideAlpha = 0.28f;
constexpr qreal kMinTorsoLen    = 20.0; // px — collapsed pose below this

// Bones whose endpoints are both real keypoints. The neck bone and the spine
// are anchored on derived midpoints and are handled separately.
struct Bone { int a; int b; float width; };
constexpr Bone kBones[] = {
    {5,  6,  kCrossW},    {11, 12, kCrossW},   // shoulder + hip crossbars
    {5,  7,  kUpperArmW}, {7,  9,  kForearmW}, // left arm
    {6,  8,  kUpperArmW}, {8,  10, kForearmW}, // right arm
    {11, 13, kThighW},    {13, 15, kShinW},    // left leg
    {12, 14, kThighW},    {14, 16, kShinW},    // right leg
};

} // namespace

// ---------------------------------------------------------------------------

VideoOverlayPose::VideoOverlayPose(QObject *parent)
    : VideoOverlayBase(parent)
{}

void VideoOverlayPose::updatePose(const PoseResult &result)
{
    m_pose     = result;
    m_havePose = true;
}

void VideoOverlayPose::clearPose()
{
    m_havePose = false;
}

void VideoOverlayPose::overlayFrame(const QVideoFrame &frame)
{
    if (!frame.isValid()) {
        emit frameReady(frame);
        return;
    }

    QImage img = frame.toImage().convertToFormat(QImage::Format_ARGB32);

    if (m_havePose)
        drawSkeleton(img, m_pose);

    emit frameReady(QVideoFrame(img));
}

void VideoOverlayPose::drawSkeleton(QImage &img, const PoseResult &pose) const
{
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal w = img.width();
    const qreal h = img.height();

    auto kpPoint = [&](int j) -> QPointF {
        return { pose.keypoints[j].x * w, pose.keypoints[j].y * h };
    };
    auto kpScore   = [&](int j) -> float { return pose.keypoints[j].score; };
    auto kpVisible = [&](int j) -> bool  { return kpScore(j) >= kMinScore; };

    // Derived midpoints. Each is visible only when both parents are, and takes
    // the weaker parent's score so the confidence-driven alpha works unchanged.
    const bool neckVis   = kpVisible(5)  && kpVisible(6);
    const bool pelvisVis = kpVisible(11) && kpVisible(12);
    const QPointF neck   = (kpPoint(5)  + kpPoint(6))  * 0.5;
    const QPointF pelvis = (kpPoint(11) + kpPoint(12)) * 0.5;
    // neck's synthetic score = min of its parents (drives the neck-bone alpha);
    // pelvis needs no score of its own — nothing downstream is alpha-keyed to it.
    const float neckScore = std::min(kpScore(5), kpScore(6));

    // Scale reference: torso length, with graceful fallbacks (shoulder width,
    // then image height). Bail on a collapsed spine so degenerate geometry can
    // never be drawn.
    const bool haveSpine = neckVis && pelvisVis;
    qreal scale;
    if (haveSpine) {
        scale = std::hypot(pelvis.x() - neck.x(), pelvis.y() - neck.y());
        if (scale < kMinTorsoLen)
            return;
    } else if (kpVisible(5) && kpVisible(6)) {
        const QPointF s5 = kpPoint(5), s6 = kpPoint(6);
        scale = std::max<qreal>(
            std::hypot(s6.x() - s5.x(), s6.y() - s5.y()) * 1.5, kMinTorsoLen);
    } else {
        scale = std::max<qreal>(0.15 * h, kMinTorsoLen);
    }

    auto boneAlpha = [](float a, float b) -> float {
        return static_cast<float>(0.4 + 0.6 * std::min(a, b));
    };

    // 1. Faint torso sides — background structure, drawn first so bones sit on top.
    {
        QColor c(kBone);
        c.setAlphaF(kTorsoSideAlpha);
        p.setPen(QPen(c, kTorsoSideW * scale, Qt::SolidLine, Qt::RoundCap));
        if (kpVisible(5) && kpVisible(11)) p.drawLine(kpPoint(5), kpPoint(11));
        if (kpVisible(6) && kpVisible(12)) p.drawLine(kpPoint(6), kpPoint(12));
    }

    // 2. Bones — uniform cyan, graduated width, confidence-driven alpha.
    for (const Bone &b : kBones) {
        if (!kpVisible(b.a) || !kpVisible(b.b))
            continue;
        QColor c(kBone);
        c.setAlphaF(boneAlpha(kpScore(b.a), kpScore(b.b)));
        p.setPen(QPen(c, b.width * scale, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(kpPoint(b.a), kpPoint(b.b));
    }
    // Neck bone: derived neck → Nose.
    if (neckVis && kpVisible(0)) {
        QColor c(kBone);
        c.setAlphaF(boneAlpha(neckScore, kpScore(0)));
        p.setPen(QPen(c, kCrossW * scale, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(neck, kpPoint(0));
    }

    // 3. Spine — the hero element — drawn after the ordinary bones, with a
    //    neck→pelvis gradient and a short perpendicular tick at each end.
    if (haveSpine) {
        QLinearGradient grad(neck, pelvis);
        grad.setColorAt(0.0, QColor(kSpineNeck));
        grad.setColorAt(1.0, QColor(kSpinePelvis));
        QPen spinePen(QBrush(grad), kSpineW * scale);
        spinePen.setCapStyle(Qt::RoundCap);
        p.setPen(spinePen);
        p.drawLine(neck, pelvis);

        const QPointF dir = pelvis - neck;
        const qreal len = std::hypot(dir.x(), dir.y());
        if (len > 1e-3) {
            const QPointF n(-dir.y() / len, dir.x() / len); // unit normal
            const qreal half = kTickHalf * scale;
            p.setPen(QPen(QColor(kTickNeck), kCrossW * scale, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(neck - n * half, neck + n * half);
            p.setPen(QPen(QColor(kTickPelvis), kCrossW * scale, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(pelvis - n * half, pelvis + n * half);
        }
    }

    // 4. Joints — concentric rings for the 12 body keypoints (5–16): a dark-ink
    //    fill with a cyan outline. Shoulders and hips get an extra solid centre
    //    dot, marking the parents of the derived midpoints. No ring on the nose.
    const QColor ringFill = QColor::fromRgba(kRingFill); // fromRgba keeps the 200 alpha
    for (int j = 5; j <= 16; ++j) {
        if (!kpVisible(j))
            continue;
        const bool big = (j == 5 || j == 6 || j == 11 || j == 12);
        const qreal r = (big ? kRingBigR : kRingR) * scale;

        QColor outline(kBone);
        outline.setAlphaF(static_cast<float>(0.5 + 0.5 * kpScore(j)));
        p.setBrush(ringFill);
        p.setPen(QPen(outline, kRingOutlineW * scale, Qt::SolidLine, Qt::RoundCap));
        p.drawEllipse(kpPoint(j), r, r);

        if (big) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(kBone));
            const qreal dr = kCentreDotR * scale;
            p.drawEllipse(kpPoint(j), dr, dr);
        }
    }

    // 5. Head marker — the only head geometry: a single unfilled ring on the Nose.
    if (kpVisible(0)) {
        QColor c(kBone);
        c.setAlphaF(static_cast<float>(0.5 + 0.5 * kpScore(0)));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(c, kHeadOutlineW * scale, Qt::SolidLine, Qt::RoundCap));
        const qreal r = kHeadR * scale;
        p.drawEllipse(kpPoint(0), r, r);
    }

    // 6. Diamonds — filled markers on the two derived midpoints, drawn last so
    //    they sit above the rings and the joined bones.
    auto drawDiamond = [&](const QPointF &c) {
        const qreal d = kDiamondHalf * scale;
        const QPointF pts[4] = {
            {c.x(),     c.y() - d}, {c.x() + d, c.y()},
            {c.x(),     c.y() + d}, {c.x() - d, c.y()},
        };
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(kTickNeck)); // #d6f2ff
        p.drawConvexPolygon(pts, 4);
    };
    if (neckVis)   drawDiamond(neck);
    if (pelvisVis) drawDiamond(pelvis);
}

#endif // HAVE_OPENCV
