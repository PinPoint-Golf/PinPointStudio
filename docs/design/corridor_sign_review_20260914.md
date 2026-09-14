# Corridors and sign conventions — a review against the corpus (2026-09-14)

**Status: APPLIED, 2026-09-14 (same day).** Mark approved all fourteen changes in §7 as a batch; §0
below records what was actually done per item, where the implementation departed from the
recommendation and why, and what the corpus reads after it. §§1-7 are left as written, as the record
of the review that was decided over.

## 0. What was applied

| # | disposition | note |
|---|---|---|
| 1 | **done** | Grid rows p2-p8 mirrored, the two set signals swapped, descriptors and `wrist_angles.h` reworded. It went further than the norm grid: the wrist SCORER's fixtures, demo traces and two rules (F4 cast, F5 insufficient set: `below` → `above`) were authored in the same + = set sign and had to move with it, and `docs/reference/wristmetrics.md` said "hinge = ulnar". `insufficient_set` 10/10 → 0/10. |
| 2 | **done, and a code fix with it** | The July swings had NO club declared; the picker's `DRIVER` **stub** was being graded. 56 swings now carry `review.club = "7 IRON"` (the app's own override; `corpus/relabel-20260914.json` lists them and how to reverse it), and `measure_sample.cpp` now reads `swingDocDeclaredClub()` so an undeclared club grades in the default context, never as a driver. Driver-context shots 64 → 8 (the LM-only session's, which are declared). |
| 3 | **done** | `m_shoulderPlane` → noProducer on the planned `shoulderPlaneAngle3d`. `flat_shoulder_plane` 34/58 → not produced. |
| 4 | **done** | `m_attackAngle` reads `lm.attackAngle` only; `m_lowPointAhead` gets plausibility caps ±10 in (the validator requires caps outside every row's 3σ band, so not the ±6 proposed). Attack conditions 82/119 → 0/29 (LM shots). ⚠ Residual: low point still fires on 30 of the 48 in-cap readings — the same tracker impact geometry as the lean bias (§0 item 12). Decide whether it goes LM-only too. |
| 5 | **done** | `m_leadHandWidth` floor 65 → 90 ± 8. |
| 6 | **done, seated** | Window P1-P3; seated at 46 ± 6 from 95 fresh shots (42-56 %). `disconnection` 96/96 → 0/95. |
| 7 | **done** | `m_axisTiltImpact` 12/15/10/8 ± 6-8 by context. `reverse_spine_p7` 56/96 → 0/96 and with it the `reverse_pivot` conjunction 55 → 0; `reverse_spine_p4` stays at 94/96, which the video agrees with. |
| 8 | **done, narrower than proposed** | `m_faceToPath` σ 1 → 1.5 (firing edge at the 3° the row's own citation names; `lm_corridor_test` re-pinned: 2.5 silent, 3.5 watch, 5 action). `m_lmLaunchDirection` left at σ 2: a 4° start-line miss on a driver is a miss. |
| 9 | **done, the other route** | The producer's own comment argues a span gate is a domain question, not a validity one, so the LOW tail of `m_pelvisLiftTop` is declared unwatched with the reason, and `pelvis_sink_backswing` moved to a new `m_pelvisSinkTop`, noProducer on a planned `pelvisLiftBelt` (a tracked waistband edge) so the condition resolves and says why. 96/96 → not produced. |
| 10 | **done** | `m_ballPosition` plausible −30..130 (outside every row's band). |
| 11 | **done — it was the anchor** | The Finish tick lands 200-300 ms after impact while the body is still rotating; the balance curve read 50-60 % of stance from the lead ankle there and 16-25 % two hundred ms later. `lower_body_metrics.cpp` now stamps the Finish sample as the median of the valid samples 300-800 ms after the tick — the HELD finish — falling back to the tick when the capture ends sooner. ⚠ Residual: `off_balance_finish` still fires on 39/96 at the held finish (7-iron median 33 %, corridor 15 + 10). Either this golfer does hold his weight short of the lead foot or the corridor's 15 is wrong; one golfer cannot say which. |
| 12 | **measured, carried as error** | Against 39 hand-marked P7 frames the tracker overshoots by a median +12° (sd 9.5) and it is NOT a timing offset (implied offset 0-20 ms; one session reads more lean 15 ms AFTER impact). The producer now stamps σ 9.5 on the series and every `m_impactShaftLean` row has σ 10 both sides, mu unchanged; a bias corrected in a norm would hide the fault. `excessive_shaft_lean` 42/96 → 9/96. The tracker fix (exposure smear at 6.5 ms? the P7 emission geometry?) is producer work still open. |
| 13 | **done, seated** | Peak taken as the LAST sample within 97 % of the maximum (a club still at full speed into the ball reads 0 whatever a wobble did earlier); re-seated 3 + 15 ms from 96 fresh shots (median 2.5, p95 53 — a distinct top decile where the composed speed dips and recovers inside the last 60 ms). `deceleration` fires 15/96. |
| 14 | **done** | Eight forearm-rotation rows carry an UNVERIFIED citation. |

### The corpus after the batch (123 shots; before = the same fresh series under the pre-batch pack)

| condition | before | after |
|---|---|---|
| reverse_spine_p4 | 94/96 | 94/96 |
| sway | 62/96 | 62/96 |
| lead_knee_drifts_in_at_top | 57/96 | 57/96 |
| flying_elbow | 35/58 | 35/58 |
| pelvis_sink_backswing | 96/96 | not produced |
| disconnection | 96/96 | 0/95 |
| flat_shoulder_plane | 34/58 | not produced |
| reverse_spine_p7 / reverse_pivot | 56 / 55 | 0 / 0 |
| stance_narrow | 61/96 | 27/96 |
| excessive_shaft_lean | 42/96 | 9/96 |
| attack_too_steep / _shallow | 53 / 29 of 119 | 0 / 0 of 29 |
| insufficient_set | 10/10 | 0/10 |
| low_point_behind_ball | 49/85 | 30/48 (residual, see item 4) |
| off_balance_finish | 39/96 | 39/96 (residual, see item 11) |
| deceleration | 1/7 | 15/96 |

Per session the pattern count fell from 6-12 to 1-6, and the 9 Sep session reads three patterns —
reverse spine at the top, lead knee working in, sway — which is the video's list and the lesson's.

Tests: 190 of 190 (one networking suite is flaky and passes alone). Producers changed (finish sample,
lean σ, peak plateau), so the corpus and the library 9 Sep session were re-analysed on the Mac and
re-graded; every ledger's predecessor sits beside it as `diagnostics.json.pre-batch14`.

---


## 1. Method

- **Evidence base.** 123 shots: the 12 corpus sessions plus the library 9 Sep session, all
  re-analysed on 14 Sep at beta2+ (pose reused, shaft re-run, the camera-tier rotation series gone)
  and re-graded with `regrade_ledger` under the pack as of 79ffef1. Every graded ledger row carries
  the driving measure's value, so the corpus distribution of every measure that has a norm is
  available without a second pass (Appendix A).
- **The test applied to a corridor.** If a measure grades beyond ±2σ on more than 40 % of the shots
  it was read on, one of three things is true: the golfer has that fault chronically, the corridor
  is seated on the wrong scale, or the measure is not measuring what its condition names. The
  first is a finding, the second and third are defects. The 9 Sep session, read by eye and by a
  PGA professional, is the ground truth that separates them; the July frames were checked where
  the July numbers disagreed with September.
- **The test applied to a sign.** Every corridor signal was joined to its measure's `highMeans`
  and then to the PRODUCER's `signPositive` in the metric manifest, which is the only authority
  (the head-sway inversion fixed in 79ffef1 got past `axis_direction_test` because the fixture
  quoted the measure's paraphrase, not the producer).
- **One golfer.** All 123 shots are one right-handed golfer. Nothing here can seat a population
  norm; what it can do is show where a corridor cannot be right for anyone.

## 2. Certain defects — sign and scale

### 2.1 The lead-wrist hinge grid is authored in the opposite sign to its producer

`wrist_angles.h` and `hm_frame.h` emit radial/ulnar deviation with **+ = ulnar, − = radial** (ISB).
Cocking the club at the top is RADIAL deviation — the thumb toward the forearm — and the ten
HackMotion shots in the corpus read **−40 to −53°** at P4 relative to address. The norm grid says
the top should read **+38 ± 12**, P3 +28, P5 +38, P6 +30, P7 +8: a grid authored as "+ = set", which
is the vendor's sign, not ours. Consequences today: `insufficient_set` fires on 10 of 10 wrist-sensor
shots by construction, `over_set` can never fire, and the descriptor text calls ulnar deviation "the
wrist hinge/cock", which is anatomically backwards. (+45° of ulnar deviation at the top is beyond
the joint's range; the numbers are right and the words and the grid are wrong.)

**Recommend:** negate `mu`/`monitorLo`/`monitorHi` on `m_leadWristRadUln_p2..p8`; swap
`sig_insufficientSet` to `high` and `sig_overSet` to `low`; fix the two source comments and the
manifest `signPositive`; two rows in `axis_direction_test` quoting the producer. The flex/ext grid
is in the producer's convention (+ bow) and its readings are plausible (cup gained at the top on 5
of 10; impact within ±10); leave it. The forearm-rotation grid has **no corpus reading at all** and
its sign cannot be audited from data — mark it unverified in the norm citations rather than trust it.

### 2.2 `m_leadHandWidth` — the floor sits below where a straight arm can reach

Floor at mu 65 ± 10 % of arm length; the corpus reads 86–125 (median 90–95) on every club. A
lead arm that collapses to 70 % — a real loss of width — grades **Ideal** today because 70 is above
the floor's mu. `loss_of_width` cannot fire until the arm folds to 45 %.
**Recommend:** re-seat mu ≈ 90, σ ≈ 8, floor, heuristic, "seated on one golfer".

### 2.3 `m_leadUpperArmToChest` — measured at the wrong phase for the fault it names

25 ± 12 % of shoulder width, extremum over P1–P4; corpus 53–82 on every shot of every club (100 %
fire, `disconnection` is the top pattern on every session). At P4 the lead upper arm is above the
chest line by geometry — that is what raising the arms does — so the maximum over a window that
includes the top measures arm elevation, not connection. The coaching concept ("the lead upper arm
stays on the chest") is about the takeaway and halfway back.
**Recommend:** re-window to `["p1","p3"]` and re-seat from the corpus; until then the 100 % is a
scale artefact and should not headline a session. The 9 Sep video shows the arms lifting steeply,
so *some* of it is real — the measure just cannot say how much.

### 2.4 `m_axisTiltImpact` — a 3-D literature figure graded on a 2-D projection

Norm 25 ± 8 (irons) / 30 ± 8 (driver). The producer reads 4–9° on 100 % of shots in every context,
and my own pose measurement of the same quantity on 9 Sep gave 7°. A face-on projection of the
pelvis-to-shoulder-centre line cannot reproduce the 20–30° that 3-D systems report, because at
impact the shoulders are open and much of the lateral bend has rotated into depth. The corridor
is right about bodies and wrong about this camera.
**Recommend:** re-seat as an explicitly 2-D quantity (≈ 10 ± 6 iron, 15 ± 8 driver, heuristic,
citation stating why the literature number does not transfer), or move the condition pair
(`reverse_spine_p7`, `excessive_axis_tilt_impact`) to the triangulated pair as planned. Same
question for `m_axisTiltAtTop` (12 ± 6): it fires on 100 % too, but there the video AGREES — the
9 Sep golfer really is tilted toward the target at the top — so that corridor may simply be right
and the golfer wrong. Keep, note.

### 2.5 `m_faceToPath` and `m_lmLaunchDirection` — corridors at tour tolerance

0 ± 1° face-to-path and 0 ± 2° start line grade a good amateur as a miss on a third of shots (driver
38 % / 25 %). **Recommend:** ±2.5° and ±3° respectively, heuristic; these are launch-monitor readings
so re-seating from a second golfer is cheap when one exists.

## 3. Measures that answer a different question from their condition

### 3.1 `m_shoulderPlane` at P4 — foreshortened into noise

`flat_shoulder_plane` was the sixth pattern on 9 Sep. At the top the shoulder joints have turned
~70–90° and their image-plane separation collapses from 131 px at address to **19–79 px**, with
keypoint confidence 0.40–0.54; the "plane angle" is then atan(dy / dx) with dx near zero, and reads
5.7°, 28.7°, 4.6°, 26.8°, 8.2°, 13.2°, 30.4° across seven swings of the same shape. The descriptor
already says the line "foreshortens as the thorax turns"; at P4 that is the whole reading. Same
class as the rotation family retired in 8524467 — an image-plane angle whose meaning is destroyed by
the turn exactly where the condition reads it. The series itself is fine at address and impact.
**Recommend:** `m_shoulderPlane` → `noProducer` on a planned `shoulderPlaneAngle3d` from the
triangulated pair; `flat_/steep_shoulder_plane` report "not produced" with a gapReason.

### 3.2 `m_attackAngle` and `m_lowPointAhead` from the camera — tracker noise graded as strikes

Camera attack angle p5–p95: **−65° to +38°** (driver), **−33° to +46°** (6-iron). Low point: **−20 to
+23 inches**. No golf swing does this; it is the shaft track's clubhead path in the last two frames.
The two conditions fire on 26–50 shots each and FLIPPED between shallow and steep on the same
swings when the shaft stage re-ran (attack_too_shallow 53 → 26, attack_too_steep 23 → 50 across
116 shots). Both measures `preferKeys` the launch monitor and fall back to the projected series.
**Recommend:** drop the camera fallback (launch-monitor only) until the tracker's impact geometry
is validated against the hand-marked P7 truth — or, minimally, plausibility caps (attack ±15°, low
point ±6 in) so the readings become `Unavailable(implausible)` rather than findings. The tracker's
`m_impactShaftLean` has the same provenance and reads ~2× the hand-marked lean (4–19° vs 1–6° on
9 Sep; wedge median 33°); calibrate before grading `excessive_shaft_lean` (62 % on "driver").

### 3.3 `m_pelvisLiftTop` — 100 % fire, and the frames do not show it

−18 to −37 % of stance (≈ 8–15 cm of pelvis drop) on every shot of every club. Address and top
crops of the 9 Sep swing show the belt line perhaps 2–3 cm lower at the top, not 8. The hip
keypoints sit on the visible trochanter/thigh crease, which moves DOWN the body as the pelvis turns
and the lead knee flexes — the measure is reading keypoint migration. (The trail-hip-hike measure
on the same joints refuses the frame when the hip span foreshortens; this one does not.)
**Recommend:** gate `pelvisLift` on the same hip-span ratio `hipLineTilt` uses, or declare the low
tail unwatched pending a ruler check on a taped-belt session. Do not headline `pelvis_sink_backswing`
until then.

### 3.4 `m_comOverLeadFootFinish` — 71 % off-balance on visibly balanced finishes

Ceiling 15 + 10 % of stance; 7-iron median 46 %, driver 30 %. Every 9 Sep finish is stacked over the
lead foot in the frame. Either the `finish` anchor is an early post-impact tick rather than the
held finish, or the CoM proxy is off. **Recommend:** investigate the anchor before touching the
corridor; treat `off_balance_finish` as unreliable meanwhile.

## 4. Data quality: the corpus club labels

Sessions 2026-07-03/04/05/08/09/10 are labelled **DRIVER** and their address frames show an
iron — a short, taped shaft with an iron head (the memory note says the taped 7-iron). That is
**64 of 123 shots graded against driver corridors**: `stance_narrow` 95 % (driver norm 115 % of
shoulder width vs the iron 102 — the same 84 % stance grades Watch under the iron row),
`excessive_shaft_lean` 62 %, `smash_deficit` 75 % (1.35 is a fine 7-iron smash), `ball_forward` /
`ball_back` split 38/62 on impossible −48…+93 % positions. Nothing in the driver rows can be
judged until the labels are right. **Recommend:** set `capture.club` to 7 IRON on those sessions
(a `swing.json` edit under backup) and re-grade; then re-read §2/§3 for the driver context.
Add plausibility caps 0–100 to `m_ballPosition` regardless.

## 5. Chronic patterns the corridors are right about

These fire on most shots AND the video agrees, so the corridor is doing its job on a golfer with
a stable fault: `sway` (pelvis −22 % of stance on every club; the video read is −21), `reverse_
spine_p4` (§2.4), `lead_knee_drifts_in_at_top` (−18 %; visible), `hanging_back` on the 6-iron
(chest −17 %; the head falls 10 cm behind by P8), `flying_elbow` in July (the July top frame shows
the trail elbow well above the shoulder; 9 Sep reads 10 % and shows it tucked — a genuine change
between sessions, not a measurement), `cupped_at_top` on the wrist-sensor shots (−17°, plausible).
`sway` and `lead_knee` corridors are `heuristic` and centred at 0/−5; a second golfer would tell
us whether −18 is chronic or ordinary.

## 6. The new producer, and what it showed

`clubheadPeakLead` went live today (kinematic_series.cpp `peakLeadSeries`): the time of the
composed clubhead speed's maximum, searched from the Top tick to the P7 knot, in ms before the
knot. On the 9 Sep swings: 2.8, 54.9, 1.4, 1.6, 0.3, 1.8, 0.1 ms — `deceleration` fires once (the
55 ms swing, whose speed dips 69 → 58 → 65 mph over the last 60 ms) and is clean on six, which is
the discrimination hand speed could never give. Two cautions for the corridor (5 + 15 ms, ceiling,
heuristic): the composed speed plateaus over the last ~60 ms, so a ±3 mph tracker wobble can move
the argmax by tens of ms — a more robust producer would report the LAST time the speed rose above
~97 % of its peak; and the corpus figures it was seated on (median 3.7, p95 49) come from the
pre-re-run shaft stage. Re-seat after a corpus pass with the new stage.

## 7. The change list to decide over

| # | change | class | effort | files |
|---|---|---|---|---|
| 1 | Negate the `m_leadWristRadUln_p2..p8` rows, swap the two set signals, fix the descriptor and two source comments | sign | S | norms.json, core.json, manifest, wrist_angles.h, axis_direction_test |
| 2 | Relabel the six July sessions 7 IRON and re-grade | data | S (studio, backup) | swing.json ×58, regrade_ledger |
| 3 | `m_shoulderPlane` → noProducer on a planned pair series | measure | S | core.json, manifest, tests (counts) |
| 4 | Drop the camera fallback on `m_attackAngle` / `m_lowPointAhead`, or plausibility caps | measure | S | core.json (preferKeys / plausible), norm rows |
| 5 | `m_leadHandWidth` floor mu 65 → 90 ± 8 | scale | XS | norms.json |
| 6 | `m_leadUpperArmToChest` window P1–P3, re-seat | scale | S | core.json, norms.json, live test pins |
| 7 | `m_axisTiltImpact` re-seat as 2-D (≈10 ± 6 / 15 ± 8) with citation | scale | XS | norms.json |
| 8 | `m_faceToPath` ±2.5, `m_lmLaunchDirection` ±3 | scale | XS | norms.json |
| 9 | `m_pelvisLiftTop` gated on hip-span ratio, or low tail unwatched | measure | S–M | lower_body producer or core.json |
| 10 | `m_ballPosition` plausible 0–100 | plausibility | XS | norms.json |
| 11 | Investigate the `finish` anchor behind `m_comOverLeadFootFinish` | investigation | M | — |
| 12 | Calibrate tracker `impactShaftLean` against hand-marked P7 (9 Sep: 2×) | producer | M | shaft tracker |
| 13 | `clubheadPeakLead`: robust peak (last time ≥ 97 % of max); re-seat after a corpus pass | producer | S | kinematic_series.cpp, norms.json |
| 14 | Forearm-rotation grid: mark the eight rows unverified until a sensor session reads them | provenance | XS | norms.json citations |

Items 1, 3, 4, 5, 7, 8, 10 and 14 are content-only and can go in one commit with the count
re-pins; 2 is a data pass on the studio; 6 changes what a live measure reads and moves the
coverage pins; 9, 11, 12, 13 are producer work.

## Appendix A — every graded measure, corpus distribution vs corridor

Fresh ledgers, 123 shots, per (measure, context) with n ≥ 5. `<−2z` / `>+2z` are the shares of
shots beyond two tolerances each side; `**` marks rows over 40 % combined.

| measure | ctx | n | mu | σlo | σhi | shape | p5 | median | p95 | <−2z | >+2z |
|---|---|---|---|---|---|---|---|---|---|---|---|
| m_attackAngle ** | driver | 64 | 2 | 3 | 3 | two-sided | -65.2 | -7.0 | 37.5 | 52% | 27% |
| m_attackAngle ** | iron_6 | 14 | 0 | 3 | 3 | two-sided | -33.0 | -5.1 | 45.6 | 43% | 43% |
| m_attackAngle | iron_7 | 32 | 0 | 3 | 3 | two-sided | -75.2 | -3.2 | 7.7 | 25% | 9% |
| m_attackAngle ** | wedge_gap | 9 | 0 | 3 | 3 | two-sided | -52.5 | -25.5 | 13.3 | 67% | 33% |
| m_axisTiltAtTop ** | driver | 56 | 12 | 6 | 6 | two-sided | -11.7 | -8.1 | -1.2 | 98% | 0% |
| m_axisTiltAtTop ** | iron_6 | 14 | 12 | 6 | 6 | two-sided | -14.3 | -5.7 | -1.3 | 100% | 0% |
| m_axisTiltAtTop ** | iron_7 | 17 | 12 | 6 | 6 | two-sided | -13.5 | -6.4 | -0.5 | 94% | 0% |
| m_axisTiltAtTop ** | wedge_gap | 9 | 12 | 6 | 6 | two-sided | -18.0 | -11.1 | -7.4 | 100% | 0% |
| m_axisTiltImpact ** | driver | 56 | 30 | 8 | 8 | two-sided | 3.9 | 6.3 | 8.2 | 100% | 0% |
| m_axisTiltImpact ** | iron_6 | 14 | 25 | 8 | 8 | two-sided | 5.2 | 7.1 | 8.8 | 100% | 0% |
| m_axisTiltImpact ** | iron_7 | 17 | 25 | 8 | 8 | two-sided | 5.0 | 6.1 | 7.3 | 100% | 0% |
| m_axisTiltImpact ** | wedge_gap | 9 | 25 | 8 | 8 | two-sided | -0.1 | 4.9 | 7.4 | 89% | 0% |
| m_ballPosition ** | driver | 24 | 5 | 8 | 8 | two-sided | -47.7 | 35.4 | 92.8 | 38% | 62% |
| m_ballPosition | iron_6 | 10 | 30 | 14 | 14 | two-sided | 34.9 | 52.9 | 65.2 | 0% | 20% |
| m_ballPosition | iron_7 | 13 | 30 | 14 | 14 | two-sided | 8.3 | 26.0 | 46.4 | 0% | 8% |
| m_clubheadPeakLead | iron_6 | 7 | 5 | 15 | 15 | ceiling | 0.1 | 1.6 | 2.8 | 0% | 14% |
| m_comOverLeadFootFinish | driver | 56 | 15 | 10 | 10 | ceiling | 3.7 | 29.9 | 56.7 | 0% | 39% |
| m_comOverLeadFootFinish | iron_6 | 14 | 15 | 10 | 10 | ceiling | 17.0 | 23.9 | 42.7 | 0% | 14% |
| m_comOverLeadFootFinish ** | iron_7 | 17 | 15 | 10 | 10 | ceiling | 2.3 | 45.7 | 54.7 | 0% | 71% |
| m_comOverLeadFootFinish | wedge_gap | 9 | 15 | 10 | 10 | ceiling | 7.1 | 25.7 | 41.4 | 0% | 33% |
| m_faceToPath ** | driver | 8 | 0 | 1 | 1 | two-sided | -3.8 | 0.7 | 2.1 | 38% | 25% |
| m_faceToPath | iron_7 | 15 | 0 | 1 | 1 | two-sided | -2.8 | -0.9 | 3.1 | 13% | 13% |
| m_feetAlignment | driver | 56 | 0 | 4 | 4 | two-sided | -0.6 | 2.0 | 3.7 | 0% | 0% |
| m_feetAlignment | iron_6 | 14 | 0 | 4 | 4 | two-sided | 0.4 | 1.6 | 3.1 | 0% | 0% |
| m_feetAlignment | iron_7 | 17 | 0 | 4 | 4 | two-sided | -0.1 | 2.6 | 3.6 | 0% | 0% |
| m_feetAlignment | wedge_gap | 9 | 0 | 4 | 4 | two-sided | -0.3 | 1.3 | 3.3 | 0% | 0% |
| m_headLiftBack | driver | 56 | 0 | 3 | 3 | two-sided | -1.1 | 0.6 | 2.2 | 0% | 0% |
| m_headLiftBack | iron_6 | 14 | 0 | 3 | 3 | two-sided | -0.3 | 0.4 | 1.1 | 0% | 0% |
| m_headLiftBack | iron_7 | 17 | 0 | 3 | 3 | two-sided | -0.7 | 0.6 | 1.4 | 0% | 0% |
| m_headLiftBack | wedge_gap | 9 | 0 | 3 | 3 | two-sided | -4.3 | 1.1 | 2.2 | 0% | 0% |
| m_headLiftDown | driver | 56 | 0 | 3 | 3 | two-sided | 2.2 | 3.5 | 4.6 | 0% | 0% |
| m_headLiftDown | iron_6 | 14 | 0 | 3 | 3 | two-sided | 2.4 | 3.3 | 4.8 | 0% | 0% |
| m_headLiftDown | iron_7 | 17 | 0 | 3 | 3 | two-sided | 1.7 | 3.2 | 3.9 | 0% | 0% |
| m_headLiftDown | wedge_gap | 8 | 0 | 3 | 3 | two-sided | -3.0 | 2.1 | 2.7 | 0% | 0% |
| m_headSwayBack | driver | 56 | -4 | 3 | 3 | two-sided | -5.6 | -3.6 | -1.5 | 0% | 0% |
| m_headSwayBack | iron_6 | 14 | -4 | 3 | 3 | two-sided | -5.3 | -4.0 | -2.3 | 0% | 0% |
| m_headSwayBack | iron_7 | 17 | -4 | 3 | 3 | two-sided | -6.2 | -4.3 | -2.3 | 0% | 0% |
| m_headSwayBack | wedge_gap | 9 | -4 | 3 | 3 | two-sided | -7.3 | -1.7 | 1.2 | 0% | 0% |
| m_hipAlignment | driver | 56 | 0 | 5 | 5 | two-sided | -3.3 | -1.4 | 0.7 | 0% | 0% |
| m_hipAlignment | iron_6 | 14 | 0 | 5 | 5 | two-sided | -6.6 | -3.6 | -1.9 | 0% | 0% |
| m_hipAlignment | iron_7 | 17 | 0 | 5 | 5 | two-sided | -5.7 | -4.0 | 1.1 | 0% | 0% |
| m_hipAlignment | wedge_gap | 9 | 0 | 5 | 5 | two-sided | -3.8 | -1.3 | -1.2 | 0% | 0% |
| m_hipLineTiltTop | driver | 56 | 10 | 7 | 7 | two-sided | 8.1 | 10.9 | 14.9 | 0% | 0% |
| m_hipLineTiltTop | iron_6 | 14 | 10 | 7 | 7 | two-sided | 5.1 | 7.4 | 9.1 | 0% | 0% |
| m_hipLineTiltTop | iron_7 | 17 | 10 | 7 | 7 | two-sided | 7.1 | 9.0 | 13.2 | 0% | 0% |
| m_hipLineTiltTop | wedge_gap | 9 | 10 | 7 | 7 | two-sided | 8.4 | 11.1 | 14.8 | 0% | 0% |
| m_impactShaftLean ** | driver | 56 | 0 | 4 | 6 | two-sided | 5.0 | 14.5 | 28.0 | 0% | 62% |
| m_impactShaftLean | iron_6 | 14 | 5 | 4 | 6 | two-sided | 4.0 | 11.0 | 19.0 | 0% | 14% |
| m_impactShaftLean | iron_7 | 17 | 5 | 4 | 6 | two-sided | 2.5 | 9.0 | 18.9 | 0% | 12% |
| m_impactShaftLean ** | wedge_gap | 9 | 5 | 4 | 6 | two-sided | 17.0 | 33.0 | 52.0 | 0% | 100% |
| m_lagAngleDown | driver | 56 | 70 | 22 | 30 | two-sided | 43.8 | 48.2 | 53.7 | 0% | 0% |
| m_lagAngleDown | iron_6 | 14 | 70 | 22 | 30 | two-sided | 45.9 | 51.8 | 61.8 | 0% | 0% |
| m_lagAngleDown | iron_7 | 17 | 70 | 22 | 30 | two-sided | 42.6 | 50.5 | 55.9 | 0% | 0% |
| m_lagAngleDown | wedge_gap | 8 | 70 | 22 | 30 | two-sided | 39.7 | 42.6 | 47.1 | 0% | 0% |
| m_leadArmToTorso | driver | 56 | 20 | 10 | 10 | ceiling | 12.8 | 16.9 | 20.2 | 0% | 0% |
| m_leadArmToTorso | iron_6 | 14 | 20 | 10 | 10 | ceiling | 22.7 | 23.5 | 27.6 | 0% | 0% |
| m_leadArmToTorso | iron_7 | 17 | 20 | 10 | 10 | ceiling | 8.8 | 21.6 | 26.6 | 0% | 0% |
| m_leadArmToTorso | wedge_gap | 9 | 20 | 10 | 10 | ceiling | -12.0 | 15.6 | 19.8 | 22% | 0% |
| m_leadHandWidth ** | driver | 56 | 65 | 10 | 10 | floor | 85.8 | 90.4 | 94.2 | 0% | 98% |
| m_leadHandWidth ** | iron_6 | 14 | 65 | 10 | 10 | floor | 89.6 | 95.4 | 125.7 | 0% | 100% |
| m_leadHandWidth ** | iron_7 | 17 | 65 | 10 | 10 | floor | 87.0 | 94.6 | 98.3 | 0% | 100% |
| m_leadHandWidth ** | wedge_gap | 9 | 65 | 10 | 10 | floor | 88.2 | 90.7 | 93.5 | 0% | 100% |
| m_leadHeelLiftTop | driver | 49 | 2 | 2 | 2 | ceiling | 0.1 | 1.5 | 3.1 | 0% | 0% |
| m_leadHeelLiftTop | iron_6 | 10 | 2 | 2 | 2 | ceiling | -1.4 | -0.7 | 2.0 | 0% | 0% |
| m_leadHeelLiftTop | iron_7 | 14 | 2 | 2 | 2 | ceiling | -0.9 | 0.3 | 3.7 | 0% | 0% |
| m_leadHeelLiftTop | wedge_gap | 8 | 2 | 2 | 2 | ceiling | 0.2 | 2.7 | 5.7 | 0% | 12% |
| m_leadKneeDriftImpact | driver | 56 | 0 | 8 | 8 | two-sided | -8.6 | -3.4 | 2.2 | 0% | 0% |
| m_leadKneeDriftImpact | iron_6 | 14 | 0 | 8 | 8 | two-sided | -4.9 | -1.1 | 4.4 | 0% | 0% |
| m_leadKneeDriftImpact | iron_7 | 17 | 0 | 8 | 8 | two-sided | -6.7 | -3.0 | 1.6 | 0% | 0% |
| m_leadKneeDriftImpact | wedge_gap | 9 | 0 | 8 | 8 | two-sided | -9.7 | -6.5 | -2.0 | 0% | 0% |
| m_leadKneeDriftTop ** | driver | 56 | 0 | 8 | 8 | two-sided | -23.9 | -17.9 | -10.8 | 59% | 0% |
| m_leadKneeDriftTop ** | iron_6 | 14 | 0 | 8 | 8 | two-sided | -24.0 | -17.9 | -15.9 | 86% | 0% |
| m_leadKneeDriftTop ** | iron_7 | 17 | 0 | 8 | 8 | two-sided | -26.8 | -18.1 | -12.3 | 71% | 0% |
| m_leadKneeDriftTop | wedge_gap | 9 | 0 | 8 | 8 | two-sided | -13.8 | -11.7 | -7.6 | 0% | 0% |
| m_leadUpperArmToChest ** | driver | 56 | 25 | 12 | 12 | two-sided | 54.7 | 61.6 | 67.8 | 0% | 100% |
| m_leadUpperArmToChest ** | iron_6 | 14 | 25 | 12 | 12 | two-sided | 65.1 | 68.6 | 73.0 | 0% | 100% |
| m_leadUpperArmToChest ** | iron_7 | 17 | 25 | 12 | 12 | two-sided | 53.0 | 66.3 | 73.9 | 0% | 100% |
| m_leadUpperArmToChest ** | wedge_gap | 9 | 25 | 12 | 12 | two-sided | 53.4 | 71.1 | 81.6 | 0% | 100% |
| m_leadWristFlexExt_p4 ** | iron_7 | 10 | 5 | 11 | 11 | two-sided | -23.8 | -17.1 | -12.1 | 50% | 0% |
| m_leadWristFlexExt_p7 | iron_7 | 10 | 8 | 7 | 7 | two-sided | -10.9 | -1.8 | 4.8 | 30% | 0% |
| m_leadWristRadUln_p4 ** | iron_7 | 10 | 38 | 12 | 12 | two-sided | -53.1 | -44.6 | -40.5 | 100% | 0% |
| m_lmLaunchAngle | driver | 8 | 13 | 3 | 3 | two-sided | 9.9 | 12.1 | 14.9 | 0% | 12% |
| m_lmLaunchAngle | iron_7 | 27 | 15 | 5 | 5 | two-sided | 15.1 | 20.2 | 22.2 | 0% | 0% |
| m_lmLaunchDirection | driver | 8 | 0 | 2 | 2 | two-sided | -8.7 | -2.5 | 1.8 | 25% | 0% |
| m_lmLaunchDirection | iron_7 | 27 | 0 | 2 | 2 | two-sided | -4.1 | -0.9 | 1.3 | 7% | 0% |
| m_lowPointAhead ** | driver | 46 | -1.2 | 1.6 | 1.6 | two-sided | -16.7 | -10.7 | 1.6 | 67% | 7% |
| m_lowPointAhead | iron_6 | 14 | 2 | 1.6 | 1.6 | two-sided | -19.3 | 0.7 | 4.8 | 29% | 0% |
| m_lowPointAhead ** | iron_7 | 17 | 2 | 1.6 | 1.6 | two-sided | -2.9 | -0.4 | 10.8 | 24% | 18% |
| m_lowPointAhead ** | wedge_gap | 8 | 2 | 1.6 | 1.6 | two-sided | -20.0 | 15.3 | 23.0 | 25% | 75% |
| m_pelvisLiftTop ** | driver | 56 | 0 | 6.2 | 6.2 | two-sided | -30.3 | -24.8 | -17.7 | 100% | 0% |
| m_pelvisLiftTop ** | iron_6 | 14 | 0 | 6.2 | 6.2 | two-sided | -20.3 | -17.9 | -15.1 | 100% | 0% |
| m_pelvisLiftTop ** | iron_7 | 17 | 0 | 6.2 | 6.2 | two-sided | -37.1 | -28.1 | -21.2 | 100% | 0% |
| m_pelvisLiftTop ** | wedge_gap | 9 | 0 | 6.2 | 6.2 | two-sided | -29.5 | -28.0 | -26.6 | 100% | 0% |
| m_pelvisSwayBack ** | driver | 56 | -5 | 7.5 | 7.5 | two-sided | -27.4 | -21.5 | -15.4 | 57% | 0% |
| m_pelvisSwayBack ** | iron_6 | 14 | -5 | 7.5 | 7.5 | two-sided | -26.4 | -22.9 | -19.3 | 86% | 0% |
| m_pelvisSwayBack ** | iron_7 | 17 | -5 | 7.5 | 7.5 | two-sided | -31.2 | -22.6 | -18.9 | 82% | 0% |
| m_pelvisSwayBack ** | wedge_gap | 9 | -5 | 7.5 | 7.5 | two-sided | -24.3 | -16.8 | -16.0 | 44% | 0% |
| m_pelvisSwayDown | driver | 53 | 20 | 10 | 10 | two-sided | 9.7 | 18.3 | 23.8 | 0% | 0% |
| m_pelvisSwayDown | iron_6 | 14 | 20 | 10 | 10 | two-sided | 6.8 | 11.8 | 16.3 | 0% | 0% |
| m_pelvisSwayDown | iron_7 | 17 | 20 | 10 | 10 | two-sided | 9.6 | 16.0 | 20.7 | 0% | 0% |
| m_pelvisSwayDown | wedge_gap | 8 | 20 | 10 | 10 | two-sided | 16.5 | 22.5 | 26.1 | 0% | 0% |
| m_pelvisSwayImpact | driver | 52 | 7.5 | 10 | 10 | two-sided | 9.7 | 18.7 | 24.6 | 0% | 6% |
| m_pelvisSwayImpact | iron_6 | 8 | 7.5 | 10 | 10 | two-sided | 9.6 | 10.5 | 16.3 | 0% | 0% |
| m_pelvisSwayImpact | iron_7 | 16 | 7.5 | 10 | 10 | two-sided | 9.6 | 16.1 | 20.8 | 0% | 0% |
| m_pelvisSwayImpact | wedge_gap | 8 | 7.5 | 10 | 10 | two-sided | 16.5 | 22.8 | 26.1 | 0% | 0% |
| m_shaftAngleP4 | driver | 56 | 10 | 15 | 15 | two-sided | -35.9 | -14.2 | -3.2 | 23% | 2% |
| m_shaftAngleP4 | iron_6 | 14 | 0 | 15 | 15 | two-sided | -34.5 | -13.5 | -11.4 | 14% | 0% |
| m_shaftAngleP4 | iron_7 | 17 | 0 | 15 | 15 | two-sided | -48.0 | -6.9 | -0.5 | 12% | 0% |
| m_shaftAngleP4 | wedge_gap | 9 | 0 | 15 | 15 | two-sided | -36.2 | -21.7 | -17.5 | 11% | 0% |
| m_shoulderPlane | driver | 30 | 50 | 10 | 10 | two-sided | 9.4 | 33.6 | 43.2 | 37% | 0% |
| m_shoulderPlane ** | iron_6 | 10 | 50 | 10 | 10 | two-sided | 5.6 | 14.7 | 27.8 | 100% | 0% |
| m_shoulderPlane ** | iron_7 | 9 | 50 | 10 | 10 | two-sided | 5.4 | 19.6 | 40.0 | 56% | 0% |
| m_shoulderPlane ** | wedge_gap | 9 | 50 | 10 | 10 | two-sided | 8.9 | 17.4 | 28.2 | 89% | 0% |
| m_smashFactor ** | driver | 8 | 1.48 | 0.05 | 0.05 | floor | 1.3 | 1.4 | 1.4 | 75% | 0% |
| m_smashFactor | iron_7 | 21 | 1.4 | 0.08 | 0.08 | floor | 1.3 | 1.3 | 1.4 | 5% | 0% |
| m_spinAxis | driver | 8 | 0 | 5 | 5 | two-sided | -7.8 | 2.3 | 15.7 | 0% | 38% |
| m_spinAxis | iron_7 | 27 | 0 | 5 | 5 | two-sided | -4.3 | 2.1 | 8.5 | 0% | 7% |
| m_spinRate | driver | 8 | 2600 | 600 | 600 | two-sided | 1964.0 | 2374.0 | 3034.0 | 0% | 12% |
| m_spinRate | iron_7 | 27 | 5000 | 2000 | 2000 | two-sided | 3424.0 | 4814.0 | 5809.0 | 0% | 0% |
| m_stanceWidth ** | driver | 56 | 115 | 10 | 10 | two-sided | 76.8 | 84.0 | 94.3 | 95% | 0% |
| m_stanceWidth | iron_6 | 14 | 102 | 12 | 12 | two-sided | 83.4 | 87.9 | 88.7 | 0% | 0% |
| m_stanceWidth | iron_7 | 17 | 102 | 12 | 12 | two-sided | 72.3 | 82.3 | 88.4 | 12% | 0% |
| m_stanceWidth | wedge_gap | 9 | 102 | 12 | 12 | two-sided | 75.4 | 80.9 | 82.3 | 33% | 0% |
| m_strikeLocation | driver | 8 | 0 | 8 | 8 | two-sided | -8.7 | 3.5 | 17.1 | 0% | 38% |
| m_strikeLocation | iron_7 | 15 | 0 | 8 | 8 | two-sided | -16.5 | -4.4 | 8.1 | 7% | 0% |
| m_tempoRatio | driver | 56 | 2.6 | 0.4 | 0.4 | two-sided | 2.3 | 2.5 | 2.7 | 0% | 0% |
| m_tempoRatio | iron_6 | 14 | 2.6 | 0.4 | 0.4 | two-sided | 2.5 | 2.7 | 3.5 | 0% | 14% |
| m_tempoRatio | iron_7 | 17 | 2.6 | 0.4 | 0.4 | two-sided | 2.3 | 2.6 | 2.9 | 0% | 0% |
| m_tempoRatio | wedge_gap | 9 | 2.6 | 0.4 | 0.4 | two-sided | 2.0 | 2.4 | 2.8 | 0% | 0% |
| m_thoraxDrift | driver | 56 | 0 | 10 | 10 | two-sided | -21.6 | -5.4 | 22.9 | 7% | 9% |
| m_thoraxDrift ** | iron_6 | 14 | 0 | 10 | 10 | two-sided | -28.3 | -16.8 | -7.4 | 43% | 0% |
| m_thoraxDrift | iron_7 | 17 | 0 | 10 | 10 | two-sided | -20.5 | -12.3 | 0.5 | 6% | 0% |
| m_thoraxDrift | wedge_gap | 8 | 0 | 10 | 10 | two-sided | -19.1 | -6.4 | -4.3 | 0% | 0% |
| m_trailElbowRise ** | driver | 30 | 0 | 15 | 15 | two-sided | 4.9 | 60.1 | 70.7 | 0% | 77% |
| m_trailElbowRise | iron_6 | 10 | 0 | 15 | 15 | two-sided | -1.0 | 10.2 | 23.4 | 0% | 0% |
| m_trailElbowRise ** | iron_7 | 9 | 0 | 15 | 15 | two-sided | 15.1 | 38.2 | 60.9 | 0% | 78% |
| m_trailElbowRise ** | wedge_gap | 9 | 0 | 15 | 15 | two-sided | 7.9 | 39.1 | 55.6 | 0% | 56% |
| m_transitionPlaneDelta | driver | 56 | 0 | 25 | 25 | two-sided | -10.0 | 0.7 | 10.5 | 0% | 0% |
| m_transitionPlaneDelta | iron_6 | 14 | 0 | 25 | 25 | two-sided | -3.1 | 2.9 | 13.2 | 0% | 0% |
| m_transitionPlaneDelta | iron_7 | 17 | 0 | 25 | 25 | two-sided | -3.4 | 6.5 | 15.1 | 0% | 0% |
| m_transitionPlaneDelta | wedge_gap | 9 | 0 | 25 | 25 | two-sided | -40.7 | 1.4 | 3.1 | 0% | 0% |

## Appendix B — conditions firing on ≥ 15 % of the shots they were assessed on

| condition | fired | assessed | rate |
|---|---|---|---|
| pelvis_sink_backswing | 96 | 96 | 100% |
| disconnection | 96 | 96 | 100% |
| reverse_spine_p4 | 94 | 96 | 98% |
| sway | 62 | 96 | 65% |
| stance_narrow | 61 | 96 | 64% |
| lead_knee_drifts_in_at_top | 57 | 96 | 59% |
| reverse_spine_p7 | 56 | 96 | 58% |
| reverse_pivot | 55 | 96 | 57% |
| attack_too_steep | 53 | 119 | 45% |
| low_point_behind_ball | 49 | 85 | 58% |
| excessive_shaft_lean | 42 | 96 | 44% |
| off_balance_finish | 39 | 96 | 41% |
| flying_elbow | 35 | 58 | 60% |
| flat_shoulder_plane | 34 | 58 | 59% |
| attack_too_shallow | 29 | 119 | 24% |
| ball_back | 21 | 50 | 42% |
| club_short_of_parallel | 15 | 96 | 16% |
| ball_forward | 12 | 50 | 24% |
| thin | 10 | 23 | 43% |
| insufficient_set | 10 | 10 | 100% |
| cupped_at_top | 9 | 10 | 90% |
| smash_deficit | 8 | 29 | 28% |
| closed_face_to_path | 5 | 23 | 22% |
| open_face_to_path | 4 | 23 | 17% |
| scooping | 4 | 10 | 40% |
