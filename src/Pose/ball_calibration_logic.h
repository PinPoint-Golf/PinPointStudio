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

// Round bookkeeping for the user-in-the-loop calibration protocol
// (docs/design/ball_detection_calibration.md §5 Validate). Pure logic, no Qt,
// no OpenCV beyond ball_model.h types — standalone-tested in src/Pose/tests.
//
// Each validation round appends its observed scores to the running sets and
// re-derives theta; PASS requires kCleanRoundsRequired consecutive clean
// rounds AND the margin floor. A failed round resets the consecutive count
// (the samples are kept — they make the threshold honest).

#include "ball_model.h"

namespace pinpoint::ballcal {

inline constexpr int kCleanRoundsRequired = 2;
inline constexpr int kMaxFailedRounds     = 3;

struct CalibSession {
    std::vector<double> ballScores, emptyScores;
    ThresholdResult     current;
    int  cleanRounds  = 0;
    int  failedRounds = 0;

    void seed(std::vector<double> ball, std::vector<double> empty)
    {
        ballScores  = std::move(ball);
        emptyScores = std::move(empty);
        cleanRounds = failedRounds = 0;
        rederive();
    }

    ThresholdResult rederive()
    {
        current = deriveThreshold(ballScores, emptyScores);
        return current;
    }

    // One validation round's outcome. removeClean = no detection while the
    // user had removed the ball; placeAcquired = the ball was re-found after
    // placement. The round's scores are appended EITHER WAY — failed rounds
    // carry exactly the evidence that must move theta.
    void addRound(bool removeClean, bool placeAcquired,
                  const std::vector<double> &roundEmpty,
                  const std::vector<double> &roundBall)
    {
        emptyScores.insert(emptyScores.end(), roundEmpty.begin(), roundEmpty.end());
        ballScores.insert(ballScores.end(), roundBall.begin(), roundBall.end());
        rederive();

        if (removeClean && placeAcquired && current.pass)
            ++cleanRounds;
        else {
            cleanRounds = 0;
            ++failedRounds;
        }
    }

    bool passed()    const { return cleanRounds >= kCleanRoundsRequired && current.pass; }
    bool exhausted() const { return failedRounds >= kMaxFailedRounds; }

    // Margin mapped to a 0–1 robustness meter (0.4 margin reads as 100%).
    double robustness() const
    {
        return std::clamp(current.margin / 0.40, 0.0, 1.0);
    }
};

}  // namespace pinpoint::ballcal
