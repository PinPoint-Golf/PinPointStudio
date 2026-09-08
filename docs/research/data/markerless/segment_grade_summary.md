### Population

38 swings, 26602 traced frames.

### 0. Yardstick — the marked club's band lock vs the segment lock, all span frames

θ is scored against the DP's direction (the tracker's own answer) for both; band is the corpus-validated 0.3° reference.

| phase | span frames | band lock | segment lock | either | band θ vs DP p50/p90 | seg θ vs DP p50/p90 | tier band | tier seg | tier ray | tier wedge | tier pred |
|---|---|---|---|---|---|---|---|---|---|---|---|
| addr | 570 | 8% | 63% | 67% | 0.4°/10.5° | 0.5°/1.0° | 5% | 61% | 15% | 0% | 20% |
| back | 4462 | 25% | 66% | 78% | 0.3°/0.5° | 1.0°/1.0° | 25% | 53% | 18% | 0% | 5% |
| top | 190 | 56% | 54% | 89% | 0.3°/2.6° | 1.0°/1.1° | 52% | 35% | 11% | 0% | 3% |
| down | 950 | 9% | 33% | 38% | 0.2°/2.2° | 0.5°/1.0° | 8% | 30% | 52% | 9% | 1% |
| impact | 1060 | 28% | 58% | 74% | 0.3°/5.3° | 1.0°/1.0° | 26% | 46% | 27% | 1% | 0% |
| thru | 1070 | 19% | 30% | 40% | 0.3°/0.5° | 1.0°/1.0° | 18% | 21% | 51% | 9% | 1% |
| finish | 563 | 75% | 50% | 92% | 0.3°/0.5° | 1.0°/1.2° | 75% | 17% | 6% | 1% | 1% |
| ALL | 8865 | 26% | 56% | 69% | 0.3°/0.5° | 1.0°/1.0° | 25% | 44% | 25% | 2% | 4% |

Band and segment scale, where both exist on a frame, are compared in A; the band's own frame-to-frame scale jitter (the reference's precision) is in A4.

### 0b. Still frames OUTSIDE the evidence span (address hold, held finish) — segment lock only, no band reference exists there

| phase | frames | segment lock | FULL | tier seg | tier pred |
|---|---|---|---|---|---|
| addr | 11220 | 30% | 22% | 30% | 70% |
| finish | 6517 | 2% | 1% | 2% | 98% |
| ALL | 17737 | 19% | 15% | 19% | 81% |

### A. Segment lock vs band lock, same frame (band = reference)

| phase | band frames | seg any | seg FULL | θ err p50 | θ err p90 | θ >15° | s err p50 | s err p90 | r0 err p50 (mm) | conflict >6° |
|---|---|---|---|---|---|---|---|---|---|---|
| addr | 47 | 51% | 51% | 7.5° | 10.5° | 0.0% | 38.1% | 48.3% | 52 | 58.3% |
| back | 1137 | 55% | 45% | 0.8° | 1.3° | 0.5% | 4.8% | 38.1% | 24 | 2.4% |
| top | 106 | 37% | 36% | 0.9° | 1.8° | 2.6% | 7.3% | 39.5% | 15 | 7.7% |
| down | 88 | 41% | 36% | 1.0° | 5.9° | 2.8% | 25.9% | 49.9% | 41 | 11.1% |
| impact | 301 | 46% | 45% | 1.0° | 4.2° | 0.0% | 10.6% | 56.2% | 40 | 6.5% |
| thru | 200 | 48% | 31% | 1.0° | 1.3° | 0.0% | 6.2% | 14.7% | 24 | 1.0% |
| finish | 421 | 44% | 31% | 1.0° | 1.4° | 0.0% | 3.9% | 11.0% | 37 | 0.0% |
| ALL | 2300 | 50% | 40% | 1.0° | 1.4° | 0.4% | 6.2% | 43.0% | 28 | 4.0% |

### A2. Landmark error vs the band geometry (same frame; grip end 265 mm, steel end 870 mm)

| phase | FULL locks | rG err p50 (px) | rG err p90 | rF err p50 (px) | rF err p90 | rF err p50 (% of steel) | TERMINUS locks | rF err p50 (px) | rF err p90 |
|---|---|---|---|---|---|---|---|---|---|
| addr | 10 | 37 | 64 | 112 | 136 | 48% | 0 | nan | nan |
| back | 499 | 39 | 49 | 20 | 105 | 9% | 111 | 100 | 123 |
| top | 36 | 33 | 45 | 26 | 80 | 12% | 0 | nan | nan |
| down | 28 | 30 | 38 | 81 | 112 | 32% | 4 | 88 | 93 |
| impact | 128 | 39 | 46 | 30 | 108 | 15% | 1 | 20 | 20 |
| thru | 61 | 25 | 34 | 23 | 34 | 13% | 35 | 10 | 23 |
| finish | 132 | 16 | 31 | 26 | 36 | 12% | 53 | 18 | 36 |
| ALL | 894 | 35 | 47 | 21 | 105 | 10% | 204 | 29 | 118 |

(rows restricted to locks within 6° of the band direction, so the landmark error is measured on the right ray)

### A2b. Terminus by distal tag: signed error vs the steel end (870 mm) and vs the hosel end (922 mm)

| distal | locks | vs 870: p50 (px) | vs 922: p50 (px) | within ±15 px of the better | 
|---|---|---|---|---|
| ferrule resolved | 17 | -96 | -115 | 12% |
| head after | 173 | +16 | -1 | 58% |
| dark end | 908 | +16 | -2 | 51% |

### A3. Proximal landmark by onset type (FULL locks on band frames within 6°)

| phase | onset | locks | s err p50 | s err p90 | measured m_G p50 (mm from butt) | m_G p10 | m_G p90 | assumed |
|---|---|---|---|---|---|---|---|---|
| addr | grip end | 7 | 37.0% | 47.5% | 161 | 158 | 175 | 265 |
| addr | hands edge | 3 | 29.0% | 51.7% | 90 | 71 | 235 | 180 |
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
| ALL | grip end | 82 | 14.5% | 39.9% | 187 | 166 | 295 | 265 |
| ALL | hands edge | 812 | 5.6% | 40.2% | 165 | 121 | 208 | 180 |

### A4. Reference precision — band lock scale and offset, consecutive-frame relative change

| quantity | p50 | p90 | n pairs |
|---|---|---|---|
| band s, % change between adjacent band frames | 1.6% | 6.8% | 1863 |
| band r0, mm change between adjacent band frames | 10 | 42 | 1863 |

(a segment-vs-band scale error at or below the band's own adjacent-frame change is at the reference's floor)

### B. Segment lock where the band lock is ABSENT (θ vs the tracker's final θ on RAY frames)

| phase | frames | seg any | seg FULL | seg TERMINUS | RAY frames | θ err p50 | θ err p90 | θ >15° |
|---|---|---|---|---|---|---|---|---|
| addr | 11743 | 31% | 24% | 7% | 0 | nan° | nan° | nan% |
| back | 3325 | 70% | 59% | 12% | 0 | nan° | nan° | nan% |
| top | 84 | 75% | 67% | 8% | 0 | nan° | nan° | nan% |
| down | 862 | 32% | 27% | 5% | 0 | nan° | nan° | nan% |
| impact | 759 | 63% | 59% | 4% | 0 | nan° | nan° | nan% |
| thru | 870 | 26% | 15% | 11% | 0 | nan° | nan° | nan% |
| finish | 6659 | 3% | 2% | 1% | 0 | nan° | nan° | nan% |
| ALL | 24302 | 30% | 24% | 6% | 0 | nan° | nan° | nan% |

### C. Lock anatomy (all segment locks)

- locks: 8403 of 26602 frames (32%); pass 1 6676, pass 2 1727
- distal: ferrule 725, hosel 2175, dark end 5503
- landmarks n: 1: 1727, 2: 2594, 3: 2175, 4: 1110, 5: 386, 6: 252, 7: 119, 8: 40
- support p50 0.85, p10 0.66
- unlocked frames by furthest stage reached: no run 8989, support 1514, off-frame 6, no distal landmark 588, distal edge 35, s/r0 gate 5299, length gate 1768

### D. Per swing

| run | frames | band | seg | both | θ err p50 (both) | θ >6° (both) | s err p50 |
|---|---|---|---|---|---|---|---|
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0001 | 745 | 56 | 273 | 31 | 0.5° | 0% | 9.7% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0002 | 745 | 52 | 279 | 26 | 1.0° | 0% | 6.9% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0003 | 745 | 50 | 254 | 36 | 1.0° | 3% | 6.5% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0004 | 745 | 42 | 191 | 31 | 1.0° | 0% | 5.0% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0005 | 745 | 60 | 218 | 23 | 1.1° | 4% | 7.9% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0006 | 745 | 54 | 186 | 26 | 0.9° | 12% | 6.4% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0007 | 745 | 52 | 281 | 32 | 1.0° | 3% | 9.2% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0008 | 745 | 68 | 348 | 43 | 1.0° | 7% | 12.2% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0009 | 745 | 52 | 227 | 26 | 1.0° | 4% | 10.7% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0010 | 745 | 49 | 211 | 29 | 1.0° | 7% | 10.7% |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0001 | 745 | 88 | 168 | 41 | 0.8° | 10% | 7.5% |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0002 | 745 | 83 | 243 | 41 | 1.0° | 5% | 4.0% |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0003 | 745 | 79 | 281 | 37 | 0.8° | 0% | 5.8% |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0004 | 745 | 84 | 266 | 51 | 1.0° | 0% | 8.5% |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0005 | 745 | 69 | 281 | 36 | 1.0° | 0% | 9.6% |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0006 | 745 | 78 | 291 | 57 | 1.0° | 2% | 7.3% |
| 2026-07-10_Mark-Liversedge_Wrist_01__swing_0001 | 745 | 55 | 151 | 23 | 1.0° | 22% | 15.8% |
| 2026-07-10_Mark-Liversedge_Wrist_01__swing_0002 | 745 | 47 | 165 | 16 | 1.0° | 19% | 12.1% |
| 2026-07-10_Mark-Liversedge_Wrist_01__swing_0003 | 745 | 54 | 191 | 36 | 0.7° | 3% | 6.4% |
| 2026-07-10_Mark-Liversedge_Wrist_01__swing_0004 | 745 | 55 | 197 | 37 | 0.7° | 3% | 4.3% |
| 2026-07-10_Mark-Liversedge_Wrist_01__swing_0005 | 745 | 51 | 138 | 30 | 1.0° | 3% | 4.4% |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0001 | 745 | 51 | 137 | 32 | 0.9° | 0% | 4.1% |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0002 | 745 | 54 | 139 | 27 | 0.7° | 0% | 7.8% |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0003 | 745 | 57 | 130 | 32 | 1.0° | 3% | 4.8% |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0004 | 745 | 71 | 167 | 29 | 0.8° | 0% | 1.4% |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0005 | 745 | 47 | 176 | 22 | 1.0° | 0% | 1.3% |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0006 | 745 | 62 | 209 | 29 | 1.1° | 14% | 4.7% |
| 2026-08-18_Mark-Liversedge_Wrist_01__swing_0001 | 597 | 61 | 320 | 28 | 0.9° | 0% | 3.7% |
| 2026-08-18_Mark-Liversedge_Wrist_01__swing_0002 | 597 | 73 | 267 | 25 | 0.9° | 0% | 7.3% |
| 2026-08-18_Mark-Liversedge_Wrist_01__swing_0003 | 517 | 63 | 168 | 23 | 1.0° | 4% | 7.6% |
| 2026-08-18_Mark-Liversedge_Wrist_01__swing_0004 | 597 | 66 | 234 | 22 | 0.9° | 23% | 23.5% |
| 2026-08-18_Mark-Liversedge_Wrist_01__swing_0005 | 597 | 57 | 275 | 19 | 0.7° | 11% | 5.1% |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0001 | 597 | 55 | 246 | 29 | 1.0° | 0% | 6.4% |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0002 | 597 | 73 | 219 | 23 | 0.8° | 0% | 3.8% |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0003 | 597 | 61 | 243 | 33 | 1.0° | 3% | 1.9% |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0004 | 597 | 62 | 212 | 16 | 0.8° | 0% | 4.1% |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0005 | 597 | 54 | 141 | 22 | 0.8° | 5% | 3.0% |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0006 | 597 | 55 | 280 | 25 | 1.0° | 4% | 3.3% |
