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

// THE RECORDING LOADER — a swing folder → the Recording (refactor step 1.2; docs/developer/
// information_model.md §2). Moved out of src/Analysis/swing_reanalyzer.cpp, where it built the
// analysis job itself: it now gives back the RECORDING and the shot's club review, and the one job
// builder (src/Analysis/analysis_job_builder.h) makes the job from them.
//
//   loadRecording(swingDir) → { the disk-backed window, the Recording that points into it,
//                               the review's ClubReview { correction, declared }, … }
//
// What it reads, all of it window-relative (the store's own clock; the Recording's t0Us is 0):
//   streams[]        video (raw sidecar preferred, else the MP4, decoded one frame at a time into
//                    one reusable buffer per camera: memory stays one frame per camera, never the
//                    window), imu (samples in RAM), ball (the recorded stream)
//   setup            each camera's perspective (the one mapping; SwingLab's --face-on substring
//                    override is a load option), mirrored, fixedInPlace, the face-on ball search
//                    box and empty-mat baseline (validated: dims, roi, the blob on disk)
//   capture          session type, shot source, motion-capture quality, the impact (capture.
//                    impactUs with the legacy acoustic-anchor de-bias, else the recorded Impact
//                    phase, else the thumbnail instant)
//   bindings         analysis.bindings (serial-keyed), else each IMU stream's device.* snapshot
//                    (a HackMotion lane rebuilt by hm_binding_recon.h; its refusals → loadGates)
//   integrity        the host orientation filter of the host-fused lanes, the recorded `window`,
//                    the off-clock (phone-clip) cameras
//   analysisContext  the recorded block (recording_header.h), or the legacy context for a document
//                    older than it; a newer schema → readOnly
//
// Reading never writes the document.
//
// THE RECORDED BALL STREAM is dropped here when the whole of it lies outside the loaded window (the
// old wrong-time-base recording bug: warn and drop). Whether a valid baseline makes the BallRunner
// authoritative is the job builder's rule (D5), not the loader's.
//
// The types it returns (LoadedRecording, RecordingLoadOptions) are the domain's reading interface's
// (src/DomainModel/recording_reader.h); SwingStoreRecordingReader below is that interface's adapter.
//
// Adapters ring (src/Export): the only code that turns a swing folder into a Recording.

#include <QString>

#include "../DomainModel/recording_reader.h"   // LoadedRecording, RecordingLoadOptions, RecordingReader

namespace pinpoint {

LoadedRecording loadRecording(const QString &swingDir, const RecordingLoadOptions &opts = {});

// THE ADAPTER of the domain's RecordingReader (src/DomainModel/recording_reader.h, step 1.5): the
// swing store's reading of a Recording, i.e. loadRecording. The composition root holds one (each
// tool's main) and hands it to the analysis service. Stateless.
class SwingStoreRecordingReader final : public RecordingReader {
public:
    LoadedRecording load(const QString &swingDir, const RecordingLoadOptions &options = {}) override;
};

} // namespace pinpoint
