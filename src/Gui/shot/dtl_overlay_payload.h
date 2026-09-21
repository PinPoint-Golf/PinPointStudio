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

// The down-the-line replay-overlay payload — `detail.dtl`.
//
//   dtl = { pose2d: { frames, smoothed, synth, … },
//           club:   { valid, samples, positions, frameWidth, frameHeight },
//           ball:   { samples } }
//
// The SAME keys and element shapes as the face-on `pose2d` / `club` / `ball` blocks,
// so PpCameraFrame's replay painter draws the DTL tile with no second code path. It
// is NESTED under `dtl` rather than living beside them because several readers treat
// a top-level `pose2d`/`club` as face-on (shot_replay_controller's hasFaceOn,
// PpMotionPanel's availability) and must go on doing so.
//
// ONE translator for both bridges: ShotProcessor's live toAnalysisDetail (the
// in-memory DtlShaftTrack2D, serialised through dtlShaftTrackToJson with t0 = 0 so
// times stay absolute) and DiskReplaySource::load (swing.json's analysis.clubDtl,
// re-timed window-relative). What the translator deliberately does NOT do
// (dtl_shaft_tracker_design §5.9): no synth, no prediction, no bridging — a frame
// that did not publish an angle (tier below RAY) is not a sample, so the shaft
// visibly disappears across the end-on and occluded bands.

#include <QJsonObject>
#include <QJsonValue>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

namespace pinpoint {

// `pose2d`      — the DTL pose block already shaped exactly like the face-on
//                 `detail.pose2d` (empty ⇒ no pose on the DTL tile).
// `clubDtl`     — a `pinpoint.clubDtl/1` object (empty ⇒ no shaft, no ball).
// `foPositions` — the face-on `club.positions` list ({p, t_us, …}), already in
//                 the payload's time domain; each is looked up on the DTL track.
// `impactUs`    — the Impact phase in the payload's time domain; < 0 = unknown.
// `retime`      — maps a clubDtl `t_us` value into the payload's time domain
//                 (identity on the live path; relUs(·, t0) on the disk path).
//
// Returns an empty map when there is nothing to draw, so the caller can omit
// `dtl` altogether.
QVariantMap dtlOverlayDetail(const QVariantMap &pose2d,
                             const QJsonObject &clubDtl,
                             const QVariantList &foPositions,
                             qint64 impactUs,
                             const std::function<qint64(const QJsonValue &)> &retime);

} // namespace pinpoint
