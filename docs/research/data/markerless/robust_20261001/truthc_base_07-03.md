### Hand truth vs tracker and segment lock — 10 swings, 100 marked frames

Yardstick, marked club vs dense taped truth: band tier 0.3°, ray tier 1.7°; Stage-2 measured head 0.8–2.4 px median.

| group | marks | tracker θ err p50 / p90 | head err p50 / p90 (px) | measured-head frames | seg lock present | seg θ err p50 / p90 | tiers |
|---|---|---|---|---|---|---|---|
| seen | 60 | 3.6° / 15.0° | 37 / 125 | 15 (30 px p50) | 37% | 2.6° / 4.6° | {'pred': 5, 'seg': 22, 'ray': 33} |
| inferred | 40 | 15.7° / 54.0° | 100 / 271 | 1 (50 px p50) | 8% | 9.7° / 15.2° | {'seg': 3, 'wedge': 14, 'pred': 19, 'ray': 4} |
| all | 100 | 5.7° / 33.9° | 57 / 190 | 16 (33 px p50) | 25% | 2.7° / 11.2° | {'pred': 24, 'seg': 25, 'ray': 37, 'wedge': 14} |

| P | marks | tracker θ err p50 | head err p50 (px) | seg present | seg θ err p50 |
|---|---|---|---|---|---|
| P1 | 10 | 7.2° | 35 | 1/10 | 16.7° |
| P2 | 10 | 2.2° | 22 | 6/10 | 2.4° |
| P3 | 10 | 3.0° | 26 | 4/10 | 2.7° |
| P4 | 10 | 2.8° | 76 | 2/10 | 2.4° |
| P5 | 10 | 4.1° | 36 | 3/10 | 3.5° |
| P6 | 10 | 5.6° | 43 | 6/10 | 1.5° |
| P7 | 10 | 8.8° | 82 | 3/10 | 9.7° |
| P8 | 10 | 3.6° | 61 | 0/10 | nan° |
| P9 | 10 | 19.4° | 102 | 0/10 | nan° |
| P10 | 10 | 51.7° | 258 | 0/10 | nan° |

| tier (seen marks) | marks | tracker θ err p50 / p90 | head err p50 / p90 (px) |
|---|---|---|---|
| seg | 22 | 2.8° / 9.2° | 39 / 78 |
| ray | 33 | 5.4° / 14.6° | 37 / 144 |
| pred | 5 | 3.7° / 23.1° | 33 / 145 |

| head, seen marks | marks | radial err p50 (signed, + = beyond the mark) | |radial| p50 / p90 | lateral p50 / p90 | tracker len / truth len p50 |
|---|---|---|---|---|---|
| measured | 17 | +18 px | 26 / 105 | 19 / 82 | 1.15 |
| projected | 43 | +23 px | 35 / 104 | 9 / 42 | 1.22 |
| all | 60 | +23 px | 31 / 106 | 11 / 53 | 1.18 |

| P | head kind counts | radial p50 (signed) | lateral p50 |
|---|---|---|---|
| P1 | {'measured': 4, 'projected': 6} | -0 | 27 |
| P2 | {'projected': 8, 'measured': 2} | +17 | 5 |
| P3 | {'measured': 3, 'projected': 7} | +25 | 6 |
| P4 | {'projected': 7, 'measured': 3} | -68 | 10 |
| P5 | {'projected': 8, 'measured': 2} | +35 | 10 |
| P6 | {'measured': 3, 'projected': 7} | +26 | 22 |

Pose grip anchor vs Mark's grip mark: 29 px p50, 46 px p90 (the anchor is the hands' midpoint; the mark is where the shaft leaves the hands).
