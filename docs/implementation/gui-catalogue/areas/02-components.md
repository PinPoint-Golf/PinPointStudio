# Shared components

*Directory:* `src/Gui/components/`  
*37 components · 3,421 lines of QML*

The app-wide primitives: buttons, chips, cards, pills, badges, toasts and the coaching-card shell (PpCard family). The intended single home for anything reused by more than one area.

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [ClubFormat](#clubformat) | 38 | `QtObject` | 8 | 0 | singleton |
| [PpBadge](#ppbadge) | 130 | `Rectangle` | 12 | 0 |  |
| [PpButton](#ppbutton) | 112 | `Rectangle` | 27 | 1 |  |
| [PpCard](#ppcard) | 73 | `Item` | 2 | 2 |  |
| [PpCardNote](#ppcardnote) | 32 | `Text` | 19 | 0 |  |
| [PpCardShell](#ppcardshell) | 84 | `Item` | 7 | 0 |  |
| [PpChip](#ppchip) | 53 | `Rectangle` | 11 | 0 |  |
| [PpChipGroup](#ppchipgroup) | 92 | `Row` | 4 | 1 |  |
| [PpComboBox](#ppcombobox) | 135 | `ComboBox` | 15 | 0 |  |
| [PpConnectingFrame](#ppconnectingframe) | 117 | `Item` | 4 | 0 |  |
| [PpDisplayText](#ppdisplaytext) | 102 | `Item` | 22 | 0 |  |
| [PpDivider](#ppdivider) | 32 | `Rectangle` | 17 | 0 |  |
| [PpFact](#ppfact) | 63 | `Item` | 1 | 1 |  |
| [PpFlowIndicator](#ppflowindicator) | 444 | `Item` | 1 | 0 |  |
| [PpInset](#ppinset) | 62 | `Item` | 4 | 0 |  |
| [PpLink](#pplink) | 51 | `Text` | 5 | 0 |  |
| [PpMeter](#ppmeter) | 44 | `Row` | 3 | 0 |  |
| [PpMetricSwatches](#ppmetricswatches) | 74 | `Flow` | 1 | 1 |  |
| [PpMicro](#ppmicro) | 31 | `Text` | 37 | 0 |  |
| [PpNotificationHost](#ppnotificationhost) | 158 | `ListView` | 1 | 1 |  |
| [PpPip](#pppip) | 55 | `Item` | 2 | 0 |  |
| [PpPips](#pppips) | 51 | `Row` | 3 | 1 |  |
| [PpPopoverCard](#pppopovercard) | 29 | `PpCardShell` | 9 | 1 |  |
| [PpPressable](#pppressable) | 97 | `MouseArea` | 62 | 0 |  |
| [PpQualityPill](#ppqualitypill) | 77 | `Rectangle` | 2 | 1 |  |
| [PpSegmentedControl](#ppsegmentedcontrol) | 83 | `Rectangle` | 6 | 0 |  |
| [PpStageCard](#ppstagecard) | 110 | `Item` | 1 | 2 |  |
| [PpStarRating](#ppstarrating) | 60 | `Row` | 4 | 0 |  |
| [PpStrengthMark](#ppstrengthmark) | 59 | `Column` | 1 | 1 |  |
| [PpSwingTimeline](#ppswingtimeline) | 89 | `Item` | 1 | 1 |  |
| [PpTextField](#pptextfield) | 56 | `TextField` | 17 | 0 |  |
| [PpToast](#pptoast) | 156 | `Rectangle` | 5 | 1 |  |
| [PpToolPill](#pptoolpill) | 125 | `Rectangle` | 1 | 0 |  |
| [PpTopoBackground](#pptopobackground) | 245 | `Item` | 3 | 0 |  |
| [PpTrace](#pptrace) | 51 | `Item` | 1 | 0 |  |
| [PpTypePill](#pptypepill) | 63 | `Rectangle` | 1 | 0 |  |
| [PpUnitToggle](#ppunittoggle) | 88 | `Row` | 2 | 0 |  |

### ClubFormat
`src/Gui/components/ClubFormat.qml` · 38 lines · root `QtObject` · **singleton**

> Club-name DISPLAY formatting. Club ids are stored/keyed UPPERCASE (the
> canonical club_vocabulary.h vocabulary — bag keys, swing.json review.club,
> truth.json meta.club); this singleton is the ONE place that turns an id into
> the human-facing sentence-case label at the last moment before it is shown.
> "DRIVER" -> "Driver", "7 IRON" -> "7 iron", "GAP WEDGE" -> "Gap wedge"
> Mirror of pinpoint::clubDisplayLabel (session_summary.h) for the C++ clubMix.
> Never feed display() output back into storage or index lookups.

- **Functions:** `display(id)`
- **Used by (8):** `AthleteClubsSection`, `PpClubPanel`, `PpMarkupPanel`, `PpSessionToolbar`, `PpShotActionBar`, `PpSwingEditPanel`, `ScreenAthleteForm`, `ScreenHome`

### PpBadge
`src/Gui/components/PpBadge.qml` · 130 lines · root `Rectangle`

> A leading badge: a round mark at the head of a coaching line, a shape for each meaning so
> colour is never the only channel.
> check        a tick, green: something done well, an ideal to aim for
> target       a ring and dot, amber: something to work on
> easing       a down arrow, green: a fault that is fading
> unconfirmed  a dashed ring and dot, grey and untinted: placed on evidence no recent session
> could renew
> Every kind but unconfirmed sits on a faint fill of its tone. Only the kind's own glyph is
> loaded, so a badge draws exactly what its inline original drew.

- **Properties:** `string kind`, `color tone`, `int size`
- **Used by (12):** `HmFocus`, `HmSwingSummary`, `HmWorkOns`, `PpCameraFrame`, `PpCameraPanel`, `PpChartSummary`, `PpImuPanel`, `PpQualityPill`, `PpSequenceStrip`, `PpSessionToolbar` (+2)

### PpButton
`src/Gui/components/PpButton.qml` · 112 lines · root `Rectangle` · 3 hard-coded colour(s)

The app's standard button: label and/or glyph/icon, with primary, destructive and attention variants. Built on PpPressable. Emits clicked.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `string label`, `string glyph`, `Component icon`, `real iconSize`, `bool primary`, `bool destructive`, `bool attention`, `readonly color contentColor`
- **Signals:** `clicked()`
- **Uses:** `PpPressable`
- **Used by (27):** `AppearancePanel`, `ArchivingPanel`, `ArmCalibrationStatus`, `AthleteClubsSection`, `BallPage`, `CameraCalibrationFlow`, `CamerasPanel`, `DiagnosticModel`, `GeneralPanel`, `ImuCalibrationFlow` (+17)

### PpCard
`src/Gui/components/PpCard.qml` · 73 lines · root `Item`

> A coaching card: the shell, a mono title in the card's tone, an optional aside on the title's
> baseline at the right, and a column of content under them.
> The aside right-aligns over a column `asideWidth` wide when it is narrower than that, so a
> "SESSIONS" heading sits over the pips it heads rather than at the card's edge.

- **Properties:** `color tone`, `string title`, `string aside`, `int asideWidth`, `int pad`, `int itemGap`, `int bodyGap`, `alias content`, `readonly int innerWidth`, `readonly alias titleItem`
- **Uses:** `PpCardShell`, `PpMicro`
- **Used by (2):** `HmGoesTogether`, `HmSwingSummary`

### PpCardNote
`src/Gui/components/PpCardNote.qml` · 32 lines · root `Text`

> A card with nothing in it says so in one quiet line, so the card keeps its shape.

- **Used by (19):** `HmSwingSummary`, `PpCameraFrame`, `PpCameraPanel`, `PpCameraTiles`, `PpClubPanel`, `PpDataViewer`, `PpExportOptionsSheet`, `PpImuPanel`, `PpLaunchMonitorPanel`, `PpMarkupPanel` (+9)

### PpCardShell
`src/Gui/components/PpCardShell.qml` · 84 lines · root `Item`

> The coaching card's background, and nothing else: the surface, a hairline, and a rule in the
> card's tone along the top edge that follows the rounded corners. The caller fills it and lays
> its own content over it.
> The rule is a rounded tone shape with its lower part covered by the surface again, so it bends
> with the corners where a plain bar would square them off.
> standard   3 px rule, colorBorderMid hairline (colorBorderStrong when floating, so a popover
> lifts off whatever it sits over)
> hero       4 px rule, no hairline on the surface; a faint wash of the tone over the whole card
> with a tone hairline, just enough to lift the one card that matters off the page
> The hero pieces are always there and only shown in hero, so the two draw exactly what their
> inline originals drew (HmSwingSummary's Card, HmFocus's shell).
> The theme sets the rule heights (Theme.cardRule / heroRule) and whether a hero is tinted
> (Theme.heroTinted). A rule of 0 draws no rule at all, the tone shape and its surface cover both
> hidden, so no sliver of tone shows at the corners. An untinted hero is a standard card: the
> surface and its hairline, no wash. Folio uses both, for a card that is only a hairline box.

- **Properties:** `color tone`, `bool hero`, `bool floating`, `readonly int ruleH`, `readonly bool tinted`
- **Used by (7):** `HmFocus`, `HmWorkOns`, `PpCard`, `PpChartsBody`, `PpPopoverCard`, `PpSessionDiagnosticsBody`, `PpStageCard`

### PpChip
`src/Gui/components/PpChip.qml` · 53 lines · root `Rectangle`

> A pill of tiny mono words in a tone: "↑ growing" / "↓ easing" from a trend, or any words a
> caller gives it ("NEXT", "last seen 4 Jul"). Untinted it is a grey outline, for something
> stated without weight. With no words it takes no room.

- **Properties:** `int trend`, `string text`, `color tone`, `bool tinted`, `real tracking`
- **Used by (11):** `HmFocus`, `HmGoesTogether`, `HmSwingSummary`, `HmWorkOns`, `PpCameraFrame`, `PpCameraPanel`, `PpDataViewer`, `PpImuPanel`, `PpShotCarousel`, `PpTransitTimeline` (+1)

### PpChipGroup
`src/Gui/components/PpChipGroup.qml` · 92 lines · root `Row` · 2 hard-coded colour(s)

Single-select row of text chips (options array, selected value, optional disabled options). Emits selectionChanged, and blockedClicked when a disabled chip is pressed.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `var options`, `string selected`, `var disabledOptions`
- **Signals:** `selectionChanged(string value)`, `blockedClicked(string value)`
- **Uses:** `PpPressable`
- **Used by (4):** `AppearancePanel`, `DisplaysPanel`, `GeneralPanel`, `ScreenAthleteForm`

### PpComboBox
`src/Gui/components/PpComboBox.qml` · 135 lines · root `ComboBox`

> Shared dropdown selector. A thin styling layer over the Basic ComboBox so every
> drop-down across the app shares one look, one (text-scaled) height, and one place
> to evolve "app flair" later. Callers drive it like a normal ComboBox — set
> `model`, `currentIndex`, `onActivated`, `implicitWidth`, `enabled`, `visible` — and
> keep any external sync logic (Component.onCompleted / Connections) on the instance.
> Extras over the stock control:
> displaySuffix  — text appended to every label, closed view and rows (e.g. " Hz")
> itemEnabledFn  — optional function(index) -> bool to disable individual rows
> (e.g. an already-assigned placement); disabled rows are inert
> and muted. Read whatever reactive state you need inside it so
> QML tracks the dependency.

- **Properties:** `string displaySuffix`, `var itemEnabledFn`, `var displayFn`
- **Used by (15):** `ArchivingPanel`, `AthleteClubsSection`, `AudioPage`, `CamerasPanel`, `CapturePage`, `DisplaysPanel`, `FilmPage`, `GeneralPanel`, `ImusPanel`, `LaunchMonitorPanel` (+5)

### PpConnectingFrame
`src/Gui/components/PpConnectingFrame.qml` · 117 lines · root `Item` · 1 hard-coded colour(s)

> Traveling-light "connecting" frame. Overlay it on a button with
> `anchors.fill: parent`; drive `running` from a connection-in-flight boolean
> (CameraInstance.isConnecting / ImuInstance.busy / the manager anyConnecting
> aggregates). While running, a dim track outlines the whole rounded-rect
> border and a single bright comet of stroke chases around it. When `running`
> clears (connect succeeded, failed, or timed out) the frame fades away and the
> button returns to normal. Purely decorative — captures no input.
> Honours Theme.reduceMotion: the comet stops and a steady accent outline marks
> the in-flight state instead (no motion).

- **Properties:** `bool running`, `real radius`, `real lineWidth`, `color color`
- **Used by (4):** `ImusPanel`, `PpCameraPanel`, `PpImuPanel`, `ScreenSessionSetup`
- **Exercised by tests/tools:** `SetupDriver`

### PpDisplayText
`src/Gui/components/PpDisplayText.qml` · 102 lines · root `Item`

> Large display title with the brand warm→cool gradient fill (the site's
> signature). Falls back to a flat Text when Theme.gradientTitlesActive is
> false (reduce-motion / user preference). Sizes to its text, but tracks an
> externally-assigned width (Layout.fillWidth / explicit width) so wrapping
> titles wrap to the available space — implicitWidth/Height still report the
> natural glyph geometry so plain (un-stretched) placement is unchanged.

- **Properties:** `alias text`, `int pixelSize`, `string fontFamily`, `int fontWeight`, `bool italic`, `real letterSpacing`, `color flatColor`, `int horizontalAlignment`, `alias wrapMode`, `alias lineHeight`, `alias elide`, `alias maximumLineCount`
- **Used by (22):** `AppearancePanel`, `ArchivingPanel`, `ArmCalibrationStatus`, `CameraCalibrationFlow`, `CamerasPanel`, `CheckArmPage`, `DiagnosticModel`, `DisplaysPanel`, `GeneralPanel`, `ImusPanel` (+12)

### PpDivider
`src/Gui/components/PpDivider.qml` · 32 lines · root `Rectangle`

Hairline rule, horizontal or vertical, in the theme's border colour.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `int orientation`
- **Used by (17):** `AppearancePanel`, `ArchivingPanel`, `CamerasPanel`, `DisplaysPanel`, `GeneralPanel`, `ImusPanel`, `LaunchMonitorPanel`, `MetricDetail`, `MetricLibrary`, `MicrophonesPanel` (+7)

### PpFact
`src/Gui/components/PpFact.qml` · 63 lines · root `Item`

> A label and its value, in an opened row: a micro label in a fixed column, then the value's
> words. Marks placed inside it (pips, a key) go in a row before the words; beside them the label
> sits on their centre, otherwise on the words' first baseline. Empty, it takes no room.

- **Properties:** `string label`, `string text`, `int labelWidth`, `alias extra`
- **Uses:** `PpMicro`
- **Used by (1):** `HmWorkOns`

### PpFlowIndicator
`src/Gui/components/PpFlowIndicator.qml` · 444 lines · root `Item` · 1 hard-coded colour(s)

> PpFlowIndicator — the progress indicator of a multi-step flow (session_wizard_refactor_design.md
> §4.9). It replaces the session wizard's tab strip, and is generic: it knows nothing of session
> setup, so the toolbar calibration and the trunk ceremony can reuse it (`compact`).
> SESSION      CAMERAS                      SENSORS                              READY
> ●───────────●──────────●──────────◉━━━━━━━○──────────○──────────○──────────○
> Goals ✓     Cameras ✓  Ball ⚠      IMUs       Calibrate   Check       Ready
> ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━░░░░░░░░░░░░░░░░░░░░░░░░░░  3 of 7 done
> ── Model ─────────────────────────────────────────────────────────────────────────────────────
> model    [{ key, label, group, state, current, attention, number, tip }] in flow order —
> state "pending" | "done" | "skipped"; `current` marks the step on screen; `attention`
> an amber ring (a visited step that needs attention); `number` the 1-based display
> number the page eyebrow also shows; `tip` the hover text ("" for none).
> groups   group → label ("Cameras"); drawn capitalised over each group's run.
> progress { passed, total } — a skipped step counts as passed ("3 of 7 done").
> canGoTo  function(key) → bool, or null; a done or skipped pip is clickable when it says yes.
> jumpRequested(key)  emitted on such a click. Future and current pips are inert (D5).
> …

- **Properties:** `var model`, `var groups`, `var progress`, `var canGoTo`, `bool compact`, `readonly real cellWidth`, `readonly real pipSize`, `readonly real groupHeight`, `readonly real labelHeight`, `readonly real barHeight`, `readonly bool collapsed`
- **Signals:** `jumpRequested(string key)`
- **Used by (1):** `ScreenSessionSetup`
- **Exercised by tests/tools:** `tst_setup_indicator`

### PpInset
`src/Gui/components/PpInset.qml` · 62 lines · root `Item`

> Something set into a card: a faintly darker panel with a hairline, for the part a reader acts
> on (a drill, what to try). With `bar`, a short bar in the tone down its left edge marks it as
> the card's own — the focus card's HOW TO PRACTISE.
> The content is a column inset by padX / padY; the panel's natural height follows it.

- **Properties:** `color tone`, `bool bar`, `int padX`, `int padY`, `int spacing`, `alias content`, `readonly real contentHeight`
- **Used by (4):** `HmFocus`, `HmWorkOns`, `PpChartSummary`, `PpSwingEditPanel`

### PpLink
`src/Gui/components/PpLink.qml` · 51 lines · root `Text` · 1 hard-coded colour(s)

> A text link in the accent: words that go somewhere. It brightens under the pointer where
> `hoverTint` is on. A caller with a larger press target of its own sets `interactive: false`
> and binds `hovered` to it; `pressMargin` grows the link's own target past its words.

- **Properties:** `bool interactive`, `bool hoverTint`, `bool hovered`, `int pressMargin`
- **Signals:** `clicked(var mouse)`
- **Used by (5):** `HmSwingSummary`, `HmWorkOns`, `PpShotFilter`, `PpSwingEditPanel`, `ScreenSwingDiagnostics`

### PpMeter
`src/Gui/components/PpMeter.qml` · 44 lines · root `Row`

> How often, as ten rounded segments: a share of 0..1 lights round(share × 10) of them in the
> tone. The segment count carries the reading, so colour is never the only channel.

- **Properties:** `real share`, `color tone`, `readonly int filled`
- **Used by (3):** `HmFocus`, `HmSwingSummary`, `HmWorkOns`

### PpMetricSwatches
`src/Gui/components/PpMetricSwatches.qml` · 74 lines · root `Flow`

> The metric palette as a row of swatches — one per NAME, drawn in the current theme's colour for
> it (Theme.paletteColor, so a user's retune shows here too). Picking one emits its name; what the
> name is written to is the caller's business (a metric's colour in the inspector, the name being
> retuned in Appearance).

- **Properties:** `string selected`, `real swatchSize`
- **Signals:** `picked(string name)`
- **Uses:** `PpPressable`
- **Used by (1):** `ModelInspector`

### PpMicro
`src/Gui/components/PpMicro.qml` · 31 lines · root `Text`

> A micro heading: the app's mono caps, quiet by default. Callers tint it (a card's title in the
> card's tone) or loosen its tracking (asides and counts use trackingData).

- **Used by (37):** `HmFocus`, `HmSwingSummary`, `HmWorkOns`, `PpCameraFrame`, `PpCameraPanel`, `PpCameraTiles`, `PpCard`, `PpChartSummary`, `PpChartsBody`, `PpClubPanel` (+27)

### PpNotificationHost
`src/Gui/components/PpNotificationHost.qml` · 158 lines · root `ListView`

> The ONE window-level notification surface, rendering NotificationCenter.
> ⛔ WHY THIS IS A MODEL AND NOT FOUR PpToasts.  Until this component the four
> shot-pipeline notices were four PpToast instances, each wired to its own
> signal and each positioned by hand relative to `saveErrorToast.y` — three of
> them computing the SAME y, so any two visible at once overlapped. Worse,
> PpToast.show() sets the text and restarts a hide timer, so ten consecutive
> failures were one toast showing the tenth message. Identity, counting,
> cause-suppression and latching all live in C++ now (see notification_center.h);
> this file only draws what the model says is live.
> PpToast itself is untouched and still serves the five panel-local imperative
> confirmations (copy-to-clipboard, an export result) — those are not this
> problem, per design §7.4.

- **Properties:** `readonly int sevInfo`, `readonly int sevWarn`, `readonly int sevError`, `readonly int sevProgress`, `readonly int kindCondition`
- **Uses:** `PpPressable`
- **Used by (1):** `Main`

### PpPip
`src/Gui/components/PpPip.qml` · 55 lines · root `Item`

> One session pip. Two vocabularies share it:
> seen / unseen              the summary's: a dot in the tone where the session saw it, an empty
> ring where it did not
> fired / clean /            PpTickRun's: a dot in the tone where it was a pattern, a short
> notAssessable              dash in cleanTone where the session was clean, and an outlined ring
> where the session could not tell — present, never a gap
> The dash is centred on the dot's line, so a mixed row keeps one centre.

- **Properties:** `string kind`, `color tone`, `color cleanTone`, `int size`
- **Used by (2):** `HmWorkOns`, `PpPips`

### PpPips
`src/Gui/components/PpPips.qml` · 51 lines · root `Row`

> A row of session pips, oldest first. `marks` are the summary's seen/unseen bools; `ticks` are
> PpTickRun's { state } and win when there are any. Callers keep their own latest-N slicing, so
> every row on a card can share one width.

- **Properties:** `var marks`, `var ticks`, `color tone`, `color cleanTone`, `int size`, `int gap`, `readonly bool useTicks`
- **Uses:** `PpPip`
- **Used by (3):** `HmFocus`, `HmSwingSummary`, `HmWorkOns`

### PpPopoverCard
`src/Gui/components/PpPopoverCard.qml` · 29 lines · root `PpCardShell`

> A popup's background in the card family: the standard shell with a stronger hairline, so it
> lifts off whatever it is drawn over, and a quiet rule. For a Popup's `background:`.

- **Uses:** `PpCardShell`
- **Used by (9):** `PpAnnotationToolbar`, `PpDataViewer`, `PpExportOptionsSheet`, `PpMarkupPanel`, `PpMetricChart`, `PpSessionDrawer`, `PpSessionToolbar`, `PpShotActionBar`, `PpShotCarousel`

### PpPressable
`src/Gui/components/PpPressable.qml` · 97 lines · root `MouseArea`

> Drop-in replacement for a plain MouseArea that gives any clickable element the
> standard PinPoint hover-grow / press-dip motion — the same interaction language
> as the home tiles, shot cards and toolbar pills. It drives its PARENT's `scale`
> through an animated internal value, so the host item needs to declare nothing:
> just swap `MouseArea { ... }` for `PpPressable { ... }`.
> Rectangle {
> color: pressIt.containsMouse ? Theme.colorBg3 : Theme.colorBg2   // optional hover brighten
> Text  { ... }
> PpPressable { id: pressIt; onClicked: doThing() }
> }
> Scale is a render transform (an Item's default transformOrigin is Center), so it
> never reflows neighbours or affects layout, and reduceMotion zeroes the duration.
> Use this ONLY for visually separated controls — a scale on a contiguous segmented
> control would break the shared seam, so brighten those instead. A filled/outline
> fill usually also wants a hover brighten; keep that on the host and read this
> element's `containsMouse` via its id.

- **Properties:** `real hoverScale`, `real pressScale`, `bool held`, `bool confirmOnClick`
- **Used by (62):** `AppearancePanel`, `AthleteClubsSection`, `CamerasPanel`, `DiagnosticModel`, `GeneralPanel`, `GoalsPage`, `ImusPage`, `ImusPanel`, `Main`, `MetricDetail` (+52)

### PpQualityPill
`src/Gui/components/PpQualityPill.qml` · 77 lines · root `Rectangle` · 1 hard-coded colour(s)

> Shot-quality score pill — fill graded by quartile via Theme.qualityColor.
> Small variant sits on the shot card; large variant ("NN /100") heads the
> review panel. Text colour flips with Theme.dark (the toolbar count-badge
> idiom) so it stays legible across all themes. With `mark`, the band's shape
> (Theme.qualityMark: a tick in the top band, a target below it) leads the
> number, so the band reads without its colour where nothing beside the pill
> says it in words.

- **Properties:** `int score`, `bool large`, `bool mark`
- **Uses:** `PpBadge`
- **Used by (2):** `PpShotActionBar`, `PpShotCard`

### PpSegmentedControl
`src/Gui/components/PpSegmentedControl.qml` · 83 lines · root `Rectangle` · 2 hard-coded colour(s)

> Segmented "pill" selector: one recessed capsule track, equal-width segments,
> the active one filled. Used for the View popup's layout presets. Width-driven
> (fills its parent); set `options` (string list) and `selected`.

- **Properties:** `var options`, `string selected`, `bool solid`
- **Signals:** `activated(string value)`
- **Used by (6):** `DiagnosticModel`, `PpDataViewer`, `PpMarkupPanel`, `PpMetricChart`, `PpSessionToolbar`, `SwingViz3DView`

### PpStageCard
`src/Gui/components/PpStageCard.qml` · 110 lines · root `Item`

> A card for a session stage's panel: the coaching card's shell, tighter padding, and a body that
> fills the card rather than a column that sizes it — a stage panel is given its size by the
> stage and lays out whatever it shows (a chart, a video, a table) in the room it has.
> The heading row is the title and aside, as a coaching card's; a panel with its own heading
> (tabs, a picker) passes it as `heading` and that replaces the title. The aside stays either
> way, at the right on the first line of the row — the place a panel says "reading…" or what
> its columns are. A panel whose aside names what it is showing may make it the control that
> changes it (`asideActive`: a ▾ after it, clicks raise asideClicked, `asideOn` while its picker
> is open) — the Charts panel's preset, so the heading is the selector, not said twice.
> The body CLIPS: whatever a panel draws stays inside its card, so a panel squeezed into the
> stage arrangement's side column is cut at the card's edge rather than spilling over the next.

- **Properties:** `color tone`, `string title`, `string aside`, `bool asideActive`, `bool asideOn`, `Component heading`, `int pad`, `int bodyGap`, `alias content`, `readonly int innerWidth`
- **Signals:** `asideClicked()`
- **Uses:** `PpCardShell`, `PpMicro`
- **Used by (1):** `PpModeStage`

### PpStarRating
`src/Gui/components/PpStarRating.qml` · 60 lines · root `Row`

> Reusable star rating — read-only on shot cards, interactive in the review
> panel, and the threshold picker in the filter. Tapping the star that equals
> the current value clears it (emits rated(0)); the host writes the new value
> back to the model (no state is kept here).

- **Properties:** `int value`, `int max`, `bool interactive`, `int starSize`, `color onColor`, `color offColor`
- **Signals:** `rated(int newValue)`
- **Used by (4):** `PpShotActionBar`, `PpShotCard`, `PpShotFilter`, `PpSwingEditPanel`
- **Exercised by tests/tools:** `tst_shot_card`

### PpStrengthMark
`src/Gui/components/PpStrengthMark.qml` · 59 lines · root `Column`

> How firmly something holds, as a signal-strength mark: 3 / 2 / 1 bars for firm / probably /
> possibly, the word under it. The bar count carries it, so colour is never the only channel.

- **Properties:** `string tier`, `color tone`, `readonly int bars`
- **Uses:** `PpMicro`
- **Used by (1):** `HmGoesTogether`

### PpSwingTimeline
`src/Gui/components/PpSwingTimeline.qml` · 89 lines · root `Item`

> Six stops through the swing, Address to Finish; where something first shows is marked and the
> track runs on from there in the tone. stopX(i) and trackY let a caller set words on the track.

- **Properties:** `int start`, `color tone`, `readonly var stops`, `readonly real inset`, `readonly real step`, `readonly int trackY`
- **Functions:** `stopX(i)`
- **Uses:** `PpMicro`
- **Used by (1):** `HmGoesTogether`

### PpTextField
`src/Gui/components/PpTextField.qml` · 56 lines · root `TextField`

Themed single-line text input (a styled TextField) with a hasError state.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `bool hasError`
- **Used by (17):** `AppearancePanel`, `ArchivingPanel`, `AthleteClubsSection`, `CamerasPanel`, `DiagnosticModel`, `GeneralPanel`, `ImusPanel`, `LaunchMonitorPanel`, `MetricLibrary`, `ModelCausePicker` (+7)

### PpToast
`src/Gui/components/PpToast.qml` · 156 lines · root `Rectangle`

> Transient snackbar — elevated surface fill with a strong border so it
> reads as a floating notice on any theme. Auto-hides on a Timer; the UNDO
> action is the host's to wire (it restores via the model invokable). Soft
> delete is reversible, so this replaces a modal confirm. Hosts can also set
> copyText to expose a copy-to-clipboard action.

- **Properties:** `string message`, `string glyph`, `bool showUndo`, `string copyText`, `bool sticky`, `string severity`
- **Signals:** `undoClicked()`
- **Functions:** `show(msg)`
- **Uses:** `PpPressable`
- **Used by (5):** `CamerasPanel`, `DiagnosticModel`, `PpDataViewer`, `PpShotCarousel`, `ScreenResourceMonitor`

### PpToolPill
`src/Gui/components/PpToolPill.qml` · 125 lines · root `Rectangle`

> PpToolPill — THE standard toolbar item: a glyph tile, a micro label, the current
> value and a ▾ affordance, with the shared hover/press motion. Was an inline
> `component ViewPill` private to PpSessionToolbar; extracted here so surfaces
> OUTSIDE the toolbar can present the same control rather than a lookalike that
> drifts. Every toolbar pill — View, Motion, Club, Cameras, IMUs — is this
> component.
> Anchoring convention for the popup a pill owns (see PpSessionToolbar):
> parent: <pill>;  y: <pill>.height + Theme.sp(10);  x: <pill>.width - width
> Parenting to the pill is what makes the popup track it — positioning against a
> coordinate computed in some ancestor's space silently misaligns.

- **Properties:** `string label`, `bool active`, `string glyph`, `string microLabel`, `bool badge`, `color badgeColor`
- **Signals:** `clicked()`
- **Used by (1):** `PpSessionToolbar`

### PpTopoBackground
`src/Gui/components/PpTopoBackground.qml` · 245 lines · root `Item` · no direct `Theme.*` refs · 4 hard-coded colour(s)

Animated topographic contour-line background (noise-driven contours in three tone colours), used behind home, session-mode and placeholder screens.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `color colorLow`, `color colorMid`, `color colorHigh`, `color backgroundColor`, `bool fillBackground`, `real levels`, `real lineWidth`, `real noiseScale`, `real intensity`, `real speed`, `bool animated`, `real hoverLift`, `real hoverRadius`, `real rippleRadius` … (+13 more)
- **Functions:** `ripple(nx, ny)`
- **Used by (3):** `ScreenHome`, `ScreenPlaceholder`, `ScreenSessionMode`

### PpTrace
`src/Gui/components/PpTrace.qml` · 51 lines · root `Item`

> Declarative polyline trace — used for the shot card's IMU-only fallback and
> the review panel's chart. `points` is a list of normalised (0..1) points
> from the model's tracePoints role; the binding below is a pure scale
> transform to item coordinates (no Canvas, no imperative drawing).

- **Properties:** `var points`, `color strokeColor`
- **Used by (1):** `PpShotCard`

### PpTypePill
`src/Gui/components/PpTypePill.qml` · 63 lines · root `Rectangle`

> PpTypePill — what KIND of thing this page is about, beside its title.
> The diagnostics library is six views over five kinds of object, and its detail pages hide the
> switcher bar: drill into anything and the page is a big title over stacked sections, which is
> the same shape whether you are looking at a characteristic, a measure or a metric. The reader
> who arrived by following a link two pages back has nothing on screen telling them which.
> OUTLINE, not filled, and that is the whole design. The filled capsules under these titles are
> ATTRIBUTES of the thing — its group, its status, its tier — and a sixth filled capsule saying
> "MEASURE" would read as a seventh attribute rather than as the class the other six belong to.
> An outline reads as a frame around the subject, which is what it is.
> Deliberately not `PpPill`: the design system reserves that name for the status pill
> (neutral/live/rec/warn/good, with the blinking REC dot), and two components differing only in
> what they mean is how a design system stops being one.

- **Properties:** `string label`
- **Used by (1):** `MetricDetail`

### PpUnitToggle
`src/Gui/components/PpUnitToggle.qml` · 88 lines · root `Row`

Compact segmented toggle between units (e.g. cm/ft-in, kg/lb). Emits selectionChanged.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `var units`, `string selected`
- **Signals:** `selectionChanged(string unit)`
- **Used by (2):** `GeneralPanel`, `ScreenAthleteForm`
