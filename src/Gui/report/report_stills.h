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

#include <QJsonObject>
#include <QObject>
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

// ── The pure parts, exposed for report_stills_test ───────────────────────────────────────────────────
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

} // namespace pinpoint::report::stills
