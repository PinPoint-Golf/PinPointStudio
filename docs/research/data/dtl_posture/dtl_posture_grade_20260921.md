# Down-the-line posture and swing plane — graded 2026-09-21

Run: `build/fusion_run/run24.sh <root>` (07-04 s1–15 taped 7-iron, 06-11 s1–9 bare wedge; pinned poses). Table: `python3 tools/swinglab/dtl_posture_grade.py <root>`.
No truth exists for any row: this is repeatability and plausibility. `pelvisThrust` is absent on 06-11 because no ruler passed the shoulder-width check (the shadow-cue ball is not a ruler; the face-on ruler carried across implied 52–55 cm of shoulders).

### 2026-06-11  (9 swings)

| measure | at | unit | n | median | sd | min | max |
|---|---|---|---|---|---|---|---|
| balanceHeelToe | P1 | % foot length | 9 | 63.9 | 3.9 | 56.2 | 68.5 |
| ballBodyDistance | P1 | % shoulder width | 9 | 159.3 | 3.9 | 151.3 | 161.6 |
| leadKneeFlexion | P1 | ° | 9 | 19.1 | 2.1 | 15.4 | 21.1 |
| leadKneeFlexion | P4 | ° | 9 | 39.9 | 1.5 | 37.3 | 41.6 |
| leadKneeFlexion | P7 | ° | 9 | 19.8 | 3.1 | 13.1 | 21.9 |
| leadKneeFlexion | P8 | ° | 9 | 4.7 | 5.4 | 2.8 | 18.3 |
| spineForwardBend | P1 | ° | 9 | 36.2 | 1.4 | 34.0 | 38.3 |
| spineForwardBend | P4 | ° | 9 | 30.7 | 0.9 | 28.7 | 31.3 |
| spineForwardBend | P7 | ° | 9 | 28.1 | 1.2 | 26.8 | 30.3 |
| swingPlane | P5 | ° | 8 | 3.2 | 1.8 | -0.2 | 5.2 |
| swingPlane | P6 | ° | 8 | 3.2 | 1.8 | -0.2 | 5.2 |
| trailKneeFlexion | P1 | ° | 9 | 15.5 | 2.2 | 12.6 | 18.3 |
| trailKneeFlexion | P4 | ° | 9 | 1.2 | 1.2 | 0.3 | 3.4 |
| trailKneeFlexion | P7 | ° | 9 | 29.8 | 2.4 | 28.8 | 36.5 |

### 2026-07-04  (15 swings)

| measure | at | unit | n | median | sd | min | max |
|---|---|---|---|---|---|---|---|
| balanceHeelToe | P1 | % foot length | 15 | 52.6 | 5.9 | 40.9 | 61.5 |
| ballBodyDistance | P1 | % shoulder width | 14 | 181.6 | 4.7 | 172.9 | 191.0 |
| leadKneeFlexion | P1 | ° | 15 | 19.8 | 1.8 | 16.7 | 23.8 |
| leadKneeFlexion | P4 | ° | 15 | 36.5 | 2.3 | 32.9 | 40.5 |
| leadKneeFlexion | P7 | ° | 15 | 18.3 | 2.5 | 11.2 | 19.9 |
| leadKneeFlexion | P8 | ° | 15 | 6.2 | 2.5 | 3.1 | 11.3 |
| pelvisThrust | P4 | cm | 15 | 4.0 | 1.1 | 2.7 | 6.1 |
| pelvisThrust | P5 | cm | 15 | 4.9 | 1.2 | 3.2 | 7.1 |
| pelvisThrust | P7 | cm | 15 | 8.6 | 1.7 | 4.9 | 11.4 |
| pelvisThrust | max P1-P4 | cm | 15 | 3.9 | 1.0 | 3.0 | 6.1 |
| pelvisThrust | max P5-P7 | cm | 15 | 8.6 | 1.7 | 4.9 | 11.3 |
| spineForwardBend | P1 | ° | 15 | 33.7 | 1.2 | 31.1 | 35.7 |
| spineForwardBend | P4 | ° | 15 | 27.3 | 0.8 | 25.5 | 28.2 |
| spineForwardBend | P7 | ° | 15 | 24.4 | 1.1 | 23.3 | 27.0 |
| swingPlane | P5 | ° | 15 | 6.0 | 1.2 | 5.2 | 9.6 |
| swingPlane | P6 | ° | 15 | 6.0 | 1.2 | 5.2 | 9.6 |
| trailKneeFlexion | P1 | ° | 15 | 17.3 | 1.7 | 13.3 | 20.5 |
| trailKneeFlexion | P4 | ° | 15 | 1.4 | 1.3 | 0.4 | 4.9 |
| trailKneeFlexion | P7 | ° | 15 | 31.2 | 1.7 | 29.5 | 34.5 |

## Alignment-stick yaw probe (07-04)

```
swing_0001  stick rows 810-906  tilt from vertical +13.15 deg  resid 0.43 px  x at horizon rows 30/40/50/60% H: 338 314 290 266
swing_0002  stick rows 847-931  tilt from vertical +12.36 deg  resid 0.36 px  x at horizon rows 30/40/50/60% H: 330 307 285 262
swing_0003  no stick found
swing_0004  stick rows 795-924  tilt from vertical +1.47 deg  resid 0.34 px  x at horizon rows 30/40/50/60% H: 374 371 368 366
swing_0005  stick rows 786-931  tilt from vertical +1.49 deg  resid 0.70 px  x at horizon rows 30/40/50/60% H: 373 371 368 365
swing_0006  stick rows 785-926  tilt from vertical +1.98 deg  resid 0.63 px  x at horizon rows 30/40/50/60% H: 379 375 372 368
swing_0007  stick rows 754-926  tilt from vertical +1.69 deg  resid 0.47 px  x at horizon rows 30/40/50/60% H: 376 373 370 366
swing_0008  stick rows 801-926  tilt from vertical +1.50 deg  resid 0.54 px  x at horizon rows 30/40/50/60% H: 374 371 368 366
swing_0009  stick rows 794-925  tilt from vertical +1.42 deg  resid 0.36 px  x at horizon rows 30/40/50/60% H: 373 371 368 366
swing_0010  stick rows 799-930  tilt from vertical +1.34 deg  resid 0.40 px  x at horizon rows 30/40/50/60% H: 372 370 368 365
swing_0011  stick rows 789-925  tilt from vertical +1.26 deg  resid 0.35 px  x at horizon rows 30/40/50/60% H: 372 369 367 365
swing_0012  stick rows 798-925  tilt from vertical +1.45 deg  resid 0.38 px  x at horizon rows 30/40/50/60% H: 373 371 368 366
swing_0013  stick rows 753-927  tilt from vertical +1.48 deg  resid 0.57 px  x at horizon rows 30/40/50/60% H: 373 371 368 365
swing_0014  stick rows 801-930  tilt from vertical +1.54 deg  resid 0.36 px  x at horizon rows 30/40/50/60% H: 374 372 369 366
swing_0015  stick rows 789-923  tilt from vertical +1.32 deg  resid 0.35 px  x at horizon rows 30/40/50/60% H: 372 370 368 365

2 swing(s) with the stick > 5 deg off vertical left out of the summary: swing_0001 swing_0002

vanishing column x_vp: median 370  range 365-379 over horizon rows 30-60% of the frame, 12 swings (frame centre 256)
yaw = atan((x_vp - cx)/f), cx = frame centre (NOT recorded: an ROI offset moves it), f = D / 0.270 cm/px:
  D (m)    f (px)   yaw at x_vp lo / median / hi
  2.0        741     8.4 /   8.7 /   9.4 deg
  2.5        926     6.7 /   7.0 /   7.5 deg
  3.0       1111     5.6 /   5.8 /   6.3 deg
  3.5       1296     4.8 /   5.0 /   5.4 deg
  4.0       1481     4.2 /   4.4 /   4.7 deg
```
