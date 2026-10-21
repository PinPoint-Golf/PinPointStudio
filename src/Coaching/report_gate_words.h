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

// WHAT WE COULD NOT CHECK, IN A GOLFER'S WORDS (docs/design/session_report_design.md §9.3 item 5, §10
// rule 7). The gate catalogue's own claim words ("club peak drawn, not judged", "swing plane not one
// plane") are written for an analyst; a client report says the same thing once, on its back page, in
// these. One clause per catalogue id — per-swing gates and engine rules alike — lower case, no digits,
// and WITHOUT ITS SUBJECT: one gate (sequence.sightedBand) refuses several readings, so the clause is
// written to follow the reading's name ("when your hips turned fastest: the face-on camera cannot time a
// turn while you are square to it…"). The subjects have their own words below; a metric subject uses
// its measure's `golferName` (the caller resolves that through the pack).
//
// Authored in the content phase (build/report-build/content/gatewords.merged.json, agents B1–B3; the
// subject words are part3.notes.md's proposal), copied here verbatim so the app carries no build-tree
// file. A gate the table does not name returns empty, and the report then leaves that line out rather
// than printing the analyst's words: an internal phrase on a client document is worse than a gap the
// technical appendix fills.
//
// Header-only and pure; report_model_test pins that every entry is lower case and digit-free.

#include <QHash>
#include <QString>

namespace pinpoint::report {

// The clause for a gate or rule id ("sequence.sightedBand", "rule.diag.noCorridor"); empty if unknown.
inline QString gateGolferWords(const QString &gateId)
{
    static const QHash<QString, QString> w{
        { QStringLiteral("sequence.sightedBand"),
          QStringLiteral("the face-on camera cannot time a turn while you are square to it; a down-the-line camera or a body sensor can") },
        { QStringLiteral("sequence.validityHole"),
          QStringLiteral("the cameras lost track of you for a moment just there") },
        { QStringLiteral("sequence.reversalSpike"),
          QStringLiteral("the reading jumped as your arms crossed your body, so there was no true peak to time") },
        { QStringLiteral("sequence.risingAtEnd"),
          QStringLiteral("it was still speeding up when the part of the swing we time had ended") },
        { QStringLiteral("sequence.routePlacement"),
          QStringLiteral("this camera set-up can draw it, but is not yet trusted to time it") },
        { QStringLiteral("sequence.maxPlaceSigma"),
          QStringLiteral("we could not pin down its timing closely enough to compare it with the rest") },
        { QStringLiteral("sequence.credibleClub"),
          QStringLiteral("the cameras lost the club through the downswing") },
        { QStringLiteral("sequence.clubWithheld"),
          QStringLiteral("our cameras could see it, but not well enough to judge") },
        { QStringLiteral("sequence.pairRoute"),
          QStringLiteral("the two-camera reading was not usable, so the face-on camera's was used instead") },
        { QStringLiteral("sequence.pairMinCorr"),
          QStringLiteral("the two camera views disagreed, so the face-on camera's reading was used instead") },
        { QStringLiteral("sequence.noPeak"),
          QStringLiteral("the cameras could not see it through the downswing") },
        { QStringLiteral("sequence.routeWithdrawn"),
          QStringLiteral("the two-camera reading could not time it, so the face-on camera's was used instead") },
        { QStringLiteral("body.lowerAddress"),
          QStringLiteral("the cameras could not see your hips, knees and ankles clearly at address") },
        { QStringLiteral("body.stanceSpan"),
          QStringLiteral("you were too small in the face-on picture to measure your stance") },
        { QStringLiteral("body.hipLine"),
          QStringLiteral("your hips were edge-on to the camera for part of the swing") },
        { QStringLiteral("body.upperAddress"),
          QStringLiteral("the cameras could not see your shoulders, feet and lead arm clearly at address") },
        { QStringLiteral("body.shoulderSpan"),
          QStringLiteral("you were too small in the face-on picture to measure your shoulders") },
        { QStringLiteral("body.shoulderLine"),
          QStringLiteral("your shoulders were edge-on to the camera for part of the swing") },
        { QStringLiteral("body.elbowLine"),
          QStringLiteral("your elbows were too close together in the picture to measure") },
        { QStringLiteral("body.sideBendLines"),
          QStringLiteral("your hips or shoulders were edge-on to the camera for part of the swing") },
        { QStringLiteral("body.rotation"),
          QStringLiteral("a single face-on camera cannot measure how far you turn; a down-the-line camera or a body sensor can") },
        { QStringLiteral("body.xFactorStretchTop"),
          QStringLiteral("we could not find the top of the swing to measure from") },
        { QStringLiteral("body.ballPosition"),
          QStringLiteral("the camera could not find the ball clearly at address") },
        { QStringLiteral("posture.address"),
          QStringLiteral("the down-the-line camera could not see your feet and hips clearly at address") },
        { QStringLiteral("posture.ruler"),
          QStringLiteral("we had no reliable way to turn the picture into real distances") },
        { QStringLiteral("posture.sizeFloor"),
          QStringLiteral("you were too small in the down-the-line picture to measure it") },
        { QStringLiteral("posture.handLoop"),
          QStringLiteral("the down-the-line camera could not follow your hands clearly enough") },
        { QStringLiteral("posture.shaftLie"),
          QStringLiteral("the down-the-line camera never measured the shaft clearly") },
        { QStringLiteral("club.trackRefused"),
          QStringLiteral("the face-on camera could not follow the club well enough to measure it") },
        { QStringLiteral("club.followThrough"),
          QStringLiteral("after impact the camera followed your arm instead of the club") },
        { QStringLiteral("club.ballSuspect"),
          QStringLiteral("the ball the camera found did not line up with the club at address, so this is less certain") },
        { QStringLiteral("club.headConfidence"),
          QStringLiteral("the clubhead was blurred or hidden at the top") },
        { QStringLiteral("club.attackFallback"),
          QStringLiteral("the club's path through the ball was only partly seen, so this is less certain") },
        { QStringLiteral("club.lowPoint"),
          QStringLiteral("the camera could not see the bottom of the swing clearly enough") },
        { QStringLiteral("club.peakLeadSamples"),
          QStringLiteral("the camera did not see enough of the club in the downswing") },
        { QStringLiteral("club.planeWindow"),
          QStringLiteral("the club's path was not seen clearly enough to fit a plane to it") },
        { QStringLiteral("club3d.planeScatter"),
          QStringLiteral("the club did not move on one plane that the cameras could measure") },
        { QStringLiteral("club3d.illConditioned"),
          QStringLiteral("the two cameras were looking along nearly the same line there") },
        { QStringLiteral("clubDtl.topOutOfView"),
          QStringLiteral("the club went out of the top of the down-the-line picture, which is the camera's framing, not your swing") },
        { QStringLiteral("clubDtl.quarantine"),
          QStringLiteral("the down-the-line camera could not find your hands where it expected them") },
        { QStringLiteral("clubDtl.lateEscape"),
          QStringLiteral("in the follow-through the camera picked up something that was not the club") },
        { QStringLiteral("impact.pathTooShort"),
          QStringLiteral("the impact camera saw too little of the club around the ball") },
        { QStringLiteral("impact.headOffsetAssumed"),
          QStringLiteral("the impact camera rarely saw the clubhead itself, so its path is an estimate") },
        { QStringLiteral("lm.noData"),
          QStringLiteral("the launch monitor did not measure the club on these swings, usually because it could not see the stickers on the face") },
        { QStringLiteral("lm.outOfRange"),
          QStringLiteral("the launch monitor sent a value no golf shot produces") },
        { QStringLiteral("lm.noInputs"),
          QStringLiteral("the launch monitor measured neither the clubface nor the path") },
        { QStringLiteral("shot.captureHoles"),
          QStringLiteral("the recording dropped frames, so the whole swing was left out") },
        { QStringLiteral("shot.refusionParity"),
          QStringLiteral("a body sensor's recording did not hang together, so the whole swing was left out") },
        { QStringLiteral("skeleton.refused"),
          QStringLiteral("the three-dimensional model of you needs both cameras to see you clearly, and could not be built") },
        { QStringLiteral("tempo.refused"),
          QStringLiteral("we could not find address, the top and impact clearly enough to time them") },
        { QStringLiteral("wrist.hmBinding"),
          QStringLiteral("the HackMotion recording did not say how the sensor was worn") },
        { QStringLiteral("wrist.cellRefused"),
          QStringLiteral("the wrist sensor had too few clean readings at some positions") },
        { QStringLiteral("wrist.findingLowConfidence"),
          QStringLiteral("the wrist readings were not clean enough to be sure") },
        { QStringLiteral("wrist.strengthWithoutFault"),
          QStringLiteral("a wrist strength is only shown beside a fault it guards against") },
        { QStringLiteral("rule.diag.patternWilson"),
          QStringLiteral("seen, but not on enough swings to call it a habit") },
        { QStringLiteral("rule.diag.patternMinShots"),
          QStringLiteral("it could be judged on too few swings to say") },
        { QStringLiteral("rule.diag.directionAgreement"),
          QStringLiteral("it missed both ways, so there is no single direction to report") },
        { QStringLiteral("rule.diag.trend"),
          QStringLiteral("too few swings to say whether it was getting better or worse") },
        { QStringLiteral("rule.diag.dependence"),
          QStringLiteral("too few swings to tell whether the two go together") },
        { QStringLiteral("rule.diag.warmUp"),
          QStringLiteral("your first few swings count for less, as warm-up") },
        { QStringLiteral("rule.diag.debounce"),
          QStringLiteral("the picture was still settling") },
        { QStringLiteral("rule.diag.rankBand"),
          QStringLiteral("the order is held steady so it does not shuffle from one swing to the next") },
        { QStringLiteral("rule.diag.borderline"),
          QStringLiteral("it sat so close to the edge of the target range that it could go either way") },
        { QStringLiteral("rule.diag.rootStability"),
          QStringLiteral("how firm a cause is depends on how precisely the swings were measured") },
        { QStringLiteral("rule.diag.noCorridor"),
          QStringLiteral("there is no target range for it with this club yet") },
        { QStringLiteral("rule.diag.implausible"),
          QStringLiteral("the reading was not believable, so we treated it as a camera or sensor fault") },
        { QStringLiteral("rule.diag.peakAfterImpact"),
          QStringLiteral("your hips were still speeding up at impact, so whether they slowed could not be read") },
        { QStringLiteral("rule.diag.nonPositiveDenominator"),
          QStringLiteral("one of the two readings it compares was missing or zero") },
        { QStringLiteral("rule.diag.mixedScale"),
          QStringLiteral("the two readings it compares came from different kinds of measurement") },
        { QStringLiteral("rule.diag.inferredContext"),
          QStringLiteral("no club was set, so it was judged against a default club's range") },
        { QStringLiteral("rule.diag.workOnLimits"),
          QStringLiteral("a session names at most five work-ons") },
        { QStringLiteral("rule.diag.workOnQuiet"),
          QStringLiteral("quiet means it showed on fewer than half the swings we could judge") },
        { QStringLiteral("rule.diag.workOnClear"),
          QStringLiteral("a work-on leaves the list after two quiet sessions in a row") },
        { QStringLiteral("rule.diag.themesMin"),
          QStringLiteral("themes need more swings, over more sessions, than we have yet") },
        { QStringLiteral("rule.diag.themeSessionJudged"),
          QStringLiteral("this session judged it on too few swings to count toward your history") },
        { QStringLiteral("rule.diag.themeCoverage"),
          QStringLiteral("it was measured on too few of your swings to take part") },
        { QStringLiteral("rule.diag.sequenceSessionMin"),
          QStringLiteral("too few swings had all four peaks timed to judge the session's sequence") },
        { QStringLiteral("rule.diag.sequenceOrderShare"),
          QStringLiteral("the share of swings in order is stated cautiously, at the generous end of what the swings support") },
        { QStringLiteral("rule.charts.groupMin"),
          QStringLiteral("too few swings to draw a typical shape") },
        { QStringLiteral("rule.charts.instantMin"),
          QStringLiteral("the cameras lost sight of it on most swings just there") },
        { QStringLiteral("rule.charts.kindsMin"),
          QStringLiteral("too few swings to split into two kinds") },
        { QStringLiteral("rule.charts.leadToWatch"),
          QStringLiteral("one group had too few swings, so the difference is a lead to watch") },
        { QStringLiteral("rule.charts.correlationMin"),
          QStringLiteral("too few swings had both readings to see whether they move together") },
        { QStringLiteral("rule.charts.boxMin"),
          QStringLiteral("too few swings to show a spread") },
        { QStringLiteral("rule.charts.kindsAsClubs"),
          QStringLiteral("the two kinds of swing matched the two clubs, so they are named as clubs") },
        { QStringLiteral("rule.charts.warpAnchors"),
          QStringLiteral("a swing position was found too uncertainly to line the swings up on") },
        { QStringLiteral("rule.lm.spreadMin"),
          QStringLiteral("too few shots to show how much they varied") },
        { QStringLiteral("rule.lm.clubVotes"),
          QStringLiteral("the ball numbers did not clearly read like a wood or an iron") },
        { QStringLiteral("rule.lm.flightShape"),
          QStringLiteral("the launch monitor did not measure enough to name the shot shape") },
        { QStringLiteral("rule.lm.strikeQuality"),
          QStringLiteral("the launch monitor did not measure where on the face you struck it") },
    };
    return w.value(gateId);
}

// The reading a gate acted on (GateRecord subject: "ks.pelvis", "track.club", "shot"), in golfer words;
// empty for a subject this table does not name (a metric key: the caller uses its measure's golferName).
inline QString gateSubjectWords(const QString &subject)
{
    static const QHash<QString, QString> w{
        { QStringLiteral("ks.pelvis"), QStringLiteral("when your hips turned fastest") },
        { QStringLiteral("ks.thorax"), QStringLiteral("when your chest turned fastest") },
        { QStringLiteral("ks.leadArm"), QStringLiteral("when your lead arm moved fastest") },
        { QStringLiteral("ks.club"), QStringLiteral("how fast the club was moving at its peak") },
        { QStringLiteral("track.club"), QStringLiteral("the club, seen face-on") },
        { QStringLiteral("track.clubDtl"), QStringLiteral("the club, seen down the line") },
        { QStringLiteral("track.club3d"), QStringLiteral("the club in three dimensions") },
        { QStringLiteral("track.impact"), QStringLiteral("the club at the ball") },
        { QStringLiteral("track.skeleton3d"), QStringLiteral("the three-dimensional model of you") },
        { QStringLiteral("shot"), QStringLiteral("the whole swing") },
    };
    return w.value(subject);
}

} // namespace pinpoint::report
