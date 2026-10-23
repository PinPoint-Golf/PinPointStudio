# Posture chart, stage 4: lines from the person mask at every position, measured

*2026-10-22. Stage 4 of `docs/design/posture_chart_design.md` §10. Built uncommitted and run on the cabin PC
through a patch on the share (Mark, 22 Oct: `tools/studio/studio-build.cmd … patch <file>`, sha256 9fbc4524…,
over 79899b2f). The gate was run on scratch copies of the three golfers' libraries first, then the live library
was re-analysed (backup: `/mnt/swingdata/scratch/posture-chart-20261022/backup-live`, 242 swing documents and
18 ledgers).*

## What was built

- **(a) The back line and the butt line at P1–P7, down the line.** `AddressMarksStage` runs its mask
  measurement at every P-frame, not only at address. The rear side is frozen from address.
  - The back line's upper point sits behind the ears at every position. At collar height the outline
    becomes the trail shoulder once the shoulders turn.
  - Two new series, sparse at P1–P7:
    - `spineBackLine`: degrees from vertical, + over the ball.
    - `hipDepthButt`: the butt line's change from address, in cm, + toward the ball. It is refused
      with no ruler.
  - The DTL ruler is now saved as `analysis.dtlRuler`.
  - Versions: `kAddressMarksStageVersion` 3, and a new `kDtlPostureStageVersion` 1.
  - Cost: about 1.5 s a swing on the Mac.
- **(b) The face-on belt line was built and reverted** (Mark's decision). Four cues were tried. On this footage
  the waist is too dark to read, and one pixel of tuning flipped a reading between +2 % and −20 % (§2.3 of the
  design). Pelvis height keeps its ⚠ at P3–P5.

## The gate: 176 down-the-line swings

Re-analysis of 243 swings with pose, ball and shaft reused: 217 succeeded. The 26 failures are Mark's
4 August session, which is launch monitor only and has no video. The table gives the change from address,
as the median [interquartile range], with readings / swings.

| Golfer | | Trunk bend, joints | Trunk bend, back line | Hip depth, joints | Hip depth, butt line | \|butt − joints\| |
|---|---|---|---|---|---|---|
| Nick (28) | P4 | −8.5° | **−2.6° [−3.2, −1.8]** 26/28 | +4.1 cm | **+0.1 cm [−1.2, +1.5]** 26/27 | 3.8 |
| | P6 | −9.5° | −7.8° | +5.6 | +4.1 | 1.5 |
| | P7 | −13.9° | −13.7° | +6.3 | +6.7 | 0.5 |
| Mark (127) | P4 | −7.3° | +2.3° [−1.5, +5.8] 89/127 | +3.0 | **−0.9 [−2.5, 0.0]** 104/108 | 3.4 |
| | P6 | −5.2° | −2.8° | +6.5 | +5.5 | 1.1 |
| | P7 | −10.8° | −9.3° | +7.5 | +8.7 | 2.2 |
| Jess (23) | P4 | +2.1° | +9.0° (fails) 22/23 | −1.8 | −7.0 7/7 | 5.2 |
| | P6 | −0.2° | +1.3° | +3.4 | +3.2 | 0.5 |
| | P7 | −2.2° | −1.4° [−18.4, +0.4] | +0.5 | +2.7 | 2.5 |

**The verdicts (Mark, 22 Oct):**

- **The butt line goes in for hip depth at P3–P5, and the ⚠ comes off.**
  - It reads Nick and Mark level at the top, as Mark's eye does; the joints read 3–4 cm "toward the ball".
  - Where the joints are trusted, it agrees with them at P6 within 1.5 cm on all three golfers.
  - At P7 it agrees for Nick (0.5 cm), but not for Mark (2.2 cm) or Jess (2.5 cm). The chart keeps reading
    P6 and P7 from the joints, which is where early extension is graded.
  - Where the butt line is refused, the cell falls back to the joints, with the ⚠.
- **The back line is held, and trunk bend keeps its ⚠ at P3–P5.**
  - At the top the line runs up to the raised arms. On Jess's top frame the upper point is her hands, not
    her neck, so her +9° is not her back.
  - On Mark the line is wide and refused on 30 % of swings.
  - It is good only on Nick, where P4 sits at −2.6°, inside the good-player band.
  - The series stays recorded for a later fix that cuts the arms out of the walk.

**Firing table.** The live library, regraded under this build's content, differs from stage 1's table on
**0 of 37,179** condition rows. No condition reads the new series; they feed only the chart.

## Reproduce

- **Per-swing readings:** `posture_backline_buttline_2026-10-22.csv` (one row per swing, reading and
  position).
- **The table:** `tools/diagnostics/posture_backline_buttline_gate.py <library root> <out.csv>` prints it
  from re-analysed swings.
