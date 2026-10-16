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

// The gate catalogue's Body entries (trust stage 6, docs/design/trust_stage6_plan.md). Included by
// gate_catalogue.h after GateEntry and the detail::add / detail::rule helpers; not a standalone header.

namespace pinpoint::gates::detail {

inline void appendBodyGates(std::vector<GateEntry> &c)
{
    namespace L = tuned::lowerBody;
    namespace U = tuned::upperBody;
    namespace R = tuned::bodyRotation;
    namespace B = tuned::ballpos;
    const QString body    = QStringLiteral("Body");
    const QString posture = QStringLiteral("Down-the-line posture");
    const char *lowerProd = "src/Body/lower_body_metrics.cpp";
    const char *upperProd = "src/Body/upper_body_metrics.cpp";
    const char *rotProd   = "src/Body/body_rotation.cpp";
    const char *dtlProd   = "src/Body/dtl_posture.h";
    const char *lowerDoc  = "docs/design/lower_body_face_on_metrics.md";
    const char *upperDoc  = "docs/design/upper_body_face_on_metrics.md";
    const char *rotDoc    = "docs/design/body_rotation_estimation.md";
    const char *dtlDoc    = "docs/design/dtl_posture_design.md";

    // ── Face-on lower body (lower_body_metrics.cpp) ─────────────────────────────────────────────
    add(c, "body.lowerAddress", body, "Lower body not seen at address", "lower body not seen",
        QStringLiteral("Every lower-body reading is measured from the address position: how far the knee, "
                       "the hips and the balance point have moved since then, as a share of the stance. With "
                       "no frame that clearly shows both hips, the lead knee and both ankles, there is nothing "
                       "to measure from, so none of them is read."),
        "No frame showed both hips, the lead knee and both ankles clearly enough[ ({posedFrames} frames looked at)].",
        { GateEffect::Refused }, lowerProd, lowerDoc)
        .tuning = { tune("lowerBody.confMin", L::kConfMin, "") };

    add(c, "body.stanceSpan", body, "Stance too small in the frame", "stance too small",
        QStringLiteral("The lower-body readings are shares of the stance width at address. When the feet are only "
                       "a few pixels apart in the picture, the golfer is too small or too far away for that to be "
                       "a ruler: every share would be keypoint noise divided by keypoint noise."),
        "The ankles were {spanPx:px} apart at address; at least {minPx:px} is needed.",
        { GateEffect::Refused }, lowerProd, lowerDoc)
        .tuning = { tune("lowerBody.minStanceSpanPx", L::kMinStanceSpanPx, "px") };

    add(c, "body.hipLine", body, "Hips edge-on", "hips edge-on",
        QStringLiteral("A tilt is measured across the line between the two hips. As the pelvis turns, the hips "
                       "line up one behind the other in the face-on picture and the line's angle runs toward 90° "
                       "while the posture has not changed. Frames where the hips are that narrow have no hip line, "
                       "and are left out of the reading."),
        "No hip line on {frames} of {ofFrames} frames ({frac:pct})[ from {fromMs:t} to {toMs:t}]; the hips must stay "
        "at least {minRatio:pct} as wide as at address.",
        { GateEffect::Masked }, lowerProd, lowerDoc)
        .tuning = { tune("lowerBody.minHipSpanRatio", L::kMinHipSpanRatio, "") };

    // ── Face-on upper body (upper_body_metrics.cpp) ─────────────────────────────────────────────
    add(c, "body.upperAddress", body, "Upper body not seen at address", "upper body not seen",
        QStringLiteral("The upper-body readings are scaled by the shoulders, the stance and the lead arm as they "
                       "were at address. With no frame that clearly shows both shoulders, both ankles and the "
                       "lead arm, there is no scale to read them in, so none of them is read."),
        "No frame showed both shoulders, both ankles and the lead arm clearly enough[ ({posedFrames} frames looked at)].",
        { GateEffect::Refused }, upperProd, upperDoc)
        .tuning = { tune("upperBody.confMin", U::kConfMin, "") };

    add(c, "body.shoulderSpan", body, "Shoulders too small in the frame", "shoulders too small",
        QStringLiteral("Several upper-body readings are shares of the shoulder width at address. When the "
                       "shoulders are only a few pixels across, the golfer is too small in the picture for that "
                       "to be a ruler."),
        "The shoulders were {spanPx:px} across at address; at least {minPx:px} is needed.",
        { GateEffect::Refused }, upperProd, upperDoc)
        .tuning = { tune("upperBody.minShoulderSpanPx", U::kMinShoulderSpanPx, "px") };

    add(c, "body.shoulderLine", body, "Shoulders edge-on", "shoulders edge-on",
        QStringLiteral("The shoulder plane, and the trail elbow's height against the shoulder line, are measured "
                       "across the line between the two shoulders. As the chest turns, the shoulders line up one "
                       "behind the other in the face-on picture and that line stops having an angle the camera can "
                       "read. Frames where the shoulders are that narrow are left out of the reading."),
        "No shoulder line on {frames} of {ofFrames} frames ({frac:pct})[ from {fromMs:t} to {toMs:t}]; the shoulders "
        "must stay at least {minRatio:pct} as wide as at address.",
        { GateEffect::Masked }, upperProd, upperDoc)
        .tuning = { tune("upperBody.minShoulderSpanRatio", U::kMinShoulderSpanRatio, "") };

    add(c, "body.elbowLine", body, "Elbows too close together", "elbows too close",
        QStringLiteral("The elbow line's tilt needs the two elbows to be apart across the picture. At address the "
                       "arms hang together, and a line between two points a few pixels apart is keypoint noise, "
                       "so frames where they are that close are left out of the reading."),
        "No elbow line on {frames} of {ofFrames} frames ({frac:pct})[ from {fromMs:t} to {toMs:t}]; the elbows must be "
        "at least {minPx:px} apart across the picture.",
        { GateEffect::Masked }, upperProd, upperDoc)
        .tuning = { tune("upperBody.minElbowSpanPx", U::kMinElbowSpanPx, "px") };

    add(c, "body.sideBendLines", body, "Hips or shoulders edge-on", "hips or shoulders edge-on",
        QStringLiteral("Side bend is the shoulder line's tilt against the hip line's, so it needs both lines. When "
                       "either the hips or the shoulders have turned edge-on to the camera, that frame has no side "
                       "bend and is left out of the reading."),
        "No side bend on {frames} of {ofFrames} frames ({frac:pct})[ from {fromMs:t} to {toMs:t}]; the shoulders must "
        "stay {minShoulderRatio:pct} and the hips {minHipRatio:pct} as wide as at address.",
        { GateEffect::Masked }, upperProd, upperDoc)
        .tuning = { tune("upperBody.minShoulderSpanRatio", U::kMinShoulderSpanRatio, ""),
                    tune("lowerBody.minHipSpanRatio", L::kMinHipSpanRatio, "") };

    // ── Body rotation (body_rotation.cpp) ───────────────────────────────────────────────────────
    {
        GateEntry &e = add(c, "body.rotation", body, "Turn not measured", "turn not measured",
            QStringLiteral("Turn about the body's own vertical axis cannot be seen by one face-on camera: it can "
                           "tell how narrow the hips or shoulders look, not which way they turned or by how much. It "
                           "is measured by a sensor on the pelvis or chest, or triangulated from the face-on and "
                           "down-the-line cameras together. With neither, or when the two-camera fit cannot place "
                           "the address position, the turn is not read, and the X-factor with it."),
            "{code:code}.",
            { GateEffect::Refused }, rotProd, rotDoc);
        e.tuning = { tune("bodyRotation.triAddrMaxGapUs", double(R::kTriAddrMaxGapUs) / 1000.0, "ms"),
                     tune("bodyRotation.addrWindowUs", double(R::kAddrWindowUs) / 1000.0, "ms"),
                     sw("bodyRotation.triangulated", R::kTriangulated) };
        e.lift = { QStringLiteral("faceOn+dtl"), QStringLiteral("pelvisRotation:pelvisImu"),
                   QStringLiteral("thoraxRotation:thoraxImu") };
        e.codes = {
            { QStringLiteral("noSensor"),       QStringLiteral("No sensor on this part of the body, and no down-the-line camera to triangulate it") },
            { QStringLiteral("routeOff"),       QStringLiteral("No sensor on this part of the body, and the two-camera turn is switched off") },
            { QStringLiteral("sensorUnusable"), QStringLiteral("The sensor on this part of the body did not cover the swing") },
            { QStringLiteral("addressGap"),     QStringLiteral("The two cameras' nearest clear frame was {nearestMs:ms} from address; it must be within {maxGapMs:ms}") },
            { QStringLiteral("addressWindow"),  QStringLiteral("The two cameras had no clear frame within {windowMs:ms} of address") },
            { QStringLiteral("noFrames"),       QStringLiteral("The two cameras never saw this line of the body clearly") },
            { QStringLiteral("input"),          QStringLiteral("The two-camera fit handed over too few frames[ ({frames})]") } };
    }

    add(c, "body.xFactorStretchTop", body, "No top to measure from", "no top found",
        QStringLiteral("X-factor stretch is how much the separation between chest and hips grows after the top of "
                       "the backswing. Without a top on this swing's timeline there is no instant to measure it "
                       "from, and a stretch from the wrong instant would be a plausible number about nothing."),
        "The top of the swing was not found, so the stretch has no starting point.",
        { GateEffect::Refused }, rotProd, rotDoc);

    // ── Ball position (Ball/ball_position.cpp, recorded from the foot stage) ────────────────────
    {
        GateEntry &e = add(c, "body.ballPosition", body, "Ball position not read", "not read",
            QStringLiteral("Ball position is the ball's place between the heels at address, and the ball's size is "
                           "also the ruler that turns pixels into centimetres and inches. It is read from the ball "
                           "sightings around address; too few of them, sightings that scatter (the detector locked "
                           "on to something else), or a ball far outside the stance are a detector failure rather "
                           "than a setup, and nothing is published. Readings that needed the ruler go with it."),
            "{code:code}.",
            { GateEffect::Refused }, "src/Ball/ball_position.cpp", "docs/design/trust_stage6_plan.md");
        e.tuning = { tune("ballpos.minSamples", double(B::kMinSamples), ""),
                     tune("ballpos.maxJumpPx", B::kMaxJumpPx, "px"),
                     tune("ballpos.fracLo", B::kFracLo, ""),
                     tune("ballpos.fracHi", B::kFracHi, "") };
        e.codes = {
            { QStringLiteral("fewSamples"),    QStringLiteral("Only {samples} ball sightings around address; {minSamples} needed") },
            { QStringLiteral("scattered"),     QStringLiteral("Only {samples} of {ofSamples} ball sightings agreed within {maxJumpPx:px}; {minSamples} needed") },
            { QStringLiteral("outsideStance"), QStringLiteral("The ball read {frac:pct} of the way from the lead heel to the trail heel, outside {fracLo:pct} to {fracHi:pct}") } };
    }

    // ── Down-the-line posture (dtl_posture.h) — the literals are DtlPostureConfig's defaults ────
    {
        GateEntry &e = add(c, "posture.address", posture, "Address not read down the line", "address not read",
            QStringLiteral("Every down-the-line posture reading is signed toward the ball, and which way the ball is "
                           "comes from the golfer's own feet at address: toes are ball-side of heels. When the feet "
                           "or the hips are not seen at address, or the ball sits on the other side, a reading could "
                           "come out with its sign backwards, so none of them is read."),
            "{code:code}.",
            { GateEffect::Refused }, dtlProd, dtlDoc);
        e.tuning = { tune("dtlPosture.confMin", 0.30, ""), tune("dtlPosture.addrMinFrames", 5, "frames"),
                     tune("dtlPosture.minToeHeelPx", 4.0, "px") };
        e.codes = {
            { QStringLiteral("feet"),          QStringLiteral("The toes and heels were not seen clearly at address") },
            { QStringLiteral("feetEndOn"),     QStringLiteral("The toes and heels were only {toeHeelPx:px} apart at address; {minPx:px} is needed to tell which way the ball is") },
            { QStringLiteral("hips"),          QStringLiteral("The hips were not seen clearly at address") },
            { QStringLiteral("ballDisagrees"), QStringLiteral("The ball and the feet disagree on which way the golfer faces") } };
    }
    {
        GateEntry &e = add(c, "posture.ruler", posture, "No trustworthy ruler", "no ruler",
            QStringLiteral("Pelvis thrust is in centimetres, and centimetres need a ruler: the ball, measured in the "
                           "down-the-line picture or carried across from the face-on one. A ruler must also measure "
                           "the golfer plausibly; one that makes an adult's shoulders narrower than 30 cm or wider "
                           "than 50 cm is wrong, and the thrust is dropped rather than published in error. The angles "
                           "need no ruler and are unaffected."),
            "{code:code}.",
            { GateEffect::Refused }, dtlProd, dtlDoc);
        e.tuning = { tune("dtlPosture.shoulderMinCm", 30.0, "cm"), tune("dtlPosture.shoulderMaxCm", 50.0, "cm") };
        e.codes = {
            { QStringLiteral("implausible"), QStringLiteral("The ball ruler made the shoulders {shoulderCm:cm} wide; an adult's are {minCm:cm} to {maxCm:cm}") },
            { QStringLiteral("none"),        QStringLiteral("No ball to measure with in either view") } };
    }
    {
        GateEntry &e = add(c, "posture.sizeFloor", posture, "Too small to read", "too small",
            QStringLiteral("Balance heel to toe is a share of the foot's length, and ball distance a share of the "
                           "shoulder width. When either is only a few pixels in the down-the-line picture, the share "
                           "is keypoint noise and is not read."),
            "{code:code}.",
            { GateEffect::Refused }, dtlProd, dtlDoc);
        e.tuning = { tune("dtlPosture.minSizePx", 8.0, "px") };
        e.codes = {
            { QStringLiteral("foot"),     QStringLiteral("The foot was {lengthPx:px} long in the picture; more than {minPx:px} is needed") },
            { QStringLiteral("shoulder"), QStringLiteral("The shoulders were {lengthPx:px} wide in the down-the-line picture; more than {minPx:px} is needed") } };
    }
    {
        GateEntry &e = add(c, "posture.handLoop", posture, "Hand loop not read", "not read",
            QStringLiteral("The hand-path loop compares where the lead wrist comes down with where it went up, at the "
                           "same heights, as a share of the hands' rise to the top. It needs the wrist at address and "
                           "at the top, a rise long enough to be a swing seen, and enough heights where both paths "
                           "can be read."),
            "{code:code}.",
            { GateEffect::Refused }, dtlProd, dtlDoc);
        e.tuning = { tune("dtlPosture.loopMinRisePx", 40.0, "px"), tune("dtlPosture.loopMinLevels", 3, "heights"),
                     tune("dtlPosture.loopTopWindowUs", 80.0, "ms") };
        e.codes = {
            { QStringLiteral("wristAtAddress"), QStringLiteral("The lead wrist was not seen clearly at address") },
            { QStringLiteral("wristAtTop"),     QStringLiteral("The lead wrist was not seen within {windowMs:ms} of the top") },
            { QStringLiteral("rise"),           QStringLiteral("The hands rose only {risePx:px} to the top; {minPx:px} is needed") },
            { QStringLiteral("levels"),         QStringLiteral("The two hand paths could be compared at only {levels} heights; {minLevels} needed") } };
    }

    // ── Down-the-line shaft lie (Club/dtl_shaft_lie.h) ─────────────────────────────────────────
    {
        GateEntry &e = add(c, "posture.shaftLie", posture, "Shaft lie not read", "not read",
            QStringLiteral("Shaft lie is read only off down-the-line frames where the shaft was actually measured; "
                           "a predicted or held frame never becomes a reading. Without a measured frame, or without "
                           "an address on the swing's timeline, there is nothing to read."),
            "{code:code}.",
            { GateEffect::Refused }, "src/Club/dtl_shaft_lie.h", "docs/design/trust_stage6_plan.md");
        e.codes = {
            { QStringLiteral("noTrack"),    QStringLiteral("No down-the-line shaft track on this swing") },
            { QStringLiteral("noAddress"),  QStringLiteral("No address on this swing's timeline") },
            { QStringLiteral("noMeasured"), QStringLiteral("The down-the-line shaft was never measured, only predicted") } };
    }
}

} // namespace pinpoint::gates::detail
