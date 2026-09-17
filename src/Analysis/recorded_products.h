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

// Readers that rebuild the RECORDED analysis products from a swing document, for
// version-gated reuse (analysis_versions.h, swing_reanalyzer.cpp).
//
// The pose track has had this since the gate existed (PoseRunner::fromJsonObject); the
// ball track too (BallRunner::fromAnalysisJson). The shaft track and the phase ladder
// did not, so every re-analysis — including a metrics-only one — re-ran the tracker on
// whatever frames the swing directory still held. For a swing with no raw sidecar that
// is the mp4, and on 2026-09-17 a sweep that only wanted to add four metric series
// replaced nine good live tracks with coasting ones because a compressed, dim club is
// not the club the live tracker saw. These readers are the other half of the gate.
//
// EXACT INVERSES of swing_doc.cpp's `club`, `segmentation` and `phases` writers, field
// for field; a field the writer omits (predicted / synth / positions when empty,
// lineConf when unmeasured, timing on an unfused ladder, fusion when empty) comes back
// as its in-memory default. Times are taken AS WRITTEN — the document domain — exactly
// as the pose reader does; the caller's window is that domain on the re-analysis path.
//
// What a rebuilt track does NOT carry: `addressPhaseFrame` and `onsetFloorFrame` (never
// persisted; both read −1 = dark) and the Layer C synth tier as recorded — the Shaft
// stage re-synthesises it from the samples (shaft_track_assembly.h resynthesizeLayerC)
// so the visualisation follows the current rule rather than the one in force when the
// swing was captured.

#include "swing_analysis.h"   // ShaftTrack2D, Segmentation

#include <QJsonObject>

#include <optional>

namespace pinpoint::analysis {

// `club` is `analysis.club`. Returns a track with `samples` empty (and `valid` false)
// when the block is absent, has no samples, or carries no frame size to de-normalise by.
ShaftTrack2D shaftTrackFromAnalysisJson(const QJsonObject &club, pinpoint::SourceId camera);

// `analysis` is the whole `analysis` object: the ladder is `phases[]` plus the
// `segmentation` bounds / conf / version / fusion record. nullopt when there are no phases.
std::optional<Segmentation> segmentationFromAnalysisJson(const QJsonObject &analysis);

} // namespace pinpoint::analysis
