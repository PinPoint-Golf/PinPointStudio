# C. Charts and data-viz: re-check at HEAD, 31 Oct 2026

**What I checked:** every finding C1–C22 from [C-charts-dataviz.md](C-charts-dataviz.md) (19 Oct, repo `8f5225a8`), and new duplication in the charts / data-viz cluster added since then: the Posture and Club delivery Charts sections, `PpChPlaneLabels`, `PpChShotShapeGrid`, `PpChTrustFold`, `diagnostics/PpWhatChanged.qml`, the session report pages under `src/Gui/report/`, and the C++ formatters in `report/`, `charts/session_posture.cpp`, `charts/session_delivery.cpp` and `charts/charts_posture_presenter.cpp`.

**HEAD:** `9ac19450`. 147 commits since `8f5225a8`, 80 of them touch `src/Gui`.

**Method:** read-only. I read the code, used `grep` and `git log` / `git diff 8f5225a8..HEAD` / `git show 8f5225a8:<file>` to compare then and now, and used the regenerated [data/duplicate-evidence.md](../data/duplicate-evidence.md). I counted the value-to-pixel maps and y-axis gutter labels with the same pattern at both commits, so "then → now" counts are like for like. Where the 19 Oct report under-counted, I say so. Nothing was built, run or probed. Paths are relative to `src/Gui/` unless given in full.

**A correction to the brief.** The regenerated evidence *does* still list identical `byKey` (PpChSessionFigure, PpChTraceFigure), `vy` (PpChReadingTrace, PpChRunBox; and now also PpChPostureProfile, PpChWristProfile), `_hasMean` / `_meanOf` (PpChartPlot, PpChartSummary, PpMetricChart) and `_typeName` (MetricDetail, MetricLibrary, MetricRow) in its section 2. None of these was consolidated or rewritten. The code is unchanged.

**Counts by status:** FIXED 0 · PARTLY 2 · STILL 11 · WORSE 8 · MOOT 1.

## Re-check of C1–C22

| id | old claim (≤10 words) | status | count then → now | where now | note |
|---|---|---|---|---|---|
| C1 | Metric catalogue chain and PpReplayCharts are dead | PARTLY | 6 dead files → 5 | `review/MetricLibrary.qml` (334), `MetricDetail.qml` (567), `MetricRow.qml` (215), `NormativeBar.qml` (200), `trust/PpGateNote.qml` (74). Still listed in `cmake/PinPointQmlModule.cmake:361, 380–383` | `PpReplayCharts.qml` was deleted in 722cf3c2 (30 Oct). Nothing instantiates the other four. Only comments mention them (`review/MetricRoute.qml:24`, `review/chart_metrics.h:465`, `diagnostics/session_diagnostics_model.cpp:237`). `settings/ScreenSettings.qml:99 showMetricDetail` still goes to the Diagnostic Model panel. The `_typeName` ×3 is still inside this chain. |
| C2 | Old review/ stack: production use only in camera tiles | PARTLY | PpMetricChart 1,549 → 1,521; dependants ≈1,364 → 1,077 | `cameras/PpCameraTiles.qml:367` is still the only production mount (`compact: true`). `review/PpChartSummary.qml` (713, was 703), `PpSegmentBrush.qml` (364) and `PpChartPlot.qml` (1,007) are reached only through non-compact mode or probes | 722cf3c2 deleted `PpSequenceStrip.qml` (297) and removed the strip from PpMetricChart. The commit says "the metric chart itself stays" as the short replay graph in Capture. Pinned by `tests/qml/tst_chart_presets.qml`, `tests/probes/metric_colours.qml` and `tools/probes/{instant_cards,plumb_bob_chart,ks_overlay_chart,ks_rising_chart,shaft_lie_card}.qml`. |
| C3 | Measured/mean/nearest predicates copied in four QML files | STILL | 4 files + C++ → same | `review/PpChartPlot.qml:224, 243, 252, 275, 278`; `PpSegmentBrush.qml:98, 105, 121`; `PpMetricChart.qml:577, 594, 603, 640, 643`; `PpChartSummary.qml:135, 138, 170` | No change. The 19 Oct list missed that PpMetricChart also has its own `_measuredAt` (`:603`), so there are two `_measuredAt` copies, not one. |
| C4 | Two curve painters, PpChartPlot (Shape) and PpChPlot (Canvas) | STILL | 2 → 2 | `review/PpChartPlot.qml` (1,007); `charts/PpChPlot.qml` (168) | PpChPlot still has 4 users (KindCurve, ReadingTrace, SessionFigure, TraceFigure). PpChartPlot changed by one line. New: `charts/PpChPlaneLabels.qml:44–45` copies PpChPlot's `xOf` / `yOf` (`PpChPlot.qml:58–59`) to label lines over the same box (see C35). |
| C5 | Fault-forward figure primitives redrawn outside PpZonePlot | STILL | wash 2, break slash 3, stub 6 → same | wash: `typeset/PpZonePlot.qml:33`, `diagnostics/PpSparkline.qml:66–73`. Slashes: `PpZonePlot.qml:96`, `PpSparkline.qml:100`, `PpSmallMultiple.qml:193`. Stub (`stubH`): `PpRunChart.qml:94`, `PpSparkline.qml:111`, `PpSmallMultiple.qml:227`, `charts/PpChRunBox.qml:109`, `typeset/PpShotStrip.qml:99`, `typeset/PpPageKey.qml:106` | None of these files changed except a one-line text change in PpSmallMultiple. |
| C6 | Three box-plot adapters repeat marks/style; key glyph hand-drawn | STILL | 3 adapters → 3; PpChBoxCell users 6 → 12 | `charts/PpChBoxCell.qml:101, 111`; `diagnostics/PpBoxStrip.qml:120, 154, 163–164`; `diagnostics/PpHistoryStrip.qml:129, 139–140`; glyph `typeset/PpPageKey.qml:108–112` | PpBoxPlot still has no `style` property. The one-box-plot rule still holds: every new box (Posture, Delivery, report pages) goes through PpChBoxCell. This is a positive. |
| C7 | Run chart exists twice (PpChRunBox, PpRunChart) | WORSE | 2 (+ sparkline) → 3 (+ sparkline) | `charts/PpChRunBox.qml`, `diagnostics/PpRunChart.qml`, and new `report/PpWorkOnPage.qml:93–170` (`component RunChart`) | See C28. `report/PpWristPage.qml:158` reuses PpChRunBox in print roles, which shows PpChRunBox could also serve the work-on page. |
| C8 | Pasted "Sequence checks" block in PpChSeqTable and PpSequenceStrip | MOOT | 2 → 1 | `charts/PpChSeqTable.qml:108–185` only | PpSequenceStrip was deleted in 722cf3c2. |
| C9 | KeyFigures column-wrap line identical in two files | STILL | 2 → 2 (+2 outside the cluster) | `charts/PpChKeyFigures.qml:39`, `diagnostics/PpKeyFigures.qml:62` | The same line is also at `session/PpLmGraphicsBody.qml:533, 956`. Those copies existed on 19 Oct and were not counted. |
| C10 | ViewFlick ×3, cast-zoom ×2, view preamble ×5 | WORSE | preamble 5 → 8; ViewFlick 3, cast-zoom 2, firePulse 2 unchanged | ViewFlick `charts/PpChartsRegister.qml:107`, `diagnostics/PpDiagReplay.qml:189`, `PpDiagAnalyse.qml:260`. Cast-zoom `PpChartsRegister.qml:105`, `PpDiagReplay.qml:213`. firePulse `PpDiagReplay.qml:142`, `PpDiagAnalyse.qml:168`. Preamble: 5 `PpCharts*View.qml` plus `PpChPostureShotSection.qml:42–48`, `PpChDeliveryShotSection.qml:47–53`, `PpChWristShotSection.qml:45–51` | The evidence shows one 7-line run shared by 8 files. See C24. |
| C11 | Panel shell: `_folder` ×4, `_pointAtSession` ×3, reader's place ×2 | STILL | `_folder` 4, `_pointAtSession` 3 (+`_point`), reader's place 2, carousel lookup 3 → 3 | `charts/PpChartsPanel.qml:95–96, 299–360, 392`; `diagnostics/PpSessionDiagnosticsPanel.qml:153–157, 258–380, 451`; `trust/PpTrustPanel.qml:44–45`; `trust/PpTrustSummaryFold.qml:65, 70`; `session/PpLaunchMonitorPanel.qml:130 _focusShot` (now by shotId) | 785f1c2f made the same edit (`remembersNav`) to both reader's-place copies, which shows the twins are maintained in lockstep. |
| C12 | ~15 value→pixel maps, 5 y-axis gutters in Charts figures | WORSE | guarded affine maps 19 → 27 (+1 unguarded); gutter labels 7 → 8 | New maps: `charts/PpChPostureProfile.qml:130`, `PpChPostureAddress.qml:79`, `PpChPlaneLabels.qml:44–45`, `report/PpReportProfile.qml:108`, `report/PpPosturePage.qml:150`, `report/PpWristPage.qml:445–446`, `report/PpWorkOnPage.qml:118` (`fy`). New gutter: `charts/PpChPostureProfile.qml:162` | Counted with the same `Math.max(1e-9, hi - lo)` pattern at both commits. The 19 Oct "5 gutters" missed the two in PpChSessionFigure and PpChTraceFigure (now `:256`, `:255`). |
| C13 | Gated "words ›" link hand-coded ~15×; PpGateNote unused | STILL | ~15 → ~15 | Same files as 19 Oct: `charts/PpChKeyFigures`, `PpChSummary`, `PpChSeqTable`, `PpChSeqChain`, `PpChProfile`, `PpChTraceFigure`, `PpChWristProfile`, `PpChWristShotSection`, `diagnostics/PpLedgerTable`, `PpFigureRow:236`, `PpDiagReplay:415`, `PpDiagAnalyse:452`, `trust/PpTrustSummaryFold`, `review/PpDataViewer` | The new Posture and Delivery sections added no new hand-coded "›" links. They send all reasons to `PpChTrustFold`. |
| C14 | Ledger tables: spec-driven vs hand-laid columns ×5 | WORSE | hand-laid tables 5 → 9 | New: `charts/PpChPostureLedger.qml` (copy of PpChWristLedger, see C26), `report/PpNumbersTable.qml:99–119`, `report/PpLmAppendix.qml:154–175, 263` (two tables), and the `mv` table in `report/PpWristPage.qml:340–450`. `diagnostics/PpWhatChanged.qml` adds a third column-spec format (`cols: [{k, head, w, r}]`) | See C31 for the report's measure-to-fit column code. |
| C15 | True-minus number formatting ×6 in C++, plus QML twins | WORSE | 6 → 9 base formatters; QML 2 → 4 | `charts/charts_words.h:57 fig`; `diagnostics/session_spread.h:395 spreadNumber`; `diagnostics/session_diagnostics_model.cpp:87 fmtNumber` (still "—" for missing) and `:346 fmtSpread`; `diagnostics/work_ons_controller.cpp:62 fmtNumber`; `review/chart_metrics.cpp:819 formatBare` (still an ASCII hyphen through `QString::number(r, …)`); `charts/charts_relations_presenter.cpp:91 r2`; `charts/charts_wrist_presenter.cpp:1262 r2` lambda; new `report/session_report_model.cpp:164 lmFig`. QML: `typeset/PpTypeset.qml:286 num`, `diagnostics/PpDiagAnalyse.qml:117`, new `charts/PpChDeliveryFlight.qml:44 deg`, new `report/PpWorkOnPage.qml:119 tick` | `fmtSpread` and the wrist `r2` both existed on 19 Oct and were missed. The two `r2` copies differ: the relations copy prints ".00" without a minus, the wrist copy can print "−.00". See C33 for the unit-joining and "+" copies. |
| C16 | Nice-ticks ×3, `shortUnit` ×3 | STILL | 3 / 3 → 3 / 3 | `review/chart_metrics.cpp:626` (cutoffs 1.5/3/7), `charts/charts_relations_presenter.cpp:2430 axisTicks` (≤1.5/≤3/≤7), `diagnostics/session_spread.h:492` (1.5/**3.5**/**7.5**). `shortUnit`: `charts_relations_presenter.cpp:104`, `chart_metrics.cpp:753`, `diagnostics/PpDiagRoles.qml:115` | One good change: `axisTicks` is now static and is the only tick source for the Wrist, Posture and Delivery presenters and the report (`charts_wrist_presenter.cpp:1773`, `session_report_model.cpp:1962`, `PpChPostureProfile.qml:131` via `ticker`). No fourth generator was added. |
| C17 | Word helpers in QML and C++; diagnostics composes/parses prose in QML | WORSE | 21-word list 2 → 3, `cap1` 2 → 3, `list` 3 → 4 | `typeset/PpTypeset.qml:281, 291–295`; `charts/charts_words.h:98–123`; new `report/report_words.h:65 numberWord`, `:73 cap1`, `:87 plural`; inline list join `charts/session_posture.cpp:361`; QML `diagnostics/PpQuestions.qml:69`, `PpDiagPrint.qml:97`; `home/Hm*.qml capitalised` ×3 | `report_words.h` (1,119 lines, new) has its own copy of the number words and `cap1` instead of including `charts_words.h`. It does use `spreadNumber`. `PpDiagRoles.qml` (229 → 248 lines) still regex-parses C++ strings (`fig`/`unitOf` `:113–114`, `recParts` `:116`, `corridorShort/Parts/Clause` `:132–149`) and builds sentences (`:163–222`). |
| C18 | Scrub-to-seek MouseArea ×3, each coupled to `shotReplay` | WORSE | 3 scrub areas → 3, plus 4 new direct seeks | Scrub areas: `charts/PpChReadingTrace.qml:109–110`, `PpChTraceFigure.qml:456–461`, `PpChSessionFigure.qml:322–341`. New: identical `seekTo(us)` at `PpChPostureShotSection.qml:60`, `PpChDeliveryShotSection.qml:68`, `PpChWristShotSection.qml:70`, and an inline seek at `PpChPostureSession.qml:83` | The evidence lists `seekTo` as an identical body in the three ShotSections. |
| C19 | `byKey` ×2, `byId` ×2, review phase/window helpers | WORSE | byKey 2, byId 2 unchanged; +2 new identical helpers in the same pair | `charts/PpChSessionFigure.qml:111`, `PpChTraceFigure.qml:112` (byKey); `typeset/PpTypeset.qml:296`, `diagnostics/PpDiagReplayState.qml:84` (byId). New identical pair: `planeLayersOf` / `withPlane` at `PpChSessionFigure.qml:66–75` and `PpChTraceFigure.qml:73–82` (206f7f55) | `_phaseUs` and the window-clamp pair in review/ are unchanged. |
| C20 | Dash-stroke loop hand-rolled ×3 | STILL | 3 → 3 | `typeset/PpZonePlot.qml:74`, `typeset/PpPageKey.qml:145, 156` | `setLineDash` users went from 2 to 7 in charts (PostureFrame, PostureProfile, WristProfile, KindCurve, TraceFigure, Plot, Scatter), so the two styles both persist. |
| C21 | PpSmallMultiple rebuilds corridor ladder from bands in QML | STILL | 3 places → 3 | `diagnostics/PpSmallMultiple.qml:56–80, 120, 177`; `typeset/PpFigureScale.qml:55–58`; `diagnostics/PpRunChart.qml` dist | No change. By contrast, the new Posture code keeps its z in C++ (`charts/session_posture.cpp PostureBand::z`). |
| C22 | Inline pickers / question blocks: mostly legitimate variants | STILL | same | `charts/PpChChoices.qml` (99), `PpChReadingPicker.qml` (67), `PpChQuestions.qml` (67), `typeset/PpInlinePicker.qml` (73), `diagnostics/PpQuestions.qml` (154) | The new Posture and Wrist sections reuse PpChChoices through the same `sets` shape. This is good. |

## New findings

### C23. The P1–P8 position profile is drawn by three components
- **Verdict:** PARALLEL IMPLEMENTATION (two of them are copies)
- **Where:**
  - `charts/PpChWristProfile.qml` (299 lines).
  - `charts/PpChPostureProfile.qml` (311). Its header says it is "the wrist profile's form (PpChWristProfile)".
  - `report/PpReportProfile.qml` (212), the print version. One good point: the Posture and Wrist report pages already share it.
  - The evidence shows: Jaccard 0.451 / containment 0.636 between the two Charts files; 5 shared runs (longest 12 lines); an identical `vy` (`PpChPostureProfile.qml:130`, `PpChWristProfile.qml:105`); `cx` ×3; `fin` in all three. The y-gutter label (`PpChPostureProfile.qml:162` / `PpChWristProfile.qml:137`) and the position-column heads also repeat.
- **What is duplicated:** The same figure in all three: one row per reading, eight position columns, the band per cell as a wash in the metric colour, fault hairlines, a box per column in Session (PpChBoxCell), the numbers under each column with the outside word and the ⚠ words, and the pole word. Posture adds the second reading (alt). Wrist adds gated-value links and "curves behind".
- **Single home:** `charts/PpChPositionProfile.qml` with optional `alt` and `trustLinks` features, and `mode: shot | session`. PpChWristProfile and PpChPostureProfile become thin wrappers or disappear. PpReportProfile could be the same component in `PpChRoles { medium: "print" }` with per-row pager atoms. That last step depends on C27.
- **Effort:** M (the Charts pair); M to L (folding in the print one). The objectNames `chPostureRow` / `chWristRow` / `reportPostureRow` need a `prefix`, as PpReportProfile already does.
- **Confidence:** high.

### C24. Section shell repeated in the Posture, Wrist and Delivery shot sections (and their Session twins)
- **Verdict:** TRUE DUPLICATE (shell) / LEGITIMATE VARIANT (bodies)
- **Where:**
  - `charts/PpChPostureShotSection.qml` (193), `PpChWristShotSection.qml` (300), `PpChDeliveryShotSection.qml` (249).
  - Identical 8-line preamble (`roles, brush, body, reg, interactive, cast`, `replay`, `ty`) at `:42–50`, `:45–53` and `:47–55`.
  - Identical `seekTo(us)` at `:60`, `:70` and `:68`.
  - The same `has` / `folded` / height formula, and the same `PpChFoldHead { key, title, collapsed, state_, onToggled: body.toggleSection(key) }` at `:74`, `:115` and `:71`.
  - The same `sets` builder, including `a.id === -1 && a.current && …After` (`PpChPostureShotSection.qml:64`, `PpChWristShotSection.qml:101`), and the same `picked(set, opt)` → `body.choose(...)` switch (`:68`, `:107`).
  - `PpChTrustFold` placed under the body in all three.
  - Session twins: `PpChPostureSession.qml` (186), `PpChWristSession.qml` (175), `PpChDeliverySession.qml` (163). They share 20–24 normalised lines pairwise: the head, the pattern line and the trust fold.
- **What is duplicated:** About 30 lines of section chrome per file, which every future Charts section will copy again.
- **Single home:** `charts/PpChSection.qml` (shell: fold head, folded height, controls slot, trust-fold slot, `seek(us)` signal) plus a `PpChSeek` helper that owns the `shotReplay` guard (shared with C18). The `sets` builder could become a `PpChChoices.fromPresenter(choices, map)` helper.
- **Effort:** M. Section objectNames (`chPostureSection`, `chWristSection`, `chDeliverySection`) and `body.collapsed.<key>` must stay.
- **Confidence:** high for the shell, medium for the `sets` helper.

### C25. Picture strip: PpChDeliveryPicture is a copy of PpChPosturePicture
- **Verdict:** TRUE DUPLICATE
- **Where:** `charts/PpChPosturePicture.qml` (100) and `charts/PpChDeliveryPicture.qml` (99). The delivery header calls itself "the twin of PpChPosturePicture". The evidence shows Jaccard 0.619 / containment 0.807, and an identical `rowH(frames)` at `:47` in both. Both place `PpChPostureFrame` per frame. The differences are the title, where `sets` comes from, `coachAlpha` and the objectName prefix.
- **Single home:** one `charts/PpChPictureStrip.qml { title, sets, prefix, coachAlpha }`.
- **Effort:** S.
- **Confidence:** high.

### C26. Findings ledger: PpChPostureLedger is a copy of PpChWristLedger
- **Verdict:** TRUE DUPLICATE with small variants
- **Where:** `charts/PpChPostureLedger.qml` (100) and `charts/PpChWristLedger.qml` (117). The evidence shows Jaccard 0.617 / containment 0.809. A diff shows the same row layout (mark and word, name, positions with a seek, explanation). Wrist adds points, magnitude, confidence pips and a "more" fold. Posture adds a name link (`openCondition`) and a single "Go to P7".
- **Single home:** `charts/PpChFindingsLedger.qml` with optional columns (points, pips, more, nameOpens).
- **Effort:** S to M.
- **Confidence:** high.

### C27. The report re-draws Charts blocks it could reuse in print roles
- **Verdict:** PARALLEL IMPLEMENTATION (print siblings)
- **Where:**
  - "At address": `report/PpPosturePage.qml:116–200` mirrors `charts/PpChPostureAddress.qml` (178). Both have the same label column, band wash, two-lane PpChBoxCell (session / earlier sessions), figures line and ⚠ line. Each has its own `vx` (`PpPosturePage.qml:150`, `PpChPostureAddress.qml:79`).
  - "What moves with the lead wrist": `report/PpWristPage.qml:340–460` mirrors `charts/PpChMovesWith.qml`. The −1…1 interval `px(v)` is byte-identical (`PpWristPage.qml:416`, `PpChMovesWith.qml:99`), and the sparkline-scatter Canvas is copied (`PpWristPage.qml:432–447` against `PpChMovesWith.qml:118–131`). The header (`:41–45`) says why: the screen version draws the swatch through the live theme, and its rows are not pager atoms.
  - The same pages *do* reuse Charts blocks under `PpChRoles { medium: "print"; fixedAesthetic: "folio" }`: PpChBoxCell, PpChScatter (`PpDeliveryPage.qml:201`), PpChRunBox (`PpWristPage.qml:158`), PpChPostureFrame (`PpPosturePage.qml:376`) and PpChShotShapeGrid. So reuse is possible.
- **Single home:** give PpChPostureAddress and PpChMovesWith a print mode: rows as pager atoms (`property bool atom`), and the swatch from `roles.hue`, which a folio print PpChRoles can answer in Folio light. Then the two report blocks become instances.
- **Effort:** M.
- **Confidence:** medium. The pager-atom and fixed-palette constraints are real, but the existing reuse shows they can be met.

### C28. A third run chart in the report work-on page
- **Verdict:** PARALLEL IMPLEMENTATION (extends C7)
- **Where:** `report/PpWorkOnPage.qml:93–170` (`component RunChart`). It has its own `fy` (`:118`, unguarded `span.hi - span.lo`), its own tick formatter with "−" (`:119–124`), and its own swing-label step `[1, 2, 5, 10, 20]` (`:116`) instead of `PpTypeset.shotLabelStep` / `axisLabelShown`. It uses the report's own mark (`PpSwingGrid.SwingMark`) rather than `PpMark` and the `stubH` stub. The target band is drawn as a wash with ticked edges.
- **Single home:** PpChRunBox in print roles, as the Wrist page already does at `PpWristPage.qml:158`. It would need a band and a "target range" label if PpChRunBox lacks them.
- **Effort:** S to M.
- **Confidence:** high.

### C29. The report has its own shot mark and its own roles object
- **Verdict:** PARALLEL IMPLEMENTATION (Mark's call: the report's "Folio light" look is deliberate)
- **Where:**
  - `report/PpSwingGrid.qml:42 component SwingMark` (fired filled dot, clean ring, not measured a stub) is used by PpSwingGrid, `PpWorkOnPage.qml:156` and `PpReportCover.qml:194`. It does the job of `typeset/PpMark` plus the `stubH` stub.
  - `report/PpSessionReport.qml:348–421` builds a hand-written roles object `rr`: inks, about 30 fonts, `hw`, `em`, `hex()`, and a `hue()` that reads Folio light. Beside it the same file builds `PpTypeset { medium: "print"; fixedAesthetic: "folio" }` (`:443`) and a `pagerRoles` shim (`:427`). Each chart page also builds `PpChRoles { medium: "print"; fixedAesthetic: "folio" }` (3 pages).
  - So one printed page holds three roles vocabularies. `rr.hue` and `PpChRoles.hue` (`charts/PpChRoles.qml:54`, `Theme.metricColor`) answer the same question differently. The report pages work around this by passing `color: rw.hue` to each box.
- **Single home:** a `PpReportRoles` that *extends* PpTypeset (print, folio), as PpChRoles and PpDiagRoles do, with `hue()` reading the fixed aesthetic's palette in PpTypeset itself. Then SwingMark can become a PpMark.
- **Effort:** M.
- **Confidence:** medium. The duplication is plain. Whether the report should share the typeset type scale is a design decision.

### C30. Report section-page scaffolding repeated per page
- **Verdict:** TRUE DUPLICATE (small)
- **Where:**
  - The head block (kicker, H1, standfirst; `breakBefore`, `section`): `report/PpPosturePage.qml:79–87`, `PpWristPage.qml:104–112`, `PpDeliveryPage.qml:73–81`, and the same shape in `PpWorkOnPage.qml:174–186`.
  - The "How far can I trust this?" block: `PpPosturePage.qml:416–433` and `PpWristPage.qml:468–485` are identical apart from the id; `PpDeliveryPage.qml:245–300` is a two-column variant.
  - `component Para`: `PpReportAbout.qml:43` and `PpWorkOnPage.qml:72` (the evidence lists both).
  - The tight-squeeze `fitCheck` Timer: `PpWorkOnPage.qml` and `PpOrderPage.qml:39–40`, whose comment reads "as PpWorkOnPage".
  - The PpChRoles "declare last for teardown" block, ×3.
- **Single home:** `report/PpReportSectionHead.qml`, `report/PpReportTrust.qml { lines, more, columns }`, `report/PpReportPara.qml`, and a `squeeze` helper.
- **Effort:** S.
- **Confidence:** high.

### C31. Report tables re-implement "fit columns to the widest word" three times
- **Verdict:** TRUE DUPLICATE
- **Where:**
  - `report/PpNumbersTable.qml:99–119` (`widths`, `xOf`).
  - `report/PpLmAppendix.qml:154–175` (ShotTable `measure`, `widths`, `xOf`) and `:263–270` (StatsTable `widths`).
  - `xOf` is identical in PpLmAppendix and PpNumbersTable (evidence). The rule is the same in each: widest head word, widest unit, widest value, then a shared gap, with a denser font when it does not fit.
  - The local `ShotTable` (`PpLmAppendix.qml:120`) has the same name and job as `session/PpLmGraphicsBody.qml:1637 ShotTable` (the launch monitor's per-club shot table). The layouts differ: equal cells on screen, measured columns on paper.
- **Single home:** a `report/PpFitColumns` QtObject (`cols`, `measure(fonts)`, `gap`, `xOf`). It belongs with C14's column spec. Do not merge the two ShotTables.
- **Effort:** S.
- **Confidence:** high for the column code, low for the ShotTables.

### C32. Shot-shape classification is written twice in C++, with different thresholds and words
- **Verdict:** TRUE DUPLICATE of a business rule, already drifted
- **Where:**
  - `src/LaunchMonitor/lm_inferred_reads.h:62–75` (`LmInferenceTuning`: start 1°, a curve when the **spin axis** is past 2°, hook/slice past 10° axis or 4.5° measured curvature; face-to-path fallback 1° / 4°) and `:225–265`. Names are "Pull–fade" style, used by the launch monitor panel through `launchmonitor/lm_session_model.cpp`.
  - `charts/session_delivery.h:128–132` plus `charts/session_delivery.cpp:689–718 deliveryShotShape`. Start 1°. A curve when the spin axis is past 1.5° (`kDeliverySpinAxisPerFaceToPath 1.5 × kDeliveryShapeCurveDeg 1.0`). "Slight" up to 4.5°. Hook/slice past 9° (1.5 × 6). Names are "Pull, slight fade" style, used by the Club delivery section, PpChShotShapeGrid and the report's Delivery page.
- **What is duplicated:** The same start-and-curve classification from the same monitor fields. From the constants: a shot with a 1.8° spin axis reads as a curve in Club delivery and as straight on the launch monitor card. Between 9° and 10° of axis it is a slice in one and a fade in the other. The two also spell the names differently.
- **Single home:** one classifier in `LaunchMonitor/lm_inferred_reads.h` returning the start and curve indexes plus slight/severe. `session_delivery` maps those to its own words, so the thresholds live in one place.
- **Effort:** M. The thresholds and wording are a product decision, and both are pinned by tests.
- **Confidence:** high on the duplication. The drift is computed from the constants and has not been probed on a real shot.

### C33. C++ unit joining and signed figures: new copies with different rules
- **Verdict:** PARALLEL IMPLEMENTATION (extends C15)
- **Where:**
  - Figure plus unit, 7 copies with 4 rules:
    - `charts/charts_words.h:69 figUnit`: no-break space; closes up °, `%` and `:1`; "% hand rise" collapses to "%".
    - `diagnostics/session_spread.h:409 spreadWithUnit`: plain space; "13 % hand rise".
    - `diagnostics/session_diagnostics_model.cpp:335 withUnit`: never a space.
    - `diagnostics/work_ons_controller.cpp:69 withUnit`: a space only before a letter.
    - New, identical to each other: `charts/session_posture.cpp:102 withUnit` and `charts/charts_posture_presenter.cpp:125 joinUnit`. Both use a no-break space and close up only °.
    - New: `report/report_words.h:286 withUnit`: closes up ° and %, keeps the rest after a space, so "11% hand rise"; ratio becomes ×.
  - The same reading therefore prints as "13 % hand rise" in Session diagnostics, "13% hand rise" in the report and "13%" in Charts.
  - Signed "+" figures, 4 copies: `charts_words.h:169 figSigned`, `session_posture.cpp:640 postureFigure` (new), `report/session_report_model.cpp:174 lmSigned` (new), and `review/chart_metrics.cpp:819 formatBare` (degrees only).
  - Another base formatter: `session_report_model.cpp:164 lmFig` (thousands grouping). It sits beside `session/PpLmGraphicsBody.qml:316 fmtKey`, which takes its decimals from the board's text by regex.
- **Single home:** C15's `figure_format.h`: `fig`, `figSigned`, `withUnit(number, unit, style)` with one rule chosen deliberately, and `figThousands`.
- **Effort:** M. Strings are pinned by the `charts_*`, `session_diagnostics`, `report_model` and `session_posture` tests.
- **Confidence:** high.

### C34. "N ms before impact" is worded twice with different "at impact" windows
- **Verdict:** TRUE DUPLICATE, drifted
- **Where:**
  - `charts/charts_words.h:186 whenImpact`: "at impact" within 2 ms, no-break space.
  - `report/report_words.h:313 timeVsImpact`: "at impact" only when `lround(ms) == 0`.
  - `charts/charts_traces_presenter.cpp:1871–1934` also appends " before impact" / " after impact" by hand in four places.
- **What is duplicated:** An instant 1.6 ms before impact reads "at impact" in Charts and "2 ms before impact" in the report.
- **Single home:** `charts_words.h whenImpact`, called from `report_words.h`. Choose the window deliberately.
- **Effort:** S.
- **Confidence:** high.

### C35. Small helpers copied into the new files
- **Verdict:** TRUE DUPLICATE (small)
- **Where:**
  - QML finite-number test `v !== null && v !== undefined && isFinite(v)`, 10 copies: `charts/PpChPlot.qml:57`, `PpChPostureAddress.qml:55`, `PpChPostureProfile.qml:70`, `PpChWristProfile.qml:174`, `PpChDeliverySession.qml:54`, `PpChWindowBrush.qml:65`, `PpChMovesWith.qml:121`, `report/PpReportProfile.qml:69`, `PpWristPage.qml:86`, `PpPosturePage.qml:72`. There are other forms in `typeset/PpBoxPlot` and `session/PpLmGraphicsBody`.
  - C++ `bool fin(double)` ×7 in `charts/*.cpp`.
  - `planeLayersOf` / `withPlane`, identical in `PpChSessionFigure.qml:66–75` and `PpChTraceFigure.qml:73–82`.
  - `PpChPlaneLabels.qml:44–45` re-declares PpChPlot's map for the same box.
- **Single home:** `PpTypeset.fin(v)`; one C++ `fin` in `charts_words.h` (or `box_stats.h`); `withPlane` folded into the presenter's y-range, or into PpChPlot as a layer-aware range; PpChPlaneLabels taking the PpChPlot's `xOf`/`yOf`, or living inside PpChPlot as a label layer.
- **Effort:** S.
- **Confidence:** high.

### C36. Posture "pattern by shot" strip is hand-built beside PpShotStrip
- **Verdict:** CANDIDATE UNIFICATION (possibly a legitimate variant)
- **Where:** `charts/PpChPostureSession.qml:96–140`: a bar per shot, filled for the session's pattern, a warn-ringed hollow bar for another pattern, a hairline for none, an ink ring for this shot, and hover words. `typeset/PpShotStrip.qml` ("ONE drawing used everywhere") already draws per-shot cells with a pick column, wrapping and a fit to width.
- **Single home:** a PpShotStrip `ticks` state for "differs", if the three states map; otherwise keep it and say why in the header.
- **Effort:** S to M.
- **Confidence:** medium. The bar semantics differ from fired/clean/stub.

### Shared well since 19 Oct (for balance)
- PpChBoxCell went from 6 to 12 users, including the Posture, Delivery and report pages. The one box plot holds.
- `PpChTrustFold` is the one section fold for five Charts uses.
- `PpChShotShapeGrid` has three users (Delivery shot and session sections, report).
- `PpChPostureFrame` serves both picture strips and the report.
- `PpReportProfile` serves the Posture and Wrist report pages.
- `ChartsRelationsPresenter::axisTicks` is now static and is the one tick source for the Wrist, Posture and Delivery presenters and the report.
- `report_posture.h`, `report_wrist.h` and `report_delivery.h` adapt presenter output rather than recompute it.
- PpChChoices is reused by the new sections.

## Not examined
- Bodies of `charts/PpChPostureFrame.qml`, `posture_picture.cpp` and `delivery_picture.cpp` against `report/report_stills*.cpp` (frame placement and crop may overlap).
- Whether `SessionDiagnosticsModel::whatChangedOf` (`session_diagnostics_model.cpp:2759`, posture composites) and `charts/session_posture.cpp` compute the same posture readings twice in C++. They look like different data paths, but I did not check.
- C++ presenter bodies beyond formatters and ticks: `charts_posture_presenter.cpp`, `charts_delivery_presenter.cpp`, `session_report_model.cpp` (2,182 lines), `report_words.h` (1,119 lines, read only for formatters and word helpers).
- `report/PpReportCover.qml`, `PpReportSheet.qml`, `PpReportPrintHost.qml`, `PpSessionReport.qml` pager logic against `typeset/PpPager.qml`, `PpBallTiles.qml`, `PpStill.qml`, `PpDrillCard.qml`, `PpOrderPage.qml` timeline. These were only skimmed for chart overlap.
- The Sessions-screen report files (`PpSessionReportList`, `PpSessionsTableRow`, `PpSessionsSelectionBar`, `PpSessionRowMenu`, `PpSessionBulkJobs`). They are list UI, not data-viz. The evidence's `bytesWords` ×3 and `_plural` ×3 there belong to another cluster.
- `review/PpDataViewer.qml`, `PpTransitTimeline.qml`, `PpCoverageStrip.qml`, `diagnostics/PpCauseGraph.qml` and `PpPathChains.qml`, as on 19 Oct.
- Tests and probes, except as evidence of reachability.
