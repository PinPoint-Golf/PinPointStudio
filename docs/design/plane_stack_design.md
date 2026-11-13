# The plane stack: set-up, turn, takeaway, arms and club, each judged against the layer beneath

Design, 13 Nov 2026. Status: **approved, being built**. Mark approved the frame, the links and the order on 13 Nov (§10). The norms are provisional and heuristic.

Related:
- `docs/design/planned_metrics_promotion.md` §6 W7: the shoulder tilt against posture, which is layer 1 here and is already built;
- `docs/design/swing_plane_area.md`: the plane readings and the on-plane window;
- `docs/design/shaft_fusion_design.md`: why a plane's inclination is camera-safe.

## 1. Why

The backswing plane a golfer ends up on is a sum of choices:
- how they set up;
- whether the shoulders turn on the spine;
- where the club starts back;
- how far the arms lift against the turn;
- what the wrists and trail elbow do to the club on the arms.

Today the model grades the end result twice: an absolute plane against the club's norm, and the climb from the golfer's own address plane. It has no reading for the arms against the turn, and its takeaway reads a direction that carries the down-the-line camera's direction error. The result is a flat plane with no named cause, faults that disagree with each other (Jess, 13 Nov: absolute says flat, relative says fine), and no way to express "level shoulders with the arms lifting" (Mark).

**The idea.** Measure each layer against the one beneath it. Each fault then has one cause and one fix, and the causal links run up the stack.

| Layer | Question | Reading | Today |
|---|---|---|---|
| 0. Set-up | What plane did the golfer set? | the shaft plane at address; posture, hands, ball distance | posture and set-up faults exist |
| 1. Turn | Do the shoulders turn on the spine? | `shoulderTiltVsBend` (tilt at the top − forward bend at address) | **built 13 Nov (ab99dc53)** |
| 2. Takeaway | Does the club start back on the plane it was set on? | the shaft plane at P2 − at address | **new measure** (pack only) |
| 3. Arms | Do the arms lift with the turn, above it, or swing around under it? | the lead arm against the shoulder-turn plane at the top | **new producer** |
| 4. Club on arms | Wrist set and trail elbow | laid off / across the line, trail elbow deep / flying, `m_trailElbowRise` | exist |
| Result | Where did the club go? | the climb at lead-arm-parallel, placed in the **on-plane window** halfway back | **new derived reading** |
| Context | Where is that for this club? | the absolute plane at P3 against the club's norm | **shown, not graded** |

## 2. Evidence from the library (13 Nov, read-only; 283 swings, 206 with a two-camera fit)

The scripts, per-swing CSV and full results are in `docs/research/data/plane_stack/` (`RESULTS.md`).
- The sampler reproduces the grader exactly: `m_shaftPlaneBackswing` on 156/156 swings, `m_backswingPlaneAngle` on 161/161, `m_shoulderTiltVsBend` on 205/205.
- Correlations are **within-session**: ranks taken within each session, then pooled, with permutation p. Camera set-ups change between sessions and make between-session correlations spurious; that was found on 13 Nov with shaft direction and the hand-path loop.
- **Chris is reported separately and left out of pooled statistics**, because his 3-D shoulder line fails (§6). He stays in the corpus as test material.

### 2.1 Medians per golfer

| | Mark (7i, GW) | Nick (7i) | Jess (8i) | Paddy (driver) |
|---|---|---|---|---|
| Takeaway: plane P2 − P1 | −3.1° [−4.0, −2.2] | −1.6° | **+1.6°** | – (club track refused) |
| Climb at P3: plane P3 − P1 | 8.6° | 13.1° | **2.2°** | – |
| Absolute plane at P3 | 65.6° | 65.8° | **54.9°** | – |
| Lead-arm inclination at the top (3-D) | 42.7° | 30.1° | **48.8°** | 22.2° |
| **Arm lift a2** (lead arm against the shoulder-turn plane) | 13.9° | 9.1° | **22.4°** | 7.9° |
| Arm lift b (down-the-line image) | 19.3° | 13.7° | 18.6° | 8.4° |
| Shoulder tilt − bend | −6.1° | −3.2° | −2.6° | −3.9° |
| Hand-path loop | 16.8 | 11.6 | 6.4 | 23.1 |
| Club path (LM) | −1.6° | +2.1° | +4.3° | +5.4° |

The spread between golfers is several times the spread within a session:

| Reading | Within-session SD | Golfer medians range |
|---|---|---|
| Takeaway | 1.4° | −3.1 to +1.6 |
| Climb | 3.9° | 2.2 to 13.1 |
| a2 | 4.5° | 7.9 to 22.4 |

These readings separate golfers; four golfers of unknown standard cannot say where "good" lies.

### 2.2 Within-session relations

| x | y | ρ | n | p |
|---|---|---|---|---|
| **Takeaway P2 − P1** | **hand-path loop** | **−0.51** | 168, 10 sessions | < 0.001 |
| Climb P3 − P1 | club path | −0.30 | 52 | 0.045 |
| Arm lift a2 / b | club path | −0.22 / −0.24 | 89 | 0.04 / 0.03 (borderline with 20 tests) |
| Arm lift (any) | spin axis, face to path, loop | about 0 | 60–194 | n.s. |
| Arm lift a2 | trail elbow rise | +0.43 | 89 | — |

- **A club taken back below its address plane goes with a bigger loop coming down.** That is the model's "inside takeaway → over the top" link, seen in the data and camera-safe: one camera, one day, two different trackers.
- **Mark** takes the club away 3.1° under his plane, the lowest of the four golfers, and over the top fires on 118 of his 127 shots. That makes it a root-cause candidate for his over the top.
- **More arm lift and more climb go weakly with a path further out-to-in**, the direction the "arms lift → over the top" link predicts.

### 2.3 What the stack shows that one plane number could not

- **Jess** sets the plane, keeps the club on it at lead-arm-parallel (+2°), and her arms then lift higher above her shoulder turn than anyone's (a2 22°). Her absolute plane at P3 (54.9°) is the lowest because the club stays on her set-up plane while Nick's and Mark's steepen. "Flat" was never in her arms. Her stronger stories are a short chest turn (73°) and a face shut to the path (−8°) on an in-to-out path.
- **Mark** turns the shoulders a little level (−6°), starts the club under the plane (−3°), and the arms lift. That is a level turn with the arms going up.

## 3. The readings

### 3.1 Layer 2: the takeaway against the set-up
- **`m_takeawayPlane`**: `swingPlaneCurve` at P2 minus at P1 (a composed measure: reducer `delta`, anchor p1, window p1..p2, as `m_shaftPlaneBackswing` is for p1..p3). Degrees.
  - Sign: + means the club starts back ABOVE the plane it was set on; − means below.
- **Camera-safe without calibration.** It is a plane inclination from one camera within one swing (shaft fusion: an inclination moves less than 1° under ±15° of camera turn).
- **Pack-only change:** no new producer.
- **Not assessable** where the club track was refused (Paddy's drivers, `club.trackRefused`), with that reason named.
- The existing `inside_takeaway` / `outside_takeaway` keep reading shaft DIRECTION at P2 (camera-yaw sensitive, with its ⚠) and stay as they are. Once calibration (#74) lands, direction and plane are two views of one takeaway.

### 3.2 Layer 3: arm lift against the turn
- **`armLiftTop`** (new producer): the angle between the lead UPPER ARM (lead shoulder → lead ELBOW; Mark, 13 Nov: a bent lead arm is a cheat that makes the arms look lifted, and a fault in its own right, graded by `bent_lead_arm`) and the **shoulder-turn plane** at the top.
  - The shoulder-turn plane is the plane containing the shoulder line, as square to the spine as a plane holding that line can be. Its normal is the spine vector (mid-hip → mid-shoulder) with its component along the shoulder line removed.
  - Value: asin(â · n̂). 0 means the arm lies in the plane the shoulders turn on (one-plane); + means the arm is lifted above it.
  - **Why this definition:** the simple difference of two inclinations is measured in two vertical planes that point different ways at the top, so it is dominated by the shoulder-tilt reading (within-session ρ −0.69 with the tilt). On Nick it gives −5° where the image shows +14°. This version is steadier (SD 4.5° against 5.9°) and agrees better with the image.
- **Routes:**
  - `faceOn+dtl` (3-D fit, Estimated): as above.
  - `dtl` (down-the-line image): the angle between the lead UPPER ARM (lead shoulder → lead ELBOW) and the shoulder line in the down-the-line frame at the top, + with the elbow above the shoulder line. It is camera-safe as a difference of two lines in one image, and it is the coach's own check.
  - **Precedence (revised after the 13 Nov frame sheet): the down-the-line image first while the cameras are uncalibrated; the 3-D fit once calibrated, or where there is no image reading.**
    - The sheet (Jess `Wrist_03/swing_0003` against Nick `Wrist_02/swing_0002`) showed the 3-D gap between them (a2 +24° against +7.5°) is mostly Nick's 3-D shoulder line: 40.9° in 3-D against 30.5° in the image.
    - The image gap is about 4° (b +19.3° against +14.9°), which is what the eye sees.
    - The 3-D shoulder tilt also moves with the session (Nick 37–41° on 10-20, 23.5° on 11-02).
    - To the wrist, the image ran about 4° above a2 overall, ρ 0.47 within a session. To the ELBOW (as built, D2) the image reads a median 1.3° BELOW the 3-D reading (IQR −5.7 … +4.3), ρ 0.46 within a session. It compresses the golfers' differences, but it is the coach's own check and safe to camera turn.
    - The image's weak point is the trail shoulder at the top, half hidden behind the lead arm (confidence about 0.7). Its σ must carry that.
  - The chest sensor can later supply the shoulder-turn plane's normal (its long axis), with the 3-D arm. That waits on #109.
- **Measured to the ELBOW, never the wrist** (Mark, 13 Nov). Mark's 3-D lead elbow reads 31° bent at the top (§6, real: the fit lands a median 3 px from his face-on elbow keypoint). A shoulder → wrist line reads that bend as lift: to the wrist, Mark's image reading is 12° higher and Nick's 15° higher than to the elbow. Every description of the reading says it is measured to the elbow and why.
- **σ:** the joints' (3-D) or keypoints' (image) σ through the angle. In the image each keypoint's smoother σ is multiplied by max(1, 0.8 / confidence), the 3-D fit's own rule (`body3d.confSigmaRef`), so the half-hidden trail shoulder (≈ 0.7) carries more; the library's image σ is a median 1.1–2.0° per golfer.

### 3.3 The result: the climb, placed in the on-plane window halfway back
- **Terminology (Mark, 13 Nov 2026).** This reading was first called "the slot". In golf the slot is a DOWNSWING position (halfway down, the club approaching from inside, the trail elbow near the trail hip, the shaft flatter than at the same point going back). This is a BACKSWING reading at lead-arm-parallel, so everything a golfer or coach reads calls it the **on-plane window** or **halfway back**. The code identifiers (`clubSlotTop`, `club_slot_top.h`, `club.slotUnread`, `club.slotCameraAssumed`, `club.slotMinWidthDeg`) keep "slot": they are stored keys, record ids and override names.
- **`clubSlotTop`** (new derived reading, one value at P3): (plane at P3 − plane at P1) / (shoulder plane at address − plane at P1).
  - **0** means the club sits on the plane it was set on (the lower edge of the on-plane window).
  - **1** means it sits on the plane through the ball and the trail shoulder at address (the upper edge; `shoulderPlane`, 29 Oct, 747f1bc1).
  - This is the on-plane window halfway back made a number. It is per swing, so it follows each golfer's set-up.
- **Verified (D3, §11):** `shoulderPlane` is in the metrics list (`analysis.metrics`, key `shoulderPlane`, one phase sample at Address with its σ), written by `ShaftFusionStage` wherever the down-the-line ball was found: 174 of 283 library swings, every swing with a found ball and none without. The 2026-10-20 documents without it are Mark's (no down-the-line camera) and 3 of Jess's (no ball found). Nothing had to be persisted.
- **As built (D3):** `club_slot_top.h`, derived once after the run, each part read as the grader reads it; σ first order through the ratio; not produced, with `club.slotUnread` naming why, when a part is missing or the window is under 4° wide (`club.slotMinWidthDeg`). The pair grades it; the norm is μ 0.5, tolerance 0.25, firing below 0 and above 1. While the down-the-line camera is uncalibrated it carries the Downgraded `club.slotCameraAssumed` ⚠ (D6 round 2): compare it within a session, not between sessions.
- Without it, the climb itself (`m_shaftPlaneBackswing`) is still shown, but it no longer grades: where the shoulder plane is missing the pair is not assessable, with the reason named.

### 3.4 Context
- `m_backswingPlaneAngle` (the absolute plane at P3) stays live and SHOWN against the club's corridor. It no longer grades, matching the absolute shoulder plane on 13 Nov.

## 4. Conditions, names, norms

**Names** (the golfer line first, then the coach's label):

| id | Label | Golfer words | Measure, side |
|---|---|---|---|
| `flat_takeaway` | Flat takeaway | "the club starts back below the plane you set it on" | `m_takeawayPlane` low |
| `steep_takeaway` | Steep takeaway | "the club starts back above the plane you set it on" | `m_takeawayPlane` high |
| `arms_swing_around` | Arms swing around | "your arms sit low against your shoulder turn at the top" | `m_armLiftTop` low |
| `arms_lift` | Arms lift | "your arms lift high above your shoulder turn at the top" | `m_armLiftTop` high |
| `flat_backswing_plane` → relabelled | Club under plane halfway back | "halfway back, the club sits below the plane you set at address" | `m_clubSlotTop` low (was `m_shaftPlaneBackswing`) |
| `steep_backswing_plane` → relabelled | Club above plane halfway back | "halfway back, the club climbs above the line through your trail shoulder" | `m_clubSlotTop` high |
| `backswing_plane_too_flat` / `_too_upright` | retired | — | their measure stays shown (§3.4) |

The ids of the two relabelled conditions are kept, so existing ledgers and edges stay valid.

**Norms (provisional, heuristic, and each one is Mark's decision):**

| Measure | μ | Corridor | Basis |
|---|---|---|---|
| `m_takeawayPlane` | 0 | **fires below −2 / above +3** (σLo 1, σHi 1.5; amber from −1 / +1.5). D1 shipped −2 … +3 as the amber corridor, firing at −4 / +6; Mark moved the firing edges in on 13 Nov evening (§10, §11 D6) | Coaching: the club starts back on or just above the address shaft plane ("stay above the rod"), and getting under it is the common amateur fault. The library has within-session SD 1.4° and golfer medians −3.1 … +1.6, so the band is narrower on the flat side. Within a session the hand-path loop rises steadily as the takeaway flattens: by golfer the median loop steps from 11.5 % (1–2° under) to 15.9 % (2–3° under), and over the top surfaces at about 9 % of loop and is firm at 13 %. |
| `m_armLiftTop` (as built, D2, to the ELBOW) | 5 (image, the row in `norms.json`); 7 (3-D, proposed, not loadable) | −3 … +12, firing below −11 / above +19 (image); −2 … +16, firing below −11 / above +25 (3-D) | **The literature has no number.** On the shoulder line is "one-plane", above it is "two-plane"; one instructor ties it to wingspan, and an informal count put most tour players above the line. Re-seated on the UPPER ARM from the library (§11 D2): golfer-weighted p2.5 / p97.5 −11.4 / +19.6 (image) and −12.3 / +26.8 (3-D), so only the extremes fire. A norm row cannot be keyed by route (§11 D2), so the 3-D reading, where it stands, is judged against the image row with a Downgraded record. The first figures here (image 15, corridor 7 … 23; 3-D 12, corridor 5 … 24) were measured to the WRIST and are replaced. |
| `m_leadElbowBendTop3d` (D2; a change from the address hold since D6) | 0 | ceiling, tolerance 8: amber from +8, firing above +16 | `bent_lead_arm`'s 3-D reading, `leadElbowBendChange3d`: the bend at the top LESS the median bend over the address hold, both where both cameras saw the elbow. The fit reads a straight address arm at 5–19° by golfer (Chris 18.6°), so D2's absolute reading against a fixed μ 15 (amber from 23, firing above 31) gave some golfers a head start; the change keeps D2's 16° firing distance from each golfer's own straight arm. §11 D6. |
| `m_clubSlotTop` (as built, D3) | 0.5 | 0.25 … 0.75 (tolerance 0.25), amber outside it, firing below 0 / above 1 | The on-plane window halfway back, from the shaft plane to the shoulder plane at address. Outside it means under or above plane halfway back (Mark approved the pair 13 Nov 2026, provisional). |

## 5. Fault or tendency: decided, all faults

Mark, 13 Nov: use "tendency" only if it fits the current model, otherwise fault. It does not fit. A tendency in this model is a shot outcome, three factors per shot: strike, direction and flight (memory `feedback_tendencies_are_three_factors`). It is not a swing pattern. So **every condition here is a fault**, and no `judgement` field is added.
- The style nature of arm lift (one-plane against two-plane) is carried instead by a deliberately wide corridor and the golfer words. It is also stated in the condition's `golferWhy`: both styles work, and only the extremes are flagged.
- The conditions ship as `draft`, like the rest of the plane family, with heuristic norms.

## 6. Data problems to fix or rule out first
1. **Chris's 3-D shoulder line fails.** His shoulder tilt is 11° at the top on a 35° bend (tilt − bend −24, against −3 … −6 for the others), and it contaminates his 3-D arm lift (ρ −0.93 with the tilt; his image value is ordinary). This needs diagnosing as processing work. Until then, the precedence in §3.2 must fall back to the image when the fit's shoulder line is implausible against the forward bend.
2. **Mark's 3-D lead elbow reads 31° bent at the top** (Nick 18°, Jess 12.5°). Check it on frames before seating the arm-lift norm.
3. **Jess's `leadArmToTorso` after impact** jumps between about 7° and 150° (a keypoint swap or wrap), so `chicken_wing` fires on all her shots from a broken reading. This is a separate fix.
4. **The club track was refused on all of Paddy's driver swings** (and on 8 of Nick's 11-02 swings), so takeaway and plane halfway back are not assessable there.
5. **The 3-D shoulder tilt depends on the session** (Nick 37–41° on 10-20, 23.5° on 11-02). It is uncalibrated; calibration (#74) is the lever.

## 7. Links (Mark approved 13 Nov)

**New:**

| From | To | Type | Strength |
|---|---|---|---|
| `flat_takeaway` | `over_the_top` | causes | **strong** (Mark: "sounds inevitable"; library ρ −0.51 with the loop) |
| `flat_takeaway` | `flat_backswing_plane` (club under plane halfway back) | causes | strong |
| `flat_takeaway` | `trail_elbow_deep` | causes | moderate |
| `flat_takeaway` | `arms_swing_around` | causes | moderate |
| `steep_takeaway` | `steep_backswing_plane` (club above plane halfway back) | causes | strong |
| `steep_takeaway` | `disconnection` | causes | moderate |
| `arms_swing_around` | `flat_backswing_plane` | causes | strong |
| `arms_swing_around` | `under_plane_stuck` | causes | moderate |
| `arms_swing_around` | `in_to_out_path` | causes | moderate |
| `arms_lift` | `steep_backswing_plane` | causes | strong |
| `arms_lift` | `over_the_top` | causes | moderate |
| `arms_lift` | `steep_downswing_shaft` | causes | moderate |
| `flat_shoulder_plane` | `arms_lift` | causes | moderate (a level turn compensated by lifting the arms) |

**Moved:** the edges into and out of the retired `backswing_plane_too_flat` / `_too_upright` move to the relabelled pair, after removing duplicates:
- `flat_shoulder_plane → backswing_plane_too_flat` becomes `→ flat_backswing_plane`, which already exists;
- `steep_shoulder_plane → backswing_plane_too_upright` becomes `→ steep_backswing_plane`;
- the downswing links (`→ downswing_plane_too_flat / _too_steep`) move with them.

**Kept:** every other link, including `inside_takeaway` / `outside_takeaway` (direction) and `flat_shoulder_plane → flat_backswing_plane`.

**Not in scope:** the absolute downswing pair (`downswing_plane_too_steep` / `_too_flat`, at P6) has the same absolute-against-relative question. It is noted for a later pass.

## 8. Work items (when approved; serial, each signed off by a reviewer)
1. **D1, pack:**
   - `m_takeawayPlane`, its two conditions, norms, golfer words;
   - the relabelled backswing-plane pair, temporarily on `m_shaftPlaneBackswing` until D3;
   - retire the absolute backswing pair;
   - the §7 links.
   - Tests: the pack, catalogue integrity and DAG suites. Library effect: a regrade only.
2. **D2, producer:**
   - `armLiftTop` from the 3-D fit (Skeleton3dBodyStage) and the down-the-line image (DtlPosture), with σ, precedence and the §6.1 plausibility fallback;
   - catalogue descriptor, measure, the two conditions, norm, links;
   - tests on a synthetic fit and a synthetic image.
3. **D3, derived:** `clubSlotTop`, after verifying the `shoulderPlane` reading (§3.3); re-point the backswing-plane pair to it.
4. **Sweep and regrade** on the cabin PC, with before/after per golfer, then Mark judges.

## 9. Validation (#109, the cabin day)
- Deliberate sets, with the chest sensor on, both cameras and the launch monitor:
  - takeaway flat / on / steep;
  - arms around / with / lifted;
  - shoulder turn flat / normal / steep.
- Measure what each does to the loop, the club path, the attack angle and the face. This settles cause, which the library's correlations cannot.
- Seat the arm-lift and takeaway norms from these sets plus the library, and recheck the upper edge of the on-plane window halfway back.

## 10. Mark's answers (13 Nov)
1. The takeaway corridor of −2 … +3: "too early to say, but sounds about right". It stays provisional.
2. Fault or tendency: all faults (§5).
3. `flat_takeaway → over_the_top`: strong.
4. Order: no hurry, so the most sensible plan of attack:
   1. first the data problems that contaminate these readings (§6.1–6.3);
   2. then D1 (pack), D2 (arm lift, image first), D3 (plane halfway back);
   3. then one sweep and regrade.

**Later, 13 Nov:**
- **Fixes A, B and C approved** (P8 bound and the arm-to-torso gate; the 3-D fit's cross-view shoulders, honest tiers and the two shoulder warnings; the ball-shadow size rule).
- **Arm lift is measured on the UPPER ARM** (lead shoulder → lead ELBOW), not to the wrist. Mark: a bent lead arm is a "cheat" that makes the arms look lifted, and also a fault. Every description of arm lift must say it is measured to the elbow and why. The bent arm itself is graded by `bent_lead_arm`, which gets a 3-D route in D2, read only where the fit's lead elbow is really measured (FB's honest tiers).
- **Chicken wing (D5): face-on only, read in the 100 ms after impact, not at P8.** Mark: "just because we don't catch P8 correctly doesn't mean we cannot diagnose it … there is no reason to need 3-D for this one." The reading is the lead elbow's added bend (face-on shoulder–elbow–wrist), gated only on foreshortened arm segments. The arm-against-torso reading stays shown, with no signal. This needs a reducer window in ms after an anchor.
- **Order:** FB → FC → D5 → D2 → D3 → one sweep → the write-up on Jess.
- **The shoulder tilt norm stays at −3 ± 5,** with its wording corrected: amber from −8 / +2, firing below −13 / above +7.

**Evening, 13 Nov** (Mark: "go with your recommendations on all of them, esp. the follow-ups"; built as D6, §11):
1. **Takeaway firing edges:** the faults fire below −2 and above +3 (σLo 1, σHi 1.5; amber from −1 / +1.5), no longer at
   −4 / +6. Why: within a session the hand-path loop rises steadily as the takeaway flattens, and by golfer the median loop
   steps from 11.5 % (1–2° under the plane) to 15.9 % (2–3° under), with over the top surfacing at about 9 % of loop and firm
   at 13 %.
2. **The 3-D bent lead arm is a change from address:** `m_leadElbowBendTop3d` is the measured bend at the top less the
   measured bend at address. The fit reads a straight address arm at 5–19° by golfer (Chris 18.6°), so a fixed centre gave
   some golfers a head start. Its norm is re-seated as a ceiling: μ 0, tolerance 8, amber from +8, firing above +16.
3. **`chicken_wing`:** `reportHold` stays until the sweep shows the new reading in use, and is revisited then. It leaves the
   wrist domain: it is an arm fold, not a wrist motion, and its group is already arms.
4. **A record names why P8 was refused** (the FA follow-up): `positions.p8Refused`, one code per cause.
5. **`leadArmToTorso` stays in Arms** on the Consistency page (`consistency_design.md` decision 11): confirmed.
6. **The arm-lift norm and the one-norm stand-in are accepted as built:** μ 5, corridor −3 … +12, firing below −11 and
   above +19, seated on the image route; a 3-D reading, where it stands, is judged against that row and carries
   `body3d.armLiftImageCorridor`. No code change.
7. **After the D6 review (approved by Mark, 13 Nov):**
   - the bent arm's address reference is the median over the whole ADDRESS HOLD, where the arm is still, not the ±15 ms
     window at P1;
   - a Refused record names an unmeasured lead elbow (`body3d.leadElbowUnmeasured`, code top / address / both);
   - the slot carries a Downgraded ⚠ while the down-the-line camera is uncalibrated (`club.slotCameraAssumed`): compare it
     within a session, not between sessions.

## 11. Build notes

### D1 (pack only, 13 Nov 2026)

**Built**
- **`m_takeawayPlane`**: `swingPlaneCurve`, reducer `delta`, anchor p1, window p1..p2, live, down-the-line. It is declared `kind: provided`, like `m_shaftPlaneBackswing`. In this pack, `composed` means a measure built from body-series facets, so it does not apply here.
  - Sign: + means the club starts back above the plane it was set on.
  - Worked example: 58° at address and 53.5° at shaft-parallel back read −4.5°, which is a flat takeaway.
  - At P2 the shaft lies along the target line and defines no plane, so the curve there is the plane through the ball line and the hands (`shaft_plane_curve.h`).
  - A curve bridged (masked) round P2 gives no reading. Both faults are then not assessable.
- **Norm**: μ 0, sigmaLo 2, sigmaHi 3, heuristic. The corridor, the norm's claim μ − σ … μ + σ, is −2 … +3.
  - Under the shipped `standard` grading policy, a fault fires past two tolerances: flat below −4°, steep above +6°.
  - Mark's library median of −3.1° is therefore outside the corridor but inside the Good band. On the median it does not fire.
  - **Superseded by D6** (Mark, 13 Nov evening): σLo 1, σHi 1.5, so the faults fire below −2° and above +3°, and Mark's median fires.
- **`flat_takeaway` / `steep_takeaway`**: draft faults, group planePath, axis `takeaway_plane`, picture DTL P2 shaft.
  - Drills reused: `drill.takeawayStick`, then `drill.buttToTheLine`.
  - Prominence: common (flat), occasional (steep).
- **Retired**: `backswing_plane_too_flat`, `backswing_plane_too_upright` and their signals.
  - `m_backswingPlaneAngle` stays live with its per-club norms (shown, not graded).
  - The two rows also leave `tools/diagnostics/area_regroup_map.json` and its generated header. An old pack file that still carries them in `swingPlane` maps through the retired-token table to `planePath`.

**Edges as built**

| From | To | Type | Strength | Note |
|---|---|---|---|---|
| `flat_takeaway` | `over_the_top` | causes | strong | §7 |
| `flat_takeaway` | `flat_backswing_plane` | causes | strong | §7 |
| `flat_takeaway` | `trail_elbow_deep` | causes | moderate | §7 |
| `steep_takeaway` | `steep_backswing_plane` | causes | strong | §7 |
| `steep_takeaway` | `disconnection` | causes | moderate | §7 |
| `posture_too_bent` | `steep_backswing_plane` | causes | moderate | moved from `backswing_plane_too_upright` |
| `steep_shoulder_plane` | `steep_backswing_plane` | causes | moderate | moved |
| `steep_backswing_plane` | `downswing_plane_too_steep` | causes | moderate | moved |
| `posture_too_upright` | `flat_backswing_plane` | causes | moderate | moved from `backswing_plane_too_flat` |
| `flat_backswing_plane` | `downswing_plane_too_flat` | causes | moderate | moved |
| `early_face_roll` | `flat_takeaway` | causes | moderate | **added, not in §7**: a fault that can fire must have a cause (`core_pack_test`) |
| `limited_thoracic_rotation` | `steep_takeaway` | causes | weak | **added, not in §7**, the same reason; the plane twin of its weak link to `outside_takeaway` |

- Dropped as duplicates of existing edges, which are kept:
  - `ball_too_close → steep_backswing_plane`;
  - `ball_too_far → flat_backswing_plane`;
  - `flat_shoulder_plane → flat_backswing_plane`;
  - `arms_reaching_at_address → flat_backswing_plane` (the old edge to the retired pair was weak; the kept edge is moderate).
- **Removed: `steep_shoulder_plane ↔ over_the_top` (corroborates).** The moved edge `steep_shoulder_plane → steep_backswing_plane` creates a causal path `steep_shoulder_plane → steep_backswing_plane → over_the_top`. The validator forbids a corroborates edge alongside a causal path (`corroboratesCausal`), because the pair would count twice in the ranking.
- Not added (D2): the arm-lift edges.
- The edge count goes from 663 to 665: 9 removed with the retired pair, 5 moved back in, 5 new from §7, 1 corroborates edge removed, 2 causes added.

**For Mark's review**
- The two added causes, and the removed corroborates edge.
- The moved posture edges: `posture_too_bent → steep_backswing_plane` and `posture_too_upright → flat_backswing_plane`.
  - They follow §7 ("move"), but the slot pair is judged against the golfer's own address plane.
  - On 13 Nov the same reasoning removed the posture → shoulder-plane edges (ab99dc53).
- Whether the −2 … +3 corridor should be the firing edges instead. That would be sigmaLo 1 and sigmaHi 1.5, firing below −2 and above +3.

**Library effect**: a regrade only, with no re-analysis. The library has not been regraded.

### FA (P8 bound and the lead-arm-to-torso gate, 13 Nov 2026)

This is §6's item 3, from the arm-readings report. Jess's `chicken_wing` had been firing from two
broken readings stacked on each other: a P8 timed at the finish, and an arm-to-torso angle read
where the camera could not see one.

**Built**
- **P8 is bounded** (`shaft_positions.h` `findP8Crossing`; `shaft_position_first_design.md` Layer B):
  - P8 is the first shaft crossing timed 40–200 ms after impact (`positions.p8MinAfterImpactUs`,
    `p8MaxAfterImpactUs`).
  - It must go down → up.
  - It is refused after the shaft sat within ±12° of vertical for 40 ms from impact + 40 ms
    (`positions.p8StallBandDeg`, `p8StallMinUs`).
  - Otherwise there is no P8. All four constants are PROVISIONAL, in `pp_tuned_constants.h` with
    their provenance, and sweepable.
  - `kShaftStageVersion` 12 → 13, so a re-analysis re-runs the tracker.
  - On the stored tracks (283 swings), the 181 P8s already timed 40–200 ms are unchanged. The 24
    late ones (291–1044 ms) and the 4 at +4–6 ms (an impact flip) lose their P8. The window refuses
    the 24 late ones and three of the flips; the stall is a guard for a strip the club leaves early
    enough to cross inside the window.
  - The down → up rule refuses the fourth flip (Nick 10-20 Wrist_01/0003). There the track flips at
    +6 ms, drifts only about 45° in 75 ms (not a swinging shaft) and crosses back up → down at
    +56 ms, inside the window. Every un-flipped follow-through crosses down → up, so the rule cannot
    remove an un-flipped P8. Its cost: on a track that flips cleanly and stays flipped, the crossing
    times are the true ones, so a correctly timed P8 would be dropped too. None of the 181 is such a
    case.
- **`leadArmToTorso` is gated** (`upper_body_metrics.cpp`; `upper_body_face_on_metrics.md` §4):
  - A frame is read only when the shoulder line holds (`upperBody.minShoulderSpanRatio`, 0.40),
    and the lead upper arm images 0.45–1.4 × its address length (`upperBody.minUpperArmRatio`,
    `maxUpperArmRatio`, new and PROVISIONAL).
  - Refused frames go on the series' gated list: invalid, never bridged.
  - New gate `body.armTorsoLines`, registered in the catalogue and in `gate_words.json`, leaves:
    - a Masked record over the swing;
    - a Refused record at P7 and at P8 wherever it leaves the grader nothing within ±15 ms.
  - The Refused record is the one a missing `m_leadArmToTorso` names.
  - A frame that passes both gates reads exactly what it did before.
  - The producer has no stage version, so nothing is bumped there.

**Every P8 consumer, with P8 absent**
- Timeline fusion and the positions ladder have no camera candidate for slot 8, so they skip it. An
  IMU segmenter P8, where one exists, stays.
- DTL schedule B falls back to impact + 150 ms. The kinematic-sequence domain ends at impact. The
  skeleton trim cuts nothing, so the whole follow-through is kept (`analysis_pipeline.cpp`). `m_leadKneeFlex`'s 3-D check uses Impact ± 15 ms. The rig driver ends at
  impact + its constant.
- The phase-sample lists skip an unsegmented phase.
- The P7 → P8 and P1 → P8 measures are not assessable: `m_leadKneeFlex`, `m_leadArmToTorso` and the
  seven `_p8` deltas.

**For Mark's review**
- **How often `chicken_wing` stops being graded.** On the 148 library swings whose P8 is 60–200 ms
  after impact, the new gate passes the nearest frame to P8 on 111:
  - Mark 82 of 93;
  - Nick 25 of 29;
  - Jess 2 of 9;
  - Paddy 2 of 11;
  - Chris 0 of 6.
  - The refusals are almost all the shoulders, which are past edge-on by P8 for the golfers who
    open up hardest. This is an estimate from the arm-readings probe: it uses one address frame
    rather than the reference median, and the nearest frame rather than the ±15 ms window.
- The down → up rule was not in the brief (see NOTES).

**Library effect**: none until a re-analysis. The shaft bump re-runs the tracker on every swing.

### FB (the 3-D fit's cross-view shoulders, honest tiers, two shoulder warnings, 13 Nov 2026)

This is §6's item 1, Chris's failed shoulder line. Its diagnosis: at the top the shoulder line points
almost at the face-on camera, the face-on trail shoulder is hidden and placed 5–6 cm low, and with the
shoulder width held the fit's robust loss threw one view away. On 6 of Chris's 8 swings it threw away
the down-the-line one, and the tilt collapsed (tilt − bend −24). The camera solve, mirroring, label
swaps and the rig offset were ruled out.

**Built** (`skeleton3d_fit.cpp`, `analysis_pipeline.cpp` `Skeleton3DStage`; `swing_3d_viz_design.md` §18)
- **The two views of a shoulder or hip that disagree.** After stage 2 has settled the cameras, each
  frame's shoulder (5/6) keypoints, and the hips' (11/12) when switched in, are triangulated. Where the two rays miss by more
  than 30 mm (`body3d.crossViewMissMm`), the LOWER-confidence view's σ is multiplied by miss / 30 mm,
  at most ×5 (`body3d.crossViewSigmaCap`). Equal confidences inflate neither. A pass that inflated
  anything is followed by its own LM pass.
  - **The hips are OFF by default** (`body3d.crossViewHips` false; the coordinator's decision, 13 Nov).
    The diagnosed failure is the occluded shoulder at the top. Mark's trail hip's rays miss by a median
    79 mm over the whole swing (> 30 mm on 94 % of frames): the hip keypoint is placed differently in a
    front and a side view, a definition offset, not an occlusion, and down-weighting it would move the
    pelvis turn with no evidence that it is wrong. The switch stays, for the sweep.
  - The stage-1 cameras are the canned start (a square, level DTL camera at a guessed distance), so
    every marker's rays miss by centimetres there: the brief's "stage-1 cameras" cannot tell a
    disagreeing shoulder from a seen one. The pass runs on the cameras stage 2 has fitted instead,
    once, after the label swaps.
- **Confidence in the 2-D σ.** `observePose` multiplies the smoothed track's σ by
  max(1, 0.8 / conf) (`body3d.confSigmaRef`), and hands the confidence to the fit.
- **Honest tiers.** A view saw a joint only where the fit lands within max(3σ, 15 px)
  (`body3d.tierSeenSigmas`, `body3d.tierSeenFloorPx`; σ the keypoint's own, before the cross-view
  inflation) of one of its witness keypoints. The 15 px floor (the coordinator's decision, 13 Nov) sits
  above the fits' typical miss (median reprojection 3–8 px) and well below the poorly seen elbows'
  57–114 px; 3σ alone demoted most of the library (below). It decides
  measured (3) against constrained (2) only. A joint with a keypoint near it in time is never made
  absent by it, so every consumer of tier ≥ 2 reads what it did.
- **`body3d.shoulderViewsDisagree`** (Downgraded): at the Top ± 15 ms, the down-the-line keypoints'
  shoulder line (both confidences ≥ 0.7, at least 20 px long) against the fit's shoulder joints
  projected into that camera, as lines. A median above 7° (`body3d.shoulderViewsDisagreeDeg`) fires it.
  - Covers: `shoulderPlaneAngle3d`, `shoulderLineYaw`, `thoraxRotation`, `thoraxRotationSigned`,
    `xFactor`, `xFactorStretch`. Not the 3-D `spineSideBend`: it is graded at Impact, and this is
    evidence about the top (review). It is fired straight after the fit, before any
    reading exists, and `scopeShoulderViewsRecord` (`runProfile`, after the trunk-sensor precedence and
    before the tilt is derived) keeps only the readings that stand and came from the fit. A chest
    sensor's shoulder plane or thorax turn is not downgraded by the cameras.
  - `shoulderTiltVsBend` inherits it from its tilt.
  - Evidence `medianDeg`, `limitDeg`, `frames`, `fromMs`, `toMs`. Sentence: "At the top the
    down-the-line camera's shoulder line and the 3-D fit's differ by 12° (over 3 frames); more than 7°
    means the two cameras disagree."
- **`body.tiltBendOutOfRange`** (Downgraded, `shoulder_tilt_vs_bend.h`): `shoulderTiltVsBend` below
  −20° (`body3d.tiltBendFloorDeg`). Subject the reading; covers it and `shoulderPlaneAngle3d`;
  evidence `valueDeg`, `floorDeg`. Sentence: "Your shoulder tilt read −22° against your posture; below
  −20° is beyond what a full turn can make."
- **Versions:** `kSkeleton3DStageVersion` 17 → 18, `kShoulderTiltVsBendVersion` 1 → 2.
  - Write-back never reuses a recorded fit. The stage recomputes the fit from the (reused) poses on
    every re-analysis, and nothing reads `versions.skeleton3d` to skip it. The only fixed input is a
    session pool, which only `swinglab_run --skeleton-calib` passes. So the bump records the change, and
    the sweep re-fits every two-camera swing.
- **The norm's words** (Mark, 13 Nov): `m_shoulderTiltVsBend` stays −3 ± 5. It is amber from −8 / +2
  and fires below −13 / above +7 (`norms.json` citation, `planned_metrics_promotion.md` §6 W7).

**Synthetic swing** (`skeleton3d_test` (FB), 234 frames at 120 Hz, σ 2 px)
- The control is unchanged: the top tilt is 26.17° with and without FB (truth 26.38°), and nothing is
  inflated.
- The brief's perturbation: over top ± 120 ms the face-on trail shoulder 5 cm low and the face-on lead
  shoulder 8 cm toward the target, at confidence 0.6.
  - The pass inflates 29 face-on keypoints on 29 frames and no down-the-line one.
  - The top tilt is 26.15° (error 0.23°) and the down-the-line trail-shoulder residual 2.3 px.
  - ⚠ The fit WITHOUT FB passes too (error 0.25°, 2.4 px). The synthetic golfer's anatomy is exact,
    so the face-on view's 37 px miss is outvoted either way: the test pins the behaviour, but it does
    not reproduce the library's collapse. Only the sweep can show that.
  - The lead shoulder pushed toward the target hardly moves its rays apart, because that direction is
    nearly along the down-the-line camera's line of sight. The pass cannot see it; only the trail
    shoulder's drop is caught.
- **Runtime:** the control 571 ms against 568 ms (29 iterations each); the perturbed swing 651 ms
  against 625 ms (32 against 31 iterations: the extra LM pass converges in a few). The pass and the
  tiers' residual check are within the timing noise; only a swing the pass inflates pays the extra LM
  pass, about +25 ms (4 %).
- Honest tiers: the face-on lead elbow keypoint 100 px off over top ± 25 ms makes the lead elbow
  constrained on all 6 top frames, where the control has it measured, and no other joint's tier
  changes; 10 px off it stays measured (inside the floor). On the clean control no joint-frame changes
  tier (6,173 of 6,318 measured either way). At 3σ without the floor, 43 had (noise past 3σ).

**The library, read only** (the STORED v17 fits and cameras, 184 two-camera swings; the v18 fit itself
will differ; `fbprobe.py`, `alt.py` and `miss.py` in the session scratchpad)
- **Cross-view misses.** At the top the trail shoulder's rays miss by a median 63 mm (Mark), 60
  (Chris), 49 (Jess) and 17 (Nick): the pass fires there on 83–100 % of Mark's, Chris's and Jess's
  frames, and on 17 % of Nick's. At address a shoulder misses by 5–23 mm (Chris's and Jess's lead
  shoulder 48–58 mm).
  - **The hips miss everywhere** for some golfers (why they are off by default): Mark's trail hip by a median 79 mm over the swing
    (51 mm at address, 98 mm at the top; > 30 mm on 94 % of frames), his lead hip 45 mm; Chris's trail
    hip 66 mm. Nick and Jess: 13–29 mm. That is mostly the keypoint's placement differing between a
    front and a side view, not an occlusion. The pass then down-weights the lower-confidence view's
    hips on most of Mark's frames (the factor's median ×2.7 on his trail hip), which would move the
    fit's pelvis and so the two-camera pelvis turn.
- **The shoulder check's confidence gate.** On `sweep_p4.csv`'s top medians, 42 swings have a slope
  error over 7°. Requiring both down-the-line shoulder confidences ≥ 0.7 keeps an estimated 31: Mark
  21 of 32, Nick 3 of 3, Chris 6 of 6, Paddy 1 of 1. Mark's down-the-line trail shoulder at the top
  often reads 0.6–0.7.
- **Honest tiers: 3σ alone demoted most of the library, hence the floor.** The 2-D σ has a 1.5 px
  floor, and the stored fits' median reprojection error is 3–8 px, so 3σ alone took 49–99 % of measured
    joint-frames to constrained, by joint and golfer, the knees and hips as well as the poorly seen
  elbows. The share dropped against the floor F (a view saw the joint within max(3σ, F px)):

  | Joint (golfer) | 3σ alone | max(3σ, 10 px) | max(3σ, 15 px) (built) | max(3σ, 20 px) |
  |---|---|---|---|---|
  | lead elbow at the top: Mark | 84 % | 62 % | 47 % | 33 % |
  | lead elbow at the top: Nick / Jess / Chris | 100 / 95 / 100 % | 100 / 88 / 100 % | 100 / 85 / 100 % | 100 / 84 / 100 % |
  | lead knee, whole swing: Mark / Nick | 91 / 92 % | 77 / 64 % | 64 / 41 % | 49 / 17 % |
  | trail shoulder at the top: Mark / Chris | 87 / 100 % | 84 / 100 % | 82 / 98 % | 75 / 88 % |
  | lead shoulder, whole swing: Mark | 73 % | 43 % | 26 % | 15 % |

  - Every consumer that reads tier ≥ 2 (`body3d::jointAt`, `skeletonLines` for the two-camera turn,
    `kSkeletonMinShaftTier`, the session synthesis) reads exactly what it did: the rule never takes a
    joint below constrained.
  - What changes is what tier 3 means where it is SHOWN: the 3-D panel's frame chip (the weakest of
    13 major joints, `swing_rig_driver.cpp`) will read "constrained by the anatomy" rather than "seen
    by both cameras" on nearly every frame of a real swing.
  - **As built, max(3σ, 15 px)**: the share of measured joint-frames (stored v17 fits) taken to
    constrained, whole swing / at the top ± 15 ms:

    | Joint | Mark | Nick | Jess | Chris |
    |---|---|---|---|---|
    | lead elbow, whole swing / at the top | 24 / 47 % | 55 / 100 % | 53 / 85 % | 40 / 100 % |
    | trail elbow | 38 / 84 % | 56 / 100 % | 55 / 96 % | 61 / 100 % |
    | lead wrist | 25 / 16 % | 43 / 16 % | 62 / 88 % | 67 / 100 % |
    | trail wrist | 17 / 27 % | 13 / 7 % | 35 / 41 % | 21 / 0 % |
    | lead shoulder | 26 / 20 % | 46 / 52 % | 49 / 26 % | 30 / 0 % |
    | trail shoulder | 49 / 82 % | 35 / 35 % | 54 / 92 % | 27 / 98 % |
    | lead hip | 53 / 53 % | 15 / 0 % | 19 / 0 % | 26 / 18 % |
    | trail hip | 85 / 100 % | 16 / 10 % | 26 / 21 % | 72 / 92 % |
    | lead knee | 64 / 99 % | 41 / 79 % | 53 / 100 % | 48 / 78 % |
    | trail knee | 56 / 77 % | 33 / 55 % | 45 / 86 % | 66 / 100 % |

  - D2's `bent_lead_arm` 3-D route ("only where the lead elbow is really measured") then reads Mark's
    lead elbow on about half his top frames (53 %), and Nick's, Jess's and Chris's on none or nearly
    none (0 / 15 / 0 %): their face-on elbow misses are 57–114 px.
  - The knees and the trail side stay demoted on many frames even with the floor: the fixed-length
    anatomy lands the knee 10–17 px from its face-on keypoint. The 3-D panel's frame chip (the weakest of
    13 major joints) will still read "constrained" on most real frames.

**Decided** (the coordinator, 13 Nov, technical): the hips off by default; the 15 px tier floor.

**For Mark's review**
- The pass runs on the fitted cameras, not the stage-1 ones (above).

**Library effect**: none until a re-analysis. The bump re-fits every two-camera swing on the sweep.

### FC (the down-the-line ball's shadow is sized by the ball, 13 Nov 2026)

This is fix C of §10 ("the ball-shadow size rule"), for Chris's lost takeaway: his `swingPlaneCurve` is masked from P1 to past P2 on 6 of 8 swings (RESULTS.md). On those 6 `clubDtl.summary.ball.found` is false.
At address his clubhead hides the ball, because the down-the-line camera sits on the ball line at
clubhead height. With no ball the still club has no witness, so the track is unseen from P1 to about
+190 ms and the P2 hand-plane fill is skipped. Once the club is away, the ball's shadow crescent is crisp,
and it passes the darkness and launch tests. It failed on **area**.

**Built** (`dtl_shaft_decide.cpp` `shadowBallCue`; `dtl_shaft_tracker_design.md` §5A.1)
- The crescent's area gates are **0.08–4 × the ball's imaged disc π·r²**, with r the ball's radius
  from the scene scale. That radius was already computed in `dtlFindBall` and written as
  `summary.ball.radiusPx`.
  - Constants: `tuned::dtlBall::kShadowAreaMinFrac` / `kShadowAreaMaxFrac`, PROVISIONAL, with their
    provenance. Overrides: `shaft.dtl.ball.shadowAreaMinFrac` / `shadowAreaMaxFrac`.
  - The old gates were 12–400 px × (frame width / 512)². That is 43.6 px on a 976-px frame, and
    Chris's crescents measure 27–46 px.
  - With no scene scale the radius is the 0.012 × H guess, so the frame rule still applies there.
  - `shaft.dtl.ball.shadowAreaByRadius` false restores the frame rule everywhere, as an ablation.
  - The darkness test, the launch-rise test (40), the 4:1 elongation limit, the ball prior and the
    1.5× winner rule are unchanged. The launch rise is still what refuses a scuff or a tee hole.
  - The cue's reason now names the gate it used, for example `area gate 20.4–1018 px by the ball (r=9.0)`.
  - `clubDtl.config` gains the three keys, so `summary.configHash` changes on every swing.
- **Version:** `kDtlShaftStageVersion` 6 → 7.

**Why 0.08 of the disc** (an approximate, read-only replay of the cue in Python over every library and corpus swing
whose DTL ball the bright cue did not find: 120 library swings plus 18 corpus swings from 06-11 and 07-03)

| session | frame width | r (px) | crescent (px) | crescent / π·r² |
|---|---|---|---|---|
| 06-11, the nine the cue was seated on | 576 | 6.1–7.0 | 52–79 | 0.34–0.53 |
| Jess 20 Oct, 22 | 976 | 8.2–9.3 | 41–64 | 0.17–0.30 |
| Nick 20 Oct, 23 | 976 | 8.5–11.8 | 41–58 | 0.10–0.25 |
| Chris 24 Oct, 8 | 976 | 8.9–9.9 | 27–46 | 0.11–0.18 |

- The brief offered two routes: scale by (r / r_ref)² from the 06-11 swings, or a floor of about
  0.08 · π · r².
  - The first keeps the old 12 px. That number was never a measurement: it is a floor set well under
    06-11's 52 px. On 06-11's own frames it is 15.2 px, which is 0.10–0.13 of that ball's disc.
  - Carried to Chris's ball, that share is 24–40 px, which still cuts his 27, 33 and 34.
  - So the floor is set under the smallest share measured: 0.08 against 0.104, which is 23 % lower.
  - The share is read through r², so a radius read 10 % large reads it 17 % small. The 0.104 is on
    the largest radius in the set (Nick, 11.8 px), which is probably read large.
- The cap keeps the old cap's share of the 06-11 ball: 506 px ≈ 4 π·r².

**How far the replay can be trusted.** It is approximate. It mostly reads within a few px of production
(Chris 0003: 46 against production's 47; 0007: 45 against 45), but it errs both ways, and once by much more:
- Nick 20 Oct Wrist_03/0001 and Wrist_03/0012: production found crescents of 44 and 48 px; the replay
  reads 41 and 43 px and, under the old floor, finds nothing.
- Nick 20 Oct Wrist_02/0003: production found one 55-px crescent; the replay splits it into 22 + 3 px,
  and would refuse it under either floor.
- Chris 0006: the replay reads 45 px, but production found nothing (under 43.6 px).

So the shares in the table are approximate, and the replay alone cannot show that no found swing is lost.

**Why no found swing is lost** (the floor argument, not the replay)
- The new floor is below the old one whenever r < √(12 / 0.08π) · W/512 = 6.9 · W/512. That is
  13.2 px at 976 px wide and 7.8 px at 576.
  - Every blown-mat swing here is inside that: r 8.2–11.8 px at 976, 6.1–7.0 px at 576.
  - So every crescent production found under the old floor is still a candidate under the new one.
- The new cap is at least 4·π·6.1² = 468 px, against a largest crescent of 79 px, so no found crescent
  is cut from above either.
- The only way a found swing can now be lost is through ambiguity: a new candidate between the two floors
  that also brightens by 40 or more at launch. The replay found no such candidate on any swing. Being
  approximate, it is a check on that last step, not a proof.
- After the sweep, each recovered swing should report `source` `shadow`, and Nick Wrist_02/0003 should
  keep its ball. That is the production check the replay cannot give.

**What the replay shows changing** (expected; to be confirmed by the sweep)
- **Found where it was not:** Chris 0001, 0002, 0004, 0005, 0008 (27, 37, 34, 43, 33 px); Jess 20 Oct
  Wrist_01/0001 and Wrist_02/0001 (43, 41 px).
  - Chris 0006 reads 45 px in the replay, but production found nothing, so it sat just under 43.6 there.
    It passes the new floor either way (22 px).
  - So Chris is expected to go from 2 of 8 to 8 of 8, and Jess to gain 2.
- **Unchanged in the replay:** apart from the misreads above, every swing the shadow cue found gives the
  same crescent at the same place, with the same candidate count, under both floors.
  - No swing becomes ambiguous.
  - No dim-mat swing changes. Mark's 5, 7 and 8 Oct sessions, Nick 13 Oct and 2 Nov, and 07-03 are
    still answered by the contrast cue, because the shadow cue's blown-mat test excludes them.
  - Swings whose ball the bright cue found were not replayed. There the shadow cue is recorded but
    never consulted, so only `summary.ball.shadowX` / `shadowY` / `launchRise` / `nCandidates` can move.
- ⚠ Chris 0001's crescent is 12 × 3 px, elongation exactly 4.0. It passes the 4:1 limit with no room
  to spare (out of scope; see NOTES).

**Write-back and the bump**
- The reuse line `reuse: pose … ball recorded shaft recorded (synth refreshed) ladder … dtl pose …`
  (`swing_reanalyzer.cpp`) names the **face-on** ball track and the **face-on** shaft track. Neither is
  the DTL ball.
  - The DTL club track has no reload path. `DtlShaftStage::run` always calls `DtlShaftTracker::track`,
    and `dtlSolve` always calls `dtlFindBall`.
  - Nothing reads `versions.shaftDtl` to skip it, and the stored `analysis.clubDtl` is read back only
    to draw it (the replay tile, the delivery and posture pictures, the report stills).
  - So the DTL ball search and track already re-run on every re-analysis. The bump records the
    change; it does not have to force anything.
  - What the re-run does reuse is the DTL pose (v3 unchanged) for the ball prior. The face-on ladder
    feeds the club-away window, and it re-runs on the sweep anyway, under FA's `kShaftStageVersion` 13.
- **Cost:** none added. The stage is already paid on every re-analysis. The stored timings from the last
  library re-analysis (`analysis.timings.stages`, DtlShaft) put it at a median of 0.32 s on Chris, 0.64–0.66 s
  on Jess, 0.46–0.57 s on Nick and 0.27–0.36 s on Mark, against a 4.6–6.2 s median for the whole
  re-analysis with the face-on tracks reused.

**Synthetic tests** (`dtl_shaft_decide_test` T8F, T8A)
- On a 976-px frame, a 9-px ball and a 32-px crescent that goes at launch are found from the shadow,
  1.6 px from the planted centre. The gate is 20.4–1018 px.
  - The frame rule (`shadowAreaByRadius` off) refuses the same crescent: that is Chris's failure,
    reproduced.
  - With no scene scale the frame rule still applies.
- The same 32-px mark, left in place after launch, is a candidate and is refused by the launch rise.
- At 512 px, a 6-px ball's 34-px crescent and a static mark give the same answers under both rules.
  T8A's 320-px scenes also give the same answers under both rules.
- T11 (no ball, so the still club is not solved) is unchanged.

**For Mark's review**
- The floor is set from four blown-mat sessions in three bays. It is PROVISIONAL.
- The takeaway readings this recovers on Chris (and the 2 Jess swings) are only expected. They appear
  only after the sweep re-tracks those swings.

**Library effect**: none until a re-analysis. The DTL track re-runs on every swing in the sweep.

### D5 (chicken wing from the face-on camera, read in the 100 ms after impact, 13 Nov 2026)

Mark, 13 Nov: chicken wing "should be visible immediately between impact and [100] ms after (before
P8) … and crucially it should be easily visible from the face-on camera."

**Built**
- **`leadArmFlexion` gains a `faceOn` rung** (`upper_body_metrics.cpp`; `upper_body_face_on_metrics.md` §4):
  - the value is 180° − the angle at the lead elbow between the lead shoulder and the lead wrist, at every
    frame; 0 = straight, + = more bend, the `wristImus` rung's sign and unsigned magnitude;
  - σ: the three keypoints' σ through the angle, with the elbow's two terms combined as vectors;
  - gate: the lead upper arm (FA's 0.45–1.4 band) and the lead forearm (new `upperBody.minForearmRatio` /
    `maxForearmRatio`, 0.45–1.4, PROVISIONAL) against their address lengths. No shoulder-line gate;
  - records: new `body.leadElbowLines` (Masked over the swing; Refused at impact when nothing is left there);
  - catalogue: RM::Projected, Estimated, below `wristImus`; `UpperBodyMetricProvider` claims the key; the
    sensors win where worn (`applyLeadElbowSensorPrecedence`, after every stage); the wrist assessment never
    takes the camera series (`isCameraWristDofSeries`).
- **`windowMs`, a reducer window in milliseconds after the anchor** (`metric_reducer.h`;
  `diagnostics_developer_guide.md` §3): `extremum` and `delta`, through the JSON load and save, the validator,
  the phase grid (`msSpans`, built for the spans the reading pack names; the sidecar's guard is a subset test
  with a union rebuild), σ, the condition-reading phase, the canonical label and id, and the model browser.
- **`m_leadElbowFoldAfterImpact`**: `leadArmFlexion`, extremum max over P7 + 0…100 ms, from P7, ceiling, live.
  - Sign: + = the lead elbow bent more after impact than at impact.
  - Worked example (live_measure_source_test): 15° at impact, rising to 29° by +60 ms and held, reads +14°,
    which fires `chicken_wing`; a 7° rise reads +7° (amber), a straight arm 0°.
  - σ ≈ 4° on a library swing: the span's (2.7) and the anchor's (2.7) in quadrature.
  - Records: a missing fold names `body.leadElbowLines` when the gate emptied impact.
- **`chicken_wing`**: `sig_chickenWing` reads `m_leadElbowFoldAfterImpact` high, alone. Picture face-on, P7,
  `leadArm`. New `check` text (the brief's). `golferWell` and `goal` reworded to the straight arm (they said
  "close to your body", which was the old reading). `m_leadArmToTorso` stays live and shown, with no signal.
- **Norm** (provisional, heuristic, ceiling): μ 0, tolerance 5, so amber from +5 and firing above +10;
  `plausibleHi` 90. Seated from the library, Jess apart (below). Mark decides the final value.

**Held, not switched on.** The face-on route would have started `m_leadElbowFlex_p1…p8`, and through
`m_leadElbowFlex_p4` the two conditions `bent_lead_arm` (draft) and `locked_lead_arm` (active), on every
camera swing. Their norms are heuristics migrated from the wrist sensors' grid. So all eight are `held`, with
the reason in `gapReason`. Status is per measure, not per route, so this holds the sensor route too, but no
library swing carries the sensors' elbow (0 of 284), so no grade that was being made stops. What they would
have done on the 249 swings with a face-on reading (read-only replica from the stored keypoints):

| Measure | Norm (μ, σ, monitor) | Readable | Past the deviation edge (Watch or worse) |
|---|---|---|---|
| `m_leadElbowFlex_p1` (at P1) | 1.5, 4.5, −7…10 | 237 | 145 high |
| `m_leadElbowFlex_p2` | 3, 5, −6…12 | 236 | 65 high, 2 low |
| `m_leadElbowFlex_p3` | 4, 6, −6…14 | 233 | 146 high |
| `m_leadElbowFlex_p4` → `bent_lead_arm` / `locked_lead_arm` | 5, 7, −6…16 | 214 | **214 high (bent_lead_arm on every one)**, 0 low |
| `m_leadElbowFlex_p5` | 4, 6, −6…14 | 226 | 91 high, 3 low |
| `m_leadElbowFlex_p6` | 4, 6, −6…14 | 232 | 119 high, 1 low |
| `m_leadElbowFlex_p7` | 5, 7, −6…16 | 234 | 24 high, 2 low |
| `m_leadElbowFlex_p8` | 5, 7, −6…16 | 197 | 11 high, 59 low |

`bent_lead_arm` per golfer: Mark 168 of 168, Nick 27 of 27, Paddy 9 of 9, Jess 7 of 7, Chris 3 of 3. The
face-on bend at the top is a median 37° more than at address (10th–90th percentile 25–61°): the projection
of an arm across the chest, not a collapsed arm. D2's 3-D route for `bent_lead_arm` is the place to decide
how these grade again.

**Library check for the norm** (13 Nov, read-only; stored face-on keypoints, impact to impact + 100 ms, pose
frames 6.7 ms apart, 15 per window):

| Golfer | Swings with a fold | Median | 25th–75th | Above +5 | Above +10 |
|---|---|---|---|---|---|
| Mark | 167 | +0.1 | −0.1…+0.3 | 1 | 1 (+46, 2 Nov swing 1) |
| Nick | 28 (+7 implausible) | +3.4 | +1.6…+5.4 | 8 | 0 |
| Paddy | 11 | +0.6 | +0.4…+1.1 | 0 | 0 |
| Chris | 8 | −0.5 | −0.6…−0.4 | 0 | 0 |
| All but Jess | 214 | +0.2 | 90th +2.4, 99th +9.3 | 9 | 1 |
| **Jess** | 23 | +0.3 | +0.2…+1.3 | 3 | 3 (+31, +15, +14) |

- Most golfers' lead arm straightens a little after impact, so the fold's maximum sits at impact itself.
- Nick's 2 Nov session has 7 readings of 128–157°: the lead wrist keypoint sweeps across the elbow (its σ
  rises to 5–22 px). `plausibleHi` 90 refuses them as not a swing; the largest reading short of them is 46°.
- On Jess the old reading fired `chicken_wing` on every shot (§6.3). The new one would fire on 3 of 23 (the replica's estimate; the sweep gives the real count).

**Tests**: `upper_body_metrics_test` §16; `measure_sample_test` windowMs; `characteristic_pack_test` windowMs;
`live_measure_source_test` chicken_wing (D5); the pins in `live_measure_source_test` (215 measures, 14 not
live, rich_7iron 56 assessable), `metric_catalogue_test` (33 projected readings), `tuned_constants_parity_test`
and the `swing_themes` golden (`chicken_wing` now reads at P7).

**For Mark's review**
- The norm (μ 0, tolerance 5, firing above +10) and `plausibleHi` 90.
- Holding the eight elbow measures (the sensor route with them), rather than a per-route hold, which does not
  exist.
- The reworded `golferWell` / `goal`, and whether `chicken_wing`'s `reportHold` can now go.
- Mark's 2 Nov swing 1 (+46°) and Jess's three: frames to check before the sweep.

**Review fixes**
- The elbow precedence now cleans the face-on records whenever the sensors' elbow exists. Before, it did so only when a face-on series was dropped, so a refused address reference could keep covering the sensors' reading.
- `measure_dump` builds the pack's millisecond spans, so it reads `m_leadElbowFoldAfterImpact`.
- `m_leadArmToTorso`'s golfer words now describe the lead upper arm's angle from the torso after impact. The measure is shown, not graded.

**Library effect**: none until a re-analysis. The stored documents carry no face-on `leadArmFlexion`, so
`chicken_wing` is not assessable on them until the sweep writes it (rich_7iron: 57 → 56 assessable).

### D5 round 3 (the occlusion gate and the window rule, 13 Nov 2026; approved by Mark)

A frame sheet (`/mnt/swingdata/scratch/chicken-wing-sheet-20261113/`) showed all four folds above +10 were false:
- Mark 2 Nov s1 (+46°);
- Jess Wrist_02 s5 (+31°) and s6 (+15°);
- Jess Wrist_03 s8 (+14°).

After impact Jess keeps her head down, so her lead shoulder is behind her head from face-on. Both the smoother's predicted shoulder and the pose model's own detection then land on her chin or neck, while her sleeve is straight. On Mark's swing the model scores his elbow and wrist, near his head, under 0.40 (0.21–0.39), which the confidence rule catches. So a detected keypoint is not enough.

**Built** (`upper_body_metrics.cpp`, `measure_sample.cpp`; constants PROVISIONAL in `pp_tuned_constants.h`)
- **A lead arm point that is not seen refuses the frame.** This is a new gate, `body.leadArmHidden`. It is checked before the length band, so each frame lands on one list only. A point is not seen when either:
  - the lead shoulder, elbow or wrist sits within `upperBody.headRadiusRatio` (0.25) × the address shoulder width of the centre of the head keypoints the model scored ≥ 0.30 (nose, eyes, ears). The head's own radius is about 0.30 shoulder widths, so 0.25 is on the head, not beside it;
  - **or** any of the three scores under `upperBody.minRawConf` (0.40) on the pose model's RAW confidence (`pose.frames`). The smoother's score is never used, because a predicted point is floored at 0.5 there.
- **Records:** `body.leadArmHidden` and `body.leadElbowLines` each leave:
  - a Masked summary;
  - a Refused record at impact when nothing is left there;
  - a Refused record for the 100 ms after impact (`phase` afterImpact, with `gapMs` and `maxGapMs`) when the window is not covered.

  The hidden record's `code` is `shoulderOnHead`, `armOnHead` or `lowConfidence`, with per-reason counts. Golfer words, true for every cause: "the face-on camera couldn't clearly see your lead arm here (often your head hides your lead shoulder), so these frames can't show whether your lead arm folds".
- **Window rule:** a `windowMs` span reads only when its valid samples cover it, with no stretch longer than `reduce.msSpanMaxGapUs` (30 ms, four frames) unread, ends included. Otherwise the measure is not assessable. The reason is a new `MissingKind::WindowNotCovered` ("too little of the moment it is read over could be seen"), and the producer's afterImpact record names the gate. A readable impact is already required: no value at P7 means no fold.
  - Why a gap and not a percentage: a share of frames can be met by the frames *before* a hidden peak. On the four false folds, 46–60 ms of the window was unread.

**Library audit** (read-only, stored face-on keypoints, impact to +100 ms, a Python replica of the producer and grader):

| Golfer | Swings | Window frames | Lead shoulder: predicted / raw < 0.40 / on head | Lead elbow: predicted / raw < 0.40 / on head | Lead wrist: predicted / raw < 0.40 / on head | Frames refused (hidden / foreshortened) | Gradeable before → after | Fold after: median (p90, max) | Above +10 |
|---|---|---|---|---|---|---|---|---|---|
| Mark | 204 | 2526 | 0.0 / 0.0 / 0.0 % | 1.9 / 0.3 / 0.0 % | 5.6 / 1.3 / 0.0 % | 1.4 / 0.9 % | 167 → **165** | +0.1 (+1.0, +4.1) | 0 |
| Nick | 38 | 540 | 0.0 / 0.0 / 0.2 % | 4.4 / 2.2 / 0.0 % | 32.6 / 14.3 / 0.0 % | 15.4 / 7.8 % | 35 → **27** | +3.5 (+7.6, +9.9) | 0 |
| Jess | 23 | 345 | 4.1 / 0.6 / 16.8 % | 7.0 / 4.1 / 0.0 % | 9.3 / 3.2 / 0.0 % | 17.4 / 0.3 % | 23 → **16** | +0.3 (+0.9, +1.3) | 0 |
| Paddy | 11 | 165 | 0.0 / 0.0 / 15.2 % | 0.0 / 0.0 / 0.0 % | 3.6 / 0.0 / 0.0 % | 15.2 / 0.0 % | 11 → **7** | +0.6 (+1.6, +2.5) | 0 |
| Chris | 8 | 120 | 0.0 / 0.0 / 30.0 % | 0.0 / 0.0 / 0.0 % | 9.2 / 0.8 / 0.0 % | 30.0 / 0.0 % | 8 → **2** | −0.5 (−0.3, −0.3) | 0 |
| **All** | 284 | 3696 | 0.4 / 0.1 / 3.2 % | 2.6 / 0.9 / 0.0 % | 9.9 / 3.3 / 0.0 % | 6.5 / 1.8 % | 244 → **217 (89 %)** | +0.2 without Jess (+2.3, +9.9) | **0** |

- **The four flagged swings:** all are now **not assessable** (46–60 ms of the window unread), so none fires.
  - Jess's three are refused by the head rule. With only the confidence rule, Wrist_03 s8 would still fire at +13.
  - Mark's is refused by the confidence rule: his elbow and wrist score 0.21–0.39 at the peak frames.
- **Nick's 7 implausible readings** (2 Nov, 128–157°) are also not assessable now (wrist scored low). `plausibleHi` 90 stays as a guard.
- **The norm** (μ 0, tolerance 5, fires above +10) still makes sense on what remains:
  - The pooled 90th percentile is +2.3. Only Nick has readings in the amber band (8 of 27 above +5, max +9.9), and nothing fires.
  - But no library swing is a real chicken wing, so the firing edge has not met one; directed captures should seat it.
- **How often face-on can grade it:** 217 of 244 swings (89 %).
  - Golfers who keep the head down lose most: Chris 2 of 8, Paddy 7 of 11, Jess 16 of 23.
  - Nick loses 8 of 35 to wrists the model scores low.
  - Mark keeps 165 of 167.
  - For the first three, face-on alone will often say "not assessable". The down-the-line view or the 3-D fit is the candidate fallback (not built).

**Tests:**
- `upper_body_metrics_test` §17:
  - the shoulder on the head is refused, with the record and sentence;
  - a visible straight arm reads 0°;
  - a predicted wrist with a raw score of 0.3 is refused, while 0.45 reads;
  - the window rule's afterImpact record (70 ms unread; 30 ms is covered);
  - the overrides.
- `live_measure_source_test`:
  - a fold whose last 60 ms are hidden is not assessable with `WindowNotCovered`, and `chicken_wing` is not assessable;
  - 25 ms hidden still reads;
  - the content's span equals `kElbowFoldSpanUs`.
- `tuned_constants_parity_test` pins the new constants.

**Round 3 review fixes**
- `body.leadArmHidden`'s golfer words now hold for every cause (head, arm on head, low score).
- The Refused records at impact and for the span after it now come from the one gate that refused the most frames there, with ties going to the hidden gate. A test covers six hidden frames plus one foreshortened frame.
- The raw-score fallback on a non-parallel track is now counted and warned once per swing, with a test.
- A span stored without its coverage is rebuilt.

### D2 (the arm lift at the top, on the upper arm; a 3-D route for bent lead arm, 13 Nov 2026)

Mark, 13 Nov: arm lift is measured on the lead UPPER ARM, shoulder → elbow, never to the wrist. A bent lead arm is a
"cheat" that makes the arms look lifted, and it is its own fault (`bent_lead_arm`). Every description of the reading
says so.

**Built**
- **`armLiftTop`** (°, one value at the top, + the lead upper arm above the shoulders):
  - Route `dtl` (`dtl_posture.h`, `DtlPostureStage`): the angle between the lead shoulder → lead elbow line and the
    shoulder line in the down-the-line picture, + with the elbow above that line. It is the median over the frames
    within ±15 ms of the Top where all three keypoints score ≥ `dtlPosture.confMin` (raw confidence).
    - σ: each keypoint's smoother σ × max(1, 0.8 / conf) (`body3d.confSigmaRef`, the fit's rule), through the
      angle; the frames' median. The half-hidden trail shoulder carries it.
    - Refused: `posture.armLift` (`keypoints` / `noFrames`).
  - Route `faceOn+dtl` (`skeleton3d_body_series.h`, `Skeleton3dBodyStage`): asin(â · n̂), â the fit's lead shoulder →
    lead elbow, n̂ the spine with its shoulder-line component removed (RESULTS.md a2, to the elbow). Joints at tier ≥ 2;
    σ first order (central differences through the five joints; within 2 % of Monte Carlo on the test pose).
    - Not built on a swing where `body3d.shoulderViewsDisagree` fired. The flag rides on
      `SwingAnalysis::skeleton3dShoulderViewsDisagree`, because the executor splices the gate list only after every
      stage. If that leaves no reading at all, `body3d.armLiftShoulders` (Refused) says why.
  - **Precedence:** uncalibrated (every swing today), the image wherever it reads; the 3-D one only fills, with
    `body3d.fromFit` (replaces `dtl`). Calibrated, the 3-D one replaces the image one. On the library every swing
    with a fit also has an image reading, so the 3-D route stands on none of them today.
  - **Catalogue:** `armLiftTop`, PointInTime, group Arms. Rungs `dtl` (Projected, **Direct**: two lines in one
    image, the `handPathLoop` rung's standing) then `faceOn+dtl` (Triangulated, Estimated). The order states today's
    rule, as W5's trail knee does; there is no conflict with the best-first rule, because Direct comes before
    Estimated. `description` and `howToRead` say it is measured to the ELBOW and why.
- **`m_armLiftTop`**: provided, `at` p4, live, `viewNeeded` downTheLine; golferName "how high your lead upper arm sits
  against your shoulder turn at the top (measured to the elbow)".
- **`arms_swing_around` / `arms_lift`**: draft faults, group **planePath** (they are layer 3 of the stack, and the
  links run within it: takeaway → arms → slot), axis `arm_lift_top`, picture DTL P4 `leadUpperArm`, practice
  provenance. `golferWhy` and `check` both say it is measured to the elbow and that bending the lead arm is not a
  way to lift the arms. `golferWhy` and `consequence` carry the style point: one-plane and two-plane both work,
  only the extremes are flagged. Drills reused: `drill.mirrorTop` and `drill.buttToTheLine` (around);
  `drill.towelUnderArms` and `drill.mirrorTop` (lift).
- **Edges, all eight §7 links, as approved:** `arms_swing_around` → `flat_backswing_plane` (strong),
  → `under_plane_stuck`, → `in_to_out_path`; `arms_lift` → `steep_backswing_plane` (strong), → `over_the_top`,
  → `steep_downswing_shaft`; `flat_shoulder_plane` → `arms_lift`; `flat_takeaway` → `arms_swing_around` (moderate
  unless marked). The validator refused none, and no cause had to be added.
- **Versions:** `kDtlPostureStageVersion` 2 → 3, `kSkeleton3dBodyStageVersion` 3 → 4 (`swing_doc_test` pin).

**The norms as built, re-seated on the UPPER ARM** (the library, `docs/research/data/plane_stack/d2_upper_arm.py`,
RESULTS.md D2). The 13 Nov figures (image 15, 3-D 12) were to the wrist, which reads 12° higher on Mark and 15°
higher on Nick, whose lead elbows bend.

| route | golfer medians (Chris apart) | golfer-weighted p2.5 … p97.5 | μ | corridor (amber outside) | fires |
|---|---|---|---|---|---|
| image (`norms.json`) | Mark +7.1, Nick −2.4, Jess +15.5, Paddy +5.6 (Chris +15.7) | −11.4 … +19.6 | 5 | −3 … +12 (sigmaLo 8, sigmaHi 7) | below −11, above +19 |
| 3-D (proposed, not loadable) | Mark +4.7, Nick +4.1, Jess +21.0, Paddy +3.6 (Chris +26.2) | −12.3 … +26.8 | 7 | −2 … +16 (σ 9) | below −11, above +25 |

- On the library the image row fires on Nick 5 of 38 (low), Jess 3 of 23 and Mark 1 of 127 (high), and on no one
  else; amber on Mark 26, Nick 10, Jess 15, Paddy 1 and Chris 8.
- To the elbow the image does NOT run above the 3-D reading: it reads a median 1.3° below (IQR −5.7 … +4.3). The 3-D
  reading spreads the golfers wider (Jess +21 against +15.5).
- **Route-specific norms: the pack cannot express them.** A norm row is keyed by (measure, context, cohort). Context
  is the shot's grading context, walked up the context tree, and cohort is the golfer's sex and age band; neither is
  the route that read the value. **The smallest honest option, as built:** one row seated on the route that stands on
  every library swing (the image), and wherever the 3-D reading stands (a fill today, the answer once calibrated) it
  carries the Downgraded record `body3d.armLiftImageCorridor` (golfer words in `gate_words.json`). The 3-D row above
  is in the citation, for when a route key exists (a `route` qualifier on a norm row, resolved like the cohort: the
  loader, the validator, `find`, the engine's resolution and the norm editor).

**bent_lead_arm's 3-D route: option (b), a separate key and measure.**
- **Why not (a), per-route status:** a 3-D rung on `leadArmFlexion` would also replace the face-on series that
  `chicken_wing` reads (D5: face-on only, by Mark's ruling). The 3-D series is sparse (measured elbows only), so the
  rung would both break `chicken_wing` and still sit under the per-measure hold.
- **Why not (c):** the reading is real where it reads. Mark's 31° at the top is his bend (the fit lands a median
  3 px from his face-on elbow keypoint), and the honest tier says exactly where the fit saw the elbow.
- **Built:** `leadArmFlexion3d` (`Skeleton3dBodyStage`): 180° − the angle at the fit's lead elbow, a curve valid only
  on frames whose lead elbow is TierMeasured, read at the top. Catalogue: its own descriptor, group Arms, the elbow's
  hue. Then `m_leadElbowBendTop3d` (provided, `at` p4, ceiling, live) and `sig_bentLeadArm3d` (high).
  - Precedence wristImus > 3-D > face-on: `applyLeadElbowSensorPrecedence` erases `leadArmFlexion3d` wherever the
    wrist sensors' `leadArmFlexion` reads.
- **The "any" trap, checked and avoided:** under `any`, the held `m_leadElbowFlex_p4` signal is always unavailable,
  so `bent_lead_arm` could only ever fire or be not assessable, never clean (it would count firings only). It is now
  `detection: first` with `detectedBy` [`sig_bentLeadArm3d`, `sig_bentLeadArm`]: the measured 3-D elbow decides where
  it reads, and the held one is the fallback. In practice the condition is single-signal (3-D) today.
- **Norm** (ceiling, checked against the 3-D reading, not the wrist sensors' grid): μ 15 (the fit's straight address
  arm reads 5–19° on measured frames), tolerance 8, so amber from 23 and firing above 31; plausibleHi 90.
- **Library effect** (stored v17 fits with FB's measured rule replayed; the v18 refit will move these):

  | golfer | swings graded (measured elbow at the top) | bend at the top | fires (> 31) | amber (23–31) |
  |---|---|---|---|---|
  | Mark | 81 of 127 | 28.7 [25.2, 31.2] | 21 | 55 |
  | Jess | 3 of 23 | 13.2 | 0 | 0 |
  | Paddy | 2 of 11 | 13.2 | 0 | 0 |
  | Nick | 0 of 37 | – | – | – |
  | Chris | 0 of 8 | – | – | – |

- **What it switches on for camera-only swings** (D5's caution):
  - `m_leadElbowBendTop3d` → `bent_lead_arm`: 86 swings graded, 21 firing (all Mark's), against a norm seated on
    the 3-D reading.
  - `locked_lead_arm` and `m_leadElbowFlex_p1…p8`: unchanged, still held (D5); no wrist-sensor-seated norm grades a
    camera reading.
  - `m_armLiftTop` → `arms_swing_around` / `arms_lift`: 207 swings with a down-the-line pose, 9 firing.

**Tests:** `dtl_posture_test` §9; `skeleton3d_body_metrics_test` §15–§16 (the §8 armless fixture now marks its arms
absent); `upper_body_metrics_test` §16h; `live_measure_source_test` (D2 block and pins: 192 conditions, 217
measures, 164 asked); `metric_catalogue_test` (137 descriptors, the two new ones); `core_pack_test` (planePath 20);
`axis_direction_test` (three rows); the `swing_themes` golden (two new `conditionFocus` entries); `swing_doc_test`
(skeleton3dBody 4).

**For Mark's review**
- The two norms (image μ 5, corridor −3 … +12, firing below −11 / above +19; bent arm 15, firing above 31).
- Holding the 3-D arm-lift row in the citation until a norm can be keyed by route, with the Downgraded record
  meanwhile.
- `bent_lead_arm` reading the 3-D elbow first, absolute at the top (not a change from address), and graded on
  Mark's swings only in today's library.
- With the wrist sensors worn, the 3-D elbow is erased (the precedence) and `m_leadElbowFlex_p4` is held, so
  `bent_lead_arm` is not assessable there until the sensor route is re-seated.
- The group (planePath) and the reused drills.

**Library effect:** none until a re-analysis. Both bumps re-run on every swing in the sweep.

**Addendum (D6, Mark 13 Nov evening):** `bent_lead_arm`'s 3-D reading is now a CHANGE FROM ADDRESS, not the absolute bend
at the top, and its norm is re-seated (μ 0, amber from +8, firing above +16). The absolute figures in the table above stand
as the record of D2; §11 D6 has the new ones. The arm-lift norm and its one-row stand-in are accepted as built (§10).

### D3 (the club's climb placed in the on-plane window halfway back, 13 Nov 2026)

Renamed the same day (Mark): "slot" is the golf term for a downswing position, so no golfer- or coach-facing text says it. The identifiers keep it (§3.3).

**The upper edge, verified first** (read-only, the stored documents, 283 swings)
- `shoulderPlane` is a metric in `analysis.metrics`: one phase sample at Address (the ladder's P1), its σ on the sample and the
  series (`sigmaKind` Propagated, a median 0.10°, at most 0.39°), no curve. `ShaftFusionStage` writes it (`shaftFusion` 9, 29 Oct).
  The diagnostics read it like any other metric, so nothing had to be persisted.
- It reads wherever the down-the-line ball was found, and nowhere else: 174 of 283 swings (Mark 123 of 203, Nick 29 of 38, Jess 20
  of 23, Chris 2 of 8, Paddy 0 of 11). Of the 109 without it, 75 have no down-the-line camera (Mark's 4 Aug, 18 Aug, 9 Sep, 20 Oct
  and 2 Nov sessions) and 34 have no ball (Paddy 11, Nick 9, Chris 6, Mark 5, Jess 3).
- The "absent from the 2026-10-20 documents" were Mark's 20 Oct session (no down-the-line camera) and 3 of Jess's (no ball).
- FC's shadow-ball fix is expected to add Chris's 6 and Jess's 2 after the sweep.

**Built** (`src/Club/club_slot_top.h`, `deriveClubSlotTop`; one step in `runProfile` after the shoulder tilt)
- **`clubSlotTop`** (ratio, one phase sample at P3, route `dtl`, Direct): (`swingPlaneCurve` at P3 − at P1) / (`shoulderPlane` at P1 −
  `swingPlaneCurve` at P1).
  - 0 means the club at P3 sits on the plane it was set on; 1 means it sits on the shoulder plane at address. Below 0 is under the window, above 1 above it.
  - Worked example: set at 58°, shoulder plane 68° (a 10° window), 63° at P3 reads 0.5. 70° at P3 reads 1.2 (above); 56° reads −0.2 (under).
  - Each part is read exactly as the grader reads it (`tilt_vs_bend::graderReadingAt`): the fitted curve at the ladder instant, the
    shoulder plane off its address sample. So the numerator is `m_shaftPlaneBackswing` on the same swing.
  - σ is first order through the ratio, with the parts independent: √(σ_P3² + s²·σ_shoulder² + (1 − s)²·σ_P1²) / width. It has the
    same noise-floor and no-σ rules as the shoulder tilt. On the library it is a median 0.03–0.05 (Mark at most 0.23), against a
    tolerance of 0.25.
  - Records: the parts' Downgraded and Withheld records cover it, and a withheld part withholds it.
- **Not produced, reason named.** A new Refused record, `club.slotUnread` (subject and covers `clubSlotTop`), has these codes:
  - `noAddressPlane`;
  - `noP3Plane` (the curve bridged at P3: 17 of Mark's swings and 1 of Nick's that have a shoulder plane);
  - `noShoulderPlane`;
  - `narrow`, with `widthDeg` and `minWidthDeg`, under `club.slotMinWidthDeg` = 4° or not positive.

  It is fired only on a swing with a down-the-line plane, so a face-on-only swing is byte-identical. A missing part's own Refused,
  Bounded or Withheld record (`club.trackRefused` covers both parts) also covers the reading and comes first, so it is the reason
  a reader is given.
- **Why 4°** (`tuned::clubSlot::kMinWidthDeg`, PROVISIONAL):
  - One tolerance is a quarter of the window, and it must stay above the roughly 1° an uncalibrated camera puts into a plane
    inclination (shaft fusion: ≤ 1° over ±15° of yaw).
  - The library's narrowest window is 5.4° (1st percentile 6.0°, median 10.3°; Mark 9.4°, Nick 15.1°, Jess 13.5°), so the floor
    refuses none.
- **`m_clubSlotTop`**: provided, `at` p3, live, `viewNeeded` downTheLine. Golfer name: "where the club sits halfway back, between the plane you set at address and the line through your trail shoulder".
- **The pair, re-pointed and relabelled** (ids kept):
  - `flat_backswing_plane` is now **Club under plane halfway back**: "halfway back, the club sits below the plane you set at address".
  - `steep_backswing_plane` is now **Club above plane halfway back**: "halfway back, the club climbs above the line through your trail shoulder".
  - `sig_flatBackswingPlane` and `sig_steepBackswingPlane` read `m_clubSlotTop` low and high.
  - `check` had become false and is rewritten: two lines from the ball, along the shaft at address and up through the trail
    shoulder, with the shaft at P3 angled between them.
  - `golferWell`, `golferWhy`, `goal` and the steep `consequence` now name the on-plane window halfway back and the line through the trail shoulder. The aliases "under plane halfway back" and "above plane halfway back" are added. The picture (down the line, P3, shaft) is unchanged.
- **Norm** (heuristic, provisional; "Mark approved the pair 13 Nov 2026, provisional"): μ 0.5, sigmaLo 0.25. That is amber
  outside 0.25–0.75, firing below 0 and above 1.
- **`m_shaftPlaneBackswing`** stays live and shown, with its norm row, and grades nothing.
- **Version:** `kClubSlotTopVersion` 1 (`versions.clubSlotTop`).
- **Catalogue:** `clubSlotTop`, PointInTime, group Swing plane, its own card at P3. `ShaftFusionProvider` claims it.
  `swingPlaneCurve`'s `usedBy` loses the pair.

**The library, read-only** (stored documents; the grader's sampling; 156 swings with both edges and a P3 plane; `docs/research/data/plane_stack/d3_slot.py`)

| Golfer | Swings | Median [p10, p90] | Under (< 0), fires | Amber low (0–0.25) | In the corridor | Amber high (0.75–1) | Above (> 1), fires |
|---|---|---|---|---|---|---|---|
| Mark (7i 91, GW 15) | 106 | 0.87 [0.23, 1.31] | 4 | 8 | 26 | 30 | **38** |
| Nick (7i) | 28 | 0.90 [0.29, 1.21] | 0 | 1 | 10 | 6 | **11** |
| Jess (8i) | 20 | 0.16 [−0.02, 0.32] | **4** | 12 | 4 | 0 | 0 |
| Chris (7i) | 2 | 0.75 | 0 | 0 | 1 | 1 | 0 |
| Paddy | 0 | – (no down-the-line ball) | | | | | |

- 57 of 156 would fire: 49 above the window and 8 under it. The degrees norm fired `steep_backswing_plane` on 9 of 173
  (`swing_plane_area.md`), so this is a large change in what the pair says.
- Mark's above-the-window shots sit in two sessions: 7 Oct (17 of 47, median 0.96) and 8 Oct (19 of 29, median 1.12).
- Jess's low readings agree with §2.3: she keeps the club on her set-up plane (climb +2.2° over a 13.5° window).
- Where the shoulder plane is missing, the pair is now not assessable, rather than graded on the climb in degrees.

**Tests:**
- `club_slot_top_test` (new):
  - the known window and σ;
  - the grader's own reading through the written document, and the numerator as the grader's delta;
  - each refusal code and the knob;
  - σ kinds;
  - records, including `club.trackRefused` as the reason given.
- `live_measure_source_test` (D3 block; measures 217 → 218).
- `metric_catalogue_test` (138 descriptors, 67 PointInTime).
- `axis_direction_test`, `swing_doc_test` (`versions.clubSlotTop`), `tuned_constants_parity_test`, and the `swing_themes`
  golden (the pair's family is now `clubSlotTop`).

**For Mark's review**
- The norm's effect: 49 above-the-window faults on Mark's and Nick's 7-irons, against 9 under the degrees norm.
- Whether a missing shoulder plane should fall back to grading the climb in degrees. Not built: the brief retires that grading.
- The 4° minimum window.
- The rewritten `check`.

**Library effect:** none until a re-analysis writes `clubSlotTop`. ⚠ A regrade WITHOUT the sweep would leave the pair not
assessable on every stored document (they carry no `clubSlotTop`), so the regrade must follow the sweep, as §8 item 4 plans.

### D6 (Mark's decisions of 13 Nov evening, and the follow-ups, 13 Nov 2026)

Mark, 13 Nov: "go with your recommendations on all of them, esp. the follow-ups" (§10, evening).

**1. Takeaway firing edges** (`norms.json` `m_takeawayPlane`)
- σLo 1, σHi 1.5: `flat_takeaway` fires below −2°, `steep_takeaway` above +3°, amber from −1° / +1.5°. The citation states the
  firing edges and the evidence (the loop steps from 11.5 % to 15.9 % between 1–2° and 2–3° under the plane; over the top at
  about 9 % of loop, firm at 13 %).
- The library (`per_swing.csv`, read-only): `flat_takeaway` fires on Mark 95 of 123 (34 at the old −4) and Nick 8 of 29 (1
  before); Jess fires on none (10 of 20 amber high); Chris 1 of 2 steep. Mark's median of −3.1° now fires.

**2. The 3-D bent lead arm as a change from address** (as built after round 2, below)
- `m_leadElbowBendTop3d` (id kept: the signal, the axis and stored ledgers name it, and it is still the bend at the top)
  reads **`leadElbowBendChange3d`** at P4. That is a reading `Skeleton3dBodyStage` derives per swing, because the pack has
  no hold-median reference: the measured bend at the top, as the grader reads `leadArmFlexion3d` there (`grader_sample.h`),
  LESS the median of the measured bend over the **address hold**, the fit frames from P1 − 150 ms to P1, where the arm is
  still (`body3d.elbowHoldUs`, `body3d.elbowHoldMinFrames` 1, PROVISIONAL).
  - **Why 150 ms:** P1 is the end of the hold (`addressHoldEndFrame`), and the fit keeps only address − 150 ms onward
    (`trimResultBefore`). So that is all of the hold the fit has: a median 6 fit frames on the library, 4 of them with the
    elbow measured.
  - Round 1 built it as `delta` P1 → P4 on the curve (the ±15 ms window at P1). Mark replaced that with the hold after the
    review.
- σ: the top reading's ⊕ the hold frames' median σ (not ÷ √n: one still pose, as `shoulderPlaneFromHold` argues),
  Propagated.
- Where either end has no measured elbow it is not built, and the Refused **`body3d.leadElbowUnmeasured`** says which (round
  2, below).
- **Norm, re-seated** (ceiling): μ 0, tolerance 8, amber from +8, firing above +16. That keeps D2's 16° firing distance from
  the straight arm, now each golfer's own.
- **The library** (`docs/research/data/plane_stack/d6_hold_probe.py`, read-only, summarised by `d6_elbow_change.py`; the
  stored v17 fits with FB's measured rule replayed). **79 swings keep a reading** (86 have a measured top; 70 with round 1's
  ±15 ms window):

  | Golfer | Top reading | Hold reading | Both | Change: median [p25, p75] | Fires (> +16) | Amber (+8 … +16) |
  |---|---|---|---|---|---|---|
  | Mark | 81 | 118 | 75 | +12.7 [+9.1, +16.4] (top 28.7, hold 15.1) | 21 | 40 |
  | Jess | 3 | 20 | 2 | +4.0, +5.8 | 0 | 0 |
  | Paddy | 2 | 11 | 2 | +6.3, +9.3 | 0 | 1 |
  | Nick | 0 | 30 | 0 | – | – | – |
  | Chris | 0 | 8 | 0 | – | – | – |

  - Mark's real bend at the top is about 29°. Against his own hold (15.1°) it is +12.7°: amber on most swings, firing on 21
    of 75.
  - ⚠ **The hold does not cut the per-swing noise on the stored fits.** Mark's change scatters with SD 5.5° (5.4° with the
    window), and his address and top readings correlate at r 0.21 (0.16 with the window). The fit's hold is only 150 ms, not
    much longer than the window. What the hold buys is coverage: 75 of Mark's 81 top readings are kept, against 67. Cutting
    the noise would need the fit to keep more of the hold (a change to `trimResultBefore`, not made).

**3. `chicken_wing`**
- `reportHold` is kept. **To be revisited after the sweep**, once the new fold reading (D5) is seen in use.
- `domain: "wrist"` removed. The wrist domain goes from **15 to 14** conditions. The Wrist tab (Charts) no longer lists
  `chicken_wing` and the wrist score no longer weighs it (0.6, the engine's `chicken_wing` rule). The engine's
  `chicken_wing` rule is now the one fault rule whose heir is outside the domain (`wristDomainExemptEngineRules`,
  `wrist_domain_score.h`). The condition still grades in its group, arms.

**4. Why P8 was refused** (`Club/p8_refusal_gate.h`)
- `locatePTimes`' cause is kept on the track (`ShaftTrack2D::p8Refusal`, persisted as `analysis.club.diag.p8Refusal`).
- After the run, where the document's ladder has no P8, a **Refused** record `positions.p8Refused` is fired on subject
  `position.p8`, with `code` `noCrossing` / `late` / `wrongSense` / `verticalStall` and the window and stall the tracker used.
  - **Refused, not Downgraded:** the readings it explains are not produced. Downgraded means a value kept at lower trust.
  - **The subject is the phase, and `covers` is empty.** The readings that need P8 (`m_leadKneeFlex`, `m_leadArmToTorso`, the
    seven `_p8` changes) read keys that also read fine at other positions. A cascade on those keys would mark them refused
    everywhere.
  - **The phase-level reason the grader lacked:** `LiveMeasureSource::gateCause` names a phase record for a measure value()
    left "not segmented" when a phase its reducer needs has no value in its reading (`gates::phaseSubject`). It comes before
    the key's own records. The cell reads "P8 (the shaft level after impact) not timed".
  - Not fired for `noImpact` (no P7 either), or where the ladder has a P8 (the IMU segmenter's stands).
- Golfer words: "the club could not be followed far enough into the follow-through on this swing to find the moment the shaft
  is level again after impact, so the readings that need that moment are not judged". Subject: "the moment the shaft is level
  again just after impact".

**5–6. Recorded:** `leadArmToTorso` in Arms is confirmed (`consistency_design.md` decision 11); the arm-lift norm and the
one-norm stand-in are accepted as built (§10). No code change.

**7. The generated tables in the metric-catalogue developer guide**
- `armLiftTop`'s `signPositive` now carries D2's wording ("the lead UPPER ARM (shoulder → elbow, never the wrist: a bent arm
  looks lifted) above the shoulders at the top — lifted"); its group stays Arms (the descriptor's, pinned by
  `metric_catalogue_test`).
- `gen_sign_table.py` was run once. The diff is exactly: `armLiftTop` moves from Swing plane to Arms with the same wording, and
  a `leadArmFlexion3d` row is added. Appendix A still lacks 40 rows (unchanged, pre-existing).
- The JOIN table (Appendix A0) is regenerated from its snippet: three rows (`lumbarExtension`, `thoracicFlexion`,
  `pelvisLiftBelt`). The old copy ranked 19, hand-marked three as live, and listed the slot pair under `swingPlane`.

**8.** `tilt_vs_bend::graderReadingAt` and its two types move, unchanged, to `src/Body/grader_sample.h` (`grader::`), used by
`shoulder_tilt_vs_bend.h` and `club_slot_top.h`. A pure refactor: the tests pass unchanged.

**9. The slot's camera claim, softened** (`club_slot_top.h`, the catalogue's `howToRead`, the `m_clubSlotTop` citation). The
evidence (an inclination moves under 1° for ±15° of camera turn) covers camera TURN, not camera POSITION. All three planes are
view planes from the down-the-line camera. Mark's 7-iron reads 0.71 on 4 July, 0.96 on 7 Oct and 1.12 on 8 Oct, with 36 of
his 38 "above the slot" swings in those two October sessions. So the reading is not yet shown to cancel camera position, and a
frame check is pending.

**Tests:**
- `live_measure_source_test`:
  - the takeaway edges;
  - the bent arm on `leadElbowBendChange3d`, including its ceiling and `body3d.leadElbowUnmeasured` named as the reason for
    each code;
  - each P8 cause's record, sentence and reason on `m_leadKneeFlex`, `m_leadArmToTorso` and `m_leadKneeFlex_p8`, with the
    capital P;
  - the persisted names' round-trip.
- `skeleton3d_body_metrics_test` §16:
  - 12° in the hold and 30° at the top read +18° with σ;
  - an elbow unseen at P1 itself but seen earlier in the hold still reads;
  - the codes `address`, `top` and `both`;
  - the merge publishes the reading, or the record when there is none.
- `upper_body_metrics_test` §16h′: the wrist sensors erase the change and its record.
- `club_slot_top_test`: `club.slotCameraAssumed` on an uncalibrated slot, once, and none on an unread slot.
- `wrist_domain_score_test` and `core_pack_test`: 14 wrist conditions, the exempt engine rule, the eleventh gate-words subject.
- `model_browser_test`: eleven readings.
- `metric_catalogue_test`: 139 descriptors, 68 PointInTime, Arms 9, the new rung.
- `tuned_constants_parity_test`: the hold constants.
- `swing_doc_test`: `diag.p8Refusal` round-trip.

**For Mark's review**
- The takeaway now fires `flat_takeaway` on 95 of Mark's 123 swings.
- The hold reference keeps more swings but not less noise (above). A longer fit before P1 is the lever.

**Round 2 (the D6 review, and three changes Mark approved, 13 Nov)**
- **The review's defect:** `model_browser_test` pinned ten gate-word subjects; `position.p8` makes eleven. Re-pinned with
  the reason.
- **The clause keeps the position's capital:** `notJudgedClause` lower-cases a subject's label except a P-position's own name,
  so the P8 reason reads "P8 (the shaft level after impact) not timed".
- **The sign table:** `leadArmFlexion3d` (the absolute bend, which cannot go negative) is shown, not graded. The graded
  `leadElbowBendChange3d` has its own row with both signs. `gen_sign_table.py` was run once. The diff is exactly the
  `leadArmFlexion3d` row losing round 1's "graded as the change" clause, and the new row. Appendix A gains the new
  descriptor's row (the generator's 40 pre-existing missing rows stay).
- **The bent arm against the hold** (item 2 above).
- **`body3d.leadElbowUnmeasured`** (Refused, subject and covers `leadElbowBendChange3d`; `code` `top` / `address` / `both`;
  evidence `topFrames`, `holdFrames`, `holdMeasured`, `holdMs`).
  - Fired by `Skeleton3dBodyStage` on a two-camera swing with a Top and an Address where either end has no measured elbow.
    It is erased with the 3-D elbow where the wrist sensors read.
  - `bent_lead_arm`'s measure now names it ("… lead elbow not seen by both cameras") instead of "not segmented".
  - Golfer words: "both cameras didn't see your lead elbow clearly at the top or at address, so how much it bends can't be
    judged".
  - On the library (replayed) it would fire on Mark 52 two-camera swings (top 43, address 6, both 3), Nick 37 (top 30, both
    7), Jess 21, Chris 8 and Paddy 9. Those are the swings that read no change.
- **`club.slotCameraAssumed`** (Downgraded, subject and covers `clubSlotTop`, evidence `widthDeg`). It is fired by
  `deriveClubSlotTop` on every slot reading while the down-the-line camera is uncalibrated, which is the flag
  `club3d.cameraAssumed` reads (`ShaftFusionStage::configFor(job).calibrated`); every swing today.
  - Golfer words: "measured with an uncalibrated down-the-line camera: compare it within a session, not between sessions".
  - **Why:** Mark's 7-iron reads 0.71 / 0.96 / 1.12 across four camera set-ups, and re-aiming the camera on 7 Oct (between
    swings 5 and 6) moved the address planes 2–3°, a third of his ~9° slot.
  - **Why it is slot-specific:** the takeaway is also a plane difference within one swing, but it compares the club's own
    plane at two instants, which a camera move shifts alike. Mark's 7-iron session medians (`per_swing.csv`) are −2.8 (4
    Jul), −2.6 (7 Oct) and −4.0 (8 Oct). The slot divides by the gap between two DIFFERENT lines (ball → shaft, ball →
    trail shoulder), and a camera standing elsewhere moves those by different amounts.

**Library effect:** none until a re-analysis. The P8 cause is written by the re-track the sweep already makes (FA's shaft 13);
`leadElbowBendChange3d` and its record by the 3-D body stage, which is never reused; the slot ⚠ by the step after the run.
