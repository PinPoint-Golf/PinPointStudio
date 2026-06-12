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

// BallCalProfile persistence (docs/design/ball_detection_calibration.md §7).
//
// One cv::FileStorage file per camera (profile.yml.gz): the float background
// mats are stored losslessly (PNG would quantise CV_32F), compression is
// built in, and load/save stay ~20 lines. The design doc's profile.json +
// background.png split was simplified to this single file for that reason.
// Path resolution (QStandardPaths AppData / ballcal/<cameraKeyHash>/) lives
// with BallCalibrationController — this header is OpenCV-only.

#include "ball_model.h"

#include <fstream>
#include <string>
#include <opencv2/core/persistence.hpp>

namespace pinpoint::ballcal {

inline constexpr int kProfileSchemaVersion = 1;

inline bool saveProfile(const std::string &path, const BallCalProfile &p)
{
    if (!p.valid) return false;
    cv::FileStorage fs(path, cv::FileStorage::WRITE);
    if (!fs.isOpened()) return false;

    fs << "schemaVersion"   << kProfileSchemaVersion;
    fs << "theta"           << p.theta;
    fs << "margin"          << p.margin;
    fs << "roiX" << p.roiX << "roiY" << p.roiY << "roiW" << p.roiW << "roiH" << p.roiH;
    fs << "roiPxW" << p.roiPxW << "roiPxH" << p.roiPxH;
    // FileStorage has no int64 — epoch ms round-trips as a string.
    fs << "calibratedAtMs"  << std::to_string(p.calibratedAtMs);

    fs << "bgMeanGray"      << p.background.meanGray;
    fs << "bgMeanBgr"       << p.background.meanBgr;
    fs << "bgSigma"         << p.background.sigma;
    fs << "bgMedianLuma"    << p.background.calibMedianLuma;

    fs << "ballRadiusPx"    << p.ball.radiusPx;
    fs << "ballRadiusSigma" << p.ball.radiusSigma;
    fs << "ballCenterX"     << p.ball.calibCenter.x;
    fs << "ballCenterY"     << p.ball.calibCenter.y;
    fs << "ballColourMean"  << cv::Mat(p.ball.colourMean);
    fs << "ballColourCovInv"<< cv::Mat(p.ball.colourCovInv);
    fs << "ballTemplate"    << p.ball.template8u;
    fs << "ballMinContrast" << p.ball.minContrast;
    return true;
}

inline bool loadProfile(const std::string &path, BallCalProfile &out)
{
    // Probe existence first — cv::FileStorage prints a loud global ERROR for
    // a missing file, and "no profile yet" is the normal case (the wizard's
    // status binding probes on every instance change).
    if (!std::ifstream(path).good()) return false;

    cv::FileStorage fs(path, cv::FileStorage::READ);
    if (!fs.isOpened()) return false;

    int version = 0;
    fs["schemaVersion"] >> version;
    if (version != kProfileSchemaVersion) return false;   // schema mismatch = recalibrate

    BallCalProfile p;
    p.version = version;
    fs["theta"]  >> p.theta;
    fs["margin"] >> p.margin;
    fs["roiX"] >> p.roiX; fs["roiY"] >> p.roiY; fs["roiW"] >> p.roiW; fs["roiH"] >> p.roiH;
    fs["roiPxW"] >> p.roiPxW; fs["roiPxH"] >> p.roiPxH;
    std::string ts;
    fs["calibratedAtMs"] >> ts;
    p.calibratedAtMs = ts.empty() ? 0 : std::stoll(ts);

    fs["bgMeanGray"]   >> p.background.meanGray;
    fs["bgMeanBgr"]    >> p.background.meanBgr;
    fs["bgSigma"]      >> p.background.sigma;
    fs["bgMedianLuma"] >> p.background.calibMedianLuma;

    fs["ballRadiusPx"]    >> p.ball.radiusPx;
    fs["ballRadiusSigma"] >> p.ball.radiusSigma;
    fs["ballCenterX"] >> p.ball.calibCenter.x;
    fs["ballCenterY"] >> p.ball.calibCenter.y;
    cv::Mat colourMean, covInv;
    fs["ballColourMean"]   >> colourMean;
    fs["ballColourCovInv"] >> covInv;
    if (colourMean.total() == 3) p.ball.colourMean = cv::Vec3f(colourMean);
    if (covInv.rows == 3 && covInv.cols == 3) p.ball.colourCovInv = cv::Matx33f(covInv);
    fs["ballTemplate"]    >> p.ball.template8u;
    fs["ballMinContrast"] >> p.ball.minContrast;

    p.ball.valid = p.ball.radiusPx >= tuning::kMinCandRadius && !p.ball.template8u.empty();
    p.valid = p.background.valid() && p.ball.valid && p.theta > 0.0
              && p.roiPxW > 0 && p.roiPxH > 0;
    if (!p.valid) return false;

    out = std::move(p);
    return true;
}

}  // namespace pinpoint::ballcal
