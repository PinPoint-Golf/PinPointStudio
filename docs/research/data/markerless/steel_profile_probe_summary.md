### Population

| condition | session | club | swings | frames |
|---|---|---|---|---|
| taped-steel | 2026-07-04 | DRIVER | 15 | 3867 |
| taped-steel | 2026-07-05 | 7 IRON | 10 | 2930 |
| taped-steel | 2026-07-08 | ? | 10 | 2636 |
| taped-steel | 2026-07-08 | DRIVER | 1 | 247 |
| taped-steel | 2026-07-09 | DRIVER | 6 | 1400 |
| taped-steel | 2026-07-10 | 7 IRON | 1 | 242 |
| taped-steel | 2026-07-10 | DRIVER | 10 | 2478 |
| taped-steel | 2026-08-18 | 7 IRON | 11 | 2819 |
| untaped-nonsteel-label | 2026-07-03 | 3 HYBRID | 1 | 247 |
| untaped-nonsteel-label | 2026-07-03 | 4 HYBRID | 1 | 235 |
| untaped-nonsteel-label | 2026-07-03 | 5 WOOD | 2 | 467 |
| untaped-nonsteel-label | 2026-07-03 | DRIVER | 1 | 236 |
| untaped-steel | 2026-07-03 | 7 IRON | 2 | 631 |
| untaped-steel | 2026-07-03 | 9 IRON | 1 | 236 |
| untaped-steel | 2026-07-03 | GAP WEDGE | 2 | 632 |
| untaped-steel-720w | 2026-06-11 | GAP WEDGE | 8 | 2096 |
| untaped-steel-720w | 2026-06-11 | GW | 1 | 236 |

Condition is decided per swing from evidence: taped = ≥ 20 BAND-tier frames in the recorded track (E1 locked on the band ratios), or a session documented/inspected as taped (07-04 tape pilot, 07-08). Neither the recorded club record (pre-0.1.10011 it was the athlete's single record) nor corpus.json `conditions.club` (the July "DRIVER" sessions show an iron head and a banded shaft) is trusted. 06-11 is a 720-px-wide capture and is kept separate.

### A. Shaft run per phase — all span frames, best θ within ±6° of the tracked shaft

Detectable = run ≥ 90 px with support ≥ 0.40 (the tracker's own RAY gates). e is the E2 evidence (grey levels above local background, −12 bias, clipped at 90); on = on-ridge grey level.

| condition | phase | frames | detectable | run len p50 (px) | support p50 | e p50 | e p10 | on p50 | clipped p50 | regime dark/mid/blown |
|---|---|---|---|---|---|---|---|---|---|---|
| taped-steel | address | 5186 | 71% | 116 | 0.98 | 90 | 68 | 246 | 48% | 49/38/13 |
| taped-steel | backswing | 3484 | 85% | 177 | 1.00 | 90 | 90 | 253 | 60% | 95/5/0 |
| taped-steel | top | 959 | 86% | 226 | 1.00 | 90 | 64 | 223 | 40% | 99/1/0 |
| taped-steel | downswing | 1798 | 77% | 128 | 0.99 | 90 | 71 | 243 | 46% | 68/32/0 |
| taped-steel | delivery | 414 | 46% | 84 | 0.97 | 90 | 67 | 251 | 53% | 1/98/1 |
| taped-steel | through | 2387 | 30% | 64 | 1.00 | 77 | 24 | 156 | 13% | 56/44/0 |
| taped-steel | finish | 2391 | 38% | 75 | 1.00 | 90 | 27 | 137 | 24% | 83/15/0 |
| untaped-nonsteel-label | address | 388 | 90% | 168 | 0.98 | 90 | 89 | 227 | 33% | 49/48/3 |
| untaped-nonsteel-label | backswing | 278 | 38% | 61 | 1.00 | 90 | 57 | 224 | 35% | 63/37/0 |
| untaped-nonsteel-label | top | 75 | 20% | 46 | 1.00 | 90 | 57 | 228 | 40% | 20/80/0 |
| untaped-nonsteel-label | downswing | 129 | 26% | 54 | 1.00 | 90 | 49 | 216 | 33% | 47/53/0 |
| untaped-nonsteel-label | delivery | 33 | 61% | 97 | 0.98 | 59 | 27 | 199 | 23% | 3/97/0 |
| untaped-nonsteel-label | through | 190 | 8% | 44 | 1.00 | 38 | 14 | 117 | 0% | 19/74/1 |
| untaped-nonsteel-label | finish | 92 | 0% | 47 | 1.00 | 90 | 54 | 254 | 74% | 11/89/0 |
| untaped-steel | address | 652 | 70% | 116 | 0.98 | 90 | 90 | 252 | 54% | 51/49/0 |
| untaped-steel | backswing | 240 | 65% | 119 | 1.00 | 90 | 44 | 251 | 53% | 74/26/0 |
| untaped-steel | top | 75 | 21% | 70 | 0.97 | 88 | 41 | 112 | 13% | 100/0/0 |
| untaped-steel | downswing | 171 | 50% | 89 | 0.98 | 86 | 40 | 190 | 31% | 60/38/2 |
| untaped-steel | delivery | 53 | 40% | 80 | 0.98 | 90 | 43 | 252 | 57% | 0/100/0 |
| untaped-steel | through | 189 | 20% | 51 | 1.00 | 55 | 30 | 146 | 0% | 23/76/0 |
| untaped-steel | finish | 119 | 5% | 53 | 1.00 | 89 | 39 | 202 | 11% | 13/87/0 |
| untaped-steel-720w | address | 680 | 85% | 114 | 0.97 | 72 | 48 | 171 | 31% | 29/65/6 |
| untaped-steel-720w | backswing | 462 | 86% | 176 | 1.00 | 90 | 71 | 251 | 50% | 94/6/0 |
| untaped-steel-720w | top | 135 | 64% | 126 | 0.99 | 90 | 48 | 197 | 28% | 70/29/1 |
| untaped-steel-720w | downswing | 261 | 62% | 106 | 0.99 | 90 | 48 | 212 | 34% | 53/46/1 |
| untaped-steel-720w | delivery | 44 | 27% | 76 | 0.96 | 90 | 36 | 254 | 60% | 14/77/9 |
| untaped-steel-720w | through | 342 | 17% | 55 | 1.00 | 85 | 41 | 228 | 35% | 30/68/1 |
| untaped-steel-720w | finish | 408 | 4% | 58 | 1.00 | 78 | 45 | 189 | 6% | 11/86/2 |

### B. Bare shaft between the bands on the taped clubs (within-frame, lighting-fair)

Frames where ≥ 2 band plateaus were found on the run; the gaps between them are bare steel under the same light as the bands.

| phase | frames with gaps | gap steel e p50 | e p10 | gap on p50 | band on p50 | steel/band on ratio | regime dark/mid/blown |
|---|---|---|---|---|---|---|---|
| address | 3601 | 63 | 22 | 104 | 252 | 0.41 | 65/34/1 |
| backswing | 2275 | 69 | 33 | 95 | 250 | 0.38 | 97/3/0 |
| top | 763 | 85 | 27 | 109 | 227 | 0.48 | 100/0/0 |
| downswing | 1174 | 60 | 22 | 97 | 234 | 0.41 | 74/26/0 |
| delivery | 184 | 34 | 7 | 120 | 248 | 0.48 | 1/99/0 |
| through | 1120 | 55 | 16 | 96 | 174 | 0.56 | 71/29/0 |
| finish | 1080 | 48 | 18 | 72 | 149 | 0.48 | 98/2/0 |

### C. Bare steel by background regime — untaped steel run vs taped-club gaps

| condition | regime | frames | e p50 | e p10 | on p50 | detectable |
|---|---|---|---|---|---|---|
| taped-steel | dark | 11323 | 90 | 65 | 230 | 70% |
| taped-steel | mid | 4562 | 90 | 35 | 233 | 50% |
| taped-steel | blown | 673 | 90 | 72 | 123 | 71% |
| untaped-nonsteel-label | dark | 489 | 90 | 54 | 235 | 57% |
| untaped-nonsteel-label | mid | 673 | 90 | 35 | 212 | 37% |
| untaped-nonsteel-label | blown | 13 | 85 | 12 | 154 | 62% |
| untaped-steel | dark | 745 | 90 | 40 | 247 | 60% |
| untaped-steel | mid | 748 | 90 | 48 | 245 | 43% |
| untaped-steel | blown | 5 | 90 | 39 | 140 | 80% |
| untaped-steel-720w | dark | 1021 | 90 | 66 | 205 | 80% |
| untaped-steel-720w | mid | 1242 | 73 | 46 | 194 | 39% |
| untaped-steel-720w | blown | 59 | 80 | 46 | 149 | 19% |
| taped GAPS | dark | 8026 | 66 | 26 | 94 | — |
| taped GAPS | mid | 2143 | 49 | 12 | 117 | — |
| taped GAPS | blown | 28 | 90 | 58 | 168 | — |

### D. Recorded tracker tier per phase (context for selection bias)

| condition | phase | band | ray | wedge | other | pred |
|---|---|---|---|---|---|---|
| taped-steel | address | 12% | 53% | 0% | 16% | 19% |
| taped-steel | backswing | 12% | 86% | 0% | 0% | 1% |
| taped-steel | top | 31% | 68% | 0% | 0% | 1% |
| taped-steel | downswing | 12% | 87% | 1% | 0% | 0% |
| taped-steel | delivery | 2% | 80% | 16% | 0% | 2% |
| taped-steel | through | 5% | 78% | 15% | 1% | 2% |
| taped-steel | finish | 22% | 21% | 2% | 0% | 56% |
| untaped-nonsteel-label | address | 0% | 65% | 0% | 8% | 27% |
| untaped-nonsteel-label | backswing | 0% | 90% | 0% | 0% | 10% |
| untaped-nonsteel-label | top | 0% | 100% | 0% | 0% | 0% |
| untaped-nonsteel-label | downswing | 0% | 83% | 13% | 0% | 4% |
| untaped-nonsteel-label | delivery | 0% | 79% | 21% | 0% | 0% |
| untaped-nonsteel-label | through | 0% | 35% | 34% | 1% | 30% |
| untaped-nonsteel-label | finish | 0% | 2% | 2% | 0% | 96% |
| untaped-steel | address | 0% | 55% | 0% | 22% | 23% |
| untaped-steel | backswing | 0% | 100% | 0% | 0% | 0% |
| untaped-steel | top | 0% | 100% | 0% | 0% | 0% |
| untaped-steel | downswing | 0% | 84% | 0% | 13% | 2% |
| untaped-steel | delivery | 0% | 87% | 13% | 0% | 0% |
| untaped-steel | through | 0% | 52% | 40% | 0% | 8% |
| untaped-steel | finish | 0% | 8% | 3% | 0% | 90% |
| untaped-steel-720w | address | 0% | 64% | 0% | 16% | 20% |
| untaped-steel-720w | backswing | 0% | 100% | 0% | 0% | 0% |
| untaped-steel-720w | top | 0% | 93% | 2% | 0% | 5% |
| untaped-steel-720w | downswing | 0% | 87% | 1% | 7% | 5% |
| untaped-steel-720w | delivery | 0% | 73% | 5% | 23% | 0% |
| untaped-steel-720w | through | 0% | 66% | 12% | 8% | 14% |
| untaped-steel-720w | finish | 0% | 40% | 1% | 0% | 59% |
