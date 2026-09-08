### Population

38 swings, 26602 traced frames.

### A. Segment lock vs band lock, same frame (band = reference)

| phase | band frames | seg any | seg FULL | θ err p50 | θ err p90 | θ >15° | s err p50 | s err p90 | r0 err p50 (mm) | conflict >6° |
|---|---|---|---|---|---|---|---|---|---|---|
| addr | 47 | 51% | 51% | 7.5° | 10.5° | 0.0% | 38.1% | 48.3% | 52 | 58.3% |
| back | 1137 | 56% | 45% | 0.8° | 1.3° | 0.5% | 4.8% | 38.1% | 24 | 1.9% |
| top | 106 | 36% | 36% | 0.9° | 1.2° | 2.6% | 7.3% | 39.5% | 15 | 5.3% |
| down | 88 | 41% | 36% | 1.0° | 5.9° | 2.8% | 25.9% | 49.9% | 41 | 11.1% |
| impact | 301 | 48% | 45% | 1.0° | 4.2° | 0.0% | 10.6% | 56.2% | 40 | 7.0% |
| thru | 200 | 55% | 31% | 1.0° | 1.3° | 0.0% | 6.2% | 14.7% | 24 | 0.9% |
| finish | 421 | 51% | 31% | 1.0° | 1.4° | 0.0% | 3.9% | 11.0% | 37 | 0.0% |
| ALL | 2300 | 52% | 40% | 1.0° | 1.4° | 0.4% | 6.2% | 43.0% | 28 | 3.6% |

### A2. Landmark error vs the band geometry (same frame; grip end 265 mm, steel end 870 mm)

| phase | FULL locks | rG err p50 (px) | rG err p90 | rF err p50 (px) | rF err p90 | rF err p50 (% of steel) | TERMINUS locks | rF err p50 (px) | rF err p90 |
|---|---|---|---|---|---|---|---|---|---|
| addr | 10 | 37 | 64 | 112 | 136 | 48% | 0 | nan | nan |
| back | 499 | 39 | 49 | 20 | 105 | 9% | 128 | 22 | 122 |
| top | 36 | 33 | 45 | 26 | 80 | 12% | 0 | nan | nan |
| down | 28 | 30 | 38 | 81 | 112 | 32% | 4 | 65 | 92 |
| impact | 128 | 39 | 46 | 30 | 108 | 15% | 5 | 11 | 21 |
| thru | 61 | 25 | 34 | 23 | 34 | 13% | 48 | 14 | 25 |
| finish | 132 | 16 | 31 | 26 | 36 | 12% | 81 | 16 | 33 |
| ALL | 894 | 35 | 47 | 21 | 105 | 10% | 266 | 19 | 114 |

(rows restricted to locks within 6° of the band direction, so the landmark error is measured on the right ray)

### A2b. Terminus by distal tag: signed error vs the steel end (870 mm) and vs the hosel end (922 mm)

| distal | locks | vs 870: p50 (px) | vs 922: p50 (px) | within ±15 px of the better | 
|---|---|---|---|---|
| ferrule resolved | 16 | -101 | -119 | 6% |
| head after | 180 | +16 | -1 | 61% |
| dark end | 964 | +16 | -1 | 53% |

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

### B. Segment lock where the band lock is ABSENT (θ vs the tracker's final θ on RAY frames)

| phase | frames | seg any | seg FULL | seg TERMINUS | RAY frames | θ err p50 | θ err p90 | θ >15° |
|---|---|---|---|---|---|---|---|---|
| addr | 11743 | 3% | 2% | 0% | 139 | 0.5° | 1.0° | 0.0% |
| back | 3325 | 71% | 59% | 13% | 2269 | 1.0° | 1.0° | 0.0% |
| top | 84 | 74% | 67% | 7% | 62 | 1.0° | 1.0° | 0.0% |
| down | 862 | 32% | 27% | 5% | 260 | 0.5° | 1.0° | 0.0% |
| impact | 759 | 63% | 59% | 3% | 476 | 1.0° | 1.0° | 0.0% |
| thru | 870 | 26% | 15% | 11% | 214 | 1.0° | 1.0° | 0.0% |
| finish | 6659 | 1% | 1% | 0% | 86 | 1.0° | 1.0° | 0.0% |
| ALL | 24302 | 16% | 13% | 3% | 3506 | 1.0° | 1.0° | 0.0% |

### C. Lock anatomy (all segment locks)

- locks: 5044 of 26602 frames (19%); pass 1 4095, pass 2 949
- distal: ferrule 182, hosel 1177, dark end 3685
- landmarks n: 1: 949, 2: 1497, 3: 1173, 4: 677, 5: 340, 6: 251, 7: 118, 8: 39
- support p50 0.88, p10 0.68
- unlocked frames by furthest stage reached: not probed 17737, no run 456, support 393, off-frame 5, no distal landmark 142, distal edge 13, s/r0 gate 2312, length gate 500

### D. Per swing

| run | frames | band | seg | both | θ err p50 (both) | θ >6° (both) | s err p50 |
|---|---|---|---|---|---|---|---|
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0001 | 745 | 56 | 196 | 30 | 0.5° | 0% | 9.7% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0002 | 745 | 52 | 204 | 25 | 1.0° | 0% | 6.9% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0003 | 745 | 50 | 141 | 32 | 1.0° | 3% | 6.5% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0004 | 745 | 42 | 141 | 33 | 1.0° | 0% | 5.0% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0005 | 745 | 60 | 132 | 25 | 1.1° | 4% | 7.9% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0006 | 745 | 54 | 119 | 28 | 1.0° | 11% | 6.4% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0007 | 745 | 52 | 171 | 32 | 1.0° | 3% | 9.2% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0008 | 745 | 68 | 181 | 44 | 1.0° | 7% | 12.2% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0009 | 745 | 52 | 139 | 26 | 1.0° | 4% | 10.7% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0010 | 745 | 49 | 144 | 32 | 1.0° | 6% | 10.7% |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0001 | 745 | 88 | 136 | 40 | 0.8° | 10% | 7.5% |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0002 | 745 | 83 | 142 | 50 | 1.0° | 4% | 4.0% |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0003 | 745 | 79 | 152 | 44 | 0.9° | 0% | 5.8% |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0004 | 745 | 84 | 150 | 54 | 1.0° | 0% | 8.5% |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0005 | 745 | 69 | 151 | 48 | 1.0° | 2% | 9.6% |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0006 | 745 | 78 | 159 | 58 | 1.0° | 2% | 7.3% |
| 2026-07-10_Mark-Liversedge_Wrist_01__swing_0001 | 745 | 55 | 122 | 29 | 1.0° | 17% | 15.8% |
| 2026-07-10_Mark-Liversedge_Wrist_01__swing_0002 | 745 | 47 | 113 | 19 | 1.0° | 16% | 12.1% |
| 2026-07-10_Mark-Liversedge_Wrist_01__swing_0003 | 745 | 54 | 142 | 40 | 0.7° | 2% | 6.4% |
| 2026-07-10_Mark-Liversedge_Wrist_01__swing_0004 | 745 | 55 | 140 | 39 | 0.7° | 3% | 4.3% |
| 2026-07-10_Mark-Liversedge_Wrist_01__swing_0005 | 745 | 51 | 123 | 28 | 1.0° | 4% | 4.4% |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0001 | 745 | 51 | 127 | 32 | 0.9° | 0% | 4.1% |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0002 | 745 | 54 | 128 | 27 | 0.7° | 0% | 7.8% |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0003 | 745 | 57 | 116 | 32 | 1.0° | 3% | 4.8% |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0004 | 745 | 71 | 119 | 40 | 0.6° | 0% | 1.4% |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0005 | 745 | 47 | 107 | 23 | 1.0° | 0% | 1.3% |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0006 | 745 | 62 | 117 | 28 | 1.0° | 0% | 4.7% |
| 2026-08-18_Mark-Liversedge_Wrist_01__swing_0001 | 597 | 61 | 121 | 27 | 0.9° | 0% | 3.7% |
| 2026-08-18_Mark-Liversedge_Wrist_01__swing_0002 | 597 | 73 | 110 | 28 | 0.7° | 0% | 7.3% |
| 2026-08-18_Mark-Liversedge_Wrist_01__swing_0003 | 517 | 63 | 101 | 23 | 1.0° | 4% | 7.6% |
| 2026-08-18_Mark-Liversedge_Wrist_01__swing_0004 | 597 | 66 | 105 | 22 | 0.9° | 23% | 23.5% |
| 2026-08-18_Mark-Liversedge_Wrist_01__swing_0005 | 597 | 57 | 96 | 18 | 0.7° | 11% | 5.1% |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0001 | 597 | 55 | 117 | 29 | 1.0° | 0% | 6.4% |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0002 | 597 | 73 | 105 | 23 | 0.8° | 0% | 3.8% |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0003 | 597 | 61 | 124 | 31 | 1.0° | 3% | 1.9% |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0004 | 597 | 62 | 112 | 15 | 0.7° | 0% | 4.1% |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0005 | 597 | 54 | 111 | 23 | 1.0° | 4% | 3.0% |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0006 | 597 | 55 | 130 | 26 | 1.0° | 4% | 3.3% |
