# Stage audit: the other panels against the house aesthetic
11 Oct 2026. Measured against RULES.md (the 12 house rules) and BRIEF3.md (the registers: the Replay glance and the Analyse read). This is render and read only: no repo edits and no builds.

## How it was rendered
- Probe: `build/render_stage.qml` (adapted from render_variant3.qml), run by `build/render_stage.sh` through `guard.sh` in slot `base`. It uses the real app and the real data (4 Jul 2026, Wrist session, shot 8) on the 1680 × 1050 offscreen screen.
- For each panel the probe set ViewLayout and grabbed the window and the panel's stage card:
  - full: tabs with all seven panels on and the panel's tab selected through `stage.tabIndex`.
  - half: split with one neighbour.
  - third: split with two neighbours.
  - Timeline and carousel were on in both modes.
- Card sizes were the same in both modes: full 1562 × 553, half 774 × 553, third 511 × 553. With body type at about 14 px, that makes full about 111 em (wide), half 55 em (compact) and third 36 em (narrow), which matches RULES 1. The card is only 553 px, about 39 em, tall in BOTH modes, because the timeline and carousel take the rest. Height is the scarce resource on this stage, more than width.
- Renders are archived at `/mnt/swingdata/scratch/typeset-harness/archive/2026-10-11/renders/audit_*`: 148 PNGs.
  - Analyse: 7 panels × 3 widths × 2 themes (Folio light 12, Vector dark 7), window plus card, 84 files.
  - Replay: camera, 3-D swing, launch monitor, wrist motion and charts × 3 widths × 2 themes, 60 files.
  - 4 crops of the timeline and carousel.
- What could NOT be rendered:
  - Camera video and the 3-D swing draw nothing offscreen. Their content (footage, skeleton, shaft, P marks, the hull figure) is unrendered, so only their chrome, overlays and controls are audited.
  - The launch monitor has no readings in the 4 July session. Only its empty state rendered; its tiles and graphics are audited from source (PpLaunchMonitorPanel / PpLmCard / PpLmGraphicsBody).
  - Markup's frames DO render, because it decodes its own frames.

## Stage-wide findings (they apply to every panel, so fix them once)
1. **The card's tone spends colour on chrome (RULES 7).** PpModeStage `_defs` gives `wristMotion` (and `sessionDiagnostics`) the `warn` tone. Its card title is orange at half and third, and its tab underline is orange at full, on a swing that scores 100/100 with "No faults found". Colour then means "this panel", not "out". All cards should take the quiet tone, and colour should come only from content.
2. **In Vector, orange is the selection colour everywhere.** It appears on the selected segment (Model "Auto", Compare "Address", Tiles/Graphics, the 3-D preset, the Table's "Delivery" and "Full" chips), the playhead and P7 line in Charts, the whole Transit track, the play button, the 0.25× speed, the carousel's picked tile, the active Markup pane border and "acting on the Face-On pane". Orange can never mean "out" on this stage while that holds. RULES 7 already moves Vector links to ink with an underline; the same has to apply to SELECTION. The stage tab strip already shows how: an ink label over a rule, which fits.
3. **Pills and segmented capsules are the default control.** Examples: Wrist Model and Compare, LM Tiles/Graphics, 3-D presets, Table Region and Segment (14 chips) and Resolves-to (5 chips with ×), the transport speeds, the timeline's ten P pills, the carousel's ⊙ 0 badges and the 3-D status chips. The brief asks for hairlines and whitespace instead of boxes. One quiet selection control (the Tabs block: underline, no capsule) would serve all of them.
4. **Collapsible disclosure rows stand in for structure (RULES 6 and 12).**
   - Charts: Controls / Chart / Sequence / Summary.
   - Table: Scope / Coverage / Table.
   - Wrist: "Data · position × degree of freedom".

   These are manual height management: each row costs 30 to 40 px of a 553 px card and asks the reader to manage the page. RULES 12 says spare height should admit the next tier automatically, and a block should keep its heading.
5. **Width is decided in px, never in em (RULES 1).**
   - Wrist: `_wide: width >= Theme.sp(640)`.
   - LM graphics: `kMinWideW: Theme.sp(880)`.
   - Markup: rail `Theme.sp(496)` fixed.
   - Chart summary: `Theme.sp(190)` per card.

   None of these asks `Roles.sizeClass(width)`.
6. **Type is mostly right already (RULES 2).** Every panel except two sets Theme tokens at a fixed size, and nothing grows with the panel the way the old diagnostics `k` did. The exceptions:
   - The LM **tiles** board multiplies one fit factor `k` (1 to 4×) into every font.
   - The camera's telestrator palette (and Markup's frame pair) shrinks its chrome with `scale: fit` at narrow, so "Delete" and "Clear" render smaller at third.

   In Vector, sentence fragments are set in letter-spaced mono caps ("POSE (NO TRAIL IMU) CONFIDENCE", "DATA · POSITION × DEGREE OF FREEDOM", "P DOTS"). Mono caps are for short labels only.
7. **Ellipsis and clipping (RULES 3).**
   - Charts title: "Kinematic seque…" at third.
   - Markup key hint: "…s skeleto…" at full, clipped mid-word at half and third.
   - Markup's DTL HUD clipped to "0.0(" at full.
   - 3-D preset labels overprint at third.
   - LM's caption is designed to elide ("THE CAPTION MUST NOT WRAP").
   - At half and third, the camera's second tile runs past the card edge and is cut.
8. **The answer is below the fold at half and third (RULES 9).** Wrist's Findings, Charts' Summary figures and the Table's data rows all sit under 130 to 330 px of controls. Every Analyse panel puts its controls first and its answer last.

---

## 1 · Charts (`review/PpReplayCharts.qml` → `PpMetricChart.qml`, 1545 lines)
**What it is for:** the focused swing's metric traces against time from impact, plus a metric preset (Kinematic sequence in Analyse, Club & speed / Custom in Replay). It is ON BY DEFAULT in both Replay and Analyse.
- **Replay glance:** "how fast, and when", meaning this shot's two or three key figures (club speed, hand speed, lag at P6) and one trace that shows the shape.
- **Analyse read:** "was the sequence in order, by how much, and where", with the traces as the evidence.

**Breaks:**
- R8 chart grammar breaks at EVERY width:
  - Peak labels collide: at full "Lead arm −107 ms" sits on the arm trace; at half "+58 ms Club −49 ms +69 ms" print on one another.
  - "Chest +20 ms" is printed in a tint inside the Pelvis label.
  - The x-axis unit "ms ← impact" overprints the last tick: "+50" in Analyse and "+200" in Replay, at all three widths.
  - The P labels inside the plot jam ("P5 P6|P7") at third.
  - Ticks do not thin and labels do not move out of the plot; nothing changes form.
- R7 colour:
  - Four identity hues per preset; Pelvis is a dark red that reads as warn.
  - The phase bands alternate with an ACCENT-blue wash, which spends the link colour on a time region.
  - In Vector, the playhead and P markers are orange.
  - "Out of sequence (? = uncertain)" is correctly warn, the one real "out" on the panel, but it competes with everything else.
- R3: the title is ellipsised at third. The title repeats the picker ("Kinematic sequence" twice in Analyse); in Replay the title says "Club & speed" while the picker says "Custom", so the two disagree.
- R12 height: about 130 px of title row, Controls and Chart disclosure before the plot starts. The Summary cards (the Replay figures) are cut to a 75 px sliver at full and are below the fold at half and third.
- R4: the summary cards are a px grid (`sp(190)` each), and each card is a box with a coloured left rule ("Club speed · Partial").
- The legend line repeats units ("372 °/s Δ358 °/s"). A missing value shows "—" ("Club speed —"); the house mark is "-".

**Already fits:**
- The windowed-mean honesty, with raw dots kept faint behind the line.
- A sensible plot height and hairline gridlines.
- Direct labels on peaks (the idea is right; the placement is not).
- The legend's "position, not judged" words.

**Redesign sketch:** voice is FIGURES (C), with A's ledger for the numbers.
- Analyse:
  - The default becomes the existing split mode as SmallMultiples sharing one time axis, one series per row in ink, directly labelled at the line end.
  - The out-of-order peak gets the only warn mark.
  - The sequence becomes one typeset line in the PathChains style: "Pelvis → Chest → Arm → Club · Pelvis late by 20 ms".
  - The Summary becomes a LedgerTable or KeyFigures (peak, Δ, ± as the σ) that folds by tier.
  - Peak labels get collision-aware placement and thin out by em.
- Replay: KeyFigures for the preset's two or three figures above one RunChart-grammar trace. The title is the preset, and there is no disclosure rows.
- Phase bands become hairline P ticks with a neutral wash only on the selected window.

**Effort L** (the label engine and summary live inside a 1545-line chart). **Priority 1:** it is on screen by default in both registers, and its grammar breaks at every width.

## 2 · Wrist motion (`diagnostics/WristDiagnostics.qml`)
**What it is for:** the Tier-1 wrist read for the selected checkpoint: the per-DOF trajectory against the model's band, findings, and working well.
- **Replay glance:** "at this position, is the wrist where it should be?", meaning one score, one sentence and the strip.
- **Analyse read:** "which DOF left its band, at which position, and by how much, against my address, my previous swing or my reference."

**Breaks:**
- R7, the worst on the stage. On a 100/100 swing with "No faults found":
  - The card title is orange and "Findings" is orange.
  - "100/100" is said four ways: a green ✓ disc, a green pill, a green "good" and the score.
  - The strip uses traffic lights: a RED square at P3, an AMBER triangle at P2 and GREEN dots at P4 and P5, inside a GREEN corridor wash.
  - "Working well" is green.
  - The red square contradicts the verdict and nothing explains it.
  - In Vector, the selected Model and Compare segments are solid orange.
- R9 and R12, controls before the answer. Model (4 segments + "→ Neutral"), Compare (3) and "Set as reference" take a row at full and three rows at third (about 140 px). Findings and Working well sit at the bottom: at half the Findings sentence is cut by the card edge, and at third it is below the fold.
- R5 and R11: the controls row never folds to a summary line. At third it wraps capsule by capsule.
- R8: only one DOF strip shows (trail wrist, pose-only). Its plot keeps good direct labels ("↑ held", "↓ scoop", "−2° at P8"), but the RAG marker shapes encode a grade a second time.
- R1: `_wide` is decided at `Theme.sp(640)` px.
- Vector sets "POSE (NO TRAIL IMU) CONFIDENCE" in mono caps.

**Already fits:**
- Direction words instead of a legend.
- The "At P8" card aside.
- Hairline-separated Findings / Working well columns.
- Confidence as ●●○○.
- Fixed Theme type.
- The position name with its note ("Shaft-parallel through · release complete") is a good standfirst.

**Redesign sketch:** voice is LEDGER (A) for findings, with C's figure and B's standfirst.
- Open with a Standfirst: "Shaft-parallel through, release complete. No faults; trail wrist 2° extended at P8."
- One RunChart/ZonePlot per DOF with the house corridor wash (neutral ink); warn only on a position outside its band; the picked position gets the one Mark.
- Findings as LedgerTable `fired` rows (position, DOF, value, out by). Working well folds to an IndexList.
- Model and Compare fold to a one-line state ("Model Auto → Neutral · vs Address") that opens Tabs in place.
- Replay: SparkRow per DOF + the score figure in ink.

**Effort M**, because it can reuse the diagnostics blocks nearly as they are. **Priority 2:** it is the Wrist screen's own analysis, its colour actively misleads, and it shares the corridor grammar the diagnostics redesign just settled.

## 3 · Carousel and Transit timeline (rails; not stage panels, but on screen in every mode)
**What they are for:**
- Carousel: "which shot am I on, which next", plus the swing's actions (Export, Re-analyse, Move to trash) and the replay transport.
- Timeline: "where is the playhead in the swing", with P-position provenance.
- **Glance:** the picked shot and the playhead. **Read:** stepping shots (it is the Brush's `pickedShot` source).

**Carousel breaks:**
- R7: all 15 tiles carry the same red "⊙ 0" pill and the same orange "⚠ Data" warning. Fifteen identical alarms are noise, and "Data" is a data-health note, not an out-of-corridor fault. The swing line repeats the red "⊙ 0" pill.
- Five stars on every tile.
- "Move to trash" is drawn in warn.
- Vector sets "SWING" and "FILTER" in mono caps and uses an orange pick border.
- The speed control is a capsule of four pills.

**Timeline breaks:**
- R12 width-as-resource: about 45% of the track at full is the pre-address recording, so P1 to P10 are crowded into the right 55%.
- Ten bordered P pills, plus a floating "Finish | 3.77s −233°" pill.
- "Impact" and "Finish" are accent-coloured as if they were links.
- In Vector, the whole track is orange.

**Already fits:** the provenance vocabulary (filled = fitted, hollow = sampled) with its key in words; phase names on leader lines; the carousel's pick mark as a single border.

**Sketch:**
- Carousel tiles carry the shot number plus ONE mark, and only when that shot fired something (ShotStrip semantics). Data health is said once on the session line ("data partial on 15 of 15"). The pick and hover use the library's Mark through the shared Brush, so a hovered tile lights the same shot in every stage block (BRIEF3 interaction rule 2). The transport speed uses Tabs.
- Timeline: FigureScale with a drawn, labelled break for the pre-roll; P labels as plain figures on hairline ticks; ink track; phase names as a running small-caps line.

**Effort M. Priority 3:** always on screen, so every bit of noise is paid on every view, and it is where the shared shot brush has to live.

## 4 · Table (`review/PpDataViewer.qml`)
**What it is for:** an inspector of the focused swing.json covering scope, element coverage, and the sample table. It is on by default in Analyse.
- **Analyse read:** "what was measured, how completely, and what are the numbers at the positions that matter."
- **Replay glance:** none; it is not a glance panel and need not be offered there.

**Breaks:**
- R9 and R12: at 553 px the data table is never above the fold. At full only its header row (and a half-cut first row in Vector) shows. 19 chips and a coverage block come first. At half and third the coverage block is cut through its "Ball" row.
- R8 and R3: the coverage strip's phase labels overprint into an illegible run ("|| Ad|dTakeaway…Shaft-parallel Through") at EVERY width, full included.
- R7 colour:
  - Series identity hues: Wrists and Shaft are orange-brown (Vector salmon), next to warn.
  - "low conf" is khaki, Ball is green, and the table heads carry coloured top rules.
  - Vector prints some values in yellow-orange.
  - The current-row band is a brown wash in Vector.
- R4: the chip clouds wrap by px, so at half "P8→P10" orphans onto a third line.
- R11: the "Properties" link and the Scope disclosure stay at every width while the data goes.

**Already fits:** numbers in the data face, right-aligned columns with `conf` beside each value, hairline separators, and the "show gaps / nearest" choice.

**Sketch:** voice is LEDGER (A).
- The table LEADS: a LedgerTable whose rows are the P positions by default (with "every sample" as a tier), whose columns fold by tier into the row's second line, and whose low-confidence cells are greyed rather than coloured.
- Coverage becomes a Barcode (C): one strip per element across time, gap = blank, low-conf = short ink3 tick. Its phase ticks thin out, with labels only where they fit.
- Scope becomes one Tabs line ("Delivery · Full · 5 elements"); the chips open in place.

**Effort M. Priority 4:** it is a default Analyse panel with an every-width grammar failure and a fold failure, but it is a reference tool, not the first read.

## 5 · Camera (`cameras/PpCameraTiles.qml`). Content unrendered (video draws nothing offscreen)
**What it is for:** the loaded shot's streams, with motion overlays and, in Analyse, the telestrator.
- **Replay glance:** the swing itself; chrome should disappear.
- **Analyse:** the same, plus drawing on it.

**Breaks (chrome only):**
- At full the tiles pack left and leave about 45% of the card empty (both modes).
- At half and third the second tile runs past the card edge and is clipped. This is placeholder geometry, so it must be confirmed live.
- The Analyse telestrator palette is open over the top of the footage. At third it is scaled down by `fit`, so the type shrinks (R2) and the palette overhangs the card.
- Its ink swatches include orange and red next to the house warn. They are tool inks, not verdicts, but they sit in the same row as Delete, which is drawn in a warn tint.
- The `cardAside` P legend "P ● fitted ○ sampled" is a good, quiet key.

**Already fits:** the stage frames it and the tiles draw no chrome; the gutter is reserved so the tiles do not reflow when the palette toggles.

**Sketch:**
- Centre the tile row.
- Fit tiles to width AND height (never clip).
- The palette folds to its toggle at narrow and wraps to two rows rather than scaling.
- Nothing else; the content is the voice.

**Effort S. Priority 5** (with the 3-D swing): visible bugs, cheap fixes. Verify on live video.

## 6 · 3-D swing (`swing3d/SwingViz3DView.qml`). Content unrendered (View3D draws nothing offscreen)
**What it is for:** the fitted skeleton, club and ball from any side, posed at the playhead.
- **Glance:** the figure at the playhead from the chosen view. **Read:** orbiting, plus the camera-match presets.

**Breaks (chrome only):**
- R3 and R11: the seven-preset segmented capsule overprints itself at third ("Face-onDown the lineTop Target sideBehindFace-on camDTL cam"), so it is illegible and cannot be hit reliably. At half it is crowded but legible.
- The status reads as two pill chips ("seen by both cameras", "held at P8").
- In Vector, the selected preset is orange.

**Already fits:** it takes the full card height; the status is in words.

**Sketch:**
- Presets as Tabs that wrap to two lines below compact (fold, don't drop). The two camera presets go on their own short line.
- Status as a Notes caption with no pills.

**Effort S. Priority 5.**

## 7 · Launch monitor (`session/PpLaunchMonitorPanel.qml`, PpLmCard, PpLmGraphicsBody). Data unrendered (no readings on 4 Jul)
**What it is for:** every reading the device produced this session, banded, with this shot against the session mean ± SD, in a Tiles board or a Graphics (schematic) view. Its home is often a bay TV.
- **Replay glance:** "what did this shot do": carry, ball speed, launch and spin, read from the mat.
- **Analyse read:** "how repeatable was I, and is anything out of its corridor."

**Rendered (empty state):**
- The Tiles | Graphics capsule (solid orange in Vector) and one quiet line, "No launch monitor readings in this session". It fits, apart from the capsule.
- The panel is off by default, which is right.

**Breaks (from source):**
- R2: the Tiles board multiplies `k` (1 to 4×) into every font and spacing. This is exactly the "type grows with the panel" pattern RULES 2 removes. Its bay-TV case is the cast surface's job: BRIEF3 allows viewing-distance zoom of the WHOLE surface, but not type growing inside a panel.
- R7, three colour channels:
  - band identity hues on titles and lines (Launch teal, Strike purple, …);
  - Watch amber (`colorAttention`) AND Action `colorWarn` as two verdict colours;
  - the house wants one ("out").
- R3: the card caption elides by design.
- R1: graphics reflow at `Theme.sp(880)` × `sp(470)` px.

**Already fits:**
- The Graphics view already went fixed-type, with only the drawing scaling, and stacks cards below a threshold; that is the house rule, arrived at independently.
- "A reading inside its corridor looks like one with no corridor", meaning silence by default.
- Gains are stated in captions.
- ±1 SD as geometry, not colour.

**Sketch:**
- Replay: KeyFigures (4 to 6, ≥ 10 em each) with a BoxStrip under each (the SpreadStrip was retired on 12 Oct 2026 for the box plot, §15.8).
- Analyse: LedgerTable `session` kind (reading · this shot · mean ± SD · strip · corridor), folding by tier.
- Graphics stays as the figure voice.
- One warn for "out", band hue only as a hairline key if at all, and no `k`.

**Effort M. Priority 6:** it is off by default, the 4 July data cannot show it, and it needs an LM session to verify.

## 8 · Markup (`session/PpMarkupPanel.qml`). A tool, Analyse only
**What it is for:** ground-truth labelling of the focused swing (club, ball, P keys) into truth.json and truth_dtl.json. It is not a read or a glance; its question is "what have I marked on this swing, and what do I mark next."

**Breaks:**
- R5, the rail never folds. The right rail is a fixed `Theme.sp(496)` (about 620 px). At half the two frames collapse to roughly 60 px thumbnails ("Fac…"). At third the frames vanish ENTIRELY, the rail's prose is cut mid-word at the card edge, and "0/10 P" and "Saved" are gone. That is a silent drop of the tool's main surface.
- R3:
  - The key hint is ellipsised at full ("… u undo · s skeleto…").
  - The DTL HUD is clipped at full ("0.0(").
  - At third the scrubber disappears and "d ▶" sits on its knob.
- R7: "face-on ball at 0.531, 0.967" is green for a state; "acting on the Face-On pane" is accent (Vector orange); the active pane border is accent (Vector orange).
- Noise: instructions are two grey paragraphs at body size per section, a wall of text for a tool used by one person who knows it.

**Already fits:** the frames render well at full; there is a single mark for the active pane; Positions / Metadata are already tabs with an underline.

**Sketch:**
- Frames lead at every width. Below compact, the rail goes UNDER the frames (RULES 5).
- Instructions become one Notes line per step ("Grip, then head · P key tags · b ball").
- The key legend wraps and never elides.
- State in ink, with the active pane as the only mark.

**Effort S to M. Priority 7:** an internal tool with one user, but the narrow layout is broken outright, so fix the fold even if nothing else changes.

---

## The programme, in order
0. **Foundations (S; land with the diagnostics redesign).**
   - Move Roles into Theme (type roles, size classes in em, ink roles).
   - Make every PpModeStage card tone quiet.
   - Give Vector a SELECTION style that is not orange (ink and underline, like the stage tabs).
   - Provide one quiet selection control (Tabs) for capsules and chip clouds.
   - Retire the panel-level px thresholds in favour of `sizeClass(width)`.

   Every later step depends on these, and steps 1 and 2 of the stage colour story are fixed by them alone.
1. **Charts (L).** On by default in both registers. Its label collisions and axis overprint break chart grammar at every width, and its figures sit below the fold.
2. **Wrist motion (M).** The Wrist screen's own read. Its colour contradicts its verdict (orange title and red mark on a 100/100 swing) and its answer is below the fold; the diagnostics blocks already exist to rebuild it.
3. **Carousel and timeline (M).** On screen in every mode, with 15 identical alarms and ten pills. They are the home of the shared shot brush that the other panels' interaction needs.
4. **Table (M).** A default Analyse panel whose data is never above the fold and whose coverage labels are illegible at every width.
5. **Camera and 3-D chrome (S).** Clipped tiles, a scaled palette and an overprinted preset bar. Cheap to fix, but verify on live video and View3D.
6. **Launch monitor (M).** Remove the tiles' `k`, collapse to one verdict colour, and use KeyFigures / LedgerTable. It is off by default and unverifiable on the 4 July data; render it on an LM session first.
7. **Markup (S to M).** Fold the rail below compact and stop the ellipsis. This is an internal tool, but its third-width layout loses the frames outright.

Renders: 148 PNGs archived at `/mnt/swingdata/scratch/typeset-harness/archive/2026-10-11/renders/audit_*`: 72 window grabs, 72 card grabs and 4 rail crops.
