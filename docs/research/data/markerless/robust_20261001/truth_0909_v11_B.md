### Hand truth vs tracker and segment lock — 7 swings, 64 marked frames

Yardstick, marked club vs dense taped truth: band tier 0.3°, ray tier 1.7°; Stage-2 measured head 0.8–2.4 px median.

| group | marks | tracker θ err p50 / p90 | head err p50 / p90 (px) | measured-head frames | seg lock present | seg θ err p50 / p90 | tiers |
|---|---|---|---|---|---|---|---|
| seen | 42 | 2.1° / 4.9° | 32 / 92 | 21 (33 px p50) | 45% | 3.8° / 8.2° | {'ray': 23, 'seg': 19} |
| inferred | 22 | 10.6° / 79.5° | 78 / 317 | 5 (44 px p50) | 9% | 10.4° / 14.4° | {'wedge': 6, 'recon': 1, 'pred': 3, 'ray': 10, 'seg': 2} |
| all | 64 | 2.9° / 12.4° | 39 / 133 | 26 (34 px p50) | 33% | 3.9° / 9.0° | {'ray': 33, 'seg': 21, 'wedge': 6, 'recon': 1, 'pred': 3} |

| P | marks | tracker θ err p50 | head err p50 (px) | seg present | seg θ err p50 |
|---|---|---|---|---|---|
| P1 | 7 | 9.3° | 63 | 3/7 | 9.0° |
| P2 | 7 | 2.9° | 31 | 4/7 | 2.4° |
| P3 | 7 | 1.7° | 31 | 1/7 | 6.3° |
| P4 | 7 | 0.3° | 39 | 3/7 | 8.0° |
| P5 | 7 | 2.0° | 28 | 5/7 | 3.9° |
| P6 | 7 | 2.5° | 80 | 3/7 | 1.9° |
| P7 | 7 | 11.3° | 84 | 1/7 | 15.4° |
| P8 | 7 | 10.4° | 112 | 0/7 | nan° |
| P9 | 1 | 86.3° | 334 | 0/1 | nan° |
| P10 | 7 | 6.4° | 33 | 1/7 | 5.4° |

| tier (seen marks) | marks | tracker θ err p50 / p90 | head err p50 / p90 (px) |
|---|---|---|---|
| seg | 19 | 2.0° / 5.1° | 31 / 71 |
| ray | 23 | 2.2° / 4.9° | 33 / 110 |

| head, seen marks | marks | radial err p50 (signed, + = beyond the mark) | |radial| p50 / p90 | lateral p50 / p90 | tracker len / truth len p50 |
|---|---|---|---|---|---|
| measured | 21 | +15 px | 27 / 114 | 15 / 33 | 1.20 |
| projected | 21 | +12 px | 30 / 80 | 5 / 12 | 1.23 |
| all | 42 | +13 px | 29 / 92 | 8 / 33 | 1.22 |

| P | head kind counts | radial p50 (signed) | lateral p50 |
|---|---|---|---|
| P1 | {'measured': 4, 'projected': 3} | -35 | 46 |
| P2 | {'measured': 2, 'projected': 5} | +27 | 5 |
| P3 | {'projected': 1, 'measured': 6} | +26 | 15 |
| P4 | {'measured': 4, 'projected': 3} | -32 | 26 |
| P5 | {'projected': 5, 'measured': 2} | +22 | 5 |
| P6 | {'projected': 4, 'measured': 3} | -80 | 3 |

Pose grip anchor vs Mark's grip mark: 39 px p50, 60 px p90 (the anchor is the hands' midpoint; the mark is where the shaft leaves the hands).
