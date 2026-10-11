> **Historical working document (11 Oct 2026).** `$D` was a session scratchpad, now gone. Its
> mock library (`$D/lib`, `$D/reg`) and data are archived at `/mnt/swingdata/scratch/typeset-harness/archive/2026-10-11/` (`mock_lib_round4/`,
> `mock_reg_round4/`, `data_4jul_shot8/`); the shipped code is in `src/Gui/typeset/` and
> `src/Gui/diagnostics/`. Kept as a worked example of the method in `docs/design/stage_typeset_playbook.md`.

# Port contract: the typeset diagnostics panel moves from the scratchpad into the app

Unattended run (Mark away; approved 11 Oct). NEVER commit. Decide by this contract; record decisions.
Repo: /Users/markliversedge/Projects/PinPointStudio · Scratch: $D (…/scratchpad/diag)
Sources of truth: $D/lib (blocks + Roles + Brush), $D/reg (Replay/Analyse/Print/Panel registers),
$D/build/INVENTORY.md (§5 objectNames, §6 outside users), $D/build/BRIEF4.md (decisions D1–D11),
docs/design/pinpoint_qml_design_system.md §15 (the spec, already written), $D/RULES.md.

## 1. Theme holds the DATA (themes stay data — §14's rule: a new theme is one `_themes` entry)
- Theme.qml gains `_typesetDefault` (= Roles.table._), a `typeset: {…}` block in the `folio` and `vector`
  entries (= Roles.table.folio / .vector), and `typesetPrint` (= Roles.table.print). Expose
  `readonly property var typeset: Object.assign({}, _typesetDefault, _a.typeset || {})` and
  `readonly property var typesetPrint`. The other five aesthetics take the default (unjudged; §15.11).
- NOTHING existing in Theme changes value: themes 0–13 tokens identical (src/Gui/tests/probes/theme_tokens.qml
  dump before/after must match except the new keys); tst_theme_tables must pass.

## 2. The helpers
- src/Gui/typeset/PpTypeset.qml — Roles.qml's computation (medium, u, em, sizeClass, shows/tiers for the
  GENERIC elements: chrome/rail/margin, gutter, columns, measureW, fold helpers, ink roles, mark roles, the
  font roles) reading Theme.typeset / Theme.typesetPrint. Not a singleton: one per register (medium differs).
- src/Gui/diagnostics/PpDiagRoles.qml — `PpTypeset { … }` that ADDS the diagnostics vocabulary: the text and
  sentence helpers (fig, unitOf, corridor*, recWords, trendWords, *Standfirst, firedEntry, sessionEntry,
  headlineHtml …) and the diagnostics tier tables (fired/session/watching/path columns). Registers make a
  PpDiagRoles; other stage panels will later make a PpTypeset (or their own subclass). That split is the
  point of the playbook — keep diagnostics words out of PpTypeset.
- src/Gui/typeset/PpBrush.qml — Brush.qml.

## 3. Blocks: generic → src/Gui/typeset/, diagnostics → src/Gui/diagnostics/
Rule: a block that reads the DIAGNOSTICS MODEL'S SHAPES (condition, card, readout, detail, chain) lives in
src/Gui/diagnostics/; a block that takes plain values (shots, ticks, a corridor, text, pages) lives in
src/Gui/typeset/. Prefix every type `Pp` (names checked collision-free: PpTypeset, PpBrush, PpMark,
PpShotStrip, PpTabs, PpNotes, PpSectionHead, PpPager, PpRunningHead, PpFolio, PpStandfirst, PpMasthead,
PpFigureScale, PpZonePlot, PpSparkline, PpPageKey, PpLedgerTable, PpIndexList, PpKeyFigures, PpEntryFlow,
PpFeatureHead, PpPathChains, PpDotPlot, PpSmallMultiple(s), PpBarcode, PpFigureRow, PpSpreadStrip,
PpRunChart, PpHistoryStrip, PpCauseGraph, PpQuestions, PpDetailBar, plus round-4 additions — grep again
for any new name). Drop `import "../lib"`; the module is flat (one PinPointStudio module, qrc dir-mapped).
Gallery/FigureGallery/LiveStates do NOT ship; they move to src/Gui/tests/probes/ as probe helpers if useful.

## 4. Registers
src/Gui/diagnostics/PpDiagReplay.qml, PpDiagAnalyse.qml, PpDiagPrint.qml (+ their helpers renamed PpDiag*).
PpSessionDiagnosticsBody.qml KEEPS its whole data/state API (every property, function and signal that the
panel, the cast window, PpModeStage, tests and probes use — INVENTORY §0/§2/§4) and the objectName "sdBody";
its OLD VISUAL COMPOSITION is replaced by a Loader choosing the register: surface cast → Replay always (D4);
otherwise SessionMode.mode === analyse → Analyse, else Replay. The register gets `body: root` and binds
view = detailOpen ? "detail" : _tab. Remove the panel fit factor `k` (house rule 2); the cast keeps its
whole-surface viewing-distance zoom in PpSessionDiagnosticsWindow.
objectNames: INVENTORY §5 plus those R recorded in $D/reg/NOTES4.md.

## 5. CMake
cmake/PinPointQmlModule.cmake: add every new .qml to PP_QML_FILES (none is a singleton); remove the old
diagnostics QML that becomes unused — ONLY after `grep -rn "<Type>" src` shows no user outside the old panel
(INVENTORY §6; the Wrist screen still uses some). The PDF writer is already registered.

## 6. Print
PpDiagPrint + PpPdfPrinter (src/Gui/print, done, tested). No UI entry (decided; revisit at the end).
After the build: run $D/build/print_probe.qml (it will need --probe-register-type PpDiagPrint) → a real PDF.

## 7. Builds and tests (HARD)
- Agents do NOT build the app. I (the orchestrator) build: app tree build/Qt_6_11_1_for_macOS_Debug (Ninja,
  `-- -j4`), test tree build/tests (Makefiles, `-- -j4`, target qml_ui_test). ≤ 3 builds total.
- Tests via ctest only: `ctest --test-dir build/tests -R 'qml_ui|pdf_document_writer' --output-on-failure`.
- Tests and probes must be silent (no audio), offscreen.
- No commits. No edits outside the files named here without recording why.

## 8. Card tone (Mark, 11 Oct evening: "Quiet tone please. The previous design system was confused and noisy.")
Every stage card takes the QUIET tone. In src/Gui/session/PpModeStage.qml `_defs`, sessionDiagnostics and
wristMotion move from tone "warn" to "quiet" (the tab rule and card title stop being orange). Any test that
pins the warn tone on those cards is updated to the quiet tone. §13.11 of the design system gives way to §15.
