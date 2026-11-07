# Variability marks: decisions

**7 Nov 2026.** What was decided for the variability marks, where the build differs from the spec or fills a gap in it, and what is still open. The spec is `spec.md`; the plan is `plan.md`. The design system (`docs/design/system/`) describes what was built. Where the two differ, this file says so.

## 1. The spec's decisions (spec §12)

| Decision | Chosen | Rejected, and why |
|---|---|---|
| Consistency mark | Dispersion (scalar) and Overlay (time series) | Pendulum: closest to the Wi-Fi idiom, but less golf, less kinematic |
| Measurement mark | Needle with wedge | Focus (camera-specific); Error bar (too technical) |
| Trend | Spread funnel | Mark strip (no within-grade movement); grade steps (generic, technical) |
| Typicality | Your group plus trace on envelope | Band and pin. Its corridor bracket was adopted as `showCorridor`. |
| Ball flight | Keep the existing launch-monitor plot and add a badge | New shot-pattern, strip or zone visuals: unnecessary, since real data is already plotted |
| Rendering | Shapes with `CurveRenderer` | Canvas, SVG, QPainter, custom scene-graph nodes |

The spec's open items:
- **Golfer-layer box-plot replacement:** a data-driven Dispersion cell with a target line, bias offset and `showCorridor`. Not before the bands are calibrated.
- **Measurement-quality surfaces** that show no figure today (spec §9.4): add them after the audit.
- **Cohort-specific scales:** wait for the cohort norms.

## 2. This run's decisions (sessions 1–4)

### Mark's rulings (7 Nov)
- **Corridor half-width** is (σLo + σHi)/2, the in-range band (|z| ≤ 1): the σ the Consistency matrix and Posture repeatability already use. Spec §8.2 step 4.1 amended.
- **`data` is renamed `values`** on the mark (§4.1 amended): `data` is Qt's own child list.
- **Print takes `roles`.** The mark, badge and funnel take an optional `roles` (a PpTypeset) and, in print, use its inks, `u()`/`g()` and type (§9.5.4 amended). PpTypeset gains a print `good` ink: the fixed aesthetic's `colorGood`, as `warn` and `attention` are read.
- **The Needle Cell keeps its ruler ticks at grade 0.**
- **Windows D3D11 is not measured in this run.** Recorded as open; check it in Session 5.
- **This run is sessions 1–4 only.** The full qml_ui suite runs once, at the end.
- **The build follows the spec; the docs describe what was built.** A gap between the two is a stop for Mark first, then recorded here.
- **The design system is the repo folder** (`docs/design/system/` and `Theme.qml`). Nothing is synced to a claude.ai artifact.
- **Dark-theme wash strength** (after the windowed sheets). In dark themes the Needle's 12/16/24 % wedges could not be told apart (options compared: `build/run-me/variability-review/needle_fill_options.png`). Ruling: a new Theme token `variabilityWashAlpha`, a screen-only multiplier on every translucent wash, 1 in light themes and 3 in dark, the product clamped to 1. The washes are the Dispersion Cell's group ellipse, the Needle wedges (Icon and Cell, small-size alphas included), the funnel's band fill, the typicality inner zone and the trace's envelope. Strokes, the band's edge, the 70–85 % dots, the ghost dots, the dashed outer zone and every line are not. Print is unaffected: it uses `variabilityPrintAlphaScale` alone, since paper is light. `variabilityWashAlphaStrong` is not added. Spec §3 and §10 amended. Built as an optional theme key whose default is `dark ? 3 : 1` (a theme or mode block may set its own); the mark and the funnel apply it in one helper each (`_washAlpha`), and the geometry provider keeps the spec's base alphas.

### Session 1: grader and data model

Gaps the build filled:
- **scaleType.** 20 metrics are Ratio (speeds, distances, durations, smash, tempo ratio, peak speeds, spin magnitudes). `impactShaftLean` is Circular (a line angle that crosses the seam at the top). `kinematicSequence` is Ordinal (k = m!). Headings and body angles stay Interval: they never reach ±180°. No `variabilityScale` or `measurementTolerance` is set; a guard test enforces that.
- **Trend SE:** per-session SE ρ/√(2(n−1)), propagated through the unweighted least-squares slope. Sessions are indexed 0…m−1 whatever their dates. Ordinal series get no trend.
- **Basis-string scale names:** "of corridor" only against a corridor; any other scale reads "of scale"; the CV fallback reads "CV 0.04". A fallback scale is never called a corridor.
- **Amplitude-limited trace:** the scalar basis form with the RMS SD (§7.5 has no row for it).
- **Measurement basis:** empty (§7.5 has no Needle row).
- **"Peak on time", "higher", "lower":** |z| ≤ 1 against the window, as §7.4.
- **Units:** ° and % (and :) attach to the number; other units take a space.
- **Zero-spread typicality:** z = 0 when the swing matches, else ±∞ (Unusual).
- **Precedence:** "no scale" is checked before n, so a measure that more swings couldn't grade never shows "Not enough swings".
- **σnoise** is subtracted from the trace amplitude too, point by point.
- **measurementLimited never changes the grade;** the surface substitutes "Checking".
- **Reviewer's rulings:** trace and typicality use the circular SD and mean for Circular metrics; a measurement with no `uncertaintyN` skips N_MIN (a validation RMSE comes from its own study); a flat trace with no amplitude scale is ungraded, never graded on ρₜ alone; the §7.5 ball-flight string is built; the trend run is capped at the latest 12 sessions, the funnel's cap.
- **Façade:** `variabilityResult` adds `hasRho`. ρ is NaN for ordinal and Needle results. `ungradedReason` "" means none. `domain` is "measurement" for the Needle basis only.
- **Calibration data:** a read-only extract of all five athlete libraries, made on the cabin PC on 7 Nov (23 sessions, 284 swings); grading runs on the Mac. No general outlier or mishit flag exists: swings are excluded on warm-up, and readings on the P-ladder or traces on `consistencyGuarded`. The calibration tool gives launch-monitor rows no σnoise and no measurement rows (vendor figures); the LM value per shot is its first finite phase sample. Guarded swings are excluded from the ordinal window as well.

Awaiting Mark:
- The eleven grader gaps above, as a set.
- **Ordinal wording.** "Same order in X of Y swings" reads badly for a binary checkpoint.
- **kinematicSequence** (ordinal) gets no trend and no funnel, because §8.8 is defined on ρ.
- **Ordinal partial orders.** The calibration graded the modal placed set; calibration-only (report proposal 8).
- **Trace typicality thresholds.** A typical swing's RMS z(t) sits near the |z| = 1 edge (median 1.08; "Your usual" only 15–55 % of the time). A calibration item.

### Session 2: the mark, the badge and the rendering spike

Gaps the build filled:
- **CurveRenderer stays** on the macOS evidence: clip leak 0 px, 60 fps flat over 500 rows, no delegate created mid-scroll (`spike/README.md`). The 11 Oct PpMark clip note is most likely an offscreen artefact.
- **Overlay Cell baseline colour:** `colorBorderStrong`, as the Icon's.
- **Strokes vs radii:** radii are design units × k; only strokes are constant pixels (§4.2).
- **Needle details:** the Cell ruler and ticks are 1 px; both pivots are full tone; the Cell needle has a round cap and is 1.5 px at every size; Cell wedges have no small-size alphas. Wedge alphas 12/16/24 % pair with half-angles a / 0.62a / 0.3a in that order. Icon sectors are closed polygons with chords of at most 1 unit.
- **Hosting tables need a real model:** an integer model breaks unqualified names in delegates.
- **The badge's layer property is `contentLayer`** ("golfer" | "technical"): `Item.layer` is FINAL. §5 doesn't name it.
- **Tooltip:** there is no design-system tooltip component, so the badge uses the Controls attached ToolTip, as PpButton and PpRowAction do; golfer layer only.
- **Type:** §5's body 12 and body2 are the same token (`fontSzBody2`, 12); data-sm is `fontSzDataSm` (13). The dense icon is `sp(16)`: §5 gives the number and no token.
- **Print fills:** dots, ellipse, wedges (and later the typicality fills, envelope and funnel band) × `variabilityPrintAlphaScale`, clamped to 1; strokes keep their alpha. `medium` is a plain property; typeset hosts bind `roles.medium`.
- **Measurement-limited badge** reads "Measurement: Checking (0 of 5)".
- **Tokens:** `variabilityHysteresis` is a plain Theme property, not a theme key; the generator refuses to run if it differs from `kHysteresis`. `tokens.json` gains a top-level "behaviour" group for it. Token spellings: trend `tightening | loosening | holding` ("insufficient" returns "" quietly); typicality `usual | aLittleOff | unusual`.
- **Windowed sheets** (`sheets/`, 14 themes, Metal, dpr 2): every grade separates and all five grades stay distinct in greyscale. Weakest pair: the Dispersion Icon at grades 1 and 2; the word carries it. Studio's light body face makes the words look faint; that is the theme. The dark-theme washes were visible but, Mark judged, too faint to tell the Needle's grades apart, so `variabilityWashAlpha` was added (Mark's rulings, above).

Awaiting Mark:
- **Tone fade on delegate reuse.** §4.2 asks for a fade; in a scrolling table it looks like colour lag. Snap on reuse?
- **Grade-0 accessible name** reads "… (0 of 5)" when §4.2 is followed literally.
- **Overlay Cell, grade 1, trace 4** (h 22.1) peaks at y ≈ −0.56, half a unit above the cell. Followed literally.

### Session 3: companion visuals

Gaps the build filled:
- **Funnel breaks:** sessions stay evenly spaced; each band runs 2 px past its last session, so the gap is 4 px, centred between sessions. The break tick spans ±(H/2 − 2)/2.
- **Dashes:** [2, 2] for every dashed element. The funnel's centre line runs the full width.
- **Sessions with no ρ** are skipped, and their break carries forward. The 12-session cap applies after skipping.
- **Ghost jitter:** FNV-1a 32-bit over the UTF-8 swing id, finished with murmur3's fmix32 (plain FNV-1a put sequential ids in one row), mapped to [−4, 4). Clamped ghosts are held at x 4 / 76.
- **Chevron:** an open ‹ or › just inside the clamped ring, 1.5 px, in the tone; one unit further out (tip 71, arms 69, mirrored) so it clears the outer zone's dashes.
- **Corridor bracket:** at y 23.25 with 0.75-unit end ticks; a clamped end has no tick. Below k = 1 it overflows the item's bottom by at most 0.25 px.
- **Funnel layout:** compact label beside the graphic (gap 10), wide label beneath (gap 6). The compact width 120 is a number, not a token. The trend word is `colorText2`.
- **Funnel too few sessions:** the accessible name is "M: Word. Trend after 3 sessions". Bands of older runs with 3 or more sessions are hidden too; the centre line and markers still draw.
- **Funnel `variant`** (Dispersion | Overlay, default Dispersion) so the too-few badge is the measure's own mark.
- **Typicality Icon ring:** a 1.75 px stroke and a surface fill, as the Cell's (spec silent). Past |z| 2.2 the Icon looks the same, with no chevron.
- **Clamping:** a ring or ghost at |z| = 2.6 sits 0.4 units further out than a clamped one (the spec, literally).

Awaiting Mark:
- **The trace corridor** is undefined in the spec. Built as a timing window `{ loT, hiT }` in normalised time.
- **Trace vertical scale:** min(17 / maxPositive, 2.5 / maxNegative), zero on the baseline. A mostly negative curve draws small.
- **Typicality Icon ring at |z| 2.2** (x 23.55, r 2.6) overflows the 24-unit icon by about 2 units. Followed literally.
- **The funnel never applies the "Checking" substitution:** its word comes from the session grade. Should a measurement-limited funnel say "Checking"?
- **TypicalityTrace asked for an Icon** warns and draws the 80 : 24 Cell, breaking the icon slot. The alternative: draw the Your-group Icon from `values.rmsZ`.

### Session 4: design-system documentation

- **Pages:** `components/PpVariabilityMark.md`, `PpVariabilityBadge.md`, `PpVariabilityFunnel.md`; the index under Marks; the four vocabularies beside `ClubFormat` in the index's note on helpers; "Variability words" under Content and "Variability marks" under Cards and marks in the README. Static previews in `previews/`, cut from the windowed Instrument Light renders.
- **Token usage notes corrected** in `tools/theme/make_design_system.py` (tokens.json regenerated; Theme.qml unchanged): `variabilityFunnelH` is the graphic's height in both forms, not only the compact one; `variabilityCellW` is read by no component (a Cell is drawn `size` × 80/24); a bare Cell mark needs `size` set to `variabilityCellH`; `variabilityPrintAlphaScale` applies to every translucent fill (dots, ellipse, wedges, inner zone, ghosts, envelope, funnel band), never with `roles.tintK`; the hysteresis also applies to s′ and ρm and is read by no component. Theme.qml's own comment on `variabilityPrintAlphaScale` still lists only the dots, ellipse and wedges.
- **Not synced, by design:** the §4 and §6 geometry tables and the §8 grade basis are not repeated in the docs. They live in `spec.md` and `src/Gui/variability/variability_geometry.cpp` / `src/Metrics/variability/`, and the pages point there.
- **Sync step (plan session 4, tasks 1 and 7): skipped (Mark, 7 Nov).** The design system is the repo folder; there is no claude.ai artifact to sync.

Found while documenting, for Mark:
- **No variant for ordinal measures.** §2 names Dispersion for scalar and Overlay for time series, and none for an ordinal result (the kinematic-sequence order). The pages say: use the recommended default, Dispersion.
- **No C++ helper spells the typicality band or the trend direction** as the QML tokens. The facade covers `variabilityResult` only, so a presenter must write "usual" / "aLittleOff" / "unusual" and "tightening" / "loosening" / "holding" / "insufficient" itself. Session 5 should add one before it wires the marks.
- **Ball flight's one badge.** §9.6 asks for one per-club Dispersion badge, but offline and carry are graded separately and nothing says how the two combine into one grade (the worse, as a trace takes the worse of ρₐ and ρₜ?). Not built; the badge page says so.
- **The typicality corridor in z.** The provider takes the corridor in z units; the pages define it as (lo − x̄)/SD and (hi − x̄)/SD over the same window, the only reading of that. No presenter builds it yet.
- **Badge previews.** The windowed sheets set each word under its mark, so the graded, technical-layer and measurement-limited badge previews (`previews/badge_graded.png`, `badge_technical_limited.png`) are cut from the Session 2 offscreen grid (`/mnt/swingdata/scratch/variability/renders/s2/grid_instrument_light_dpr1.png`, software painter, dpr 1). Every other preview is cut from the windowed renders of the current build (Metal, dpr 2, 7 Nov ~15:50, `/mnt/swingdata/scratch/variability/renders/windowed/`): the sheets, the funnel and the typicality gallery. They show the fixed ghost jitter (ghosts spread vertically), the chevrons one unit outboard, the dark-theme ×3 wash (`mark_needle_dark.png`) and the synthetic funnel bases on the latest unbroken run, capped at 12 ("over 4 sessions", "over 12 sessions").

### Session 5: audit and retrospective application

- **The family guide moved out of the README (Mark, 7 Nov).** The README's "Variability words" and "Variability marks" sections outweighed every other component's entry, so their text moved word for word to `docs/design/system/variability.md` (principles, family, eligibility, classification, rules, words, phrasing, basis strings). The README keeps one bullet under Content and one under Cards and marks, each linking there; the component pages and the index link to it too.

#### Mark's rulings on the audit (7 Nov)

The audit's classification and copy audit were posted and ruled on one question at a time. The rules that belong to the design system are in `docs/design/system/variability.md`; the rest are here.

- **R1, cells that pool readings.** The Consistency matrix keeps its layout and shows no marks. Each cell takes the grade at its loosest third (the 67th-percentile ρ of its graded readings) and shows that grade's word. The marks go on the reading rows a cell opens to.
- **R2a, the matrix's club pooling.** Residuals about each club's median, pooled across clubs, are within-club spread: eligible, one grade.
- **R2b, displays that pool raw values across clubs** (box strips, Charts session curves, Readings, Wrist, Posture): one badge per club, each named; the plot stays as drawn.
- **R3, noise.** Calibration proposal 1 is adopted: grades are taken on the observed spread and a propagated σ is never subtracted as σnoise. A reading whose observed spread is no larger than its own propagated σ (the matrix's old "⚠ error") is measurement-limited: "Checking" on the golfer layer, "· measurement-limited" on the technical layer. This supersedes spec §8.6's subtraction until a validated noise figure exists for a reading.
- **R4, ball flight.** Ungraded until shots record an intent: no badge on the launch-monitor dispersion, the Consistency ball line, the cover's ball tiles or the ball half of the launch-monitor tiles.
- **R4b, club delivery.** Movement, so eligible per club: club speed, path, face, face to path, attack, dynamic loft, lie, low point, strike location. Smash factor is an outcome and stays ungraded.
- **R5, Home's Consistency column.** It grades the measure of the **Biggest fault** cell beside it (how that fault is chosen is unchanged: `summariseAreas`, `src/Coaching/swing_areas.h`). The window is the fault's last 20 readings on that measure across sessions (`Variability.nWindow`), from each session's `diagnostics.json` ledger, warm-ups and unassessable rows left out, centred per club. One golfer-layer badge. Yes/no, composite and fixed-number conditions are ungraded ("-"). The opened area's row of span words goes; each fault row in the opened list gets its own badge over the same window.
- **R6, sequence peak-timing spread.** Ungraded: its only yardstick is a cohort (19 touring professionals). It waits for a scale decision. The work-on page's "2.1× as widely as touring professionals" sentence stays.
- **R7, Posture and Wrist.** Graded against the band each panel draws (the posture pattern's σ, the wrist style's σ), "of scale" in the basis. The Wrist ranking moves from raw degrees to the grade.
- **R8, typicality's window.** The session's other swings with the same club, this swing left out, at least 8.
- **R9, client report pages.** A badge replaces only the figures that describe spread; the pages' other figures stay.
- **R10, work-ons.** The consistency badge sits beside a work-on's status wherever it shows. Easing and cleared still follow the firing rule alone.
- **R11, wording.** The design system's words apply everywhere, through the vocabularies. Ungraded spreads use statistic words; level trends say "no change"; the median is "median"; "Repeatable?" becomes "Consistency"; "Sequence not repeatable" becomes "Sequence changes from swing to swing". The old-words table is in `variability.md`.
- **R12, one-sided norms (review, 7 Nov).** A floor or ceiling is how a value is judged, not how wide the golfer may spread. Grade against (σLo + σHi)/2 whenever the norm states both, whatever its shape; only a norm without them is ungraded. The launch-monitor panel follows the same rule.
- **R13, warm-ups (review, 7 Nov).** Warm-ups stay in every variability window: the golfer should be warmed up before PinPoint assesses them. The ledger's first-shots down-weighting is diagnostics' own and doesn't apply here. Guarded readings (an untrusted P-ladder) are still left out.
- **R14, a work-on's report page** grades that session only; Home grades the last 20 readings. They can differ, by design.
- **R15, dead code.** `consistency_summary` and the other code the review found dead is removed; the old `consistency.json` files stay in the libraries, unread.
- **Ball flight has no badge at all** (R4), typicality included: no typicality badge on the launch monitor's ball tiles or ledger rows, or on the Consistency tab's carry line.
- **R16, traces ungraded until calibrated (renders, 7 Nov).** No trace has a corridor, and on the grader's fallbacks (10 % of the mean peak, 20 ms timing) every session curve read Scattered. Until a trace has a real corridor (calibration proposal 5), the fallback rungs count as no scale: no Overlay badge on session curves and no TypicalityTrace badge on Against.
- **R17, the marks take the host's `roles` on screen (Mark approved, 7 Nov).** In a typeset panel the badge's word and basis, the mark's size and the funnel's label follow the host's PpTypeset on screen as they already do on paper; with no `roles` they read Theme as before. A design-system behaviour change, approved before it was built.
- **R18, typicality's basis says "average".** The basis reads "this swing 612 · average 620": it is the window mean (z needs it), and it sits beside median columns, so it never says "usual".
- **R19, the Launch monitor appendix is the technical layer:** its badges keep their basis strings on paper.
- **R20, Posture and Wrist on paper: one badge per row (review sheet, 7 Nov).** The client report's Posture and Wrist pages give each row one golfer-layer badge, the cell rule (R1, loosest third) over the row's graded positions, beside the row's name. The per-position badges stay on the Charts screens.
- **R21, Wrist P2 keeps its badge;** the most/least ranking still starts at P3 ("ranked from P3 on").
- **R22, typicality uses the robust z everywhere:** z = (x − median) / (IQR/1.349) over the same window (R8), for the band, the ghosts and the corridor in z. The basis reads "this swing X · median Y" (supersedes R18's "average"). Spec §8.7 (mean/SD) is amended. It now agrees with the stand-out pick (robust z ≥ 3.5) and with the medians the screens show.
- **R23, the common order** is graded over the segments the common-order sentence uses (a segment that is never or rarely placed, such as the face-on chest, drops out), with k = m! for those m segments; the basis names them ("Same order in 12 of 15 swings · pelvis, lead arm, club").
- **R24, the condition rename stays;** sessions regrade lazily, once each, when next opened. No sweep.
- **The 5 Oct matrix after the fixes** ("backswing repeatable … impact position moves most") matches Mark's sense of the session.
- **Work on `main`.** The plan's branch instructions were replaced: every session commits on `main`.

#### Session 5: applied (7 Nov)

What the retrofit left alone, what has no surface yet, the measurement surfaces still without a Needle, and the readings the build had to make. Where each badge, mark and funnel now sits: the component pages' "Used by". The Consistency feature's own record: `docs/design/consistency_design.md` §10.

**Leave rows.** Unchanged, each with its reason.

| surface | display | why it is left |
|---|---|---|
| Home | Trend chips, the Trend column | How often a fault fires from session to session, not spread. Level trends say "no change" (R11). |
| Home | What you do well | The in-band share: accuracy, not repeatability. |
| Home | Typical value against the corridor | Position against the corridor (accuracy). |
| Home | Work-on status (active, easing, cleared) | Follows the firing rule (R10); the badge sits beside it and doesn't change it. |
| Home | Matchups | Faults that occur together: co-occurrence, not spread. |
| Session diagnostics | Shot-by-shot strip, medians, key figures, out-by | Per-shot values or location against the norm (accuracy). |
| Session diagnostics | Kendall trend | Drift in the level through a session, not spread. |
| Session diagnostics | Direction agreement | How far the readings agree on a direction: a finding's certainty. |
| Session diagnostics | Bookends | First shots against last: a comparison of level. |
| Session diagnostics | Usually yours | A typical value (the median): level. Its trend words follow R11. |
| Session diagnostics | Causes | A causal finding: `PpStrengthMark`'s job. |
| Session diagnostics | Norm tags | A label naming the norm, not a figure. |
| Diagnostic model | Model browser | The model's structure, with no swing data. |
| Sequence | Peak-timing spread (R6) | Its only yardstick is a cohort (19 touring professionals); it waits for a scale decision. |
| Sequence | "In sequence" | Accuracy (the right order). The common order is graded separately, as an ordinal. |
| Charts | P-timing ticks; the peak-time middle half | An ungraded spread, named by its statistic. Timing traces have no corridor (R16). |
| Charts | Wrist score | How closely a style is matched: accuracy. |
| Charts | At address | Set-up positions against the norm. |
| Charts | Usually / median | Location; it now says "median" (R11). |
| Charts | Flight scatter, shot shape | Ball flight (R4). |
| Charts | Relations | Co-movement between readings, not one reading's spread. |
| Charts | Kinds | Groups of shots; spread across kinds is expected. |
| Charts | Findings' confidence | Certainty: `PpStrengthMark`. |
| Charts | Box legends | A key, not a value. |
| Launch monitor | Scatters | Ball flight (R4): the plot stays, no badge. |
| Launch monitor | Notes | Text. |
| Launch monitor | Classifier vote | Detector confidence: never a Needle. |
| Launch monitor | Inferred reads | Provenance, not spread. |
| Launch monitor | Outside | Position against a range (accuracy). |
| Everywhere | Ball flight (R4) | No intent is recorded: no badge on the dispersion, tiles, ledger, appendix ball rows or the Consistency ball line, typicality included. |
| Club delivery | The plane, its spread | Ungraded: no corridor. Its words follow R11 ("Narrowest at … widest at …"). |
| Every panel | Trust gates, data warnings, dropouts, detector confidence, vendor ranges | The Needle's never-list (`variability.md`, Rules); not golfer spread either. |
| Session report | Cover ball tiles | Ball flight (R4). |
| Session report | Numbers page "Typical" | The median: "typical" for a median is left alone (`variability.md`, Phrasing). |
| Session report | Earlier today | A comparison with an earlier session's level. |
| Session report | Trust caveats | Trust gates. |
| Session report | Work-on run chart | The values against the corridor shot by shot; the work-on's Consistency badge sits beside it (R10, R14). |
| Session report | Cause dots | A finding's strength. |

**Rules with no surface yet.**
- **Intent grouping.** No shot records an intent, so there is nothing to group by; meanwhile outcome measures are ungraded (R4).
- **Roster.** No roster or multi-golfer screen exists.
- **Cohort scales.** No cohort norms exist, so the "never mix cohort and universal scales" rule has no column to apply to.
- **Work-on completion gating (R10).** Display only: the badge sits beside the status, and achieved, easing and cleared still follow the firing rule.

**Needle surfaces still without a Needle.** Each is a measurement figure (class Measurement), and none can be graded: no metric sets `measurementTolerance`, so the grader has no measurement basis.
- **PPCP clock-sync σ:** the session toolbar ("sync ±N ms", warns over 5 ms), Settings → Phones ("Clock agreement ± N ms", warns over 5 ms) and the Resource monitor's phone card (warns over 2 ms). The two thresholds disagree: for Mark.
- **Charts' ±σ chips and the sequence table's ±σ:** a reading's propagated σ.
- **Review chart σ** (`PpChartPlot`, `PpChartSummary`): dormant.
- **HackMotion rest angle** (setup).
- **Stereo / triangulation** (setup): a placeholder, with no figure.
- **IMU noise and drift:** no figure exists.

**Gaps the build filled, and interpretations worth keeping.**
- **Data-warning shots are graded.** Home and the report used to skip every shot carrying a data warning (4 July: 15 of 15; 5 Oct: 29 of 31), which left Home with no readings. The ledger's NotAssessable decides instead (work-on rule version 2).
- **One outcome rule, by measure** (`src/Metrics/variability/variability_outcome.h`): ball flight and smash are ungraded everywhere (Home, the report's work-on, the diagnostics Spread box and funnel, the launch monitor). Club delivery is any launch-monitor key that is not an outcome, so spin loft and closure rate are eligible too.
- **Posture and Wrist badges** sit in a line per club under each row ("P4 [badge]"), not under each column: a badge, its word and basis don't fit a P column. Gate-warned and joints-only ⚠ readings are left out (the guarded rule).
- **Wrist grades** use the dominant style's band, on screen too, although the screen shades the picked shot's band. The ranking reads "ranked from P3 on".
- **Launch-monitor badges** sit in a named list under the tiles: too wide for a tile.
- **Span readings** (Consistency): each of the five points is graded, and the reading takes the median point by ρ.
- **The common order** (Charts) counts only swings with every segment placed. Face-on loses the chest, so the badge rarely shows.
- **The matrix's short form** shows the grade's figure, 5 to 1 (0 for Not enough swings), ⚠ for Checking and "-". The old · ◐ ● are gone.
- **Key figures** split the badge's basis onto its own line when the column can't hold it, column-wide.
- **The Launch monitor appendix badge** sits on its own line under each graded row: a ninth column overflows the 674 px page.
- **The report's work-on badge** grades that session only (R14), through the rule Home shares (`src/Gui/variability/variability_ledger.h`).
- **Traces are ungraded** (R16): the Overlay and TypicalityTrace hosts stay in place and show nothing until a trace has a corridor.
- **Diagnostics never sets measurement-limited:** ledger rows store no σ. Setting it would need a ledger schema change (`diagnostic_ledger.h`).
- **Typicality's basis** says "average" (R18), so Replay's "median 72 mph" can sit beside "average 74 mph". Both are right.
- **The Home column** widened from `sp(96)` to `sp(136)`. A plain Active work-on has no NOW line, so its badge shows only on the area row.
- **Tuning values changed** in `gate_catalogue_rules.h` along with the texts: minShots 5 → `variability.nMin` 8, `minClubShots` 3, `cellLoosestPercentile` 0.67. They can be reverted.
- **Retired** (R15): `consistency_summary.{h,cpp}`, the matrix verdicts and cell words, Home's roll-up, `consistency_dump`, `consistency_tally.py`, `consistency_make_goldens.sh` and the Home goldens. `goldens_1005_consistency.json` is frozen, and only its stand-outs, flags and shuffle figures are still read. The `consistency.json` files in the libraries stay, unread.

**Still open for Mark** (collected during the build, not yet ruled):
- The common order needs every segment placed: allow "probable" peaks?
- A Consistency "stood out" row (robust z ≥ 3.5) can read "Your usual" (mean and SD, |z| ≤ 1): two rules, both by design. Should typicality on stand-outs use the robust z?
- The matrix's short form on paper: grade figures on a golfer-layer page?
- `PpChMatrix`'s full-form threshold was sized for "steady": "Not enough swings" and "Repeatable" may overflow at middle widths (a design-system change if they do).
- The report's Posture page is badge-heavy (up to 6 badges a row, on 2 lines); a smaller paper badge would be a design-system change.
- The Launch monitor appendix badge on its own line: OK, or a shorter basis? The longest basis (attack angle) crosses the AVG column's edge.
- `core.json`'s new label ("Sequence changes from swing to swing") changes the pack's content stamp, so every session's `diagnostics.json` regrades once when next opened. The library and corpus sweeps (cabin PC).
- `SESSION_DIAG_MARKS_SOURCES` (`src/Analysis/tests/CMakeLists.txt`) now feeds `regrade_ledger`, `diag_uncertainty_report` and `diag_rank_diff`: the cabin build must pick it up.
- Diagnostics measurement-limited needs a σ in the ledger (a schema change).
- `gate_words`' consistencyVerdict split needs a new gate id.
- Launch-monitor data the build listed as unread (the launch / spin, path / face and strike ellipses) is still published; check its readers before removing it.
- Old consistency words in QML test fixtures (`tst_*.qml`, `src/Gui/tests/qml/data/session_report/*.json`), not run; the frozen `jess.json` fixture keeps "Sequence not repeatable".
- `build/report-build/REPORT_MAP.md` (untracked) should gain `workOns[].consistency` and the Launch monitor appendix rows' `variability`.

## 3. Open items

- **Windows D3D11 is unmeasured.** The cabin PC has only a shipping build, with the probe hook compiled out. Check frame time and clipping when Session 5 puts the marks on real screens.
- **The calibration proposals are not applied** (`calibration/report.md`; every constant still carries `TODO(calibrate)`):
  1. §8.6 noise: don't subtract the propagated σ as noise or use it as the Needle's uncertainty; grade on the observed SD until a measured noise exists.
  2. CV fallback: leave ratio measures without a corridor ungraded until each has a `variabilityScale` (61 of 67 CV rows grade Grooved).
  3. Scalar bands: keep 0.25 / 0.50 / 0.75 / 1.00.
  4. TIMING_SCALE_MS: not yet; first decide which traces have a meaningful peak inside top → impact.
  5. Trace amplitude: give each trace an associated scalar corridor in the descriptor (a model change).
  6. MEAS_TOL_FRACTION: leave at 0.25 until a validation RMSE exists.
  7. Trace typicality: calibrate a separate threshold pair for RMS z(t).
  8. Ordinal partial orders: choose a rule.
  9. N_MIN = 8: keep it.
- **Gallery fixture, superseded.** Three of the six fixture funnels were flat at ρ 0 because the noise subtraction zeroed them. That is superseded: the gallery now reads `gallery_real.json`, graded on the observed SD.
- **The gallery's real funnels use observed SD.** `gallery_real.json` grades Mark's 7-iron on ρ from the observed SD (no noise subtraction, as proposal 1), while the shipped grader still subtracts σnoise. So `previews/funnel_wide.png` and the real rows of the gallery show what proposal 1 would give, not what the grader computes today.
- **Print without `roles` follows the screen theme (known limitation).** On paper without a PpTypeset, the marks, badge and funnel draw with the live screen theme's inks, so a dark screen theme puts dark-theme inks on white paper. The pages say: on paper, always pass `roles`. A printed client report is the golfer layer; only the technical appendix is technical.
- **Print hosting choices await a real printed page.** No report page hosts a mark yet. As built: fills × `variabilityPrintAlphaScale`, never `roles.tintK`; neutral elements map to `roles.baseline` / `roles.grid` at their opacities; the word in `roles.fBody`, the basis in `roles.fFig`; PpTypeset's new `good` ink; on paper without `roles` the marks keep Theme's colours, and only the gaps stop following Density.
- **For Mark** (collected from above):
  - the Session 1 grader gaps, ordinal wording, kinematicSequence trend, ordinal partial orders, trace typicality thresholds;
  - tone fade on delegate reuse; the grade-0 accessible name; the Overlay Cell grade-1 overshoot;
  - the trace corridor; the trace vertical scale; the Icon ring overflow at |z| 2.2; "Checking" on the funnel; TypicalityTrace as an Icon;
  - the ordinal variant; the C++ token helper; ball flight's one badge; the typicality corridor in z;
  - the funnel's last session with no ρ: it is not drawn, but `currentGrade` (tone and word) comes from it, and a break it carried is dropped;
  - the reviewer's contrast note (S4 cold read): at ×3 in dark themes the innermost Needle wedge composites to about 91 % of the tone, so the full-tone needle is nearly lost inside it at grades 1–3; and the funnel band's fill (48 %) sits close to its 55 % edge. The wedge widths still carry the grade.
- **Funnel accessibility, too-few state (open).** The embedded badge's mark is a second accessible Graphic inside the funnel's own, so a screen reader may read the grade twice. A clean fix needs a badge property (a design-system API change), so it is left for Session 5 or Mark.
- **Verification.** Full ctest 281/281 (qml_ui run once), 7 Nov 2026.
- **README line 10** named "the PinPoint Studio design system artifact on claude.ai" as the visual reference. Reworded in Session 5: the design system is the repo folder, and the visual reference is the component pages and their previews (`previews/`).
