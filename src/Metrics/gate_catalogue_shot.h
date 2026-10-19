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

// The gate catalogue's Shot entries (trust stage 6, docs/design/trust_stage6_plan.md): the whole
// swing, the 3-D skeleton, tempo and the wrist. Included by gate_catalogue.h after GateEntry and the
// detail::add / detail::rule helpers; not a standalone header.

namespace pinpoint::gates::detail {

inline void appendShotGates(std::vector<GateEntry> &c)
{
    const QString shot  = QStringLiteral("The whole swing");
    const QString skel  = QStringLiteral("3-D skeleton");
    const QString tempo = QStringLiteral("Tempo");
    const QString wrist = QStringLiteral("Wrist");

    // ── The whole swing (manifest_gates.h, from the document's integrity blocks) ──
    {
        GateEntry &e = add(c, "shot.captureHoles", shot,
            "Frames missing", "frames missing",
            QStringLiteral("The computer fell behind while recording and a camera's frames stopped arriving for a "
                           "while: the gap between two frames was more than %1 frame periods. Positions and readings "
                           "near the gap would be guessed across it, so nothing on this swing is judged, and it is "
                           "left out of the session's assessment.").arg(3),
            "{framesLost} frames missing in {holes} gap(s), the longest {worstHoleMs:ms}[; the first at {firstHoleMs:t}], {code:code}.",
            { GateEffect::Withheld }, "src/Analysis/manifest_gates.h", "docs/design/trust_stage6_plan.md");
        e.tuning = { tune("capture.holePeriods", 3.0, "frame periods") };
        e.codes = { { QStringLiteral("pre"),  QStringLiteral("before the ball was struck") },
                    { QStringLiteral("post"), QStringLiteral("after the ball was struck") },
                    { QStringLiteral("both"), QStringLiteral("before and after the ball was struck") } };
    }
    {
        GateEntry &e = add(c, "shot.refusionParity", shot,
            "Sensor recording inconsistent", "sensor recording inconsistent",
            QStringLiteral("A body sensor's stored orientation should follow from its own stored movement readings: "
                           "working it out again from them must land within %1° of what was recorded. When it does "
                           "not, the recording does not hang together, so nothing on this swing is judged and it is "
                           "left out of the session's assessment.").arg(0.5),
            "Worked out again, the orientation differed by up to {worstDeg:deg}; the limit is {maxDeg}°[ ({sensors} sensor(s) checked)].",
            { GateEffect::Withheld }, "src/Analysis/manifest_gates.h", "docs/design/trust_stage6_plan.md");
        e.tuning = { tune("imu.refusionThresholdDeg", 0.5, "°") };
    }

    // ── The 3-D skeleton (skeleton3d_fit.cpp) ──
    {
        GateEntry &e = add(c, "skeleton.refused", skel,
            "No 3-D skeleton", "not fitted",
            QStringLiteral("The 3-D skeleton is fitted to the golfer as both cameras saw them, sized from the legs or "
                           "body height at address and placed from the hips. It needs the down-the-line camera as "
                           "well as the face-on one: one camera cannot see depth. Without the second camera, or "
                           "enough frames, or those landmarks, or when the fit cannot settle, there is no skeleton, "
                           "and the readings that come from it are not taken."),
            "{code:code}.",
            { GateEffect::Refused }, "src/Skeleton3d/skeleton3d_fit.cpp", "docs/design/swing_3d_viz_design.md §3");
        e.tuning = { tune("skeleton3d.minFaceOnFrames", 10.0, "frames") };
        e.codes = { { QStringLiteral("fewFrames"),  QStringLiteral("Only {frames} face-on frames; {minFrames} are needed") },
                    { QStringLiteral("noScale"),    QStringLiteral("The face-on view showed neither the legs nor the body's height at address, so the golfer could not be sized") },
                    { QStringLiteral("noHips"),     QStringLiteral("The face-on view showed no hips at address") },
                    { QStringLiteral("noConverge"), QStringLiteral("The fit did not settle on an answer") },
                    { QStringLiteral("noDtl"),      QStringLiteral("This swing has the face-on camera only; the 3-D skeleton needs the down-the-line camera too") },
                    { QStringLiteral("dtlDropped"), QStringLiteral("The down-the-line camera could not be used at address, and the 3-D skeleton needs it") } };
    }

    // ── Tempo (tempo_metrics.cpp) ──
    {
        GateEntry &e = add(c, "tempo.refused", tempo,
            "No tempo", "not measured",
            QStringLiteral("Tempo is the time from address to the top, against the time from the top to impact. It "
                           "needs all three positions, found with some confidence and in order; otherwise it would "
                           "be a plausible-looking wrong number, so it is not given."),
            "{code:code}.",
            { GateEffect::Refused }, "src/Timeline/tempo_metrics.cpp", "docs/design/shot_analyzer_design.md §A.6");
        e.tuning = { tune("tempo.minConf", tuned::tempo::kMinConf, "") };
        e.codes = { { QStringLiteral("confidence"),   QStringLiteral("The swing's positions were found with confidence {conf}, at or below {minConf}") },
                    { QStringLiteral("missingEvent"), QStringLiteral("No {event} position was found on this swing") },
                    { QStringLiteral("order"),        QStringLiteral("The positions are out of order (address at {addressMs:t}, top at {topMs:t})") } };
    }

    // ── Wrist ──
    {
        GateEntry &e = add(c, "wrist.hmBinding", wrist,
            "HackMotion not used", "HackMotion not used",
            QStringLiteral("A HackMotion's readings become wrist angles only once it is known which sensor sat on the "
                           "forearm and which on the hand, and in what frame they report. When the recording cannot "
                           "say, the sensor is left out rather than read as a guess."),
            "{code:code}[ ({lanes} sensor stream(s))].",
            { GateEffect::Refused }, "src/IMU/hm_binding_recon.h", "docs/design/trust_stage6_plan.md");
        e.codes = { { QStringLiteral("noFrame"), QStringLiteral("No reporting frame has been chosen for the HackMotion sensors") },
                    { QStringLiteral("noUnit"),  QStringLiteral("The recording does not say whether the sensor was on the forearm or the hand") } };
    }
    {
        GateEntry &e = add(c, "wrist.cellRefused", wrist,
            "Positions not read", "positions not read",
            QStringLiteral("Each position's wrist angle is the middle value of the readings within %1 ms of it. With "
                           "fewer than %2 readings there, or with the wrist bent so far sideways (%3° or more) that "
                           "its angles can no longer be told apart, that position is left empty rather than guessed.")
                .arg(double(tuned::sampler::kWindowHalfUs) / 1000.0).arg(tuned::sampler::kMinValidSamples)
                .arg(tuned::sampler::kGimbalThresholdDeg),
            "{cells} of {ofCells} positions not read: {tooFew} with too few readings, {gimbal} bent too far sideways[, from {fromMs:t} to {toMs:t}].",
            { GateEffect::Masked }, "src/Wrist/wrist_angle_sampler.h", "docs/design/wristmotion_assessment_design.md §2.3, §3");
        e.tuning = { tune("sampler.minValidSamples", double(tuned::sampler::kMinValidSamples), "samples"),
                     tune("sampler.gimbalThresholdDeg", tuned::sampler::kGimbalThresholdDeg, "°") };
    }
    {
        GateEntry &e = add(c, "wrist.findingLowConfidence", wrist,
            "Low confidence", "low confidence",
            QStringLiteral("A wrist finding carries a confidence from how well its positions were found and how "
                           "clean the readings were. Below %1 it is still shown, but marked as low confidence "
                           "rather than stated outright.").arg(double(tuned::rules::kConfidenceFloor)),
            "Confidence {conf:pct}, under the {minConf:pct} floor.",
            { GateEffect::Downgraded }, "src/Wrist/assessment_rules.cpp", "docs/design/wristmotion_assessment_design.md §7.4");
        e.tuning = { tune("rules.confidenceFloor", double(tuned::rules::kConfidenceFloor), "") };
    }
    {
        GateEntry &e = add(c, "wrist.strengthWithoutFault", wrist,
            "Strength held back", "held back",
            QStringLiteral("A wrist strength is shown only beside a fault it protects against. On a swing with no "
                           "wrist fault, strengths are held back rather than listed on their own."),
            "{strengths} strength(s) held back: no wrist fault on this swing.",
            { GateEffect::Withheld }, "src/Wrist/assessment_rules.cpp", "docs/design/wristmotion_assessment_design.md §7.4");
        e.tuning = { sw("rules.strengthsRequireAdjacentFault", tuned::rules::kStrengthsRequireAdjacentFault) };
    }
}

} // namespace pinpoint::gates::detail
