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
