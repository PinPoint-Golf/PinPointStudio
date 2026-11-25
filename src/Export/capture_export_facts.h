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

// THE RECORDING'S INPUTS IN THE EXPORT, FROM THE FREEZE (refactor step 1.5, review round 1 — H-02).
//
// The live capture takes the shot's Recording at the window freeze (src/Gui/shot/capture_context.h);
// the export runs 12–37 s later, after the analysis. Every fact the export writes that the Recording
// carries — and that a re-analysis therefore reads back as its input — must be the FROZEN value, not a
// second read of the camera or the settings: by then the ball detector has re-learned its empty-mat
// baseline over the empty mat, the golfer may have moved the search box, and a setting may have
// changed. These are those facts, and the one place the export job takes them from:
//
//   per camera   its placement (perspective), mirroring, fixed-in-place flag, the ball search box and
//                the learned empty-mat baseline (blob + dims + ROI + provenance)
//   the capture  the motion-capture quality (which pose model) and the host orientation filter each
//                host-fused IMU lane records (which the integrity rule judges against)
//   per IMU lane its output rate and A/M calibration snapshot (the bindings a skip-analysis swing is
//                re-analysed from)
//
// The capture snapshot fills these at the freeze from the live devices; the export job is given them
// here. The analysis consumed exactly these values, so the file holds what the analysis used.
//
// Adapters ring (src/Export): the export job's vocabulary, no I/O.

#include <QByteArray>
#include <QRectF>
#include <QString>
#include <vector>

#include "swing_exporter.h"                 // SwingExportJob, SwingExportCamera, SwingImuDeviceInfo
#include "../Vocabulary/Analysis/swing_analysis.h"     // ImuSegmentBinding, segmentRoleName

namespace pinpoint {

struct CaptureCameraFacts {
    SourceId source       = kInvalidSourceId;
    int      perspective  = 0;      // CameraInstance::Perspective / streams[].setup.perspective
    bool     mirrored     = false;
    bool     fixedInPlace = false;
    QRectF   searchRoi;             // the hitting-area box; empty = none
    // The learned empty-mat baseline, as CameraInstance cached it at the freeze.
    QByteArray baselineBlob;        // row-major float32, w*h*4 bytes; empty = none learned
    int        baselineW = 0, baselineH = 0;
    QRectF     baselineRoi;
    double     baselineRHat = 0.0, baselineFps = 0.0, baselineNoise0 = 1.0;
    bool baselineValid() const { return !baselineBlob.isEmpty() && baselineW > 0 && baselineH > 0; }
};

// One camera of the export job, its Recording facts from the freeze.
inline void applyCaptureCameraFacts(SwingExportCamera &cam, const CaptureCameraFacts &f)
{
    cam.perspective   = f.perspective;
    cam.mirrored      = f.mirrored;
    cam.fixedInPlace  = f.fixedInPlace;
    cam.ballSearchRoi = f.searchRoi;
    cam.ballBaselineBlob.clear();
    cam.ballBaselineW = cam.ballBaselineH = 0;
    cam.ballBaselineRoi = QRectF();
    cam.ballBaselineRHat = cam.ballBaselineFps = 0.0;
    cam.ballBaselineNoise0 = 1.0;
    if (f.baselineValid()) {
        cam.ballBaselineBlob   = f.baselineBlob;
        cam.ballBaselineW      = f.baselineW;
        cam.ballBaselineH      = f.baselineH;
        cam.ballBaselineRoi    = f.baselineRoi;
        cam.ballBaselineRHat   = f.baselineRHat;
        cam.ballBaselineFps    = f.baselineFps;
        cam.ballBaselineNoise0 = f.baselineNoise0;
    }
}

const CaptureCameraFacts *captureCameraFactsFor(const std::vector<CaptureCameraFacts> &&, SourceId) = delete;   // a temporary would leave the result dangling (#247, #248)
inline const CaptureCameraFacts *captureCameraFactsFor(const std::vector<CaptureCameraFacts> &facts,
                                                       SourceId source)
{
    for (const CaptureCameraFacts &f : facts)
        if (f.source == source) return &f;
    return nullptr;
}

// The capture-wide facts: the pose model's quality tier, and the host filter on every host-fused IMU
// device (a HackMotion unit's entry records none and keeps none).
inline void applyCaptureSettings(SwingExportJob &job, const QString &motionCaptureQuality,
                                 const QString &imuOrientationFilter)
{
    job.motionCaptureQuality = motionCaptureQuality;
    for (auto it = job.imuDeviceBySerial.begin(); it != job.imuDeviceBySerial.end(); ++it)
        if (!it.value().orientationFilter.isEmpty())
            it.value().orientationFilter = imuOrientationFilter;
}

// One host-fused IMU lane's device entry: its rate and its calibration snapshot as they stood at the
// freeze (the lane's binding in the Recording), the role it was bound under.
inline void applyCaptureImuFacts(SwingImuDeviceInfo &info, int outputRateHz,
                                 const analysis::ImuSegmentBinding &b)
{
    if (outputRateHz > 0)
        info.outputRateHz = outputRateHz;
    info.hasCalibration       = true;
    info.alignA               = b.alignA;
    info.mountM               = b.mountM;
    info.anatCalibrated       = b.anatCalibrated;
    info.calibrated           = b.calibrated;
    info.mountDeviationDeg    = b.mountDeviationDeg;
    info.mountGravityErrorDeg = b.mountGravityErrorDeg;
    info.calibratedAtUtc      = b.calibratedAtUtc;
    info.calibAgeSec          = b.calibAgeSec;
    info.trunkCalib           = b.trunkCalib;
    info.role     = int(b.role);
    info.roleName = analysis::segmentRoleName(b.role);
}

} // namespace pinpoint
