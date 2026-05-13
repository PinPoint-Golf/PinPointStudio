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

#include <QImage>
#include <QPainter>
#include <QPen>

// ---------------------------------------------------------------------------
// Skeleton connectivity — 17 MoveNet Lightning joints.
//
// Each edge stores {joint_a, joint_b, QPen} where the pen colour follows
// anatomical convention: green = left side, blue = right side, yellow = midline.
// ---------------------------------------------------------------------------

namespace {

struct Edge { int a; int b; QColor color; };

// Catppuccin Mocha palette — matches the rest of the UI.
constexpr QRgb kLeft   = 0xff'a6'e3'a1; // green  — left limbs
constexpr QRgb kRight  = 0xff'89'b4'fa; // blue   — right limbs
constexpr QRgb kCenter = 0xff'f9'e2'af; // yellow — midline

static const Edge kEdges[] = {
    // Face
    {0, 1, kLeft},   {0, 2, kRight},
    {1, 3, kLeft},   {2, 4, kRight},
    // Neck to shoulders
    {0, 5, kLeft},   {0, 6, kRight},
    // Shoulder crossbar
    {5, 6, kCenter},
    // Arms
    {5, 7, kLeft},   {7, 9,  kLeft},
    {6, 8, kRight},  {8, 10, kRight},
    // Torso
    {5, 11, kLeft},  {6, 12, kRight},
    // Hip crossbar
    {11, 12, kCenter},
    // Legs
    {11, 13, kLeft},  {13, 15, kLeft},
    {12, 14, kRight}, {14, 16, kRight},
};

constexpr float kMinScore    = 0.25f; // keypoints below this are not drawn
constexpr int   kJointRadius = 4;     // pixels
constexpr int   kBoneWidth   = 2;     // pixels

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
    auto kpVisible = [&](int j) -> bool {
        return pose.keypoints[j].score >= kMinScore;
    };

    // Draw bones.
    for (const Edge &e : kEdges) {
        if (!kpVisible(e.a) || !kpVisible(e.b))
            continue;

        // Alpha proportional to the weaker endpoint's confidence.
        const float minScore = std::min(pose.keypoints[e.a].score,
                                        pose.keypoints[e.b].score);
        QColor c(e.color);
        c.setAlphaF(static_cast<float>(0.4 + 0.6 * minScore));

        p.setPen(QPen(c, kBoneWidth, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(kpPoint(e.a), kpPoint(e.b));
    }

    // Draw joints on top of bones.
    p.setPen(Qt::NoPen);
    for (int j = 0; j < PoseResult::kNumKeypoints; ++j) {
        if (!kpVisible(j))
            continue;

        // Colour shifts from green (confident) to red (uncertain).
        const float s = pose.keypoints[j].score;
        QColor c;
        if (s >= 0.6f)
            c = QColor(0xff'cd'd6'f4); // lavender — high confidence
        else if (s >= 0.4f)
            c = QColor(0xff'f9'e2'af); // yellow   — medium
        else
            c = QColor(0xff'f3'8b'a8); // pink     — low (above threshold)
        c.setAlphaF(static_cast<float>(0.5 + 0.5 * s));

        p.setBrush(c);
        p.drawEllipse(kpPoint(j), kJointRadius, kJointRadius);
    }
}

#endif // HAVE_OPENCV
