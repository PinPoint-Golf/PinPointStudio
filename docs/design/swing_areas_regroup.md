# Swing areas: one axis, what moves

Decided with Mark, 8 Nov 2026. This note is the record of why the condition groups changed, what they are now, and what the
two side-bend conditions rest on. The condition-by-condition table is `tools/diagnostics/area_regroup_map.json`. The
results of the checks are at the end.

## The rule

**An area is what moves. The phase stays in the columns. Setup is the exception**, because it is static and has its own
chart.

The old groups used two axes at once. Posture, lateral, sequence, arms and club, swing plane and release said what moves;
setup, impact and finish said when. Consistency's columns already showed the second axis: setup was read only at P1, impact
only at P6-P7 and finish only at P7-P8. The mix caused the overlaps. `impact` held five body conditions. The axis-tilt
measures were read by conditions in three groups. `setup` held 28 latent nodes that never count, plus two P2 takeaway
faults. Rotation was spread over four groups and the wrists over three. `armsAndClub` was a catch-all.

Areas run body, arms, hands, club, ball, which is the causal flow of the graph.

## The areas

| Enum | Token | Label | Words in Consistency | Columns | Conditions |
|---|---|---|---|---|---|
| Setup | `setup` | Setup | Setup | P1 | 25 |
| Posture | `posture` | Posture | Posture | P1, then every span to P7-P8 | 16 |
| Lateral | `lateral` | Lateral & balance | Lateral and balance | P1-P2 to P7-P8 | 20 |
| Rotation | `rotation` | Turn & sequence | Turn and sequence | P1-P2 to P7-P8 | 29 |
| Arms | `arms` | Arms | Arms | P1-P2 to P7-P8 | 8 |
| Wrists | `wrists` | Wrists & release | Wrists and release | P1-P2 to P7-P8 | 14 |
| PlanePath | `planePath` | Plane & path | Plane and path | P1-P2 to P7-P8 | 18 |
| Delivery | `delivery` | Delivery | Delivery | P6-P7, reading P7 | 10 |
| BallFlight | `ballFlight` | Ball flight | none | none | 20 |
| Golfer | `golfer` | Golfer | none | none: never an area | 28 |

That is 188 conditions: the 186 that were regrouped, plus the two side-bend conditions in Posture.

- The enum and `kGroups` follow the table's order.
- `areaGroupOrder()` is the table without `golfer`.
- `consistencyAreas()` is the table without `ballFlight` and `golfer`.

**Golfer** holds the latent causes: the capacity, intent and equipment kinds. The model browser and editor file them there.
It never appears on Home, in Consistency or in the report.

Beyond the token renames (sequence to rotation, release to wrists, swingPlane to planePath, impact to delivery, armsAndClub
to arms), 30 conditions changed area, and the 28 latent nodes left Setup for Golfer:

| From | To | Conditions |
|---|---|---|
| armsAndClub | planePath | `across_the_line`, `laid_off`, `overswing`, `club_short_of_parallel`, `in_to_out_path`, `out_to_in_path` |
| armsAndClub | rotation | `short_backswing`, `over_rotation_at_top`, `hips_under_rotated_at_top` |
| armsAndClub | wrists | `cupped_at_top`, `bowed_at_top`, `open_face_top`, `shut_face_top`, `early_face_roll`, `face_held_shut_takeaway` |
| finish | lateral | `off_balance_finish`, `weight_back_at_finish` |
| finish | rotation | `abbreviated_finish` |
| impact | lateral | `reverse_spine_p7`, `excessive_axis_tilt_impact` |
| impact | rotation | `hips_closed_at_impact`, `hips_too_open_at_impact` |
| impact | wrists | `bowed_lead_wrist` |
| posture | lateral | `reverse_spine_p4`, `trail_hip_hike`, `excessive_axis_tilt_top` |
| posture | rotation | `flat_shoulder_plane`, `steep_shoulder_plane` |
| setup | planePath | `inside_takeaway`, `outside_takeaway` |

The regroup changed only groups, area labels and area order, with the exceptions recorded under "Areas only classify" and
"Side bend" below. No condition, signal, measure, norm, label or id was removed or renamed.

**Old tokens keep loading.** The user layer stores full rows for the conditions a golfer edited, and community packs may
carry old tokens. On load, a condition in the map that still holds its old token takes the new one; any other retired token
maps by default, with `finish` going to `lateral` (`currentConditionGroupToken`, `characteristic_pack.cpp`). The user pack is
migrated once, with a backup beside it. Saving writes new tokens and a marker (`groupsRevision`), so a golfer who
deliberately refiles a moved condition is not bounced back on the next load.

Any group change moves the content stamp, so each session re-detects once when opened in the new build.

## Which chart each area goes with

| Area | Chart |
|---|---|
| Setup | Posture, "At address" |
| Posture | Posture section |
| Lateral & balance | Plumb Bob preset |
| Turn & sequence | Kinematic sequence section and Body rotation preset |
| Arms | Arms preset |
| Wrists & release | Wrist section |
| Plane & path | Club delivery, steps 1 and 2 |
| Delivery | Club delivery, "At impact" |
| Ball flight | Shot shape grid |

## The gaps this exposes

Three areas have a preset but no domain section: **Lateral & balance**, the turn half of **Turn & sequence** (the Body
rotation preset; the sequence half has its section), and **Arms**. Charts need no change for the regroup itself, because
section visibility is keyed on the metrics on show, not on areas.

## Areas only classify

**Link grading has no temporal check** (Mark, 8 Nov 2026). The old check took a group's position in the enum as swing order,
so moving a condition between groups changed link grades. It wrongly blocked 38 of 605 causal links. It is dropped entirely.
A link's direction is the graph's authored direction.

**One edge was wrong and is removed:** `hips_too_open_at_impact → over_the_top`. Hips at impact cannot cause a move that
starts earlier.

**The after-ball rule is per reading** (Mark, 8 Nov 2026). P7-P8 used to be fully graded only for the area `finish`, and that
area is gone. A reading is now graded after the ball when a condition reads its measure at the `finish` anchor. The set is
derived from the pack (`afterBallKeysForPack`) and is `comOverLeadFoot` and `thoraxRotation`. `comOverLeadFoot` is the reading
the old rule graded; `thoraxRotation` at P7-P8 newly grades, and that is the one intended change. In a mixed P7-P8 cell only
those readings count toward the cell and the headline. The other readings stay on show, lighter. The headline's two halves
stop at P6-P7, so P7-P8 is in neither; an after-ball cell can only help name the area that moves most.

## Side bend

Side bend is the one content addition. `spineSideBend` is the face-on `hipLineTilt − shoulderLineTilt`, positive toward the
trail side: the thorax relative to the pelvis, the shape of the trunk. It is not the whole-trunk lean `secondaryAxisTilt`
reports, which is why it stays in Posture.

### Measures and conditions

| Id | What it is |
|---|---|
| `m_spineSideBendChangeImpact` | Primary. `spineSideBend`, `delta`, anchor P1, window P1 to P7. The change from address survives camera placement |
| `m_spineSideBendImpact` | `spineSideBend` at P7, for display and reporting |
| `m_spineSideBendTop` | Authored, not detected on. Its face-on P4 reading is unstable and was read on 128 of 279 swings; its ladder prefers the planned routes |
| `sig_sideBendGainLow`, `sig_sideBendGainHigh` | `outsideCorridor` on `m_spineSideBendChangeImpact`, directions `low` and `high` |
| `insufficient_side_bend_impact` | "Not enough side bend at impact". Posture, fault, draft |
| `excessive_side_bend_impact` | "Too much side bend at impact". Posture, fault, draft |

**Provenance of the two conditions.** `insufficient_side_bend_impact` cites `ref.chu2010` at `indirect`.
`excessive_side_bend_impact` is `practice`, because the literature does not support "too much".

**The causal edges.** All thirteen are `tier: practice`. No literature supports them. Mark's changes to the brief's list, 8 Nov: `insufficient_side_bend_impact → lie_steep_at_impact` added, because too little side bend raises the shaft; `poor_pelvic_disassociation`, `limited_shoulder_flexion` (for tight lats and stiff shoulders) and `poor_setup_posture` added as causes of too little side bend; `early_extension → insufficient_side_bend_impact` dropped, because with the shaft link it completes a causal path between `over_the_top` and `steep_downswing_shaft`, which already corroborate each other and so may not also be causally linked.

| Condition | Edges in | Edges out |
|---|---|---|
| `insufficient_side_bend_impact` | `poor_pelvic_disassociation`, `limited_shoulder_flexion`, `poor_setup_posture`, `upper_body_starts_down`, `hips_closed_at_impact` | `out_to_in_path`, `attack_too_steep`, `steep_downswing_shaft`, `lie_steep_at_impact` |
| `excessive_side_bend_impact` | `trying_to_lift_the_ball` | `low_point_behind_ball`, `attack_too_shallow`, `in_to_out_path` |

There is no `corroborates` or `excludes` edge with `excessive_axis_tilt_impact`. The two read different quantities, and the
difference is the point: see the reading table below.

### On the Posture section, out of the pattern match

The Posture section used to leave side bend off, because a reading that grows into impact would read a working swing as a
loss if drawn against address. Mark's ruling meets that with the corridor: the row is the change from address against its
expected-gain corridor, never against zero. It is on This shot, Session and the
report's Posture page, and not in the "At address" block. `posture_chart_design.md` has the row's detail.

**It is drawn and graded, and never matched** (Mark, 8 Nov 2026). It does not count toward which posture pattern a golfer
matches, and it has no `posture_level` or `posture_squat_push` norm rows. There is one wash for all clubs; the fault lines
follow the club, as on every other row.

### What the library shows

Measured 8 Nov 2026 on a copy of the library: 279 swings, 5 golfers, side-bend gain on 248. One golfer supplies 169 of the 248.
Golfers are lettered, A being that golfer.

| Club | Golfers, sessions | n | P1 median | P7 median | Gain median | Gain middle half |
|---|---|---|---|---|---|---|
| All irons | four golfers | 208 | | | about +3° | about +1° to +5° |
| 7 iron | three golfers | 178 | 6.3° | 9.0° | +2.9° | +0.8° to +4.6° |
| 8 iron | one golfer | 23 | 3.4° | 6.3° | +2.3° | +0.5° to +4.3° |
| 6 iron | one golfer | 7 | 6.2° | 5.8° | −0.6° | −1.2° to +0.1° |
| Gap wedge | one golfer, one session | 29 | 5.6° | 8.5° | +2.8° | +1.5° to +4.2° |
| Driver | one golfer, one session | 11 | 8.2° | 6.7° | −1.5° | −2.1° to +1.0° |

The driver figure is the opposite of what was expected, that a driver wants more side bend than a wedge. Eleven swings from
one golfer are not enough to write a driver row, so the driver falls back to `any`.

| Golfer | n | Gain median | Gain middle half |
|---|---|---|---|
| A | 169 | +2.8° | +0.8° to +4.6° |
| B | 37 | +3.2° | +1.4° to +4.4° |
| C | 23 | +2.3° | +0.5° to +4.3° |
| D | 11 | −1.5° | −2.1° to +1.0° |
| E | 8 | +0.2° | −0.7° to +1.6° |

**Correlations, r (n), by golfer.**

| | A | B | C | D | E | All |
|---|---|---|---|---|---|---|
| P7 side bend vs secondary axis tilt at P7 | 0.43 (169) | 0.73 (37) | 0.35 (23) | 0.12 (11) | 0.21 (8) | 0.51 (248) |
| P7 side bend vs club path | 0.51 (42) | −0.20 (27) | 0.21 (23) | 0.65 (11) | no data | −0.07 (103) |
| Gain vs club path, pelvis rotation partialled out | 0.17 (29) | −0.18 (27) | −0.32 (23) | 0.45 (11) | no data | −0.17 (90) |
| Gain vs hip line change | 0.66 (169) | 0.77 (37) | −0.22 (23) | 0.28 (11) | 0.87 (8) | 0.36 (248) |
| P7 side bend vs pelvis rotation at P7 | 0.36 (127) | −0.15 (37) | −0.18 (23) | −0.46 (11) | 0.34 (8) | −0.32 (206) |

**These relations are not established across golfers.** Side bend is not independent of secondary axis tilt: r 0.43 for
golfer A and 0.73 for golfer B. P7 side bend against club path is 0.51 for golfer A, −0.20 for golfer B and −0.07 across all
golfers, so it is not a general relation. The gain follows the hip line's change for some golfers (0.66, 0.77) and not for
others (−0.22). A path relation shows only within individual golfers, and not the same one each time.

**The first figures do not reproduce.** The early evidence came from the fixture `session_1005.json`: 29 gap-wedge shots, one
golfer, one session. The fixture predates the 7 Nov sweep, the likely cause of the differences. The library's figures for
that session:

| Figure | Fixture | Library |
|---|---|---|
| P7 median | 10.1° | 8.5° |
| P7 middle half | 9.7° to 11.4° | 7.3° to 9.9° |
| P1 median | 6.8° | 5.6° |
| P4 read on | 6 of 29, −12° to +4° | 7 of 29, −19.1° to +0.7° |
| P7 vs secondary axis tilt | 0.22 | 0.41 |
| P7 vs club path | 0.44 | 0.45 |
| P7 vs attack angle | 0.47 | 0.48 |
| P7 vs hand path loop | −0.35 | −0.21 |
| P7 vs pelvis rotation | 0.60 | 0.48 |
| P7 vs club path, pelvis rotation partialled out | 0.37 | 0.40 |
| Gain vs hip line change | 0.63 | 0.49 |
| Gain vs club path, pelvis rotation partialled out | 0.37 | 0.17 |

### The corridors

On `m_spineSideBendChangeImpact`, the change from address to impact. **Provisional, 2-D, derived from the library**
(`source: heuristic`). Published side-bend figures are 3-D, and the face-on reading is a 2-D projection with the hips and
shoulders open at impact, so they are not used as corridors. Context moves the corridor, never the sign.

| Context | Corridor | Wash | Fault lines | n | In wash | Too little | Too much |
|---|---|---|---|---|---|---|---|
| `any` | +3° ±3° | 0° to +6° | −3°, +9° | 248 | 70 % | 7 (3 %) | 3 (1 %) |
| `iron` | +3° ±3° | 0° to +6° | −3°, +9° | 208 | 69 % | 5 (2 %) | 3 (1 %) |
| `wedge` | +3° ±2° | +1° to +5° | −1°, +7° | 29 | 79 % | 0 | 0 |
| driver, through `any` | no row | 0° to +6° | −3°, +9° | 11 | 27 % | 2 (18 %) | 0 |

Under the `any` row, the share in the wash by golfer is A 71 % (169), B 86 % (37), C 70 % (23), D 27 % (11), E 38 % (8).

### Reading axis tilt and side bend together

| | Side bend normal or high | Side bend low |
|---|---|---|
| **Axis tilt high** | The trunk bends and the pelvis is forward: the good pattern | Whole-body lean: hanging back |
| **Axis tilt low** | Check the pelvis | On top of it: steep, over the top |

Axis tilt is the whole trunk against vertical. Side bend is the thorax against the pelvis. The table is how they are meant to
be read together. It is a coaching reading and has not been tested on the library, and the two measures are correlated
(above).

### The producers

None of these planned routes is built in this change.

| When | Producer | What it gives |
|---|---|---|
| Now | Face-on `spineSideBend` | P1 and P7 only |
| Planned, trunk IMUs | `thoraxSideBend` (`rot.thorax.sideBend.*`, `trunk_imu_sequence_grf_design.md`) with `pelvisObliquity` for the hip line | True side bend, the thorax relative to the pelvis, which needs both sensors. Makes P4 readable, and with it the side-bend route for `reverse_spine_p4`. Removes the rotation contamination. Lets the norms become the literature's 3-D figures |
| Planned, triangulated pose | 3-D hip and shoulder lines from the three-camera rig | A further route to the same quantity |

### Appendix A: why side bend is not a posture-pattern signature

**The decision** (Mark, 8 Nov 2026). `spineSideBend` is not used to tell the Level pattern from Squat and push. It gets no
pattern-node norm rows. The one adjacent comparison read found no difference at impact, and no source read compares swing
styles on side bend or pelvis tilt. In the library the gain follows the hip line's change for some golfers and not for others
(above), so using it in the match could let pelvis tilt pass for a pattern.

**What was verified.** Only `ref.chen2024` could be read in full.

| Reference | Read | What it shows for this decision |
|---|---|---|
| `ref.chen2024` | Full text | 21 golfers split by overhead-squat score. Lumbar lateral bending at impact 21.07 (SD 0.48) against 20.93 (SD 0.70), p = 0.591; top-to-impact displacement p = 0.218. A squat-ability screen and a modelled lumbar bend, so adjacent evidence only |
| `ref.meister2011` | Abstract and opening pages | Easy, medium and hard 5-iron swings in professionals. The abstract lists peak shoulder obliquity as consistent but not pelvic obliquity. Figures not checked |
| `ref.chu2010` | Abstract | 308 golfers, four events, lateral tilting named as significant. Size and direction not confirmed |
| `ref.joyce2010` | Abstract | The Cardan sequence and the trunk definition change the reading; recommends ZYX |

Appendix A's further figures, as first drafted, could not be verified from what was readable and were left out.

Lindsay and Horton 2002 concerns side bending on the backswing, not impact. No source supports a low-back claim about side
bend at impact, and none is made here.

### A.3 What would reopen the decision

A peer-reviewed comparison of swing styles on thorax-relative-to-pelvis side bend, or a squat-and-push golfer in the library
whose side bend separates from Level golfers' once pelvis tilt is controlled for. The trunk-IMU `thoraxSideBend` and
`pelvisObliquity` routes are what would allow that control.

## Results

All measured on 8 Nov 2026, on copies of the library. The live library was not written to.

### Equality check

The commit before this work against this work, with a pack that carries the regroup and the removed edge but not the
side-bend content. 23 sessions, 287 swings, five golfers, on the cabin PC.

| What | Result |
|---|---|
| Fired and assessable counts per condition | identical (157 conditions, 0 differences) |
| Every value in the ledgers: state, driving measure, value, corridor, z, per shot | identical (800,738 values, 0 differences) |
| Root ranking per session | identical |
| Links whose grade changed | 18 rows, every one up one grade, all on 5 of the 38 links the old order blocked |
| Links that went down | none |
| Links no longer in a session's graph | 120 rows, all from the removed edge |

The 5 links that rose: `over_the_top → early_extension` (8 rows), `arms_before_chest → over_the_top` (7),
`excessive_separation_stretch → over_the_top`, `over_the_top → chicken_wing` and `over_the_top → loss_of_width` (1 each).

Removing `hips_too_open_at_impact → over_the_top` takes `hips_too_open_at_impact` and the causes above it out of the graph
in the 15 sessions where nothing else kept them there: `limited_lead_hip_ir`, `slide`, `no_pelvis_braking` and
`hip_spin_out` into it, and `poor_glute_strength`, `poor_single_leg_balance` and `late_pelvis_rotation` into
`no_pelvis_braking`.

Each session re-detects once when first opened in this build, because a group is part of the pack the content stamp hashes.

### Home, before and after

One golfer's library, all sessions. Totals are unchanged except for the two new conditions, both clean.

| Before | Measured | Habit | Occasional | Clean | Unclear |
|---|---:|---:|---:|---:|---:|
| Posture | 16 | 4 | 0 | 12 | 0 |
| Sequence | 16 | 3 | 3 | 10 | 0 |
| Impact | 15 | 2 | 1 | 11 | 1 |
| Lateral motion | 13 | 2 | 0 | 11 | 0 |
| Arms & club | 14 | 1 | 2 | 10 | 1 |
| Swing plane | 10 | 1 | 1 | 8 | 0 |
| Finish | 2 | 1 | 0 | 0 | 1 |
| Ball flight | 19 | 0 | 4 | 15 | 0 |
| Setup | 19 | 0 | 2 | 17 | 0 |
| Release | 5 | 0 | 1 | 2 | 2 |
| **Total** | **129** | **14** | **14** | **96** | **5** |

| After | Measured | Habit | Occasional | Clean | Unclear |
|---|---:|---:|---:|---:|---:|
| Turn & sequence | 22 | 4 | 4 | 13 | 1 |
| Lateral & balance | 19 | 4 | 0 | 15 | 0 |
| Posture | 15 | 3 | 0 | 12 | 0 |
| Plane & path | 14 | 1 | 2 | 11 | 0 |
| Wrists & release | 8 | 1 | 1 | 2 | 4 |
| Delivery | 10 | 1 | 1 | 8 | 0 |
| Ball flight | 19 | 0 | 4 | 15 | 0 |
| Setup | 19 | 0 | 2 | 17 | 0 |
| Arms | 5 | 0 | 0 | 5 | 0 |
| **Total** | **131** | **14** | **14** | **98** | **5** |

`golfer` yields no area, on Home, in Consistency or in the report; the unit tests hold that from code.

### Derived-area change list

Beyond the plain renames, `metricAreasForPack` moves these:

| Key | Old | New |
|---|---|---|
| `clubPath` | armsAndClub | planePath |
| `comOverLeadFoot` | finish | lateral |
| `forearmPronation` | armsAndClub | wrists |
| `forearmRotation` | armsAndClub | wrists |
| `leadWristFlexExt` | armsAndClub | wrists |
| `secondaryAxisTilt` | posture | lateral |
| `shaftAngleVsHorizontal` | armsAndClub | planePath |
| `shaftDirection` | setup | planePath |
| `shoulderPlaneAngle3d` | posture | rotation |
| `thoraxRotation` | armsAndClub | rotation |

`thoraxRotation` was Appendix B's odd home, and that is fixed. `pelvisRotation` no longer ties. `spineSideBend` now derives
`posture` from its two conditions, so it is no longer hand-placed.

### Fixture checks

On `session_1005` (29 swings): 282 of 288 readings in a cell both before and after keep their grade, 164 of them having changed area. The six that moved are all
`spineSideBend`, and none of them comes from the regroup. Giving it a corridor changed the σ the reading's spread is judged against (6 to 4, and 6 to 2 at P6-P7 for
a wedge, which is the corridor at P7). Grades moved 5 to 4 at P1, P1-P2 and P5-P6, 2 to 1 at P4-P5 and 5 to 3 at P6-P7.

Span changes, 30 cells in all: rotation and wrists gain the columns the wider spans add, `comOverLeadFoot` gains P1-P2 to P6-P7 and `secondaryAxisTilt` loses P1.
After the ball, `thoraxRotation` and `comOverLeadFoot` keep their own P7-P8 grades. The `rotation` and `lateral` P7-P8 cells are graded on those readings alone.

The client report's other pages render pixel-identical, with these exceptions: the Posture pages carry the new row and its trust lines,
the Consistency page carries the new rows, and one work-on page reads "Ruled out… 3 more" where it read "4 more", because of the removed edge.
