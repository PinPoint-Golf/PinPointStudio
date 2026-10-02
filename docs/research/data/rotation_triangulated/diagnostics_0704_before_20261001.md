# Diagnostics uncertainty — gate report (session_diagnostics_design.md §A8.8)

1 sessions. Assessed rows with σ on: 959, of which 619 quantified and 37 borderline.

| Gate | Result | Detail |
|---|---|---|
| G1 verdicts identical with σ on | PASS | 0 mismatches over 1980 rows |
| G2 no σ ⇒ today's tiers and roots (soft tier) | PASS | 0 tier changes, 0 sessions with different roots |
| G3 soft-tier moves rest on borderline shots | PASS | 1 moves, 0 not borderline |
| G4 posterior ranking (reported, not gated) | — | root order changed in 1 of 1 sessions |
| Cost < 50 ms per re-rank | PASS | worst 3.4 ms |

Screen calibration (§A8.7): 0 screen results entered across these sessions — the table is empty until screens are entered.

Words that alone decide a top root somewhere (sessions affected):
- edge c_posture → flying_elbow (1)
- edge limited_lead_ankle_dorsiflexion → early_extension (1)
- edge limited_lead_ankle_dorsiflexion → lead_knee_drifts_in_at_top (1)
- edge s_posture → reverse_spine_p4 (1)
- prominence of hips_under_rotated_at_top (1)
- prominence of limited_lead_ankle_dorsiflexion (1)
- prominence of reverse_spine_p4 (1)
- prominence of s_posture (1)

## 2026-07-04_Mark-Liversedge_Wrist_01 — 15 shots

M0 patterns: early_extension (11/15), flying_elbow (10/10), lead_knee_drifts_in_at_top (11/15), lie_steep_at_impact (9/15), low_point_behind_ball (8/13), over_the_top (15/15), pelvis_thrust_backswing (15/15), reverse_spine_p4 (14/15), trail_knee_straighten (13/15)
M0 roots: limited_thoracic_rotation, hanging_back, excessive_knee_flex
M3 patterns: early_extension (11/15, P 1.00), flying_elbow (10/10, P 1.00), lead_knee_drifts_in_at_top (11/15, P 0.94), lie_steep_at_impact (9/15, P 0.59), over_the_top (15/15, P 1.00), pelvis_thrust_backswing (15/15, P 1.00), reverse_spine_p4 (14/15, P 1.00), trail_knee_straighten (13/15, P 1.00)
M3 roots: limited_lead_ankle_dorsiflexion, s_posture, c_posture, hips_under_rotated_at_top, excessive_knee_flex

G1: 0 verdict mismatches with σ on.
G2: 0 tier changes and the same roots with σ withheld under the soft tier. Posterior with σ withheld: roots limited_thoracic_rotation, hanging_back, excessive_knee_flex → limited_lead_ankle_dorsiflexion, s_posture, c_posture, hips_under_rotated_at_top, pelvis_sink_backswing, excessive_knee_flex.
G3: 1 tier moves under the soft tier.
- low_point_behind_ball: Pattern → Watching (8 of 13 shots fired; P(Pattern) 0.51)
G4: roots, sum score → posterior: limited_thoracic_rotation, casting, excessive_knee_flex → limited_lead_ankle_dorsiflexion, s_posture, c_posture, hips_under_rotated_at_top, excessive_knee_flex
  - limited_lead_ankle_dorsiflexion: score 0.518, P(present) 0.588, old score 0.315, explains 2, firm
  - s_posture: score 0.457, P(present) 0.572, old score 0.280, explains 1, firm
  - c_posture: score 0.387, P(present) 0.646, old score 0.210, explains 1, firm
  - hips_under_rotated_at_top: score 0.361, P(present) 0.452, old score 0.280, explains 1, firm
  - excessive_knee_flex: score 0.002, P(present) 0.001, old score 0.160, explains 2, firm
Re-rank cost: 3.4 ms (explain + 200 stability draws: drawing 1.1 ms, 13 distinct Pattern sets explained).
Word sensitivity: 8 single-word moves change the top root (limited_lead_ankle_dorsiflexion).
  - edge limited_lead_ankle_dorsiflexion → early_extension often → sometimes puts s_posture first
  - edge s_posture → reverse_spine_p4 usually → always puts s_posture first
  - edge c_posture → flying_elbow often → usually puts c_posture first
  - edge limited_lead_ankle_dorsiflexion → lead_knee_drifts_in_at_top sometimes → rarely puts s_posture first
  - prominence of s_posture Common → Almost everyone puts s_posture first
  - prominence of reverse_spine_p4 Common → Occasional puts s_posture first
  - prominence of limited_lead_ankle_dorsiflexion Common → Occasional puts s_posture first
  - prominence of hips_under_rotated_at_top Common → Almost everyone puts hips_under_rotated_at_top first
