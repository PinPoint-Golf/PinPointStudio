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

// Self-contained job description.  Everything that touches QSettings or
// controllers is resolved on the UI thread; the worker sees only values.
struct SwingExportJob {
    QString swingDir;      // absolute, already created by SwingPaths
    QString swingId;       // "swing_0007"
    int     swingIndex = 0;

    std::vector<SwingExportCamera> cameras;
    QHash<QString, QString> imuAliasBySerial;  // device serial/id -> alias

    QString codec = QStringLiteral("h264");  // AppSettings videoCodec -> factory key
    int  crf     = 23;     // from AppSettings videoQuality
    bool saveImu = true;   // AppSettings saveImuStreams

    QString athleteName, athleteUuid, handedness;
    QString sessionId;     // session folder name, e.g. "2026-06-05_session-01"

    // UTC instant snapshotted on the UI thread right after the window was
    // captured — at that moment wallclock ~= monotonic endTimestampUs().
    QDateTime wallclockAnchorUtc;

    // Impact thumbnail (thumb.jpg in the swing dir): the frame nearest this
    // instant from the designated camera (face-on, else the first exported
    // stream as a fallback). -1 disables thumbnail extraction.
    SourceId thumbnailSourceId    = kInvalidSourceId;
    int64_t  thumbnailTimestampUs = -1;
};

struct SwingExportResult {
    bool    ok = false;
    QString swingDir;
    QString error;
    QString thumbnailPath;   // absolute path to thumb.jpg; empty if none written
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
