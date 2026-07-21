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

#include "metric_catalogue.h"

// The one place every descriptor is declared (design §3.3 / build order §3). v1 covers the metrics
// real producers emit today: 12 MetricSeries keys from metric_extractor (×4), kinematic_series (×3)
// and foot_metrics (×5), plus 3 Summary scores produced as a ScoreBreakdown (wristScore /
// wristResemblance — live for the Wrist session; swingScore — aspirational, no live scorer yet).
// tempo / ballPosition / the kinematic sequence have no producer and are intentionally absent; see
// the metric-catalogue developer guide for adding a new metric end-to-end.
//
// Prose (description / howToRead) is consolidated from:
//   docs/design/shot_analyzer_design.md   (metric catalog table + single-camera viability)
//   docs/reference/wristmetrics.md         (bow/cup · hinge · roll sign + norms)
//   docs/reference/swing_json_schema.md    (WB2/WB3 foot-metric definitions + units)
//
// requirement.minTier stays Angles2D for every metric: wrist DOFs come from the fused IMU regardless
// of camera reconstruction tier, and the speed/foot metrics are 2D face-on — gating is by IMU role /
// face-on camera / club track, never by tier. Wrist-Motion-session gating lives in the providers.

namespace pinpoint::analysis {

void installMetricManifest(MetricCatalogue &cat)
{
    using P = Phase;
    using R = SegmentRole;

    // ------------------------------------------------------------- Score (Summary, ScoreBreakdown)

    cat.addDescriptor({
        .key = QStringLiteral("wristScore"),
        .type = MetricType::Summary,
        .label = QStringLiteral("Wrist score"),
        .shortLabel = QStringLiteral("Wrist score"),
        .unit = QString(),
        .group = QStringLiteral("Score"),
        .description = QStringLiteral(
            "The overall Wrist Motion score for the shot (0–100): the assessment engine's rollup "
            "over the banded lead-wrist checkpoints (bow/cup, hinge, roll, elbow at Top and "
            "Impact). Criterion-referenced — how well the motion matches the reference model, not "
            "a ranking against other golfers."),
        .howToRead = QStringLiteral(
            "0–100; higher is closer to the reference. It summarises the individual wrist metrics — "
            "read those to see what is driving the number. Wrist Motion session; needs the lead "
            "forearm + hand IMUs."),
        .normative = { .heuristic = true },
        .requirement = { .imuRoles = { R::LeadForearm, R::LeadHand } },
        .usedBy = { QStringLiteral("review:verdict"), QStringLiteral("shotlist:score") },
    });

    cat.addDescriptor({
        .key = QStringLiteral("wristResemblance"),
        .type = MetricType::Summary,
        .label = QStringLiteral("Wrist resemblance"),
        .shortLabel = QStringLiteral("Pattern"),
        .unit = QString(),
        .group = QStringLiteral("Score"),
        .description = QStringLiteral(
            "Which tour lead-wrist pattern the swing most resembles — bowed, neutral or cupped — "
            "with an independent 0–100 resemblance score for each "
            "(R_p = 100·exp(−½·d_p²) over Top and Impact). Not a quality grade: all three are "
            "workable patterns."),
        .howToRead = QStringLiteral(
            "The label is the best-matching pattern (argmax); the per-pattern scores show how "
            "strongly (e.g. bowed 86 / neutral 40 / cupped 8), and 'blended' flags a close top "
            "two. v1 scores lead-wrist flex/extension only. Needs the lead forearm + hand IMUs."),
        .normative = { .heuristic = true },
        .requirement = { .imuRoles = { R::LeadForearm, R::LeadHand } },
        .usedBy = { QStringLiteral("review:verdict") },
    });

    cat.addDescriptor({
        .key = QStringLiteral("swingScore"),
        .type = MetricType::Summary,
        .label = QStringLiteral("Swing score"),
        .shortLabel = QStringLiteral("Swing score"),
        .unit = QString(),
        .group = QStringLiteral("Score"),
        .description = QStringLiteral(
            "The overall Swing / GRF / Coach adherence score (0–100): how closely the full-body "
            "action reproduces an idealised efficient swing. Adherence-referenced (design §B.0)."),
        .howToRead = QStringLiteral(
            "0–100; higher is closer to the reference action. Not yet produced — the swing "
            "adherence scorer is not wired into a live analyzer, so this metric is documented but "
            "currently unavailable."),
        .normative = { .heuristic = true },
        .requirement = { .faceOnCamera = true },
        .usedBy = { QStringLiteral("review:verdict"), QStringLiteral("shotlist:score") },
    });

    // ---------------------------------------------------------------- Wrist & forearm (IMU, scored)

    cat.addDescriptor({
        .key = QStringLiteral("leadWristFlexExt"),
        .type = MetricType::TimeSeries,
        .label = QStringLiteral("Lead wrist — bow / cup"),
        .shortLabel = QStringLiteral("Bow/cup"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Wrist & forearm"),
        .description = QStringLiteral(
            "How much the lead wrist is bowed (flexed, +) or cupped (extended, −) relative to "
            "address — the axis that most shapes the clubface. Cardan component 1 of "
            "q_forearm⁻¹·q_hand about the hand medio-lateral axis."),
        .howToRead = QStringLiteral(
            "+ bowed / flexed, − cupped / extended. Read at Top and Impact; impact is typically "
            "15–30° more flexed than address, and a cupped position at the top tends to open the "
            "face. Flex/ext drives clubhead speed more than the other wrist axes. Wrist Motion "
            "session; needs the lead forearm + hand IMUs."),
        .flexPositive = true,
        .phases = { P::Top, P::Impact },
        .scored = true,
        .normative = { .dof = PpJointDof::LeadWristFlexExt,
                       .contextNote = QStringLiteral("mid-iron · neutral archetype"),
                       .heuristic = true },
        .requirement = { .imuRoles = { R::LeadForearm, R::LeadHand } },
        .usedBy = { QStringLiteral("chart:review"), QStringLiteral("score:wrist"),
                    QStringLiteral("assessment:wrist") },
    });

    cat.addDescriptor({
        .key = QStringLiteral("leadWristRadUln"),
        .type = MetricType::TimeSeries,
        .label = QStringLiteral("Lead wrist — hinge"),
        .shortLabel = QStringLiteral("Hinge"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Wrist & forearm"),
        .description = QStringLiteral(
            "Radial (−) / ulnar (+) deviation of the lead wrist — the hinge/cock that sets and "
            "holds lag. Cardan component 2 of q_forearm⁻¹·q_hand about the dorsal-palmar axis."),
        .howToRead = QStringLiteral(
            "+ ulnar (hinged/cocked), − radial. A large ulnar value at the top is normal; less "
            "deviation at impact tends to go with lower handicaps. This is the weakest IMU axis "
            "(~5° error) — read the trend, not the absolute. Needs the lead forearm + hand IMUs."),
        .flexPositive = true,
        .phases = { P::Top, P::Impact },
        .scored = true,
        .normative = { .dof = PpJointDof::LeadWristRadUln,
                       .contextNote = QStringLiteral("mid-iron · neutral archetype"),
                       .heuristic = true },
        .requirement = { .imuRoles = { R::LeadForearm, R::LeadHand } },
        .usedBy = { QStringLiteral("chart:review"), QStringLiteral("score:wrist"),
                    QStringLiteral("assessment:wrist") },
    });

    cat.addDescriptor({
        .key = QStringLiteral("forearmPronation"),
        .type = MetricType::TimeSeries,
        .label = QStringLiteral("Lead forearm — roll"),
        .shortLabel = QStringLiteral("Roll"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Wrist & forearm"),
        .description = QStringLiteral(
            "Lead-forearm pronation (+) / supination (−) — the roll that squares the face through "
            "impact. twist(q_upperarm⁻¹·q_forearm, elbow→wrist)."),
        .howToRead = QStringLiteral(
            "+ pronated, − supinated. The forearm rolls toward square through impact. There is no "
            "published tour benchmark for this axis — read it as a trend. Needs the lead forearm, "
            "hand and upper-arm IMUs."),
        .flexPositive = true,
        .phases = { P::Top, P::Impact },
        .scored = true,
        .normative = { .dof = PpJointDof::LeadForearmRot,
                       .contextNote = QStringLiteral("mid-iron · neutral archetype"),
                       .heuristic = true },
        .requirement = { .imuRoles = { R::LeadForearm, R::LeadHand, R::LeadUpperArm } },
        .usedBy = { QStringLiteral("chart:review"), QStringLiteral("score:wrist"),
                    QStringLiteral("assessment:wrist") },
    });

    cat.addDescriptor({
        .key = QStringLiteral("leadArmFlexion"),
        .type = MetricType::TimeSeries,
        .label = QStringLiteral("Lead elbow — flexion"),
        .shortLabel = QStringLiteral("Elbow"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Wrist & forearm"),
        .description = QStringLiteral(
            "Lead-elbow flexion magnitude — how bent the lead arm is. acos(dot(û_upper, û_fore)) "
            "from the shoulder / elbow / wrist chain."),
        .howToRead = QStringLiteral(
            "0 = straight, larger = more bent. A near-straight lead arm (small flexion) through "
            "impact is the goal. Needs the lead forearm, hand and upper-arm IMUs."),
        .flexPositive = true,
        .phases = { P::Top, P::Impact },
        .scored = true,
        .normative = { .dof = PpJointDof::LeadElbowFlex,
                       .contextNote = QStringLiteral("mid-iron · neutral archetype"),
                       .heuristic = true },
        .requirement = { .imuRoles = { R::LeadForearm, R::LeadHand, R::LeadUpperArm } },
        .usedBy = { QStringLiteral("chart:review"), QStringLiteral("score:wrist"),
                    QStringLiteral("assessment:wrist") },
    });

    // ------------------------------------------------------- Club & speed (face-on club track, 2D)

    cat.addDescriptor({
        .key = QStringLiteral("clubheadSpeed"),
        .type = MetricType::TimeSeries,
        .label = QStringLiteral("Clubhead speed"),
        .shortLabel = QStringLiteral("Club spd"),
        .unit = QStringLiteral("mph"),
        .group = QStringLiteral("Club & speed"),
        .description = QStringLiteral(
            "Clubhead speed near impact from the tracked head path: ‖v_clubhead‖, a central "
            "difference of the clubhead's 2D position scaled to the ground plane."),
        .howToRead = QStringLiteral(
            "Read the peak / @impact value. Reference (TrackMan 2024): driver ~113 mph, 7-iron "
            "~89 mph. On a single face-on camera this is an in-plane estimate — depth-axis motion "
            "is approximate. Needs face-on club tracking."),
        .phases = { P::Impact },
        .normative = { .contextNote = QStringLiteral("club-dependent — see reference norms"),
                       .heuristic = true },
        .requirement = { .faceOnCamera = true, .clubTrack = true },
        .usedBy = { QStringLiteral("chart:review") },
    });

    cat.addDescriptor({
        .key = QStringLiteral("handSpeed"),
        .type = MetricType::TimeSeries,
        .label = QStringLiteral("Hand speed"),
        .shortLabel = QStringLiteral("Hand spd"),
        .unit = QStringLiteral("mph"),
        .group = QStringLiteral("Club & speed"),
        .description = QStringLiteral(
            "Speed of the lead-hand (grip) point from the tracked grip path — the hand-path proxy "
            "for delivery speed."),
        .howToRead = QStringLiteral(
            "Read the peak / @impact value. Hand speed peaks slightly before impact in an "
            "efficient release. Needs face-on club tracking (grip point)."),
        .phases = { P::Impact },
        .normative = { .heuristic = true },
        .requirement = { .faceOnCamera = true, .clubTrack = true },
        .usedBy = { QStringLiteral("chart:review") },
    });

    cat.addDescriptor({
        .key = QStringLiteral("lagAngle"),
        .type = MetricType::TimeSeries,
        .label = QStringLiteral("Lag angle"),
        .shortLabel = QStringLiteral("Lag"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Club & speed"),
        .description = QStringLiteral(
            "Angle between the lead forearm and the club shaft — the retained wrist lag. Derived "
            "from the shaft track (grip→head) and the lead-forearm pose."),
        .howToRead = QStringLiteral(
            "Larger through the downswing = more retained lag; it releases toward impact. Needs "
            "both the face-on club track and lead-forearm pose."),
        .phases = { P::Downswing, P::Impact },
        .normative = { .heuristic = true },
        .requirement = { .faceOnCamera = true, .clubTrack = true },
        .usedBy = { QStringLiteral("chart:review") },
    });

    // ------------------------------------------------ Feet & stance (whole-body pose, face-on, 2D)

    cat.addDescriptor({
        .key = QStringLiteral("stanceWidth"),
        .type = MetricType::PointInTime,
        .label = QStringLiteral("Stance width"),
        .shortLabel = QStringLiteral("Stance"),
        .unit = QStringLiteral("×frame"),
        .group = QStringLiteral("Feet & stance"),
        .description = QStringLiteral(
            "Heel-to-heel stance width at address as a fraction of frame width (isotropic), from "
            "the whole-body pose feet keypoints."),
        .howToRead = QStringLiteral(
            "Measured once, at address. Read as a proportion of the frame; wider or narrower "
            "relative to shoulder width suits different clubs. Needs a face-on whole-body camera."),
        .phases = { P::Address },
        .normative = { .heuristic = true },
        .requirement = { .faceOnCamera = true },
        .usedBy = { QStringLiteral("chart:review") },
    });

    cat.addDescriptor({
        .key = QStringLiteral("leadFootFlare"),
        .type = MetricType::PointInTime,
        .label = QStringLiteral("Lead foot flare"),
        .shortLabel = QStringLiteral("Lead flare"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Feet & stance"),
        .description = QStringLiteral(
            "Lead-foot flare at address — that foot's heel→big-toe direction vs the image +x axis."),
        .howToRead = QStringLiteral(
            "Measured once, at address. More flare (toe-out) on the lead foot can ease the "
            "follow-through. Needs a face-on whole-body camera."),
        .phases = { P::Address },
        .normative = { .heuristic = true },
        .requirement = { .faceOnCamera = true },
        .usedBy = { QStringLiteral("chart:review") },
    });

    cat.addDescriptor({
        .key = QStringLiteral("trailFootFlare"),
        .type = MetricType::PointInTime,
        .label = QStringLiteral("Trail foot flare"),
        .shortLabel = QStringLiteral("Trail flare"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Feet & stance"),
        .description = QStringLiteral(
            "Trail-foot flare at address — that foot's heel→big-toe direction vs the image +x axis."),
        .howToRead = QStringLiteral(
            "Measured once, at address. Trail-foot flare influences how the hips can turn in the "
            "backswing. Needs a face-on whole-body camera."),
        .phases = { P::Address },
        .normative = { .heuristic = true },
        .requirement = { .faceOnCamera = true },
        .usedBy = { QStringLiteral("chart:review") },
    });

    cat.addDescriptor({
        .key = QStringLiteral("toeLineAngle"),
        .type = MetricType::PointInTime,
        .label = QStringLiteral("Toe line"),
        .shortLabel = QStringLiteral("Toe line"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Feet & stance"),
        .description = QStringLiteral(
            "Alignment of the toe line at address: the lead-big-toe→trail-big-toe direction vs the "
            "image +x axis."),
        .howToRead = QStringLiteral(
            "Measured once, at address. Indicates stance alignment (open / square / closed) in the "
            "image plane. Needs a face-on whole-body camera."),
        .phases = { P::Address },
        .normative = { .heuristic = true },
        .requirement = { .faceOnCamera = true },
        .usedBy = { QStringLiteral("chart:review") },
    });

    cat.addDescriptor({
        .key = QStringLiteral("leadHeelLift"),
        .type = MetricType::TimeSeries,
        .label = QStringLiteral("Lead heel lift"),
        .shortLabel = QStringLiteral("Heel lift"),
        .unit = QStringLiteral("×frame"),
        .group = QStringLiteral("Feet & stance"),
        .description = QStringLiteral(
            "Lead-heel elevation relative to address (heel vs toe) as a fraction of frame height; "
            "+ = the heel lifts off the ground."),
        .howToRead = QStringLiteral(
            "A per-frame curve. Some lift at the top is common; staying grounded is a style "
            "choice. Read the trend, not an absolute. Needs a face-on whole-body camera."),
        .phases = { P::Top },
        .normative = { .heuristic = true },
        .requirement = { .faceOnCamera = true },
        .usedBy = { QStringLiteral("chart:review") },
    });
}

} // namespace pinpoint::analysis
