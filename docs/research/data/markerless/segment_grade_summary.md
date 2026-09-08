### Population

38 swings, 26602 traced frames.

### A. Segment lock vs band lock, same frame (band = reference)

| phase | band frames | seg any | seg FULL | θ err p50 | θ err p90 | θ >15° | s err p50 | s err p90 | r0 err p50 (mm) | conflict >6° |
|---|---|---|---|---|---|---|---|---|---|---|
| addr | 47 | 87% | 45% | 9.2° | 127.2° | 12.2% | 54.9% | 71.7% | 35 | 85.4% |
| back | 1137 | 66% | 36% | 1.2° | 67.7° | 17.3% | 23.9% | 65.3% | 91 | 22.4% |
| top | 106 | 35% | 28% | 0.7° | 62.7° | 16.2% | 23.0% | 66.4% | 91 | 16.2% |
| down | 88 | 30% | 6% | 5.9° | 95.9° | 23.1% | 74.4% | 75.0% | 51 | 50.0% |
| impact | 301 | 48% | 37% | 1.6° | 64.9° | 13.8% | 57.6% | 68.9% | 63 | 31.0% |
| thru | 200 | 20% | 4% | 3.3° | 138.2° | 22.0% | 19.5% | 51.0% | 62 | 29.3% |
| finish | 421 | 34% | 10% | 2.8° | 165.9° | 28.0% | 12.2% | 64.6% | 21 | 35.7% |
| ALL | 2300 | 51% | 27% | 1.7° | 71.6° | 18.2% | 27.7% | 66.4% | 81 | 27.9% |

### A2. Landmark error vs the band geometry (same frame; grip end 265 mm, steel end 870 mm)

| phase | FULL locks | rG err p50 (px) | rG err p90 | rF err p50 (px) | rF err p90 | rF err p50 (% of steel) | TERMINUS locks | rF err p50 (px) | rF err p90 |
|---|---|---|---|---|---|---|---|---|---|
| addr | 3 | 35 | 38 | 193 | 197 | 89% | 3 | 60 | 170 |
| back | 320 | 39 | 48 | 18 | 171 | 8% | 260 | 15 | 138 |
| top | 29 | 32 | 43 | 19 | 167 | 10% | 2 | 23 | 30 |
| down | 1 | 39 | 39 | 234 | 234 | 89% | 12 | 138 | 187 |
| impact | 80 | 37 | 46 | 96 | 178 | 43% | 20 | 31 | 148 |
| thru | 6 | 9 | 21 | 28 | 72 | 12% | 23 | 32 | 85 |
| finish | 33 | 7 | 26 | 19 | 36 | 10% | 59 | 29 | 107 |
| ALL | 472 | 37 | 47 | 20 | 173 | 10% | 379 | 19 | 140 |

(rows restricted to locks within 6° of the band direction, so the landmark error is measured on the right ray)

### B. Segment lock where the band lock is ABSENT (θ vs the tracker's final θ on RAY frames)

| phase | frames | seg any | seg FULL | seg TERMINUS | RAY frames | θ err p50 | θ err p90 | θ >15° |
|---|---|---|---|---|---|---|---|---|
| addr | 11743 | 3% | 1% | 2% | 122 | 3.0° | 110.0° | 18.0% |
| back | 3325 | 38% | 14% | 24% | 1102 | 3.0° | 74.5° | 20.5% |
| top | 84 | 21% | 14% | 7% | 15 | 68.0° | 86.5° | 60.0% |
| down | 862 | 24% | 3% | 21% | 185 | 18.2° | 146.9° | 51.9% |
| impact | 759 | 25% | 8% | 17% | 183 | 4.0° | 100.6° | 22.4% |
| thru | 870 | 23% | 4% | 19% | 181 | 134.0° | 166.5° | 55.2% |
| finish | 6659 | 0% | 0% | 0% | 23 | 1.5° | 124.2° | 13.0% |
| ALL | 24302 | 9% | 3% | 6% | 1811 | 4.0° | 133.0° | 27.4% |

### C. Lock anatomy (all segment locks)

- locks: 3403 of 26602 frames (13%); pass 1 1332, pass 2 2071
- distal: ferrule 1671, hosel 390, dark end 1342
- landmarks n: 1: 2071, 2: 442, 3: 502, 4: 240, 5: 104, 6: 35, 7: 5, 8: 4
- support p50 0.92, p10 0.70
- unlocked frames by furthest stage reached: not probed 17794, no run 102, support 60, no distal landmark 44, distal edge 7, no onset, no prior 254, s/r0 gate 3729, length gate 1209

### D. Per swing

| run | frames | band | seg | both | θ err p50 (both) | θ >6° (both) | s err p50 |
|---|---|---|---|---|---|---|---|
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0001 | 745 | 56 | 222 | 48 | 3.4° | 35% | 30.5% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0002 | 745 | 52 | 104 | 35 | 1.0° | 9% | 22.5% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0003 | 745 | 50 | 164 | 42 | 7.0° | 57% | 37.6% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0004 | 745 | 42 | 49 | 20 | 1.0° | 5% | 21.7% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0005 | 745 | 60 | 37 | 21 | 2.1° | 29% | 21.6% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0006 | 745 | 54 | 52 | 29 | 1.0° | 17% | 19.1% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0007 | 745 | 52 | 224 | 38 | 3.7° | 45% | 52.0% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0008 | 745 | 68 | 238 | 47 | 2.6° | 23% | 36.2% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0009 | 745 | 52 | 0 | 0 | nan° | nan% | nan% |
| 2026-07-05_Mark-Liversedge_Wrist_02__swing_0010 | 745 | 49 | 160 | 39 | 3.5° | 38% | 55.1% |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0001 | 745 | 88 | 162 | 71 | 9.8° | 63% | 58.5% |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0002 | 745 | 83 | 156 | 62 | 9.8° | 55% | 60.0% |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0003 | 745 | 79 | 156 | 57 | 3.3° | 33% | 48.5% |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0004 | 745 | 84 | 159 | 74 | 4.4° | 43% | 62.1% |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0005 | 745 | 69 | 145 | 61 | 3.5° | 44% | 60.9% |
| 2026-07-09_Mark-Liversedge_Wrist_01__swing_0006 | 745 | 78 | 158 | 71 | 5.7° | 49% | 63.4% |
| 2026-07-10_Mark-Liversedge_Wrist_01__swing_0001 | 745 | 55 | 66 | 25 | 1.0° | 24% | 17.6% |
| 2026-07-10_Mark-Liversedge_Wrist_01__swing_0002 | 745 | 47 | 81 | 29 | 1.6° | 21% | 12.7% |
| 2026-07-10_Mark-Liversedge_Wrist_01__swing_0003 | 745 | 54 | 71 | 24 | 1.2° | 4% | 14.3% |
| 2026-07-10_Mark-Liversedge_Wrist_01__swing_0004 | 745 | 55 | 71 | 24 | 2.2° | 17% | 7.5% |
| 2026-07-10_Mark-Liversedge_Wrist_01__swing_0005 | 745 | 51 | 103 | 32 | 2.3° | 6% | 10.2% |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0001 | 745 | 51 | 31 | 19 | 0.5° | 11% | 9.7% |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0002 | 745 | 54 | 4 | 4 | 1.3° | 0% | 6.5% |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0003 | 745 | 57 | 30 | 18 | 0.7° | 0% | 14.4% |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0004 | 745 | 71 | 3 | 2 | 1.8° | 0% | 11.1% |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0005 | 745 | 47 | 67 | 26 | 1.0° | 12% | 8.5% |
| 2026-07-10_Mark-Liversedge_Wrist_02__swing_0006 | 745 | 62 | 79 | 29 | 1.0° | 0% | 16.6% |
| 2026-08-18_Mark-Liversedge_Wrist_01__swing_0001 | 597 | 61 | 43 | 19 | 0.9° | 5% | 15.1% |
| 2026-08-18_Mark-Liversedge_Wrist_01__swing_0002 | 597 | 73 | 66 | 39 | 0.5° | 3% | 18.9% |
| 2026-08-18_Mark-Liversedge_Wrist_01__swing_0003 | 517 | 63 | 35 | 21 | 0.6° | 0% | 17.5% |
| 2026-08-18_Mark-Liversedge_Wrist_01__swing_0004 | 597 | 66 | 56 | 22 | 1.3° | 27% | 14.9% |
| 2026-08-18_Mark-Liversedge_Wrist_01__swing_0005 | 597 | 57 | 47 | 9 | 0.7° | 22% | 15.3% |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0001 | 597 | 55 | 52 | 15 | 1.0° | 7% | 14.2% |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0002 | 597 | 73 | 99 | 50 | 1.1° | 0% | 16.3% |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0003 | 597 | 61 | 55 | 20 | 1.0° | 10% | 15.7% |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0004 | 597 | 62 | 52 | 10 | 0.8° | 0% | 15.0% |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0005 | 597 | 54 | 49 | 11 | 1.0° | 0% | 16.6% |
| 2026-08-18_Mark-Liversedge_Wrist_02__swing_0006 | 597 | 55 | 57 | 17 | 0.7° | 6% | 13.4% |
