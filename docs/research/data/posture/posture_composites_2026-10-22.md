# Posture composites — the measurement (V1–V5), 22 October 2026

*The measurement stage of `docs/design/posture_metrics_design.md` §9. Every number here comes from the
run root and commands below, and can be reproduced from the CSVs committed beside this file.*

## Summary

| Gate | Result | What it changed |
|---|---|---|
| **V1** face-on trunk rise against down-the-line bend | **Failed**: Spearman 0.12 / 0.15 / 0.23 (bar 0.7) | `m_trunkRiseImpact` held; a face-on-only shot cannot assess the posture held into impact |
| **V2** ρ per composite | Measured: 0.17 impact, 0.11 top, 0 setup | Written to the composites' norm rows (`source: measured`) |
| **V3** norms for the parts | Measured; three golfers of unknown standard cannot seat a good-swing norm | Part norms kept as authored, each citing what the three golfers read; axis tilt at address re-seated for the 2-D producer |
| **V4** firing table | Written; agrees with Mark's coach labels at impact and at the top | `loss_of_posture` and both knee-flex conditions to active, as the design said; everything else stays draft |
| **V5** repeatability | Head pitch passes (0 flips, p90 2.5°); impact composite 1 flip in 15 (bar 1 in 20), borderline | `m_headPitchAddress` live; the unstable part is named (pelvis thrust), not held — Mark's call |

Two content changes came out of the measurement rather than the gates:

- **The hip-joint pelvis lift leaves the top composite.** At the top it reads −20.7 % (Mark), −17.5 %
  (Jess) and −5.3 % (Nick) of stance width on essentially every swing. That is the keypoint migration
  the measure's own row already describes (its low tail is unwatched for that reason). As a part it
  pushed every Mark swing toward "sinking". The part now reads only the belt-line measure
  `m_pelvisSinkTop`, which has no producer yet, so the top composite reads from four parts (2.8 of
  3.4 weight).
- **A composite no longer reads a part whose measure is not live.** Found here: with trunk rise held, a
  face-on-only swing still read the trunk part through it. Fixed in the engine (the part is unread,
  reason `notLive`).

## Populations and the run

Re-analysed **copies** of the three athlete libraries and of the corpus (the live library was not
touched). Copies are on the reuse path: the recorded pose is reused and only the metrics are recomputed,
so the new series (`trunkRise`, `armHang`, `headPitch`) land.

| Golfer | Shots | Face-on | DTL | Both | Club |
|---|---:|---:|---:|---:|---|
| Mark | 191 | 165 | 129 | 129 | mostly 7-iron (144), gap wedge 31, driver 8, 6-iron 7 |
| Nick Henn | 28 | 28 | 28 | 28 | 7-iron |
| Jess Ratcliffe | 23 | 23 | 23 | 23 | 8-iron |

Shots marked capture-withheld are dropped. Mark's 26 shots of 2026-08-04 (launch monitor only) have no
video and never analyse.

**Run root:** `/mnt/swingdata/scratch/posture-20261021/` on the share (= `C:\PinPointStudio\scratch\posture-20261021\`
on the cabin PC). Commits: producers `d78e82d3`, engine + content `61540d0b`, `measure_dump` `89cd11a1`,
the measured content and the held-part fix `cfbe0994`.

**Commands** (cabin PC, Release `swinglab_run`, Debug `measure_dump`/`regrade_ledger` from
`tools/studio/studio-diag.cmd`):

1. `sweep.ps1` — `robocopy` each of `Mark-Liversedge`, `Nick-Henn`, `Jess-Ratcliffe`, `corpus\swings` into
   `copies\`, then `swinglab_run <swing dir> --write-back --session-type 1` on every copied swing
   (356 ok, 52 exit 1 = the two copies of 2026-08-04).
2. `dump.ps1 dump3 content3` — `measure_dump --out dump3\<golfer>.csv <every session dir>` with
   `PINPOINT_CORE_*` pointing at `content3\` (this commit's `core.json`/`norms.json` before the V4 state
   changes; the state changes do not alter readings).
3. `regrade.ps1 v4b content3` — `regrade_ledger --tag v4b <every session dir of the copies>`.
4. On the Mac: `tools/diagnostics/posture_composites_analysis.py docs/research/data/posture` prints every
   table below from the committed extracts `posture_dump_<golfer>_2026-10-22.csv` (the posture measures'
   rows of the dumps) and `posture_firing_2026-10-22.csv` (the regraded ledgers' states for the new and
   member conditions).

## V1 — face-on trunk rise against the down-the-line bend

`m_trunkRiseImpact` (trunk rise at P7, %) against −`m_spineBendChangeImpact` (bend lost P1→P7, °) on
every two-camera swing.

| Golfer | n | Spearman | Median rise | Median bend lost | SD rise | SD bend lost |
|---|---:|---:|---:|---:|---:|---:|
| Mark | 126 | **+0.12** | +8.3 % | 10.3° | 3.1 % | 1.6° |
| Nick | 27 | **+0.15** | +6.1 % | 13.6° | 2.0 % | 1.0° |
| Jess | 23 | **+0.23** | +2.1 % | 2.2° | 3.4 % | 1.5° |
| pooled | 176 | +0.21 | | | | |

**Fails on every golfer.** The geometry is not the problem: at the medians Mark reads 0.8 % per degree and
Jess 1.0 %, near the 1.2 % the cosine predicts. The problem is noise. Within one golfer the bend changes
by an SD of 1–1.6°, which should move trunk rise by about 1.5–2 %, and the face-on reading scatters by
2–3.4 % on its own. Across golfers it does not even order them (Nick loses the most bend but reads less
rise than Mark). Following §9: `trunkRise` ships **held**, the reason is in its `gapReason`, and a
face-on-only shot reads the impact composite from 1.0 of 2.7 weight, so it is unavailable. The verdict
comparison (face-on-only against both cameras) is therefore moot.

## V2 — ρ

The mean pairwise Pearson r of the parts' z-scores (coherent composites: loss-signed sᵢzᵢ), with each
golfer centred on their own mean so only swing-to-swing co-movement counts; floored at 0. Held parts and
parts with no producer are left out, as the engine leaves them out.

| Composite | Mark | Nick | Jess | **Within golfer (stored)** | Between golfers too |
|---|---:|---:|---:|---:|---:|
| `m_postureHeldImpact` | +0.13 | +0.26 | +0.59 | **0.17** | +0.28 |
| `m_postureHeldTop` | +0.03 | +0.38 | +0.22 | **0.11** | +0.55 |
| `m_setupPosture` | −0.03 | −0.11 | −0.07 | **0 (−0.02)** | −0.06 |

The between-golfer figure is higher for both posture-change composites: the golfers who stand up do it
with every part (Mark, Nick) and the one who doesn't, doesn't (Jess). Three golfer means cannot estimate
that, so it is reported and not used. If more golfers confirm it, ρ should rise, which makes the family
slightly harder to trip. Pairs worth knowing: trunk hinge against arm hang −0.61 at address (geometric:
the more bent the trunk, the further the arms' line leans from vertical), and trunk against pelvis height
−0.29 at impact (backwards for real standing up; another sign that hip-joint height is not clean).

## V3 — the parts against their norms

Per golfer: n, mean ± SD (z of the mean against the shipped norm).

| Measure | Norm | Mark | Nick | Jess |
|---|---|---|---|---|
| `m_spineBendChangeImpact` (°) | 0, σ 2.5 loss / 5 gain | −10.1 ± 1.6 (−4.0) | −13.2 ± 2.0 (−5.3) | −2.4 ± 1.5 (−0.9) |
| `m_pelvisThrustImpact` (cm) | 0 ± 2.5 | +7.5 ± 1.8 (+3.0) | +6.7 ± 1.3 (+2.7) | +1.2 ± 1.8 (+0.5), n 7 |
| `m_headLiftImpact` (cm) | 0 ± 3 | +2.0 ± 3.3 (+0.7) | −0.6 ± 3.1 (−0.2) | −5.6 ± 2.5 (−1.9) |
| `m_pelvisLiftImpact` (% stance) | 0 ± 6.2 | −3.7 ± 4.2 (−0.6) | +5.2 ± 1.9 (+0.8) | −3.0 ± 4.5 (−0.5) |
| `m_spineBendChangeTop` (°) | 0 ± 2.5 | −7.3 ± 1.8 (−2.9) | −8.2 ± 2.2 (−3.3) | +2.0 ± 1.7 (+0.8) |
| `m_trailKneeFlex` P1→P4 (°) | 0 ± 6 | −18.2 ± 2.8 | −19.7 ± 4.3 | +0.8 ± 1.8 |
| `m_headLiftBack` (cm) | 0 ± 3 | +0.7 ± 2.9 | −0.8 ± 2.9 | −9.2 ± 1.8 |
| `m_pelvisLiftTop` (% stance) | 0 ± 6.2 | −20.7 ± 3.8 | −5.3 ± 1.4 | −17.5 ± 4.9 |
| `m_spineBendAtAddress` (°) | 40 ± 8 | 36.5 ± 2.1 | 38.3 ± 1.4 | 29.4 ± 1.4 |
| `m_kneeFlexAddress` trail (°) | 22 ± 7 | 20.1 ± 2.3 (−0.3) | 28.4 ± 4.2 (+0.9) | 13.9 ± 1.0 (−1.2) |
| `m_balanceHeelToeAddress` (% foot) | 53 ± 6 | 56.8 ± 7.6 (+0.6) | 61.9 ± 1.9 (+1.5) | 77.7 ± 7.2 (**+4.1**) |
| `m_armHangAddress` (°) | 0 ± 5 | +4.9 ± 1.8 (+1.0) | −1.7 ± 1.0 (−0.3) | +3.8 ± 1.3 (+0.8) |
| `m_axisTiltAddress` (°) | **−2 ± 3** (was 6 ± 3) | −1.7 ± 1.6 | −1.7 ± 1.1 | −6.1 ± 2.0 |
| `m_headPitchAddress` (°) | 26 ± 8 | 22.9 ± 6.4 | 24.8 ± 3.4 | 32.9 ± 5.0 |

**Why the part norms were not re-seated from these numbers.** A norm describes a good swing. Two of the
three golfers stand up through impact (that is what the composite is for), so their means are faults,
not centres; and nobody here is labelled as the standard. Each part row keeps its authored numbers and
now cites this table, with how far off centre each golfer sits. Two exceptions:

- **Axis tilt at address** is re-seated to −2 ± 3°. The design's "about 6°" was the 3-D figure; the
  face-on projection reads near zero, slightly toward the target, on Mark and Nick. Jess's whole tilt
  curve sits about 8° below Mark's at every position (address −6.1°, impact −1.6° against Mark's +6.9°),
  which is either camera roll or her body; nothing here can separate them. Absolute face-on tilt is
  camera-sensitive.
- **Absolute address bend is rig-dependent.** Mark judges all three "marginally too bent" at address,
  yet they read 36.5°, 38.3° and 29.4°. The 40 ± 8 row fires `posture_too_bent` on none of them, which
  fits "marginally", but the 9° spread between golfers Mark sees alike says camera height and position
  move this number.

## V4 — firing, and Mark's labels

Mark's coach labels (22 Oct, golfer-level): Mark and Nick stand up going back and lose their posture into
early extension coming down; Jess has an excellent swing posture-wise apart from a dip going back; all
three are marginally too bent at address.

Fired / assessable, per golfer:

| Condition | Mark | Nick | Jess | Against the labels |
|---|---:|---:|---:|---|
| `loss_of_posture` | 123 / 127 | 27 / 28 | 1 / 23 | **agrees** |
| `posture_sinks_impact` | 0 / 127 | 0 / 28 | 0 / 23 | agrees |
| `loss_of_posture_backswing` | 127 / 127 | 27 / 28 | 0 / 23 | **agrees** |
| `posture_sinks_backswing` | 0 / 127 | 0 / 28 | 6 / 23 | agrees in direction; catches the dip on 6 of 23 |
| `poor_setup_posture` | 13 / 127 | 0 / 27 | **21 / 23** | not settled by the labels (driven by the balance reading) |
| `weight_on_toes_address` | 14 / 127 | 1 / 28 | **23 / 23** | not settled by the labels |
| `arms_reaching_at_address` | 0 / 127 | 0 / 27 | 0 / 22 | — |
| `reverse_tilt_at_address` | 0 / 163 | 0 / 27 | 0 / 23 | — |
| members: `early_extension` | 125 / 125 | 27 / 27 | 0 / 6 | agrees |
| `trail_knee_straighten` | 124 / 127 | 27 / 28 | 0 / 23 | agrees |
| `head_drop_backswing` | 7 / 165 | 3 / 28 | 22 / 23 | the dip |
| `weight_in_heels_address` | 26 / 127 | 0 / 28 | 0 / 23 | — |
| `posture_too_bent` / `_upright` | 0 | 0 | 0 | fits "marginally" |

Family without a member firing / a member firing without the family:

| Family | Mark | Nick | Jess |
|---|---|---|---|
| `loss_of_posture` | 0 / 2 | 0 / 0 | 1 / 0 |
| `loss_of_posture_backswing` | 3 / 0 | 0 / 0 | 0 / 0 |
| `posture_sinks_backswing` | 0 / 7 | 0 / 3 | 0 / 16 |
| `poor_setup_posture` | 0 / 27 | 0 / 0 | 0 / 2 |

**What it says.**

- The two posture-change families agree with Mark at impact and at the top on all three golfers. As §9
  expected, they fire on nearly every Mark and Nick swing with the single faults as drivers.
- The case the family exists for — losses spread thinly, no single member firing — barely occurs: 1
  Jess swing at impact, 3 Mark swings at the top. The family is acting as a hub. Its weights and ρ are
  still right; these golfers' faults are simply large.
- **Jess's dip** is mostly her head (−9 cm at the top) with the trunk slightly more bent (+2°) and the
  trail knee held. The coherent family needs several parts together, so it catches 6 of 23 while
  `head_drop_backswing` catches 22. With the hip-joint pelvis part gone, there is one fewer part to see
  a sink. When the belt-line producer lands, that part comes back.
- **The setup family fires on Jess through the balance reading, and the labels cannot say whether
  that is right.** Jess reads 77.7 % toward the toes (z +4.1) on every swing, against 57 % (Mark) and
  62 % (Nick). Mark's labels speak about her posture in the swing and say nothing about where her weight
  sits at address, so this may well be true. What is not known is whether the number is the golfer or
  the camera: the proxy's absolute value has never been checked against a pressure plate, and other
  absolute readings here (address bend, side tilt) do shift between golfers by camera-sized amounts.
  Nothing was changed on the strength of it: the balance norm keeps its authored numbers, and
  `weight_on_toes_address` and `poor_setup_posture` stay draft, as the design had them. **Draft does
  not keep a condition out of a golfer's report** — it is an authoring label nothing gates on — so both
  are work-ons 1 and 2 in Jess's session report today. `reportHold` is the switch that would hold them. The way to
  settle it is a coach's look at Jess's weight at address (or a pressure plate), not a rule change.

**State changes made** (design §7.2, §7.4): `loss_of_posture` → active; `excessive_knee_flex` and
`insufficient_knee_flex` → active (they fire on no one here, which is consistent with the labels). The
other seven new conditions stay draft — which, as above, still grades them and puts them in reports.

## V5 — repeatability

The corpus copy of a swing carries its own pose run, so library against corpus on the same swing is a
repeatability test — where the two runs differ. 107 swings match; **85 carry the identical pose run**
in both copies, so they test nothing. **22 are independent:
Mark only**, 15 from 2026-07-04 (two cameras) and 7 from 2026-09-09 (face-on).

| Measure | n | median \|diff\| | p90 \|diff\| | verdict flips |
|---|---:|---:|---:|---:|
| `m_postureHeldImpact` | 15 | 0.48 z | 1.20 z | **1 of 15** |
| `m_postureHeldTop` | 15 | 0.27 z | 0.92 z | 0 of 15 |
| `m_setupPosture` | 15 | 0.01 z | 0.09 z | 0 of 15 |
| `m_headPitchAddress` | 15 | 0.42° | **2.54°** | 0 of 15 |
| `m_pelvisThrustImpact` | 10 | 1.91 cm | 2.48 cm | 0 of 10 |
| control: `m_spineBendLossDown` (early extension) | 15 | 0.07° | 0.48° | 1 of 15 |
| control: `m_pelvisThrustDown` (early extension) | 10 | 1.88 cm | 2.42 cm | 1 of 10 |

- **Head pitch passes** (spread ≤ 3°, no flips) and goes live — on one golfer, 15 swings.
- **The impact composite flips once in 15**, against a bar of one in 20. The flip is 07-04 swing 3:
  pelvis thrust was read on one pose run and not the other, so the composite was read from 3 parts
  instead of 4 (2.86 against 1.76). The unstable part is **pelvis thrust**: between two pose runs it moves
  by a median 1.9 cm against a norm σ of 2.5 cm, and the shipped early-extension thrust flips at the same
  rate. The design's rule says hold the unstable part; holding thrust would also take away the hips' half
  of standing up, and the control shows the shipped fault already lives with it. **Not held; Mark's
  call.** 15 swings cannot tell 1 in 15 from 1 in 20.

## Not claimed

- There is no truth for any posture number. V1 graded face-on against down-the-line, which is not truth;
  V4 graded against Mark's coach eye at the level of a golfer, not a swing.
- ρ rests on within-golfer variation of three golfers; the part norms on authored judgement; V5 on 15
  swings of one golfer.
- Several absolute readings (address bend, balance, axis tilt) differ between golfers by amounts that
  could be the camera rather than the golfer; nothing here separates the two. Re-recording Jess with
  HackMotion is on issue #76.
