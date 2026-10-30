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

// THE CLUB DELIVERY PICTURE STRIP (docs/design/club_delivery_chart_design.md §9 stage 6): the golfer's own
// down-the-line frames at address, the top and P6, with the ADDRESS LINE and the SHOULDER PLANE drawn on them —
// both FROZEN at address and drawn identically on all three, so the golfer sees where the club and the hands sit
// against them at the top and at P6. The slot between them is a faint wash. No pro body, no norm angle: the
// golfer's own lines only. The twin of the Posture strip (posture_picture.h), built the same way: pure C++ over
// the swing document; the Charts Club delivery presenter puts it on `thisShot.picture`; the QML only scales what it
// is given with the image (PpChDeliveryPicture → PpChPostureFrame).
//
// ── WHICH FRAME, WHICH CROP ──────────────────────────────────────────────────────────────────────
// The Posture picture's DTL reading (pictureCameraDocFrom: the report stills' golfer crop, widened sideways to the
// golfer's mask marks), the position's instant (positionInstantUs: P1, P4, P6) and the NEAREST encoded frame
// (nearestFrame) — the report stills' rule, so the still is cut where the lines were placed (StillRequest::cropPx).
// One departure: the crop is widened to take in the ball, plus the stills' body margin (kBallMargin of the crop's
// height), since every line starts there.
//
// ── THE LINES (normalised in the frame, then in the frame's own crop-local pixels) ────────────────────
//   · THE ADDRESS LINE — the shaft line at address: from the DTL ball (clubDtl.summary.ball, the ball the hand
//     plane and the shoulder plane are measured from) through the address grip (the MEASURED DTL club sample
//     nearest address, within 60 ms — not coasted / held / predicted / off-frame / synthesised; the replay's address
//     shaft plane, AutoAnnotations.js planeLine, takes the nearest sample and refuses it when it is held, which leaves
//     no line on a swing whose nearest frame is a held one), on to the frame's edge. Its view-plane angle is what the
//     swing plane curve reads at P1. No ball: the shaft itself, grip → head, extended both ways, said.
//   · THE SHOULDER PLANE — from the ball through the TRAIL shoulder at address, on to the edge: the `shoulderPlane`
//     metric's own point (shaft_plane_curve.h shoulderPlaneFromHold, as ShaftFusionStage reads it: the trail
//     shoulder's per-axis median over the last 15 confident raw DTL pose frames before P1 − 100 ms), so the drawn
//     line IS the metric. Drawn only when the document carries the metric (an older analysis: said) and the ball.
//   · THE SLOT — a faint wash in the wedge between the two, from the ball out (drawn past the frame and clipped by
//     it). Only when both lines are drawn.
// Every mark is role "coach", set "plane": the strip draws them whatever the reader's Coach lines toggles are (they
// are the section's reading, the plot's own two hairlines, not an overlay to switch off; the strip would otherwise go
// blank) — `defaultSets` { plane: true } is what the QML hands the frame.
//
// ── SHAPE (posture_picture.h's, one camera) ─────────────────────────────────────────────────────────
//  picture: { available, swingDir, ordinal, legend, why: [string], pending, defaultSets: { plane: true },
//             lines: { address (bool), shoulders (bool), slot (bool) },
//             groups: [{ camera "dtl", label "down the line", frames: [FRAME × 3] }] }   (no DTL camera: groups [],
//             why [], available false — the strip draws nothing at all)
//  FRAME = posture_picture.h's FRAME: { key ("dtl_P6"), camera, tag ("P1" | "P4" | "P6"), pos (0 | 3 | 5),
//            when ("address" | "the top" | "P6"), caption, note ("" — the presenter fills it from the plane story),
//            tUs, seekUs, frameUs, frameIndex, width, height, frameWidth, frameHeight, crop, src, imageWhy, still, marks }
//  MARK  = { kind ("polyline" | "poly"), role "coach", set "plane", label ("address line" | "shoulder plane" |
//            "address shaft line" | "the slot"), dash ("" | "dash"), hueKey "", pts: [[x, y], …] }
//          "poly" is a filled polygon (the slot's wash).

#include <QHash>
#include <QJsonObject>
#include <QPointF>
#include <QRect>
#include <QString>
#include <QVariantMap>

#include <array>
#include <limits>

namespace pinpoint::charts {

inline constexpr int kDeliveryPictureFrames = 3;   // address, the top, P6

struct DeliveryPictureDoc {
    bool    ok = false;                       // the document was read
    QString why;
    QString swingDir, stamp;
    bool    dtl = false;                      // the swing has a down-the-line video
    int     W = 0, H = 0;
    QRect   cropPx;                           // frame px: the posture DTL crop, widened to the ball
    std::array<qint64, kDeliveryPictureFrames> posUs{ { -1, -1, -1 } };
    std::array<qint64, kDeliveryPictureFrames> frameUs{ { -1, -1, -1 } };
    std::array<int, kDeliveryPictureFrames>    frameIndex{ { -1, -1, -1 } };
    bool    hasBall = false;  QPointF ball;   // normalised
    bool    hasClub = false;  QPointF grip, head;
    // the shoulder plane: the document's metric, and the trail shoulder it was read through (normalised)
    bool    shoulderMetric = false;
    double  shoulderDeg = std::numeric_limits<double>::quiet_NaN();
    bool    hasShoulder = false;  QPointF shoulder;
    int     shoulderFrames = 0;
    QString shoulderWhy;                      // why there is no shoulder line ("" when there is one)
};

// The document → the picture's inputs. `root` is the whole swing document (SwingStore::load).
DeliveryPictureDoc deliveryPictureDocFrom(const QJsonObject &root, const QString &swingDir, const QString &stamp = QString());

struct DeliveryPictureOptions {
    QString stillDir;                         // where the stills are cached ("" → no outPath, no src)
    int     maxPx = 900;
    int     ordinal = 0;
    QHash<QString, QString> stillErrors;      // outPath → why the still could not be made
};

QVariantMap deliveryPicture(const DeliveryPictureDoc &doc, const DeliveryPictureOptions &opt);

// The picture's reason when the swing's document could not be read at all.
QVariantMap deliveryPictureUnread(const QString &swingDir, const QString &why);

} // namespace pinpoint::charts
