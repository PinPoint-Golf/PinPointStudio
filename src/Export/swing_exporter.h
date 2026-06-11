/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <vector>

#include "types.h"

namespace pinpoint {

class SwingWindow;

// One camera to export.  alias/fileName are resolved (sanitised, deduped) on
// the UI thread before the job is handed to the worker.
struct SwingExportCamera {
    SourceId sourceId = kInvalidSourceId;
    QString  alias;        // human label recorded in swing.json
    QString  fileName;     // "<alias>.mp4"
};

// One camera's 2D pose series for the swing window, resolved on the UI thread.
// The swing exporter only *serialises* whatever is present here — it does NOT
// run pose estimation. There is no producer yet (pose buffering / the analyzer
// pose pipeline are a separate scope), so this is normally empty and nothing is
// written; the plumbing is in place for when a producer lands.
struct SwingPoseStream {
    QString alias;                 // camera label (matches the video stream alias)
    QString serial;                // camera serial, for cross-stream correlation
    std::vector<int64_t> tUs;      // per-frame timestamps, window-relative (us)
    // 17 COCO keypoints per frame as (y, x, score) normalised 0..1, flattened to
    // 51 floats per frame; size MUST be tUs.size() * 51.
    std::vector<float> keypoints;
};

// Self-contained job description.  Everything that touches QSettings or
// controllers is resolved on the UI thread; the worker sees only values.
struct SwingExportJob {
    QString swingDir;      // absolute, already created by SwingPaths
    QString swingId;       // "swing_0007"
    int     swingIndex = 0;

    std::vector<SwingExportCamera> cameras;
    QHash<QString, QString> imuAliasBySerial;  // device serial/id -> alias
    std::vector<SwingPoseStream> poseStreams;  // pose to serialise (empty today)

    QString codec = QStringLiteral("h264");  // AppSettings videoCodec -> factory key
    int  crf     = 23;     // from AppSettings videoQuality
    bool saveImu = true;   // AppSettings saveImuStreams

    // AppSettings videoResolutionMode — export-time downscale (never upscale):
    // "native" (source), "half" (½), "1080p" / "4k" (fit to that line count).
    QString resolutionMode = QStringLiteral("native");
    // AppSettings saveRawFrames — also dump the undecoded sensor payloads to an
    // "<alias>.raw" sidecar (single concatenated blob) per camera.
    bool    saveRaw = false;
    // AppSettings imuDataFormat — "json" (inline in swing.json), "csv", or
    // "binary"; csv/binary write an "imu_<alias>.<ext>" sidecar instead.
    QString imuFormat = QStringLiteral("json");
    // AppSettings savePoseKeypoints — gate for serialising poseStreams (above).
    bool    savePose = true;

    QString athleteName, athleteUuid, handedness;
    QString sessionId;     // session folder name, e.g. "2026-06-05_Mark-Liversedge_Swing_01"

    // UTC instant snapshotted on the UI thread right after the window was
    // captured — at that moment wallclock ~= monotonic endTimestampUs().
    QDateTime wallclockAnchorUtc;

    // Impact thumbnail (thumb.jpg in the swing dir): the frame nearest this
    // instant from the designated camera (face-on, else the first exported
    // stream as a fallback). -1 disables thumbnail extraction.
    SourceId thumbnailSourceId    = kInvalidSourceId;
    int64_t  thumbnailTimestampUs = -1;

    // Video encode span (segmentation v3 G2): camera frames outside
    // [encodeStartUs, encodeEndUs] are skipped — MP4s span address → finish
    // instead of the raw 5 s ring. 0/0 = encode the full window. Raw IMU
    // streams in swing.json stay full-window regardless (cheap, and lets
    // segmentation be re-run offline).
    int64_t encodeStartUs = 0;
    int64_t encodeEndUs   = 0;
};

struct SwingExportResult {
    bool        ok = false;
    QString     swingDir;
    QString     error;
    QString     thumbnailPath;   // absolute path to thumb.jpg; empty if none written
    QJsonObject manifest;        // the raw pinpoint.swing tree (no "analysis"); the GUI
                                 // thread writes the unified swing.json at the join.
};

// Stateless worker entry point.  Runs on a worker thread; the window must stay
// alive (buffer Paused) until this returns — enforced by CameraManager's
// resume gating.  Peak extra memory is one BGR scratch frame plus the
// encoder's single YUV frame; payloads are read zero-copy from the window.
class SwingExporter {
public:
    static SwingExportResult run(const SwingWindow& window, const SwingExportJob& job);
};

} // namespace pinpoint
