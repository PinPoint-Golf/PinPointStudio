> **Historical working document (11 Oct 2026).** `$D` was a session scratchpad, now gone. Its
> mock library (`$D/lib`, `$D/reg`) and data are archived at `/mnt/swingdata/scratch/typeset-harness/archive/2026-10-11/` (`mock_lib_round4/`,
> `mock_reg_round4/`, `data_4jul_shot8/`); the shipped code is in `src/Gui/typeset/` and
> `src/Gui/diagnostics/`. Kept as a worked example of the method in `docs/design/stage_typeset_playbook.md`.

# Session diagnostics panel: inventory of the old panel, mapped onto the round-3 registers

Written 11 Oct 2026. This is a reading of the current code; nothing was edited, built or run. It is the contract for the build phase: every behaviour of the old panel is listed with its fate in the new design.

**Shorthand used throughout.** All repo paths are relative to `/Users/markliversedge/Projects/PinPointStudio`. `$D` = `/private/tmp/claude-501/-Users-markliversedge-Projects-PinPointStudio/ef54e254-26d6-4277-927e-847226f29bf8/scratchpad/diag`.

**Old files**
- `B` = `src/Gui/diagnostics/PpSessionDiagnosticsBody.qml`
- `P` = `src/Gui/diagnostics/PpSessionDiagnosticsPanel.qml`
- `W` = `src/Gui/shot/PpSessionDiagnosticsWindow.qml`
- `CD` = `PpConditionDetail.qml`
- `PC` = `PpPatternCard.qml`
- `TS` = `PpThisShotStrip.qml`
- `RS` = `PpReviewShotStrip.qml`
- `WR` = `PpWatchingRow.qml`
- `CS` = `PpCorridorStrip.qml`
- `VR` = `PpValueRun.qml`
- `SH` = `PpSessionHistory.qml`
- `CN` = `PpChainNodeCard.qml`
- `CL` = `PpChainLink.qml`
- `CR` = `PpChainRail.qml`
- `TR` = `PpTickRun.qml`
- `SM` = `PpStrengthMeter.qml`
- `M` = `src/Gui/diagnostics/session_diagnostics_model.{h,cpp}`
- `T` = `src/Gui/tests/qml/tst_session_diagnostics.qml`

The short component names (CD to SM) are all in `src/Gui/diagnostics/`.

**New files**
- `RP` = `$D/reg/Replay.qml`
- `AN` = `$D/reg/Analyse.qml`
- `PN` = `$D/reg/Panel.qml`
- `PR` = `$D/reg/Print.qml`
- `lib/X` = `$D/lib/X.qml`

**Coverage marks:** ✅ handled · ◐ partly handled · ✗ not handled.

---

## 0. Architecture facts the build must respect

These come before the inventory because several gaps follow from them.

1. **`sdBody` is the integration seam, and it must keep its API.** Four things reach into the body:
   - the panel's wiring (`P`);
   - the stage (`PpModeStage`);
   - the cast (`W`);
   - every probe.

   The new registers are designed to sit OVER the body. The try-out (`$D/try_registers.qml`) lays them over it and binds `view = b.detailOpen ? "detail" : b._tab`. The build should make the body host the register, not replace the body.

   What the body must keep:
   - **Properties:** `source`, `readout`, `tab`, `_tab`, `cardFilter`, `watchingExpanded`, `interactive`, `framed`, `cardAside`, `readingShot`, `reviewing`, `detailOpen`, `detailConditionId`, `detail`, `cards`, `header`, `chains`, `pulseCue`.
   - **Functions:** `_openDetail`, `_closeDetail`, `_setDetailMeasure`, `_declareFocus`, `_declareMiss`, `pulseAfterShot`.
   - **Signals:** `screenRequested`, `shotRequested`.
   - **objectName:** `sdBody`.

   Where these are defined and used:
   - Defined in `B:53-293`.
   - Used by `P:243-261` (navigation save and restore), `P:304-308` (Connections), `P:342` (readout), `P:362` (pulse) and `P:412-418`.
   - `cardAside` and `framed` are read by `PpModeStage.qml:113-118`.
2. **Panel navigation save and restore** (`P:210-325`) saves `body.tab`, `body.cardFilter`, `body.watchingExpanded`, the detail id, the detail measure, and the scroll of the Flickables named `sdShotCardsFlick`, `sdCardsFlick`, `sdWatchingFlick` and `sdDetailFlick` (`P:211-212`). The new registers have:
   - one unnamed `Flickable` (`RP:145`, `AN:219`);
   - the session filter as register-local state (`AN:40`, `sessionFilter`).

   So today a mode switch would lose both the filter and the scroll.
3. **The registers use `Loader { active: view === … }`** (`RP:155-157`, `AN:229-232`). The old body swaps views by VISIBILITY and never tears them down (`B:185-190`, `B:1108-1109`), so that BACK restores the scroll, an open path and the hover. With a Loader, every tab switch and every Back rebuilds the view.
4. **`AN:152-165` `applyDemo()` runs on `onReadoutChanged` and `onViewChanged`.** Its last branch (`else flick.contentY = 0`) runs whenever `demoState` is `""`, which is always in the app. So in the live app:
   - every republish (each shot landing, each carousel pick, each regrade, via `P:339-351`) scrolls Analyse to the top;
   - every republish clears the hover (`br.clear()`).

   This breaks `T` test_48 ("a hover in progress survives a republish"), the navigation scroll restore and `detail_hover.qml`. **The demo code must not run when `demoState === ""`.**
5. **`PN:18` chooses the register from `SessionMode.mode` alone.** The cast window (`W:129-136`) hosts its own panel. Under that rule the cast would switch to the Analyse register whenever the stage is in Analyse, but the brief says the cast shares Replay. **Choose the register by `surface === "cast"` first, then by mode.**
6. **Blocks set `interactive: medium === "screen"`** (for example `lib/ShotStrip:21`, `lib/LedgerTable:20`). They never read `body.interactive`. The old body turns off every affordance on the auto-closing cast (`B:96`, `W:84-89`).
7. **No block in `$D/lib` or `$D/reg` sets an objectName.** The only matches in those files are lookups of `sdBody` (`RP:83`, `AN:184`). Section 5 is therefore all new work.
8. **The registers assume 15 shots.** Real sessions run to 30-50 swings: the probes pick ordinals 20 and 30, and the fixtures have 50. The old `TR` narrows its pitch to fit any length ("no width at which one disappears", `TR` header).

   Where the 15 is hard-coded:
   - `lib/LedgerTable:57`, `lib/SparkRow:25` and `reg/ReplaySessionRows:45` (`stripW: 15 * stripPitch`);
   - `lib/Sparkline:89,99` (`/15`);
   - `lib/SmallMultiple:221` (`[1, shot, 15]`) and `:227`;
   - `PR:119` (the text "15 shots");
   - the readout-null fallbacks `shotCount: … : 15` and `shotNo … : 8` (`RP:50`, `AN:43`, `AN:297`, `lib/Roles:342,350`, `lib/LedgerTable:37`, `lib/Barcode:30`, `lib/EntryFlow:33`, `lib/Masthead:15`, `lib/FeatureHead:102`, `reg/ReplayShotRuler:20`).

---

## 1. STATES

The old panel's states come from four inputs:
- the model's `stage` (`B:439,446-449`);
- `header.reviewBadge`, which is non-empty exactly when the model is reviewing (`B:476-477`);
- the readout (`B:488-490`);
- `interactive` (`B:96`).

The new registers branch only on `view`, on `readout` (null or not), on `surface` and on width. They never read `stage`, `quiet`, `afterShotDelta`, `expectations`, `declaredMiss` or `interactive`.

| # | State | Defining property or condition | What the old panel shows | New design | Notes / what is missing |
|---|---|---|---|---|---|
| S1 | No source | `source === null` (`B:52`) | Empty chrome only: frame and tabs. | ◐ | `RP:171-172` says "No shot yet / the next swing reads here". The Session head reads "Zero patterns · counted over **15** shots · shot **0**" from the null fallbacks (§0.8). |
| S2 | Live cold start, no shots | `stage==="cold"`, `header.shotCount===0`, `readout===null` | Session tab: `coldLine` "Too few swings to call a pattern." (`B:1130-1139`), `countLine` "0 patterns · counted over all 0 shots" (`B:1140-1148`), and the miss invitation (`B:468-469`, `B:679-701`). This shot: the empty strip. | ◐ | This shot is fine. Session is a 0-row ledger with no sentence, and the 15/8 fallbacks are wrong here too. |
| S3 | Cold, 1-2 shots | `isCold` (`B:446`) and `header.coldLine` non-empty (`M.cpp:2420-2421`) | `sdColdBody` (`B:1113-1249`) instead of the cards. | ✗ | The registers never read `coldLine`. Session shows an empty ledger and an empty Replay row list. "Say the n is too small" is lost. |
| S4 | Cold with a fault profile (expectations) | `source.expectations.length>0`, Cold only (`M.h:203-205`) | `sdExpectations`: dashed cards with the unconfirmed badge, the "Usually yours · expectations to test, not findings" heading and the "biases ranking and presentation only…" caption (`B:1151-1247`). Omitted ENTIRELY when the list is empty (`B:1151-1153`). | ✗ | Not read anywhere in `$D`. |
| S5 | Forming | `stage==="forming"`, `cards.length>0` | Card grid; NEW tag on fresh cards (`PC:306-316`); filter chips when there are more than 1 card (`B:1301`). | ✅ | `AN:302-311` ledger with an italic "new" (`lib/LedgerTable:131`); `RP:244-258` tiles or sparkline rows. |
| S6 | Established | `stage==="established"`; `header.reachedEstablished` | Same composition as Forming, but the stage word in the aside reads "established" (`B:317-321`, `B:368-373`), and the miss **invitation** stands down (`B:468-469`). | ✅ | The composition is identical. The stage word lives in the body chrome (`cardAside`) and survives if the body keeps it (§5). |
| S7 | After-shot pulse | `body.pulseAfterShot()` called from `P:361-363` on `shotIngested(id, surfaced)` when `surfaced`; sets `pulseCue {token, ids, focusId}` (`B:233-280`) | Changed cards flash an accent wash once, staggered 70 ms in card order (`PC:159-226`). The focused card leads (`B:274-276`). Chain nodes pulse too (`CN:84-86`). Nothing under `reduceMotion` (`PC:221`). | ✗ | The registers have no `pulseCue` and no after-shot cue. BRIEF3 principle 1 ("Nothing moves … until the reader moves") conflicts with design §B3 ("changed cards pulse once"). **Needs Mark's decision.** |
| S8 | Quiet shot (bandwidth cadence) | `source.quiet===true` (`M.cpp:2512-2514`; `surfaced=false`) | This-shot strip swaps to `sdQuietState`, the SAME height: "BANDWIDTH · QUIET" + the delta headline + "the ledger kept accumulating at full rate · cadence gates surfacing, never the engine" (`TS:200-261`). Shot tab note "quiet" (`B:137`). No pulse (`P:361-362`). | ✗ | The registers ignore cadence and always draw every fired condition. Design §B4 calls cadence "likely the most consequential UX decision". |
| S9 | Live, newest swing focused | `readout` non-null, `!reviewing`, `readout.shotIndex===shotCount-1` → `readingShot` false (`B:488-490`) | The after-shot strip (`TS`) from `source.thisShot` + `afterShotDelta`: delta headline "N of M conditions fired · K of them patterns", chips with a meter per fired chip, **ghosted dashed chips for patterns** "FIRED ↓" (`TS:117-195`), an explicit clean chip and a not-assessable chip (`M.cpp:2479-2489`). Under it: "Your patterns that fired here" + the meter legend (`B:865-894`) + the fired cards (`B:907-948`), or `sdFiredEmpty` (`B:896-905`). | ◐ | Shows the readout of the newest shot (dot plot / ledger). Lost: `afterShotDelta.headline`, `newPatterns` ("new pattern" moment), `streakBroken`, `focusHeadline` ("X — fired here"). Fired patterns are no longer drawn as cards under the strip, but the ledger rows carry the same facts. |
| S10 | Live, nothing focused but shots exist | `readout===null` (`P:342-344`; `selectedShotId<0`), `!reviewing`, `!isClosing` → `_shotAvailable` true (`B:119`) | The after-shot strip of the **newest** shot (`focusIndex()` = newest, `M.cpp:1632-1640`). | ✗ | `RP:171` "No shot yet / the next swing reads here" although shots exist. Fix at the panel: when `selectedShotId<0` and live, fetch `shotReadout(newest)`, or the register falls back to `source.thisShot`. |
| S11 | Live, older swing picked | `readingShot` true (`!reviewing && shotIndex!==shotCount-1`) | `PpReviewShotStrip` (`B:821-838`) labelled "SHOT 9 OF 14" in the accent (`RS:172-185`); the header's newest-shot label stands down (`B:315-316`); `sdReviewNote`/`sdReviewHint` foot (`B:953-977`). | ✅ | `RP:163-226`, `AN:251-287`. The new label uses `readout.shotId`, not `shotIndex+1` (§3). |
| S12 | Picking a shot opens This shot (edge) | `onReadingShotChanged: if (readingShot) tab="shot"` (`B:125`) | Opens THIS SHOT once, on the edge; a later re-fetch does not drag the reader back (`T` test_07c). | ✅ | Body logic; it is kept as long as the body is kept. |
| S13 | Closing (live session ended), nothing picked | `stage==="closing"`, `header.closed`, `!reviewing` | Session tab: the bookends band "SESSION BOOKENDS · +N" with clickable bookends (`B:982-1101`); cards kept. This shot tab: `sdShotEmpty` "The session has closed — pick a shot in the carousel to read it here." (`B:854-863`); `_shotAvailable` false (`B:119`). | ◐ | Bookends become `typical/worst/best` ledger columns (tier 4, wide only, else folded; `lib/Roles:112-114`), a FeatureHead fact (`lib/FeatureHead:103-104`) and a SmallMultiple line. This shot shows "No shot yet / the next swing reads here": the wrong tense, because no swing is coming. |
| S14 | Closing + newest picked | `isClosing`, `readout.shotIndex===shotCount-1` | **Old defect:** `readingShot` false and `_shotAvailable` false, so it shows "The session has closed — pick a shot…" with the newest shot picked. | ✅ | The new registers read the readout whatever the stage, so this is fixed. |
| S15 | Reviewing, no shot picked | `reviewing` (`header.reviewBadge!==""`), `readout===null` | Aside: "REVIEWING · shot n of N" (`B:378-385`). Tab rests on SESSION (`T` test_07c). Bookends hold the slot (`T` test_22). This shot: "Pick a shot in the carousel to read it here." (`B:859-860`). | ◐ | `reviewNote` is in the tab note (`RP:127`, `AN:202`). Bookend columns as in S13. This shot says "the next swing reads here", which is wrong in review. |
| S16 | Reviewing with a shot picked | `reviewing && readout` | `RS`: headline "5 of 9 conditions fired · 4 of them patterns" + note "2 measures not assessable" (`RS:164-214`); every condition as a cell (OUT/IN/—, value, corridor-or-REASON, session tier, meter, dashed ring for N/A; `RS:243-406`); silent tail folded behind "18 more · clean all session, not measurable on this swing · SHOW ▸" (`RS:411-467`). Cards in the review tense: "FIRED HERE", "3 more firings after this shot" (`PC:458-470`); selected tick wide and outlined (`TR`). | ◐ | Analyse: the fired ledger + a dot plot of ALL conditions + a not-measurable note with reasons (`AN:263-285`). Replay: the fired dot plot + clean rows as they fit + "N more inside their corridor · M not measurable" (`RP:176-224`). Review tense: `firingsAfterText` on ledger line 2 (`lib/LedgerTable:166`). `reviewNote` is missing from Replay This shot at narrow (the fold to the foot exists only in Session and Watching, `RP:259-264,286-291`). |
| S17 | Review of a session that never established | `header.reachedEstablished===false` while Closing | Flat cards with full tick runs; no rail (`T` test_36). | ✅ | The ledger is always flat. |
| S18 | Detection busy | `source.busy` (`M.h:165-167`) | Nothing (the model keeps the previous state). | ✅ | Equal: neither reads it. |
| S19 | Detail open (Analyse) | `detailOpen = detailConditionId!=="" && detail && detail.header` (`B:217-220`) | `CD` replaces the body (`B:783-801`). Contents: header card; measure tabs; "HOW FAR, SHOT BY SHOT · measure"; corridor strip + hover readout line + value run + legend; across sessions (review); honesty mark; causes and effects headlines; primary rails open, the rest one dashed line each with OPEN ▸; rival; capped note (`CD:132-676`). Esc closes (`CD:92-94`). BACK chip + title in the header (`B:631-670`); miss chip and tabs stand down (`B:683,710`). | ◐ | `AN:367-406`: DetailBar, FeatureHead, KeyFigures (distance to travel), PageKey, FigureRow, Questions, CauseGraph (medium+) or PathChains. Missing: measure tabs, the hover readout line, `header.mark`, Esc, path open/close (PathChains "Open" emits an unwired `pathActivated`; `AN:400-404`), true link grades (§2 F44-F45), the screened-root CTA. |
| S20 | Detail re-targeted from inside | `_openDetail(otherId)` while open (`CD:59-61`; `B:797`) | The page re-targets in place; expansions reset only on a re-target, never on a republish (`CD:101-107`). One step back always. | ◐ | `CauseGraph.conditionActivated` → `openCondition` (`AN:393,398`) works. There is no expansion state to keep. The hover-clear-on-republish bug (§0.4) applies. |
| S21 | Detail open on a non-card condition (ghost, screened root, outcome, watched) | `detail.header.mark`/`kindText` set (`M.h:366-367`) | `sdDetailMark` states what kind of thing it is (`CD:329-342`). | ◐ | FeatureHead falls back to `detail.header` (`AN:39`) but never shows `header.mark`. |
| S22 | Detail open while the stage is in Replay or Capture | `detailOpen` true and `SessionMode.mode!==analyse` (happens after a mode switch: navigation restore re-opens it, `P:257-261`; also the measure-tabs probe opens one in Replay) | Detail page drawn in place, same as Analyse. | ✗ | `RP:37` maps `view==="detail"` to This shot, and the tab row cannot escape it: `pickView` sets `body.tab` (`RP:66`), but `view` stays `"detail"` while `detailOpen`. The reader is stuck on This shot with no Back until they go to Analyse. **Replay's tab press must call `body._closeDetail()`, or Replay must map view from `_tab`.** |
| S23 | Card filter active, and a filter that empties the list | `body.cardFilter` "ball"/"roots" (`B:80-91`) | Rows filtered; ranks NOT renumbered; `sdCardFilterEmpty` explains which claim (`B:1365-1376`). Chips shown only when `interactive` and there are more than 1 card (`B:1301`). | ◐ | `AN:40-42,305-308` (tools on the ledger head). The filter is register-local, so it is lost on rebuild (§0.2). No empty explanation. Replay has no filter. |
| S24 | Watching: empty and non-empty | `source.watching.length` | Tab note = the count (`B:728`); unfolded rows with the dashed badge (`WR:128-183`); "Seen, not yet a pattern · tap one to trace it" (`WR:110`); `sdWatchingEmpty` "Nothing below the pattern gate yet — …" (`B:1459-1466`). | ◐ | `RP:269-293` / `AN:324-364` Barcode sorted by rate. Empty: "Zero watched" over an empty barcode, with no explanation. |
| S25 | Focus declared | `card.focused` / `source.focusConditionId` | Focused card: accent bar (`PC:176-183`), a held accent wash (`PC:159-165`), the tag "FOCUSED ·" (`PC:475-502`). The pulse leads with it (`B:274-276`). Chain node bar (`CN:225`). | ◐ | Only `DetailBar` "Focused · clear" for the OPEN condition (`lib/DetailBar`, `AN:215-217`). Session rows do not show which pattern is the focus (`EntryFlow:139-141` does, but in Print only). |
| S26 | Declared miss: invited / declared / picker open | `_missInvited` (`B:468-469`); `declaredMiss`, `declaredMissName` (`B:466-467`); `missPicker._open` (`B:1533`) | Accent chip "DECLARE MISS ▸" or "MISS · Slice ▸" (`B:679-701`); the popover picker of `missCandidates()` with "WHAT IS THE BAD SHOT?" and "NO DECLARED MISS" (`B:1526-1676`). | ✗ | Not read. A popover also breaks BRIEF3 principle 1 (no pop-ups), so this needs an in-place design. |
| S27 | Auto-closing cast | `W.autoClose` → `interactive:false` (`W:82-89`, `W:132`) | `_tab` forced to "shot" when a shot is available (`B:120-121`). Every tap disabled (tabs `B:769`, chips `B:695-698`, filter `B:1301`, cards `PC:238,497`, measure tabs `CD:223`, nodes `CN:255`, bookends `B:1046`). Pin hidden (`W:143`). | ◐ | `RP:121-144` cast head (no tabs), whole-surface zoom (`RP:112-119`). But the blocks stay clickable (§0.6), and the register follows `SessionMode` (§0.5). A condition click calls `SessionMode.enterAnalyse()` (`RP:63`), which flips the MAIN stage from the cast. |
| S28 | Persistent cast (panel / kiosk) | `W.autoClose===false` → interactive; `remembersNav:false` (`W:135`) | The full interactive layer: the coach works against it (`W:84-89`). Opens on SESSION (default `tab`, `B:111`). | ◐ | `RP:123` hides the tabs on EVERY cast, so a persistent cast can never leave its opening view, apart from the pick edge. `surface` must tell auto-closing from persistent. |
| S29 | Stage-hosted (framed off) | `PpModeStage._unframe` sets `framed=false` (`PpModeStage.qml:116-118,133`) | No card shell or frame head; the stage card heading carries `cardAside` (`B:300-335`). | ✅ | Body-level; kept if the body keeps `framed`/`cardAside`. |
| S30 | Framed (cast) | `framed===true` (default `B:307`) | The body draws the card shell + "SESSION DIAGNOSTICS" (or "SESSION DIAG." when compact) + the aside (`B:331-387`). | ◐ | The Replay cast head duplicates it ("Shot 8 of 15 · club"). Decide which one draws the title. |
| S31 | Compact / narrow / wide | `compact = width < 640·fontScale` (`B:421`); `k` fit 1…2.4 (`B:404-425`) | Type grows with `k`. Compact drops captions, abbreviates the title, puts the tabs on their own row under the chips (`B:627-629,712-715`), makes cards one column, and turns rails vertical (`CD:480,484`). | ✅ | Replaced by size classes (RULES 1-5, 11). Type never grows (RULES 2), so the `k` behaviour is intentionally dropped. |
| S32 | Theme / aesthetic | `Theme.themeIndex` (studio light 4 / dark 5) | Same geometry, different marks (`T` test_12). | ✅ | `Roles` per aesthetic and per light/dark. |
| S33 | Navigation restored after a stage rebuild (same session) | `SessionMode.diagnosticsNav` stamped with the folder (`P:210-265`) | Tab, filter, Watching fold, open detail, detail measure, and per-Flickable scroll are put back. | ◐ | Tab and detail survive (they live on the body and the model). The filter (§0.2), scroll (§0.2, §0.4) and measure (no tabs) do not. |
| S34 | A saved place from another session | `nav.sessionDir !== _folder(diagModel.sessionDir)` (`P:253`) | Not restored. | ✅ | Panel logic, unchanged. |
| S35 | reduceMotion | `Theme.reduceMotion` | No pulse and no stagger (`PC:191-193,221`); colour Behaviors use `Theme.durationFast`. | ✅ | `roles.fast/normal` go to 0. |
| S36 | Shot not measured for a condition | `spread.currentState==="notAssessable"`, `currentNumber "-"` | "-" in grey, never 0 (`PC:571-596`); the cell value "not measurable" + the reason in the corridor slot (`RS:44-48`); a short outlined tick, never a gap (`TR`). | ◐ | "-" survives (`lib/LedgerTable:137`). **But `lib/ShotStrip` draws a BLANK cell for not-measured (`ShotStrip:3-7,48-58`; PageKey "gap: not measurable"), which reverses design principle 1 and `TR`'s "never a gap".** |
| S37 | Review tail folded / unfolded | `RS.tailExpanded` (`RS:78,85`) | One dashed line, SHOW ▸ / HIDE ▾, opening in place (`RS:411-467`). | ◐ | Replaced: Replay shows clean conditions in order of closeness to their edge as far as the height allows, then "N more inside their corridor". Analyse shows all of them in the dot plot. The "clean all session, not measurable here" silence is no longer stated as such. |
| S38 | Zero fired on this shot | `firedCount===0` | The clean chip "clean on every measurable condition" (`M.cpp:2479-2483`); `sdFiredEmpty` "None of this session's patterns fired on this swing." / "No patterns yet — …" (`B:896-905`). | ◐ | Replay: "Inside every corridor" (`RP:171`) ✅. Analyse: "Zero outside the corridor" over an EMPTY "Outside the corridor" table (`AN:259,263-268`). |
| S39 | More patterns than fit | content taller than the viewport | Every card laid out; the area scrolls; "scroll for the rest" (`B:1334-1347`). | ✅ | One Flickable with an AsNeeded ScrollBar; Replay chooses tiles or rows by height (RULES 12). |
| S40 | Session longer than 15 shots | `readout.shotCount > 15` | `TR` narrows its pitch to fit any length. | ✗ | Every strip column is 15 × pitch (§0.8): the strips overflow their column or overlap. |
| S41 | Two live panels (stage + cast) | Each `P` makes its own model; the last one up claims `SessionMode.sessionDiagnostics` (`P:171-192`) | Carousel pips read whichever came last. | ✅ | Panel-level, unchanged. |
| S42 | Hover held while a replay runs and the detail republishes | the shot list updates under the pointer | The hover survives a republish (`CD:101-107`; `T` test_48; `detail_hover.qml`). | ✗ | §0.4: `applyDemo()` clears the brush and scrolls to the top on every `readoutChanged`. |

**Count:** 9 states are not covered (S3, S4, S7, S8, S10, S22, S26, S40, S42). Another 19 are partly covered (S1, S2, S9, S13, S15, S16, S19, S20, S21, S23, S24, S25, S27, S28, S30, S33, S36, S37, S38).

---

## 2. FEATURES and AFFORDANCES

Columns:
- **Old (file:line):** where the feature lives today.
- **New:** where the new design has it, or ✗.
- **Recommendation:** what to do (only where the new design falls short).

### 2a. Chrome, tabs and the review tense

| # | Feature | Old (file:line) | New | Recommendation |
|---|---|---|---|---|
| F1 | Three tabs THIS SHOT / SESSION / WATCHING; every tab always pressable; default `tab:"session"` | `B:111-118`, `B:703-776` (`sdTabBar` 709, `sdTab` 723) | ✅ `lib/Tabs`; `RP:121-131` (no counts), `AN:196-207` (counts) | Keep `body.tab` as the state (it already is: `RP:66`, `AN:80`). Add objectNames (§5). Gate on `body.interactive`. |
| F2 | Tab notes: shot "N fired" / "quiet", session "N patterns" (`header.patternCount`), watching count | `B:127-144`, `B:726-753` | ◐ `AN:203-205` counts fired (readout), cards and watching; Replay: none, by design (each head states its count) | Port "quiet" with the cadence design (F21). `patternCount` versus `cards.length` agree except under a filter, and the new count follows the filter. |
| F3 | `_tab` override: the non-interactive cast shows "shot" | `B:119-121` | ◐ body-level; `RP:132` relies on it | Keep in the body. Make sure `surface` and `interactive` are both passed to the register. |
| F4 | Picking a shot opens This shot, once, on the edge | `B:125` | ✅ body-level | Keep. |
| F5 | Frame (card shell, title "SESSION DIAGNOSTICS" / "SESSION DIAG.") when `framed` | `B:300-353` (`sdTitle` 347) | ◐ the Replay cast head (`RP:133-144`) | Keep the body frame for the cast, or fold the title into the cast head. Pick one (S30). |
| F6 | `cardAside`: newest shot label · stage word · review badge, on the stage card heading | `B:312-327`, `B:355-386` (`sdShotLabel` 360, `sdStageChip` 368, `sdReviewBadge` 379); read by `PpModeStage.qml:113-115,128` | ✗ in the registers (the body still provides it) | Keep it on the body; that is the only place the stage word and "REVIEWING · shot n of N" appear. |
| F7 | Review note under This shot: "final session state · this shot read inside the finished ledger", plus the hint "every condition shown · IN / OUT is this swing against its corridor" | `B:951-977` (`sdReviewNote` 961, `sdReviewHint` 969) | ◐ `reviewNote` in the tab note (`RP:127`, `AN:202`), folded to the foot below medium in Session/Watching (`RP:259-264,286-291`) and in the notes (`AN:284,317`) | Add the Replay This-shot foot fold (it is missing at narrow). Drop the IN/OUT hint: that vocabulary is gone. |
| F8 | `countLine` "N patterns · counted over all N shots" heading the Session tab | `B:1268-1277` (`sdStageNote`) | ◐ the Replay head "counted over N shots" (`RP:237`); Analyse standfirst (`lib/Roles:340-346`) | OK. The wording becomes data-built. |
| F9 | `closingLine` (Closing): "Counts are session totals; this shot is the wide tick…" | `M.cpp:2425-2429`; not drawn by the old QML | ◐ `AN:314` rewrites "wide tick" → "shaded column" | MODEL_DEFECTS #4: fix it in C++, not with a string replace in QML. |

### 2b. This shot

| # | Feature | Old (file:line) | New | Recommendation |
|---|---|---|---|---|
| F10 | After-shot delta headline "N of M conditions fired on this swing · K of them patterns" | `TS:90-105`; `M.cpp:2531-2535` | ◐ the registers compose their own head (`RP:171-174`, `lib/Roles:330-339`) | OK. Same population rule (fired against measurable). |
| F11 | This-shot chips: fired chips with a meter; **pattern chips ghosted, dashed, with "FIRED ↓"** ("its card is where it is read"); a clean chip; a not-assessable chip | `TS:108-196`; `M.cpp:2448-2489` | ◐ the DotPlot "tier" column / the "N of them session patterns" head (`RP:173-174`) | Fold in: the tier column already says pattern or watching. Drop the ghost device: there are no cards under the strip any more. |
| F12 | "Your patterns that fired here" + the fired cards under the strip, with the meter legend `sdChipHint` | `B:865-948` | ◐ the fired ledger rows carry the session facts (`AN:263-268`) | Drop with that reason: the ledger row IS the card. |
| F13 | Review strip: every condition cell (OUT/IN/—, value with a true minus, corridor or REASON, tier word, meter, a dashed ring for N/A) | `RS:243-406` | ◐ Analyse ledger + dot plot + not-measurable note with reasons (`AN:263-285`); Replay dot plot | Keep the "never a blank slot" rule. The not-measurable reasons appear only in the Analyse note; Replay shows only a count, which is acceptable for a glance. |
| F14 | Review headline + note ("2 measures not assessable") | `RS:164-214`; `readout.headline/note` | ◐ head / standfirst / quiet line | OK. |
| F15 | Silent tail fold: SHOW ▸ / HIDE ▾, resets on a new readout | `RS:74-104,411-467` | ✗ (replaced, S37) | Drop with the reason "replaced by closeness ordering + count". Keep `readout.tailSummary` available for Print. |
| F16 | Strength meter: 5-step ladder; hover tooltip `strengthText`; nothing when `strengthKnown` is false | `SM`; used on chips `TS:172-179`, cards `PC:319-329`, cells `RS:330-340`, nodes `CN:316` | ◐ "Out by" ×-excess column (tier 3) (`lib/LedgerTable:121`), `AN:73` key figure; "-" when not known | Fold in. Keep "refuses to guess" ("-" when `!strengthKnown`). Note MODEL_DEFECTS #3 (opposite-side excess). The tooltip is dropped (no floating tooltips). |
| F17 | Shot ruler (how many went out on each shot) | ✗ new | `reg/ReplayShotRuler` | New. |
| F18 | Not-measurable conditions listed with reasons | `RS` cells (corridor slot = reason) | ✅ `AN:280-285` | OK. |
| F19 | Empty This shot: "Pick a shot in the carousel…" (review) / "The session has closed — pick a shot…" (closed) | `B:854-863` (`sdShotEmpty`) | ◐ `RP:171-172` "No shot yet / the next swing reads here" in every case | **Port the three tenses:** live "the next swing reads here"; review "pick a shot in the carousel to read it here"; closed "the session has closed — pick a shot". Analyse has no empty state for This shot at all. |
| F20 | `sdFiredEmpty`: "None of this session's patterns fired…" / "No patterns yet — the strip above is everything this swing said." | `B:896-905` | ◐ Replay "Inside every corridor" | Analyse: replace the empty fired table with one line (S38). |
| F21 | Quiet (bandwidth) strip, same height, with the cadence caption | `TS:200-261`; `B:816` | ✗ | **Needs design.** Cadence is the model's contract (`M.h` decision 5). Recommend: in Replay, a quiet shot shows the head + "quiet: nothing past a pattern's line; the ledger is still counting" in place of the dot plot, the same height; Analyse is never gated (a read is not feedback). |
| F22 | Focus headline "X — fired here / clean here / not measured here" | `M.cpp:2539-2551` (published; not drawn by the old QML either) | ✗ | Optional, with the focus design (F33). |

### 2c. Session

| # | Feature | Old (file:line) | New | Recommendation |
|---|---|---|---|---|
| F23 | Pattern card row, line by line: rank badge (`#1`, `#1=`, none) · name · NEW · meter · verdict pill FIRED/CLEAN/NOT MEASURED (review: FIRED HERE…) | `PC:258-345` | ◐ ledger: rank (no "#"; `lib/LedgerTable:128`), name + phase + "new" (`:129-131`), shot figure in warn (`:137`); pill words dropped (colour of the value carries it) | Keep tie "=" and "no rank → no badge" (`T` test_05d). |
| F24 | Recurrence "4 of 5 measurable shots" (a count, never a %) | `PC:350-359` | ✅ "Fired" frac column (`lib/LedgerTable:134`, `lib/Roles` `frac`) | Check `frac` keeps "of N measurable" wording, never a rate (design principle 5). |
| F25 | Reading + corridor line ("15 % hand rise  pass up to 8.7 · fault at 13 %") | `PC:369-392` | ◐ shot/unit/median columns; corridor text only in FeatureHead and the fired ledger (tier 4) | OK, or add the corridor to the session ledger's fold line. |
| F26 | Direction text (or the withheld-claim sentence) | `PC:399-417` | ✅ line 2 (`lib/LedgerTable:164-167`) | Fix: live `firingsAfterText` is empty, so line 2 reads "Direction  ." — use `recencyText` when `firingsAfterText` is empty (§3). |
| F27 | Tick run with the selected tick wide and outlined | `PC:421-431`, `TR` | ◐ `lib/ShotStrip` (pick column wash) | A gap for not-measured must become a stub (S36). N-shot pitch (§0.8). |
| F28 | Trend + arrow, coloured worsening/improving; easing badge on improving | `PC:443-452`, `PC:247-253` | ◐ "Trend" column (tier 4) word only (`lib/LedgerTable:135`); Questions "is it getting worse?" | OK (RULES 7: colour means "out" only). |
| F29 | Recency "last fired 2 measurable shots ago" (live) / "3 more firings after this shot" (review) | `PC:458-470` | ◐ review tense only (`firingsAfterText`) | Port `recencyText` for live (§3). |
| F30 | Evidence prose (the contingency sentence or the consequence) | `PC:508-523`; `M.cpp:2817,2831-2843` | ✗ (consequence only in FeatureHead) | Port as an Analyse row's line 3 at medium+, or a tier 4 fold. Design §B2: "the per-card evidence line is the contingency sentence". |
| F31 | Two figures: session median + this shot (verdict colour) + unit once; captions drop first | `PC:536-626` | ✅ Median / Shot / Unit columns (`lib/LedgerTable:136-138`) | OK. |
| F32 | The whole card opens the condition detail | `PC:234-242` → `B:938,1411` | ✅ `conditionActivated` → `_openDetail` (`AN:309`); Replay → Analyse (`RP:59-64`) | Keep. |
| F33 | FOCUS ▸ / FOCUSED · tag per card (quiet until hovered; the only focus target), accent bar | `PC:51-74,176-183,475-502` | ◐ `DetailBar` only (`AN:217`) | **Port to the session rows**: a quiet link, shown on hover and when focused (EntryFlow's `showFocus` already does it, `lib/EntryFlow:23,139-141`). Make the focused row visible at rest. |
| F34 | Filter chips ALL / REACHES THE BALL / ROOTS; ranks not renumbered; visible only when interactive and there are more than 1 card | `B:1278-1332` | ◐ ledger tools (`AN:305-308`) | Move the state to `body.cardFilter` (navigation). Hide when `!interactive` or `cards.length<2`. |
| F35 | Empty-filter explanation (ball: "…what the model has been written to claim, not about what these faults cost you.") | `B:1365-1376` | ✗ | Port verbatim as a Notes line under the empty ledger. |
| F36 | "scroll for the rest" tail | `B:1334-1347` | ✗ (ScrollBar AsNeeded) | Drop: the scroll bar says it. |
| F37 | Whole-rows card grid (no partial row) | `B:492-554` | ✗ | Drop: ledger rows are atoms (RULES 6). |
| F38 | Bookends band (closing): one column per pattern, name ▸ opens the detail, "+N" when some don't fit | `B:556-569,982-1101` | ◐ typical/worst/best columns (`lib/Roles:112-114`) put that SHOT in the carousel (`lib/LedgerTable:139-141,324`); FeatureHead "Bookends"; Questions "worst against best" | Behaviour change: a bookend now selects a shot rather than opening the condition. That is better (design §B7: "links seeking the replay"). Make sure the columns fold rather than vanish below wide (they fold, `fo` 4-6). |
| F39 | Coverage line "N of 140 characteristics measurable…", never omitted, on the Session tab | `B:1496-1506`, `PpCoverageLine.qml` | ◐ Analyse Session notes (`AN:316`); Analyse Watching barcode margin at wide (`lib/Barcode:150`); **Replay: nowhere** (`RP:282` turns margin notes off) | Port to Replay Session as one quiet line (it is "the most load-bearing sentence", `PpCoverageLine` header). |
| F40 | Pulse on the changed cards after a shot, focus leads, honours reduceMotion | `B:222-280`, `PC:97-226`, `CN:84-86` | ✗ | Needs Mark's decision (S7). If kept: a one-pass Brush highlight of the newly fired rows (Theme `durationFast`), gated on `surfaced`, staggered in row order, the focus first, with no stagger under reduceMotion. |
| F41 | Expectations (Cold) | `B:1151-1247` | ✗ | Port as a Cold Session block: dashed rows + "expectations to test, not findings" + the profile caption; omit entirely when empty. |
| F42 | Cold line "Too few swings to call a pattern." | `B:1126-1148` | ✗ | Port as the Session head when `stage==="cold"`. |

### 2d. Watching

| # | Feature | Old (file:line) | New | Recommendation |
|---|---|---|---|---|
| F43 | Unfolded watching rows with the dashed badge; a row opens its detail; label "Seen, not yet a pattern · tap one to trace it" | `B:1453-1494`, `WR:86-183` | ✅ Barcode, sorted by rate (`RP:46,279-285`; `AN:37-38,337-341`), "On shot N" values below medium (`AN:343-361`) | OK. The old `WR` item tap ignored `interactive` (an old bug); gate the new rows. |
| F44 | `watchingExpanded` fold (legacy: the Watching tab is never foldable now) | `B:70`, `WR:41-90`; saved in navigation `P:245,256` | n/a | Keep the property on the body for navigation compatibility, or drop it from `P:241-256` and the `diag_nav` probe together. |
| F45 | Empty Watching sentence | `B:1459-1466` | ✗ | Port the sentence verbatim. |

### 2e. Detail

| # | Feature | Old (file:line) | New | Recommendation |
|---|---|---|---|---|
| F46 | BACK chip (accent) + condition name in the header; tabs and the miss chip stand down | `B:631-670` (`sdDetailBack` 644, `sdDetailTitle` 659), `B:683,710` | ✅ `lib/DetailBar` (`AN:208-218`) | Add objectNames. |
| F47 | Esc closes the detail | `CD:89-94` | ✗ | Port (Keys on the register when `view==="detail"`). |
| F48 | Header card = the same pattern card, with focus | `CD:146-158` | ✅ FeatureHead + KeyFigures (`AN:375-380`) | KeyFigures adds the distance to travel (new). |
| F49 | Measure tabs: one per measure with `countText`, the selected one ruled; `measureRequested` → `setDetailMeasure`; the pick survives a rebuild | `CD:171-230`; `B:210-213,799`; `P:247,259-260` | ✗ (FeatureHead lists the measures as text, `lib/FeatureHead:105-108`) | **Port**: a Tabs block (same lib/Tabs) above the FigureRow when `detail.measures.length>1`, wired to `body._setDetailMeasure`. MODEL_DEFECTS #1 is related (wrong measure on early extension). |
| F50 | "HOW FAR, SHOT BY SHOT · measure" label | `CD:231-242` | ✅ FigureRow measure line (`lib/FigureRow:96-100`) | OK. |
| F51 | Corridor strip: bands, norm curve, fault line + label, beeswarm dots, current ringed, recent louder, off-scale chevrons + caption, "← better / worse →" split at the fault line, norm tag with a tooltip (normTagLong + normCitation) | `CS` (`:150-240` direction, `:444-481` pick, `:504-540` caption + tag) | ◐ SpreadStrip (fault-forward) | Lost: the `recent` emphasis, the `clipped` chevrons (an off-scale reading is not marked), the direction words (`dirLeft`/`dirRight`; `highMeans` only in a fact line), the norm citation (it was a tooltip, now banned). **Port the citation as a FeatureHead "Norm" fact or a FigureRow note.** |
| F52 | Value run: one mark per shot, a stub on the baseline for not measured, fired/clean colours, trailing median, Y ticks, wide selected mark, nearest-mark pick → `shotRequested` | `VR` (`:279-319` tap) | ✅ RunChart (fault-forward) inside FigureRow; `shotActivated` → `pickShot` (`AN:386`) | Check the clean colour: RULES 7 makes clean ink, not green (intended). |
| F53 | Linked hover: strip ↔ run share one hovered shot; a readout line between them reads `run[i].readout` and falls back to `currentReadout` | `CD:243-277` | ◐ the Brush links every block (better); the run caption is static `currentReadout` (`lib/FigureRow:91`) | **Port the hover readout**: the caption reads `run[hovered].readout` while `br.hoveredShot` is set. |
| F54 | Run legend "values in X · each mark is a shot … line = median of the last N measured · grey stub = not measured" | `CD:291-304` | ✅ `lib/FigureRow:187` notes | OK. |
| F55 | Across sessions (review only): columns, IQR, median, current highlighted, caption | `CD:306-327`, `SH` | ✅ HistoryStrip in FigureRow | New: `sessionActivated(sessionDir)` exists (`lib/FigureRow:26`) but is unwired in `AN:384-387`. Leave it unwired or design it; do not ship a dead click. |
| F56 | Honesty mark for a non-card condition | `CD:329-342` | ✗ | Port as a FeatureHead kicker line ("ghost · authored as asserted, no measure"). |
| F57 | Causes / effects headlines; noCauses / noEffects lines | `CD:345-382,511-549` | ✅ CauseGraph / PathChains (`lib/CauseGraph:164`) | MODEL_DEFECTS #2 (a driver not on any path). |
| F58 | Primary path drawn in full; other paths collapsed to one dashed line with the marks + OPEN ▸ / CLOSE ▴, opening in place | `CD:384-493,551-654` | ◐ CauseGraph merges every path (medium+); PathChains lists every path, but its "Open" emits `pathActivated`, which **`AN:400-404` never connects** | Wire `pathActivated` (expand path i in place), or remove "Open" on screen. |
| F59 | Chain nodes: live (name, pill, meter, phase·measure, recurrence, tick run, trend, recency, focus, evidence), ghost (dashed, dimmed, no recurrence), screened root (attention, "RUN THE SCREEN ▸"), outcome, watched (a reading, not a ghost) | `CN` | ◐ CauseGraph node marks fired/clean/root/ghost/outcome (`lib/CauseGraph:108-114,148-154`). `nodeKind` tests `state==="fired"` FIRST, so a fired screened root or outcome draws as a plain fired dot. `watched` falls through to its state, so a watched node not measured on THIS shot draws as a ghost, which is the defect `T` test_16b was written against | Per-node recurrence and ticks are gone (acceptable; the graph is a map). Node marks appear as PathChains footnotes but not in CauseGraph. Add a mark key (CauseGraph `keyKinds` covers kinds, not mark text). |
| F60 | **Link grades: five grades, each a distinct stroke; an arrowhead only where the evidence supports a direction (movedTogether, conditionallyDependent); word + note under each link** | `CL`; `M.cpp:273-283,3114-3164` | ✗ CauseGraph has 3 dash styles; **movedTogether / conditionallyDependent fall into the dotted (weakest) style** (`lib/CauseGraph:117`) and **every edge gets an arrowhead** (`:191`). PathChains `linkGlyph` maps moved/dependent/unanchored all to "→" (`lib/PathChains:38`), and the key merges their words | **Must fix before ship:** this is the honesty device of the whole model (design §B2, §4.1). Add the two strong grades (solid, heavier, arrow); no arrow on coherent / presentTogether / unanchored; unanchored at half opacity; PathChains distinct glyphs per grade. |
| F61 | Rival named, explicitly not adjudicated | `CD:495-509` | ✅ `lib/CauseGraph:294-316` | OK. |
| F62 | Capped note "more authored paths than this page draws…" | `CD:656-674` | ✅ `lib/CauseGraph:283-285` | OK. |
| F63 | Focus toggle in the detail (header card + live nodes only) | `CD:157`, `CN:66-70,459-467` | ◐ `DetailBar` focus for the open condition (`AN:217`) | Nodes in CauseGraph: no focus. Acceptable (open the node, then Focus). |
| F64 | `screenRequested` ("Run the screen ▸" on a screened root; the node's tap requests a screen, not a page) | `CN:252-261,511-515`; `CD:63`; `B:287,794`; `P:75-84` (deliberately unconnected upstream) | ✗ CauseGraph opens the screened root's detail instead | Keep the signal on the body. Add a quiet "a physical screen would settle this" line to the screened root's detail, or a CTA beside the node. Do not invent the protocol UI (brief §9). |
| F65 | A detail click on a shot (strip dot / run mark) → `shotRequested` → the carousel's path; stays in Analyse | `CS:79`, `VR:57`; `CD:67,258,289`; `P:386-410` | ✅ `pickShot` → `body.shotRequested(dir || br.dirOf(id), id)` (`AN:81`) | `br.dirOf` needs `cards[].spread.run`; a watched or ghost-only id with no card has no dir. Pass `swingDir` from the block when it has one (the blocks do). |

### 2f. Session intent, the cast and new-only features

| # | Feature | Old (file:line) | New | Recommendation |
|---|---|---|---|---|
| F66 | Declared-miss chip: invites in Cold/Forming, reports at any stage, stands down on the detail; not toned as a finding | `B:451-469,672-701` | ✗ | Port as a quiet link at the right of the Tabs row ("Declare miss" / "Miss: Slice"). |
| F67 | Miss picker: `missCandidates()` rows, current one marked, "No declared miss" clears with `declareMiss("")`; outside click closes | `B:1514-1676` | ✗ | **Needs an in-place design** (no pop-ups, BRIEF3 §1): reveal a row list under the Tabs row in reserved space, or as a Session sub-view. |
| F68 | `missChain` (published, unread) | `M.h:181-183` | ✗ | Unchanged (it was never drawn). |
| F69 | Cast: auto-closing surface, the pin, dwell, viewing-distance scale | `W` | ◐ `RP:106-119` own zoom; the pin and dwell stay in `W` | The window's `contentScale` (`W:109-125`) and the Replay zoom (≤1.6) compound. Measure on a real cast. |
| F70 | Brushing (hover a shot or condition, lit in every block) | ✗ (only strip ↔ run in the detail) | ✅ `lib/Brush` | New. |
| F71 | Sharper questions (≤3, answered in place, "together, not cause"); "step through them" moves the carousel | ✗ | ✅ `AN:89-149`, `lib/Questions` | New. |
| F72 | Replay → Analyse door (a condition click opens the detail, then enters Analyse) | ✗ | ✅ `RP:59-64` | Guard it on the cast (S27). |
| F73 | Print register | ✗ | ✅ `PR` | New; out of scope for the stage panel. |
| F74 | Fault-forward figures, distance to travel | ✗ | ✅ FigureScale, `AN:48-75` | New. |
| F75 | Small multiples / sparkline rows by height | ✗ | ✅ `RP:239-258` | New. |
| F76 | `demoState` | ✗ | test-only | Keep; it must be inert when empty (§0.4). |

---

## 3. DATA

These are fields the old QML reads (or the model publishes for the panel) that the new registers do not. Fields marked ⚠ are meaningful and are silently dropped.

**`source` (the model)**

| Field | Old reader | New | Note |
|---|---|---|---|
| ⚠ `stage` / `header.stage`, `stageLabel` | `B:439,446-449,317` | body chrome only | Cold / Closing behaviour is lost (S3, S13). |
| ⚠ `expectations[] {name, text}` | `B:441,1158-1230` | ✗ | S4. |
| ⚠ `quiet` | `B:137,816` | ✗ | S8, F21. |
| ⚠ `afterShotDelta` `{headline, fired[], focusId, newPatterns, streakBroken, firedPatternCount, note, focusHeadline, focusState, surfaced}` | `TS:99,242`; `B:237-276` | ✗ | The pulse ids, the "new pattern" and "streak broken" moments, and the focus headline. |
| ⚠ `thisShot[] {id, name, kind, tier, focus, strength…}` incl. the explicit clean / not-assessable chips | `B:129,814` | ✗ | S10: the only this-shot data when no readout. |
| ⚠ `declaredMiss`, `declaredMissName`, `missCandidates()`, `declareMiss()` | `B:466-467,172-181` | ✗ | F66-F67. |
| `focusConditionId`, `focusFromShot` | (via `card.focused`) | ✗ | Only `card.focused` matters. |
| ⚠ `driver.screenConditionId`, `driver.screenRef` | `B:573-574,792-793` | ✗ | F64. |
| `chains` | `B:443,259-263` (pulse takeRail) | ✗ | Only the pulse order; drop with the pulse. |
| `bookends[]` | `B:442,1019,1046-1095` | ✅ `lib/LedgerTable:87`, `lib/SmallMultiple:70`, `lib/FeatureHead:103`, `lib/Questions:61` | Published in Closing only (`M.h:201-202`). |
| `coverageLine` | `B:1504` | ◐ Analyse only | F39. |
| `watching[] {id, name, recurrence}` | `B:1484` | ✅ | |
| `detailConditionId`, `detail` | `B:217-219` | ✅ via the body | |
| `unchained`, `unchainedLine`, `missChain`, `header.formingLine`, `header.reviewFootLine`, `header.cadenceNote`, `header.recordedStage*` | not read by the old QML either | n/a | Published but unread by both. |

**`header` (headerInfo)**

| Field | Old reader | New | Note |
|---|---|---|---|
| ⚠ `coldLine` | `B:1133` | ✗ | |
| `countLine` | `B:1143,1273` | Print only (`PR:107`) | The registers rebuild the same sentence. |
| `shotLabel` ("shot N", live newest) | `B:316` | Print only | Body chrome. |
| `reviewBadge` | `B:318,476-477` | ✗ in the registers | Body chrome; also the definition of `reviewing`. |
| `reviewNote` | `B:963` | ✅ | |
| `patternCount` | `B:142` | ◐ `cards.length` | Differs under a filter (by design). |
| `reachedEstablished` | `T` test_36 | n/a | |
| `closingLine` | — | `AN:314` | MODEL_DEFECTS #4. |

**`cards[]` (and `detail.header`, the same shape)**

| Field | Old reader | New | Note |
|---|---|---|---|
| ⚠ `recencyText` (live tense) | `PC:465` | ✗ | Live line 2 renders "Direction  ." (`lib/LedgerTable:166` with empty `firingsAfterText`). FeatureHead (`:102`) and SmallMultiple (`:241`) also read only `firingsAfterText`. |
| ⚠ `evidence` (contingency sentence) | `PC:515` | ✗ | F30. |
| ⚠ `strength` (0-5), `strengthText` (sentence with number) | `PC:325-327` | ✗ | Replaced by `strengthExcess` ×. The sentence version (e.g. "4 of 5 · well outside…") is lost; acceptable if out-by is honest (MODEL_DEFECTS #3). |
| `statePill` (FIRED / CLEAN / NOT MEASURED; review FIRED HERE…) | `PC:341` | ✗ | The value colour carries fired. **Not-measured on this shot must still read "-" (it does) and not look clean.** |
| `trendArrow` | `PC:448` | ✗ | The word stays. |
| ⚠ `focused` | `PC:73` | ◐ detail only | F33. |
| `consequence` | (via evidence) | ◐ FeatureHead only | |
| `spread.currentState` | `PC:571` | ✗ (uses `thisShot`) | Equivalent. |
| `id`, `name`, `rankText`, `fresh`, `phase`, `recurrence`, `directionText`, `ticks`, `trend`, `trendKnown`, `trendText`, `thisShot`, `valueText`, `corridorText`, `reachesBall`, `rootHere`, `spread.{medianNumber, currentNumber, unit, run, bands, faultLines…}`, `firingsAfterText`, `strengthKnown`, `strengthExcess` | | ✅ | |

**`readout` (`shotReadout`)**

| Field | Old reader | New | Note |
|---|---|---|---|
| `headline`, `note` | `RS:192,209` | ✗ | Recomposed. |
| `tailCount`, `tailSummary`, `conditions[].tail` | `RS:99-104,446` | ✗ | S37. |
| `conditions[].state` (OUT/IN/—) | `RS:349` | ✗ | |
| `conditions[].strength*` | `RS:336-338` | ◐ `strengthExcess` | |
| `conditions[].reason` | (corridor slot) | ✅ `AN:283` | |
| ⚠ `shotIndex` for the label | `RS:180` "Shot `shotIndex+1` of N" | ✗ the labels use `shotId` (`RP:138,172`; `AN:258`; `lib/Roles:342-353`) | `shotId` is the model's id (it can be the swing number: `M.h:243-246`). After a deleted swing, "Shot 23 of 20" is possible. Use `shotIndex+1` for "of N" labels; keep `shotId` for brushing. |
| `club`, `contextId` | — | `club` ✅ | |

**`spread` / `history` / `detail`**

| Field | Old reader | New | Note |
|---|---|---|---|
| ⚠ `spread.normTagLong`, `normCitation` | `CS:535-539` (tooltip) | ✗ | F51; provenance of the norm. |
| `spread.dirLeft/dirCentre/dirRight` | `CS:150-240` | ✗ (`highMeans` only) | |
| ⚠ `spread.clipLoCount/HiCount/clipLoText/HiText`, `dots[].clipped` | `CS` caption, chevrons | ✗ | An off-scale reading draws at the axis end, unmarked. |
| `spread.dots[].recent` | `CS` | ✗ | |
| ⚠ `spread.run[].readout` (hover line) | `CD:268` | ✗ | F53. |
| `spread.muF`, `stackMax`, `corridorKnown` | `CS` | ✗ | Internal. |
| ⚠ `detail.measures[] {id, label, countText, fired, read}`, `detail.measureId`, `measurePicked` | `CD:175-230`; `P:247` | ◐ text only | F49. |
| ⚠ `detail.header.mark`, `kindText` | `CD:336` | ✗ | F56. |
| ⚠ `causes/effects[].links[] {word, stroke, width, arrow, opacity, note}` | `CL` | ✗ CauseGraph; ◐ PathChains key (word, note) | F60. |
| `causes/effects[].nodes[] {kind, mark, phase, measure, recurrence, ticks, strength*, firingsAfterText, evidence, focused, statePill, trend}` | `CN` | ◐ kind and state (CauseGraph); mark and evidence (PathChains, path 1 only) | |
| `history.loading`, `missing`, `hiddenEarlier`, `hiddenLater` | not read by the old QML either | n/a | |

---

## 4. TEST MAP

`T` = `src/Gui/tests/qml/tst_session_diagnostics.qml` (53 test functions). The fixture hosts `PpSessionDiagnosticsBody` with `source`/`readout` and spies (`T:37-50,176-185`). **That host can stay**: the rewritten body must still load offscreen with a fixture and route `view` from `tab` / `detailOpen` to the register.

Two more requirements for the rewritten tests:
- Because the register is chosen by `SessionMode.mode`, every rewritten test must set `SessionMode.mode` (Analyse or Replay) or the body's `register` override, and assert both registers where the behaviour applies.
- New names are from §5.

| # | Test (line) | Behaviour pinned | objectNames used | Fate |
|---|---|---|---|---|
| 1 | test_01_coldSaysTheNIsTooSmall (`T:1011`) | Cold draws "Too few swings to call a pattern." + countLine; stage chip drawn | sdColdBody, sdCardsBody, sdColdHeadline, sdColdSubline, sdStageChip | **NEEDS NEW DESIGN** (S3). Then assert `sdColdHeadline` in sdSessionView; `sdStageChip` stays on the body chrome. |
| 2 | test_02_coldDrawsTheExpectationsDashed (`:1022`) | One dashed card per expectation, sized to fit | sdExpectations, sdExpectationCard | **NEEDS NEW DESIGN** (S4). Keep the names. |
| 3 | test_03_coldWithNoProfileOmitsTheBlockEntirely (`:1036`) | Empty profile → no heading at all | sdColdBody, sdColdHeadline, sdExpectations, sdExpectationCard | **NEEDS NEW DESIGN** (S4). The restraint rule must be re-asserted. |
| 4 | test_04_formingDrawsTheCardsAndTheirState (`:1048`) | Forming draws a row per pattern: name, verdict, recurrence, NEW only on the fresh one, withheld-direction sentence, row opens the detail, focus quiet until hover | sdCardsBody, sdColdBody, sdPatternCard, sdCardName, sdStatePill, sdCardRecurrence, sdCardFresh, sdCardDirection, sdCardTap, sdCardFocusTag | **KEEP** → sdSessionView, `sdSessionRow` ×2, sdSessionRowName, sdSessionRowFresh (only on the fresh), sdSessionRowFired "4 of 5", sdSessionRowLine2 contains "dispersion", sdSessionRowShot colour warn / ink. Pill words OBSOLETE. Focus-quiet half needs F33. |
| 5 | test_05_notAssessableIsShortAndOutlined_neverAGap (`:1079`) | One tick per shot; not-assessable shorter, outlined, unfilled, bottom-aligned; fired warn / clean good | sdPatternCard, sdTickRun, sdTick | **NEEDS NEW DESIGN**: `lib/ShotStrip` draws a gap (S36). After the fix: `sdShotCell` count == shots, the N/A cell has a visible stub, not empty. The clean colour becomes ink3 (RULES 7). |
| 6 | test_05b_theMeterSaysHowFarOutAndRefusesToGuess (`:1112`) | 5-step meter ladder, green→red ramp, step 0 ≠ unknown, unknown draws nothing | sdPatternCard, sdCardStrength, sdStrengthBar | **KEEP (narrowed)**: the meter is OBSOLETE; keep "refuses to guess": `sdFiredRowOutBy` reads "-" when `strengthKnown` is false, "N.N×" when known. |
| 7 | test_05c_theVerdictCarriesTheFiringAndOnlyAFiring (`:1180`) | Fired in the warn tone (never the error red); clean says clean; fired without a step is still fired | sdPatternCard, sdStatePill, sdCardBadge, sdCardStrength | **KEEP (narrowed)**: the fired row's value is `roles.warn`, the clean row's is not warn, and no item uses `colorError`. Badge and pill words OBSOLETE. |
| 8 | test_05d_theRankBadgeLeadsTheCard (`:1214`) | Rank "#1"/"#2" leads the name; tie "#1=" on both; no rank → no badge | sdPatternCard, sdCardRank, sdCardName | **KEEP** → sdSessionRowRank "1", "1=", "" (the column is empty but present). |
| 9 | test_06_quietSwapsTheStripWithoutCollapsingIt (`:1253`) | The quiet strip replaces the delta strip at the same height, with "BANDWIDTH · QUIET" + the headline | sdThisShotStrip, sdChipState, sdQuietState, sdChip, sdQuietLabel, sdQuietLine | **NEEDS NEW DESIGN** (S8 / F21). Then sdShotView: `sdQuietState` shown, height unchanged. |
| 10 | test_07_closingReplacesTheStripWithBookends (`:1276`) | Closing: bookends up, no after-shot strip, cards survive | sdBookends, sdThisShotStrip, sdBookend, sdCardsBody | **KEEP (rewritten)**: closing source → session rows carry `sdSessionRowTypical/Worst/Best` (wide) or fold text containing "typical shot"; sdShotView shows `sdShotEmpty` with "closed". |
| 11 | test_07b_theShotAndTheSessionAreTabsNotAStack (`:1292`) | Three tabs; Session rests; the This-shot tab note "2 fired"; pressing This shot swaps the body; the closed session's This shot pressable with the carousel hint | sdTab, sdCardsBody, sdThisShotStrip, sdTabNote, sdWatchingRow, sdPatternCard, sdCardName, sdChipHint, sdShotEmpty | **KEEP** → `sdTab` ×3 (with `key`), `sdTabCount` (Analyse) "2", sdSessionView / sdShotView visibility, `sdShotEmpty` text contains "carousel" (F19). Fired cards under the strip and the chip hint OBSOLETE. |
| 12 | test_07c_pickingAShotOpensItsTabAndTheCastHoldsIt (`:1324`) | The pick edge opens This shot once; the non-interactive cast is on shot | sdReviewStrip, sdThisShotStrip | **KEEP** (body logic) → assert `body._tab` and `sdShotView` shown. The cast half also asserts Replay register + no `sdTab` + blocks non-interactive (§0.5-0.6). |
| 13 | test_07d_aLivePickOfAnOlderSwingIsTheShotRead (`:1349`) | Live pick of an older swing reads it and names "SHOT 9 OF 14"; the header's newest label stands down; the newest pick keeps the after-shot strip | sdReviewStrip, sdThisShotStrip, sdReviewStripLabel, sdShotLabel | **KEEP (narrowed)**: `sdShotHead` contains "9 of 14" (from `shotIndex+1`, §3); `sdShotLabel` hidden. The "newest keeps the after-shot strip" half is OBSOLETE (one shot reading for all picks). |
| 14 | test_08_watchingIsATabOfItsOwn (`:1371`) | Watching is its own tab, counted; rows unfolded; empty stated | sdWatchingRow, sdTabNote, sdTab, sdWatchingItem, sdWatchingLine, sdCardsBody, sdWatchingEmpty | **KEEP** → sdWatchingView, `sdBarcodeRow` ×2, `sdTabCount` "2" (Analyse) / head (Replay); `sdWatchingEmpty` needs F45. |
| 15 | test_09_coverageIsAlwaysStated (`:1393`) | The coverage line is visible on Session at every stage and names "140" | sdCoverageLine | **KEEP** → `sdCoverageLine` in sdSessionView in BOTH registers (Replay currently fails, F39). |
| 16 | test_10_designSizeIsScaleOneAndAWiderStageScalesUp (`:1408`) | `k`=1 at 1168×560, type grows when wider | (body.k, body.tzBody) | **OBSOLETE**: RULES 2 (type fixed; no fit factors). Replace with "type size is equal at 1168 and 2336". |
| 17 | test_11_narrowReducesRatherThanShrinking (`:1423`) | At 396: k=1, compact, title abbreviated, tabs on their own row, one card column, all 9 laid out and reachable, "scroll for the rest" | sdTitle, sdTabBar, sdPatternCard, sdMoreTail, sdCardsFlick | **KEEP (narrowed)**: at 396 all 9 `sdSessionRow` are laid out and the last is reachable via `sdCardsFlick`; no text elided. `sdTitle` abbreviation stays if the body frame stays. k / compact / tab row / sdMoreTail OBSOLETE. |
| 18 | test_11b_theViewportHoldsWholeRowsOfCards (`:1482`) | The grid fits whole rows; rows never squashed below 0.66 | sdCardsFlick, sdPatternCard | **OBSOLETE**: no card grid; rows are atoms (RULES 6). |
| 19 | test_11c_theFilterAsksTwoQuestionsAndRenumbersNothing (`:1531`) | ALL / BALL / ROOTS; the accent shows which; ranks not renumbered; an empty filter explains itself | sdCardFilterChip, sdPatternCard, sdCardName, sdCardRank, sdCardFilterEmpty | **KEEP** → `sdFilterTool` ×3 (`key`, `on`), `body.cardFilter` updated, sdSessionRowRank "1" under the filter, `sdFilterEmpty` (F35). |
| 20 | test_12_bothStudioThemes (`:1592`) | Geometry independent of theme; the fired mark colour differs | sdPatternCard, sdTickRun, sdTick, sdColdBody, sdCoverageLine | **KEEP (adapted)**: per aesthetic, light vs dark: same `sdSessionRow` geometry, different warn. |
| 21 | test_14_theDrillInDrawsEveryPathAndOnlyTheEdgesTheModelAuthored (`:1630`) | No rail on the session; detail rails draw exactly the authored nodes and edges (2,3,4,2 nodes; 7 links) | sdCardsBody, sdChainRail, sdDetailBody, sdDetailCauseRail, sdDetailEffectRail, sdChainNode, sdChainLink | **KEEP (rewritten)**: CauseGraph `sdCauseNode` count == unique node ids across paths; `sdCauseEdge` count == unique authored (from, to) links, and no others. PathChains `sdPathChain` count == paths (4). |
| 22 | test_15_everyLinkGradeGetsItsOwnStroke (`:1655`) | 5 grades → stroke style, colour, width, arrow, opacity, word, note | sdChainLink, sdChainLinkWord, sdChainLinkNote | **NEEDS NEW DESIGN** (F60). Then assert `sdCauseEdge` (`grade`, `strokeStyle`, `hasArrow`, `opacity`) and the PathChains glyph and key per grade. |
| 23 | test_16_ghostScreenedRootAndOutcomeAreEachDrawnAsThemselves (`:1701`) | Node kinds drawn distinctly; ghost dashed and dimmed, no recurrence; screened root has the CTA; outcome named | sdChainNode, sdChainNodeName, sdChainNodePill, sdChainNodePhase, sdChainNodeRecurrence, sdChainNodeRun, sdTick, sdChainGhostFrame, sdChainNodeMark, sdChainScreenCta | **NEEDS NEW DESIGN** (F59, F64): kinds exist (`sdCauseNode.kind`); the screened-root CTA and mark text do not. |
| 24 | test_19_narrowTurnsTheRailAndCollapsesEveryPathButTheFirst (`:1746`) | At 396 the rails go vertical, one path per side open, the rest collapsed, opening in place | sdDetailCauseRail, sdDetailEffectRail, sdChainNodeDot, sdChainLink, sdDetailPathFrame, sdDetailPathToggle, sdDetailBody | **KEEP (rewritten)**: below medium `sdPathChains` replaces `sdCauseGraph` (`AN:374,390-404`); every path listed; "Open" expands in place (F58). |
| 25 | test_21_reviewStatesItsTenseInTheHeader (`:1792`) | REVIEWING badge; review note under the shot; the count heads Session | sdReviewBadge, sdShotLabel, sdReviewNote, sdStageNote | **KEEP** → `sdReviewBadge` (body chrome), `sdReviewNote` (tab note / foot) text, Session head (`sdSessionHead`) contains "14 shots". |
| 26 | test_22_reviewWithNoShotSelectedIsStillTheFinishedSession (`:1812`) | Review without a pick: tense stated, no shot strip, bookends hold; a pick swaps only that slot | sdReviewBadge, sdReviewStrip, sdBookends, sdThisShotStrip | **KEEP (rewritten)**: no readout → sdShotView shows `sdShotEmpty` "pick a shot in the carousel"; session rows carry bookend shots; a pick → sdShotView rows. |
| 27 | test_23_reviewShowsEveryConditionAndNeverABlankSlot (`:1831`) | Every condition drawn; OUT/IN/— counts; N/A value "not measurable" + reason; true minus; tiers; dashed only on N/A; cell width 220 | sdReviewStrip, sdReviewHeadline, sdReviewSubline, sdReviewCell, sdReviewCellMark, sdReviewCellValue, sdReviewCellCorridor, sdReviewCellTier, sdReviewCellDash | **KEEP (rewritten)**: Analyse `sdDotRow` count == measurable set; `sdNotMeasuredNote` names both reasons; a value text contains "−4.4°"; the tier column holds pattern / watching. Mark text, dash ring and 220 px OBSOLETE. |
| 28 | test_34_theStripOrdersByInformationAndFoldsTheSilentTail (`:1895`) | Fired first, then clean, then unread; bounded; the tail folded with its summary | sdReviewStrip, sdReviewCellMark, sdReviewGrid, sdReviewTailSummary, sdReviewTailToggle, sdReviewCell | **OBSOLETE** (tail fold replaced, S37). Keep the "what fired comes first" assertion as part of #27 (Replay `sdDotRow` order: fired, then clean by closeness). |
| 29 | test_35_theTailOpensInPlaceIntoTheSameCells (`:1932`) | SHOW / HIDE opens the tail in place | sdReviewStrip, sdReviewTailRow, sdReviewTailToggle, sdReviewCell | **OBSOLETE** (no tail). |
| 30 | test_36_aFormingSessionKeepsItsCardsWhenItClosesAndIsReviewed (`:1970`) | A Forming-only session reviewed: flat rows with full runs, one selected tick each, review pills | sdCardsBody, sdChainRail, sdPatternCard, sdTick, sdStatePill | **KEEP** → `sdSessionRow` ×2, each `sdShotStrip` with 6 `sdShotCell` and exactly one `picked` (needs N-shot strips, §0.8). Pill OBSOLETE. |
| 31 | test_24_theSelectedShotIsTheWideOutlinedTick (`:2011`) | The selected tick is wider, taller, outlined in colorText, and not recoloured | sdPatternCard, sdTick | **KEEP (rewritten)**: exactly one `sdShotCell.picked`; its bar colour is unchanged (warn); the pick column (`sdPickColumn`) is visible at that x. |
| 32 | test_25_cardsSayWhatHappenedHereAndAfter (`:2037`) | Review tense: "FIRED HERE", "3 more firings after this shot", counts unchanged | sdPatternCard, sdStatePill, sdCardRecency, sdCardRecurrence | **KEEP** → `sdSessionRowLine2` contains "3 more firings after this shot" / "No firings after this shot"; sdSessionRowFired "4 of 5". Pill words OBSOLETE. |
| 33 | test_26_thePipRowIsTheSwingsWholeReadAtCarouselSize (`:2056`) | The carousel `PpShotPipRow`: 9 pips, N/A shorter and outlined, the count colour | sdPip, sdPipCount | **KEEP unchanged**: outside the panel (`src/Gui/shot/PpShotPipRow.qml`). |
| 34 | test_16b_aMeasuredConditionBelowTheGateIsDrawnAsAReading (`:2097`) | A watched node is a reading, not a ghost (count, run, pill, meter; not dashed; mark "measured · not yet a pattern") | sdChainNode, sdChainNodeRecurrence, sdChainNodeRun, sdTick, sdChainNodePill, sdChainNodeStrength, sdChainGhostFrame, sdChainNodeMark | **KEEP (narrowed)**: `sdCauseNode.kind` for `watched` is not "ghost" (`lib/CauseGraph:108-114`; today it IS "ghost" when the node's `state` is notAssessable, so fix `nodeKind` first); the PathChains footnote carries "measured · not yet a pattern". Per-node count, run and meter OBSOLETE. |
| 35 | test_27_aPatternCardOffersFocusAndSaysWhichStateItIsIn (`:2144`) | FOCUS ▸ vs FOCUSED ·; accent bar only on the focused card; read back from the model | sdPatternCard, sdCardFocusTag, sdCardFocusBar | **NEEDS NEW DESIGN** (F33): `sdSessionRowFocus` text and a focused-row mark. |
| 36 | test_28_tappingAPatternCardsFocusChipAsksTheModelToDeclareOrClearFocus (`:2173`) | The focus tap calls `declareFocus(id)` or `clearFocus()` by model state; the panel waits for the model | sdPatternCard, sdCardFocusTag | **KEEP** → `sdDetailFocus` in DetailBar: unfocused → `focusCalls` 1 with the id; focused → `clearFocusCalls` 1; `detailCard.focused` unchanged until the source changes. Extend to `sdSessionRowFocus` once F33 lands. |
| 37 | test_29_onlyLiveChainNodesOfferTheFocusContract (`:2202`) | Live nodes offer focus; ghost and outcome don't; a screened-root tap requests the screen | sdDetailBody, sdChainNodeFocusTag, sdChainNode | **NEEDS NEW DESIGN** (F63-F64). At minimum assert a screened-root `sdCauseNode` click does NOT open a page, or emits `screenRequested`. |
| 38 | test_30_theMissChipInvitesInColdAndFormingAndReportsOnceDeclared (`:2235`) | Invites in Cold/Forming; stands down from Established; a declared miss is always reported by name | sdMissChip | **NEEDS NEW DESIGN** (F66). |
| 39 | test_31_theMissPickerListsTheModelsCandidatesAndDeclaresOne (`:2263`) | The picker lists `missCandidates`, declares by id, closes; clear = `declareMiss("")` | sdMissChip, sdMissPicker, sdMissCandidate, sdMissPickerLabel, sdMissClear | **NEEDS NEW DESIGN** (F67, in place). |
| 40 | test_32_thePulseRunsOnWhatFiredAndLeadsWithTheFocusedNode (`:2301`) | `pulseAfterShot()` publishes a cue; the focus leads; only fired rows pulse | sdPatternCard (`pulsing`) | **NEEDS NEW DESIGN** (F40; Mark's decision). If kept: `sdSessionRow.pulsing`. If dropped: OBSOLETE, with the BRIEF3 §1 reason. |
| 41 | test_33_thePulseHonoursReduceMotion (`:2332`) | No pulse and no deferred stagger under reduceMotion | sdPatternCard (`pulsing`, `_pulseT`) | **NEEDS NEW DESIGN** (with #40). |
| 42 | test_38_theCardOpensTheConditionAndTheChipStillOnlyDeclaresFocus (`:2363`) | A row click opens the detail and not focus; the focus chip only focuses; a node opens; a screened root asks for its screen; a watching row opens; a bookend opens its condition | sdPatternCard, sdCardFocusTag, sdDetailBody, sdChainNodeFocusTag, sdWatchingItem, sdBookend | **KEEP (rewritten)**: `sdSessionRow` click → `detailCalls` 1; `sdCauseNode` click → re-target; `sdBarcodeRow` click → `lastDetailId` "sway"; Replay `sdDotRow` click → `_openDetail` + enterAnalyse; a bookend shot cell → `lastShotId` (behaviour change, F38). Screened root: see #37. |
| 43 | test_39_theDetailDrawsCausesEffectsAndBothHeadlines (`:2433`) | The detail replaces the body; Back + title; the miss chip down; the header card; both headlines; one rail per side + collapsed lines with marks; link grades; outcome; rival; fits the panel; a collapsed path opens in place | sdDetailBody, sdCardsBody, sdCoverageLine, sdTitle, sdDetailBack, sdDetailTitle, sdMissChip, sdDetailHeaderCard, sdCardName, sdCardRecurrence, sdTickRun, sdTick, sdDetailCauseHeadline, sdDetailOutcomeHeadline, sdDetailCauseRail, sdDetailEffectRail, sdChainRail, sdDetailPathFrame, sdDetailPathToggle, sdDetailPathSummary, sdDetailPathNote, sdChainLink, sdChainLinkNote, sdChainNode, sdChainNodeRecurrence, sdDetailRival | **KEEP (rewritten)** → sdDetailBody shown, sdSessionView hidden, `sdDetailBack`, `sdDetailTitle` "Casting", `sdFeatureHead`, `sdDetailCauseHeadline` / `sdDetailOutcomeHeadline` exact text, `sdDetailRival` contains "not adjudicated", nothing overhangs. Link-grade parts → #22; path open → #24. |
| 44 | test_40_backRestoresTheCompositionUntouched (`:2512`) | BACK calls `closeDetail`; the composition returns exactly as left, including panel-local state | sdDetailBody, sdDetailBack (+ the compositionSnapshot names) | **KEEP (rewritten)**: Back → `closeDetailCalls` 1; after close, the same `body.tab`, `body.cardFilter`, and `sdCardsFlick.contentY` as before (fails today: §0.3 Loader, §0.2 filter). |
| 45 | test_41_theDetailGoesVerticalWhenCompact (`:2539`) | Compact: rails vertical, grades survive the turn, Back still there | sdDetailCauseRail, sdDetailEffectRail, sdChainNodeDot, sdChainLink, sdDetailBack, sdDetailTitle | **OBSOLETE** (vertical rails gone; the narrow form is PathChains, covered by #24; Back at narrow → #43 at 396). |
| 46 | test_42_openingAConditionFromTheDetailRetargetsInPlace (`:2571`) | A cause node click re-targets, no close; Back is one step | sdChainNode, sdDetailBack | **KEEP** → `sdCauseNode` click → `lastDetailId`, `closeDetailCalls` 0; `sdDetailBack` → 1. |
| 47 | test_43_aCardShowsTwoFiguresAndItsTickRunNotCharts (`:2608`) | A card shows median + this-shot figure + unit once, "-" in grey for unmeasured; tick run; no charts | sdPatternCard, sdCorridorStrip, sdValueRun, sdTickRun, sdTick, sdCardFigures, sdCardMedian, sdCardCurrent, sdCardFiguresUnit, sdCardMedianCaption, sdCardRecurrence | **KEEP (narrowed)** → Analyse `sdSessionRowMedian` "14", `sdSessionRowShot` "15" in warn, `sdSessionRowUnit` "% hand rise" once, "-" in ink3. "No charts on a row" holds for Analyse; Replay tiles have charts by design. |
| 48 | test_44_theDetailRunIsTallLabelledAndKeepsTheTickRunsPromises (`:2644`) | Run: Y labels 0/10/20, one mark per shot, an N/A stub on the baseline, colours, higher=higher, one selected; pick and click → `shotRequested` with the folder | sdDetailBody, sdDetailValueRun, sdDetailStrip, sdStripPlot, sdRunYTick, sdRunMark, sdRunSelected | **KEEP (rewritten)** → `sdRunChart` marks ×5, the N/A stub, `sdRunSelected` 1, `sdRunYTick`; click → `probe.lastShotDir`. "Twice its first height" OBSOLETE. |
| 49 | test_45_theDetailStripSaysWhichWayIsBetterAndHistoryHasAScale (`:2677`) | "← better / worse →" split at the fault; the full caption; the readout line; the legend unit; history columns, current last, caption; no history → no columns | sdDetailStrip, sdStripDirRow, sdStripDirBigLeft/Right, sdStripFaultLabel, sdStripDirLeft, sdStripCaption, sdDetailHoverReadout, sdDetailRunLegend, sdDetailHistory, sdHistoryYTick, sdHistoryColumn, sdHistoryMedian, sdHistoryCaption | **KEEP (narrowed)** → `sdSpreadStrip` caption, `sdHistoryStrip` columns ×3, current last, `sdHistoryCaption`, absent when there is no history; `highMeans` fact. Direction words → port or OBSOLETE (F51). |
| 50 | test_46_theFiguresFitTheCardsAtEverySize (`:2720`) | The card figures stay inside at 4 sizes × 2 themes × 2 font scales | sdPatternCard, sdCardFigures, sdCardCurrent, sdCardFiguresUnit, sdCardMedianCaption, sdTickRun | **OBSOLETE** as written: replace with a "fold, don't drop" test (no `sdSessionRow` child overhangs; folded columns appear in line 2 at compact and narrow). |
| 51 | test_47_aHoverLightsTheSameShotInBothDetailChartsAndReadsIt (`:2754`) | A strip hover lights the same shot in the run; the line reads it; N/A hovered reads "-"; leaving restores; hover selects nothing; nearest-dot picking | sdDetailBody, sdDetailStrip, sdDetailValueRun, sdDetailHoverReadout, sdStripDot, sdRunMark | **KEEP (rewritten)** → hovering `sdStripDot` sets `brush.hoveredShot`; the matching `sdRunMark.hovered`; `sdDetailHoverReadout` (F53) reads `run[i].readout`; `lastShotDir` stays "". |
| 52 | test_48_theOpenDetailFollowsTheCurrentShot (`:2823`) | A republish moves every shot-dependent field; a hover survives a republish; a new live shot extends the run | sdDetailBody, sdDetailHeaderCard, sdDetailStrip, sdDetailValueRun, sdDetailHoverReadout, sdCardName, sdCardCurrent, sdStatePill, sdTickRun, sdTick, sdStripDot, sdRunSelected, sdRunMark | **KEEP (rewritten)** → `sdFeatureHead` this-shot figure, the picked `sdShotCell`, `sdRunSelected`; **the hover survives** (fails today: §0.4). |
| 53 | test_20_nothingOverhangsTheChrome (`:2887`) | No region overhangs the panel at 3 sizes × 6 sources × both tabs; cards stay inside their scroller sideways | sdExpectationCard, sdThisShotStrip, sdTabBar, sdShotCardsFlick, sdBookends, sdColdBody, sdCardsBody, sdCardsFlick, sdReviewStrip, sdReviewGrid, sdChainRail, sdChainNode, sdChainLink, sdPatternCard | **KEEP (rewritten)**: the same sweep over the size classes (full / stage / half / third widths) × both registers × the three views + detail, using the §5 names. |

**Summary:** 34 KEEP (including narrowed or rewritten), 6 OBSOLETE (#16, 18, 28, 29, 45, 50), 13 NEEDS NEW DESIGN (#1, 2, 3, 5, 9, 22, 23, 35, 37, 38, 39, 40, 41).

### Probes (`src/Gui/tests/probes/`) and their runners (`build/run-me/`)

| Probe | Behaviour pinned | Old names / properties it relies on | Fate |
|---|---|---|---|
| `thisshot_follows_selection.qml` (`verify-thisshot.sh`) | Load a session, pick shot N via the carousel, enter Analyse: the panel's readout is shot N, `_tab==="shot"`, `readingShot`, strip drawn with cells, no "pick a shot" message, label "SHOT N OF" (`:113-124`) | sdBody, `body.readout`, `_tab`, `readingShot`, sdReviewStrip, sdReviewCell, sdShotEmpty, sdReviewStripLabel | **KEEP (rewritten)**: sdShotView visible, `sdDotRow`/`sdFiredRow` > 0, `sdShotEmpty` hidden, `sdShotHead` text contains "Shot N of" (from `shotIndex+1`). |
| `diag_measure_tabs.qml` (`verify-measure-tabs.sh`) | Early extension's detail: 2 measure tabs drawn; the default is the most-fired; each tab redraws its own ruler (`spread.placed`, `measure`, firings); the pick survives Replay→Analyse→Replay | sdBody, `b._openDetail`, `b.source`, sdDetailMeasureTab | **NEEDS NEW DESIGN** (F49). The model half (`setDetailMeasure`, spread per measure, survives a rebuild) is KEEP. It opens the detail in REPLAY (`:110`), where the new Replay draws no detail: either run the drawn checks after `enterAnalyse()`, or design S22. |
| `diag_nav_survives_mode_switch.qml` (`verify-diag-nav.sh`) | Tab, Watching fold, filter, detail, card-list scroll survive Replay↔Analyse rebuilds; another session's place is not restored; the ball ting off in Replay/Analyse | sdBody, `b.tab`, `b.watchingExpanded`, `b.cardFilter`, `b.detailConditionId`, `b.readingShot`, `b.cards`, `_openDetail`, `_closeDetail`, sdCardsFlick, `SessionMode.diagnosticsNav` | **KEEP**, requires §0.2: `cardFilter` on the body, `sdCardsFlick` named, §0.4 (no scroll reset). `watchingExpanded` is legacy: keep it as a no-op property or drop it from both `P:245,256` and the probe. It also exercises S22 (detail open in Replay, `:150-153`). |
| `detail_hover.qml` (`verify-detail-hover.sh`) | A REAL pointer held on a detail strip dot and a run mark for ~2 s on the REPLAY view while the replay loops, in split / stage / tabs: containsMouse holds, hoverIndex stays, the readout line reads it, no delegate rebuild | sdBody, `body.detailOpen`, sdDetailFlick, sdDetailStrip, sdDetailValueRun, sdDetailHoverReadout, sdStripPick, sdRunTap, sdStripDot, sdRunMark (`hoverIndex`, `hot`, `modelData`) | **KEEP (rewritten)**: move it to the ANALYSE view (Replay draws no detail). New handles: `sdDetailFlick`, `sdSpreadStrip`, `sdRunChart`, `sdStripDot`, `sdRunMark`, `body`'s register `brush.hoveredShot`, `sdDetailHoverReadout`. It will catch §0.4. |
| `session_spread.qml` (`verify-session-spread.sh`) | Model: dots per measured shot, N/A counted, fault line at 13 %, open low side, ringed dot = pick. Panel: the card has figures and a tick run, no charts; corridor text exact. Probe-made bodies at 1168×560 and 396 (`createObject` of `PpSessionDiagnosticsBody` with `source`, `:107-110,183-188`); detail: history columns, strip fits 396, run height + Y ticks, nearest-dot pick; hover via `strip.hovered(i)`; a run tap moves the carousel and stays in Analyse; live detail follows a pick | sdBody, sdPatternCard (`.card`), sdCorridorStrip, sdValueRun, sdTickRun, sdCardFigures, sdCardMedian, sdCardCurrent, sdCardFiguresUnit, sdCardMedianCaption, sdDetailHistory, sdDetailStrip, sdDetailValueRun, sdRunYTick, sdHistoryYTick, sdStripDot, sdDetailHoverReadout, sdDetailBody, sdDetailHeaderCard, sdStatePill, sdTick, sdRunSelected | **KEEP (rewritten)**. The model checks are unchanged. The panel checks map to `sdSessionRow` (`cardId`), `sdSessionRowMedian/Shot/Unit`, `sdFigureRow`, `sdSpreadStrip`, `sdRunChart`, `sdHistoryStrip`, `sdStripDot`. Requires the body to still instantiate standalone with only `source` and pick a register (set `SessionMode.mode` or a `register` override). The corridor-text check moves to the row's fold or line. |
| `ledger_live_states.qml` (`verify-ledger-live.sh`) | Model only: the panel's model follows today's session through relaunch / extended / new shot / ended; shot count and filing | sdBody, `.source` (`:57-61`) | **KEEP unchanged** (needs only `sdBody.source`). |

**Runner scripts.**
- `verify-detail-hover.sh:33` and `verify-session-spread.sh:33` grep the log for `PpCorridorStrip|PpValueRun|PpSessionHistory|PpPatternCard|PpConditionDetail|PpSessionDiagnostics`. Update the list to the new block file names (e.g. `SpreadStrip|RunChart|HistoryStrip|LedgerTable|CauseGraph|Replay|Analyse|PpSessionDiagnostics`), or QML errors from the new files go unreported.
- The other four runners need no edits.
- `build/run-me/` is untracked.

---

## 5. OBJECTNAME CONTRACT

Rules:
- The `sd` prefix stays.
- Names the probes, runners and `P` already use keep their name.
- Every repeated delegate exposes a stable key property (`key`, `conditionId`, `shotId`, `kind`, `grade`) so tests select by data, not by index.

### 5a. Kept exactly (relied on by `P`, probes, runners or `PpModeStage`)

| objectName | On | Needed by |
|---|---|---|
| `sdBody` | the body / host (API in §0.1) | `P`, every probe, `RP:83` / `AN:184` |
| `sdShotCardsFlick`, `sdCardsFlick`, `sdWatchingFlick`, `sdDetailFlick` | one Flickable per view (This shot / Session / Watching / Detail), each kept alive (visibility swap, §0.3) | `P:211-212` navigation scroll; `diag_nav` (`sdCardsFlick`), `detail_hover` (`sdDetailFlick`) |
| `sdDetailBody` | the Detail view root | `T`, `session_spread` |
| `sdTitle`, `sdShotLabel`, `sdStageChip`, `sdReviewBadge` | body frame head / aside (when framed) | `T` #1, #11, #13, #25 |
| `sdTabBar`, `sdTab` (`key`, `on`), `sdTabLabel` | `lib/Tabs` root, each tab, its label | `T` #11, #14, #53 |
| `sdDetailBack`, `sdDetailTitle` | `lib/DetailBar` back link, crumb | `T` #43, #44, #46 |
| `sdReviewNote` | the review note (tab note or foot) | `T` #25 |
| `sdCoverageLine` | the coverage line (both registers' Session) | `T` #15, #20, #43 |
| `sdShotEmpty`, `sdWatchingEmpty` | empty states | `thisshot` probe, `T` #11, #14 |
| `sdDetailCauseHeadline`, `sdDetailOutcomeHeadline`, `sdDetailRival`, `sdDetailCappedNote`, `sdDetailMark` | the detail's causal text lines | `T` #43 |
| `sdDetailMeasureTabs`, `sdDetailMeasureTab` (`measureId`, `on`) | measure tabs (F49) | `diag_measure_tabs` |
| `sdDetailHoverReadout` | the run caption / hover readout (F53) | `T` #49, #51, #52; `detail_hover`, `session_spread` |
| `sdStripDot` (`shotId`, `index`, `current`, `hovered`), `sdRunMark` (`shotId`, `hovered`), `sdRunSelected`, `sdRunYTick`, `sdHistoryColumn` (`current`), `sdHistoryMedian`, `sdHistoryYTick`, `sdHistoryCaption` | figure atoms inside SpreadStrip / RunChart / HistoryStrip | `T` #48-52; `detail_hover`, `session_spread` |
| `sdCardFilterEmpty`, `sdColdBody`, `sdColdHeadline`, `sdExpectations`, `sdExpectationCard`, `sdMissChip`, `sdMissPicker`, `sdMissCandidate` (`missId`), `sdMissClear`, `sdQuietState`, `sdQuietLabel` | kept for the NEEDS-NEW-DESIGN items when they land | `T` #1-3, #9, #19, #38, #39 |

### 5b. New (per register, view and block)

| objectName | On | Key properties |
|---|---|---|
| `sdRegister` | the loaded register root (Replay / Analyse) | `register` ("Replay" / "Analyse"), `view`, `surface`, `interactive` |
| `sdShotView`, `sdSessionView`, `sdWatchingView` | the view roots (Detail = `sdDetailBody`) | — |
| `sdCastHead` | Replay cast running head | — |
| `sdTabCount` | the count beside a tab (Analyse) | — |
| `sdShotHead`, `sdSessionHead`, `sdWatchingHead` | Replay `Head` / Analyse `Standfirst` per view | `title`, `sub` |
| `sdStandfirst` | `lib/Standfirst` root | — |
| `sdFiredTable` / `sdFiredRow` | LedgerTable kind fired / each row atom | `conditionId`, `stateKind` |
| `sdFiredRowOutBy` | the out-by cell or fold | — |
| `sdSessionTable` / `sdSessionRow` | LedgerTable kind session / each row | `cardId`, `rankText`, `thisShot`, `focused` |
| `sdSessionRowRank`, `sdSessionRowName`, `sdSessionRowFresh`, `sdSessionRowFired`, `sdSessionRowShot`, `sdSessionRowMedian`, `sdSessionRowUnit`, `sdSessionRowTrend`, `sdSessionRowTypical`, `sdSessionRowWorst`, `sdSessionRowBest`, `sdSessionRowLine2`, `sdSessionRowFocus` | the session row's cells or folded items (the same name whether on the grid or folded) | `shotId` on the bookend cells |
| `sdFilterTool` | each ledger tool | `key`, `on` |
| `sdFilterEmpty` | the empty-filter explanation (alias of `sdCardFilterEmpty`; keep one) | — |
| `sdDotPlot` / `sdDotRow` | `lib/DotPlot` / each condition row | `conditionId`, `stateKind`, `tierTag` |
| `sdQuietLine` | Replay "N clean · M not measurable" | — |
| `sdNotMeasuredNote` | Analyse not-measurable note | — |
| `sdShotRuler` | `reg/ReplayShotRuler` | — |
| `sdSparkRow` | Replay session row (`reg/ReplaySessionRows`) | `cardId` |
| `sdSmallMultiple` | Replay tile | `cardId` |
| `sdBarcode` / `sdBarcodeRow` | `lib/Barcode` / each row | `conditionId` |
| `sdShotStrip` / `sdShotCell` | `lib/ShotStrip` / each cell | `shotId`, `state`, `picked`, `hovered` |
| `sdPickColumn` | the LedgerTable / Barcode pick column | — |
| `sdPageKey` | `lib/PageKey` | — |
| `sdQuestions` / `sdQuestion` / `sdAnswer` | `lib/Questions` / each question / its answer | `questionId` |
| `sdFeatureHead`, `sdKeyFigures`, `sdFigureRow` | detail blocks | — |
| `sdSpreadStrip`, `sdRunChart`, `sdHistoryStrip` | FigureRow panels. (These replace `sdDetailStrip`, `sdDetailValueRun`, `sdDetailHistory`. Or keep the old names as aliases on the new items: probes are cheaper to port if they do.) | `hoveredShot` (read-through of the brush) |
| `sdStripPick`, `sdRunTap` | the pick / hover areas | `containsMouse` |
| `sdCauseGraph` / `sdCauseNode` / `sdCauseEdge` | `lib/CauseGraph` (`which`) / node / edge | node: `conditionId`, `kind`; edge: `grade`, `strokeStyle`, `hasArrow`, `opacity` |
| `sdPathChains` / `sdPathChain` / `sdPathOpen` | `lib/PathChains` / each chain / its Open link | `which`, `index`, `open` |
| `sdDetailFocus` | DetailBar focus link | `focused` |
| `sdScreenCta` | screened-root request (F64) | `conditionId` |

**Suggestion:** set `objectName` inside the lib blocks themselves (they are being moved into `src/Gui` anyway), not in the registers. That way the names exist in Print too.

---

## 6. OUTSIDE USERS, AND WHAT BECOMES DELETABLE

### 6a. Users outside `src/Gui/diagnostics`

These were found by grepping all of `src`; matches inside comments are noted.

| Location | Uses | What it needs from the new panel |
|---|---|---|
| `src/Gui/session/ScreenWrist.qml:157` | `sessionDiagnosticsDelegate: Component { PpSessionDiagnosticsPanel {} }` (the Wrist screen; probes navigate here with `navController.navigate(2)`) | `PpSessionDiagnosticsPanel` stays the delegate. |
| `src/Gui/session/ScreenSessionMode.qml:147` | the same delegate | Same. |
| `src/Gui/session/PpModeStage.qml:46,79-80,113-118,122-134` | Loads the delegate in a `PpStageCard`; reads `panel.cardAside`; sets `panel.framed=false` | `P` must keep `framed` (alias) and `cardAside`. |
| `src/Gui/shot/PpSessionDiagnosticsWindow.qml:129-136` | The cast: `PpSessionDiagnosticsPanel { interactive: win.interactive; remembersNav: false }` | `interactive` alias; add `surface: "cast"` (§0.5) and decide on auto-closing vs persistent (S27-S28). |
| `src/Gui/shell/Main.qml:403,409-417,485-500` | Creates the cast window (`autoClose` for the temporary pop); `castDwellTimer` reads `dwellHeld` | Unchanged. |
| `src/Gui/session/SessionMode.qml:54,62` | `sessionDiagnostics` seam, `diagnosticsNav` | Unchanged; the `diagnosticsNav` shape changes only if the filter or scroll keys change. |
| `src/Gui/shot/PpShotCard.qml:256-286` | Carousel pips via `SessionMode.sessionDiagnostics.shotIdForSwingDir/pipsFor/firedCountFor` | Unchanged (model). |
| `src/Gui/shot/PpShotCarousel.qml:669-670` | `SessionMode.sessionDiagnostics.regradeShot(swingDir)` | Unchanged. |
| `src/Gui/shot/PpShotPipRow.qml` | Tested in `T` #33 | Unchanged; keep `T` #33 (or move it to its own tst). |
| `src/Gui/theme/Theme.qml:668-677` | `strengthColor()` exists for `PpStrengthMeter`. Also used by `src/Gui/tests/probes/theme_tokens.qml:53` and `T:1135-1136` | Deletable with the meter, if `theme_tokens.qml:53` is updated. |
| Comments only | `PpTickRun` named in `src/Gui/components/PpPip.qml:23`, `PpPips.qml:20`, `src/Gui/shot/PpShotPipRow.qml:26`, `src/Gui/home/HmWorkOns.qml:36`, `src/Gui/diagnostics/work_ons_controller.cpp:424`, `session_diagnostics_model.cpp:1992`; `src/Analysis/tests/CMakeLists.txt:133` names `PpSessionDiagnosticsPanel` | Reword the comments when `PpTickRun` goes. |
| Docs | `docs/design/session_diagnostics_design.md`, `session_diagnostics_build_findings.md`, `pinpoint_qml_design_system.md` name `PpPatternCard`, `PpChainRail`, `PpChainLink`, `PpWatchingRow`, `PpThisShotStrip`, `PpCoverageLine`, `PpTickRun` | Update in the docs pass. |

**Not part of this panel** (they live in `src/Gui/diagnostics` but belong to the Wrist motion surface, `ScreenWrist.qml:163` `WristDiagnostics {}`): `WristDiagnostics`, `DofTrajectoryStrip`, `PositionAngleGrid`, `WristScorePill`, `FindingCard`, `FindingsList`, `StrengthsList`, `wrist_diagnostics_model.*`, `work_ons_controller.*`. Do not touch them.

Shared components the old panel uses that stay because other screens use them: `PpCardShell`, `PpCardNote`, `PpBadge`, `PpMicro`, `PpChip`, `PpPopoverCard`, `PpStageCard`.

### 6b. Old diagnostics files deletable once the new panel lands

Each also has to come out of `cmake/PinPointQmlModule.cmake:223-237`.

| File | Used only by |
|---|---|
| `PpThisShotStrip.qml` | `B` |
| `PpReviewShotStrip.qml` | `B` |
| `PpPatternCard.qml` | `B`, `CD` |
| `PpConditionDetail.qml` | `B` |
| `PpChainRail.qml` | `CD` |
| `PpChainNodeCard.qml` | `CR` |
| `PpChainLink.qml` | `CR` |
| `PpCorridorStrip.qml` | `CD`, `VR` |
| `PpValueRun.qml` | `CD` |
| `PpSessionHistory.qml` | `CD` |
| `PpTickRun.qml` | `PC`, `CN`, `VR` (others are comments) |
| `PpStrengthMeter.qml` | `PC`, `TS`, `RS`, `CN` |
| `PpWatchingRow.qml` | `B` |
| `PpCoverageLine.qml` | `B` |
| `PpDashedFrame.qml` | `B`, `TS`, `CN`, `CL`, `CD` |

**Keep, rewritten:**
- `PpSessionDiagnosticsBody.qml`: it becomes the host of the register and keeps the §0.1 API and `sdBody`. Its views, miss picker and frame either move into blocks or stay as body chrome.
- `PpSessionDiagnosticsPanel.qml`: wiring. Update `_navFlicks` if names change, and the readout fallback for S10.
- `PpSessionDiagnosticsWindow.qml`: add `surface`.

**New files to add** to `PP_QML_FILES`: the lib blocks and the two registers (Replay, Analyse; Print later), plus `ReplaySessionRows` and `ReplayShotRuler` (or fold those into lib per their NOTES requests).

---

## Most important gaps (for the build plan)

1. **Link grades are misdrawn** (F60). The new CauseGraph draws the two STRONGEST grades (moved together, conditionally dependent) as the weakest dotted line, and puts an arrowhead on every edge. PathChains gives one glyph to moved, dependent and unanchored. This inverts the model's core honesty device.
2. **Not-measured becomes a gap** (S36, `lib/ShotStrip:3-7`). It reverses design principle 1 ("an absent measure is unavailable, never a pass"), and the strip is used everywhere.
3. **The live states are missing.**
   - Cold, with its line and expectations (S3-S4).
   - Cadence and the quiet shot (S8).
   - The after-shot pulse (S7).
   - Live with nothing focused (S10, which shows "No shot yet" while shots exist).
   - The declared miss (S26).
   - The wrong empty-state tense in Closing and Review (F19).
4. **Live and navigation plumbing.**
   - `applyDemo()` scrolls to the top and clears the hover on every readout republish (§0.4).
   - The filter is register-local, and the Flickables are unnamed and rebuilt by Loaders (§0.2-0.3), so navigation restore and Back lose place.
   - Replay cannot leave an open detail (S22).
5. **15-shot hard-coding** (§0.8, S40). Real sessions have 30-50 swings, so every strip column overflows. The null-readout fallbacks print "15 shots" and "shot 8". The labels use `shotId`, not `shotIndex+1`.

Close behind: the cast picks its register by mode, not by surface, the blocks ignore `interactive`, and the tabs are hidden on persistent casts (S27-S28). Also missing: detail measure tabs (F49), live recency text (F29), card evidence prose (F30), the hover readout line (F53), the screened-root request (F64), and Replay's missing coverage line (F39).
