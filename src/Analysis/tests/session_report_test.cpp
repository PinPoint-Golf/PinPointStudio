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

// session_report.h — every rule the client report uses to choose what to say, one block per
// function, on hand-built sessions (work_ons_test's Plan/sessionOf encoding), then Nick Henn's
// 20 October session rebuilt from the model dump to show the reductions pick what the mock printed
// (docs/design/session_report_design.md §3.2, §3.3, §4.2, §6, §7.3, §10).

#include "../../Coaching/session_report.h"

#include <cstdio>
#include <limits>
#include <vector>

using namespace pinpoint::analysis;
using namespace pinpoint::analysis::report;

namespace {

int g_fail = 0;
void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

// 1 fired, 0 clean, -1 not assessable — work_ons_test's encoding.
struct Plan {
    QString id;
    std::vector<int> states;
    double z = 3.0;
    double cleanZ = 0.5;    // where a CLEAN swing reads: inside the Ideal band unless a test says otherwise
};
Plan plan(const char *id, std::vector<int> states, double z = 3.0, double cleanZ = 0.5)
{
    return Plan{ QString::fromLatin1(id), std::move(states), z, cleanZ };
}
std::vector<int> rep(int v, int n) { return std::vector<int>(size_t(n), v); }

std::vector<ShotRecord> sessionOf(const std::vector<Plan> &plans)
{
    size_t n = 0;
    for (const Plan &p : plans) n = std::max(n, p.states.size());
    std::vector<ShotRecord> shots;
    for (size_t i = 0; i < n; ++i) {
        ShotRecord s;
        s.shotId = int(i) + 1;
        s.club = QStringLiteral("7i");
        s.timestampMs = 1'700'000'000'000LL + qint64(i) * 45'000LL;
        for (const Plan &p : plans) {
            ConditionRow r;
            r.conditionId = p.id;
            r.drivingMeasureId = p.id + QStringLiteral(".measure");
            r.corridorLo = -2.0;
            r.corridorHi = 2.0;
            r.corridorShape = CorridorShape::TwoSided;
            const int v = i < p.states.size() ? p.states[i] : -1;
            if (v == 1)      { r.state = ShotState::Fired; r.direction = 1; r.z = p.z; r.value = p.z * 2.0; }
            else if (v == 0) { r.state = ShotState::Clean; r.z = p.cleanZ; r.value = 1.0; }
            else             { r.state = ShotState::NotAssessable; r.notAssessableReason = QStringLiteral("not seen"); }
            s.rows.push_back(std::move(r));
        }
        shots.push_back(std::move(s));
    }
    return shots;
}

LedgerOptions flatOpts()
{
    LedgerOptions o;
    o.warmUpWeight = 1.0;
    o.softTier = false;
    return o;
}

// A session: its ledgers and its work-on record, the record reduced from the pack facts the way
// SessionDiagnosticsModel::sessionWorkOns() marshals them (held → Excluded).
struct Session {
    std::vector<ShotRecord>      shots;
    std::vector<ConditionLedger> ledgers;
    SessionWorkOns               workOns;
};

Session sessionFrom(const std::vector<Plan> &plans, const ReportPack &pack, const QHash<QString, double> &pos = {})
{
    Session s;
    s.shots = sessionOf(plans);
    s.ledgers = conditionLedgers(s.shots, flatOpts());
    QHash<QString, WorkOnClass> cls;
    for (const ReportCondition &c : pack.conditions) {
        WorkOnClass k = WorkOnClass::Excluded;
        if (c.kind == FactKind::Fault || c.kind == FactKind::Setup) k = WorkOnClass::Movement;
        if (c.kind == FactKind::Delivery) k = WorkOnClass::Delivery;
        if (c.held) k = WorkOnClass::Excluded;
        cls.insert(c.id, k);
    }
    WorkOnCauses causes;
    for (const ReportCause &e : pack.causes) causes.emplace_back(e.from, e.to);
    s.workOns = reduceSessionWorkOns(s.shots, s.ledgers, cls, causes, pos);
    return s;
}

ReportCondition fact(const char *id, FactKind kind, int prominence = 2, const char *axis = "", bool well = true)
{
    ReportCondition c;
    c.id = QString::fromLatin1(id);
    c.kind = kind;
    c.prominence = prominence;
    c.axis = QString::fromLatin1(axis);
    c.hasGolferWell = well;
    c.liveMeasure = true;
    return c;
}

void add(ReportPack &p, const ReportCondition &c) { p.conditions.insert(c.id, c); }
void cause(ReportPack &p, const char *from, const char *to, int strength = 2)
{
    p.causes.push_back({ QString::fromLatin1(from), QString::fromLatin1(to), strength });
}

QStringList idsOf(const std::vector<StrengthPick> &v) { QStringList o; for (const auto &x : v) o << x.id; return o; }
QStringList idsOf(const std::vector<CauseItem> &v)    { QStringList o; for (const auto &x : v) o << x.id; return o; }
QStringList idsOf(const std::vector<CameWithItem> &v) { QStringList o; for (const auto &x : v) o << x.id; return o; }
QStringList idsOf(const SessionWorkOns &w)            { QStringList o; for (const auto &x : w.entries) o << x.id; return o; }

// A two-sided corridor, pass lo..hi, fault lines faultLo/faultHi.
SpreadCorridor twoSided(double lo, double hi, double faultLo, double faultHi)
{
    SpreadCorridor c;
    c.known = true;
    c.shape = CorridorShape::TwoSided;
    c.mu = 0.5 * (lo + hi);
    c.sigmaLo = c.sigmaHi = 1.0;
    c.idealLo = c.signalLo = lo;
    c.idealHi = c.signalHi = hi;
    c.faultLo = faultLo;
    c.faultHi = faultHi;
    return c;
}
// Higher is better: only the low tail grades. Pass from `lo` up, fault at `fault`.
SpreadCorridor floorAt(double mu, double lo, double fault)
{
    SpreadCorridor c = twoSided(lo, mu, fault, mu);
    c.shape = CorridorShape::Floor;
    c.mu = mu;
    c.highOpen = true;
    c.signalHi = c.idealHi = c.faultHi = mu;
    return c;
}
// Lower is better: only the high tail grades. Pass up to `hi`, fault at `fault`.
SpreadCorridor ceilingAt(double mu, double hi, double fault)
{
    SpreadCorridor c = twoSided(mu, hi, mu, fault);
    c.shape = CorridorShape::Ceiling;
    c.mu = mu;
    c.lowOpen = true;
    c.signalLo = c.idealLo = c.faultLo = mu;
    return c;
}

HabitRun run(const char *id, std::vector<double> values, const SpreadCorridor &c)
{
    return HabitRun{ QString::fromLatin1(id), std::move(values), c };
}

// ── The Nick fixture's row types (data generated below) ──────────────────────────
struct NickRow     { const char *id; const char *states; };
struct NickPattern { const char *id; double excess; };
struct NickCond    { const char *id; FactKind kind; int prominence; const char *axis; bool well, screen, live; };
struct NickEdge    { const char *from, *to; int strength; };
struct NickRun     { const char *id; const char *phase; double values[10]; bool lowOpen, highOpen;
                     double mu, signalLo, signalHi, faultLo, faultHi; const char *unit; };

// GENERATED by build/report-build/D/nick_fixture.py from dumps/nick03.json (10 swings, in folder order)
// and core.json as of 20 Oct 2026: every condition the readouts carry, every authored cause into or
// out of a session pattern, and the work-ons' runs against the corridors their charts drew.
const NickRow kNickRows[] = {
    { "trail_knee_straighten", "1111111111" },
    { "hips_closed_at_impact", "1111111111" },
    { "early_extension", "1111111111" },
    { "over_the_top", "1111111111" },
    { "reverse_spine_p4", "1011111111" },
    { "thin", "1110001101" },
    { "club_short_of_parallel", "1000000000" },
    { "posture_too_upright", "0000000000" },
    { "ball_forward", "0--------0" },
    { "ball_back", "0--------0" },
    { "stance_wide", "0000000000" },
    { "stance_narrow", "0000000000" },
    { "coming_out_of_it", "0000000000" },
    { "reverse_pivot", "0000000000" },
    { "trail_hip_hike", "0000000000" },
    { "lead_knee_drifts_in_at_top", "0000000000" },
    { "pelvis_rise_backswing", "0000000000" },
    { "sway", "0000000000" },
    { "slide", "0000000000" },
    { "hanging_back", "0000000000" },
    { "forward_lunge", "0000000000" },
    { "late_buckle", "0000-0-000" },
    { "flying_elbow", "0000000000" },
    { "chicken_wing", "0010-0-000" },
    { "loss_of_width", "0000000000" },
    { "casting", "0000000000" },
    { "xfactor_deficit", "0000000000" },
    { "transition_rush", "0000000000" },
    { "feet_alignment_open", "0000000000" },
    { "feet_alignment_closed", "0000000000" },
    { "hip_alignment_open", "0000000000" },
    { "hip_alignment_closed", "0000000000" },
    { "excessive_knee_flex", "0000000000" },
    { "insufficient_knee_flex", "0000000000" },
    { "overswing", "0000000000" },
    { "head_drop_backswing", "0001000100" },
    { "head_rise_backswing", "0000000000" },
    { "excessive_head_sway", "0111000100" },
    { "excessive_heel_lift", "0000000000" },
    { "short_backswing", "0001000000" },
    { "disconnection", "0000000000" },
    { "hip_spin_out", "0000000000" },
    { "hip_stall", "0000000000" },
    { "deceleration", "0010000000" },
    { "insufficient_shaft_lean", "0000000000" },
    { "attack_too_steep", "0000000000" },
    { "attack_too_shallow", "0000000000" },
    { "open_face_to_path", "0000000000" },
    { "closed_face_to_path", "0110110010" },
    { "reverse_spine_p7", "0000000000" },
    { "excessive_axis_tilt_impact", "0000000000" },
    { "off_balance_finish", "0000000000" },
    { "pull", "0000000000" },
    { "push", "0000000000" },
    { "launch_low", "0000000000" },
    { "launch_high", "0000000000" },
    { "slice", "0000000000" },
    { "hook", "0000000000" },
    { "pull_hook", "0000000000" },
    { "push_slice", "0000000000" },
    { "chunk", "0000000000" },
    { "top", "0000000000" },
    { "sky", "0000000000" },
    { "shank", "0000000000" },
    { "strike_toe", "0000001000" },
    { "strike_heel", "0000000000" },
    { "smash_deficit", "0000001000" },
    { "spin_excess", "0000000000" },
    { "spin_deficit", "0000000000" },
    { "carry_deficit", "0000000000" },
    { "posture_too_bent", "0000000000" },
    { "diving", "0000000000" },
    { "excessive_axis_tilt_top", "0000000000" },
    { "backing_off_the_ball", "0000000000" },
    { "pelvis_drift_lead_backswing", "0000000000" },
    { "over_rotation_at_top", "0000000000" },
    { "arms_over_connected", "0000000000" },
    { "excessive_lag", "0000000000" },
    { "excessive_shaft_lean", "0000000000" },
    { "hips_too_open_at_impact", "0000000000" },
    { "late_pelvis_rotation", "0000000000" },
    { "excessive_separation_stretch", "0011001001" },
    { "head_drift_lead_backswing", "0000000000" },
    { "hips_under_rotated_at_top", "0000000000" },
    { "shallowing", "0000000000" },
    { "pelvis_thrust_backswing", "0100000111" },
    { "lead_knee_valgus_impact", "0000000000" },
    { "arms_peak_after_impact", "0000000000" },
    { "lie_steep_at_impact", "-111-1111-" },
    { "ball_too_far", "-01000010-" },
    { "sequence_order", "-1-----1--" },
    { "arms_before_chest", "----------" },
    { "club_before_arms", "----------" },
    { "upper_body_starts_down", "----------" },
    { "chest_not_faster_than_hips", "----------" },
    { "arms_ride_the_turn", "----------" },
    { "club_not_released", "----------" },
    { "no_pelvis_braking", "----------" },
    { "steep_backswing_plane", "--00--0-0-" },
    { "flat_backswing_plane", "--00--0-0-" },
    { "steep_downswing_shaft", "--1---1---" },
    { "under_plane_stuck", "--0---0---" },
    { "low_point_behind_ball", "--010-10-0" },
    { "low_point_too_far_ahead", "--101-00-1" },
    { "hips_peak_after_impact", "-0-----1--" },
    { "chest_peak_after_impact", "----------" },
    { "club_peak_after_impact", "----------" },
    { "club_peak_early", "----------" },
    { "ball_too_close", "-00000000-" },
    { "lie_flat_at_impact", "-000-0000-" },
};
const NickPattern kNickPatterns[] = { { "lie_steep_at_impact", 8.999999999999972 }, { "trail_knee_straighten", 3.5896466217439538 }, { "hips_closed_at_impact", 3.455461096783309 }, { "early_extension", 2.610006793508569 }, { "over_the_top", 2.5121290939515077 }, { "reverse_spine_p4", 2.3605980411003418 } };
const NickCond kNickConds[] = {
    { "trail_knee_straighten", FactKind::Fault, 3, "", true, false, true },
    { "hips_closed_at_impact", FactKind::Fault, 3, "pelvis_rotation_impact", true, false, true },
    { "early_extension", FactKind::Fault, 4, "pelvis_thrust", true, false, true },
    { "over_the_top", FactKind::Fault, 4, "hand_path", true, false, true },
    { "reverse_spine_p4", FactKind::Fault, 3, "axis_tilt_top", true, false, true },
    { "thin", FactKind::Outcome, 3, "", false, false, true },
    { "club_short_of_parallel", FactKind::Fault, 2, "shaft_angle_top", true, false, true },
    { "posture_too_upright", FactKind::Setup, 3, "address_hip_hinge", false, false, true },
    { "ball_forward", FactKind::Setup, 2, "ball_position", false, false, true },
    { "ball_back", FactKind::Setup, 2, "ball_position", false, false, true },
    { "stance_wide", FactKind::Setup, 2, "stance_width", false, false, true },
    { "stance_narrow", FactKind::Setup, 2, "stance_width", false, false, true },
    { "coming_out_of_it", FactKind::Fault, 3, "", false, false, true },
    { "reverse_pivot", FactKind::Fault, 3, "", false, false, true },
    { "trail_hip_hike", FactKind::Fault, 2, "hip_line_tilt_top", true, false, true },
    { "lead_knee_drifts_in_at_top", FactKind::Fault, 2, "lead_knee_lateral_top", true, false, true },
    { "pelvis_rise_backswing", FactKind::Fault, 2, "pelvis_height_top", true, false, true },
    { "sway", FactKind::Fault, 3, "pelvis_sway_backswing", true, false, true },
    { "slide", FactKind::Fault, 3, "", true, false, true },
    { "hanging_back", FactKind::Fault, 3, "", true, false, true },
    { "forward_lunge", FactKind::Fault, 2, "", true, false, true },
    { "late_buckle", FactKind::Fault, 1, "", true, false, false },
    { "flying_elbow", FactKind::Fault, 2, "trail_elbow_height", true, false, true },
    { "chicken_wing", FactKind::Fault, 3, "", true, false, true },
    { "loss_of_width", FactKind::Fault, 3, "", true, false, true },
    { "casting", FactKind::Fault, 4, "lag_retention", true, false, true },
    { "xfactor_deficit", FactKind::Fault, 3, "xfactor_stretch", true, false, true },
    { "transition_rush", FactKind::Fault, 3, "", true, false, true },
    { "feet_alignment_open", FactKind::Setup, 3, "feet_alignment", false, false, true },
    { "feet_alignment_closed", FactKind::Setup, 2, "feet_alignment", false, false, true },
    { "hip_alignment_open", FactKind::Setup, 2, "hip_alignment", false, false, true },
    { "hip_alignment_closed", FactKind::Setup, 2, "hip_alignment", false, false, true },
    { "excessive_knee_flex", FactKind::Setup, 1, "knee_flex_address", false, false, false },
    { "insufficient_knee_flex", FactKind::Setup, 2, "knee_flex_address", false, false, false },
    { "overswing", FactKind::Fault, 3, "shaft_angle_top", true, false, true },
    { "head_drop_backswing", FactKind::Fault, 2, "head_height_backswing", true, false, true },
    { "head_rise_backswing", FactKind::Fault, 2, "head_height_backswing", true, false, true },
    { "excessive_head_sway", FactKind::Fault, 3, "head_sway_backswing", true, false, true },
    { "excessive_heel_lift", FactKind::Fault, 2, "", true, false, true },
    { "short_backswing", FactKind::Fault, 3, "thorax_rotation_top", true, false, true },
    { "disconnection", FactKind::Fault, 3, "lead_arm_connection", true, false, true },
    { "hip_spin_out", FactKind::Fault, 2, "pelvis_rotation_early", true, false, true },
    { "hip_stall", FactKind::Fault, 3, "", true, false, true },
    { "deceleration", FactKind::Fault, 2, "", true, false, true },
    { "insufficient_shaft_lean", FactKind::Delivery, 3, "shaft_lean", false, false, true },
    { "attack_too_steep", FactKind::Delivery, 3, "attack_angle", false, false, true },
    { "attack_too_shallow", FactKind::Delivery, 2, "attack_angle", false, false, true },
    { "open_face_to_path", FactKind::Delivery, 4, "face_to_path", false, false, true },
    { "closed_face_to_path", FactKind::Delivery, 2, "face_to_path", false, false, true },
    { "reverse_spine_p7", FactKind::Fault, 3, "axis_tilt_impact", true, false, true },
    { "excessive_axis_tilt_impact", FactKind::Fault, 1, "axis_tilt_impact", true, false, true },
    { "off_balance_finish", FactKind::Fault, 3, "", true, false, true },
    { "pull", FactKind::Outcome, 3, "start_line", true, false, true },
    { "push", FactKind::Outcome, 2, "start_line", true, false, true },
    { "launch_low", FactKind::Outcome, 2, "launch_angle", false, false, true },
    { "launch_high", FactKind::Outcome, 2, "launch_angle", false, false, true },
    { "slice", FactKind::Outcome, 4, "curvature", true, false, true },
    { "hook", FactKind::Outcome, 2, "curvature", true, false, true },
    { "pull_hook", FactKind::Outcome, 2, "", false, false, true },
    { "push_slice", FactKind::Outcome, 3, "", false, false, true },
    { "chunk", FactKind::Outcome, 3, "", true, false, true },
    { "top", FactKind::Outcome, 2, "", false, false, true },
    { "sky", FactKind::Outcome, 1, "", false, false, true },
    { "shank", FactKind::Outcome, 1, "", false, false, true },
    { "strike_toe", FactKind::Outcome, 3, "strike_location", false, false, true },
    { "strike_heel", FactKind::Outcome, 3, "strike_location", true, false, true },
    { "smash_deficit", FactKind::Outcome, 3, "", false, false, true },
    { "spin_excess", FactKind::Outcome, 3, "spin_rate", false, false, true },
    { "spin_deficit", FactKind::Outcome, 1, "spin_rate", false, false, true },
    { "carry_deficit", FactKind::Outcome, 3, "", false, false, true },
    { "posture_too_bent", FactKind::Setup, 2, "address_hip_hinge", false, false, true },
    { "diving", FactKind::Fault, 2, "", true, false, true },
    { "excessive_axis_tilt_top", FactKind::Fault, 1, "axis_tilt_top", true, false, true },
    { "backing_off_the_ball", FactKind::Fault, 1, "pelvis_thrust", true, false, true },
    { "pelvis_drift_lead_backswing", FactKind::Fault, 1, "pelvis_sway_backswing", true, false, true },
    { "over_rotation_at_top", FactKind::Fault, 1, "thorax_rotation_top", true, false, true },
    { "arms_over_connected", FactKind::Fault, 1, "lead_arm_connection", true, false, true },
    { "excessive_lag", FactKind::Fault, 0, "lag_retention", true, false, true },
    { "excessive_shaft_lean", FactKind::Delivery, 1, "shaft_lean", false, false, true },
    { "hips_too_open_at_impact", FactKind::Fault, 1, "pelvis_rotation_impact", true, false, true },
    { "late_pelvis_rotation", FactKind::Fault, 2, "pelvis_rotation_early", true, false, true },
    { "excessive_separation_stretch", FactKind::Fault, 0, "xfactor_stretch", true, false, true },
    { "head_drift_lead_backswing", FactKind::Fault, 1, "head_sway_backswing", true, false, true },
    { "hips_under_rotated_at_top", FactKind::Fault, 3, "pelvis_rotation_top", true, false, true },
    { "shallowing", FactKind::Delivery, 1, "transition_plane", false, false, true },
    { "pelvis_thrust_backswing", FactKind::Fault, 2, "pelvis_thrust_backswing", true, false, true },
    { "lead_knee_valgus_impact", FactKind::Fault, 2, "lead_knee_drift_impact", true, false, true },
    { "arms_peak_after_impact", FactKind::Fault, 3, "", true, false, true },
    { "lie_steep_at_impact", FactKind::Delivery, 3, "shaft_lie", false, false, true },
    { "ball_too_far", FactKind::Setup, 2, "ball_body_distance", false, false, true },
    { "sequence_order", FactKind::Fault, 3, "", true, false, true },
    { "arms_before_chest", FactKind::Fault, 3, "", true, false, true },
    { "club_before_arms", FactKind::Fault, 3, "", true, false, true },
    { "upper_body_starts_down", FactKind::Fault, 3, "", true, false, true },
    { "chest_not_faster_than_hips", FactKind::Fault, 2, "", true, false, true },
    { "arms_ride_the_turn", FactKind::Fault, 2, "", true, false, true },
    { "club_not_released", FactKind::Fault, 2, "", true, false, true },
    { "no_pelvis_braking", FactKind::Fault, 2, "", true, false, true },
    { "steep_backswing_plane", FactKind::Fault, 2, "backswing_plane", true, false, true },
    { "flat_backswing_plane", FactKind::Fault, 2, "backswing_plane", true, false, true },
    { "steep_downswing_shaft", FactKind::Delivery, 3, "delivery_plane", false, false, true },
    { "under_plane_stuck", FactKind::Delivery, 2, "delivery_plane", false, false, true },
    { "low_point_behind_ball", FactKind::Delivery, 3, "low_point", false, false, true },
    { "low_point_too_far_ahead", FactKind::Delivery, 1, "low_point", false, false, true },
    { "hips_peak_after_impact", FactKind::Fault, 3, "", true, false, true },
    { "chest_peak_after_impact", FactKind::Fault, 3, "", true, false, true },
    { "club_peak_after_impact", FactKind::Fault, 3, "", true, false, true },
    { "club_peak_early", FactKind::Fault, 3, "", true, false, true },
    { "ball_too_close", FactKind::Setup, 2, "ball_body_distance", false, false, true },
    { "lie_flat_at_impact", FactKind::Delivery, 1, "shaft_lie", false, false, true },
    { "limited_lead_hip_ir", FactKind::Capacity, 3, "", false, true, false },
    { "poor_pelvic_disassociation", FactKind::Capacity, 3, "", false, true, false },
    { "limited_lead_ankle_dorsiflexion", FactKind::Capacity, 3, "", false, true, false },
    { "s_posture", FactKind::Setup, 3, "lumbar_curve", false, false, false },
    { "limited_thoracic_rotation", FactKind::Capacity, 3, "", false, true, false },
    { "limited_trail_hip_ir", FactKind::Capacity, 3, "", false, true, false },
    { "poor_core_stability", FactKind::Capacity, 3, "", false, true, false },
    { "flat_shoulder_plane", FactKind::Fault, 2, "shoulder_plane", true, false, false },
    { "outside_takeaway", FactKind::Fault, 2, "takeaway_direction", true, false, false },
    { "inside_takeaway", FactKind::Fault, 3, "takeaway_direction", true, false, false },
    { "across_the_line", FactKind::Fault, 2, "top_direction", true, false, false },
    { "scooping", FactKind::Fault, 3, "lead_wrist_impact", true, false, true },
    { "flat_lumbar_spine", FactKind::Setup, 2, "lumbar_curve", false, false, false },
    { "in_to_out_path", FactKind::Delivery, 2, "club_path", false, false, true },
    { "out_to_in_path", FactKind::Delivery, 4, "club_path", false, false, true },
    { "limited_hip_extension", FactKind::Capacity, 3, "", false, true, false },
    { "limited_hamstring_length", FactKind::Capacity, 3, "", false, true, false },
    { "poor_glute_strength", FactKind::Capacity, 3, "", false, true, false },
    { "weight_in_heels_address", FactKind::Setup, 2, "balance_heel_toe_address", false, false, false },
    { "limited_trail_ankle_dorsiflexion", FactKind::Capacity, 3, "", false, true, false },
};
const NickEdge kNickEdges[] = {
    { "early_extension", "coming_out_of_it", 3 },
    { "limited_lead_hip_ir", "early_extension", 3 },
    { "poor_pelvic_disassociation", "early_extension", 3 },
    { "limited_lead_ankle_dorsiflexion", "early_extension", 2 },
    { "ball_too_close", "early_extension", 2 },
    { "posture_too_upright", "early_extension", 1 },
    { "s_posture", "reverse_spine_p4", 3 },
    { "limited_thoracic_rotation", "reverse_spine_p4", 3 },
    { "limited_trail_hip_ir", "reverse_spine_p4", 2 },
    { "poor_core_stability", "reverse_spine_p4", 2 },
    { "over_the_top", "chicken_wing", 3 },
    { "early_extension", "chicken_wing", 2 },
    { "poor_pelvic_disassociation", "over_the_top", 3 },
    { "limited_lead_hip_ir", "over_the_top", 3 },
    { "flat_shoulder_plane", "over_the_top", 2 },
    { "limited_thoracic_rotation", "over_the_top", 2 },
    { "transition_rush", "over_the_top", 2 },
    { "over_the_top", "loss_of_width", 1 },
    { "trail_knee_straighten", "head_rise_backswing", 3 },
    { "outside_takeaway", "over_the_top", 2 },
    { "inside_takeaway", "over_the_top", 2 },
    { "across_the_line", "over_the_top", 2 },
    { "steep_backswing_plane", "over_the_top", 2 },
    { "disconnection", "over_the_top", 2 },
    { "early_extension", "shank", 3 },
    { "early_extension", "strike_heel", 2 },
    { "early_extension", "under_plane_stuck", 2 },
    { "trail_knee_straighten", "sway", 2 },
    { "hips_closed_at_impact", "scooping", 2 },
    { "posture_too_bent", "early_extension", 2 },
    { "flat_lumbar_spine", "reverse_spine_p4", 2 },
    { "hips_closed_at_impact", "in_to_out_path", 2 },
    { "hips_too_open_at_impact", "over_the_top", 2 },
    { "late_pelvis_rotation", "over_the_top", 3 },
    { "late_pelvis_rotation", "hips_closed_at_impact", 3 },
    { "excessive_separation_stretch", "over_the_top", 1 },
    { "over_the_top", "out_to_in_path", 3 },
    { "over_the_top", "attack_too_steep", 2 },
    { "over_the_top", "strike_heel", 2 },
    { "hips_under_rotated_at_top", "over_the_top", 3 },
    { "trail_knee_straighten", "pelvis_rise_backswing", 3 },
    { "early_extension", "reverse_spine_p7", 2 },
    { "limited_trail_hip_ir", "early_extension", 3 },
    { "poor_core_stability", "early_extension", 3 },
    { "limited_hip_extension", "early_extension", 1 },
    { "limited_hamstring_length", "early_extension", 1 },
    { "over_the_top", "early_extension", 2 },
    { "casting", "early_extension", 2 },
    { "hanging_back", "early_extension", 2 },
    { "hanging_back", "over_the_top", 3 },
    { "excessive_knee_flex", "trail_knee_straighten", 3 },
    { "excessive_knee_flex", "early_extension", 2 },
    { "pelvis_thrust_backswing", "early_extension", 2 },
    { "poor_glute_strength", "early_extension", 3 },
    { "weight_in_heels_address", "early_extension", 2 },
    { "limited_trail_ankle_dorsiflexion", "early_extension", 1 },
    { "early_extension", "lie_steep_at_impact", 2 },
    { "lie_steep_at_impact", "pull", 2 },
    { "lie_steep_at_impact", "strike_heel", 2 },
    { "upper_body_starts_down", "over_the_top", 3 },
    { "arms_before_chest", "over_the_top", 2 },
};
const NickRun kNickRuns[] = {
    { "lie_steep_at_impact", "P7", { NaN, 6.5, 7, 8, NaN, 8, 7, 8.499999999999993, 7.999999999999986, NaN }, false, false, 3.5, 2.5, 4.5, 2, 5, "°" },
    { "trail_knee_straighten", "P4", { -18.90800117629965, -23.784305773940844, -16.295295999644708, -18.3683461418682, -21.4706759174352, -22.004755547254035, -21.648525745997347, -21.605083543492242, -19.20507656020476, -23.646459089322207 }, false, true, 0, -12, 0, -18, 0, "°" },
    { "hips_closed_at_impact", "P7", { 5.643165512159556, 3.3949052704382883, 1.1750320341462086, 7.264444372865225, 3.4958727938955305, 2.2841212478748707, 2.3132859657624736, 3.8476774523983472, 3.013295350331602, 8.25071784010312 }, false, false, 38.0, 18, 58, 8, 68, "°" },
    { "early_extension", "P7", { -12.442305502810818, -14.173243432977397, -12.697438970241876, -12.044436452745792, -11.9699143059011, -13.144672951788703, -13.590841053280808, -13.70055185593013, -12.318447566238913, -13.60816461254042 }, false, true, 0, -5, 0, -7.5, 0, "°" },
    { "over_the_top", "P4", { 9.692341146566523, 11.77449346335929, 10.8745939821342, 13.43778823315169, 13.001370791932228, 10.897191498778867, 12.031664623528101, 9.567104609120884, 9.844215356286153, 9.385290628423501 }, true, false, 0, 0, 8.666666666666666, 0, 13, "% hand rise" },
    { "reverse_spine_p4", "P4", { -3.1959136765123306, 2.3513036085066616, -0.6367275643476862, -0.33362644329840835, -5.555546877463192, -0.28241793786008496, -0.05067115942497291, -5.450541411172689, -4.209195539487604, -2.163588246602049 }, false, false, 12.0, 0, 24, -6, 30, "°" },
};

} // namespace

int main()
{
    std::printf("session_report_test\n");

    // ── reportPackOf: the pack's facts, marshalled once ─────────────────────────
    {
        CharacteristicPack pk;
        Measure live;  live.id = QStringLiteral("m_live");  live.status = MeasureStatus::Live;
        Measure plan_; plan_.id = QStringLiteral("m_plan"); plan_.status = MeasureStatus::Planned;
        pk.measures = { live, plan_ };
        Signal sl; sl.id = QStringLiteral("s_live"); sl.measures = { QStringLiteral("m_live") };
        Signal sp; sp.id = QStringLiteral("s_plan"); sp.measures = { QStringLiteral("m_plan") };
        pk.signalDefs = { sl, sp };
        Condition knee; knee.id = QStringLiteral("knee"); knee.kind = ConditionKind::Fault;
        knee.golferWell = QStringLiteral("your trail knee stays flexed"); knee.detectedBy = { QStringLiteral("s_live") };
        knee.prominence = Prominence::Common; knee.axis = QStringLiteral("kneeAxis");
        Condition hip; hip.id = QStringLiteral("hip_ir"); hip.kind = ConditionKind::Capacity;
        hip.observability = Observability::Latent; hip.screenRef = QStringLiteral("screen.hipIr");
        Condition curve; curve.id = QStringLiteral("lumbar"); curve.kind = ConditionKind::Setup;
        curve.detectedBy = { QStringLiteral("s_plan") };
        Condition wing; wing.id = QStringLiteral("wing"); wing.kind = ConditionKind::Fault;
        wing.reportHold = QStringLiteral("dark face-on frames after impact"); wing.state = ConditionState::Retired;
        pk.conditions = { knee, hip, curve, wing };
        Edge c1; c1.from = QStringLiteral("hip_ir"); c1.to = QStringLiteral("knee"); c1.strength = Strength::Strong;
        Edge c2; c2.from = QStringLiteral("lumbar"); c2.to = QStringLiteral("knee"); c2.type = EdgeType::Corroborates;
        pk.edges = { c1, c2 };

        const ReportPack rp = reportPackOf(pk);
        const ReportCondition *k = rp.condition("knee"), *h = rp.condition("hip_ir"), *l = rp.condition("lumbar"),
                              *w = rp.condition("wing");
        check(k && h && l && w && !rp.condition("nope"), "every condition is marshalled, an unknown id is null");
        check(k && k->kind == FactKind::Fault && k->hasGolferWell && k->liveMeasure && k->prominence == int(Prominence::Common)
                  && k->axis == QStringLiteral("kneeAxis") && !k->screenable && !k->held,
              "a live fault: its kind, words, measure status, prominence and axis");
        check(h && h->kind == FactKind::Capacity && h->screenable && !h->liveMeasure,
              "a latent capacity with a screen is screenable");
        check(l && !l->liveMeasure && !l->hasGolferWell, "a planned measure is not live");
        check(w && w->held && w->retired, "reportHold and a retired state are carried");
        check(rp.causes.size() == 1 && rp.causes[0].from == QStringLiteral("hip_ir")
                  && rp.causes[0].strength == int(Strength::Strong),
              "only `causes` edges, with their strength");
    }

    // ── Strengths (§3.2) ────────────────────────────────────────────────────────
    {
        ReportPack p;
        add(p, fact("knee", FactKind::Fault, 3));               // the work-on
        add(p, fact("sway", FactKind::Fault, 3));               // its effect, clean: "it has not spread"
        add(p, fact("headRise", FactKind::Fault, 2));           // its effect too, less prominent
        add(p, fact("squat", FactKind::Setup, 4));              // its CAUSE, clean (ruled out)
        add(p, fact("loner", FactKind::Fault, 4));              // ubiquitous, linked to nothing
        add(p, fact("heel", FactKind::Outcome, 3, "strike"));   // clean, but its axis showed
        add(p, fact("toe", FactKind::Outcome, 3, "strike"));    // ...here: once
        add(p, fact("slice", FactKind::Outcome, 4, "curve"));   // clean, and so was its axis
        add(p, fact("pull", FactKind::Outcome, 2, "start"));
        add(p, fact("mute", FactKind::Fault, 4, "", false));    // no golferWell to say it in
        add(p, fact("face", FactKind::Delivery, 4));            // a delivery number is not a strength
        ReportCondition held = fact("wing", FactKind::Fault, 4); held.held = true; add(p, held);
        add(p, fact("patchy", FactKind::Fault, 4));             // judged on 7 of 10
        add(p, fact("eight", FactKind::Fault, 1));              // judged on 8 of 10
        add(p, fact("once", FactKind::Fault, 4));               // fired once
        cause(p, "knee", "sway");
        cause(p, "knee", "headRise");
        cause(p, "squat", "knee");

        std::vector<int> toeOnce = rep(0, 10); toeOnce[4] = 1;
        std::vector<int> patchy = rep(0, 10); patchy[0] = patchy[1] = patchy[2] = -1;
        std::vector<int> eight = rep(0, 10); eight[0] = eight[1] = -1;
        std::vector<int> once = rep(0, 10); once[9] = 1;
        const Session s = sessionFrom({ plan("knee", rep(1, 10)), plan("sway", rep(0, 10)), plan("headRise", rep(0, 10)),
                                        plan("squat", rep(0, 10)), plan("loner", rep(0, 10)), plan("heel", rep(0, 10)),
                                        plan("toe", toeOnce), plan("slice", rep(0, 10)), plan("pull", rep(0, 10)),
                                        plan("mute", rep(0, 10)), plan("face", rep(0, 10)), plan("wing", rep(0, 10)),
                                        plan("patchy", patchy), plan("eight", eight), plan("once", once) }, p);
        check(idsOf(s.workOns) == QStringList{ "knee" }, "(the fixture's one work-on)");

        const std::vector<StrengthPick> st = pickStrengths(s.ledgers, 10, s.workOns, p);
        check(idsOf(st) == (QStringList{ "slice", "sway", "headRise" }),
              "one outcome then two movements; the work-on's clean effects lead the movements");
        check(st.size() == 3 && st[0].outcome && st[1].effectOfRank == 1 && st[1].judged == 10 && !st[1].outcome,
              "a pick carries what it is, how it was judged, and the work-on it did not spread to");
        check(!idsOf(st).contains("heel"),
              "the whole-axis rule: 'nothing off the heel' is not a strength when the toe showed once (Nick, 20 Oct)");

        // Every eligibility rule, on the full ranked list (cap the movements away by asking for more).
        ReportPack noLinks = p;
        noLinks.causes.clear();
        const std::vector<StrengthPick> flat = pickStrengths(s.ledgers, 10, s.workOns, noLinks);
        check(idsOf(flat) == (QStringList{ "slice", "loner", "squat" }),
              "unlinked: prominence decides, ubiquitous first; a clean setup condition counts as a movement");
        // Show the eligible set itself through a session with no work-ons and a cap that cannot bite.
        QStringList eligible;
        for (const ConditionLedger &l : s.ledgers) {
            std::vector<ConditionLedger> one{ l };
            if (!pickStrengths(one, 10, SessionWorkOns{}, noLinks).empty()) eligible << l.id;
        }
        check(eligible.contains("eight") && !eligible.contains("patchy"),
              "judged on 8 of 10 qualifies (80 %), 7 of 10 does not");
        check(!eligible.contains("once") && !eligible.contains("knee"), "a condition that showed even once is not a strength");
        check(!eligible.contains("mute") && !eligible.contains("face") && !eligible.contains("wing"),
              "no words, a delivery number, or a held reading: never a strength");
        check(eligible.contains("heel") && !pickStrengths(s.ledgers, 10, s.workOns, p).empty(),
              "…the heel alone would qualify: it is the axis that rules it out");

        // The cause of a work-on, clean, ranks above an unlinked one however prominent.
        const std::vector<StrengthPick> causeFirst = pickStrengths(
            { *ledgerFor(s.ledgers, "loner"), *ledgerFor(s.ledgers, "squat"), *ledgerFor(s.ledgers, "eight") }, 10, s.workOns, p);
        check(idsOf(causeFirst) == (QStringList{ "squat", "loner", "eight" }) && causeFirst[0].causeOfWorkOn,
              "a work-on's ruled-out cause ranks above an unlinked fault");

        // Fill: no outcome measured (no launch monitor) — three movements; only outcomes — three outcomes.
        const std::vector<StrengthPick> noBall = pickStrengths(
            { *ledgerFor(s.ledgers, "sway"), *ledgerFor(s.ledgers, "headRise"), *ledgerFor(s.ledgers, "loner"),
              *ledgerFor(s.ledgers, "eight") }, 10, s.workOns, p);
        check(idsOf(noBall) == (QStringList{ "sway", "headRise", "loner" }), "no outcome: movements fill to three");
        const std::vector<StrengthPick> onlyBall = pickStrengths(
            { *ledgerFor(s.ledgers, "slice"), *ledgerFor(s.ledgers, "pull"), *ledgerFor(s.ledgers, "heel") }, 10, s.workOns, p);
        check(idsOf(onlyBall) == (QStringList{ "slice", "heel", "pull" }), "no movement: outcomes fill to three, prominence first");

        // Jess's seven swings: 80 % is 5.6, so six judged swings qualify and five do not.
        const Session j = sessionFrom({ plan("knee", rep(1, 7)), plan("sway", { 0, 0, 0, 0, 0, 0, -1 }),
                                        plan("loner", { 0, 0, 0, 0, 0, -1, -1 }) }, p);
        check(idsOf(pickStrengths(j.ledgers, 7, j.workOns, p)) == QStringList{ "sway" },
              "seven swings: six judged is enough, five is not");
        // Three swings: never fewer than three judged.
        const Session t = sessionFrom({ plan("sway", rep(0, 3)), plan("loner", { 0, 0, -1 }) }, p);
        check(idsOf(pickStrengths(t.ledgers, 3, t.workOns, p)) == QStringList{ "sway" },
              "never fewer than three judged swings");

        // Well within the corridor, not just clean (Mark, 22 Oct, Jess's report): a condition that never
        // fired but read out at |z| 1.8 on every swing — the far side of a two-sided corridor, or close
        // to its own line — is not something going well. One at 0.9 still is.
        {
            const Session w = sessionFrom({ plan("sway", rep(0, 10), 3.0, -1.8), plan("loner", rep(0, 10), 3.0, 0.9) }, p);
            check(idsOf(pickStrengths(w.ledgers, 10, w.workOns, p)) == QStringList{ "loner" },
                  "clean but out at |z| 1.8 is not a strength; clean at 0.9 is");
            const Session nc = sessionFrom({ plan("sway", rep(0, 10)) }, p);
            std::vector<ConditionLedger> noCorridor = nc.ledgers;
            for (ConditionLedger &l : noCorridor) { l.medianExcess = std::numeric_limits<double>::quiet_NaN(); l.excessN = 0; }
            check(pickStrengths(noCorridor, 10, nc.workOns, p).empty(), "a reading with no corridor is never a strength");
        }

        // Session-scope ledgers have no swing to be clean on.
        std::vector<ConditionLedger> scoped{ *ledgerFor(s.ledgers, "sway") };
        scoped[0].sessionScope = true;
        check(pickStrengths(scoped, 10, s.workOns, p).empty(), "a session-level reading is never a strength");
    }

    // ── The reference swing (§6) ────────────────────────────────────────────────
    {
        const SpreadCorridor two = twoSided(0.0, 10.0, -5.0, 15.0);       // width 10
        const SpreadCorridor flr = floorAt(0.0, -12.0, -18.0);            // pass from −12 up (width 12)
        const SpreadCorridor ceil = ceilingAt(0.0, 8.0, 13.0);            // pass up to 8 (width 8)
        const std::vector<HabitRun> hs{
            run("a", { 12.0, 11.0, 10.0, 10.0 }, two),        // misses 0.2, 0.1, 0, 0
            run("b", { -18.0, -12.0, -15.0, -12.5 }, flr),    // misses 0.5, 0, 0.25, 1/24
            run("c", { 4.0, 12.0, -30.0, NaN }, ceil),        // misses 0, 0.5, 0 (open side), —
        };
        const ReferenceSwing r = referenceSwing(hs);
        // Swing 4 (index 3) has the smallest raw sum (0.04) but was not judged on c, so it does not
        // compete — counted as zero there, an unmeasured swing would always win. Of the rest:
        // 0.7, 0.6, 0.25 → swing 3.
        check(r.index == 2 && near(r.miss, 0.25, 1e-9) && r.judgedOn == 3,
              "the smallest summed miss, in range widths, among the swings judged on every habit");
        check(r.inside == (QStringList{ "a", "c" }),
              "a reading on a ceiling's open side is no miss: swing 3 is inside on a and c");
        check(!r.longest, "no carries given: never 'your longest'");

        const ReferenceSwing withCarry = referenceSwing(hs, { 140.0, 150.0, 151.0, 160.0 });
        check(withCarry.index == 2 && !withCarry.longest,
              "the session's longest carry (swing 4) is not a candidate, so swing 3 is not 'your longest'");
        const std::vector<HabitRun> tie{ run("a", { 5.0, 5.0, 12.0 }, two) };
        const ReferenceSwing t = referenceSwing(tie, { 140.0, 152.0, 160.0 });
        check(t.index == 1 && !t.longest, "a tie goes to the longer carry; not the longest of the session, so not said");
        const ReferenceSwing t2 = referenceSwing(tie, { 150.0, 152.0, 151.0 });
        check(t2.index == 1 && t2.longest, "...and 'your longest' only when it was");
        check(referenceSwing(tie).index == 0, "no carries: the earlier swing on a tie");
        check(referenceSwing({}).index == -1 && referenceSwing({ run("x", { NaN, NaN }, two) }).index == -1,
              "nothing judged: no reference swing");
        check(near(missInWidths(passBandOf(flr), -18.0), 0.5, 1e-12) && near(missInWidths(passBandOf(flr), 40.0), 0.0, 1e-12)
              && std::isnan(missInWidths(passBandOf(SpreadCorridor{}), 1.0)),
              "missInWidths: past the pass edge over its width; the open side is 0; no corridor is NaN");
    }

    // ── Which still (§6) ────────────────────────────────────────────────────────
    {
        const SpreadCorridor two = twoSided(0.0, 10.0, -5.0, 15.0);
        // values: median 13 → typical is swing index 2 (13); closest 9 (index 0), furthest 20 (index 4).
        const HabitRun h = run("knee", { 9.0, 12.0, 13.0, NaN, 20.0, 14.0 }, two);
        check(typicalSwing(h) == 2, "the typical swing is nearest the median");

        const StillChoice ch = chooseStill(h, "change", "P4", 1, true);
        check(ch.kind == StillKind::Change && ch.first == 2 && ch.second == 2 && ch.firstPhase == QStringLiteral("P1")
                  && ch.secondPhase == QStringLiteral("P4") && ch.addressLine,
              "change: the typical swing at address and at the position, the address line drawn");
        const StillChoice pr = chooseStill(h, "pair", "P7", 2, true);
        check(pr.kind == StillKind::Pair && pr.first == 0 && pr.second == 4 && pr.firstPhase == QStringLiteral("P7"),
              "pair: closest and furthest on the measure drawn, when they differ by half the range or more");
        const HabitRun alike = run("hips", { 11.0, 12.0, 13.0, 14.0 }, two);   // 11 vs 14: 3 < 5
        const StillChoice fb = chooseStill(alike, "pair", "P7", 2, true);
        check(fb.kind == StillKind::Typical && fb.fellBack && fb.first == 1,
              "a pair too alike to see falls back to the typical swing ('every swing looked much like this one')");
        check(chooseStill(h, "", "P7", 1, true).kind == StillKind::Pair,
              "no picture authored: the evidence chooses, pair first");
        check(chooseStill(h, "typical", "P7", 3, true).kind == StillKind::Typical
                  && !chooseStill(h, "typical", "P7", 3, true).fellBack,
              "typical when asked");
        check(chooseStill(h, "change", "P1", 1, true).kind == StillKind::Typical, "a change 'at address' is just the typical swing");
        check(chooseStill(h, "pair", "P7", 4, true).kind == StillKind::None, "work-ons 4 and 5 have no stills");
        check(chooseStill(h, "pair", "P7", 1, false).kind == StillKind::None, "no photos (photo rule): no stills anywhere");
        check(chooseStill(h, "none", "P7", 1, true).kind == StillKind::None
                  && chooseStill(h, "pair", "", 1, true).kind == StillKind::None,
              "kind none, or no position: no still");
        check(chooseStill(run("x", { NaN, NaN }, two), "pair", "P7", 1, true).kind == StillKind::None,
              "no swing judged: no still");
        check(chooseStill(run("x", { 1.0, 30.0 }, SpreadCorridor{}), "pair", "P7", 1, true).kind == StillKind::Typical,
              "no range to measure closeness against: the typical swing");
    }

    // ── Range words and magnitude words (§10 rule 5) ────────────────────────────
    {
        check(rangeWords(twoSided(5.0, 25.0, 0.0, 30.0), QStringLiteral("mm")) == QStringLiteral("5 to 25 mm"),
              "two-sided: '5 to 25 mm'");
        check(rangeWords(twoSided(-5.0, 10.0, -10.0, 15.0), QStringLiteral("°")) == QString::fromUtf8("−5° to 10°"),
              "degrees ride on both numbers, with the true minus");
        check(rangeWords(ceilingAt(0.0, 8.666666, 13.0), QStringLiteral("% hand rise")) == QStringLiteral("8.7 % hand rise or less"),
              "a ceiling: 'X or less', decimals off the corridor's span");
        check(rangeWords(floorAt(0.0, -12.0, -18.0), QStringLiteral("°")) == QString::fromUtf8("−12° or more"),
              "a floor in the raw sign: 'X or more'");
        check(rangeWords(floorAt(0.0, -12.0, -18.0), QStringLiteral("°"), -1) == QStringLiteral("12° or less"),
              "golferSign −1 mirrors it: 'trail-knee bend lost by the top: 12° or less'");
        check(rangeWords(twoSided(-5.0, 10.0, -10.0, 15.0), QStringLiteral("°"), -1) == QString::fromUtf8("−10° to 5°"),
              "…and a two-sided range reads in the words' direction");
        check(rangeWords(SpreadCorridor{}, QStringLiteral("°")).isEmpty(), "no range: no words");
        check(golferValueWords(-22.4, QStringLiteral("°"), 0, -1) == QStringLiteral("22°")
                  && golferValueWords(NaN, QStringLiteral("°"), 0) == QStringLiteral("-"),
              "a value in the golfer's sign; a missing value is '-'");

        const SpreadCorridor rs = twoSided(0.0, 24.0, -6.0, 30.0);   // Nick's reverse spine: 0° to 24°, fault outside −6 to 30
        const MissMagnitude small = missMagnitude(run("reverse_spine_p4", { -3.2, 2.4, -0.6, -0.3, -5.6, -0.3, -0.1, -5.5, -4.2, -2.2 }, rs));
        check(small.size == MissSize::Small && small.outside == 9 && small.pastFault == 0 && small.judged == 10,
              "Nick's reverse spine: 9 of 10 just outside, none past the fault line");
        check(missWords(small) == QStringLiteral("a small miss: just outside the range, never far"), "…'a small miss'");
        const MissMagnitude some = missMagnitude(run("x", { -1.0, -2.0, -3.0, -7.0, 5.0 }, rs));
        check(some.size == MissSize::Some && missWords(some) == QStringLiteral("mostly a small miss, but well past the range on 1 of 5 swings"),
              "fewer than half past the fault line: mostly small, with the count");
        const MissMagnitude clear = missMagnitude(run("x", { -7.0, -8.0, -9.0, NaN }, rs));
        check(clear.size == MissSize::Clear && missWords(clear) == QStringLiteral("a clear miss: well past the range on all 3 swings"),
              "past the fault line on every judged swing: a clear miss, 'all 3 swings'");
        check(missMagnitude(run("x", { 1.0, 2.0 }, rs)).size == MissSize::Inside, "inside the range on this measure: Inside");
        check(missMagnitude(run("x", { 1.0 }, SpreadCorridor{})).size == MissSize::Unknown
                  && missWords(missMagnitude(run("x", { NaN }, rs))).isEmpty(),
              "no range or no reading: nothing said about size");
        const MissMagnitude open = missMagnitude(run("x", { -40.0, 9.0, 14.0 }, ceilingAt(0.0, 8.0, 13.0)));
        check(open.outside == 2 && open.pastFault == 1 && open.size == MissSize::Clear,
              "the open side of a ceiling is neither outside nor past the fault line");
    }

    // ── What came with it (§3) ──────────────────────────────────────────────────
    {
        ReportPack p;
        add(p, fact("knee", FactKind::Fault));
        add(p, fact("sway", FactKind::Fault));
        add(p, fact("lift", FactKind::Fault));
        add(p, fact("toeUp", FactKind::Delivery));
        add(p, fact("now", FactKind::Fault));
        add(p, fact("few", FactKind::Fault));
        add(p, fact("other", FactKind::Fault));
        ReportCondition held = fact("wing", FactKind::Fault); held.held = true; add(p, held);
        cause(p, "knee", "sway", 2);
        cause(p, "knee", "lift", 3);
        cause(p, "knee", "toeUp", 2);
        cause(p, "knee", "now", 3);
        cause(p, "knee", "few", 3);
        cause(p, "knee", "other", 3);       // a work-on itself
        cause(p, "knee", "wing", 3);
        cause(p, "knee", "absent", 3);      // no row in the session at all
        const Session s = sessionFrom({ plan("knee", rep(1, 10), 4.0), plan("other", rep(1, 10)),
                                        plan("sway", rep(0, 10)), plan("lift", rep(0, 10)),
                                        plan("toeUp", { 1, 1, 1, 1, 0, 0, 0, 0, -1, -1 }),
                                        plan("now", { 1, 0, 0, 1, 0, 0, 0, 0, 0, 0 }), plan("few", { 0, 0, -1, -1, -1, -1, -1, -1, -1, -1 }),
                                        plan("wing", rep(1, 10)) }, p);
        const std::vector<CameWithItem> cw = cameWith("knee", s.ledgers, s.workOns, p);
        check(idsOf(cw) == (QStringList{ "toeUp", "lift", "sway", "now" }),
              "showed first, then clean (strongest edge first), then now-and-then; the work-on, the held, the unjudged and the absent are left out");
        check(cw.size() == 4 && cw[0].state == CameWithState::Showed && cw[0].fired == 4 && cw[0].judged == 8
                  && cw[1].state == CameWithState::Clean && cw[1].hasGolferWell && cw[3].state == CameWithState::Sometimes,
              "4 of 8 (half) showed; 0 of 10 is clean; 2 of 10 is only now and then");
        check(cameWith("sway", s.ledgers, s.workOns, p).empty(), "no authored effects: nothing came with it");
    }

    // ── What might be behind it (§3.3) ──────────────────────────────────────────
    {
        ReportPack p;
        add(p, fact("ee", FactKind::Fault));
        add(p, fact("ott", FactKind::Fault));                     // a session habit, on every swing
        ReportCondition hipIr = fact("hipIr", FactKind::Capacity); hipIr.screenable = true; hipIr.liveMeasure = false; add(p, hipIr);
        ReportCondition lumbar = fact("lumbar", FactKind::Setup); lumbar.liveMeasure = false; add(p, lumbar);
        add(p, fact("hidden", FactKind::Fault));                  // measurable, judged on 2
        ReportCondition plannedJudged = fact("plannedJudged", FactKind::Fault); plannedJudged.liveMeasure = false;
        add(p, plannedJudged);                                    // judged on 10 though its status says planned
        add(p, fact("fine", FactKind::Fault));
        add(p, fact("someTimes", FactKind::Fault));
        ReportCondition held = fact("held", FactKind::Fault); held.held = true; add(p, held);
        ReportCondition gone = fact("gone", FactKind::Fault); gone.retired = true; add(p, gone);
        cause(p, "lumbar", "ee", 1);
        cause(p, "ott", "ee", 2);
        cause(p, "hipIr", "ee", 3);
        cause(p, "hidden", "ee", 2);
        cause(p, "plannedJudged", "ee", 2);
        cause(p, "fine", "ee", 3);
        cause(p, "someTimes", "ee", 1);
        cause(p, "held", "ee", 2);
        cause(p, "gone", "ee", 4);
        std::vector<int> two = rep(-1, 10); two[0] = two[1] = 0;
        std::vector<int> some = rep(0, 10); some[3] = some[6] = 1;
        const Session s = sessionFrom({ plan("ee", rep(1, 10)), plan("ott", rep(1, 10)), plan("hidden", two),
                                        plan("plannedJudged", rep(0, 10)), plan("fine", rep(0, 10)),
                                        plan("someTimes", some), plan("held", rep(0, 10)) }, p);
        const CauseGroups g = causeGroups("ee", s.ledgers, s.workOns, p);
        check(idsOf(g.all) == (QStringList{ "hipIr", "fine", "ott", "hidden", "plannedJudged", "held", "lumbar", "someTimes" }),
              "every authored cause, strongest edge first then pack order; a retired condition is no longer a claim");
        check(idsOf(g.spotted) == QStringList{ "ott" } && g.spotted[0].workOnRank > 0 && !g.spotted[0].supported
                  && g.spotted[0].everySwing && g.spottedEverySwing && !g.spottedSupported,
              "spotted: a session habit, with its work-on number; on every swing, so not supported");
        check(idsOf(g.couldBe) == (QStringList{ "hipIr", "hidden", "held", "lumbar" })
                  && g.couldBe[0].check == CouldBeCheck::PhysicalTest && g.couldBe[1].check == CouldBeCheck::HiddenToday
                  && g.couldBe[2].check == CouldBeCheck::CoachCanLook && g.couldBe[3].check == CouldBeCheck::CoachCanLook,
              "could be: a screen → physical test; measurable but judged on two → hidden today; held or not measurable → coach can look");
        check(g.couldBeOrder == (std::vector<CouldBeCheck>{ CouldBeCheck::PhysicalTest, CouldBeCheck::HiddenToday, CouldBeCheck::CoachCanLook }),
              "subgroups print in the order of their likeliest cause");
        check(idsOf(g.ruledOut) == (QStringList{ "fine", "plannedJudged", "someTimes" }),
              "ruled out: judged on three or more and not a habit — whatever the measure's status says");
        check(g.lead == CauseLead::Spotted && g.couldBeMore == 0 && g.ruledOutMore == 0, "lead: one possible cause showed up");

        // A testable, supported spotted cause says the swings point to it.
        ReportPack q;
        add(q, fact("sym", FactKind::Fault)); add(q, fact("cz", FactKind::Fault));
        cause(q, "cz", "sym");
        std::vector<int> together = rep(1, 10); for (int i = 0; i < 3; ++i) together.push_back(0);
        const Session t = sessionFrom({ plan("sym", together), plan("cz", together) }, q);
        const CauseGroups gt = causeGroups("sym", t.ledgers, t.workOns, q);
        check(gt.lead == CauseLead::Spotted && gt.spottedSupported && !gt.spottedEverySwing && gt.spotted[0].supported,
              "a spotted cause the session supports: the swings point to it");
        // …unless its two ends are measured from the same readings (posture_metrics_design.md §6.5): then the
        // link is never tested, so it is never "supported" — the panel's cap, in the report.
        ReportPack qs = q;
        for (ReportCause &c : qs.causes) c.sharedEvidence = true;
        const CauseGroups gs = causeGroups("sym", t.ledgers, t.workOns, qs);
        check(gs.lead == CauseLead::Spotted && !gs.spottedSupported && !gs.spotted[0].supported,
              "a shared-evidence cause is spotted but never 'supported', however clean its 2x2");

        // The other three leads.
        check(causeGroups("hidden", s.ledgers, s.workOns, p).lead == CauseLead::NoneAuthored,
              "no authored cause: 'we don't know of a cause for this one yet'");
        ReportPack r;
        add(r, fact("k", FactKind::Fault)); add(r, fact("squat", FactKind::Setup));
        cause(r, "squat", "k");
        const Session ro = sessionFrom({ plan("k", rep(1, 10)), plan("squat", rep(0, 10)) }, r);
        check(causeGroups("k", ro.ledgers, ro.workOns, r).lead == CauseLead::AllRuledOut,
              "every authored cause judged and clean: 'we checked every cause we know of' (Nick's work-ons 1 and 2)");
        ReportPack u;
        add(u, fact("k", FactKind::Fault));
        ReportCondition cap = fact("cap", FactKind::Capacity); cap.screenable = true; add(u, cap);
        cause(u, "cap", "k");
        const Session uo = sessionFrom({ plan("k", rep(1, 10)) }, u);
        check(causeGroups("k", uo.ledgers, uo.workOns, u).lead == CauseLead::Unchecked,
              "nothing spotted, something unchecked: 'we couldn't pin down what drives this one'");

        // Caps: could-be 6 then "N more, less likely"; ruled out 4 then "N more".
        ReportPack c;
        add(c, fact("h", FactKind::Fault));
        std::vector<Plan> plans{ plan("h", rep(1, 10)) };
        static const char *const capIds[] = { "c0", "c1", "c2", "c3", "c4", "c5", "c6", "c7", "c8" };
        static const char *const outIds[] = { "o0", "o1", "o2", "o3", "o4", "o5", "o6" };
        for (int i = 0; i < 9; ++i) {
            ReportCondition x = fact(capIds[i], FactKind::Capacity); x.screenable = true; add(c, x);
            cause(c, capIds[i], "h", i < 3 ? 3 : 1);
        }
        for (int i = 0; i < 7; ++i) { add(c, fact(outIds[i], FactKind::Fault)); cause(c, outIds[i], "h", 2); plans.push_back(plan(outIds[i], rep(0, 10))); }
        const Session cs = sessionFrom(plans, c);
        const CauseGroups cg = causeGroups("h", cs.ledgers, cs.workOns, c);
        check(cg.couldBe.size() == 6 && cg.couldBeMore == 3 && cg.ruledOut.size() == 4 && cg.ruledOutMore == 3
                  && cg.all.size() == 16 && cg.lead == CauseLead::Unchecked,
              "caps: six could-be then 3 more; four ruled out then 3 more; the full list kept");
        check(cg.couldBe[0].id == QStringLiteral("c0") && cg.couldBe[2].id == QStringLiteral("c2"),
              "…the strongest edges are the ones kept");
    }

    // ── Worth checking next time (page 3) ───────────────────────────────────────
    {
        ReportPack p;
        add(p, fact("w1", FactKind::Fault)); add(p, fact("w2", FactKind::Fault)); add(p, fact("w3", FactKind::Fault));
        auto screen = [&](const char *id) { ReportCondition x = fact(id, FactKind::Capacity); x.screenable = true; add(p, x); };
        auto planned = [&](const char *id) { ReportCondition x = fact(id, FactKind::Setup); x.liveMeasure = false; add(p, x); };
        screen("leadHip"); screen("trailHip"); screen("core"); screen("tspine"); screen("glute"); screen("ankle");
        planned("flatBack"); planned("across");
        add(p, fact("unseen", FactKind::Fault));                 // measurable, never judged
        cause(p, "leadHip", "w2", 2); cause(p, "leadHip", "w3", 2);
        cause(p, "trailHip", "w1", 3); cause(p, "trailHip", "w3", 2);
        cause(p, "core", "w1", 1); cause(p, "core", "w2", 1); cause(p, "core", "w3", 1);
        cause(p, "tspine", "w2", 3);
        cause(p, "glute", "w1", 1);
        cause(p, "ankle", "w3", 1);
        cause(p, "flatBack", "w3", 2); cause(p, "across", "w2", 2); cause(p, "across", "w1", 2);
        cause(p, "unseen", "w1", 2);
        const Session s = sessionFrom({ plan("w1", rep(1, 10), 5.0), plan("w2", rep(1, 10), 4.0), plan("w3", rep(1, 10), 3.0) }, p);
        const std::vector<WorthCheckingGroup> wc = worthChecking(s.workOns, s.ledgers, p);
        check(wc.size() == 3 && wc[0].check == CouldBeCheck::PhysicalTest && wc[1].check == CouldBeCheck::CoachCanLook
                  && wc[2].check == CouldBeCheck::HiddenToday,
              "grouped by how to check: physical tests, then the coach can look, then hidden today");
        if (wc.size() == 3) {
            QStringList t; for (const WorthCheck &x : wc[0].items) t << x.id;
            check(t == (QStringList{ "core", "trailHip", "leadHip", "tspine", "ankle" }) && wc[0].more == 1,
                  "behind the most work-ons first, then the strongest edge, then id; five, then one more");
            check(wc[0].items[0].ranks == (QVector<int>{ 1, 2, 3 }) && wc[0].items[1].ranks == (QVector<int>{ 1, 3 }),
                  "each names the work-ons it could sit behind");
            check(wc[1].items.size() == 2 && wc[1].items[0].id == QStringLiteral("across") && wc[2].items[0].id == QStringLiteral("unseen"),
                  "…the coach-can-look and hidden-today groups the same way");
        }
        ReportPack none;
        add(none, fact("w1", FactKind::Fault));
        const Session n = sessionFrom({ plan("w1", rep(1, 10)) }, none);
        check(worthChecking(n.workOns, n.ledgers, none).empty(), "nothing unchecked: no worth-checking block (empty means absent)");
    }

    // ── The swing timeline (page 3) ─────────────────────────────────────────────
    {
        check(near(timelineStop(1.0), 0.0, 1e-12) && near(timelineStop(2.0), 1.0, 1e-12) && near(timelineStop(3.0), 1.0, 1e-12)
              && near(timelineStop(4.0), 2.0, 1e-12) && near(timelineStop(4.5), 2.5, 1e-12) && near(timelineStop(6.0), 3.0, 1e-12)
              && near(timelineStop(7.0), 4.0, 1e-12) && near(timelineStop(8.0), 5.0, 1e-12) && near(timelineStop(10.0), 5.0, 1e-12),
              "P-positions onto the six stops: address, backswing, top, coming down, impact, finish");
        SessionWorkOns w;
        auto entry = [](const char *id, int rank, double pos) {
            WorkOnEntry e; e.id = QString::fromLatin1(id); e.rank = rank; e.reasons.swingPos = pos; return e;
        };
        w.entries = { entry("stance", 1, 1.0), entry("dip", 2, 4.0), entry("spine", 3, 4.0), entry("wing", 4, 8.0),
                      entry("seq", 5, kWorkOnSwingPosSession) };
        const Timeline t = workOnTimeline(w);
        check(t.marks.size() == 4 && t.sessionLevel == 1 && t.firstIsEarliest, "a session-level habit has no place; work-on 1 is earliest");
        check(t.marks.size() == 4 && t.marks[1].lane == 0 && t.marks[2].lane == 1 && near(t.marks[3].stop, 5.0, 1e-12),
              "two at the top take two lanes");
        w.entries[0].reasons.swingPos = 7.0;
        check(!workOnTimeline(w).firstIsEarliest, "work-on 1 at impact is not the earliest (v3's case: 'your coach may start earlier')");
    }

    // ── The photo rule (§7.3) ───────────────────────────────────────────────────
    {
        const QDate session(2026, 10, 20);
        const PhotoDecision adult = photoRule(QDate(1980, 5, 1), session, false);
        check(adult.included && adult.reason == PhotoReason::Adult, "an adult: photos");
        const PhotoDecision ok = photoRule(QDate(2012, 3, 3), session, true);
        check(ok.included && ok.reason == PhotoReason::JuniorApproved, "a junior with approval: photos");
        const PhotoDecision no = photoRule(QDate(2012, 3, 3), session, false);
        check(!no.included && no.reason == PhotoReason::JuniorNotApproved, "a junior without approval: none");
        const PhotoDecision nodob = photoRule(QDate(), session, false);
        check(nodob.included && nodob.reason == PhotoReason::NoDateOfBirth, "no date of birth: treated as an adult, with the note");
        check(photoRule(QDate(2008, 10, 20), session, false).reason == PhotoReason::Adult
                  && photoRule(QDate(2008, 10, 21), session, false).reason == PhotoReason::JuniorNotApproved,
              "18 on the session day itself is an adult; the day before the birthday is not");
        check(!isJuniorOn(QDate(2008, 2, 29), QDate(2026, 2, 28)) && isJuniorOn(QDate(2008, 2, 29), QDate(2026, 2, 27)),
              "a 29 February birthday turns over on 28 February");
        check(photoRule(QDate(2012, 3, 3), QDate(2031, 1, 1), false).reason == PhotoReason::Adult,
              "age on the SESSION date: an old report stays as it was, a later session's changes");
    }

    // ── Why this order (§4.2) ───────────────────────────────────────────────────
    {
        // v4 in force: no link testable.
        ReportPack p;
        for (const char *id : { "wing", "stance", "dip", "spine" }) add(p, fact(id, FactKind::Fault));
        add(p, fact("toeUp", FactKind::Delivery));
        cause(p, "stance", "wing");
        const Session s = sessionFrom({ plan("wing", rep(1, 10), 8.0), plan("stance", rep(1, 10), 2.5),
                                        plan("dip", rep(1, 10), 3.0), plan("spine", rep(1, 10), 4.0) }, p,
                                      { { "wing", 8.0 }, { "stance", 1.0 }, { "dip", 4.0 }, { "spine", 4.0 } });
        const OrderExplanation x = explainOrder(s.workOns, s.ledgers, p);
        check(!x.linksTestable && x.whyNot == LinksUntestable::CauseOnEverySwing,
              "rule 2 could not run: the one authored cause showed on every swing");
        check(x.steps.size() == 4 && x.steps[0].decidedBy == OrderRule::First && x.steps[1].decidedBy == OrderRule::SwingOrder
                  && x.steps[2].decidedBy == OrderRule::BiggestMiss && x.steps[3].decidedBy == OrderRule::SwingOrder,
              "each place says which rule set it: swing order between positions, the miss within one");
        check(x.bySwingOrder == 2 && x.byMagnitude == 1 && x.namedLinks == 1 && x.behindCause == 0 && x.resolving == 0,
              "the counts the 'Today' lines are built from");

        // No authored links among the habits / a varying cause on too few swings.
        ReportPack bare;
        for (const char *id : { "a", "b" }) add(bare, fact(id, FactKind::Fault));
        const Session b = sessionFrom({ plan("a", rep(1, 10)), plan("b", rep(1, 10)) }, bare);
        check(explainOrder(b.workOns, b.ledgers, bare).whyNot == LinksUntestable::NoLinks, "no authored links: NoLinks");
        ReportPack fewP = bare;
        cause(fewP, "a", "b");
        const Session f = sessionFrom({ plan("a", { 1, 1, 1, 1, 1, 1, 0 }), plan("b", rep(1, 7)) }, fewP);
        check(explainOrder(f.workOns, f.ledgers, fewP).whyNot == LinksUntestable::TooFewSwings,
              "a cause that varied, on seven swings: too few to compare");

        // Testable, with a supported link, a resolving entry and a delivery filler.
        ReportPack q;
        for (const char *id : { "cause", "symptom", "fixed" }) add(q, fact(id, FactKind::Fault));
        add(q, fact("toeUp", FactKind::Delivery));
        cause(q, "cause", "symptom");
        std::vector<int> together = rep(1, 10); for (int i = 0; i < 3; ++i) together.push_back(0);
        std::vector<int> fixed = rep(1, 8); for (int i = 0; i < 5; ++i) fixed.push_back(0);
        const Session t = sessionFrom({ plan("symptom", together, 6.0), plan("cause", together, 2.5),
                                        plan("fixed", fixed, 9.0) }, q);
        check(idsOf(t.workOns) == (QStringList{ "cause", "symptom", "fixed" }), "(the testable fixture's order)");
        const OrderExplanation y = explainOrder(t.workOns, t.ledgers, q);
        check(y.linksTestable && y.whyNot == LinksUntestable::Testable && y.steps.size() == 3
                  && y.steps[1].decidedBy == OrderRule::CauseFirst && y.steps[2].decidedBy == OrderRule::StillHappening
                  && y.behindCause == 1 && y.resolving == 1,
              "testable: the symptom waits behind its cause; the fault fixed in the session goes last");
        const Session d = sessionFrom({ plan("cause", rep(1, 10), 5.0), plan("toeUp", rep(1, 10), 9.0) }, q);
        const OrderExplanation z = explainOrder(d.workOns, d.ledgers, q);
        check(z.steps.size() == 2 && z.steps[1].decidedBy == OrderRule::MovementsFirst,
              "a delivery number filling a short list sits behind the movements, whatever its size");
    }

    // ── Nick Henn, 20 October 2026, session 3 — against the mock's report.json ──
    {
        ReportPack p;
        for (const NickCond &c : kNickConds) {
            ReportCondition r;
            r.id = QString::fromLatin1(c.id);
            r.kind = c.kind;
            r.prominence = c.prominence;
            r.axis = QString::fromLatin1(c.axis);
            r.hasGolferWell = c.well;
            r.screenable = c.screen;
            r.liveMeasure = c.live;
            p.conditions.insert(r.id, r);
        }
        for (const NickEdge &e : kNickEdges) p.causes.push_back({ QString::fromLatin1(e.from), QString::fromLatin1(e.to), e.strength });

        // The session's swings, every condition's verdict on each, firing at its card's excess.
        std::vector<Plan> plans;
        QSet<QString> patterns;
        QHash<QString, double> excess;
        for (const NickPattern &x : kNickPatterns) { patterns.insert(QString::fromLatin1(x.id)); excess.insert(QString::fromLatin1(x.id), x.excess); }
        for (const NickRow &r : kNickRows) {
            std::vector<int> st;
            for (const char *c = r.states; *c; ++c) st.push_back(*c == '1' ? 1 : *c == '0' ? 0 : -1);
            plans.push_back(plan(r.id, st, excess.value(QString::fromLatin1(r.id), 1.5)));
        }
        Session s;
        s.shots = sessionOf(plans);
        s.ledgers = conditionLedgers(s.shots, flatOpts());
        // The model's own tiers (warm-up weighting and soft tiers included), from its cards.
        for (ConditionLedger &l : s.ledgers)
            l.tier = patterns.contains(l.id) ? Tier::Pattern : l.fired > 0 ? Tier::Watching : Tier::Clean;
        QHash<QString, WorkOnClass> cls;
        for (const ReportCondition &c : p.conditions)
            cls.insert(c.id, c.kind == FactKind::Fault || c.kind == FactKind::Setup ? WorkOnClass::Movement
                           : c.kind == FactKind::Delivery ? WorkOnClass::Delivery : WorkOnClass::Excluded);
        WorkOnCauses causes;
        for (const ReportCause &e : p.causes) causes.emplace_back(e.from, e.to);
        QHash<QString, double> pos;
        std::vector<HabitRun> runs;
        for (const NickRun &r : kNickRuns) {
            pos.insert(QString::fromLatin1(r.id), workOnSwingPosOfPhase(QString::fromLatin1(r.phase)));
            SpreadCorridor c;
            c.known = true;
            c.lowOpen = r.lowOpen;
            c.highOpen = r.highOpen;
            c.shape = r.lowOpen ? CorridorShape::Ceiling : r.highOpen ? CorridorShape::Floor : CorridorShape::TwoSided;
            c.mu = r.mu;
            c.signalLo = c.idealLo = r.signalLo;
            c.signalHi = c.idealHi = r.signalHi;
            c.faultLo = r.faultLo;
            c.faultHi = r.faultHi;
            runs.push_back(HabitRun{ QString::fromLatin1(r.id), std::vector<double>(r.values, r.values + 10), c });
        }
        s.workOns = reduceSessionWorkOns(s.shots, s.ledgers, cls, causes, pos);

        check(!s.workOns.linksTestable, "Nick: every habit on every swing (or all but one) — no link testable");
        check(idsOf(s.workOns) == (QStringList{ "trail_knee_straighten", "over_the_top", "reverse_spine_p4",
                                                "hips_closed_at_impact", "early_extension" }),
              "Nick under rule v4: the knee first (P4, biggest miss there), over the top and reverse spine (P4), "
              "then hips and early extension (P7) — the mock's v3 order was knee, hips, EE, OTT, RS");

        const std::vector<StrengthPick> st = pickStrengths(s.ledgers, 10, s.workOns, p);
        check(idsOf(st) == (QStringList{ "slice", "sway", "head_rise_backswing" }),
              "Nick's strengths match the mock: No slices, hips turn in place, head stays level");
        bool heel = false;
        for (const StrengthPick &x : st) heel = heel || x.id == QStringLiteral("strike_heel");
        check(!heel, "…and 'Nothing off the heel' is not among them (toe strike showed)");

        std::vector<HabitRun> workOnRuns;
        for (const WorkOnEntry &e : s.workOns.entries)
            for (const HabitRun &h : runs) if (h.id == e.id) workOnRuns.push_back(h);
        const ReferenceSwing ref = referenceSwing(workOnRuns);
        check(ref.index == 2, "Nick's reference swing is swing 3, as the mock printed");

        const CauseGroups knee = causeGroups("trail_knee_straighten", s.ledgers, s.workOns, p);
        const CauseGroups hips = causeGroups("hips_closed_at_impact", s.ledgers, s.workOns, p);
        const CauseGroups ee   = causeGroups("early_extension", s.ledgers, s.workOns, p);
        const CauseGroups ott  = causeGroups("over_the_top", s.ledgers, s.workOns, p);
        const CauseGroups rs   = causeGroups("reverse_spine_p4", s.ledgers, s.workOns, p);
        check(knee.lead == CauseLead::AllRuledOut && hips.lead == CauseLead::AllRuledOut,
              "Nick's knee and hips: every authored cause checked and fine (the mock's lead)");
        check(ee.lead == CauseLead::Spotted && idsOf(ee.spotted) == QStringList{ "over_the_top" }
                  && ee.spotted[0].workOnRank == 2 && ee.spottedEverySwing && !ee.spottedSupported,
              "Nick's early extension: over the top spotted (now work-on 2), on every swing so not proven");
        check(ott.lead == CauseLead::Unchecked && rs.lead == CauseLead::Unchecked,
              "Nick's over the top and reverse spine: nothing spotted, some causes unchecked");
        check(ee.couldBe.size() == 6 && ee.couldBeMore == 4, "early extension: six could-be causes, then 4 more (as the mock)");

        const MissMagnitude rsm = missMagnitude(workOnRuns[2]);
        check(workOnRuns[2].id == QStringLiteral("reverse_spine_p4") && rsm.size == MissSize::Small && rsm.outside == 9,
              "Nick's reverse spine: 'a small miss: just outside the range, never far' (§10 rule 5)");
        check(rangeWords(workOnRuns[0].corridor, QStringLiteral("°"), -1) == QStringLiteral("12° or less"),
              "Nick's knee target, in the golfer's words: 12° or less (the mock said 'less than 12°')");

        const std::vector<WorthCheckingGroup> wc = worthChecking(s.workOns, s.ledgers, p);
        check(!wc.empty() && wc[0].check == CouldBeCheck::PhysicalTest && wc[0].items.size() == 5
                  && wc[0].items[0].ranks.size() >= 2,
              "Nick's worth checking leads with physical tests that sit behind two or more work-ons");
        std::printf("    nick: worth checking —");
        for (const WorthCheckingGroup &g : wc) {
            std::printf(" [%d]", int(g.check));
            for (const WorthCheck &x : g.items) {
                std::printf(" %s(", qPrintable(x.id));
                for (int r : x.ranks) std::printf("%d", r);
                std::printf(")");
            }
        }
        std::printf("\n");

        const OrderExplanation why = explainOrder(s.workOns, s.ledgers, p);
        check(why.whyNot == LinksUntestable::CauseOnEverySwing && why.bySwingOrder == 1 && why.byMagnitude == 3,
              "Nick's page 3: links untestable (causes on every swing); swing order set one place, the miss three");
        const Timeline tl = workOnTimeline(s.workOns);
        check(tl.firstIsEarliest && tl.marks.size() == 5, "Nick's work-on 1 is now also the earliest in the swing");
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
