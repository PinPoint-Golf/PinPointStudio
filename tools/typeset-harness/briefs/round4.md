# Round 4 (pre-port): close the gaps the inventory found, in the scratch library, before anything moves into src

Mark approved building the redesign (11 Oct evening) and is AWAY. The run is unattended: never ask, never
commit, decide by this brief, and write down every decision you make.
Read: $D/build/INVENTORY.md (the contract: states S1–S42, features F1–F68, data, test map), $D/RULES.md,
$D/BRIEF3.md, $D/lib/README.md, $D/build/LIB_REQUESTS.md.
Harness: $D/variant3.sh (guarded: 6 GB kill, two app instances machine-wide). Mode replay = 9th argument.

## Decisions taken for Mark (binding)
D1 PULSE (F40, S7): keep it, quietly. After a surfaced shot, rows/marks whose state changed get ONE soft ink
   wash that fades over Theme.durationSlow; the focused condition leads. reduceMotion → none. Both registers.
D2 QUIET SHOT (S8, F21): the This shot view says so in one calm line built from the model's cadence caption
   ("Quiet shot — nothing new surfaced on this swing"), keeps the shot ruler, and the tabs stay live.
D3 LIVE WITH NOTHING PICKED (S9, S10): Replay is the LIVE glance. When `readout` is null but the session is
   live and has shots, This shot reads the after-shot data (`source.afterShotDelta`, `source.thisShot`) —
   the newest swing — instead of "No shot yet". Empty messages per state are the OLD panel's words (F19,
   F20, F45): "the next swing reads here" only when a swing can still come.
D4 CAST (S27, S28, F3): the cast is ALWAYS the Replay register (surface "cast"), whatever the stage's mode.
   It honours `body.interactive`: an auto-closing cast takes no clicks and never flips the stage; a
   persistent cast (coach/kiosk) keeps its tabs.
D5 UNMEASURED SHOT (F27, S36, design principle "never a gap"): the strip draws a not-measured shot as a short
   ink3 STUB on the baseline — three distinct marks: fired bar, clean tick, not-measured stub. Same in the
   Barcode, Sparkline and run charts. Missing values print "-", never 0.
D6 LINK GRADES (F60): the five model grades keep five distinct strokes and the arrowhead only where the
   evidence supports a direction — port the OLD mapping exactly (PpChainLink.qml) into CauseGraph AND
   PathChains (glyph per grade in text: e.g. ⇒ moved together / conditionally dependent, → coherent,
   ∼ present together, ⋯ unanchored — choose glyphs that read, key them once).
D7 SESSION LENGTH (S40): nothing assumes 15 shots. Strips and barcodes size their pitch to the shot count and
   width (a minimum pitch, then they wrap or thin labels — never clip); labels come from data; "Shot N of M"
   uses the ORDINAL (shotIndex + 1), not the shot id.
D8 PLACE-KEEPING (S33, S42, F34): demo code runs ONLY when demoState is non-empty. The card filter lives on
   the body (`body.cardFilter`), scroll areas carry stable objectNames and restore from the body's saved
   navigation, and a hover survives a republish.
D9 DETAIL IN REPLAY (S22): Replay never shows a detail; a tab press while `body.detailOpen` closes the detail
   first. Esc closes the detail in Analyse (F47).
D10 PORT, DON'T DROP: measure tabs in the detail (F49, one measure at a time, the pick survives a rebuild);
   live recency "last fired N shots ago" (F29); the evidence sentence (F30) as Analyse ledger line-2 text
   from medium width; the norm citation as an inline note (no tooltip); screened-root "Run the screen"
   (F64 → body.screenRequested); the coverage line in Replay's Session foot (F39); PathChains "Open"
   expands the path in place (F58); the honesty mark for a non-card condition (F56); Cold head + expectations
   (F41, F42) as a Session block "expectations to test, not findings"; the declared-miss chip and its picker
   (F66, F67) — the chip sits quietly at the right of the tab row; the picker opens INLINE under the tabs
   (a list, current one marked, "No declared miss" clears), never a pop-up; the empty-filter explanation
   (F35); the empty Watching sentence (F45).
D11 DROPS (with reason, recorded in NOTES): "scroll for the rest" (the scroll bar says it), the whole-rows card
   grid (rows are atoms), the silent tail fold (the clean conditions continue by closeness — rule 12).

## Work split (disjoint files; run in parallel)
L — LIBRARY ($D/lib/*, plus $D/reg/Print*.qml): D5, D6, D7 at block level; every item in LIB_REQUESTS.md
    (SparkHead, ShotRuler block, Barcode values-fold + per-row atoms + repeatable head + counts from all
    rows, DotPlot rowsFitting(h) + showTier + head-as-unit, Pager adopts PrintPager's repeating heads /
    widow / early-break / true-height 2× head, PathChains print mode + Open-in-place + grade glyphs,
    PageKey merge, KeyFigures exclude, Standfirst section, Questions forCondition/only/subject + answer
    parts, FeatureHead showPull/showFacts, SmallMultiple bottom-row band); then move Print.qml onto the
    improved Pager/PathChains and delete PrintPager/PrintPaths. Public APIs only grow (additive props with
    safe defaults). Re-render Gallery, FigureGallery and Print (portrait + landscape, themes 6,12).
R — REGISTERS ($D/reg/Replay*.qml, Analyse*.qml, Panel.qml, and a new $D/reg/LiveStates.qml): D1–D4, D8–D11
    and every state S1–S42 marked ✗ or ◐ in INVENTORY §1 that is register-level. LiveStates.qml is a
    TEST WRAPPER for the harness: it takes the real data and SYNTHESISES each live state the model can
    publish (cold 0 / cold 2 shots + expectations, forming, established, after-shot with pulse, quiet,
    live nothing picked, live older pick, closing, review no pick, declared miss + picker open, focus
    declared, detail open in Replay, 23-shot session, auto-closing cast, persistent cast) by overriding the
    fields named in INVENTORY §3 — and composes Panel.qml with it, selecting the state from `demoState`.
    It is never shipped. When L reports, adopt its new APIs (I will message you).
    Renders: every state × Replay and Analyse where it applies × half (themes 7,12), plus full for the
    states that change layout; the cast states at layout `cast`.

Both: keep RULES.md (all 12); no ellipsis; type never scales; colour = "out" only. Delete intermediates;
final tags r4_lib_* and r4_reg_*. Write $D/lib/NOTES4.md or $D/reg/NOTES4.md (≤ 60 lines): what changed,
every decision you took beyond this brief, and the objectNames you added (INVENTORY §5 is the target list).
