### Population

38 swings, 26602 traced frames.

### 0. Yardstick — the marked club's band lock vs the segment lock, all span frames

θ is scored against the DP's direction (the tracker's own answer) for both; band is the corpus-validated 0.3° reference.

| phase | span frames | band lock | segment lock | either | band θ vs DP p50/p90 | seg θ vs DP p50/p90 | tier band | tier seg | tier ray | tier wedge | tier pred |
|---|---|---|---|---|---|---|---|---|---|---|---|
| addr | 570 | 8% | 11% | 18% | 0.4°/10.5° | 1.3°/3.4° | 5% | 10% | 43% | 0% | 42% |
| back | 4462 | 25% | 69% | 79% | 0.3°/0.5° | 1.0°/1.0° | 25% | 53% | 17% | 0% | 5% |
| top | 190 | 56% | 55% | 89% | 0.3°/2.6° | 1.0°/1.0° | 52% | 35% | 11% | 0% | 3% |
| down | 950 | 9% | 31% | 36% | 0.2°/2.2° | 0.5°/1.0° | 8% | 27% | 54% | 9% | 1% |
| impact | 1060 | 28% | 59% | 73% | 0.3°/5.3° | 1.0°/1.0° | 26% | 46% | 27% | 1% | 0% |
| thru | 1070 | 19% | 30% | 39% | 0.3°/0.5° | 1.0°/1.0° | 18% | 20% | 51% | 9% | 1% |
| finish | 563 | 75% | 55% | 91% | 0.3°/0.5° | 1.0°/1.3° | 75% | 17% | 6% | 1% | 2% |
| ALL | 8865 | 26% | 54% | 66% | 0.3°/0.5° | 1.0°/1.0° | 25% | 40% | 27% | 2% | 6% |

Band and segment scale, where both exist on a frame, are compared in A; the band's own frame-to-frame scale jitter (the reference's precision) is in A4.

### 0b. Still frames OUTSIDE the evidence span (address hold, held finish) — segment lock only, no band reference exists there

| phase | frames | segment lock | FULL | tier seg | tier pred |
|---|---|---|---|---|---|
| addr | 11220 | 6% | 5% | 6% | 94% |
| finish | 6517 | 1% | 1% | 1% | 99% |
| ALL | 17737 | 4% | 4% | 4% | 96% |

### A. Segment lock vs band lock, same frame (band = reference)

| phase | band frames | seg any | seg FULL | θ err p50 | θ err p90 | θ >15° | s err p50 | s err p90 | r0 err p50 (mm) | conflict >6° |
|---|---|---|---|---|---|---|---|---|---|---|
| addr | 47 | 15% | 9% | 7.9° | 10.2° | 0.0% | 18.8% | 25.8% | 110 | 71.4% |
| back | 1137 | 60% | 45% | 0.8° | 1.3° | 0.4% | 4.8% | 38.1% | 24 | 1.7% |
| top | 106 | 39% | 36% | 0.9° | 1.2° | 2.4% | 7.3% | 39.5% | 15 | 4.9% |
| down | 88 | 40% | 36% | 1.0° | 6.1° | 2.9% | 25.9% | 49.9% | 41 | 11.4% |
| impact | 301 | 48% | 45% | 1.0° | 4.2° | 0.0% | 10.6% | 56.2% | 40 | 6.9% |
| thru | 200 | 55% | 31% | 1.0° | 1.3° | 0.0% | 6.2% | 14.7% | 24 | 0.9% |
| finish | 421 | 51% | 31% | 1.0° | 1.4° | 0.0% | 3.9% | 11.0% | 37 | 0.0% |
| ALL | 2300 | 54% | 40% | 1.0° | 1.4° | 0.4% | 5.9% | 40.7% | 28 | 2.7% |

### A2. Landmark error vs the band geometry (same frame; grip end 265 mm, steel end 870 mm)

| phase | FULL locks | rG err p50 (px) | rG err p90 | rF err p50 (px) | rF err p90 | rF err p50 (% of steel) | TERMINUS locks | rF err p50 (px) | rF err p90 |
|---|---|---|---|---|---|---|---|---|---|
| addr | 1 | 34 | 34 | 21 | 21 | 8% | 1 | 22 | 22 |
| back | 499 | 39 | 49 | 20 | 105 | 9% | 175 | 21 | 119 |
| top | 36 | 33 | 45 | 26 | 80 | 12% | 3 | 0 | 1 |
| down | 28 | 30 | 38 | 81 | 112 | 32% | 3 | 45 | 84 |
| impact | 128 | 39 | 46 | 30 | 108 | 15% | 6 | 16 | 28 |
| thru | 61 | 25 | 34 | 23 | 34 | 13% | 48 | 14 | 25 |
| finish | 132 | 16 | 31 | 26 | 36 | 12% | 83 | 18 | 34 |
| ALL | 885 | 34 | 47 | 21 | 101 | 10% | 319 | 19 | 95 |

(rows restricted to locks within 6° of the band direction, so the landmark error is measured on the right ray)

### A2b. Terminus by distal tag: signed error vs the steel end (870 mm) and vs the hosel end (922 mm)

| distal | locks | vs 870: p50 (px) | vs 922: p50 (px) | within ±15 px of the better | 
|---|---|---|---|---|
| ferrule resolved | 13 | -95 | -114 | 8% |
| head after | 179 | +17 | +0 | 63% |
| dark end | 1012 | +18 | -1 | 54% |

### A3. Proximal landmark by onset type (FULL locks on band frames within 6°)

| phase | onset | locks | s err p50 | s err p90 | measured m_G p50 (mm from butt) | m_G p10 | m_G p90 | assumed |
|---|---|---|---|---|---|---|---|---|
| addr | grip end | 1 | 12.0% | 12.0% | 184 | 184 | 184 | 265 |
| back | grip end | 36 | 27.3% | 40.5% | 171 | 164 | 184 | 265 |
| back | hands edge | 463 | 4.2% | 16.7% | 162 | 119 | 182 | 180 |
| top | hands edge | 36 | 7.2% | 36.1% | 169 | 123 | 182 | 180 |
| down | grip end | 2 | 29.0% | 30.5% | 208 | 208 | 208 | 265 |
| down | hands edge | 26 | 31.3% | 50.5% | 176 | 153 | 191 | 180 |
| impact | grip end | 1 | 19.3% | 19.3% | 175 | 175 | 175 | 265 |
| impact | hands edge | 127 | 10.1% | 48.3% | 146 | 117 | 182 | 180 |
| thru | grip end | 7 | 4.9% | 6.2% | 290 | 279 | 301 | 265 |
| thru | hands edge | 54 | 6.8% | 14.6% | 173 | 133 | 276 | 180 |
| finish | grip end | 29 | 3.7% | 10.7% | 291 | 283 | 306 | 265 |
| finish | hands edge | 103 | 4.2% | 11.0% | 209 | 159 | 279 | 180 |
| ALL | grip end | 76 | 11.9% | 37.5% | 210 | 169 | 295 | 265 |
| ALL | hands edge | 809 | 5.6% | 40.1% | 166 | 121 | 207 | 180 |

### A4. Reference precision — band lock scale and offset, consecutive-frame relative change

| quantity | p50 | p90 | n pairs |
|---|---|---|---|
| band s, % change between adjacent band frames | 1.6% | 6.8% | 1863 |
| band r0, mm change between adjacent band frames | 10 | 42 | 1863 |

(a segment-vs-band scale error at or below the band's own adjacent-frame change is at the reference's floor)

### B. Segment lock where the band lock is ABSENT (θ vs the tracker's final θ on RAY frames)

| phase | frames | seg any | seg FULL | seg TERMINUS | RAY frames | θ err p50 | θ err p90 | θ >15° |
|---|---|---|---|---|---|---|---|---|
| addr | 11743 | 6% | 5% | 1% | 0 | nan° | nan° | nan% |
| back | 3325 | 71% | 59% | 13% | 0 | nan° | nan° | nan% |
| top | 84 | 76% | 67% | 10% | 0 | nan° | nan° | nan% |
| down | 862 | 30% | 27% | 3% | 0 | nan° | nan° | nan% |
| impact | 759 | 63% | 59% | 3% | 0 | nan° | nan° | nan% |
| thru | 870 | 25% | 15% | 10% | 0 | nan° | nan° | nan% |
| finish | 6659 | 2% | 2% | 1% | 0 | nan° | nan° | nan% |
| ALL | 24302 | 18% | 15% | 3% | 0 | nan° | nan° | nan% |

### C. Lock anatomy (all segment locks)

- locks: 5491 of 26602 frames (21%); pass 1 4487, pass 2 1004
- distal: ferrule 123, hosel 1083, dark end 4285
- landmarks n: 1: 1004, 2: 1703, 3: 1213, 4: 798, 5: 363, 6: 251, 7: 119, 8: 40
- support p50 0.85, p10 0.68
- unlocked frames by furthest stage reached: not probed 3798, no run 7248, support 849, off-frame 5, no distal landmark 392, distal edge 61, s/r0 gate 4278, length gate 4480

### E. Club length per swing (result.json `analysis.club.lengths`)

| run | ball px | band px | fused px | fused / ball | ladder rung | estimators |
|---|---|---|---|---|---|---|
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0001 | -1 | 279 | 285 | — | 0 | 2 |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0002 | 337 | 293 | 332 | 0.985 | 0 | 3 |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0003 | -1 | 286 | 287 | — | 0 | 2 |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0004 | 320 | 280 | 315 | 0.984 | 0 | 3 |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0005 | 349 | 291 | 343 | 0.984 | 0 | 3 |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0006 | 349 | 288 | 340 | 0.976 | 0 | 4 |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0007 | -1 | 288 | 295 | — | 0 | 3 |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0008 | -1 | 275 | 284 | — | 0 | 3 |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0009 | 444 | 287 | 406 | 0.915 | 0 | 4 |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0010 | -1 | 278 | 287 | — | 0 | 3 |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0001 | -1 | 290 | 292 | — | 0 | 3 |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0002 | -1 | 284 | 291 | — | 0 | 3 |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0003 | -1 | 289 | 297 | — | 0 | 3 |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0004 | -1 | 284 | 296 | — | 0 | 3 |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0005 | -1 | 280 | 276 | — | 0 | 3 |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0006 | -1 | 283 | 291 | — | 0 | 3 |
| 2026-07-10_Mark-Liversedge_Wrist_01__swing_0001 | 339 | 290 | 331 | 0.976 | 0 | 4 |
| 2026-07-10_Mark-Liversedge_Wrist_01__swing_0002 | 333 | 295 | 329 | 0.989 | 0 | 4 |
| 2026-07-10_Mark-Liversedge_Wrist_01__swing_0003 | 334 | 264 | 323 | 0.969 | 0 | 4 |
| 2026-07-10_Mark-Liversedge_Wrist_01__swing_0004 | 340 | 269 | 332 | 0.977 | 0 | 4 |
| 2026-07-10_Mark-Liversedge_Wrist_01__swing_0005 | 321 | 272 | 316 | 0.983 | 0 | 4 |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0001 | 351 | 284 | 334 | 0.954 | 0 | 4 |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0002 | 351 | 297 | 342 | 0.974 | 0 | 4 |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0003 | 341 | 299 | 332 | 0.975 | 0 | 4 |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0004 | 333 | 276 | 324 | 0.974 | 0 | 4 |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0005 | 327 | 280 | 320 | 0.979 | 0 | 4 |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0006 | 344 | 289 | 338 | 0.981 | 0 | 4 |
| 2026-08-18_Mark-Liversedge_Wrist_01__swing_0001 | 341 | 276 | 334 | 0.981 | 0 | 4 |
| 2026-08-18_Mark-Liversedge_Wrist_01__swing_0002 | 335 | 289 | 333 | 0.992 | 0 | 3 |
| 2026-08-18_Mark-Liversedge_Wrist_01__swing_0003 | 334 | 287 | 330 | 0.990 | 0 | 4 |
| 2026-08-18_Mark-Liversedge_Wrist_01__swing_0004 | 339 | 284 | 333 | 0.983 | 0 | 4 |
| 2026-08-18_Mark-Liversedge_Wrist_01__swing_0005 | 336 | 277 | 332 | 0.987 | 0 | 3 |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0001 | 337 | 283 | 331 | 0.982 | 0 | 4 |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0002 | 332 | 280 | 330 | 0.994 | 0 | 4 |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0003 | 334 | 276 | 329 | 0.987 | 0 | 4 |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0004 | 338 | 284 | 329 | 0.975 | 0 | 4 |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0005 | 344 | 284 | 331 | 0.962 | 0 | 4 |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0006 | 338 | 282 | 331 | 0.980 | 0 | 4 |

### D. Per swing

| run | frames | band | seg | both | θ err p50 (both) | θ >6° (both) | s err p50 |
|---|---|---|---|---|---|---|---|
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0001 | 745 | 56 | 185 | 29 | 0.5° | 0% | 9.7% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0002 | 745 | 52 | 195 | 25 | 1.0° | 0% | 6.9% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0003 | 745 | 50 | 138 | 34 | 1.0° | 3% | 6.5% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0004 | 745 | 42 | 135 | 33 | 1.0° | 0% | 5.0% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0005 | 745 | 60 | 130 | 29 | 1.0° | 3% | 7.9% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0006 | 745 | 54 | 128 | 30 | 1.0° | 10% | 6.4% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0007 | 745 | 52 | 159 | 33 | 1.0° | 3% | 9.2% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0008 | 745 | 68 | 195 | 44 | 1.0° | 7% | 12.2% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0009 | 745 | 52 | 126 | 26 | 1.0° | 4% | 10.7% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0010 | 745 | 49 | 132 | 29 | 1.0° | 7% | 10.2% |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0001 | 745 | 88 | 138 | 40 | 0.8° | 10% | 7.5% |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0002 | 745 | 83 | 134 | 51 | 1.0° | 2% | 3.9% |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0003 | 745 | 79 | 142 | 46 | 0.8° | 0% | 5.8% |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0004 | 745 | 84 | 141 | 52 | 1.0° | 0% | 7.2% |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0005 | 745 | 69 | 140 | 49 | 1.0° | 2% | 9.1% |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0006 | 745 | 78 | 149 | 55 | 1.0° | 2% | 7.2% |
| 2026-07-10_Mark-Liversedge_Wrist_01__swing_0001 | 745 | 55 | 120 | 26 | 1.0° | 8% | 8.4% |
| 2026-07-10_Mark-Liversedge_Wrist_01__swing_0002 | 745 | 47 | 112 | 18 | 1.0° | 6% | 8.9% |
| 2026-07-10_Mark-Liversedge_Wrist_01__swing_0003 | 745 | 54 | 138 | 40 | 0.7° | 2% | 6.4% |
| 2026-07-10_Mark-Liversedge_Wrist_01__swing_0004 | 745 | 55 | 137 | 38 | 0.7° | 3% | 4.3% |
| 2026-07-10_Mark-Liversedge_Wrist_01__swing_0005 | 745 | 51 | 126 | 30 | 1.0° | 3% | 4.4% |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0001 | 745 | 51 | 129 | 32 | 0.9° | 0% | 4.1% |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0002 | 745 | 54 | 128 | 27 | 0.7° | 0% | 7.8% |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0003 | 745 | 57 | 119 | 32 | 1.0° | 3% | 4.8% |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0004 | 745 | 71 | 119 | 40 | 0.6° | 0% | 1.4% |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0005 | 745 | 47 | 109 | 23 | 1.0° | 0% | 1.3% |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0006 | 745 | 62 | 108 | 28 | 1.0° | 0% | 4.7% |
| 2026-08-18_Mark-Liversedge_Wrist_01__swing_0001 | 597 | 61 | 183 | 28 | 1.0° | 0% | 3.7% |
| 2026-08-18_Mark-Liversedge_Wrist_01__swing_0002 | 597 | 73 | 182 | 38 | 0.5° | 0% | 7.3% |
| 2026-08-18_Mark-Liversedge_Wrist_01__swing_0003 | 517 | 63 | 110 | 25 | 1.0° | 4% | 7.6% |
| 2026-08-18_Mark-Liversedge_Wrist_01__swing_0004 | 597 | 66 | 111 | 20 | 0.8° | 5% | 10.0% |
| 2026-08-18_Mark-Liversedge_Wrist_01__swing_0005 | 597 | 57 | 150 | 20 | 0.8° | 15% | 5.1% |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0001 | 597 | 55 | 142 | 33 | 1.0° | 0% | 6.4% |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0002 | 597 | 73 | 147 | 39 | 0.7° | 0% | 3.8% |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0003 | 597 | 61 | 193 | 32 | 1.0° | 3% | 1.8% |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0004 | 597 | 62 | 167 | 15 | 0.7° | 0% | 4.1% |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0005 | 597 | 54 | 258 | 23 | 1.0° | 4% | 3.0% |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0006 | 597 | 55 | 136 | 26 | 1.0° | 4% | 3.3% |
