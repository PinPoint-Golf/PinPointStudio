> **Historical working document (11 Oct 2026).** `$D` was a session scratchpad, now gone. Its
> mock library (`$D/lib`, `$D/reg`) and data are archived at `/mnt/swingdata/scratch/typeset-harness/archive/2026-10-11/` (`mock_lib_round4/`,
> `mock_reg_round4/`, `data_4jul_shot8/`); the shipped code is in `src/Gui/typeset/` and
> `src/Gui/diagnostics/`. Kept as a worked example of the method in `docs/design/stage_typeset_playbook.md`.

# Registers, round 4 (R): the old panel's states, in both registers
Files: `Replay.qml`, `Analyse.qml`, `Panel.qml`, `ReplaySessionRows.qml` (now on lib SparkHead), new `ReplayState.qml`
(the session's tense, read once for both registers), `ReplayCold.qml` (cold line + lib Expectations), and the test wrapper
`LiveStates.qml` (never ships). Deleted: `ReplayShotRuler.qml` (lib ShotRuler), the local miss picker (lib Tabs.chip +
InlinePicker), my pulse overlay (lib Brush.pulse / pulseHold / PulseWash), my mark / hover-readout / norm lines (lib). Renders: `out/r4_reg_{rp,an,cast,plain_rp,plain_an}_*`.

## States covered (INVENTORY §1; LiveStates name → registers; half 7+12, full where the layout changes)
S1 noSource (both: chrome only, as the old panel) · S2 cold0 · S3+S4 cold2 (coldLine head, countLine, dashed expectations,
omitted when empty) · S5 forming · S6 established (miss invitation stands down) · S7 afterShot (Brush.pulse; held wash
for the still) · S8 quiet (D2) · S9/S10 liveNoPick (D3: newest swing from afterShotDelta + thisShot) · S11 liveOlderPick
(live recency on line 2) · S13 closing / S15 reviewNoPick (F19 tenses) · S16 base · S19 plain detail · S21 screenedRoot
(detail id poor_core_stability) · S22 detailInReplay (Replay) · S23 filterBall, filterEmpty (Analyse) · S24 watchingEmpty ·
S25 focus · S26 missDeclared, missPicker · S27 castAuto · S28 castPersistent (layout cast) · S36 lib stubs · S38 zeroFired ·
S40 long23 (23 shots, swing id 6 deleted: labels are ordinals, brushing uses ids) · S42 + S20 (no demo code when
demoState is empty) · F49 measureTabs (detail id early_extension) · F53 hoverShot12.

## Decisions beyond the brief
1. Panel: register = forceRegister (tests only) → surface "cast" → SessionMode. Panel passes `interactive`, `cardFilter`,
   `pulseCue` from the body so a wrapper can override them without touching the live body.
2. D3 newest swing: thisShot publishes no value for a watched chip, so the newest read prints OUT-BY (the plot's own x)
   for every row; Analyse drops the value/unit/strip columns; Watching becomes names + counts (no run to draw). No ruler.
3. D2 quiet: Replay replaces the plot with the calm line + headline and keeps the ruler; Analyse states the same line
   above the full read (a read is never gated, INVENTORY F21). The shot tab's count reads "quiet" in Analyse.
4. D4 cast: condition clicks never drill from ANY cast (the cast's model is not the stage's, and a second screen must not
   flip the main stage); shots still pick on a persistent cast. The auto cast eats every input with one MouseArea.
   The register's cast head is the cast's title line; the body frame should stand down on the cast (S30).
5. D9: Replay shows the reader's tab under an open detail plus one quiet line "X is open in Analyse · Read it there".
6. Replay never filters (a glance shows every pattern); the filter is an Analyse tool on body.cardFilter, hidden when
   not interactive or under two cards. Tab count follows the filter.
7. Bookend columns are not drawn before Closing (the model publishes them at Closing only; "-" would read unmeasured).
8. Zero counts are words, not "no": "outside on none", "All 78 conditions … stayed inside", "inside … on every one".
   F20's second sentence adapted to the new layout: "No patterns yet — the plot below is everything this swing said."
9. closingLine is NOT drawn (it still says "wide tick", MODEL_DEFECTS #4); the round-3 string replace is gone.
10. F64: CauseGraph's own "Run the screen" (medium+); below medium, and on a screened root's own page, one line lists
    the screened roots on the paths, each a link → body.screenRequested(ref if it is the driver's, id).
11. Esc (F47): Keys on the detail root with focus forced on open; Main.qml's Esc Shortcut is disabled in Analyse.
12. Detail key figures: FeatureHead.showPull false; KeyFigures = its own items minus what the fact file says
    (exclude pass, faultLine, faultOutside, fired, after, trend) + the distance to travel (past the fault line, from
    the pass edge, out by). Questions come from Questions.forCondition({ subject, only }).
    body.screenRequested keeps the body's order (screenRef, conditionId): ref = driver.screenRef when the root is
    the driver's screen, else "" (the old node's rule), not a literal "screen".
13. Each view is its own Flickable, built on first show and never torn down (sticky Loader), so Back and a mode switch
    keep scroll; the detail view also keeps its expansion state.
14. Pulse ids arriving as afterShotDelta chips (the body's pulseAfterShot passes chip maps, not ids) are mapped to ids.
D11 drops recorded: "scroll for the rest" (the scroll bar says it), the whole-rows card grid (rows are atoms), the
silent-tail fold (clean conditions continue by closeness, RULES 12).

## objectNames added (register-level; the lib names its own blocks)
sdRegister (`register`, view, surface, interactive) · sdShotView sdSessionView sdWatchingView sdDetailBody ·
sdShotCardsFlick sdCardsFlick sdWatchingFlick sdDetailFlick · sdTabBar · sdCastHead · sdShotHead sdSessionHead
sdWatchingHead · sdShotEmpty sdWatchingEmpty sdFiredEmpty · sdQuietState sdQuietLabel sdQuietLine sdQuietCount ·
sdColdBody sdColdHeadline sdColdSubline sdExpectations · sdMissChip (Tabs.chip) sdMissPicker
(InlinePicker; its rows sdMissCandidate, sdMissClear) · sdCardFilterEmpty · sdCoverageLine · sdReviewNote ·
sdNotMeasuredNote · sdQuestions · sdFeatureHead sdFigureRow ·
sdDetailMeasureTabs sdDetailMeasureTab (measureId, on) · sdScreenRequests sdScreenCta (conditionId) · sdDetailParked.

## Lib requests remaining
Roles.word(0) → "No/no" in shotStandfirst / sessionStandfirst ("No of 78…", "no are session patterns", "outside on no of
them"; the registers word zero themselves). A `conditionsInteractive` flag on SparkRow/Barcode/SmallMultiples (the
persistent cast picks shots, never conditions). KeyFigures' "Shot N" uses the readout, the travel figures the detail's run.
