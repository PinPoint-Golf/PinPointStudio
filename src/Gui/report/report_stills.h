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

// STILLS FOR THE REPORT (docs/design/session_report_design.md §6) — the frame at a swing position, the
// measured body line drawn on it, cropped to the golfer (nobody else leaves the studio) and lifted in
// exposure on that crop. Made off the GUI thread.
//
// ── Which frame ────────────────────────────────────────────────────────────────────────────────────
// The swing's own instant for the position (analysis.phases, falling back to the P-position ladder in
// analysis.club.positions for a position the phase list does not carry — P8 and P9 are often only
// there), re-timed into the window-relative domain the way DiskReplaySource does it, then the NEAREST
// encoded frame of that camera by timestamp: frames.t_us index i is MP4 frame i (fixed-rate sequential
// PTS), which is exactly the frame the replay shows when its playhead sits on that instant
// (DiskReplaySource::mp4MsForStream: nearest, tie to the earlier frame — markup::frameIndexForUs).
//
// ── The line ───────────────────────────────────────────────────────────────────────────────────────
// The camera's pose (analysis.pose2d face-on, analysis.poseDtl down-the-line; the `smoothed` track when
// the swing has one) at that frame's timestamp, COCO body keypoints only (0–16 of the 133-point
// WholeBody — the hand points are never used), lead/trail from the swing's own handedness. A joint
// below the codebase's per-keypoint gate (tuned::upperBody / lowerBody kConfMin, 0.30) draws no line,
// and the result's `note` says so in plain words.
//
// ── The crop (privacy) ─────────────────────────────────────────────────────────────────────────────
// Vertically: the pose cropRect (one rect per swing, the pose runner's own person crop) plus a margin.
// Horizontally: the SAME, but never wider than the golfer's own body through the swing plus a small
// margin. The pose cropRect is already margined 15% and widened to the estimator's 3:4 aspect, and in
// the studio's face-on bay that widening reaches the people sitting beside the mat (Nick 20 Oct _03:
// a seated bystander's knee and shoe at x ≈ 0.19–0.25 of the frame, the golfer's body from x ≈ 0.31–0.33).
// Which side the seats are on is not known, so the body band bounds BOTH sides. See golferCrop().
//
// ── Exposure ───────────────────────────────────────────────────────────────────────────────────────
// A levels lift measured and applied on the crop alone (exposureCurve): 1st–99th percentile luminance
// stretch, a gentle mid-tone gamma when the crop is still dark, a soft shoulder so nothing is blown out,
// and the identity on a crop that is already well lit.

#include "report_stills_pure.h"   // the pure parts: which frame, which crop, the line's points

#include <QJsonObject>
#include <QObject>
#include <QRect>
#include <QVariantMap>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

#include <array>
#include <cstdint>
#include <optional>

struct StillRequest {
    QString swingDir;            // the swing_NNNN folder
    QString camera;              // "faceOn" | "dtl"
    QString phase;               // "P1" … "P10"
    QString line;                // segment vocabulary (CONTRACTS.md §1); empty = no line
    bool    addressLine = false; // also draw the address line, dashed white (picture kind "change")
    QString outPath;             // JPEG written here
    int     maxPx = 1400;        // long edge of the written still
    // ADDITION (agent F): the width/height the still should have, e.g. 2.0/3.0 for a portrait slot.
    // 0 = the golfer's own box. When the golfer's box is narrower than asked, the still is padded at
    // the sides with the crop's own dark tone (feathered), never widened into the room — so a slot
    // that fills with PreserveAspectCrop crops nothing and a slot that fits shows no bars.
    double  aspect = 0.0;
    // ADDITION (posture chart stage 5): the source rect in FRAME PIXELS. Valid → cut instead of the golfer's
    // crop (and `aspect` is not applied), so a caller that drew its lines in that rect's pixels (the Charts
    // Posture picture, charts/posture_picture.h) gets a still they land on. Clamped to the frame.
    QRect   cropPx;
};

struct StillResult {
    bool    ok = false;
    QString path;
    QString note;                // honest caption when something is missing ("no body line: pose not saved")
    QString error;
    // ADDITIONS (agent F), for the log and the tests: what the still was made from.
    int     frameIndex = -1;     // the encoded frame (frames.t_us index = MP4 frame index)
    QRectF  crop;                // the source rect, normalised to the full frame
    bool    lineDrawn = false;   // the body line is on the still
    bool    addressDrawn = false;// the dashed address line is on the still
};

Q_DECLARE_METATYPE(StillResult)

// A Charts Posture picture frame's `still` (charts/posture_picture.h: { swingDir, camera, phase, x, y, w, h, maxPx,
// outPath }) as a request: no body line, cut at the picture's own crop so its lines land.
inline StillRequest stillRequestOf(const QVariantMap &still, const QString &outPath = QString())
{
    StillRequest r;
    r.swingDir = still.value(QStringLiteral("swingDir")).toString();
    r.camera = still.value(QStringLiteral("camera")).toString();
    r.phase = still.value(QStringLiteral("phase")).toString();
    r.outPath = outPath.isEmpty() ? still.value(QStringLiteral("outPath")).toString() : outPath;
    r.maxPx = still.value(QStringLiteral("maxPx"), 1400).toInt();
    r.cropPx = QRect(still.value(QStringLiteral("x")).toInt(), still.value(QStringLiteral("y")).toInt(),
                     still.value(QStringLiteral("w")).toInt(), still.value(QStringLiteral("h")).toInt());
    return r;
}

class ReportStills : public QObject
{
    Q_OBJECT
public:
    explicit ReportStills(QObject *parent = nullptr);
    static StillResult renderOne(const StillRequest &request);   // synchronous, thread-safe
    void renderAll(const QVector<StillRequest> &requests);       // asynchronous; ends in finished()
    // The same work as renderAll, synchronously, on the calling thread: requests that share a swing load
    // its document once and decode each camera forward. renderAll runs one of these per swing on a pool.
    static QVector<StillResult> renderSwing(const QVector<StillRequest> &requests);
signals:
    void finished(const QVector<StillResult> &results);
};

