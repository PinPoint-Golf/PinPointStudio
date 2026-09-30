### Hand truth vs tracker and segment lock — 7 swings, 64 marked frames

Yardstick, marked club vs dense taped truth: band tier 0.3°, ray tier 1.7°; Stage-2 measured head 0.8–2.4 px median.

| group | marks | tracker θ err p50 / p90 | head err p50 / p90 (px) | measured-head frames | seg lock present | seg θ err p50 / p90 | tiers |
|---|---|---|---|---|---|---|---|
| seen | 42 | 2.0° / 4.5° | 31 / 101 | 17 (32 px p50) | 45% | 3.8° / 7.7° | {'ray': 23, 'seg': 19} |
| inferred | 22 | 7.8° / 78.4° | 88 / 315 | 3 (30 px p50) | 14% | 5.4° / 13.0° | {'wedge': 7, 'recon': 1, 'pred': 2, 'ray': 9, 'seg': 3} |
| all | 64 | 3.3° / 13.3° | 38 / 155 | 20 (31 px p50) | 34% | 4.6° / 8.0° | {'ray': 32, 'seg': 22, 'wedge': 7, 'recon': 1, 'pred': 2} |

| P | marks | tracker θ err p50 | head err p50 (px) | seg present | seg θ err p50 |
|---|---|---|---|---|---|
| P1 | 7 | 4.3° | 68 | 3/7 | 7.7° |
| P2 | 7 | 3.1° | 32 | 4/7 | 3.6° |
| P3 | 7 | 1.7° | 30 | 1/7 | 6.3° |
| P4 | 7 | 0.5° | 30 | 3/7 | 7.6° |
| P5 | 7 | 2.0° | 29 | 6/7 | 4.0° |
| P6 | 7 | 1.4° | 21 | 2/7 | 1.5° |
| P7 | 7 | 7.8° | 84 | 1/7 | 14.9° |
| P8 | 7 | 13.5° | 96 | 0/7 | nan° |
| P9 | 1 | 85.3° | 327 | 0/1 | nan° |
| P10 | 7 | 6.4° | 30 | 2/7 | 5.0° |

| tier (seen marks) | marks | tracker θ err p50 / p90 | head err p50 / p90 (px) |
|---|---|---|---|
| seg | 19 | 2.0° / 4.9° | 30 / 113 |
| ray | 23 | 1.9° / 4.5° | 32 / 81 |

| head, seen marks | marks | radial err p50 (signed, + = beyond the mark) | |radial| p50 / p90 | lateral p50 / p90 | tracker len / truth len p50 |
|---|---|---|---|---|---|
| measured | 17 | +14 px | 23 / 87 | 13 / 27 | 1.20 |
| projected | 25 | +8 px | 28 / 90 | 5 / 33 | 1.23 |
| all | 42 | +12 px | 28 / 92 | 8 / 29 | 1.21 |

| P | head kind counts | radial p50 (signed) | lateral p50 |
|---|---|---|---|
| P1 | {'projected': 4, 'measured': 3} | -25 | 29 |
| P2 | {'projected': 5, 'measured': 2} | +24 | 6 |
| P3 | {'projected': 1, 'measured': 6} | +23 | 14 |
| P4 | {'measured': 3, 'projected': 4} | -11 | 5 |
| P5 | {'projected': 6, 'measured': 1} | +18 | 5 |
| P6 | {'projected': 5, 'measured': 2} | -6 | 4 |

Pose grip anchor vs Mark's grip mark: 38 px p50, 60 px p90 (the anchor is the hands' midpoint; the mark is where the shaft leaves the hands).
