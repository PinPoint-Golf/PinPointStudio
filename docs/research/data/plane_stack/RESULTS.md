# Plane stack: takeaway, climb at P3, arm lift at the top

This is a read-only pass over /mnt/swingdata (Mark, Nick, Jess, Paddy, Chris) on 13 Nov 2026. Nothing was re-analysed and nothing was written to the share.

Files, all in this folder (`docs/research/data/plane_stack/`):
- `extract.py` reads the documents and ledgers and writes `swings.jsonl`.
- `analyze.py` produces the tables (`analysis_out.txt`) and `per_swing.csv`.
- `supplement.py` covers elbow flexion, keypoint confidence and the reasons readings are missing.
- `extra.py` covers coupling and reliability.

## How the numbers were read

- **Phases** (src/Analysis/swing_analysis.h): 0 = P1, 12 = P2, 8 = P3, 2 = P4, 5 = P7. The first occurrence in `analysis.phases` is used.
- **Sampling** is the grader's (measure_sample.cpp buildPhaseGrid, series_reduce.h):
  - `swingPlaneCurve` is a fitted curve, so it is read at the instant, interpolated linearly between two valid samples.
  - Every other series is the median of its valid samples within ±15 ms, falling back to its phaseSamples entry.
  - The sampler reproduces the ledger exactly. P3 − P1 equals `m_shaftPlaneBackswing` on 156 of 156 swings. P3 equals `m_backswingPlaneAngle` on 161 of 161. shoulderTiltVsBend from the document equals the ledger on 205 of 205. The largest difference is 0.00°.
- **Handedness:** every one of the 283 analysed swings is right-handed. Lead is LEFT: 3-D joints LeftArm 8 and LeftHand 10, trail shoulder RightArm 13. DTL COCO keypoints are 5 (L shoulder), 6 (R shoulder) and 9 (L wrist).
- **3-D frame** (skeleton3d `frame`): X is face-on image-right, Y is the face-on view ray, Z is up, positions in mm.
  - A frame is used when the joints involved are tier ≥ 2.
  - At P4 every swing has 5 skeleton frames inside ±15 ms. All are tier 3 (measured), except the lead hand on 3 of Paddy's swings, which is tier 2.
  - Each quantity is computed per frame and the median is taken.
- **The four arm-lift definitions.** For every one, + means the lead arm sits more upright, above the shoulders.
  - **a1, inclination difference:** α − β.
    - α = atan2(z_wrist − z_shoulder, horizontal length) of the lead shoulder → lead wrist line. + means the hand is above the shoulder.
    - β = `shoulderPlaneAngle3d` at P4, read the grader's way. Its sign is + when the trail shoulder is higher.
    - + therefore means the lead arm rises out of horizontal more steeply than the shoulder line does.
  - **a1j:** the same as a1, but β is taken from the same joints and frames. a1 − a1j has a median of 0.0° and an IQR of ±0.1°. So the series is the joint line (route `faceOn+dtl` on every swing; none came from the chest sensor).
  - **a2, angle to the shoulder-turn plane:** asin(â · n̂).
    - â is the unit vector from the lead shoulder to the lead wrist.
    - n̂ is the spine vector (pelvis midpoint at joints 17/22 → shoulder midpoint at 8/13), with its component along the shoulder line removed.
    - The plane contains the shoulder line and is as square to the spine as a plane holding that line can be.
    - 0 means the lead arm lies in the plane the shoulders turn on (one-plane). + means it points headward out of that plane, i.e. lifted.
  - **b, DTL image proxy:** the angle between the shoulder line s (lead → trail shoulder) and the arm a (lead shoulder → lead wrist) in DTL pixels.
    - Pixels are the normalised keypoints × the encoded width and height, with y flipped to point up.
    - The sign is + when the wrist lies on the upper side of the shoulder line.
    - Raw `poseDtl.frames` are used. Each keypoint needs conf ≥ 0.30 (dtlPosture.confMin). The median is taken over ±15 ms of P4.

**Which 3-D definition is more robust: a2.**
- a1 subtracts two inclinations measured in two different vertical planes. At the top, the arm and the shoulder line point in different compass headings.
- So a steep shoulder tilt can exceed a fairly upright arm's inclination even when the arm is clearly above the shoulder-turn plane. Nick shows this: a1 is −5, a2 is +9 and DTL b is +14.
- Within a session, a1's variation is mostly the shoulder-tilt reading itself:
  - a1 against the tilt: ρ −0.69.
  - The arm's inclination alone against shoulderTiltVsBend: ρ +0.02.
- Compared with a1, a2 is:
  - steadier (within-session SD 4.5° against 5.9°);
  - less tied to the tilt (ρ −0.38);
  - closer to DTL within a session (ρ 0.47 against 0.41).
- Its only extra cost is the hip joints, and they were present on all 197 swings.

## Coverage

283 swings were analysed: 287 folders, less 3 with no swing.ppsw (Mark 08-04 swing_0003; 11-02_02 swing_0007 and swing_0009) and 1 marked excluded (Mark 10-05 swing_0008).

| input | Mark | Nick | Jess | Paddy | Chris |
|---|---|---|---|---|---|
| swings | 203 | 38 | 23 | 11 | 8 |
| two-camera 3-D fit (valid) | 126 | 37 | 23 | 11 | 8 |
| takeaway (P2 − P1) | 123 | 29 | 20 | 0 | 2 |
| climb (P3 − P1) | 106 | 28 | 20 | 0 | 2 |
| swingPlaneCurve at P3 | 106 | 28 | 22 | 0 | 6 |
| arm lift a1 / a2 | 126 | 37 | 23 | 11 | 8 |
| arm lift b (DTL) | 126 | 38 | 23 | 11 | 8 |
| shoulderTiltVsBend | 126 | 37 | 23 | 11 | 8 |
| m_trailElbowRise | 72 | 14 | 23 | 11 | 8 |
| m_leadUpperArmToChest | 167 | 37 | 22 | 10 | 8 |
| m_handPathLoop | 126 | 37 | 22 | 11 | 8 |
| club path | 72 | 27 | 23 | 11 | 0 |
| spin axis | 78 | 35 | 23 | 11 | 8 |
| face to path | 36 | 27 | 23 | 10 | 0 |

## Per golfer: n, median [IQR], degrees

Chris is shown in this table but is left out of every pooled statistic.

| quantity | Mark | Nick | Jess | Paddy | Chris |
|---|---|---|---|---|---|
| **1. Takeaway: plane P2 − P1** | 123: −3.1 [−4.0, −2.2] | 29: −1.6 [−2.1, −1.1] | 20: +1.6 [0.9, 2.1] | – | 2: +3.0 [2.8, 3.3] |
| **2. Climb: plane P3 − P1** (= m_shaftPlaneBackswing) | 106: 8.6 [5.0, 10.4] | 28: 13.1 [5.1, 16.9] | 20: 2.2 [1.2, 2.8] | – | 2: 13.6 [12.7, 14.6] |
| **2. Plane at P3** (= m_backswingPlaneAngle) | 106: 65.6 [61.8, 67.7] | 28: 65.8 [57.0, 69.6] | 22: 54.9 [53.0, 56.1] | – | 6: 76.4 [67.7, 78.1] |
| **3a1. Arm lift, inclination difference** | 126: 12.5 [5.6, 18.1] | 37: −5.1 [−7.5, −0.2] | 23: 21.6 [15.9, 24.5] | 11: 7.6 [6.8, 9.5] | 8: 17.2 [15.3, 17.9] |
| **3a2. Arm lift vs shoulder-turn plane** | 126: 13.9 [9.4, 17.2] | 37: 9.1 [7.4, 11.8] | 23: 22.4 [19.1, 25.2] | 11: 7.9 [6.1, 9.4] | 8: 25.7 [23.5, 27.8] |
| **3b. Arm lift, DTL image** | 126: 19.3 [17.0, 22.1] | 38: 13.7 [11.8, 14.9] | 23: 18.6 [15.2, 20.1] | 11: 8.4 [6.4, 9.4] | 8: 20.7 [20.1, 21.2] |
| shoulderPlaneAngle3d at P4 | 30.2 [27.1, 32.9] | 36.5 [29.1, 40.4] | 26.9 [25.1, 30.8] | 14.3 [10.8, 15.6] | 11.2 [9.2, 14.3] |
| lead-arm inclination (3-D) at P4 | 42.7 [37.6, 46.2] | 30.1 [28.2, 32.9] | 48.8 [45.3, 51.5] | 22.2 [19.1, 25.1] | 29.1 [26.1, 29.6] |
| shoulderTiltVsBend | −6.1 [−10.5, −2.9] | −3.2 [−8.9, 1.9] | −2.6 [−4.1, 1.7] | −3.9 [−4.8, −2.5] | **−24.0** [−25.6, −21.5] |
| m_trailElbowRise (% sh. width) | 27.3 [1.6, 63.0] | 7.4 [−12.2, 11.4] | 32.3 [26.6, 40.1] | −32.1 [−63.5, 14.0] | 19.5 [17.4, 20.9] |
| m_leadUpperArmToChest (%) | 50.3 [48.7, 52.7] | 45.3 [44.4, 59.6] | 45.9 [43.9, 47.7] | 53.0 [51.6, 54.5] | 51.6 [51.3, 52.2] |
| m_handPathLoop (% rise) | 16.8 [13.3, 20.0] | 11.6 [10.8, 13.2] | 6.4 [4.3, 9.1] | 23.1 [20.5, 26.4] | 6.9 [5.6, 9.1] |
| club path | −1.6 [−3.0, −0.5] | +2.1 [1.0, 2.7] | +4.3 [3.8, 4.9] | +5.4 [4.5, 6.1] | – |
| spin axis | +1.4 [−1.1, 5.1] | −2.5 [−4.9, 0.4] | −12.6 [−15.8, −8.3] | −13.8 [−29.8, 0.0] | −1.2 [−4.4, 0.1] |
| face to path | +0.7 [−1.0, 2.2] | −2.4 [−3.7, −1.2] | −8.0 [−10.7, −6.0] | −1.9 [−5.9, −0.7] | – |

Clubs: Mark hit 98 7-irons and 28 gap wedges (2026-10-05). Nick and Chris hit 7-irons, Jess 8-irons and Paddy **drivers**.

Mark's gap-wedge session reads much lower than his 7-iron sessions:

| Mark | a1 | a2 | b |
|---|---|---|---|
| gap-wedge session (2026-10-05) | 2.5 | 4.9 | 18.4 |
| 7-iron sessions | 12–20 | 13–17 | 19–24 |

## Same day, 2026-10-20

Mark's session that day was **face-on only** (no DTL stream and no 3-D fit), so he has launch-monitor data only. The plane comparison is Jess against Nick.

| quantity | Jess (8i, 3 sessions) | Nick (7i, 3 sessions) | Mark (face-on only) |
|---|---|---|---|
| takeaway P2 − P1 | 20: +1.6 [0.9, 2.1] | 26: −1.5 [−1.9, −1.1] | – |
| climb P3 − P1 | 20: 2.2 [1.2, 2.8] | 25: 13.4 [5.1, 17.0] | – |
| plane at P3 | 22: 54.9 [53.0, 56.1] | 25: 65.8 [57.1, 69.6] | – |
| arm lift a1 | 23: 21.6 [15.9, 24.5] | 26: −6.2 [−8.7, −4.9] | – |
| arm lift a2 | 23: 22.4 [19.1, 25.2] | 26: 8.8 [6.2, 10.6] | – |
| arm lift b (DTL) | 23: 18.6 [15.2, 20.1] | 26: 13.6 [12.1, 14.7] | – |
| shoulder tilt 3-D at P4 | 26.9 | 38.2 | – |
| shoulderTiltVsBend | −2.6 | −0.6 | – |
| hand-path loop | 6.4 | 11.7 | – |
| club path | +4.3 [3.8, 4.9] | +1.3 [0.7, 2.5] | 7: −2.9 [−4.0, −2.1] |
| spin axis | −12.6 [−15.8, −8.3] | −3.5 [−5.3, −1.0] | 7: +4.3 [2.7, 5.4] |

Each golfer is consistent across their three sessions that day (session medians):

| golfer | takeaway | climb | a2 | b |
|---|---|---|---|---|
| Jess | 1.4 to 1.8 | 1.9 to 2.8 | 21.1 to 23.3 | 17.8 to 18.7 |
| Nick | −1.1 to −2.0 | 5.4 to 17.4 | 7.3 to 10.4 | 12.3 to 14.2 |

Nick's climb jumps between sessions while his takeaway stays steady. On 2026-11-02, Nick's 3-D shoulder tilt reads 23.5, against 37–41 on 10-20, which moves his a1 to +5.1 while a2 stays at 13.9. That points to the shoulder tilt reading depending on the set-up.

## Agreement between the arm-lift versions

| pair | pooled ρ (4 golfers, n = 197) | within-session ρ (n = 195, 12 sessions) |
|---|---|---|
| a1 vs b (DTL) | 0.60 | 0.41 (p < 0.001) |
| **a2 vs b (DTL)** | 0.47 | **0.47** (p < 0.001) |
| a1 vs a2 | 0.79 | 0.63 |

- a2 vs b by golfer: Jess 0.83, Nick 0.45, Mark 0.42, Paddy 0.15 (n = 11), Chris −0.10.
- The golfers' ranking on b matches a2 except that Mark and Jess swap places.
  - a2: Jess > Mark > Nick > Paddy.
  - b: Mark ≈ Jess > Nick > Paddy.
- b has the smallest within-session SD (3.4°, against 4.5° for a2 and 5.9° for a1), but it compresses the differences between golfers: golfer medians span 8–19°, against 8–22° for a2. It also depends on where the DTL camera sits.
- b runs 4° above a2: median difference −3.7, IQR [−8.0, −0.6].

## Within-session rank correlations (Mark, Nick, Jess, Paddy)

Method: each variable is ranked within each session that has at least 3 pairs. The ranks are standardised, pooled, and the Pearson correlation of the pooled ranks is taken. p comes from 5000 permutations within sessions. The p values are not adjusted, and this is 20 tests, so about one is expected below 0.05 by chance.

| x | club path | spin axis | face to path | hand-path loop | shoulderTiltVsBend |
|---|---|---|---|---|---|
| arm lift a1 | −0.03 (n 89, 8 s, p .80) | +0.07 (97, 9, .53) | +0.01 (60, 7, .97) | −0.00 (194, 12, .97) | **−0.71** (195, 12, <.001)* |
| arm lift a2 | −0.22 (89, 8, **.04**) | +0.14 (97, 9, .20) | +0.04 (60, 7, .78) | +0.06 (194, 12, .39) | −0.37 (195, 12, <.001)* |
| arm lift b (DTL) | −0.24 (89, 8, **.03**) | +0.09 (97, 9, .41) | −0.01 (60, 7, .96) | −0.07 (194, 12, .33) | −0.26 (195, 12, <.001) |
| takeaway P2 − P1 | −0.11 (64, 6, .39) | −0.04 (72, 7, .75) | +0.01 (37, 5, .95) | **−0.51** (168, 10, <.001) | −0.18 (168, 10, .02) |

\* These are partly mechanical. a1 = α − β and shoulderTiltVsBend = β − bend share the 3-D shoulder tilt β with opposite signs; the arm's inclination alone against shoulderTiltVsBend is +0.02. a2 uses the shoulder line to build its plane. The **b** figure (−0.26) is the clean one, since b does not use the 3-D fit: when the shoulders tilt more at the top, the arm sits a little lower against them.

- **The takeaway predicts the loop:** ρ −0.51 over 168 swings in 10 sessions. A swing taken away flatter than its set-up (P2 below P1) has a bigger hand-path loop. Both readings come from the DTL camera but from different trackers (shaft and hands), and within a session the camera does not move. It is the strongest signal here.
- **Arm lift and club path:** a2 and b show a weak negative relation (−0.22, −0.24; n = 89, 8 sessions). More arm lift goes with a path further out-to-in. It is borderline once multiple testing is allowed for.
- None of the arm-lift versions or the takeaway relates to spin axis or face to path.
- Context checks:
  - Arm lift against m_trailElbowRise: a1 +0.30, a2 +0.43, b +0.35 (n = 89). Higher arms go with a higher trail elbow, as expected.
  - Arm lift against m_leadUpperArmToChest: about 0.
  - Climb against club path: −0.30 (n = 52, p .045).
  - Club path against spin axis: −0.34 (n = 133), the expected sign for a right-hander.
- Chris, from his one session of n = 8: a1 and a2 against shoulderTiltVsBend are −0.83 and −0.93, while b is 0.00. His 3-D arm lift follows his tilt reading, and his DTL arm lift does not.

## For norm seating: pooled distribution of the new quantities

**These are four golfers of unknown standard (Mark, Nick, Jess, Paddy), not a good-swing norm.** Mark contributes 64% of the swings, so a golfer-weighted column (each golfer counts equally) is given too.

**All clubs:**

| quantity | n (M/N/J/P) | p10 | p25 | median | p75 | p90 | golfer-weighted median [p10, p90] |
|---|---|---|---|---|---|---|---|
| Takeaway P2 − P1 | 172 (123/29/20/0) | −5.0 | −3.7 | −2.6 | −1.1 | +0.7 | −1.2 [−4.0, 2.1] |
| Climb P3 − P1 | 154 (106/28/20/0) | 1.5 | 4.0 | 7.6 | 10.9 | 14.4 | 5.4 [0.6, 16.0] |
| Plane at P3 | 156 (106/28/22/0) | 55.5 | 58.6 | 64.3 | 67.7 | 69.6 | 59.4 [53.1, 69.7] |
| Arm lift a1 | 197 (126/37/23/11) | −5.0 | 2.7 | 11.8 | 17.7 | 22.4 | 8.3 [−5.9, 23.2] |
| Arm lift a2 (recommended 3-D) | 197 (126/37/23/11) | 4.9 | 8.6 | 13.4 | 17.5 | 21.6 | 11.9 [4.8, 24.3] |
| Arm lift b (DTL proxy) | 198 (126/38/23/11) | 10.3 | 14.2 | 17.8 | 20.7 | 23.3 | 14.5 [6.5, 22.0] |

**7- and 8-irons only** (Paddy's drivers and Mark's gap wedges out):

| quantity | n (M/N/J/P) | p10 | p25 | median | p75 | p90 | golfer-weighted median [p10, p90] |
|---|---|---|---|---|---|---|---|
| Takeaway P2 − P1 | 145 (96/29/20/0) | −4.9 | −3.7 | −2.5 | −1.0 | +0.9 | −1.2 [−4.0, 1.9] |
| Climb P3 − P1 | 139 (91/28/20/0) | 1.5 | 4.2 | 8.2 | 11.0 | 14.7 | 5.7 [0.6, 16.0] |
| Plane at P3 | 141 (91/28/22/0) | 55.1 | 57.9 | 64.9 | 67.7 | 69.7 | 59.4 [53.1, 69.7] |
| Arm lift a1 | 158 (98/37/23/0) | −5.9 | 4.7 | 13.5 | 18.9 | 23.6 | 13.8 [−6.8, 24.7] |
| Arm lift a2 | 158 (98/37/23/0) | 7.6 | 11.0 | 14.5 | 18.2 | 21.6 | 15.4 [7.4, 25.0] |
| Arm lift b (DTL) | 159 (98/38/23/0) | 11.9 | 14.5 | 18.2 | 21.5 | 23.4 | 16.5 [10.3, 22.9] |

Paddy has no takeaway or climb reading, so the plane rows are three golfers. Paddy's arm lift was read with a driver. The between-golfer spread is several times the within-session spread:

| quantity | within-session SD | golfer medians |
|---|---|---|
| takeaway | 1.4 | −3.1 to +1.6 |
| climb | 3.9 | 2.2 to 13.1 |
| a2 | 4.5 | 7.9 to 22.4 |
| b | 3.4 | 8.4 to 19.3 |

So these quantities separate golfers. With four golfers they cannot say where "good" lies.

## Data-quality notes

- **3-D coverage.** 77 of Mark's swings have no two-camera fit:
  - 33 have no skeleton3d at all (08-04, 08-18 ×2, 09-09, 11-02 ×2: no DTL stream recorded).
  - 44 are face-on only: the reason is "the 3-D skeleton needs the down-the-line camera". This includes the whole 2026-10-20 session.
  - Nick 10-13_02 swing_0001 has a DTL pose (so b) but no valid 3-D fit.
  - Every rig is uncalibrated (`camera.calibrated` false).
- **swingPlaneCurve missing.** The club track was refused (gate `club.trackRefused`, coverage under 0.6) on:
  - all 11 of Paddy's driver swings;
  - 8 of Nick's 11-02 session;
  - 2 of Mark's and 1 of Jess's.
- **Chris's swingPlaneCurve exists but is masked invalid** from P1 to past P2 on 6 of 8 swings (about 100 samples, roughly 500 ms). So takeaway and climb exist on only 2 of his swings, and P3 on 6. Mark has 17 swings with P2 but no P3 reading.
- **Chris's tilt.**
  - His 3-D shoulder tilt at the top is 11.2, with a range of 2.8 to 25.5, against a forward bend at address of 35.2. That gives shoulderTiltVsBend −24, where the other golfers sit at −2.6 to −6.1.
  - His DTL b of 20.7 is ordinary. His a1 of 17.2 and a2 of 25.7 (the highest of anyone) both rise as his tilt reading falls (ρ −0.83 / −0.93 within the session), and b does not.
  - This is consistent with the 3-D shoulder line failing, not with unusual arms. a2 is contaminated too, so it is not immune to this failure.
  - Paddy's tilt is also low (14.3), but his forward bend is 18 (very upright, with a driver), so his shoulderTiltVsBend of −3.9 is normal.
- **Jess's leadArmToTorso** (the series in her document, not used above):
  - P1 3.0, P4 84.5 [75.9, 107.5], P7 5.1.
  - **P8 78.8 [61.2, 152.9] and Finish 114 [7.1, 156.8]** are wildly spread after impact. Values near 150° and near 7° at the finish look broken (a keypoint swap or wrap), not like a chicken wing.
  - Her m_leadUpperArmToChest (45.9) is normal.
- **Keypoint confidence at the top (DTL).**
  - Medians per golfer:

    | keypoint | median confidence | p10 |
    |---|---|---|
    | lead shoulder | 0.86–0.92 | – |
    | lead wrist | 0.87–0.92 | – |
    | lead elbow | 0.7–0.9 | – |
    | trail shoulder (lowest) | 0.73–0.84 | 0.64–0.69 |

    The trail shoulder is turned away from the DTL camera at the top.
  - Two swings lost the DTL pose at the top (conf about 0.1 on all three keypoints, so no b): Mark 2026-10-05 swing_0002 and swing_0003.
  - The 3-D joints at P4 are tier 3 everywhere, except 3 of Paddy's lead hands at tier 2.
- **The lead elbow is bent at the top (3-D):**

  | golfer | lead-elbow flexion at P4, median [IQR] |
  |---|---|
  | Mark | 31° [27, 35] |
  | Nick | 18° [7, 34] |
  | Jess | 12.5° |
  | Paddy | 18.5° |
  | Chris | 4° |

  A bent lead arm turns the shoulder → wrist line away from the upper arm, and every arm-lift version here uses that line. Mark's 31° is large and may be partly the fit; check it before seating a norm.
- **The shoulder tilt depends on the session.** Nick reads 37–41 on 10-20 and 23.5 on 11-02, so a1 moves sign between his sessions while a2 moves less. All shoulderPlaneAngle3d readings came by the `faceOn+dtl` route.
- **Cross-checks passed.** The ledger and the recomputed values agree exactly for m_shaftPlaneBackswing, m_backswingPlaneAngle and m_shoulderTiltVsBend. shoulderPlaneAngle3d equals the joint shoulder line (ρ 1.00).

## D2 (13 Nov 2026): the arm lift measured to the ELBOW, and the 3-D lead elbow

Mark, 13 Nov: arm lift is measured on the lead UPPER ARM (shoulder → elbow), never to the wrist, because a
bent lead arm makes the arms look lifted and is its own fault. `d2_upper_arm.py` repeats the definitions above
with the wrist replaced by the elbow (COCO 7 / joint `LeftForeArm` 9 for a right-hander), and reads the 3-D lead
elbow where the fit really measured it. `d2_summary.py` makes the tables; its output is `d2_summary_out.txt`.
Read-only, the same 283 swings, stored documents (the v17 fits).

| golfer | b to the elbow (image) | a2 to the elbow (3-D) | b to the wrist | a2 to the wrist |
|---|---|---|---|---|
| Mark | 127: +7.1 [+4.3, +11.0] | 127: +4.7 [−0.8, +9.6] | +19.1 | +13.9 |
| Nick | 38: −2.4 [−9.1, +0.2] | 37: +4.1 [+1.2, +7.2] | +13.7 | +9.1 |
| Jess | 23: +15.5 [+12.6, +17.9] | 23: +21.0 [+17.6, +24.1] | +18.5 | +22.4 |
| Paddy (driver) | 11: +5.6 [+3.2, +7.0] | 11: +3.6 [+0.4, +8.5] | +8.3 | +7.9 |
| Chris (apart) | 8: +15.7 [+15.1, +16.8] | 8: +26.2 [+23.8, +28.1] | +20.9 | +25.7 |

- To the wrist reads 12° (Mark) and 15° (Nick) higher in the image than to the elbow: their lead elbows bend.
  Jess and Paddy move 2–3°.
- Golfer-weighted (Chris out) p2.5 / p10 / median / p90 / p97.5: image −11.4 / −3.0 / +6.0 / +16.4 / +19.6;
  3-D −12.3 / −2.1 / +7.2 / +21.9 / +26.8. Within-session SD: image 3.4°, 3-D 4.3°.
- To the elbow the image reads a median 1.3° BELOW the 3-D (IQR −5.7 … +4.3), within-session ρ 0.46. To the wrist
  it ran 4° above.
- The image reading's σ (keypoint σ × max(1, 0.8 / conf) through the angle): golfer medians 1.1–1.2°, Paddy 2.0°.
  The trail shoulder at the top scores a median 0.7–0.8.
- Every swing with a 3-D fit also has an image reading, so while the cameras are uncalibrated the 3-D reading
  stands on none of them.
- FB's shoulder-views check (replayed: the DTL shoulder line against the fit's, median over the top > 7°) fires on
  Mark 17 of 127, Nick 4 of 37, Paddy 1 of 11, Chris 6 of 8 and Jess 0.

**The 3-D lead elbow at the top, only where both cameras saw it** (stored tier 3 AND the fit's elbow within
max(3σ, 15 px) of the elbow keypoint in both views, FB's rule):

| golfer | swings with a measured elbow at the top | bend at the top | all tier ≥ 2 frames | measured at address |
|---|---|---|---|---|
| Mark | 81 of 127 (53 % of top frames) | 28.7 [25.2, 31.2] | 31.1 | 14.5 (108 swings) |
| Jess | 3 of 23 (12 %) | 13.2 | 12.5 | 8.5 (10) |
| Paddy | 2 of 11 (5 %) | 13.2 | 18.5 | 6.6 (11) |
| Nick | 0 of 37 | – | 17.6 | 4.9 (28) |
| Chris | 0 of 8 | – | 4.2 | 18.6 (8) |

The fit reads a straight address arm at 5–19° on measured frames, so a bend at the top is read against that, not
against 0.
