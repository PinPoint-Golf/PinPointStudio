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

#include "recording_integrity.h"

#include "analysis_job_builder.h"   // impactCameraOf — the job builder's own camera-role rule

#include <QString>

namespace pinpoint::analysis {

IntegrityVerdicts integrityFor(const pinpoint::recording::Recording &recording)
{
    IntegrityVerdicts out;
    if (!recording.window)
        return out;
    const pinpoint::SwingWindow &window = *recording.window;
    const pinpoint::recording::IntegrityInputs &in = recording.integrity;

    // ⚠ ONLY WHEN THE RECORDING SAYS MADGWICK. Parity is a claim that the stored quaternion is
    // reproducible from the stored vectors, which can only be asked of a Madgwick host fusion — the
    // sole filter that warm-starts exactly. Under ESKF, or when the recording does not say, no claim
    // beats a fabricated one: nullopt, and the writer removes the block.
    if (in.hostOrientationFilter.compare(QStringLiteral("madgwick"), Qt::CaseInsensitive) == 0)
        out.imu = pinpoint::checkImuRefusion(window);

    // Frame-timestamp holes in every camera lane, and each lane's coverage of the RECORDED capture
    // window — not the window's own span, which covers only the frames that exist. Both the impact
    // and the window are taken onto the window's clock with t0Us; the impact camera (its keep band)
    // and the off-clock lanes (a phone clip, timed from its own first frame) are judged for holes only.
    const int64_t t0 = recording.t0Us;
    pinpoint::CaptureCoverageSpec cov;
    cov.judge         = true;
    cov.windowStartUs = t0 + in.windowStartUs;
    cov.windowEndUs   = t0 + in.windowEndUs;
    std::vector<pinpoint::SourceId> exempt = in.offClockCameras;
    exempt.push_back(impactCameraOf(recording.sources));
    const int64_t impactUs = recording.capture.impactUs >= 0 ? t0 + recording.capture.impactUs : -1;
    out.capture = pinpoint::checkCaptureIntegrity(window, impactUs, cov, exempt);
    // ONE TIME BASE for the verdict too: where the first hole opens is window time, whichever clock the
    // window runs on (before step 1.5 a live write stored it on the ring clock and a re-analysis
    // window-relative). Every other figure in the verdict is already a duration or ms from impact.
    if (out.capture->firstHoleUs >= 0.0)
        out.capture->firstHoleUs -= double(t0);
    return out;
}

} // namespace pinpoint::analysis
