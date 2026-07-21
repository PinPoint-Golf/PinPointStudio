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

// The one place every descriptor is declared (design §3.3 / build order §3) — the full design
// catalogue (shot_analyzer_design.md §A), each metric either LIVE or a PLANNED placeholder.
//
// LIVE (18) — a producer emits it today: metric_extractor ×4, kinematic_series ×3 + shaft-lean,
//   foot_metrics ×5, head_track ×3, plus wristScore / wristResemblance (Summary, from a
//   ScoreBreakdown, Wrist session).
// PLANNED (20, `.planned = true`) — in the design catalogue but no producer in this build: the
//   whole-body rotation / spine / pelvis / club-delivery / tempo / kinematic-sequence metrics and
//   swingScore. The PlannedMetricProvider claims these so they resolve "planned", and their
//   `.requirement` reads as "will need …" on the detail page.
//
// See the metric-catalogue developer guide for promoting a placeholder to live (add the producer,
// drop `.planned`, move the key from PlannedMetricProvider to a real provider).
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
        .planned = true,
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

    // ---------------------------------------------------- Body rotation (PLANNED — body IMUs, no producer)

    cat.addDescriptor({
        .key = QStringLiteral("pelvisRotation"),
        .type = MetricType::TimeSeries,
        .label = QStringLiteral("Pelvis rotation"),
        .shortLabel = QStringLiteral("Pelvis turn"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Body rotation"),
        .description = QStringLiteral(
            "Axial pelvis turn relative to address — the pelvis medio-lateral axis rotated into the "
            "horizontal plane."),
        .howToRead = QStringLiteral(
            "Read at Top and Impact. Reference: ~45° at the top, ~35–45° open at impact. Will need "
            "a pelvis IMU."),
        .phases = { P::Top, P::Impact },
        .planned = true,
        .normative = { .heuristic = true },
        .requirement = { .imuRoles = { R::Pelvis } },
    });

    cat.addDescriptor({
        .key = QStringLiteral("thoraxRotation"),
        .type = MetricType::TimeSeries,
        .label = QStringLiteral("Thorax rotation"),
        .shortLabel = QStringLiteral("Chest turn"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Body rotation"),
        .description = QStringLiteral("Axial thorax (chest) turn relative to address."),
        .howToRead = QStringLiteral(
            "Read at Top and Impact. Reference: shoulders ~90° at the top. Will need a thorax IMU."),
        .phases = { P::Top, P::Impact },
        .planned = true,
        .normative = { .heuristic = true },
        .requirement = { .imuRoles = { R::Thorax } },
    });

    cat.addDescriptor({
        .key = QStringLiteral("xFactor"),
        .type = MetricType::TimeSeries,
        .label = QStringLiteral("X-factor"),
        .shortLabel = QStringLiteral("X-factor"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Body rotation"),
        .description = QStringLiteral(
            "Thorax-minus-pelvis axial separation — the classic X-factor (shoulder-vs-pelvis is "
            "≈2× the spine measure)."),
        .howToRead = QStringLiteral(
            "Read at the top. Reference ~40–42° (TPI). Will need pelvis + thorax IMUs."),
        .phases = { P::Top },
        .planned = true,
        .normative = { .heuristic = true },
        .requirement = { .imuRoles = { R::Pelvis, R::Thorax } },
    });

    cat.addDescriptor({
        .key = QStringLiteral("xFactorStretch"),
        .type = MetricType::TimeSeries,
        .label = QStringLiteral("X-factor stretch"),
        .shortLabel = QStringLiteral("X-stretch"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Body rotation"),
        .description = QStringLiteral(
            "The extra separation gained early in the downswing: max(X-factor over early "
            "downswing) − X-factor at the top. The stretch-shorten power signal."),
        .howToRead = QStringLiteral(
            "Larger = more stored stretch; a better speed predictor than static X-factor. "
            "Reference ~5°. Will need pelvis + thorax IMUs."),
        .phases = { P::Transition, P::Downswing },
        .planned = true,
        .normative = { .heuristic = true },
        .requirement = { .imuRoles = { R::Pelvis, R::Thorax } },
    });

    cat.addDescriptor({
        .key = QStringLiteral("hipInternalRotation"),
        .type = MetricType::TimeSeries,
        .label = QStringLiteral("Hip internal rotation"),
        .shortLabel = QStringLiteral("Hip rotation"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Body rotation"),
        .description = QStringLiteral(
            "True hip-joint rotation — thigh axial turn relative to the pelvis, per side."),
        .howToRead = QStringLiteral(
            "Read at Top and Impact. Reference: lead ~50° / trail ~40° amplitude. Will need pelvis "
            "+ thigh IMUs."),
        .phases = { P::Top, P::Impact },
        .planned = true,
        .normative = { .heuristic = true },
        .requirement = { .imuRoles = { R::Pelvis, R::LeadThigh, R::TrailThigh } },
    });

    // ------------------------------------------------ Spine & pelvis (PLANNED — camera-3D / fused)

    cat.addDescriptor({
        .key = QStringLiteral("spineForwardBend"),
        .type = MetricType::TimeSeries,
        .label = QStringLiteral("Spine forward bend"),
        .shortLabel = QStringLiteral("Fwd bend"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Spine & pelvis"),
        .description = QStringLiteral(
            "Posture / forward bend — flexion-extension of the thorax relative to the pelvis."),
        .howToRead = QStringLiteral(
            "Read at Address and Impact; a retained ~30–40° (irons) from address is the goal. Will "
            "need pelvis + thorax IMUs (or a 3D camera)."),
        .phases = { P::Address, P::Impact },
        .planned = true,
        .normative = { .heuristic = true },
        .requirement = { .imuRoles = { R::Pelvis, R::Thorax } },
    });

    cat.addDescriptor({
        .key = QStringLiteral("spineSideBend"),
        .type = MetricType::TimeSeries,
        .label = QStringLiteral("Spine side bend"),
        .shortLabel = QStringLiteral("Side bend"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Spine & pelvis"),
        .description = QStringLiteral(
            "Lateral flexion (trail-side bend) of the thorax relative to the pelvis."),
        .howToRead = QStringLiteral(
            "Read at Impact. Reference: thorax ~32° / pelvis ~10° at driver impact. Will need a "
            "face-on camera (or IMUs)."),
        .phases = { P::Impact },
        .planned = true,
        .normative = { .heuristic = true },
        .requirement = { .faceOnCamera = true },
    });

    cat.addDescriptor({
        .key = QStringLiteral("secondaryAxisTilt"),
        .type = MetricType::TimeSeries,
        .label = QStringLiteral("Secondary axis tilt"),
        .shortLabel = QStringLiteral("Axis tilt"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Spine & pelvis"),
        .description = QStringLiteral(
            "Spine lean away from the target — angle of the mid-hip→mid-shoulder vector from "
            "vertical in the frontal plane."),
        .howToRead = QStringLiteral(
            "Read at Impact. Reference ~20–25° at impact (~6–8° at address). Will need a face-on "
            "camera."),
        .phases = { P::Impact },
        .planned = true,
        .normative = { .heuristic = true },
        .requirement = { .faceOnCamera = true },
    });

    cat.addDescriptor({
        .key = QStringLiteral("pelvisSway"),
        .type = MetricType::TimeSeries,
        .label = QStringLiteral("Pelvis sway"),
        .shortLabel = QStringLiteral("Sway"),
        .unit = QStringLiteral("cm"),
        .group = QStringLiteral("Spine & pelvis"),
        .description = QStringLiteral(
            "Lateral pelvis displacement along the target line relative to address."),
        .howToRead = QStringLiteral(
            "Away from target in the backswing, toward it in the downswing. Read near the top and "
            "at Impact. Will need a face-on camera + ground plane."),
        .phases = { P::Top, P::Impact },
        .planned = true,
        .normative = { .heuristic = true },
        .requirement = { .faceOnCamera = true },
    });

    cat.addDescriptor({
        .key = QStringLiteral("pelvisThrust"),
        .type = MetricType::TimeSeries,
        .label = QStringLiteral("Pelvis thrust"),
        .shortLabel = QStringLiteral("Thrust"),
        .unit = QStringLiteral("cm"),
        .group = QStringLiteral("Spine & pelvis"),
        .description = QStringLiteral(
            "Toward-ball pelvis displacement; positive too early is an early-extension fault."),
        .howToRead = QStringLiteral(
            "Should stay minimal toward the ball until late. Read in the downswing and at Impact. "
            "Needs a down-the-line camera (the depth axis) — a lone face-on camera cannot resolve "
            "it."),
        .phases = { P::Downswing, P::Impact },
        .planned = true,
        .normative = { .heuristic = true },
        .requirement = { .faceOnCamera = true, .minTier = ReconstructionTier::Stereo3D },
    });

    cat.addDescriptor({
        .key = QStringLiteral("pelvisLift"),
        .type = MetricType::TimeSeries,
        .label = QStringLiteral("Pelvis lift"),
        .shortLabel = QStringLiteral("Lift"),
        .unit = QStringLiteral("cm"),
        .group = QStringLiteral("Spine & pelvis"),
        .description = QStringLiteral("Vertical pelvis displacement relative to address."),
        .howToRead = QStringLiteral(
            "A small controlled rise is normal. Read at Impact. Will need a face-on camera + "
            "ground plane."),
        .phases = { P::Impact },
        .planned = true,
        .normative = { .heuristic = true },
        .requirement = { .faceOnCamera = true },
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

    cat.addDescriptor({
        .key = QStringLiteral("impactShaftLean"),
        .type = MetricType::TimeSeries,
        .label = QStringLiteral("Shaft lean"),
        .shortLabel = QStringLiteral("Shaft lean"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Club & speed"),
        .description = QStringLiteral(
            "Forward shaft lean — the shaft's angle from vertical, from the tracked club/shaft."),
        .howToRead = QStringLiteral(
            "Read at Impact; forward lean (hands ahead of the ball) is typical for irons. Needs "
            "the face-on club track. Wrist Motion session."),
        .phases = { P::Impact },
        .normative = { .heuristic = true },
        .requirement = { .faceOnCamera = true, .clubTrack = true },
        .usedBy = { QStringLiteral("chart:review") },
    });

    // ------------------------------------------------ Club delivery (PLANNED — club track / DTL)

    cat.addDescriptor({
        .key = QStringLiteral("swingPlane"),
        .type = MetricType::TimeSeries,
        .label = QStringLiteral("Swing plane"),
        .shortLabel = QStringLiteral("Plane"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Club delivery"),
        .description = QStringLiteral(
            "Best-fit plane of the clubhead (or lead-hand proxy) path over the knee-to-knee "
            "downswing — reported as tilt vs ground plus azimuth."),
        .howToRead = QStringLiteral(
            "Read over the downswing; values are club-dependent. A down-the-line camera is the "
            "classic swing-plane view. Will need the club track."),
        .phases = { P::Downswing },
        .planned = true,
        .normative = { .heuristic = true },
        .requirement = { .clubTrack = true, .minTier = ReconstructionTier::Stereo3D },
    });

    cat.addDescriptor({
        .key = QStringLiteral("clubPath"),
        .type = MetricType::PointInTime,
        .label = QStringLiteral("Club path"),
        .shortLabel = QStringLiteral("Path"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Club delivery"),
        .description = QStringLiteral(
            "Horizontal angle of the clubhead velocity vs the target line at impact "
            "(+ in-to-out / − out-to-in)."),
        .howToRead = QStringLiteral(
            "Read at Impact; near 0 ± a few degrees. The discriminating axis is the optical axis, "
            "so this is a canonical down-the-line metric. Will need the club track + a DTL camera."),
        .phases = { P::Impact },
        .planned = true,
        .normative = { .heuristic = true },
        .requirement = { .clubTrack = true, .minTier = ReconstructionTier::Stereo3D },
    });

    cat.addDescriptor({
        .key = QStringLiteral("attackAngle"),
        .type = MetricType::PointInTime,
        .label = QStringLiteral("Attack angle"),
        .shortLabel = QStringLiteral("Attack"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Club delivery"),
        .description = QStringLiteral("Vertical angle of the clubhead velocity at impact."),
        .howToRead = QStringLiteral(
            "Read at Impact. Reference: driver ~−1.3°, 7-iron ~−4.5° (TrackMan). Will need the "
            "club track (a DTL camera makes it fully in-plane)."),
        .phases = { P::Impact },
        .planned = true,
        .normative = { .heuristic = true },
        .requirement = { .clubTrack = true, .minTier = ReconstructionTier::Stereo3D },
    });

    cat.addDescriptor({
        .key = QStringLiteral("faceAngle"),
        .type = MetricType::PointInTime,
        .label = QStringLiteral("Face angle"),
        .shortLabel = QStringLiteral("Face"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Club delivery"),
        .description = QStringLiteral(
            "Horizontal angle of the clubface normal vs the target line at impact — the primary "
            "gate on ball start direction."),
        .howToRead = QStringLiteral(
            "Read at Impact; small open/closed. Needs a club device (or club tracking); a camera "
            "alone gives only a labelled forearm+wrist proxy."),
        .phases = { P::Impact },
        .planned = true,
        .normative = { .heuristic = true },
        .requirement = { .clubTrack = true, .minTier = ReconstructionTier::ClubInstrumented },
    });

    cat.addDescriptor({
        .key = QStringLiteral("lowPointAhead"),
        .type = MetricType::PointInTime,
        .label = QStringLiteral("Low point"),
        .shortLabel = QStringLiteral("Low pt"),
        .unit = QStringLiteral("in"),
        .group = QStringLiteral("Club delivery"),
        .description = QStringLiteral(
            "Signed target-line distance from the ball to the clubhead arc's low point "
            "(+ ahead = a descending blow)."),
        .howToRead = QStringLiteral(
            "Read near Impact; + (ahead of the ball) is a descending blow for irons, driver is "
            "behind. Will need a face-on camera with shaft-head + ball tracking; deferred pending "
            "the measured-clubhead detector."),
        .phases = { P::Impact },
        .planned = true,
        .normative = { .heuristic = true },
        .requirement = { .faceOnCamera = true, .clubTrack = true, .ballTrack = true },
    });

    // --------------------------------------------- Tempo & sequence (PLANNED — phase events / fused)

    cat.addDescriptor({
        .key = QStringLiteral("tempoBackswing"),
        .type = MetricType::Summary,
        .label = QStringLiteral("Backswing tempo"),
        .shortLabel = QStringLiteral("Backswing"),
        .unit = QStringLiteral("s"),
        .group = QStringLiteral("Tempo & sequence"),
        .description = QStringLiteral("Duration of the backswing (Address → Top)."),
        .howToRead = QStringLiteral(
            "Reference ~0.75–0.85 s (TPI 0.847 ± 0.111). Will need a segmented swing (phase "
            "events)."),
        .planned = true,
        .normative = { .heuristic = true },
    });

    cat.addDescriptor({
        .key = QStringLiteral("tempoRatio"),
        .type = MetricType::Summary,
        .label = QStringLiteral("Tempo ratio"),
        .shortLabel = QStringLiteral("Tempo"),
        .unit = QString(),
        .group = QStringLiteral("Tempo & sequence"),
        .description = QStringLiteral(
            "Backswing-to-downswing time ratio (backswing time ÷ Top→Impact time)."),
        .howToRead = QStringLiteral(
            "Reference ~3:1 (tour 2.2–3.0:1). Will need a segmented swing."),
        .planned = true,
        .normative = { .heuristic = true },
    });

    cat.addDescriptor({
        .key = QStringLiteral("kinematicSequence"),
        .type = MetricType::Sequence,
        .label = QStringLiteral("Kinematic sequence"),
        .shortLabel = QStringLiteral("Sequence"),
        .unit = QString(),
        .group = QStringLiteral("Tempo & sequence"),
        .description = QStringLiteral(
            "The proximal-to-distal peak-speed sequence — the order, timing gaps and magnitudes of "
            "peak axial angular speed for pelvis → thorax → lead-arm → club."),
        .howToRead = QStringLiteral(
            "Look for a clean proximal-to-distal order with only the club still accelerating "
            "through impact (reference peaks e.g. pelvis ~480, thorax ~605, lead-arm ~1310 °/s; "
            "transition ~50 ms). Will need body IMUs (pelvis + thorax + lead forearm) and the club "
            "track."),
        .phases = { P::Transition, P::Impact },
        .planned = true,
        .normative = { .heuristic = true },
        .requirement = { .imuRoles = { R::Pelvis, R::Thorax, R::LeadForearm }, .clubTrack = true },
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

    // -------------------------------------------- Alignment (PLANNED — pose lines at address/impact)

    cat.addDescriptor({
        .key = QStringLiteral("shoulderAlignment"),
        .type = MetricType::PointInTime,
        .label = QStringLiteral("Shoulder alignment"),
        .shortLabel = QStringLiteral("Shoulders"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Alignment"),
        .description = QStringLiteral(
            "Angle of the shoulder line (lead→trail shoulder) in the image plane — how open or "
            "closed the shoulders are."),
        .howToRead = QStringLiteral(
            "Read at Address and Impact; square at address and a touch open at impact is typical. "
            "Will need a face-on camera (a down-the-line view gives true target-line alignment)."),
        .phases = { P::Address, P::Impact },
        .planned = true,
        .normative = { .heuristic = true },
        .requirement = { .faceOnCamera = true },
    });

    cat.addDescriptor({
        .key = QStringLiteral("elbowAlignment"),
        .type = MetricType::PointInTime,
        .label = QStringLiteral("Elbow alignment"),
        .shortLabel = QStringLiteral("Elbows"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Alignment"),
        .description = QStringLiteral(
            "Angle of the line between the elbows (lead→trail elbow) in the image plane."),
        .howToRead = QStringLiteral(
            "Read at Address and Impact; reflects how the arms are structured through the strike. "
            "Will need a face-on camera."),
        .phases = { P::Address, P::Impact },
        .planned = true,
        .normative = { .heuristic = true },
        .requirement = { .faceOnCamera = true },
    });

    cat.addDescriptor({
        .key = QStringLiteral("hipAlignment"),
        .type = MetricType::PointInTime,
        .label = QStringLiteral("Hip alignment"),
        .shortLabel = QStringLiteral("Hips"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Alignment"),
        .description = QStringLiteral(
            "Angle of the hip line (lead→trail hip) in the image plane — how open or closed the "
            "hips are."),
        .howToRead = QStringLiteral(
            "Read at Address and Impact; the hips are typically more open than the shoulders at "
            "impact. Will need a face-on camera (down-the-line for true target-line alignment)."),
        .phases = { P::Address, P::Impact },
        .planned = true,
        .normative = { .heuristic = true },
        .requirement = { .faceOnCamera = true },
    });

    cat.addDescriptor({
        .key = QStringLiteral("feetAlignment"),
        .type = MetricType::PointInTime,
        .label = QStringLiteral("Feet alignment"),
        .shortLabel = QStringLiteral("Feet"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Alignment"),
        .description = QStringLiteral(
            "Angle of the foot line (lead→trail ankle) in the image plane — the stance line."),
        .howToRead = QStringLiteral(
            "Read at Address and Impact; complements the address-only toe line. Will need a "
            "face-on camera."),
        .phases = { P::Address, P::Impact },
        .planned = true,
        .normative = { .heuristic = true },
        .requirement = { .faceOnCamera = true },
    });

    // ---------------------------------------------------- Head (whole-body pose, face-on, 2D; live)

    cat.addDescriptor({
        .key = QStringLiteral("headSway"),
        .type = MetricType::TimeSeries,
        .label = QStringLiteral("Head sway"),
        .shortLabel = QStringLiteral("Head sway"),
        .unit = QStringLiteral("×frame"),
        .group = QStringLiteral("Head"),
        .description = QStringLiteral(
            "Lateral head displacement relative to address, as a fraction of frame width "
            "(isotropic)."),
        .howToRead = QStringLiteral(
            "A per-frame curve; some lateral movement is normal, excessive sway is a fault — read "
            "the trend. Needs a face-on camera. Wrist Motion session."),
        .phases = { P::Top, P::Impact },
        .normative = { .heuristic = true },
        .requirement = { .faceOnCamera = true },
        .usedBy = { QStringLiteral("chart:review") },
    });

    cat.addDescriptor({
        .key = QStringLiteral("headLift"),
        .type = MetricType::TimeSeries,
        .label = QStringLiteral("Head lift"),
        .shortLabel = QStringLiteral("Head lift"),
        .unit = QStringLiteral("×frame"),
        .group = QStringLiteral("Head"),
        .description = QStringLiteral(
            "Vertical head displacement relative to address, as a fraction of frame width; "
            "+ = the head rises."),
        .howToRead = QStringLiteral(
            "A per-frame curve; read the trend against address. Needs a face-on camera. Wrist "
            "Motion session."),
        .phases = { P::Top, P::Impact },
        .normative = { .heuristic = true },
        .requirement = { .faceOnCamera = true },
        .usedBy = { QStringLiteral("chart:review") },
    });

    cat.addDescriptor({
        .key = QStringLiteral("headTilt"),
        .type = MetricType::TimeSeries,
        .label = QStringLiteral("Head tilt"),
        .shortLabel = QStringLiteral("Head tilt"),
        .unit = QStringLiteral("°"),
        .group = QStringLiteral("Head"),
        .description = QStringLiteral("Eye-line tilt angle relative to address."),
        .howToRead = QStringLiteral(
            "A per-frame curve of the eye-line angle change from address. Needs a face-on camera. "
            "Wrist Motion session."),
        .phases = { P::Top, P::Impact },
        .normative = { .heuristic = true },
        .requirement = { .faceOnCamera = true },
        .usedBy = { QStringLiteral("chart:review") },
    });
}

} // namespace pinpoint::analysis
