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

// The gate catalogue's Club entries (trust stage 6, docs/design/trust_stage6_plan.md). Included by
// gate_catalogue.h after GateEntry and the detail::add / detail::rule helpers; not a standalone header.
//
// Thresholds that live in config structs (ShaftV3Config, DtlShaftConfig, fusion::Config) or are
// hard-coded at the gate are catalogued with their literal compiled value, named by the override key
// that sets them (or the key they would move to: rule 5 of the plan). No value here changes a gate.

namespace pinpoint::gates::detail {

inline void appendClubGates(std::vector<GateEntry> &c)
{
    namespace CD = tuned::clubDelivery;
    namespace FT = tuned::shaft::followThrough;
    const QString club   = QStringLiteral("Club and shaft");
    const QString impact = QStringLiteral("Impact camera");
    const char *asmProd    = "src/Club/shaft_track_assembly.cpp";
    const char *pipeProd   = "src/Analysis/analysis_pipeline.cpp";
    const char *cdProd     = "src/Club/club_delivery.cpp";
    const char *kinProd    = "src/Kinematics/kinematic_series.cpp";
    const char *dtlProd    = "src/Club/dtl_shaft_post.cpp";
    const char *impProd    = "src/Ball/impact_runner.cpp";
    const char *trackDoc   = "docs/design/club_tracking_v3_design.md";
    const char *posDoc     = "docs/design/shaft_position_first_design.md";
    const char *planeDoc   = "docs/implementation/transition_plane_producer_brief.md";
    const char *fusionDoc  = "docs/design/shaft_fusion_design.md";
    const char *dtlDoc     = "docs/design/dtl_continuous_track_design_update.md";
    const char *impactDoc  = "docs/design/impact_camera_design.md";

    // ── The face-on club track as a whole (shaft_track_assembly.cpp faceOnTrackGates) ────────────
    {
        GateEntry &e = add(c, "club.trackRefused", club, "Club track refused", "refused",
            QStringLiteral("Every club reading from the face-on camera — clubhead and hand speed, lag, shaft lean, "
                           "the swing plane, attack angle, low point — is read off one track of the shaft through "
                           "the swing. The track is published only when the camera saw the club on enough of the "
                           "swing and the track agrees with what else is known: at address the club points at the "
                           "ball and is as long as the hands-to-ball distance, the backswing shows up in the club's "
                           "own motion, and the hands it is gripped by are consistent. A track that fails any of "
                           "these is not a measurement, so none of the readings built on it is shown."),
            "{code:code}.",
            { GateEffect::Refused }, asmProd, trackDoc);
        e.tuning = { tune("shaft.coverageMin", 0.60, ""),
                     tune("shaft.addr.p1BallConflictDeg", 25.0, "°"),
                     tune("shaft.addr.lenBallTol", 0.30, ""),
                     tune("shaft.hands.unusableFrac", 0.5, "") };
        e.codes = {
            { QStringLiteral("coverage"), QStringLiteral("The club was seen on {coverage:pct} of the swing; {minCoverage:pct} is needed") },
            { QStringLiteral("p1Ball"),   QStringLiteral("At address the club pointed {p1BallDeg:deg} away from the ball (more than {maxP1BallDeg:deg}), and no clear frame showed it there") },
            { QStringLiteral("phases"),   QStringLiteral("The backswing could not be found in the club's motion") },
            { QStringLiteral("length"),   QStringLiteral("The club's length at address did not match the hands-to-ball distance (ratio {lenBallRatio}; it must be within {lenBallTol:pct} of 1)") },
            { QStringLiteral("hands"),    QStringLiteral("The hands were inconsistent on {handFramesFixed} of the swing's {frames} frames: there was no grip to track from") } };
    }

    add(c, "club.followThrough", club, "Follow-through not the club", "follow-through not the club",
        QStringLiteral("After impact the club only slows down. A shaft angle that turns faster after the ball than "
                       "the club ever turned into it is the tracker following something else — usually an arm — "
                       "so those frames are demoted to predictions: drawn, but not read as the club."),
        "{frames} of {ofFrames} frames after impact ({frac:pct})[ from {fromMs:t} to {toMs:t}] turned faster than the "
        "club did into the ball.",
        { GateEffect::Masked }, asmProd, posDoc)
        .tuning = { tune("shaft.followThrough.rateFactor", FT::kRateFactor, ""),
                    tune("shaft.followThrough.minRateCapDps", FT::kMinRateCapDps, "°/s"),
                    tune("shaft.followThrough.peakWindowUs", double(FT::kPeakWindowUs) / 1000.0, "ms") };

    add(c, "club.ballSuspect", club, "Address ball doubtful", "ball doubtful",
        QStringLiteral("Shaft lean at impact is read as the line from the hands to the ball, because at contact the "
                       "clubhead is at the ball. When the club at address, clearly measured, did not point at the "
                       "ball that was found, it is the ball that is in doubt, so the lean read from it is kept at a "
                       "lower trust."),
        "At address the club pointed {p1BallDeg:deg} away from the ball found (more than {maxP1BallDeg:deg}).",
        { GateEffect::Downgraded }, pipeProd, trackDoc)
        .tuning = { tune("shaft.addr.p1BallConflictDeg", 25.0, "°") };

    // ── Club delivery (club_delivery.cpp) ────────────────────────────────────────────────────────
    add(c, "club.headConfidence", club, "Clubhead not seen clearly", "clubhead not seen",
        QStringLiteral("The shaft angle at the top is read from the clubhead against the grip. Where the clubhead "
                       "was not found with confidence — blurred, hidden, or placed along the shaft at an assumed "
                       "length — there is no angle to read, and those frames are left out."),
        "No clear clubhead on {frames} of {ofFrames} frames ({frac:pct})[ from {fromMs:t} to {toMs:t}][, "
        "{lowConfFrames} of them below the confidence floor of {minHeadConf}].",
        { GateEffect::Masked, GateEffect::Refused }, cdProd, trackDoc)
        .tuning = { tune("clubDelivery.headConfMin", CD::kHeadConfMin, "") };

    {
        GateEntry &e = add(c, "club.attackFallback", club, "Attack angle off the measured heads",
            "read off the measured heads",
            QStringLiteral("Attack angle is read from the clubhead's path through impact. The smooth arc between the "
                           "located club positions reads it within a few degrees of a launch monitor; the clubhead "
                           "the camera measures frame by frame is a blur at impact and read a median 36° away. When "
                           "the arc does not reach impact densely enough, or jumps there, the measured heads answer "
                           "instead, at a lower trust — or nothing does."),
            "{code:code}.",
            { GateEffect::Downgraded, GateEffect::Refused }, cdProd, posDoc);
        e.tuning = { tune("clubDelivery.attackSynthMinSamples", CD::kAttackSynthMinSamples, "samples"),
                     tune("clubDelivery.attackSynthWinUs", double(CD::kAttackSynthWinUs) / 1000.0, "ms"),
                     tune("clubDelivery.attackSynthMaxStepRatio", CD::kAttackSynthMaxStepRatio, "") };
        e.codes = {
            { QStringLiteral("fewArc"), QStringLiteral("The arc had {arcSamples} samples within {winMs:ms} of impact; {minArcSamples} are needed") },
            { QStringLiteral("jump"),   QStringLiteral("The arc jumped through impact (a step over {maxStepRatio} times its usual one)") } };
    }

    {
        GateEntry &e = add(c, "club.lowPoint", club, "Low point not found", "not found",
            QStringLiteral("The low point is where the club's arc bottoms out, measured against the ball along the "
                           "target line. It needs the arc around impact, an arc that visibly turns over inside that "
                           "window, and the ball to measure from and to give the scale in inches. Without any of "
                           "these the low point would be a guess about a part of the swing nothing observed."),
            "{code:code}.",
            { GateEffect::Refused }, cdProd, posDoc);
        e.tuning = { tune("clubDelivery.lowPointMinSamples", CD::kLowPointMinSamples, "samples"),
                     tune("clubDelivery.lowPointWinUs", double(CD::kLowPointWinUs) / 1000.0, "ms") };
        e.codes = {
            { QStringLiteral("noBall"),     QStringLiteral("No address ball, or no scale from it, to measure the low point against") },
            { QStringLiteral("fewSamples"), QStringLiteral("The arc had {samples} samples within {winMs:ms} of impact; {minSamples} are needed") },
            { QStringLiteral("edge"),       QStringLiteral("The arc did not turn over within {winMs:ms} of impact") },
            { QStringLiteral("noSweep"),    QStringLiteral("The clubhead did not move across the picture through impact") } };
    }

    // ── Club kinematics (kinematic_series.cpp) ───────────────────────────────────────────────────
    add(c, "club.peakLeadSamples", club, "Downswing not covered", "downswing not covered",
        QStringLiteral("How long before the ball the clubhead stopped gaining speed is read from the clubhead speed "
                       "between the top and impact. With fewer than three usable samples there, the track never "
                       "covered the downswing, and a peak found would be the edge of the data, not the club's."),
        "{samples} usable samples between the top and impact; at least {minSamples} are needed.",
        { GateEffect::Refused }, kinProd, posDoc)
        .tuning = { tune("kinematics.peakLeadMinSamples", 3.0, "samples") };

    // ── Swing plane, face-on (analysis_pipeline.cpp ShaftPlaneStage; shaft_plane.h) ──────────────
    {
        GateEntry &e = add(c, "club.planeWindow", club, "Plane not fitted", "plane not fitted",
            QStringLiteral("The transition plane compares the ellipse the shaft sweeps in the backswing with the one "
                           "it sweeps in the downswing. An arc too short or too sparse still admits an ellipse — an "
                           "arbitrarily thin one — and its tilt then runs to 90° and takes the comparison with it. "
                           "So each half needs enough samples and an ellipse at least a quarter as wide as it is "
                           "long; with neither the measured nor the smoothed track giving both, there is no reading."),
            "{code:code}.",
            { GateEffect::Refused }, pipeProd, planeDoc);
        e.tuning = { tune("shaftPlane.minAxisRatio", 0.26, ""),
                     tune("shaftPlane.minSamples", 12.0, "samples") };
        e.codes = {
            { QStringLiteral("needle"),     QStringLiteral("The shaft's path over one half of the swing was a line, not an ellipse (axis ratio {ratio}; {minRatio} needed)") },
            { QStringLiteral("fewSamples"), QStringLiteral("Too few shaft samples to fit the plane ({nBack} in the backswing, {nDown} in the downswing; {minSamples} needed in each)") },
            { QStringLiteral("noEllipse"),  QStringLiteral("The shaft's path did not fit an ellipse") } };
    }

    // ── The shaft in 3-D (analysis_pipeline.cpp ShaftFusionStage; shaft_fusion.h) ────────────────
    {
        GateEntry &e = add(c, "club3d.planeScatter", club, "Not one plane", "not one plane",
            QStringLiteral("The swing plane is the plane the shaft's 3-D direction moves in, fitted to the frames both "
                           "cameras saw. When those directions scatter too far either side of the fitted plane, the "
                           "club did not move in one plane that the cameras could see — in the backswing most often "
                           "because one stretch of the down-the-line track was read mirrored — so that half of the "
                           "reading is not offered."),
            "{code:code}: the shaft scattered {rmsDeg:deg} about the plane over {frames} frames; at most {maxRmsDeg:deg}.",
            { GateEffect::Refused }, pipeProd, fusionDoc);
        e.tuning = { tune("shaft.fusion.planeOopMaxDeg", 5.0, "°"),
                     tune("shaft.fusion.backIncoherentDeg", 12.0, "°") };
        e.codes = {
            { QStringLiteral("down"), QStringLiteral("Downswing") },
            { QStringLiteral("back"), QStringLiteral("Backswing") } };
    }

    add(c, "club3d.illConditioned", club, "Views too close to cross", "views too close",
        QStringLiteral("The shaft's 3-D direction is where the two cameras' views of it cross. When the two views "
                       "look along nearly the same plane — down the line, at impact, the camera sits on the plane's "
                       "edge — they barely cross, and the direction they give is noise. Those frames are left out "
                       "of the plane fits."),
        "{frames} of {ofFrames} frames ({frac:pct})[ from {fromMs:t} to {toMs:t}] had the two views within "
        "{minCond} of each other.",
        { GateEffect::Masked }, pipeProd, fusionDoc)
        .tuning = { tune("shaft.fusion.minCond", 0.26, "") };

    // ── Why there is no P8 (p8_refusal_gate.h; plane stack D6, 13 Nov 2026) ─────────────────────────
    // REFUSED, on the PHASE (subject position.p8): the readings that need P8 are not produced, and a measure
    // whose reducer needs P8 names this record through gates::phaseSubject (LiveMeasureSource::gateCause).
    // Fired after the run only where the document's ladder has no P8 and the face-on tracker said why.
    {
        namespace PO = tuned::positions;
        GateEntry &e = add(c, "positions.p8Refused", club, "P8 not timed", "not timed",
            QStringLiteral("P8 is the moment after impact when the shaft comes back up through level, the "
                           "follow-through's mirror of shaft-parallel going back. It is looked for only a short "
                           "time after impact, it must be the club rising through level, and it is refused after "
                           "the tracked shaft sat upright for a while first, which is the tracker holding a bright "
                           "upright line in the background rather than the club. Where none of that is met there "
                           "is no P8, and the readings that need it (the lead knee and the lead arm against the "
                           "body after impact, and the changes from address read at P8) are not judged, rather "
                           "than read at a moment that was not the follow-through."),
            "No P8: {code:code}.",
            { GateEffect::Refused }, "src/Club/p8_refusal_gate.h", "docs/design/plane_stack_design.md §11 D6");
        e.tuning = { tune("positions.p8MinAfterImpactUs", double(PO::kP8MinAfterImpactUs), "µs"),
                     tune("positions.p8MaxAfterImpactUs", double(PO::kP8MaxAfterImpactUs), "µs"),
                     tune("positions.p8StallBandDeg", PO::kP8StallBandDeg, "°"),
                     tune("positions.p8StallMinUs", double(PO::kP8StallMinUs), "µs") };
        e.codes = {
            { QStringLiteral("noCrossing"),
              QStringLiteral("after impact the shaft was never seen coming back up through level from {minAfterMs:ms} on") },
            { QStringLiteral("late"),
              QStringLiteral("the shaft first came back up through level more than {maxAfterMs:ms} after impact, too "
                             "late to be the follow-through; the tracker was most likely on a bright line in the "
                             "background rather than the club") },
            { QStringLiteral("wrongSense"),
              QStringLiteral("the first time the shaft crossed level after impact it was going down, not up: the club "
                             "track had turned end for end") },
            { QStringLiteral("verticalStall"),
              QStringLiteral("the tracked shaft sat within {stallBandDeg:deg} of upright for {stallMinMs:ms} before it "
                             "crossed level: the tracker was on something upright in the background, not the club") } };
    }

    // ── The club's climb placed in the on-plane window halfway back (club_slot_top.h; plane stack D3) ──
    // REFUSED: clubSlotTop is a ratio, so it needs all three of its parts and a window wide enough to divide by.
    // The ids keep "slot" (stored records); every word a reader sees says "on-plane window" / "halfway back",
    // because the golf "slot" is a DOWNSWING position (Mark, 13 Nov 2026).
    // Fired only on a swing with a down-the-line plane at all; a part's own refusal (club.trackRefused) also
    // covers the reading and comes first, so this is the reason given only where nothing else refused.
    {
        GateEntry &e = add(c, "club.slotUnread", club, "Plane halfway back not read", "not read",
            QStringLiteral("Where the club sits halfway back is read against your own on-plane window: the wedge "
                           "between the plane the club was set on at address and the plane from the ball up "
                           "through your trail shoulder. With the lead arm level with the ground, 0 is on the "
                           "plane you set and 1 on the line through your trail shoulder. It needs the swing plane "
                           "at address and at that point, and the shoulder plane at address, which needs the ball "
                           "and your trail shoulder seen from down the line. A window only a few degrees wide is "
                           "too narrow to place the club in: a degree of camera error would move the reading by a "
                           "large part of it, so none is given."),
            "{code:code}.",
            { GateEffect::Refused }, "src/Club/club_slot_top.h", "docs/design/plane_stack_design.md §3.3");
        e.tuning = { tune("club.slotMinWidthDeg", tuned::clubSlot::kMinWidthDeg, "°") };
        e.codes = {
            { QStringLiteral("noAddressPlane"),  QStringLiteral("No swing plane at address") },
            { QStringLiteral("noP3Plane"),       QStringLiteral("No swing plane with the lead arm level going back") },
            { QStringLiteral("noShoulderPlane"), QStringLiteral("No shoulder plane at address: it needs the ball and the trail shoulder seen from down the line") },
            { QStringLiteral("narrow"),          QStringLiteral("The shoulder plane sat {widthDeg:deg} above the plane the club was set on; the window between them must be at least {minWidthDeg:deg} wide") } };
    }

    // ── The on-plane window halfway back with the camera assumed (club_slot_top.h; plane stack D6 round 2) ──
    // DOWNGRADED, never refused: the reading is kept and judged with its ⚠ while the down-the-line camera is
    // uncalibrated (the flag club3d.cameraAssumed reads). It lifts when a measured calibration reaches the job.
    add(c, "club.slotCameraAssumed", club, "Plane halfway back read with an uncalibrated camera", "camera position assumed",
        QStringLiteral("The two edges of the on-plane window halfway back, the plane the club was set on and the plane "
                       "from the ball through your "
                       "trail shoulder, and the club's plane going back are all lines in the down-the-line "
                       "picture. A camera turned a few degrees moves them little, but a camera standing "
                       "somewhere else, higher or nearer the hands, moves them by different amounts, and the "
                       "club's place between them does not cancel that. Until the camera is calibrated the reading is kept and "
                       "judged a step lower in trust: compare it within a session, where the camera stayed put, "
                       "not between sessions. The takeaway is not affected the same way: it compares the club's "
                       "own plane at two moments, which a camera move shifts alike."),
        "The down-the-line camera is uncalibrated, so where it stands can move the edges of this {widthDeg:deg} window; compare "
        "the reading within a session.",
        { GateEffect::Downgraded }, "src/Club/club_slot_top.h", "docs/design/plane_stack_design.md §11 D6");

    // ── Where the shaft points (shaft_direction_series.h, for ShaftDirectionStage) ───────────────
    // DOWNGRADED, never refused: both readings are shown and judged, a step lower in trust, with the
    // warning. The camera record lifts when a measured calibration reaches the job.
    const char *sdProd = "src/Club/shaft_direction_series.h";
    add(c, "club3d.cameraAssumed", club, "Camera direction assumed", "camera direction assumed",
        QStringLiteral("Where the shaft points is read against the target line, and the down-the-line camera is "
                       "what tells the two cameras which way that line runs. Until the camera has been calibrated "
                       "with the alignment sticks, its direction is assumed to be straight down the line — and a "
                       "camera turned a few degrees turns every reading of where the shaft points by the same few "
                       "degrees. So the reading is kept and judged, a step lower in trust, and carries that "
                       "much more uncertainty. Calibrating the camera removes this warning."),
        "The down-the-line camera's direction is assumed, so where the shaft points carries ±{sigmaYawDeg:deg}.",
        { GateEffect::Downgraded }, sdProd, fusionDoc);
    {
        GateEntry &e = add(c, "club3d.shaftDirectionFromFit", club, "Read from the 3-D fit", "from the 3-D fit",
            QStringLiteral("Where the shaft points is normally read where the two cameras' tracks of the shaft cross. "
                           "When, at a position, the two tracks could not fix the shaft's direction — their views "
                           "met too nearly edge-on (halfway back the shaft points almost straight at the "
                           "down-the-line camera), they disagreed about which way along the shaft the head was, "
                           "or the face-on camera did not measure the club there — the reading comes from the 3-D "
                           "fit of the golfer and club instead, which holds the club in the hands and on its swing "
                           "plane. It is kept and judged, a step lower in trust, with this warning."),
            "{code:code} came from the 3-D fit rather than the two shaft tracks[ ({fitFrames} of {ofFrames} frames of "
            "the curve did)].",
            { GateEffect::Downgraded }, sdProd, fusionDoc);
        e.codes = {
            { QStringLiteral("p2"),    QStringLiteral("The reading halfway back") },
            { QStringLiteral("top"),   QStringLiteral("The reading at the top") },
            { QStringLiteral("both"),  QStringLiteral("The readings halfway back and at the top") } };
    }

    // ── The down-the-line club track (dtl_shaft_post.cpp dtlShaftGates) ──────────────────────────
    add(c, "clubDtl.topOutOfView", club, "Top out of the picture", "top out of the picture",
        QStringLiteral("At the top of the backswing the club rises above the hands. When the hands are less than a "
                       "club's length below the top of the down-the-line picture, the club leaves it, and the "
                       "tracker has nothing to measure there. The camera framing is the cause, not the swing."),
        "Nothing measured over {frames} frames[ from {fromMs:t} to {toMs:t}]: the hands were {handsFromTopPx:px} below "
        "the top of the picture and the club is {clubPx:px} long in it.",
        { GateEffect::Masked }, dtlProd, dtlDoc);

    add(c, "clubDtl.quarantine", club, "Hands not where expected", "hands not where expected",
        QStringLiteral("The down-the-line track starts each frame from the hands. When the hands it found are not at "
                       "the height the face-on camera puts them — or the wrist was not seen with confidence — the "
                       "frame has no trustworthy start point, and nothing is published for it."),
        "{frames} of {ofFrames} frames ({frac:pct})[ from {fromMs:t} to {toMs:t}] had their hands quarantined"
        "[ ({wristFrames} with the wrist unseen, {rowFrames} off the face-on height by more than {maxRowPx:px})].",
        { GateEffect::Masked }, dtlProd, "docs/design/dtl_shaft_tracker_design.md")
        .tuning = { tune("shaft.dtl.quarantine.absPx", 80.0, "px"),
                    tune("shaft.dtl.quarantine.postAbsPx", 40.0, "px"),
                    tune("shaft.dtl.quarantine.postImpactUs", 80.0, "ms") };

    add(c, "clubDtl.lateEscape", club, "Not the club in the follow-through", "not the club",
        QStringLiteral("Once the shaft has passed parallel to the ground after impact, a line found outside the "
                       "direction the face-on camera allows is not the club — on the recorded swings it was a line "
                       "parked at the edge of the picture. Those frames are not published."),
        "{frames} frames[ from {fromMs:t} to {toMs:t}] after the club passed parallel[ at {p8Ms:t}] left the face-on "
        "camera's corridor.",
        { GateEffect::Masked }, dtlProd, dtlDoc)
        .tuning = { sw("shaft.dtl.refuseLateEscape", true) };

    // ── The impact camera (impact_runner.cpp) ────────────────────────────────────────────────────
    add(c, "impact.pathTooShort", impact, "Club path not fitted", "path not fitted",
        QStringLiteral("The club's path through the ball is a curve fitted to the clubhead in the impact camera's "
                       "frames around the moment the ball leaves. With fewer clean frames of the club than the "
                       "curve needs, no path is drawn."),
        "{points} clean club frames around the ball's departure; {minPoints} are needed.",
        { GateEffect::Refused }, impProd, impactDoc)
        .tuning = { tune("impact.minPathPoints", 4.0, "frames") };

    add(c, "impact.headOffsetAssumed", impact, "Clubhead position assumed", "head position assumed",
        QStringLiteral("The path is fitted to the hosel, which the camera finds reliably; the clubhead sits at an "
                       "offset from it that is measured from the frames that show the head. When too few do, a "
                       "generic head offset is used, so the path drawn for the head is the hosel's shifted by an "
                       "assumption."),
        "{headFrames} frames showed the clubhead; {minHeadFrames} are needed to measure its offset.",
        { GateEffect::Downgraded }, impProd, impactDoc)
        .tuning = { tune("impact.minHeadFrames", 3.0, "frames") };
}

} // namespace pinpoint::gates::detail
