# Posture chart, stage 1: measures, bands and the lead-knee condition, measured

*2026-10-22. Stage 1 of `docs/design/posture_chart_design.md` §10. Run on the Mac (Mark, 22 Oct: stage 1 is
light, so it does not need the cabin PC), on video-free copies of the three golfers' libraries at
`/mnt/swingdata/scratch/posture-chart-20261022/copies/` (Mark 10 sessions, Nick 5, Jess 3; 18 ledgers).*

## What went in

- **31 per-position measures.** Changes from address, `delta` anchored at P1, read at P2…P8, with no
  signals:
  - head height P2, P3, P5, P6, P8;
  - hip depth P2–P6;
  - trunk bend P2, P3, P5, P6;
  - lead knee P2–P8;
  - trail knee P2, P3, P5–P8;
  - pelvis height P2, P3, P5, P6.
- **Nine cells reuse a measure that already existed:**
  - P4: `m_headLiftBack`, `m_spineBendChangeTop`, `m_trailKneeFlex`, `m_pelvisLiftTop`;
  - P7: `m_headLiftImpact`, `m_pelvisThrustImpact`, `m_spineBendChangeImpact`, `m_pelvisLiftImpact`.

  Each one was checked to be a P1→PN delta.
- **No P8 measure on three rows.** Hip depth, trunk bend and pelvis height are masked or outside
  their domain after impact.
- **Two context nodes,** `posture_level` and `posture_squat_push`, under `full_swing`.
- **46 band rows; no existing row changed** (Q2: fault corridors unchanged).
  - 26 `any` rows on the new measures: the Level band.
  - 7 `posture_level` rows on the reused measures.
  - 13 `posture_squat_push` rows: 6 where §7.1 differs, and 7 copies of Level on the reused
    measures. The squat-and-push node's parent is `full_swing`, so without the copies those cells
    would fall back to the fault corridor.
  - Literature rows carry n and the DOI; heuristic rows carry their authoring note.
  - There are no rows at pelvis height P3–P5, and no P8 rows.
- **`rigSensitive`** set on the six address absolutes: `m_spineBendAtAddress`, `m_kneeFlexAddress`,
  `m_armHangAddress`, `m_headPitchAddress`, `m_balanceHeelToeAddress`, `m_axisTiltAddress`. It is
  written only when true, and it is a warning on a change from address.
- **`lead_knee_folds_backswing`** (Q5, draft):
  - read on `m_leadKneeFlex_p4` against Kim 2021's +13 ± 7° (literature);
  - fires above +27° (Watch), Action above +34°;
  - `causes → head_drop_backswing` (moderate); `poor_hip_abductor_control → lead_knee_folds_backswing`
    (weak);
  - new drill `drill.leadKneeSteady`.
- **Two test fixtures that had been red since 79899b2f** were brought up to date:
  - `axis_direction_test` had no row for `sig_standUpBackDtl`;
  - `seed_conversion_test` expected 18 one-sided measures, but there are 19 since `m_headRiseBackDtl`.

## The gates

**1. Every row at every position, as stage 0 read it.** `measure_dump` on the copies gives these
medians (n), which are the design's §2.1 table to the decimal. The per-swing difference from
`posture_positions_2026-10-22.csv` is 0.0 at every row and position (142–217 swings per cell).

| Row | Golfer | P2 | P3 | P4 | P5 | P6 | P7 | P8 |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| Head height (cm) | Mark | 0.1 | 0.0 | 0.8 | −0.2 | 0.8 | 2.4 | 6.6 |
| | Nick | 0.2 | 0.5 | −0.1 | −0.4 | −0.1 | 0.1 | 3.8 |
| | Jess | −2.6 | −5.2 | −9.5 | −8.6 | −7.3 | −6.0 | 2.1 |
| Lead knee (°) | Mark | 3.9 | 14.2 | 14.2 | 13.0 | 4.4 | −3.0 | −15.0 |
| | Nick | 1.6 | 9.3 | 16.6 | 3.0 | −0.8 | −4.8 | −11.4 |
| | Jess | 6.1 | 19.4 | 36.0 | 23.4 | 2.5 | −9.3 | −7.9 |

(The other four rows match §2.1 the same way.)

**2. No grade changes from the measures and bands.** Regrading the 18 ledgers under the stage 1a
content, against a baseline regraded under HEAD's content: **0 of 36,936** condition rows differ.

**3. Only the new condition changes once it lands.** Regrading under the 1b content: 243 rows
differ, and every one is the new condition's first appearance:

| Golfer | fired | clean | not assessable |
|---|---:|---:|---:|
| Jess | **21** | 2 | 0 |
| Mark | 0 | 127 | 65 (no down-the-line view) |
| Nick | 0 | 28 | 0 |

In all three of Jess's sessions `lead_knee_folds_backswing` is a session pattern listed ahead of
`head_drop_backswing`. Her report now leads with the knee, the root, as Mark asked.

## "Well within" on home (carried-over decision C3)

The rule was built and then reverted with Mark's agreement. A ledger row stores the Ideal band
(|z| ≤ 1) as its corridor, so home's "inside the corridor by value" already means "well within".
On the copies, 14,987 two-sided readings were checked and none sits inside its stored corridor at
|z| > 1. Mark's "what you do well" list and its shares are the same under both rules. Nick and Jess
have no items under either, because each is short of the rule's minimum of judged swings or
sessions. A comment in `src/Coaching/swing_themes.h` now records this. The probe
`swing_themes_home.qml` had a stale check expecting "FAULTS" (Folio writes "Faults"); it now asks
the theme.

## Reproduce

- **Regrade:** `regrade_ledger --tag <t> <copies>/*/*/`, with `PINPOINT_CORE_*` pointing at the
  content under test.
- **Firing table:** one row per golfer, session, shot and condition, read from each ledger's
  `ledger.shots[].rows[].state`.
- **Readings:** `measure_dump --measures <the 39 ids> <copies>/<golfer>/*/`.

The copies are scratch, and are deleted when the build is accepted.
