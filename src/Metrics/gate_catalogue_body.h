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
                       "read. Frames where the shoulders are that narrow are left out of the reading. Trunk rise "
                       "is left out on the same frames: it reads the point midway between the shoulders, and "
                       "once they overlap in the picture that point is guesswork."),
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

    // leadArmToTorso (13 Nov 2026, plane stack FA): Masked over the swing, Refused at an instant the
    // grade reads (P7, P8) when the gate leaves nothing there.
    {
        GateEntry &e = add(c, "body.armTorsoLines", body, "Lead arm or shoulders out of view",
            "lead arm or shoulders out of view",
            QStringLiteral("The lead arm's angle from the torso needs two directions the face-on camera can see: the "
                           "torso's, which runs down from the point midway between the shoulders, and the upper "
                           "arm's, from the lead shoulder to the elbow. As the chest turns, the shoulders line up "
                           "one behind the other and that midpoint is guesswork; when the upper arm points toward "
                           "or away from the camera it is a few pixels long and its direction is noise. Frames "
                           "where either happens are left out, and the angle is not judged at impact or at the "
                           "shaft parallel after impact if they leave nothing there."),
            "No lead arm angle on {frames} of {ofFrames} frames ({frac:pct})[ from {fromMs:t} to {toMs:t}][, none at "
            "{phase:code}: the shoulders were {shoulderRatio:pct} as wide as at address and the upper arm "
            "{armRatio:pct} as long]; the shoulders must stay at least {minRatio:pct} as wide, and the upper arm "
            "{minArmRatio:pct} to {maxArmRatio:pct} as long.",
            { GateEffect::Masked, GateEffect::Refused }, upperProd, upperDoc);
        e.tuning = { tune("upperBody.minShoulderSpanRatio", U::kMinShoulderSpanRatio, ""),
                     tune("upperBody.minUpperArmRatio", U::kMinUpperArmRatio, ""),
                     tune("upperBody.maxUpperArmRatio", U::kMaxUpperArmRatio, "") };
        e.codes = { { QStringLiteral("P7"), QStringLiteral("impact") },
                    { QStringLiteral("P8"), QStringLiteral("the shaft parallel after impact") } };
    }

    // The face-on lead elbow (leadArmFlexion's faceOn rung, plane stack D5, 13 Nov 2026): a frame whose lead
    // upper arm or forearm images outside its length band against address is left out. Its own gate rather
    // than an extension of body.armTorsoLines: this reading never uses the shoulder midpoint, so it is never
    // refused for the shoulders, and its sentence and golfer words must not name them. A Masked summary over
    // the swing, and a REFUSED record at impact when the gate leaves the grader nothing there — the instant
    // the fold after impact is measured from.
    {
        GateEntry &e = add(c, "body.leadElbowLines", body, "Lead arm pointing at the camera",
            "lead arm pointing at the camera",
            QStringLiteral("The bend at the lead elbow is read from the face-on picture as the angle between the "
                           "upper arm (lead shoulder to elbow) and the forearm (elbow to wrist). When either part "
                           "points toward or away from the camera it looks much shorter than at address and the "
                           "picture hides how bent the elbow is; when it looks much longer, a point has jumped off "
                           "the arm. Frames where either happens are left out, and the bend is not judged at "
                           "impact if they leave nothing there."),
            "No lead elbow bend on {frames} of {ofFrames} frames ({frac:pct})[ from {fromMs:t} to {toMs:t}][, none "
            "{phase:code}: the upper arm was {armRatio:pct} as long as at address and the forearm "
            "{forearmRatio:pct}][, leaving {gapMs:ms} unread where at most {maxGapMs:ms} may be]; the upper arm must "
            "stay {minArmRatio:pct} to {maxArmRatio:pct} as long, and the forearm {minForearmRatio:pct} to "
            "{maxForearmRatio:pct}.",
            { GateEffect::Masked, GateEffect::Refused }, upperProd, upperDoc);
        e.tuning = { tune("upperBody.minUpperArmRatio", U::kMinUpperArmRatio, ""),
                     tune("upperBody.maxUpperArmRatio", U::kMaxUpperArmRatio, ""),
                     tune("upperBody.minForearmRatio", U::kMinForearmRatio, ""),
                     tune("upperBody.maxForearmRatio", U::kMaxForearmRatio, "") };
        e.codes = { { QStringLiteral("P7"), QStringLiteral("at impact") },
                    { QStringLiteral("afterImpact"), QStringLiteral("across the 100 ms after impact") } };
    }

    // The face-on lead elbow's OCCLUSION gate (plane stack D5 round 3, 13 Nov 2026). The pose model places a
    // hidden joint somewhere plausible — Jess's lead shoulder on her chin once her head covers it after impact,
    // Mark's elbow and wrist scored low near his head — so a lead shoulder, elbow or wrist on the head, or one the model scored low on
    // its RAW confidence, is not seen. Its own gate rather than a code of body.leadElbowLines: the cause, the
    // fix (nothing the golfer can change; another view) and the golfer words all differ from a foreshortened arm.
    {
        GateEntry &e = add(c, "body.leadArmHidden", body, "Lead arm hidden from the face-on camera",
            "lead arm hidden from the camera",
            QStringLiteral("The bend at the lead elbow is read from the face-on picture, so all three of the lead "
                           "shoulder, elbow and wrist must really be in view. After impact a golfer who keeps the "
                           "head down hides the lead shoulder behind the head, and the pose model then places it "
                           "on the face, chin or neck; a hand or elbow can be placed on the head the same way. A "
                           "frame where any of the three sits on the head, or where the model was unsure of one "
                           "on its own score, is left out. If that leaves impact, or too much of the 100 ms after "
                           "it, unread, the fold after impact is not judged rather than read from what is left."),
            "No lead elbow bend on {frames} of {ofFrames} frames ({frac:pct})[ from {fromMs:t} to {toMs:t}][, none "
            "{phase:code}][, leaving {gapMs:ms} unread where at most {maxGapMs:ms} may be][: {code:code}][; the lead "
            "shoulder sat on the head in {shoulderOnHead} of them][, the elbow in {elbowOnHead}][, the wrist in "
            "{wristOnHead}][, and a point was scored under {minRawConf} in {lowConfidence}]. A point within "
            "{headRadius:pct} of the shoulder width of the head's centre is on the head.",
            { GateEffect::Masked, GateEffect::Refused }, upperProd, upperDoc);
        e.tuning = { tune("upperBody.headRadiusRatio", U::kHeadRadiusRatio, ""),
                     tune("upperBody.minRawConf", U::kMinRawConf, "") };
        e.codes = { { QStringLiteral("P7"), QStringLiteral("at impact") },
                    { QStringLiteral("afterImpact"), QStringLiteral("across the 100 ms after impact") },
                    { QStringLiteral("shoulderOnHead"), QStringLiteral("the lead shoulder was behind the head") },
                    { QStringLiteral("armOnHead"), QStringLiteral("the lead elbow or wrist was placed on the head") },
                    { QStringLiteral("lowConfidence"), QStringLiteral("the pose model was unsure where the lead arm was") } };
    }

    // ── Body rotation (body_rotation.cpp) ───────────────────────────────────────────────────────
    {
        GateEntry &e = add(c, "body.rotation", body, "Turn not measured", "not measured",
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

    // ── The face-in cue (body_rotation.cpp recordFaceIn; trunk design §6.3, W2.1d) ──────────────
    // DOWNGRADED, never refused or withheld: every reading is shown with its warning. No `lift`: no
    // other route fixes a mis-worn sensor; re-seating it does. The threshold is the record's
    // `limitDeg` (BodyRotationConfig::faceInBendDeg, bodyRotation.faceInBendDeg).
    const char *faceInDoc = "docs/design/trunk_imu_sequence_grf_design.md §6.3";
    add(c, "body.pelvisFaceIn", body, "Hip sensor probably on the front or label-in", "sensor probably on the front or label-in",
        QStringLiteral("The hip sensor is worn on the lower back, USB-down with its label facing out. Worn on the "
                       "front, or label-in, it is the right mount turned half a turn about the spine: standing tall "
                       "and the turns of the set-up routine cannot tell the two apart. The turn and its speed still "
                       "read correctly, but the bends and tilts read backwards, so an address that bends forward at the hips "
                       "reads as a lean back. A real address always bends forward, so a backward lean at this "
                       "swing's address means the sensor is probably on the front or label-in. Its readings are kept "
                       "and shown, a step lower in trust, with this warning: wear the sensor on your back, label out."),
        "At address the hip sensor read a forward bend of {bendDeg:deg}, a lean backward[ (the cue fires below {limitDeg:deg})].",
        { GateEffect::Downgraded }, rotProd, faceInDoc);
    add(c, "body.thoraxFaceIn", body, "Chest sensor probably on the front or label-in", "sensor probably on the front or label-in",
        QStringLiteral("The chest sensor is worn in the vest's back pocket, USB-down with its label facing out. Worn "
                       "on the front, or label-in, it is the right mount turned half a turn about the spine: standing "
                       "tall and the turns of the set-up routine cannot tell the two apart. The turn and its speed "
                       "still read correctly, but the bends and tilts read backwards, so an address that bends forward from the "
                       "hips reads as the chest leaning back. A real address always tilts the chest forward, so a "
                       "backward lean at this swing's address means the sensor is probably on the front or label-in. "
                       "Its readings are kept and shown, a step lower in trust, with this warning: wear the sensor on "
                       "your back, label out."),
        "At address the chest sensor read a forward bend of {bendDeg:deg}, a lean backward[ (the cue fires below {limitDeg:deg})].",
        { GateEffect::Downgraded }, rotProd, faceInDoc);

    // ── The trunk sensors' bends and tilts (body_rotation.cpp): their three assumptions ─────────────
    // DOWNGRADED, never refused: the sensor readings are kept and judged, a step lower in trust, with the
    // warning. Withdrawn with the reading when a camera reading stays the answer (applyTrunkSensorPrecedence).
    add(c, "body.chestBendSite", body, "Chest bend read on the upper back", "read on the upper back",
        QStringLiteral("The forward bend, and the spine's lean away from the target, are normally read along the line "
                       "from your hips to your shoulders: the bend from down the line, the lean face-on. With the "
                       "chest sensor worn they are read by that sensor instead, on your upper back, which tilts with "
                       "the upper spine rather than with that line. The two have not yet been compared on a swing "
                       "where both were recorded, so the reading is kept and judged, a step lower in trust."),
        "Read by the chest sensor on the upper back, not along the hips-to-shoulders line; not yet checked against the camera.",
        { GateEffect::Downgraded }, rotProd, "docs/design/body_rotation_estimation.md");
    // W4, 13 Nov 2026: the chest sensor's shoulder plane (shoulderPlaneAngle3d's thoraxImu rung).
    add(c, "body.chestShoulderSite", body, "Shoulder tilt read on the upper back", "read on the upper back",
        QStringLiteral("The shoulder plane is the tilt of the line through your two shoulder joints, from the 3-D fit "
                       "of both cameras. With the chest sensor worn it is read by that sensor instead: the tilt of "
                       "your upper back across, not of the shoulder joints themselves, which also move with your "
                       "shoulder blades. The two have not yet been compared on a swing where both were recorded, so "
                       "the reading is kept and judged, a step lower in trust."),
        "Read by the chest sensor as the upper back's tilt across, not the line through the shoulder joints; not yet "
        "checked against the cameras.",
        { GateEffect::Downgraded }, rotProd, "docs/design/body_rotation_estimation.md");
    add(c, "body.trunkMountYawAssumed", body, "Sensor direction on the back assumed", "sensor direction assumed",
        QStringLiteral("The bends and tilts read by the motion sensors need to know exactly which way each sensor faces "
                       "on your back. Standing tall cannot measure that turn, so it is taken from how the sensors are "
                       "worn. A sensor turned a little on the back reads part of your forward bend as side bend, and "
                       "part of a side lean as forward bend, and it turns the direction the sensor takes as the "
                       "target line, so these readings are kept and judged, a step lower in trust, and carry that "
                       "much more uncertainty."),
        "Each sensor's turn on the back is assumed (±{sigmaDeltaDeg:deg}), so the bends and tilts read from it carry more "
        "uncertainty[; at this address the side bend carries ±{thoraxAddrDeg:deg} from the chest sensor][ and "
        "±{pelvisAddrDeg:deg} from the hip sensor].",
        { GateEffect::Downgraded }, rotProd, "docs/design/trunk_imu_sequence_grf_design.md §6.2");
    // W4c, 13 Nov 2026: the sensor readings whose target line, or the plane through the ball, is each
    // sensor's own front at this swing's address (trunk_sensor_series.h ballPlaneFront) — the forward
    // bend, the hip tilt and the spine's lean. Interim, until the target line comes from the alignment
    // stick (#178): the set-up is taken as square, and that is said on the reading, never hidden.
    // The evidence also carries chestAddressHeadingDeg / hipAddressHeadingDeg (each sensor's heading at
    // address against stand tall, + open), for diagnostics only: they are deliberately NOT in the sentence.
    // That heading holds the golfer's turn since stand tall, the mount yaw and the sensor's heading drift,
    // so a square golfer can read 17° "open" — shown, it would read as a set-up fault (W4c review).
    add(c, "body.targetFromAddress", body, "Target line taken from your address", "target line from your address",
        QStringLiteral("These readings are measured against your target line. The motion sensors cannot see the "
                       "target line yet, so each one takes it from the way you stood at this swing's address, as "
                       "if you were set up square to the target. An open or closed set-up is therefore not seen "
                       "in these readings: the bend, the hip tilt and the spine's lean are read against your own "
                       "address line, not the target line. They are kept and judged, a step lower in trust, with "
                       "this warning."),
        "Your address was taken as square to the target line.",
        { GateEffect::Downgraded }, rotProd, "docs/design/trunk_imu_sequence_grf_design.md §6.6");

    // ── The shoulder tilt against the forward bend (shoulder_tilt_vs_bend.h, after the precedence) ──
    // W7, 13 Nov 2026 (Mark's decision): shoulderTiltVsBend is the tilt at the top less the bend at
    // address, each from whichever route won. DOWNGRADED, never refused, when the two halves came from
    // different instruments (the 3-D fit's tilt with the down-the-line bend, or one half from the chest
    // sensor and the other from a camera): the difference carries both instruments' errors, and no
    // swing has yet compared them. Both halves from the chest sensor: no record.
    {
        GateEntry &e = add(c, "body.tiltBendMixedSources", body, "Tilt and posture read by different instruments",
            "different instruments",
            QStringLiteral("This reading compares the tilt of your shoulders at the top with your forward bend at "
                           "address. Here the two were measured by different instruments, which have not yet "
                           "been checked against each other on one swing, so the comparison carries both of "
                           "their errors. Where one of them states no margin (the down-the-line camera's bend, "
                           "today), the reading shows none either, and this warning is the only sign of that "
                           "uncertainty. The reading is kept and judged, a step lower in trust."),
            "Your shoulder tilt was read {tiltFrom:code} and your posture {bendFrom:code}.",
            { GateEffect::Downgraded }, "src/Body/shoulder_tilt_vs_bend.h", "docs/design/planned_metrics_promotion.md §6 W7");
        e.codes = {
            { QStringLiteral("thoraxImu"),  QStringLiteral("by the chest sensor") },
            { QStringLiteral("trunkImus"),  QStringLiteral("by the chest sensor") },
            { QStringLiteral("faceOn+dtl"), QStringLiteral("from the 3-D fit of both cameras") },
            { QStringLiteral("dtl"),        QStringLiteral("by the down-the-line camera") },
            { QStringLiteral("unstated"),   QStringLiteral("by a route that was not recorded") } };
    }

    // FB, 13 Nov 2026 (plane stack §11): the BACKSTOP. A tilt − bend below body3d.tiltBendFloorDeg is
    // outside what a turn can make (a 35° bend would need under ~27° of shoulder turn) and the library's
    // clean 1st percentile. DOWNGRADED, never refused: kept and judged with the warning that it is
    // probably a misreading of the shoulders (Chris's −24, the 3-D fit having thrown the down-the-line
    // trail shoulder away).
    {
        GateEntry &e = add(c, "body.tiltBendOutOfRange", body, "Shoulder tilt beyond a full turn",
            "beyond a full turn",
            QStringLiteral("This reading compares the tilt of your shoulders at the top with your forward bend at "
                           "address. A turn made on the spine leaves the shoulders tilted by about the bend; to "
                           "finish this much flatter than the bend, the shoulders would have to have turned far "
                           "less than any full backswing does. A value this far out is much more likely to be a "
                           "misreading of where the shoulders were than a real turn, so it is kept and judged, a "
                           "step lower in trust, with this warning."),
            "Your shoulder tilt read {valueDeg:deg} against your posture; below {floorDeg:deg} is beyond what a full turn can make.",
            { GateEffect::Downgraded }, "src/Body/shoulder_tilt_vs_bend.h", "docs/design/plane_stack_design.md §11 FB");
        e.tuning = { tune("body3d.tiltBendFloorDeg", tuned::body3d::kTiltBendFloorDeg, "°") };
    }

    // ── Hip internal rotation (trunk_sensor_series.h, for HipInternalRotationStage) ────────────────
    // DOWNGRADED, never refused: where the two-camera fit did not read a thigh's own turn, the hip's
    // rotation is the hip sensor's turn over a thigh taken as not turning, kept and judged with this
    // warning and the assumption's σ. Fired only when a graded reading (the top or impact) rests on it,
    // covering exactly those readings.
    {
        GateEntry &e = add(c, "body.thighTurnAssumed", body, "Thigh turn assumed", "thigh turn assumed",
            QStringLiteral("Hip internal rotation is the hips' turn against the thigh's. The hip sensor measures "
                           "the hips' turn; no sensor is worn on the thigh, so the thigh's own turn comes from the "
                           "3-D fit of your body, which needs both cameras to see the legs. Where the fit did not "
                           "read it, the thigh is taken as not turning, which a planted foot nearly allows, so "
                           "the reading is kept and judged, a step lower in trust, with more uncertainty."),
            "{code:code}, so the thigh is taken as not turning and hip rotation carries ±{sigmaDeg:deg} more.",
            { GateEffect::Downgraded }, "src/Body/trunk_sensor_series.h", "docs/design/body_rotation_estimation.md");
        e.codes = {
            { QStringLiteral("noFit"),       QStringLiteral("There is no 3-D fit of the body on this swing") },
            { QStringLiteral("thighUnread"), QStringLiteral("The 3-D fit did not read that thigh's own turn there") } };
    }

    // ── The readings from the 3-D body (skeleton3d_body_series.h, for Skeleton3dBodyStage) ─────────
    // DOWNGRADED, never refused: the aim is shown and judged, a step lower in trust, with the warning.
    // Only the AIM carries it — a bearing turns one-for-one with the cameras' assumed direction; the
    // shoulder tilt, the side bend and the knee bend are inclinations and do not. It fires on every
    // published aim: skeleton3d solves its own cameras and no measured calibration reaches the fit,
    // so the words promise no way to remove it.
    add(c, "body3d.cameraAssumed", body, "Camera direction assumed", "camera direction assumed",
        QStringLiteral("Where the shoulders aim is read against the target line, and the cameras are what tell the "
                       "3-D fit of your body which way that line runs. The fit works out the cameras' directions "
                       "for itself, from the swing it sees, and a direction off by a few degrees turns the reading "
                       "of where the shoulders aim by the same few degrees. So the reading is kept and judged, a "
                       "step lower in trust, and carries that much more uncertainty."),
        "The 3-D fit works out the cameras' directions itself, so where the shoulder line points carries ±{sigmaYawDeg:deg}.",
        { GateEffect::Downgraded }, "src/Body/skeleton3d_body_series.h", "docs/design/body_rotation_estimation.md");
    // The 3-D route standing in where the single-camera route had nothing (route_substitution.h): the
    // reading is kept and judged from the fit, a step lower in trust; the failed route's own refusals
    // are moved onto this record's `replacedRecords` after the run, so they no longer sit beside the value.
    {
        GateEntry &e = add(c, "body3d.fromFit", body, "Read from the 3-D fit", "from the 3-D fit",
            QStringLiteral("This reading is normally taken by one camera. On this swing that camera could not read it, "
                           "so it was read from the 3-D fit of your body, which combines both cameras. It is kept and "
                           "judged, a step lower in trust, with this warning."),
            "{code:code}, so this was read from the 3-D fit of the body.",
            { GateEffect::Downgraded }, "src/Body/skeleton3d_body_series.h", "docs/design/body_rotation_estimation.md");
        e.codes = {
            { QStringLiteral("faceOn"), QStringLiteral("The face-on camera could not read it on this swing") },
            { QStringLiteral("dtl"),    QStringLiteral("The down-the-line camera could not read it on this swing") } };
    }

    // FB, 13 Nov 2026 (plane stack §11): the two cameras disagree about the shoulders at the top — the
    // down-the-line camera's shoulder line against the 3-D fit's, in that camera (Skeleton3DStage;
    // skeleton3d_body_metrics.h shoulderViewsAtTop). DOWNGRADED, never refused: every reading built on the
    // fit's shoulders there is kept and judged a step lower in trust (scoped to the readings that stand
    // and came from the fit, scopeShoulderViewsRecord; shoulderTiltVsBend inherits it from its tilt).
    {
        GateEntry &e = add(c, "body3d.shoulderViewsDisagree", body, "Cameras disagree about the shoulders",
            "cameras disagree at the top",
            QStringLiteral("At the top of the backswing the shoulder line points almost straight at the face-on "
                           "camera, so the shoulder furthest from it is hidden behind the chest and that camera "
                           "can only guess where it is. The 3-D fit of your body combines both cameras; here the "
                           "down-the-line camera's view of your shoulder line and the fit's differ by more than "
                           "the cameras' agreement allows. Readings built on the shoulders at the top are kept "
                           "and judged, a step lower in trust, with this warning."),
            "At the top the down-the-line camera's shoulder line and the 3-D fit's differ by {medianDeg:deg} "
            "(over {frames} frames); more than {limitDeg:deg} means the two cameras disagree.",
            { GateEffect::Downgraded }, "src/Body/skeleton3d_body_series.h", "docs/design/plane_stack_design.md §11 FB");
        e.tuning = { tune("body3d.shoulderViewsDisagreeDeg", tuned::body3d::kShoulderViewsDisagreeDeg, "°") };
    }

    // Plane stack D2 (13 Nov 2026): the 3-D arm lift at the top (Skeleton3dBodyStage, skeleton3d_body_series.h).
    // Measured against the shoulder-turn plane, which is built on the fit's shoulder line at the top — so where
    // the two cameras disagree about that line (body3d.shoulderViewsDisagree) the fit's arm lift is not read.
    // REFUSED only when that leaves no arm lift at all (no down-the-line reading either).
    {
        GateEntry &e = add(c, "body3d.armLiftShoulders", body, "Arm lift not read from the 3-D fit",
            "cameras disagree at the top",
            QStringLiteral("The arm lift is measured against the plane your shoulders turn on, and the 3-D fit builds "
                           "that plane from its shoulder line at the top. On this swing the two cameras disagree "
                           "about where the shoulders are there, so the fit's arm lift is not used, and the "
                           "down-the-line picture had no reading of its own."),
            "{code:code}.",
            { GateEffect::Refused }, "src/Body/skeleton3d_body_series.h", "docs/design/plane_stack_design.md §11 D2");
        e.codes = { { QStringLiteral("shoulderViews"),
                      QStringLiteral("The cameras disagree about the shoulders at the top, so the 3-D arm lift is not used") } };
    }
    // Plane stack D6 (Mark, 13 Nov 2026): bent_lead_arm's 3-D reading is the measured bend at the top less the
    // median over the address hold, and both ends need the lead elbow MEASURED (seen by both cameras, FB's honest
    // tier). REFUSED where either end has none, so the reading says why rather than "not segmented".
    {
        GateEntry &e = add(c, "body3d.leadElbowUnmeasured", body, "Lead elbow not seen by both cameras",
            "lead elbow not seen by both cameras",
            QStringLiteral("How much the lead arm bends between address and the top is read from the 3-D fit of the "
                           "body, only on frames where both cameras saw the lead elbow; a frame the fit placed by "
                           "its anatomy alone is left out. It needs such frames at the top and in the address hold, "
                           "where the arm is still. Where either is missing the bend is not judged, rather than "
                           "read against a guessed elbow."),
            "{code:code} ({topFrames} fit frames at the top, {holdMeasured} of {holdFrames} measured in the "
            "{holdMs:ms} address hold).",
            { GateEffect::Refused }, "src/Body/skeleton3d_body_series.h", "docs/design/plane_stack_design.md §11 D6");
        e.tuning = { tune("body3d.elbowHoldUs", double(tuned::body3d::kElbowHoldUs), "µs"),
                     tune("body3d.elbowHoldMinFrames", double(tuned::body3d::kElbowHoldMinFrames), "frames") };
        e.codes = { { QStringLiteral("top"),     QStringLiteral("Both cameras did not see the lead elbow at the top") },
                    { QStringLiteral("address"), QStringLiteral("Both cameras did not see the lead elbow in the address hold") },
                    { QStringLiteral("both"),    QStringLiteral("Both cameras saw the lead elbow neither at the top nor in the address hold") } };
    }
    // Plane stack D2: the corridor a 3-D arm lift is judged against was seated on the down-the-line picture's
    // reading. A norm row is keyed by the shot's context and the golfer's cohort, never by the route that read
    // the value, so the 3-D reading — a fill today, the answer once the cameras are calibrated — borrows it.
    // DOWNGRADED: the reading stands and is judged, a step lower in trust.
    add(c, "body3d.armLiftImageCorridor", body, "Judged against the down-the-line corridor", "corridor from the other camera",
        QStringLiteral("This arm lift was read from the 3-D fit of your body. The range it is judged against was set on "
                       "the down-the-line camera's own reading of the arm lift, which agrees with the fit's on average "
                       "but spreads golfers less widely, so the verdict is a little less certain."),
        "Read from the 3-D fit (route {route}) and judged against the corridor set on the {normRoute} reading.",
        { GateEffect::Downgraded }, "src/Body/skeleton3d_body_series.h", "docs/design/plane_stack_design.md §11 D2");

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
            QStringLiteral("Balance heel to toe is a share of the foot's length. When the foot is only a few pixels "
                           "in the down-the-line picture, the share is keypoint noise and is not read."),
            "{code:code}.",
            { GateEffect::Refused }, dtlProd, dtlDoc);
        e.tuning = { tune("dtlPosture.minSizePx", 8.0, "px") };
        e.codes = {
            { QStringLiteral("foot"),     QStringLiteral("The foot was {lengthPx:px} long in the picture; more than {minPx:px} is needed") } };
    }
    {
        GateEntry &e = add(c, "posture.ballReach", posture, "Ball distance not read", "not read",
            QStringLiteral("The ball's distance is measured with the club as the ruler: the ball sits one club "
                           "length from the hands, along the shaft. It needs the club's length, the wrists at "
                           "address, and a ball where a club held in those hands could be soled — below them at "
                           "a club's angle, and, when the picture has a ruler of its own, about a club length away."),
            "{code:code}.",
            { GateEffect::Refused }, dtlProd, dtlDoc);
        e.tuning = { tune("dtlPosture.ballAngleMinDeg", 30.0, "°"), tune("dtlPosture.ballAngleMaxDeg", 75.0, "°"),
                     tune("dtlPosture.ballLeverMin", 0.80, ""), tune("dtlPosture.ballLeverMax", 1.20, "") };
        e.codes = {
            { QStringLiteral("noClub"),     QStringLiteral("No club length was known for this swing") },
            { QStringLiteral("wrists"),     QStringLiteral("The wrists were not seen clearly at address") },
            { QStringLiteral("ballAngle"),  QStringLiteral("The ball found sat {angleDeg:deg} below the hands; a club at address points {minDeg:deg} to {maxDeg:deg} down, so it was not the ball being hit") },
            { QStringLiteral("ballLength"), QStringLiteral("The ball found sat {lengthCm:cm} from the hands, and the club is {clubCm:cm} long, so it was not the ball being hit") } };
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

    // Plane stack D2 (13 Nov 2026): the arm lift at the top in the down-the-line picture — the lead upper arm
    // (shoulder → elbow) against the shoulder line — needs the lead shoulder, the trail shoulder and the lead
    // elbow on one frame near the top. The trail shoulder is the one that goes: half hidden behind the lead
    // arm there.
    {
        GateEntry &e = add(c, "posture.armLift", posture, "Arm lift not read", "not read",
            QStringLiteral("The arm lift at the top is the angle between the lead upper arm, from the lead shoulder to "
                           "the lead elbow, and the shoulder line, in the down-the-line picture. It needs both "
                           "shoulders and the lead elbow seen clearly on a frame near the top; at the top the trail "
                           "shoulder is half hidden behind the lead arm, and it is usually the one missing."),
            "{code:code} ({frames} frames looked at within {windowMs:ms} of the top; each point must score {minConf} or more).",
            { GateEffect::Refused }, dtlProd, "docs/design/plane_stack_design.md §3.2");
        e.tuning = { tune("dtlPosture.confMin", 0.30, "") };
        e.codes = {
            { QStringLiteral("keypoints"), QStringLiteral("No frame near the top showed both shoulders and the lead elbow clearly") },
            { QStringLiteral("noFrames"),  QStringLiteral("The down-the-line camera has no frame near the top") } };
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
