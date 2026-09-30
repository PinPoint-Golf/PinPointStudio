### Hand truth vs tracker and segment lock — 7 swings, 64 marked frames

Yardstick, marked club vs dense taped truth: band tier 0.3°, ray tier 1.7°; Stage-2 measured head 0.8–2.4 px median.

| group | marks | tracker θ err p50 / p90 | head err p50 / p90 (px) | measured-head frames | seg lock present | seg θ err p50 / p90 | tiers |
|---|---|---|---|---|---|---|---|
| seen | 42 | 2.3° / 4.6° | 40 / 87 | 13 (41 px p50) | 64% | 3.7° / 6.6° | {'ray': 15, 'seg': 27} |
| inferred | 22 | 8.3° / 17.7° | 58 / 139 | 5 (139 px p50) | 18% | 1.0° / 4.6° | {'ray': 12, 'seg': 4, 'wedge': 4, 'pred': 2} |
| all | 64 | 2.7° / 12.1° | 40 / 125 | 18 (44 px p50) | 48% | 3.3° / 6.5° | {'ray': 27, 'seg': 31, 'wedge': 4, 'pred': 2} |

| P | marks | tracker θ err p50 | head err p50 (px) | seg present | seg θ err p50 |
|---|---|---|---|---|---|
| P1 | 7 | 2.3° | 74 | 3/7 | 3.7° |
| P2 | 7 | 2.7° | 34 | 6/7 | 6.0° |
| P3 | 7 | 1.7° | 25 | 5/7 | 3.3° |
| P4 | 7 | 0.8° | 47 | 5/7 | 6.3° |
| P5 | 7 | 2.4° | 31 | 6/7 | 1.7° |
| P6 | 7 | 3.1° | 56 | 2/7 | 2.3° |
| P7 | 7 | 17.6° | 125 | 0/7 | nan° |
| P8 | 7 | 9.3° | 91 | 0/7 | nan° |
| P9 | 1 | 9.3° | 24 | 0/1 | nan° |
| P10 | 7 | 2.3° | 25 | 4/7 | 1.0° |

| tier (seen marks) | marks | tracker θ err p50 / p90 | head err p50 / p90 (px) |
|---|---|---|---|
| seg | 27 | 2.2° / 4.5° | 35 / 96 |
| ray | 15 | 2.6° / 5.2° | 42 / 82 |

| head, seen marks | marks | radial err p50 (signed, + = beyond the mark) | |radial| p50 / p90 | lateral p50 / p90 | tracker len / truth len p50 |
|---|---|---|---|---|---|
| measured | 13 | -29 px | 39 / 80 | 12 / 23 | 1.04 |
| projected | 29 | -4 px | 33 / 91 | 5 / 11 | 1.14 |
| all | 42 | -6 px | 37 / 87 | 5 / 17 | 1.13 |

| P | head kind counts | radial p50 (signed) | lateral p50 |
|---|---|---|---|
| P1 | {'projected': 5, 'measured': 2} | -74 | 4 |
| P2 | {'projected': 6, 'measured': 1} | -4 | 7 |
| P3 | {'projected': 5, 'measured': 2} | +21 | 5 |
| P4 | {'projected': 5, 'measured': 2} | -46 | 6 |
| P5 | {'projected': 6, 'measured': 1} | +28 | 5 |
| P6 | {'measured': 5, 'projected': 2} | -55 | 5 |

Pose grip anchor vs Mark's grip mark: 36 px p50, 50 px p90 (the anchor is the hands' midpoint; the mark is where the shaft leaves the hands).
