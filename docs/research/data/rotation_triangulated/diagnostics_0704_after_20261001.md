# Diagnostics uncertainty — gate report (session_diagnostics_design.md §A8.8)

1 sessions. Assessed rows with σ on: 1122, of which 788 quantified and 80 borderline.

| Gate | Result | Detail |
|---|---|---|
| G1 verdicts identical with σ on | PASS | 0 mismatches over 1980 rows |
| G2 no σ ⇒ today's tiers and roots (soft tier) | PASS | 0 tier changes, 0 sessions with different roots |
| G3 soft-tier moves rest on borderline shots | PASS | 1 moves, 0 not borderline |
| G4 posterior ranking (reported, not gated) | — | root order changed in 1 of 1 sessions |
| Cost < 50 ms per re-rank | PASS | worst 8.4 ms |

Screen calibration (§A8.7): 0 screen results entered across these sessions — the table is empty until screens are entered.

## 2026-07-04_Mark-Liversedge_Wrist_01 — 15 shots

M0 patterns: ball_too_far (15/15), early_extension (9/15), flying_elbow (5/5), hips_closed_at_impact (15/15), lie_steep_at_impact (9/15), low_point_behind_ball (7/13), over_the_top (15/15), pelvis_thrust_backswing (15/15), reverse_spine_p4 (14/15), sway (9/15), trail_knee_straighten (13/15)
M0 roots: poor_pelvic_disassociation, limited_thoracic_rotation, hanging_back, excessive_knee_flex, late_pelvis_rotation
M3 patterns: ball_too_far (15/15, P 1.00), early_extension (9/15, P 1.00), flying_elbow (5/5, P 1.00), hips_closed_at_impact (15/15, P 1.00), lie_steep_at_impact (9/15, P 0.66), over_the_top (15/15, P 1.00), pelvis_thrust_backswing (15/15, P 1.00), reverse_spine_p4 (14/15, P 1.00), sway (9/15, P 0.69), trail_knee_straighten (13/15, P 1.00)
M3 roots: s_posture, c_posture, limited_lead_ankle_dorsiflexion, across_the_line, over_rotation_at_top, excessive_knee_flex, late_pelvis_rotation

G1: 0 verdict mismatches with σ on.
G2: 0 tier changes and the same roots with σ withheld under the soft tier. Posterior with σ withheld: roots poor_pelvic_disassociation, limited_thoracic_rotation, hanging_back, excessive_knee_flex, late_pelvis_rotation → s_posture, c_posture, limited_lead_ankle_dorsiflexion, across_the_line, pelvis_sink_backswing, excessive_knee_flex, late_pelvis_rotation, stance_wide.
G3: 1 tier moves under the soft tier.
- low_point_behind_ball: Pattern → Watching (7 of 13 shots fired; P(Pattern) 0.51)
G4: roots, sum score → posterior: poor_pelvic_disassociation, limited_thoracic_rotation, excessive_knee_flex, late_pelvis_rotation → s_posture, c_posture, limited_lead_ankle_dorsiflexion, across_the_line, over_rotation_at_top, excessive_knee_flex, late_pelvis_rotation
  - s_posture: score 0.457, P(present) 0.572, old score 0.280, explains 1, firm
  - c_posture: score 0.297, P(present) 0.495, old score 0.210, explains 1, firm
  - limited_lead_ankle_dorsiflexion: score 0.209, P(present) 0.348, old score 0.210, explains 1, firm
  - across_the_line: score 0.155, P(present) 0.259, old score 0.120, explains 1, firm
  - over_rotation_at_top: score 0.002, P(present) 0.005, old score 0.060, explains 1, fragile
  - excessive_knee_flex: score 0.002, P(present) 0.001, old score 0.160, explains 2, firm
  - late_pelvis_rotation: score 0.001, P(present) 0.001, old score 0.160, explains 1, firm
Re-rank cost: 8.4 ms (explain + 200 stability draws: drawing 1.4 ms, 33 distinct Pattern sets explained).
Word sensitivity: 0 single-word moves change the top root (s_posture).
