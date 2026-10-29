# Ball distance from the body, by club

*2026-10-29. Status: **built**; tested on one wrong-ball swing and one "too far" swing; the library is not yet
re-analysed. Replaces the ball-distance row of `dtl_posture_design.md` §0 and closes its §6 item 5.*

## 0. Summary

`ball_too_far` fired on every golfer. Its corridor was one authored number, 130 ± 20 % of shoulder width
(110–150 %), with no source and no measurement behind it (norms.json, 27 Jul 2026), and the same for every
club. Every golfer with a 7 or 8 iron read 160–195 %:

| golfer, club | ball distance (old, % shoulder width) | Ball too far | arm hang |
|---|---|---|---|
| Mark, 7 iron (4 sessions) | 160–178 | 26 of 70 | +4 to +7° |
| Mark, gap wedge | 143 | 0 of 16 | +4.5° |
| Nick, 7 iron | 167 | 4 of 25 | −1.6° |
| Chris, 7 iron | 176 | 2 of 2 | −1.4° |
| Jess, 8 iron | 189 | 3 of 3 | +5.3° |

Arm hang, the direct reading of reaching, was inside its band for all of them. Most of the ball's distance is
the club, not the golfer. The graded reading is now the distance **less the club's reach**, in cm. One corridor
serves every club, and the golfer's own club length (the bag record, else the standard for the club named) is
what takes the club out.

## 1. What sets the ball's distance

The ball sits one club length from the hands, along the shaft. Seen down the line, the distance across the
stance line from the front of the feet to the ball is:

    ball distance = (hands past the toes) + L·cos φ

Here L is the club's length and φ is the angle of the hands→ball line below the horizontal. Coaching holds that
the first term stays the same from 9 iron to driver while the club changes the second. With standard lengths,
that puts the ball about 54 cm from the toes with a wedge, about 57 cm with a 7 iron and about 75 cm with a
driver. A single corridor in any unit cannot fit that range.

The October 2026 library confirms the split (all two-camera swings with a ball, read at address):

| session | club | wrist→ball / L | φ | hands past toes (club ruler) | hands past toes (ball ruler) |
|---|---|---|---|---|---|
| Mark 07-04 | 7 iron | 1.01 | 54° | 10.3 cm | 10.4 cm |
| Mark 10-05 | gap wedge | 0.92 | 57° | 7.1 | 6.9 |
| Mark 10-07 (bright ball) | 7 iron | 0.91 | 57° | 12.5 | 11.6 |
| Mark 10-08 | 7 iron | 0.94 | 54° | 9.1 | 8.6 |
| Nick 10-20 | 7 iron | 1.01 | 52° | 5.3 | 5.3 |
| Chris 10-24 | 7 iron | 1.01 | 51° | 11.0 | 11.1 |
| Jess 10-20 | 8 iron | 1.01 | 54° | 14.5 | 14.7 |

- The wrist→ball distance is one club length to within 10 %, so **the club is a ruler**. It agrees with the
  DTL ball ruler to 0.5 cm, and it exists on every swing that has wrists and a ball, which the ball ruler does
  not (#94).
- After the club is taken out, the hands sit **5–15 cm past the toes for every golfer and club**.

## 2. The readings

Both readings come from `dtl_posture.h`, at address:

- `ballBodyDistance`, in **cm** (was % shoulder width): toe line → ball, with cm/px = L / |wrist→ball|px. It
  is shown, not graded, and is comparable only between swings with the same club.
- `ballReachExcess`, in cm: `ballBodyDistance − L·cos φ`, the part the club does not account for. This is
  what `m_ballBodyGap` reads, so it is what Ball too close and Ball too far are graded on.

L comes from the golfer's club record (`capture.club.lengthMm`, from the bag's editable length). Without one
it is the standard length of the club the swing names (`club_standards.h`, men's, women's or junior set), and
with no club named it is the 1.12 m driver default. Each carries its σ (5 mm, 2.5 cm, 12 cm), and that σ goes
onto both readings as `AssumedInput`.

**φ is measured, not the club's catalogue lie.** The camera reads a 62° 7 iron at 49–55° on every session and
for every golfer. Whether that is the camera's height and offset or golfers really setting the hands low cannot
be told apart without a calibrated camera (`dtlShaftLie.h`: the absolute angle is not comparable across
cameras). Against the catalogue lie, every 7 iron would read about 8 cm too far, which is the old false firing
again. The catalogue lie stays where it already is, on the club record. The cost is that standing too far by
dropping the hands, with the shaft flatter, is not seen by this reading.

## 3. A ball the club cannot reach is refused

The gate is `posture.ballReach`, one record covering both readings. It refuses:

- `ballAngle`: the "ball" is outside 30–75° below the hands. On 7 Oct 2026, 18 of 34 swings found the hands'
  own shadow at 88–90°, straight under the wrists. Ball too close fired on 18 swings from it (#114).
- `ballLength`: when the picture has a ruler of its own, the ball is outside 0.8–1.2 club lengths from the
  hands.
- `wrists`: no wrists at address.
- `noClub`: no length at all.

The bounds are `dtlPosture.ballAngleMinDeg` / `MaxDeg` / `ballLeverMin` / `Max`.

## 4. The corridor

`m_ballBodyGap` uses one row for every club: centre 10 cm, σ 4 cm, heuristic. The citation records the measured
spread above, and it is not adopted as a good-swing norm: four golfers of unknown standard cannot seat one.
Ball too far starts beyond 18 cm and Ball too close below 2 cm (the hands at the toe line). No driver, wood or
hybrid swing has been graded against it yet. Paddy's twelve 24 Oct driver swings carry a recorded length and
are the first to try once re-analysed.

## 5. Checked

- `dtl_posture_test`: both readings, σ carried, mirror invariance, and the three refusals.
- 10-07 swing 5 (shadow ball): old reading 35 % (Ball too close fired); now refused, `ballAngle` 92°.
- 10-08 swing 6: old reading 182 % (Ball too far fired); now 64.0 cm from the toes, 8.5 cm beyond the 7 iron's
  reach, clean.

## 6. Owed

1. Re-analyse the two-camera library (cabin PC; `kDtlPostureStageVersion` 2), then regrade, and compare the
   Ball too far / too close counts with the 26 / 20 of 86 before.
2. A driver session down the line, to see that one row holds for the long clubs.
3. "Hands set low" (the address shaft flatter than the club's lie) as a reading of its own, once a DTL camera
   calibration makes the absolute shaft angle comparable (`camera_calibration` thread).
