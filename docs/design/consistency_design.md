# Consistency: where your swing repeats, where it gets loose, and what stood out on a bad shot

Status: DESIGN, 6 Nov 2026. Nothing built. The mock is done (§3); the items in §8 marked open need Mark's word
before the build. Mark's brief (6 Nov): "for each area, e.g. setup, posture there are curves where there is a clear
and consistent pattern at each phase of the swing … there are also some where there is a lot of movement and
inconsistency from one swing to the next … let the non-statistical expert golfer [see] where their patterns look
consistent … and where there is variance … where does your swing keep falling out of the same pattern. Secondly, when
a bad shot is hit … what in that shot was particularly different to any other? … make sure we don't already cover this
in some other way — the last thing we need to do is build another solution to pretty much the same problem."

Every number here reproduces from `docs/research/data/consistency/` with `tools/diagnostics/consistency_extract.py` and
`tools/diagnostics/consistency_tally.py` (Appendix A). The tally is also the reference the C++ goldens are cut from.

---

## 0. Summary

Both questions fit one table. **Areas of the swing run down the side and the spans of the swing run across the top.**
Each cell says how much that part of the swing moves from shot to shot: **steady, some or loose**, on a one-hue wash
that deepens as it loosens. Picking a shot puts a ◆ where it left the pattern, and lists what stood out on it: the ball
first, then the club, then the swing. A shot the launch monitor reads as short or offline is flagged for you.

- **Where it goes:** a new **Consistency** tab in Session diagnostics, between Session and Watching, and a Consistency
  column on Home's Your swing table.
- **What it is:** a roll-up. No new statistics engine, chart or norm. It reuses the box statistics, the P-aligned
  time, the norms, the launch-monitor readings, `PpChProfile` and the Readings curve. The table is `PpChMatrix`, extended.
- **What it replaces:** "Compare the cleanest and worst shot" in #66, and the rule behind Charts' "Apart from the session".
- **The honest limits:** a shot can stand out by chance, and the tab says so. Noise can make a steady swing look loose,
  so a reading that may only be noise carries a ⚠. Steady does not mean good: Session diagnostics still judges the fault.

---

## 1. What we already have

Most of this exists, in pieces, and that was the risk Mark named. The gap is an overview, and a way in from the ball.

| Question | Already in the app | What it lacks |
|---|---|---|
| Where do I repeat? | Readings: a curve with a box at each P-position, one reading at a time | No overview; every row has to be opened. This is what Mark was reading by eye |
| | Charts Session: where the curves part (`partAt`), one instant per row | One chart at a time |
| | Posture, Wrist, Club delivery: a "tightest / loosest" line | Three areas, three rules. Posture divides by the band's σ (`session_posture.cpp`); the others quote the middle half |
| | Session diagnostics Session: how often a fault fired | Judged against the norm, not your own repeatability |
| What was different about this shot? | Charts This shot: "Apart from the session" (`apartFrom`) | Cannot tell a real stand-out from chance (§2.4); knows nothing of the ball; flat list; whole-session box |
| | Relations "Moves with": includes launch-monitor readings | A session trend, not one shot |
| | Session diagnostics This shot: what fired | Against the norm, so a bad shot can read "quiet" |
| | #66: cleanest vs worst shot side by side | Never built; replaced by this |

Not a duplicate of Kinds (which groups the shots) or of Drift (which reads a trend through the session).

---

## 2. What the library shows

Mark's 12 sessions, 204 shots, read on the cabin PC. Five have 15 or more camera shots and are judged here: 4 July (15),
5 Oct (31), 7 Oct (50), 8 Oct (32), 20 Oct (19). One golfer only; the thresholds are first guesses on him.

### 2.1 The table holds up

Which reading is loose, against which is steady, is stable when a session is split in half at random (rank correlation
0.78 to 0.91). That figure is flattered by the gap between one metric and another, so the sharper test is **within a
metric: is the same span the loosest in another session?**

| Sessions compared | Rank correlation |
|---|---|
| 5, 7, 8 Oct against each other | 0.77 to 0.82 |
| 20 Oct against those | 0.62 to 0.68 |
| 4 July against the rest | 0.51 to 0.58 |

So the pattern is real and repeats, more strongly between neighbouring sessions. Measurement noise would repeat the same
way, so this does not prove all of it is the golfer.

5 Oct, the session in the mock (⚠ never fires on it):

```
                 P1      P1-P2   P2-P3   P3-P4   P4-P5   P5-P6   P6-P7   P7-P8
Setup            some
Posture          steady  steady  steady  steady  some    steady  steady  loose
Arms and club            steady  steady  some    some    steady  some    loose
Sequence                         some    some    some    some    loose
Lateral motion           some    some    some    some    some    some    loose
Release                                                  steady  steady  loose
Impact                                                           loose
Finish                                                                   loose
```

P7–P8 is not always loose: Posture and Lateral motion are steady there on 4 July and 20 Oct.
Cells across the 33 in a session: steady 7 to 18, some 8 to 19, loose 0 to 10, ⚠ 0 to 2.

### 2.2 Noise is a smaller problem than it looked

Between 35 % and 53 % of readings with a σ have a spread no bigger than that σ, which looked fatal. It is not, because
**noise can only widen a spread**. A small spread is steady whatever σ says. σ only matters for a reading that would
otherwise be called loose, and those are 1 to 6 readings a session (of 233 to 375). σ may also carry fixed offsets that
do not change from shot to shot, so even that may overstate it.

### 2.3 About a quarter of readings have no norm

About 27 % of readings have no norm to judge a spread against. They are not judged, and they stay in the list behind a
cell, marked as such. Filling these in belongs to #63, not this feature.

### 2.4 "Apart from the session" cannot find a stand-out

On the trimmed 4 July fixture (15 shots, 69 readings), `apartFrom` flags at least one reading on 12 of 15 shots. Shuffling
the shots gives 3.5 a shot, and the worst real shot's 9 only matches the shuffled session's 95th percentile. A bad shot
gets a list whether or not anything stood out.

A stricter rule (§4.3) still leaves background, so **a count of one to three means nothing by itself**:

| Session | Stand-outs a shot: median · mean | Shots with none | Most on a shot | Shuffled: mean · session max (95th pct) |
|---|---|---|---|---|
| 4 July | 3 · 2.33 | 3 of 15 | 8 | 3.39 · 7 |
| 5 Oct | 1 · 2.00 | 6 of 31 | 7 | 2.27 · 7 |
| 7 Oct | 1 · 1.68 | 11 of 50 | 6 | 2.15 · 6 |
| 8 Oct | 1 · 2.47 | 9 of 32 | **37** | 3.05 · 9 |
| 20 Oct | 0 · 0.84 | 11 of 19 | 5 | 1.26 · 4 |

The shuffle moves each reading on its own, which breaks the links between readings, so it is a rough yardstick.

**Swing 3 on 8 Oct** is the 37. Its top of backswing is 0.46 s and its impact 0.61 s after address, against about 0.74 s
and 1.00 s on another shot of that session. Every reading then compares the wrong part of the swing. Either the P-positions
are wrong or it was a very different swing; the video has not been checked. The build needs a guard either way (§4.3).

### 2.5 Bad shots: only two to test on

Flagged by the launch monitor, club by club, across the library: five shots. Three have no camera data. The two that do:

- **5 Oct swing 8:** 11 yd carry, 30 yd right, launched at 1°. Three swing readings stood out, within the background.
- **5 Oct swing 7:** 86 yd, smash 0.99 and ball speed 74 mph, both the session's lowest. Nothing in the swing stood out.

Swing 7 is the useful result: the tab says nothing stood out, instead of inventing a reason. Two shots are not a
validation. 5 Oct is the only large session with cameras and a full launch monitor, and Premier sessions have carry
but no offline.

---

## 3. What the golfer sees

The mock runs on real components and real figures. Files are in `docs/design/consistency/`.

![The tab](consistency/1_consistency_tab.png)

**The tab, top to bottom** (the Charts page pattern: headline, the plot, then the detail):

1. Kicker and headline in plain words: "You repeat your swing best in the backswing. It loosens on the way down, and your
   impact position moves most." Built by the core from the table, never by QML.
2. The ball in one line: carry and offline, each with the middle half, in whole units.
3. The table, then its key.
4. A cell, when picked: the readings behind the word, loosest first, as `PpChProfile` rows. A row opens the same curve
   as Readings.
5. The picked shot: what the launch monitor made of it, a sentence, then what stood out, grouped as the ball, the club
   and your swing. Each row carries this shot, the median and the session's box. It ends: "These happened on the same
   shot. That does not show one caused another."

![Two bad shots](consistency/2_bad_shots.png)

![Opening a cell](consistency/3_open_a_cell.png)

**Narrow.** When the words no longer fit, cells switch to marks on the same wash (· ◐ ● ⚠) and the key appears. No
column is ever dropped.

![Narrow form](consistency/4_narrow_form.png)

**Home.** One column on Your swing, after Trend: a word and the loosest span, from the sessions that can judge it, the
newest counting most (fading 0.75 a session, as the work-ons do). Words only, because that table already has its bars.
An open area gets its spans in its detail. Ball flight shows "-": it has no spans.

![Home](consistency/6_home_your_swing.png)

**Replay and Print** (a diagnostics surface goes everywhere). Replay opens on the headline, the table and the picked
shot's one sentence. Print gets a Consistency page in the session report, built from the same presenter, with the table
and the flagged shots, no affordances. Neither is mocked yet.

**The words.** Steady, some, loose. Mark, 6 Nov: "the loose term means 'all over the place'". The first draft's "mixed"
and "varies" both just meant "not steady", and had no order (§8; options on `consistency/7_wording_and_heat.png`).

---

## 4. Rules

Constants live in one header and are pinned by the goldens. All are first guesses on one golfer.

### 4.1 A reading

- **Like with like:** each shot is compared with its own club. Each value is taken less its club's median. A club with
  under 3 shots is left out, and the head says so.
- **A span:** the curve is sampled at five points inside the span, on P-aligned time, valid samples only. **Spread** is the
  middle half ÷ 1.349, taken at each point, and the median of those. At least 5 shots.
- **Positions, not spans, where the position is the point:** Setup reads P1. Impact reads P7 itself: a shaft lean halfway
  down is not what the Impact area judges. An instant-only reading sits in the span ending at its P-position. Backswing
  timing reads are stamped at impact but sit in P3–P4.
- **The verdict:** spread ÷ the norm's σ, Posture's rule (decision 6 there) for everything. Under 0.75: steady. At or
  over: loose, unless the spread is no bigger than the reading's own σ, when it is ⚠ "too close to measurement error". No norm: not judged.

### 4.2 A cell

Of the judged readings in that area and column: a third or more loose is **loose**, at least one is **some**, a third or
more ⚠ is **⚠**, else **steady**. No judged readings shows "-". A blank means the area does not apply to that span:

| Area | Read at |
|---|---|
| Setup | P1 |
| Posture | P1, every span |
| Arms and club, Lateral motion | every span |
| Sequence | P2–P3 to P6–P7 |
| Release | P5–P6 to P7–P8 |
| Impact | P6–P7, reading P7 |
| Finish | P7–P8 |

**P7–P8 is after the ball has gone.** It draws lighter, and it is left out of the headline and Home, except for Finish.

**Home's word:** steady 0, some ½, loose 1, averaged over an area's columns: under 0.2 Steady, under 0.5 Some, else
Loose. The loosest span is named unless the word is Steady.

### 4.3 A stand-out

One shot against the session's others, on the same club, **itself left out** of the comparison:

- at least 3.5 robust spreads from their median, and past 2σ where the reading has a σ;
- across the whole span, not one instant in it;
- one row per metric, at its most extreme column; ordered ball, club, swing, then by size.

The sentence says what the count means: with none, "Nothing in your swing stood out"; with a count no higher than most
shots show, "this may be chance"; above the shuffled session's 95th percentile, "more than chance would give". The
shuffle is seeded, memoised per snapshot and off the GUI thread.

**The P-ladder guard.** Alignment uses `session_warp`'s anchors, which already drop a P-position whose segment is an
outlier. A shot that loses anchors keeps its readings with a ⚠ ("the timing of this swing's positions looks unusual"), and
its span comparisons are not counted as stand-outs. It is shown, never hidden.

### 4.4 A bad shot

The launch monitor, against the same club that session, needing 8 shots with the reading:

- **short:** carry below the lower Tukey fence;
- **offline:** offline past either fence.

Descriptive only: PPS does not model intent, so a shot deliberately aimed elsewhere can read offline. With carry and no
offline (Premier), it flags short only and the tab says so. With no launch monitor, nothing is flagged and every shot
can still be picked. Mark, 6 Nov: LM data for now, "we can fix it later". `review.rating` exists on every swing as a
possible later route; two of the 204 shots carry one.

### 4.5 The house rules that apply

Gate on data, never the session type. Every reading is shown with its ⚠, never withheld. "-" means no value, and a
reading with a value keeps it. Metric hues stay. Figures follow the house rounding, with σ only as the ±. Co-occurrence,
never cause. Words come from C++, and QML only places them. No reading is judged on another club's shots.

---

## 5. One stand-out rule

Charts' "Apart from the session" moves to §4.3, so the Consistency tab and Charts can never disagree. Its heading and place
stay. `apartFrom` keeps its name for the boxes: a dot past a whisker is still drawn, and `side` still means past a whisker.
The change is what counts as apart, and it now compares within the club. This changes what Charts shows on existing
sessions, and its tests.

---

## 6. Design-system changes

Each needs Mark's approval first (`docs/design/system/README.md`). Approved items are in §8.

| Change | Detail | Screens |
|---|---|---|
| Extend `PpChMatrix` | Named columns, full rectangle, generic cells, optional wash, a general `cellActivated`, a short form that never drops a column. A new page in `docs/design/system/components/` | Consistency, Relations (must look exactly the same: a control render is part of the build) |
| Move it to `src/Gui/typeset/` | `roles.ch` exists only on PpChRoles. The cell width goes through `r.ch` so any PpTypeset can use it | Both |
| Heat tokens | Three alphas on `colorAttention`, in `Theme.qml`: some 0.15, loose 0.40, after the ball 0.12. `make_design_system.py` regenerates `tokens.json` | Consistency |
| The wash rule | Design system §15 says the corridor is "the only wash in a chart". It gains one exception: the heat of a table cell. The word stays beside it, so colour is never the only channel | Consistency |
| A new Session diagnostics tab: This shot, Session, Consistency, Watching | `PpTabs` as it stands | Session diagnostics |
| A Home column | `HmSwingAreas`'s columns and `HmAreaRow` | Home |

Why one hue: it says how much, not good or bad. Red and green would say a steady fault is good. `colorAttention` is "watch",
not `colorWarn`, which means a fault. A picked cell takes an outline, because the wash has taken over its old highlight.

`PpChProfile` and `PpChText` need PpChRoles, which `PpDiagRoles` is not. The tab makes its own PpChRoles, and the mock
proves that works. No line is added to `design_system_allowlist.txt`.

---

## 7. What it does not do

- It does not say why. Two things on the same shot are not a cause.
- It does not judge good or bad: a steady fault reads steady.
- It does not compare one golfer with another: the spread is always the golfer's own, and the norm's σ only scales it.
- It does not read the session's order: Drift does that. Shots 1 to 3 of 4 July had the most flags, possibly warm-up, unchecked.
- No new chart, no heat map on a curve, no bad-shot verdict without a launch monitor.

---

## 8. Decisions

| # | Decision | State |
|---|---|---|
| 1 | Roll up the existing maths; no second engine | Mark, 6 Nov ("this is excellent") |
| 2 | Both the session and Home | Mark, 6 Nov |
| 3 | Bad shots come from launch-monitor data for now | Mark, 6 Nov |
| 4 | Extend `PpChMatrix`, not a new component | Mark, 6 Nov ("this looks good") |
| 5 | Words: steady, some, loose | Mark, 6 Nov |
| 6 | The one-hue wash, and the exception to the wash rule | **Open:** read as yes from Mark's 6 Nov reply; needs saying outright |
| 7 | The tab sits in Session diagnostics, is called Consistency and comes before Watching | Mark, 6 Nov ("move the consistency tab to before Watching") |
| 8 | P7–P8 lighter, and out of the headline and Home except Finish | **Open:** the mock does this |
| 9 | Narrow panels use marks on the wash | **Open:** the mock does this |
| 10 | The Home column's width comes from the Habits bar | **Open:** the mock takes it from Biggest fault, which cuts off two names in Folio |
| 11 | The 28 hand-placed readings (Appendix B) | **Open:** Mark to review |
| 12 | Charts' Apart moves to the stricter rule (§5) | **Open:** it changes what Charts shows |

---

## 9. Build plan

Each stage ends with something Mark can look at. All commits come at the end.

1. **The maths.** `src/Gui/charts/session_consistency.{h,cpp}`, pure and header-tested like `session_relations.h`: spreads,
   verdicts, cells, stand-outs, ball flags, Home's word. Reuses `box_stats.h`, `session_warp`, `session_traces` (`envelope`)
   and the norms. A golden test against the tally. Add an optional `consistencyArea` to `MetricDescriptor` for the 28
   hand-placed readings; the rest come from the conditions through `swing_areas_pack.h`.
2. **The shared rule.** Charts' Apart moves to the stand-out function (§5).
3. **The presenter.** `charts/charts_consistency_presenter.{h,cpp}` on `SessionSeriesModel`: every word and figure, off the
   GUI thread, a Sentence for the head and each line.
4. **The component.** Extend and move `PpChMatrix`, heat tokens, design-system page, regenerate `tokens.json`, then a
   Relations control render.
5. **The tab.** Analyse, then Replay. Rows open Readings' curve. Windowed run with Mark (one batch; it takes his screen).
6. **Home.** `<session>/consistency.json`, written by the work-ons catch-up with the same fingerprint rule (rule version,
   content stamp, swing document size and mtime), off the GUI thread, because Home cannot load swing documents. Then
   the area roll-up in `swing_areas.h` and the column.
7. **Print.** The Consistency page in the session report.

Files: new as above, plus `PpConsistencyView.qml` in `src/Gui/diagnostics/`. Modified: `session_relations.{h,cpp}`,
`PpChMatrix.qml` (moved), `PpChartsRelationsView.qml`, `charts_relations_presenter.cpp`, `PpDiagAnalyse.qml`,
`PpDiagReplay.qml`, `PpDiagPrint.qml`, `work_ons_controller.{h,cpp}`, `swing_areas.h`, `HmSwingAreas.qml`, `HmAreaRow.qml`,
`metric_catalog.cpp`, `metric_descriptor.h`, `Theme.qml`, `tokens.json`, `pinpoint_qml_design_system.md`.

**Done means:** the goldens match the tally; `design_system_lint_test` and `design_system_tokens_check` pass; Relations is
pixel-identical to its control; the tab, Replay, Home and Print have been shown to Mark as annotated sheets, including a
windowed run; a tally over other golfers' sessions has been read and the thresholds kept or changed. Then #66's cleanest
vs worst item is closed as replaced.

---

## Appendix A: reproduce

```
# on the cabin PC, where the share is local (read-only; 15 s for 204 swings)
python tools\diagnostics\consistency_extract.py C:\PinPointStudio\Mark-Liversedge C:\PinPointStudio\scratch\consistency\extract

# on the Mac
python3 tools/diagnostics/consistency_tally.py /mnt/swingdata/scratch/consistency/extract docs/research/data/consistency
```

The tally prints every figure in §2 and §4.2 and writes `consistency_readings.csv`, `consistency_cells.csv` and
`consistency_shots.csv`. Its seeds are fixed. The extract is run output, on the share; delete it once this design is
accepted, as the CSVs hold what the doc quotes.

## Appendix B: readings placed by hand

A reading's area normally comes from the conditions that detect on it, taking the group that uses it most (51 of the 83
readings on 5 Oct). These 28 have no such condition:

| Area | Readings |
|---|---|
| Sequence | `clubAngularSpeed`, `clubheadSpeed`, `handSpeed`, `leadArmAngularSpeed`, `thoraxAngularSpeed`, `xFactor`, `tempoBackswing` |
| Arms and club | `thoraxRotationSigned`, `elbowAlignment`, `handPlaneCurve`, `swingPlaneCurve`, `shoulderPlaneAngle`, `handPlaneSteepening` |
| Posture | `headTilt`, `spineSideBend`, `trunkRise`, `hipDepthButt`, `headPitch`, `spineBackLine` |
| Setup | `ballBodyDistance`, `stanceWidthMm`, `toeLineAngle`, `leadFootFlare`, `trailFootFlare` |
| Impact | `attackAngle`, `lowPointFromAttack` |
| Lateral motion | `plumbBobDistance` |
| Release | `trailWristFlexExt` |

Four more are unplaced and not read: `compoundMiss` (ball flight, which has no spans), `shoulderPlane`, `swingPlaneIotaBack`
and `swingPlaneIotaDown`.

The group rule itself gives some odd homes: `thoraxRotation` lands in Arms and club and `pelvisRotation` in Sequence.
Worth reading with this table.

## Appendix C: the mock

`build/run-me/consistency-mock/` (not committed): `mock/` holds the QML and its data, `sheets.py` the review sheets.
It runs on the typeset harness with no build; Home uses its own probe, on a copy of the athlete folder.
