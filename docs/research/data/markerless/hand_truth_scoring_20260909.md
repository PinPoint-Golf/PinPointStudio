# Hand-truth scoring, 2026-09-09

`tools/shaftlab/segment_truth_score.py` against Mark's markup-lab truth.json. Seen frames =
P1–P6; inferred = P7–P10 (Mark's standing rule for the 6-iron session: through impact the
shaft is invisible and the mark is a line from the hands to the clubhead). Direction is the
tracker's published θ vs the mark's; head is the published head px vs the marked head.

| configuration | untaped 7-iron 2026-07-03 (10 swings, 60 seen marks) | unmarked 6-iron 2026-09-09 (3 swings, 18 seen marks) |
|---|---|---|
| existing approach, as shipped | 4.0° / 15.0°, head 48 / 116 px | — |
| segment lock, head placed from the terminus | — | 5.5° / 8.9°, head 73 / 114 px |
| segment lock, terminus head off (`shaft.seg.placeHead` 0, now default) | — | 5.5° / 8.9°, head 58 / 101 px |
| snap at defaults (15 px / 3°) | 3.3° / 13.9°, head 44 / 119 px | — |
| snap widened (45 px / 10°), all phases | 2.9° / 18.2°, head 43 / 146 px | 1.8° / 8.7°, head 57 / 101 px |
| snap widened, skipped on Impact/Thru (`shaft.snap.skipBlur`) | 2.9° / 17.5°, head 39 / 141 px | 2.6° / 7.9°, head 57 / 101 px |

Reference: the taped 7-iron (2026-08-18 Wrist_02, 36 seen marks) with the existing
band-pinned approach scores 4.5° / 9.7° and 34 / 87 px against the same kind of mark;
its 4 band-tier marks score 0.4° / 0.7° and 15 px. The 2026-08-18 Wrist_01 session
scores as nonsense on every tier (tens of degrees) — a truth/run time-domain mismatch,
excluded and not chased. The 2026-07-05 truth is auto-generated band truth in another
time domain and is skipped by the scorer.

Inferred frames (P7–P10) are unchanged by any of it: 13.7° on the 7-iron, 9.6° on the
6-iron with the blur-skip. The pose grip anchor sits 39 px (p50) from Mark's grip mark —
the drawn line starts off the shaft axis, which is what snap corrects.

Open: the snap tail on the 7-iron (p90 15° → 18° when widened — it sometimes re-registers
onto the wrong ridge); head position (58 px on the 6-iron vs 34–48 on the 7-irons).

## Afternoon: all seven 6-iron swings marked; head position

Projection prior on the head pass (`shaft.head.projPrior`: in-plane length × lead-arm
reach ratio as the Gaussian prior on every frame, not only still ones) — on its own the
published head does not move on the bare club (6-iron 53 → 53 px p50, 42 seen marks;
untaped 7-iron 39 → 38 px). The frames the head pass measures improve (96 → 61 px), so
the prior pulls the right way but cannot choose the clubhead's blob because the terminus
walk never offers it: on a bare club its candidates end at the last lit steel.

Combined stack (segment lock + snap widened, blur skipped + projection prior) against
Mark's marks, seen frames, p50 direction / head:

| | existing approach | new stack |
|---|---|---|
| taped 7-iron 2026-08-18 W2 (36 marks) | 4.5°, 34 px (p90 87) | 2.3°, 29 px (p90 57) |
| untaped 7-iron 2026-07-03 (60 marks) | 4.0°, 48 px | 3.0°, 38 px |
| unmarked 6-iron 2026-09-09 (42 marks) | — | 2.4°, 53 px |

Head error is radial (lateral 4–5 px): the line is right, the end is short — the
published head sits at the end of the visible steel, ~70 px before the clubhead at the
top on both bare clubs. Mark marks the head where the shaft meets it; the tracker
publishes the sole (40–60 mm further), so a correct tracker sits ~15–20 px BEYOND the mark.
Next: make the far end a candidate in the head pass's terminus walk.
