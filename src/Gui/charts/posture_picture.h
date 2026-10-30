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

// THE POSTURE PICTURE STRIP (docs/design/posture_chart_design.md §5.4, stage 5): the golfer's own frames at
// address, the top and impact, down the line and face-on (each camera only where the swing has it), with the
// address coach lines FROZEN on all three and the good-player bands drawn as OFFSETS OF THE GOLFER'S OWN LINES —
// never a pro body. Pure C++ over the swing document; the Charts presenter puts it on `thisShot.picture`, the report
// on `posture.picture` (for the session's typical swing). The QML only scales what it is given with the image.
//
// ── WHICH FRAME, WHICH CROP (one rule with the report stills) ────────────────────────────────────────────
// Each frame is the still report_stills makes for that camera at that position: the position's instant
// (positionInstantUs), the NEAREST encoded frame (nearestFrame), the golfer's privacy crop (stillGolferCrop →
// stillCropPixels, the SAME for the three frames of a camera). The picture fixes the crop in frame pixels and the
// still is cut there (StillRequest::cropPx), so the lines land on the pixels they were measured on.
// The one departure: the crop is widened SIDEWAYS to the golfer's own mask marks (the butt line, the hip edges, the
// butt at impact) plus the stills' body margin — the crop bounds the joints, and the butt sits behind them.
//
// ── THE COACH LINES (AutoAnnotations.js, the replay camera's, in C++) ───────────────────────────────────
// The same marks the replay tile draws from the replay payload (src/Gui/cameras/AutoAnnotations.js build()), read
// here from the swing document that payload is made from: face-on the head circle (the mask's head outline, else
// the address pose's head points; radius a fraction of the frame HEIGHT) and the hip edges (analysis.addressMarks.faceOn); down the
// line the butt line (addressMarks.dtl.buttX), the address shaft plane (the measured DTL club sample nearest address,
// through the DTL ball), the Hogan line (ball → the trail shoulder), the spine (the trail hip joint → the neck,
// halfway from the shoulder midpoint to the ears: it joins the trail leg), the shoulder plane (parallel, 0.25 of its
// length above the trail shoulder) and the trail leg. Each carries its `set` (posture | plane | headHips); the reader's Coach lines toggles
// pick which are drawn. Frozen at address: the same lines on the top and impact frames.
//   · NOT drawn: the golfer's back line at the top. It was held at the 22 Oct gate (§2.3: it catches the arms at
//     the top); the address spine line stands alone there.
//
// ── THE BAND OFFSETS (§5.4, on the golfer's own lines) ────────────────────────────────────────────────
//   · DTL impact — a faint strip in front of the address butt line where good players' hips sit at impact: the
//     hip-depth band at P7 (the presenter's bandLo…bandHi, cm, + toward the ball) converted with the persisted DTL
//     ruler: x = buttX·W + toward · cm / cmPerPx (analysis.dtlRuler — the scale pelvisThrust was measured in). No
//     ruler → no strip, and the frame's note says why.
//   · Face-on impact — anchored on the MEASURED head point (Mark, 22 Oct): a tick "your head at address" at the
//     tracked head centre at address (trackHead's centre, headLift / headSway read at P1 by the chart's sampler),
//     the head-height band at P7 (cm, + up; the presenter's band) hung from it, y = yAddr − cm · pxPerCm, with the
//     inter-ear ruler headLift is measured with (head_track.h trackHead: the median ear span over the address hold
//     ÷ head.earWidthMm, px → pxPerCm = 10 · span / earWidthMm, the same px → cm as buildHeadSeries' linGain), and
//     the golfer's own head centre at impact as a white tick, its rise said in the note ("your head at impact:
//     1.4 cm higher" — headLift P7 − P1, the chart's cell). No ruler → no band, said. The band is NOT anchored on
//     the head circle's top: headLift measures the head's centre, and the circle is a drawing of the skull.
//   · The head circle (coach lines, face-on): the person mask's head outline (analysis.addressMarks.faceOn.head*,
//     stage version 4), else the face points' circle as the replay draws it — said in `why`.
//   · DTL impact — the golfer's OWN butt line at impact (addressMarks.dtlPhases, Impact: stage 4a) in ink against the
//     frozen address butt line, with its travel in cm (address_marks.h buttShiftCm) in the frame's note.
//
// ── SHAPE (every coordinate in the FRAME'S OWN IMAGE: crop-local pixels of the native frame) ────────────
//  picture: {
//    available (a frame to show), swingDir, ordinal, typical (the report's: "" on Charts), legend,
//    why: [string] (what is absent and why, said once: no address marks, a camera missing, the document unread),
//    defaultSets: { posture, plane, headHips } (drawn when the reader has no Coach lines set on, and in print),
//    pending (a frame's still is not made yet),
//    groups: [{ camera ("dtl" | "faceOn"), label ("down the line" | "face-on"), frames: [FRAME] }]  (DTL first) }
//  FRAME = { key ("dtl_P1"), camera, tag ("P1" | "P4" | "P7"), pos (0 | 3 | 6), when ("address" | "the top" | "impact"),
//            caption ("impact: good players' hips sit in the shaded strip"), note ("" | the frame's one line),
//            tUs (the position's instant, window-relative), seekUs (= tUs; −1 none), frameUs (the still's frame),
//            frameIndex, width, height (the crop, px: the image space every mark is in), frameWidth, frameHeight,
//            crop: { x, y, w, h } (frame px), src (a file URL | ""), imageWhy ("" | why there is no picture),
//            still: { swingDir, camera, phase, x, y, w, h, maxPx, outPath } (what to make), marks: [MARK] }
//  MARK  = { kind ("circle" | "vline" | "polyline" | "rect"), role ("coach" | "band" | "ink"), set ("posture" | "plane" |
//            "headHips" | ""), label, dash ("" | "dash" | "dot" | "long"), hueKey ("" | the band's metric),
//            cx, cy, r (circle) | x (vline) | pts: [[x, y], …] (polyline) | x, y, w, h (rect) }
//          Face-on impact also carries the tick "your head at address" (polyline, role "band": the band's anchor, drawn
//          whatever the reader's sets) and "your head at impact" (polyline, role "ink").

#include <QHash>
#include <QJsonObject>
#include <QPointF>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <array>
#include <cmath>
#include <limits>

namespace pinpoint::charts {

inline constexpr int    kPicturePositions = 3;   // address, the top, impact
inline constexpr double kPicNaN = std::numeric_limits<double>::quiet_NaN();

// One camera's reading of the document: everything the picture draws, small (no pose blob is kept).
struct PictureCameraDoc {
    bool    present = false;                  // the swing has this camera's video
    bool    dtl = false;
    int     W = 0, H = 0;                     // the encoded frame
    QRect   cropPx;                           // the stills' golfer crop, frame px
    std::array<qint64, kPicturePositions> posUs{ { -1, -1, -1 } };     // the positions' instants (window-relative)
    std::array<qint64, kPicturePositions> frameUs{ { -1, -1, -1 } };   // the still's frame time
    std::array<int, kPicturePositions>    frameIndex{ { -1, -1, -1 } };
    QVector<float> addressKp;                 // COCO 17 × (x, y, conf), the pose frame nearest address (≤ 60 ms)
    // analysis.addressMarks for this view (normalised; NaN = not read)
    bool    marksFound = false;
    double  leftX = kPicNaN, rightX = kPicNaN, buttX = kPicNaN, rowY = kPicNaN;
    // the head outline off the mask (stage version 4): the circle (cx ÷ W, cy and r ÷ H), else why not
    bool    headFound = false;
    double  headCx = kPicNaN, headCy = kPicNaN, headR = kPicNaN;
    QString headRefused;
    // DTL: the ball and the measured club sample nearest address (normalised)
    bool    hasBall = false;  QPointF ball;
    bool    hasClub = false;  QPointF grip, head;
    // DTL: the butt line at impact (addressMarks.dtlPhases, the Impact entry)
    bool    impactButtFound = false;
    double  impactButtX = kPicNaN;
    QString impactButtRefused;
};

struct PictureDoc {
    bool    ok = false;                       // the document was read
    QString why;                              // why not
    QString swingDir, stamp;                  // stamp: the document's size + mtime (the stills' cache key)
    bool    leadIsLeft = true;
    int     marksVersion = 0;                 // analysis.addressMarks stageVersion; 0 = never measured
    PictureCameraDoc faceOn, dtl;
    double  dtlCmPerPx = kPicNaN;   // analysis.dtlRuler
    int     dtlToward = 0;
    QString dtlRulerWhy;                      // why there is no DTL ruler ("" when there is one)
    double  headPxPerCm = kPicNaN;  // the face-on inter-ear ruler
    QString headRulerWhy;
    // The face-on head height at address and at impact (THE BAND OFFSETS, face-on): the tracked head centre
    // (head_track.h trackHead) as headLift and headSway carry it, read at P1 and P7 by the chart's own sampler
    // (measure_sample.h buildPhaseGrid: the windowed median at the position's instant), in frame px.
    // liftCm[] is headLift there (cm, + up); its P7 − P1 is the chart's head-height cell at P7.
    bool    headTicks = false;
    QPointF headAddrPx, headImpactPx;
    double  headLiftAddrCm = kPicNaN, headLiftImpactCm = kPicNaN;
    QString headTicksWhy;
};

// The document → the picture's inputs. `root` is the whole swing document (SwingStore::load).
PictureDoc postureDocFrom(const QJsonObject &root, const QString &swingDir, const QString &stamp = QString());

// One camera's reading of the document, as the picture reads it (the stills' frame and crop at address, the top and
// impact, widened to the golfer's mask marks; the address pose; down the line the ball and the measured club sample
// nearest address): the Club delivery picture (delivery_picture.h) reads its frames, ball and address grip HERE, so
// the two strips cut the same stills and draw from the same points. No head track. `present` false when the swing has
// no such camera or no analysis.
PictureCameraDoc pictureCameraDocFrom(const QJsonObject &root, bool dtl);

// The bands at impact the picture offsets the golfer's lines by (the presenter's cells at P7, the pattern in use).
struct PictureBands {
    bool    hipHas = false;   double hipLo = kPicNaN, hipHi = kPicNaN;
    bool    headHas = false;  double headLo = kPicNaN, headHi = kPicNaN;
    QString patternName;                      // "Level"
    QString hipHueKey = QStringLiteral("pelvisThrust"), headHueKey = QStringLiteral("headLift");
};

struct PictureOptions {
    QString stillDir;                         // where the stills are cached ("" → no outPath, no src: the caller makes them)
    int     maxPx = 900;                      // the still's long edge
    int     ordinal = 0;
    QHash<QString, QString> stillErrors;      // outPath → why the still could not be made (the maker's answer)
};

QVariantMap posturePicture(const PictureDoc &doc, const PictureBands &bands, const PictureOptions &opt);

// The picture's reason when the swing's document could not be read at all.
QVariantMap posturePictureUnread(const QString &swingDir, const QString &why);

// The figure words the picture's notes use: "7.6 cm toward the ball" | "1.2 cm back from the ball".
QString postureButtTravelWords(double cm);
// The head's rise at impact: "1.4 cm higher" | "0.6 cm lower" | "level with address".
QString postureHeadRiseWords(double cm);

} // namespace pinpoint::charts
