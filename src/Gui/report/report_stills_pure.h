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

// THE PURE PARTS OF THE REPORT STILLS (report_stills.h has the renderer): which frame a still takes, the
// golfer's crop, the body line's points, the exposure curve — Qt Core and JSON only, no decoder. Split out of
// report_stills.h on 22 Oct 2026 (posture chart stage 5) so the Charts Posture picture can use them inside the
// QML module, which links no OpenCV. report_stills.h includes this header; nothing else changed.

#include <QJsonObject>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QVector>

#include <array>
#include <cstdint>
#include <optional>

namespace pinpoint::report::stills {


// A drawable body segment: a chain of points, each the mean of one or more COCO body keypoints (the
// spine runs mid-hip → mid-shoulder). `joints` empty = a name outside the vocabulary.
struct Segment {
    QVector<QVector<int>> joints;
    bool    lowerBody = false;   // which per-keypoint gate applies (lowerBody vs upperBody kConfMin)
    bool    club = false;        // `shaft`: the club's grip → head from the camera's shaft track, not the pose
    QString words;               // golfer words for the part ("trail thigh"), for the note
    bool known() const { return club || !joints.isEmpty(); }
};

// "Left" → the right side leads (leadIsLeft false); anything else, including unknown, is the
// right-handed convention every body module uses (`handedness != 2`).
bool leadIsLeftFor(const QString &handedness);

// The segment vocabulary of CONTRACTS.md §1. leadArm / trailArm run shoulder → elbow → wrist, so a bent
// arm is drawn bent; the forearm and upper-arm names are the two halves. `shaft` (added for the club
// readings — plane, lean, lie, the shaft at the top, the takeaway) is the club, grip → head: see shaftTrack.
Segment segmentFor(const QString &line, bool leadIsLeft);

// One camera's club shaft through the swing, as the replay overlay reads it, window-relative and ascending.
//   face-on — analysis.club.samples (only when club.valid, as the overlay gates it). A sample is DRAWABLE
//             when vision measured the shaft at it: ShaftMeasured (0x01) or ShaftWedge (0x08, a blur-wedge
//             measurement), not ShaftImplausible (0x200), and headConf at or above the club.headConfidence
//             gate (0.30) when the sample carries one. A coasted sample (prediction only) is not.
//             A measured angle with a projected head (0x10: grip + the club's measured length along θ) is
//             drawn — the direction is the measurement.
//   DTL     — analysis.clubDtl.frames: drawable on the tiers that measured an angle (RAY / SEG / BAND) with
//             both points; END_ON, UNSEEN and the band's HELD estimate are not (dtl_overlay_payload: the
//             overlay draws HELD dim, as a guess; a report still does not draw guesses).
// grip / head are normalised to that camera's frame.
struct ShaftSample {
    qint64  tUs = 0;
    QPointF grip, head;
    bool    drawable = false;
};
QVector<ShaftSample> shaftTrack(const QJsonObject &analysis, bool dtl, qint64 t0);

// The shaft at `us`: the NEAREST sample within maxUs, and only if it is drawable — a hole in the
// measurement is not filled from a neighbour. Empty when there is none.
QVector<QPointF> shaftAt(const QVector<ShaftSample> &track, qint64 us, qint64 maxUs);

// The segment's points (normalised, full frame) from a flat x,y,conf keypoint list. Empty when any joint
// is missing or below confMin; *weak then names the part in golfer words.
QVector<QPointF> segmentPoints(const Segment &seg, const QVector<float> &kp, double confMin,
                               QString *weak = nullptr);

// The golfer's own extent through the swing: per-frame bounding boxes of the confident body keypoints
// (0–16), then a robust envelope (3rd percentile of the minima, 97th of the maxima) so one stray keypoint
// cannot widen it. nullopt when fewer than 3 frames contribute.
std::optional<QRectF> bodyExtent(const QVector<QVector<float>> &frames, double confMin);

// The crop, normalised to the full frame. Margins are fractions of the rect they grow:
//   cropRect + cropMargin each side, clamped to the frame; horizontally bounded by body + bodyMargin
//   (bodyMargin a fraction of the body's HEIGHT, so it scales with how big the golfer stands in frame);
//   no cropRect → body + cropMargin (horizontal bodyMargin);
//   neither → the centre 60% × 100% of the frame, and *note says the golfer was not found.
// `source`, when given, receives "pose" | "body" | "centre".
QRectF golferCrop(const std::optional<QRectF> &cropRect, const std::optional<QRectF> &body,
                  double cropMargin, double bodyMargin, QString *note = nullptr, QString *source = nullptr);

// Bring the crop to a width/height `aspect` WITHOUT taking in more of the room sideways. A crop too wide
// for the aspect grows downwards and upwards within the frame; a crop too narrow is never widened — it is
// padded. Returns the source rect (normalised) and, in *padX / *padY, how much padding the output needs
// as a fraction of the source rect's width / height (split evenly both sides). aspect <= 0 → the crop
// unchanged, no padding. frameAspect = frame W / H (the rect is normalised, the aspect is in pixels).
QRectF fitAspect(const QRectF &crop, double aspect, double frameAspect, double *padX, double *padY);

// The exposure curve for a crop, from its 256-bin luminance histogram. Monotone non-decreasing, f(0) = 0,
// never above 255; the identity on a well-lit crop (p1 ≤ 16 and p99 ≥ 235 and a median that is not dark).
std::array<uint8_t, 256> exposureCurve(const std::array<quint32, 256> &lumaHist);

// "P1" … "P10" → the analyzer Phase enum (swing_analysis.h) and the P number; -1 when not a position.
int phaseEnumFor(const QString &position);
int positionNumber(const QString &position);

// The window-relative instant of a position in a swing document's `analysis` block: analysis.phases
// first (the analyzer's own events), then analysis.club.positions (the P-position ladder, which carries
// P8 / P9 when the phase list does not). nullopt when the swing has neither. t0 = clock.t0_us.
std::optional<qint64> positionInstantUs(const QJsonObject &analysis, const QString &position, qint64 t0);

// An analysis t_us in the window-relative domain (DiskReplaySource's rule: live captures write absolute,
// re-analysed swings write relative; subtract t0 only from an absolute value).
inline qint64 relUs(qint64 t, qint64 t0) { return t >= t0 ? t - t0 : t; }

// Nearest frame for a window-relative instant (the replay's rule); -1 for no frames.
int nearestFrame(const QVector<qint64> &frameTimesUs, qint64 us);

// One pose sample: window-relative time and the flat x,y,conf list (133 × 3, normalised).
struct PoseSample {
    qint64         tUs = 0;
    QVector<float> kp;
};

// The keypoints at `us`: the nearest sample when it is within maxNearestUs; else, when the two samples
// bracketing `us` are both within maxBracketUs of it, their linear blend (conf = the lower of the two);
// else empty. Samples must be ascending by tUs.
QVector<float> poseAt(const QVector<PoseSample> &samples, qint64 us, qint64 maxNearestUs,
                      qint64 maxBracketUs);

// ── Document-level helpers, shared by the renderer and the Charts Posture picture (stage 5) ──────────
// So the picture can say which frame and which crop a still takes, and draw its lines in that still's
// pixels, without decoding: the renderer takes the SAME frame (positionInstantUs → nearestFrame) and the
// SAME crop (stillGolferCrop → stillCropPixels), or the crop the request names (StillRequest::cropPx).

// One camera's pose as the stills read it: the `smoothed` track when the swing has one, COCO body keypoints
// only (the first 17 × (x, y, conf)), window-relative and ascending, and the pose runner's cropRect.
struct PoseTrackLite {
    bool                  present = false;   // the block exists at all
    QVector<PoseSample>   samples;           // ascending, window-relative
    std::optional<QRectF> cropRect;
};
PoseTrackLite readPoseTrack(const QJsonObject &analysis, bool dtl, qint64 t0);

// The golfer's own extent through the swing, address to finish (or the last position the swing names),
// padded 200 ms either side: bodyExtent over those samples. nullopt as bodyExtent.
std::optional<QRectF> swingBodyBand(const PoseTrackLite &pose, const QJsonObject &analysis, qint64 t0);

// The crop a still takes: golferCrop(pose.cropRect, band) with the stills' margins (12 % of the pose crop,
// 6 % of the golfer's height). Normalised.
QRectF stillGolferCrop(const PoseTrackLite &pose, const std::optional<QRectF> &band, QString *note = nullptr);

// The normalised source rect in frame pixels, as the renderer cuts it (floor of the near edges, ceil of the
// far ones, inside the frame).
QRect stillCropPixels(const QRectF &srcN, int W, int H);

// One camera's video stream in a swing document, by the SAME rule as markup::readVideoStream: face-on =
// perspective 2, else an alias holding "Face", else the first video; DTL = perspective 1, else the whole-token
// "dtl" name — never the face-on pick. `width` / `height` are the stream's encoded size (0 when not recorded).
struct StillStream {
    bool            ok = false;
    QString         file;
    QVector<qint64> tUs;
    int             width = 0, height = 0;
};
StillStream stillStreamOf(const QJsonObject &root, bool dtl);

} // namespace pinpoint::report::stills
