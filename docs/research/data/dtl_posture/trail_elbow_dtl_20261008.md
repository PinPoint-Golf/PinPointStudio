# Flying trail elbow from the DTL camera — Stage 0 (8 Oct 2026)

Why: `flying_elbow` was not assessable on 21 of 32 swings on 2026-10-08. Its only measure,
face-on `trailElbowHeight` at P4, is refused by the shoulder foreshortening gate
(`upperBody.minShoulderSpanRatio` = 0.40) whenever the turn collapses the shoulder line. That is
most swings. On all 32 swings the gated set is exactly the set where `shoulderPlaneAngle` is also
missing at Top.

Data: `trail_forearm_dtl_20261008.csv` (113 swings). Contact sheet of extremes:
`/mnt/swingdata/scratch/trail_forearm_sheet_20261008.png`. Prototype: the session scratchpad
`forearm.py` (to be folded into `tools/swinglab/dtl_posture_offline.py` with the producer).

## Quantity

The trail forearm at P4 in the DTL image is the wrist→elbow vector measured from straight down.
It is signed + when the elbow is AWAY from the ball, i.e. behind the golfer.
- Ball side is taken from the feet at Address (toes vs heels, ±150 ms), as `dtl_posture.h` does.
- Values come from `poseDtl.smoothed`; confidence comes from the raw frames, gated at 0.30.
- The reading is the median over ±40 ms of Top.
- 0° means the elbow is directly under the hands, pointing at the ground. Large + means the elbow
  points out behind.

## Results

| session | swings | DTL read | face-on read | forearm median | sd | range |
|---|---|---|---|---|---|---|
| 2026-10-05 | 31 | 29 (2 feet unresolved) | 9 | 13° | 9 | −13..26 |
| 2026-10-07 | 50 | 50 | 8 | 17° | 7 | −2..30 |
| 2026-10-08 | 32 | 32 | 11 | 19° | 5 | 4..24 |

- **Coverage:** 111/113 for DTL, against 28/113 for face-on.
- **P4 timing:** the reading at the single Top frame and the ±40 ms median differ by ≤ 2° at p90.
  The reading is not timing-sensitive.
- **Wrist keypoint:** the wrist sometimes lands mid-forearm, short of the hands. On 10-08 #32,
  #23 and 10-07 #39 this pulls the angle toward 0°. Using the pose's trail-hand track instead
  moves those by +2..+6° and raises the lowest reading on 10-05 from −13° to −3°. The session
  medians are within 1–4° either way. The 133-pt hands are a cross-check only (memory), so the
  wrist stays primary.
- **Camera framing:** 10-05 is framed differently from 10-07/08 (tighter, blurrier), and the
  wider spread comes from that session. The angle is in the image plane, so DTL camera height
  and offset bias it, as with every DTL posture metric.

## Against face-on

The face-on `trailElbowHeight` fired on 8 of the 11 swings it could read on 10-08 (36–147 %).
DTL reads those same swings at 6–21°, the same as the swings face-on called clean (10-08 #3:
face-on −43 %, DTL 21°; #5: face-on −13 %, DTL 24°). There is no relationship between the two.
The contact sheet shows the same golfer and position throughout: upper arm level and pointing
back, forearm near vertical.

## Textbook norm

No source gives a number. Searched 2026-10-08 for "flying elbow top of backswing trail forearm
angle", and for "golf biomechanics trail elbow position at top of backswing measurement study".
The consistent qualitative standard:
- MyTPI "Flying Elbow": at the top, most golfers' trail elbow points at the ground. From down the
  line, flying means it points behind the golfer. Not necessarily a fault.
- Keiser University College of Golf (Tomasi): flying is acceptable if the elbow returns in line
  with the trail hip in the downswing.
- RotarySwing: flying lays the club across the line, which leads to over the top.

So a corridor has to be set by convention, not sourced. "Points at the ground" read
geometrically means the elbow is more under the hands than behind them, i.e. the forearm is
within 45° of vertical. A stricter ±30° is the other natural line.

## Implication

Under either convention, almost none of Mark's 111 DTL swings is flying. The maximum is 30°
(10-07 #18), and 10-08 never exceeds 24°. The face-on fires look like the foreshortening artefact
reaching the swings that pass the gate, rather than a flying elbow.

`trail_elbow_deep` (the low side) still uses the face-on measure. It is out of scope here.
