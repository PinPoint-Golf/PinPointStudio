# Pelvic tilt: what we measure, what the evidence says, what to build

> **Date:** 2026-11-19 · **Status:** parked. Nothing built. Come back to it after the architecture refactor
> (`docs/implementation/swe-audit/04-remediation-plan.md`), which freezes new functionality until it lands. Every
> proposal below is marked "(proposed, for Mark)"; the norm needs his decision before anything grades by it
> (memory note "provisional norms need a decision").

## 1. The question

Greg Rose (TPI) is quoted as saying tour players address the ball with the pelvis tilted forward 20–25° and arrive at
impact at 0 to −5°. Do we measure pelvic tilt, and how do our norms compare? (Coaching Lead, 19 Nov 2026.)

"Pelvic tilt" here means the **forward/back (sagittal) tilt**: the pelvis tipping toward the ball at address and
"tucking under" through impact. It is not the sideways tilt (lead hip high or low), which our docs also call tilt:
`hipLineTilt`, `pelvisObliquity` and the `rot.pelvis.tilt.*` row of `trunk_imu_sequence_grf_design.md` §8.1 are all
the sideways one.

## 2. Short answer

- **We do not measure it.** There is no metric, no measure and no norm (`posture_chart_design.md` §3.3: "We have no
  producer for pelvic tilt").
- **The cameras cannot measure it.** Pose gives two hip points, and a line through two points cannot show the pelvis
  tipping forward or back. The 3-D skeleton fit admits this (`src/Skeleton3d/skeleton3d_fit.h`, the comment above
  `pelvisTiltAccRad`). Its pelvis tilt comes from a smoothness prior, not a measurement. Before that prior was added,
  the fitted pelvis swung from 5° to 55° and back every third of a second.
- **The hip sensor can, and most of the arithmetic is written.** `src/Body/body_rotation.cpp` (around line 170)
  computes the hip sensor's forward bend at every sample, in the ball plane (`trunk_sensor::ballPlaneBendDeg`). Today it
  is used only inside the side-bend σ. `pelvisAddressBendDeg` (same file, `addressBendDeg`) exists only to catch a
  sensor worn face-in. Neither is published. `trunk_imu_sequence_grf_design.md` §3.2 and §8.3 already plan the
  metric (`pelvisTilt`) and the measure (`m_pelvisTiltChangeImpact`, "companion to `m_pelvisThrustDown`").
- **The camera readings nearest to it are side effects of the tuck, not the tilt itself.** These are hip depth toward
  the ball (`m_pelvisThrust*`, down the line), the `early_extension` fault and the trunk bend's change through impact
  on the Posture chart.

## 3. What the evidence says

| Source | Who | Address (P1) | Impact (P7) | Change |
|---|---|---|---|---|
| Okuda, Gribble, Armstrong 2010 (`ref.okuda2010`) | 13 skilled, handicap 0.8, driver, 3-D video | −11.2 ± 6.5° (forward) | +6.6 ± 5.2° | about 18° |
| same study, low-skilled group | 17 players, handicap 30.8 | — | 0.0 ± 8.1° | — |
| Evans et al. 2008 (`ref.evans2008`) | 29 skilled men, handicap 1.6, 5-iron, electromagnetic sensors | 23.1° flexion | 4.9° | about 18° |
| Greg Rose / TPI, as quoted to Mark | tour players | 20–25° | 0 to −5° | about 20–30° |

The figures for the two studies come from `posture_chart_design.md` §3.3. The TPI figures are as Mark quoted them.
The TPI article already in our sources ([understanding and assessing pelvic tilt](https://www.mytpi.com/articles/swing/understanding-and-assessing-pelvic-tilt-in-the-golf-swing),
cited in `posture_chart_design.md` §3.7) has **not** been checked for these numbers. Until a source is found, it is a
**[claim]**, like the Sportsbox tour ranges in §3.4.

What the table supports:

1. **Compare the change, not the absolute angle.** The two studies disagree by 34° at address and agree on an 18°
   change. The address value depends on where each lab puts zero: against vertical, against the anatomical pelvis
   (whose standing tilt is itself several degrees forward), or against the golfer's own standing posture. Evans' 23°
   at address matches TPI's band, which suggests TPI uses a similar zero, but that is inferred, not checked.
2. **The planned zero is the golfer's stand-tall posture.** `trunk_imu_sequence_grf_design.md` §3.2 defines
   `pelvisTilt` "from upright stance", and the sensor bends already read 0 standing tall. On that zero no golfer would
   read 20–25° at address. A band copied from TPI's absolute values would fail everyone.
3. **TPI's impact figure goes further than either study.** Evans ends at +4.9° and TPI says 0 to −5°, so TPI's change
   is about 20–30° against the measured 18°.
4. **Neither study measured tour players.** Both are skilled amateurs (handicaps 0.8 and 1.6), and they used
   different clubs (driver, 5-iron).

## 4. Why TPI's figure might still be right for some players (the Coaching Lead's hypotheses, 19 Nov 2026)

The Coaching Lead was also sceptical of the TPI claim, but suggested two ways it could still hold:

- **The very best players may tuck more.** Okuda is consistent with tucking growing with skill: the skilled group
  reached +6.6° at impact and the low-skilled group 0.0°. That is two skill levels, not a trend up to tour level, so
  it is a hint, not support.
- **It may depend on the swing pattern: "squatters vs swingers".** The Posture chart already names two patterns
  (`posture_chart_design.md` §4.2): **Level** ("stays in posture") and **Squat and push** (sits into the lead side in
  transition, then "straightens hard through impact"). Posting up the lead leg drives the pelvis up and under, so a
  Squat and push player might show a bigger tuck than a Level one. **No source has been found that splits pelvic tilt
  by pattern.** This is the Coaching Lead's hypothesis.

How each could be tested:

- **Within our own data:** read the P1→P7 change for every swing with a hip sensor worn, and set it beside the
  Posture chart's nearest pattern for that swing. With one or two wearers this shows whether the idea holds for
  those golfers. It cannot set a norm (`trunk_imu_sequence_grf_design.md` §3.1, "One golfer").
- **For tour level:** only a published study that measured tour players, ideally split by pattern, can confirm the
  20–30° change. A search for one is the first job when this is picked up (proposed, for the Coaching Lead).

## 5. What to build, after the refactor (proposed, for the Coaching Lead)

1. **Publish the series `pelvisTilt`:** the hip sensor's forward bend in the ball plane, the same definition and
   sign as `spineForwardBend`'s `trunkImus` route, so 0 = stand tall and + = tipped toward the ball. Route
   `pelvisImu`, Direct. σ from the existing §8.2 budget for the hip sensor: garment 2°, gyro scale, drift and mount
   yaw through `ballPlaneBendDeg`. No new σ term. It inherits `body.pelvisFaceIn` and `body.trunkMountYawAssumed`.
   It needs the hip sensor only. A swing without one gets no reading, says why, and stays byte-identical.
2. **Add the measure `m_pelvisTiltChangeImpact`:** P7 minus P1, in degrees, negative = the pelvis tucked under.
   This is the number to compare with the literature, because it does not depend on the zero (§3, point 1).
3. **Set a provisional band from the two studies:** a change of about −18°. The width would come from the studies'
   SDs, which are not worked out here. Label it skilled amateurs, driver and 5-iron, provisional. Put the TPI figure
   in the band's note as a claim, not in the band. The Coaching Lead decides the band before anything grades by it.
4. **Per-pattern bands only with a source.** If a study turns up that splits the tuck by Level vs Squat and push, or
   our own wearers show a clear split, the band moves to the pattern nodes, the way the head and pelvis-height bands
   already sit at `posture_level` / `posture_squat_push` in `norms.json`. Without a source, the general row stays.
5. **Where it shows:** the Posture chart's trunk row is the natural home (`posture_chart_design.md` §3.3 already says
   the tuck "explains why the lower back's line changes through impact even in a good swing"). Showing it is a
   design-system question for the Design Lead when the time comes. No layout is proposed here.

**First read-only step when this is picked up:** check whether any of the 4 July swings (the trunk-sensor sessions
`body_rotation.cpp` refers to) has a hip sensor reading, and pull Mark's own P1→P7 change from it. That says whether
the sensor's numbers are sensible before anything is built.

## 6. Ownership

| New or changed thing (ids) | 1. Kind | 2. Owner, level | 3. Created / ended by; identity | 4. If derived: inputs, rule version, invalidating events | 5. Persisted where; the one writer | 6. Ring of each new file | 7. How a view reaches it |
|---|---|---|---|---|---|---|---|
| `pelvisTilt` series | derived | the shot's analysis, shot | made by re-analysis of the shot; keyed by metric id on the shot | the hip sensor's fused stream, the phase ladder, the address front; a new `versions.pelvisTilt`; re-analysis | `swing.ppsw` `analysis`, written by the body-rotation stage through `SwingStore` | no new file (`src/Body`, domain) | the metric catalogue (`BodyRotationProvider::provides`) |
| `m_pelvisTiltChangeImpact` measure | rules data | the rule set, application | added to `core.json` | `pelvisTilt` at P1 and P7; the rule version | `src/Resources/diagnostics/core.json` | rules | diagnostics, like every measure |
| the provisional band | rules data | the rule set, application | added to `norms.json` on the Coaching Lead's decision | — | `src/Resources/diagnostics/norms.json` | rules | diagnostics, the Posture chart |

Information-model change? Probably not. It is a new metric in an existing measurement family (the trunk sensors'
bends), which §1.2 excludes. The reviewer confirms when it is planned.

## 7. Decisions for the Coaching Lead when this is picked up

1. Build it at all, given how few swings have a hip sensor?
2. Sign and zero: stand tall = 0 and + = toward the ball, matching `spineForwardBend`, as proposed in §5.1?
3. The provisional band: about −18° from Okuda and Evans, with TPI as a note. Or wait for a tour-level source?
4. Test the Squat and push hypothesis on our own wearers first, or only on a published source?
