### Population

7 swings, 4147 traced frames.

### 0. Yardstick — the marked club's band lock vs the segment lock, all span frames

θ is scored against the DP's direction (the tracker's own answer) for both; band is the corpus-validated 0.3° reference.

| phase | span frames | band lock | segment lock | either | band θ vs DP p50/p90 | seg θ vs DP p50/p90 | tier band | tier seg | tier ray | tier wedge | tier pred |
|---|---|---|---|---|---|---|---|---|---|---|---|
| addr | 105 | 0% | 9% | 9% | nan°/nan° | 7.8°/8.3° | 0% | 1% | 98% | 0% | 1% |
| back | 1190 | 0% | 50% | 50% | nan°/nan° | 1.0°/1.0° | 0% | 50% | 46% | 0% | 4% |
| top | 35 | 0% | 51% | 51% | nan°/nan° | 1.0°/1.0° | 0% | 51% | 49% | 0% | 0% |
| down | 175 | 0% | 16% | 16% | nan°/nan° | 1.0°/1.0° | 0% | 16% | 65% | 16% | 3% |
| impact | 195 | 0% | 54% | 54% | nan°/nan° | 1.0°/1.0° | 0% | 54% | 46% | 0% | 1% |
| thru | 226 | 0% | 15% | 15% | nan°/nan° | 1.0°/1.0° | 0% | 15% | 63% | 14% | 8% |
| finish | 105 | 0% | 17% | 17% | nan°/nan° | 0.8°/1.0° | 0% | 17% | 21% | 2% | 60% |
| ALL | 2031 | 0% | 40% | 40% | nan°/nan° | 1.0°/1.0° | 0% | 40% | 51% | 3% | 6% |

Band and segment scale, where both exist on a frame, are compared in A; the band's own frame-to-frame scale jitter (the reference's precision) is in A4.

### 0b. Still frames OUTSIDE the evidence span (address hold, held finish) — segment lock only, no band reference exists there

| phase | frames | segment lock | FULL | tier seg | tier pred |
|---|---|---|---|---|---|
| addr | 994 | 9% | 8% | 2% | 98% |
| finish | 1122 | 0% | 0% | 0% | 100% |
| ALL | 2116 | 4% | 4% | 1% | 99% |

### A. Segment lock vs band lock, same frame (band = reference)

| phase | band frames | seg any | seg FULL | θ err p50 | θ err p90 | θ >15° | s err p50 | s err p90 | r0 err p50 (mm) | conflict >6° |
|---|---|---|---|---|---|---|---|---|---|---|

### A2. Landmark error vs the band geometry (same frame; grip end 265 mm, steel end 870 mm)

| phase | FULL locks | rG err p50 (px) | rG err p90 | rF err p50 (px) | rF err p90 | rF err p50 (% of steel) | TERMINUS locks | rF err p50 (px) | rF err p90 |
|---|---|---|---|---|---|---|---|---|---|

(rows restricted to locks within 6° of the band direction, so the landmark error is measured on the right ray)

### A2b. Terminus by distal tag: signed error vs the steel end (870 mm) and vs the hosel end (922 mm)

| distal | locks | vs 870: p50 (px) | vs 922: p50 (px) | within ±15 px of the better | 
|---|---|---|---|---|

### A3. Proximal landmark by onset type (FULL locks on band frames within 6°)

| phase | onset | locks | s err p50 | s err p90 | measured m_G p50 (mm from butt) | m_G p10 | m_G p90 | assumed |
|---|---|---|---|---|---|---|---|---|

### A4. Reference precision — band lock scale and offset, consecutive-frame relative change

| quantity | p50 | p90 | n pairs |
|---|---|---|---|
| band s, % change between adjacent band frames | nan% | nan% | 0 |
| band r0, mm change between adjacent band frames | nan | nan | 0 |

(a segment-vs-band scale error at or below the band's own adjacent-frame change is at the reference's floor)

### B. Segment lock where the band lock is ABSENT (θ vs the tracker's final θ on RAY frames)

| phase | frames | seg any | seg FULL | seg TERMINUS | RAY frames | θ err p50 | θ err p90 | θ >15° |
|---|---|---|---|---|---|---|---|---|
| addr | 1099 | 9% | 8% | 1% | 8 | 7.9° | 8.4° | 0.0% |
| back | 1190 | 50% | 41% | 10% | 0 | nan° | nan° | nan% |
| top | 35 | 51% | 40% | 11% | 0 | nan° | nan° | nan% |
| down | 175 | 16% | 15% | 1% | 0 | nan° | nan° | nan% |
| impact | 195 | 54% | 50% | 4% | 0 | nan° | nan° | nan% |
| thru | 226 | 15% | 7% | 8% | 0 | nan° | nan° | nan% |
| finish | 1227 | 2% | 1% | 1% | 0 | nan° | nan° | nan% |
| ALL | 4147 | 22% | 18% | 4% | 8 | 7.9° | 8.4° | 0.0% |

### C. Lock anatomy (all segment locks)

- locks: 901 of 4147 frames (22%); pass 1 735, pass 2 166
- distal: ferrule 375, hosel 106, dark end 420
- landmarks n: 1: 166, 2: 735
- support p50 0.85, p10 0.67
- unlocked frames by furthest stage reached: not probed 348, no run 1329, support 55, no distal landmark 151, distal edge 16, s/r0 gate 759, length gate 588

### E. Club length per swing (result.json `analysis.club.lengths`)

| run | ball px | band px | fused px | fused / ball | ladder rung | estimators |
|---|---|---|---|---|---|---|
| 2026-09-09_Mark-Liversedge_Wrist_01__swing_0001 | 310 | 192 | 307 | 0.990 | 0 | 3 |
| 2026-09-09_Mark-Liversedge_Wrist_01__swing_0002 | 366 | 192 | 358 | 0.979 | 0 | 3 |
| 2026-09-09_Mark-Liversedge_Wrist_01__swing_0003 | -1 | 165 | -1 | — | 2 | 0 |
| 2026-09-09_Mark-Liversedge_Wrist_01__swing_0004 | 351 | 219 | 352 | 1.003 | 0 | 2 |
| 2026-09-09_Mark-Liversedge_Wrist_01__swing_0005 | -1 | 189 | 205 | — | 0 | 2 |
| 2026-09-09_Mark-Liversedge_Wrist_01__swing_0006 | 373 | 330 | 371 | 0.994 | 0 | 3 |
| 2026-09-09_Mark-Liversedge_Wrist_01__swing_0007 | 342 | 227 | 331 | 0.967 | 1 | 3 |

### D. Per swing

| run | frames | band | seg | both | θ err p50 (both) | θ >6° (both) | s err p50 |
|---|---|---|---|---|---|---|---|
| 2026-09-09_Mark-Liversedge_Wrist_01__swing_0001 | 597 | 0 | 145 | 0 | nan° | nan% | nan% |
| 2026-09-09_Mark-Liversedge_Wrist_01__swing_0002 | 589 | 0 | 120 | 0 | nan° | nan% | nan% |
| 2026-09-09_Mark-Liversedge_Wrist_01__swing_0003 | 593 | 0 | 107 | 0 | nan° | nan% | nan% |
| 2026-09-09_Mark-Liversedge_Wrist_01__swing_0004 | 589 | 0 | 118 | 0 | nan° | nan% | nan% |
| 2026-09-09_Mark-Liversedge_Wrist_01__swing_0005 | 597 | 0 | 177 | 0 | nan° | nan% | nan% |
| 2026-09-09_Mark-Liversedge_Wrist_01__swing_0006 | 589 | 0 | 132 | 0 | nan° | nan% | nan% |
| 2026-09-09_Mark-Liversedge_Wrist_01__swing_0007 | 593 | 0 | 102 | 0 | nan° | nan% | nan% |
