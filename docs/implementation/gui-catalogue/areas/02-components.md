# Shared components

*Directory:* `src/Gui/components/`  
*46 components · 4,890 lines of QML*

The app-wide primitives: buttons, chips, cards, pills, badges, toasts and the coaching-card shell (PpCard family). The intended single home for anything reused by more than one area.

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [ClubFormat](#clubformat) | 38 | `QtObject` | 8 | 0 | singleton |
| [PpBadge](#ppbadge) | 166 | `Rectangle` | 10 | 0 |  |
| [PpBandBar](#ppbandbar) | 103 | `Item` | 1 | 0 |  |
| [PpButton](#ppbutton) | 112 | `Rectangle` | 34 | 1 |  |
| [PpCard](#ppcard) | 202 | `Item` | 6 | 2 |  |
| [PpCardNote](#ppcardnote) | 32 | `Text` | 17 | 0 |  |
| [PpCardShell](#ppcardshell) | 84 | `Item` | 5 | 0 |  |
| [PpCheckRow](#ppcheckrow) | 68 | `Item` | 2 | 1 |  |
| [PpChip](#ppchip) | 63 | `Rectangle` | 12 | 0 |  |
| [PpChipGroup](#ppchipgroup) | 92 | `Row` | 5 | 1 |  |
| [PpComboBox](#ppcombobox) | 135 | `ComboBox` | 16 | 0 |  |
| [PpConfirm](#ppconfirm) | 159 | `Popup` | 2 | 1 |  |
| [PpConnectingFrame](#ppconnectingframe) | 117 | `Item` | 4 | 0 |  |
| [PpDeviceFilterBar](#ppdevicefilterbar) | 113 | `Flow` | 2 | 3 |  |
| [PpDeviceTags](#ppdevicetags) | 49 | `Row` | 2 | 1 |  |
| [PpDisplayText](#ppdisplaytext) | 102 | `Item` | 24 | 0 |  |
| [PpDivider](#ppdivider) | 32 | `Rectangle` | 18 | 0 |  |
| [PpExpandRow](#ppexpandrow) | 178 | `Item` | 3 | 1 |  |
| [PpFact](#ppfact) | 63 | `Item` | 1 | 1 |  |
| [PpFilePicker](#ppfilepicker) | 391 | `Popup` | 1 | 3 | 1 local comp. |
| [PpFlowIndicator](#ppflowindicator) | 444 | `Item` | 1 | 0 |  |
| [PpInset](#ppinset) | 62 | `Item` | 3 | 0 |  |
| [PpLink](#pplink) | 51 | `Text` | 9 | 0 |  |
| [PpMeter](#ppmeter) | 49 | `Row` | 2 | 0 |  |
| [PpMetricSwatches](#ppmetricswatches) | 74 | `Flow` | 1 | 1 |  |
| [PpMicro](#ppmicro) | 31 | `Text` | 39 | 0 |  |
| [PpNotificationHost](#ppnotificationhost) | 158 | `ListView` | 1 | 1 |  |
| [PpPip](#pppip) | 55 | `Item` | 2 | 0 |  |
| [PpPips](#pppips) | 59 | `Row` | 2 | 1 |  |
| [PpPopoverCard](#pppopovercard) | 29 | `PpCardShell` | 12 | 1 |  |
| [PpPressable](#pppressable) | 97 | `MouseArea` | 66 | 0 |  |
| [PpQualityPill](#ppqualitypill) | 77 | `Rectangle` | 2 | 1 |  |
| [PpRowAction](#pprowaction) | 74 | `Rectangle` | 2 | 1 |  |
| [PpSegmentedControl](#ppsegmentedcontrol) | 83 | `Rectangle` | 8 | 0 |  |
| [PpStageCard](#ppstagecard) | 110 | `Item` | 1 | 2 |  |
| [PpStarRating](#ppstarrating) | 60 | `Row` | 4 | 0 |  |
| [PpStrengthMark](#ppstrengthmark) | 59 | `Column` | 1 | 1 |  |
| [PpSwingTimeline](#ppswingtimeline) | 89 | `Item` | 1 | 1 |  |
| [PpTableRow](#pptablerow) | 146 | `Item` | 4 | 0 |  |
| [PpTextField](#pptextfield) | 56 | `TextField` | 19 | 0 |  |
| [PpToast](#pptoast) | 156 | `Rectangle` | 6 | 1 |  |
| [PpToolPill](#pptoolpill) | 125 | `Rectangle` | 1 | 0 |  |
| [PpTopoBackground](#pptopobackground) | 245 | `Item` | 1 | 0 |  |
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
`src/Gui/components/PpBadge.qml` · 166 lines · root `Rectangle`

> A leading badge: a round mark at the head of a coaching line, a shape for each meaning so
> colour is never the only channel.
> check        a tick, green: something done well, an ideal to aim for
> target       a ring and dot, amber: something to work on
> easing       a down arrow, green: a fault that is fading
> unconfirmed  a dashed ring and dot, grey and untinted: placed on evidence no recent session
> could renew
> dot          a filled 10-px dot, accent: a strength (a table row's mark)
> ring         a 2-px ring, amber: a fault (a table row's mark)
> Every kind but unconfirmed sits on a faint fill of its tone, unless `plain` drops it: a table
> row's bare ✓, dot or ring. Only the kind's own glyph is loaded, so a badge draws exactly what its
> inline original drew.

- **Properties:** `string kind`, `color tone`, `int size`, `bool plain`, `readonly int markSize`
- **Used by (10):** `HmFocus`, `HmSwingSummary`, `PpCameraFrame`, `PpCameraPanel`, `PpChartSummary`, `PpImuPanel`, `PpQualityPill`, `PpSessionToolbar`, `PpShotFilter`, `PpSwingEditPanel`
- **Exercised by tests/tools:** `components_gallery`, `tst_pp_badge`

### PpBandBar
`src/Gui/components/PpBandBar.qml` · 103 lines · root `Item`

> A band bar: counts by category as one stacked horizontal bar, each category's share of the
> total in its tone, 1 px apart, on a Theme.bandBarH track. An area row passes its habit counts:
> segments: [{ count: habits,     tone: Theme.bandTone("habit") },
> { count: occasional, tone: Theme.bandTone("occasional") },
> { count: clean,      tone: Theme.bandTone("clean") },
> { count: unclear,    tone: Theme.bandTone("unclear") }]
> Segments are drawn in the order given, a zero (or missing, or negative) count drawing nothing.
> A count that is there is never rounded away: every drawn segment is at least `minSegment` wide,
> and the rest of the width is shared by count. With nothing to count the bar is a plain track.
> The ends are rounded; the joins are square.
> The bar says how many in proportion only: the caller prints the count beside it ("3 habits"),
> so colour is never the only channel.

- **Properties:** `var segments`, `int gap`, `int minSegment`, `readonly int total`, `readonly var pieces`, `readonly real r`
- **Used by (1):** `HmAreaRow`
- **Exercised by tests/tools:** `components_gallery`, `tst_band_bar`

### PpButton
`src/Gui/components/PpButton.qml` · 112 lines · root `Rectangle` · 3 hard-coded colour(s)

The app's standard button: label and/or glyph/icon, with primary, destructive and attention variants. Built on PpPressable. Emits clicked.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `string label`, `string glyph`, `Component icon`, `real iconSize`, `bool primary`, `bool destructive`, `bool attention`, `readonly color contentColor`
- **Signals:** `clicked()`
- **Uses:** `PpPressable`
- **Used by (34):** `AppearancePanel`, `ArchivingPanel`, `ArmCalibrationStatus`, `AthleteClubsSection`, `BallPage`, `CalibrateTrunkPage`, `CameraCalibrationFlow`, `CamerasPanel`, `DiagnosticModel`, `GeneralPanel` (+24)

### PpCard
`src/Gui/components/PpCard.qml` · 202 lines · root `Item`

> A card, in one of two looks.
> THE COACHING CARD (the default): the shell, a mono title in the card's tone, an optional aside on
> the title's baseline at the right, and a column of content under them, padded and spaced. The
> aside right-aligns over a column `asideWidth` wide when it is narrower than that, so a
> "SESSIONS" heading sits over the pips it heads rather than at the card's edge.
> THE TABLE CARD (`table: true`, the §2.5 visual language): a hairline box on a colorBg2 ground
> (Theme.tableRadius, no tone rule — PpCardShell is not used, so its defaults and Theme.cardRule
> stay the session panels'), full-bleed rows separated by colorHairline hairlines.
> title / aside   optional; when set they take a caption-height band of their own at the top
> columns         optional column template, [{ title, width | fill, align }]:
> width   pixels (the caller sp()s it, as for any width)
> fill    true, or a weight: shares what the fixed columns leave
> align   "left" (default) · "right" · "center"
> Any titled column draws a caption row (Theme.tableHeaderH, quiet sentence-case
> captions, a hairline under it).
> children        rows (PpTableRow, PpExpandRow, or anything else), stacked with no gap; a
> …

- **Properties:** `color tone`, `string title`, `string aside`, `int asideWidth`, `int pad`, `int itemGap`, `int bodyGap`, `alias content`, `readonly int innerWidth`, `readonly alias titleItem`, `bool table`, `var columns`, `int columnGap`, `readonly bool isTableCard` … (+9 more)
- **Functions:** `resolveColumns(cols)`
- **Uses:** `PpCardShell`, `PpMicro`
- **Used by (6):** `HmFocus`, `HmGoesTogether`, `HmSwingAreas`, `HmSwingSummary`, `PpSessionReportList`, `ScreenHome`
- **Exercised by tests/tools:** `components_gallery`, `tst_table_card`

### PpCardNote
`src/Gui/components/PpCardNote.qml` · 32 lines · root `Text`

> A card with nothing in it says so in one quiet line, so the card keeps its shape.

- **Used by (17):** `PpCameraFrame`, `PpCameraPanel`, `PpCameraTiles`, `PpClubPanel`, `PpDataViewer`, `PpExportOptionsSheet`, `PpImuPanel`, `PpLaunchMonitorPanel`, `PpMarkupPanel`, `PpMetricChart` (+7)

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
- **Used by (5):** `PpCard`, `PpChartsBody`, `PpPopoverCard`, `PpSessionDiagnosticsBody`, `PpStageCard`

### PpCheckRow
`src/Gui/components/PpCheckRow.qml` · 68 lines · root `Item`

> A square checkbox and its words, the whole row pressable: the export sheet's camera and JSON rows
> (PpExportOptionsSheet), the Sessions screen's "Show archived (N)". Press-dip only, no hover-grow:
> the row is the target, and a growing full-width row would nudge its neighbours' edges.

- **Properties:** `string label`, `bool checked`
- **Signals:** `toggled()`
- **Uses:** `PpPressable`
- **Used by (2):** `PpExportOptionsSheet`, `ScreenSessions`

### PpChip
`src/Gui/components/PpChip.qml` · 63 lines · root `Rectangle`

> A pill of tiny mono words in a tone, or any words a caller gives it ("NEXT", "last seen 4 Jul").
> Untinted it is a grey outline, for something stated without weight. With no words it takes no
> room.
> Given a `trend` it is the trend tag: "↑ growing" in the fault coral, "↓ easing" in the accent,
> set in the body face, semibold, on a tinted pill with no outline. Only the trend case looks that
> way; every chip that sets its own words keeps the mono pill.

- **Properties:** `int trend`, `string text`, `color tone`, `bool tinted`, `real tracking`, `readonly bool isTrend`
- **Used by (12):** `HmAreaRow`, `HmFocus`, `HmGoesTogether`, `HmSwingSummary`, `PpCameraFrame`, `PpCameraPanel`, `PpDataViewer`, `PpDeviceTags`, `PpImuPanel`, `PpShotCarousel` (+2)
- **Exercised by tests/tools:** `components_gallery`, `tst_pp_chip`

### PpChipGroup
`src/Gui/components/PpChipGroup.qml` · 92 lines · root `Row` · 2 hard-coded colour(s)

Single-select row of text chips (options array, selected value, optional disabled options). Emits selectionChanged, and blockedClicked when a disabled chip is pressed.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `var options`, `string selected`, `var disabledOptions`
- **Signals:** `selectionChanged(string value)`, `blockedClicked(string value)`
- **Uses:** `PpPressable`
- **Used by (5):** `AppearancePanel`, `DisplaysPanel`, `GeneralPanel`, `ScreenAthleteForm`, `StudioPanel`

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
- **Used by (16):** `ArchivingPanel`, `AthleteClubsSection`, `AudioPage`, `CamerasPanel`, `CapturePage`, `DisplaysPanel`, `FilmPage`, `GeneralPanel`, `LaunchMonitorPanel`, `PpMarkupPanel` (+6)

### PpConfirm
`src/Gui/components/PpConfirm.qml` · 159 lines · root `Popup`

> The house confirmation prompt, as the session-active close interception in Main.qml draws it:
> modal, centred, dimmed, Esc cancels, display-font title, right-aligned outlined primary followed by
> a neutral Cancel. Written once here because this panel raises two of them and two hand-drawn copies
> of one standard drift apart.
> `tone` is the ONE thing the two callers differ on, and the difference is meant. Theme.colorAttention
> is defined as a call-to-action frame — "draws the eye to a row/control that needs the user to act" —
> and the close prompt wears it because it interrupts something live, not because it destroys
> anything. A prompt that discards saved work wears the error family instead, which is how the app
> already draws a destructive action: removing something is a write to the user's pack, and is
> styled as one. Two different events must not read alike.

- **Properties:** `string title`, `string body`, `string confirmText`, `string cancelText`, `string tone`
- **Signals:** `confirmed()`, `cancelled()`
- **Uses:** `PpPressable`
- **Used by (2):** `DiagnosticModel`, `PpSessionReportList`

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

### PpDeviceFilterBar
`src/Gui/components/PpDeviceFilterBar.qml` · 113 lines · root `Flow` · 1 hard-coded colour(s)

> "RECORDED WITH" — the device filter over a list of sessions: one chip per device any saved session
> used, on whichever DeviceFilterModel it is given (the carousel's session drawer; the golfer's sessions
> on Home and the golfer page). Multi-select, AND: two chips on = sessions that had both. Each chip's
> count is what adding it would leave. Tap again to drop one; Clear drops them all. Hidden when no saved
> session says what recorded it.
> showCount puts "3 of 12" after Clear while the filter is on, for a list with no header of its own to
> say it in.

- **Properties:** `var model`, `bool showCount`, `readonly bool active`
- **Uses:** `PpLink`, `PpMicro`, `PpPressable`
- **Used by (2):** `PpSessionDrawer`, `ScreenSessions`

### PpDeviceTags
`src/Gui/components/PpDeviceTags.qml` · 49 lines · root `Row`

> What recorded a session — one PpChip per device, grey outlines, the ones the device filter asked for
> (PpDeviceFilterBar) in the accent. A row of any session list: the carousel's drawer, the golfer's
> sessions on Home and the golfer page.

- **Properties:** `var tokens`, `var labels`, `var selected`
- **Uses:** `PpChip`
- **Used by (2):** `PpSessionDrawer`, `PpSessionsTableRow`

### PpDisplayText
`src/Gui/components/PpDisplayText.qml` · 102 lines · root `Item`

> Large display title with the brand warm→cool gradient fill (the site's
> signature). Falls back to a flat Text when Theme.gradientTitlesActive is
> false (reduce-motion / user preference). Sizes to its text, but tracks an
> externally-assigned width (Layout.fillWidth / explicit width) so wrapping
> titles wrap to the available space — implicitWidth/Height still report the
> natural glyph geometry so plain (un-stretched) placement is unchanged.

- **Properties:** `alias text`, `int pixelSize`, `string fontFamily`, `int fontWeight`, `bool italic`, `real letterSpacing`, `color flatColor`, `int horizontalAlignment`, `alias wrapMode`, `alias lineHeight`, `alias elide`, `alias maximumLineCount`
- **Used by (24):** `AppearancePanel`, `ArchivingPanel`, `ArmCalibrationStatus`, `CalibrateTrunkPage`, `CameraCalibrationFlow`, `CamerasPanel`, `CheckArmPage`, `CheckTrunkPage`, `DiagnosticModel`, `DisplaysPanel` (+14)

### PpDivider
`src/Gui/components/PpDivider.qml` · 32 lines · root `Rectangle`

Hairline rule, horizontal or vertical, in the theme's border colour.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `int orientation`
- **Used by (18):** `AppearancePanel`, `ArchivingPanel`, `CamerasPanel`, `DisplaysPanel`, `GeneralPanel`, `ImusPanel`, `LaunchMonitorPanel`, `MetricDetail`, `MetricLibrary`, `MicrophonesPanel` (+8)

### PpExpandRow
`src/Gui/components/PpExpandRow.qml` · 178 lines · root `Item`

> An expand row (the §2.5 visual language): a PpTableRow with a drawn chevron that turns 90° when
> the row opens, and a detail block revealed under it in place.
> expanded      open or closed
> toggled()     the user asked to flip it: a click, Space / Return / Enter with the row focused,
> or an assistive technology's press. With `autoToggle` (the default) the row flips
> `expanded` itself first; a caller that binds `expanded` (one row open at a time)
> sets autoToggle: false and answers toggled() itself, so its binding survives
> cells         the header's cells, as PpTableRow's (the default property)
> detail        a Component, loaded under the header when the row is created and shown while
> expanded; `detailIndent` / `detailRightInset` narrow it
> chevronSide   "left" (in the row's left padding, the area table's) or "right" (at the row's
> right edge, in a trailing column of its own: Home's fault rows); `chevronCentreY` centres it on a line of the
> header's own rather than the header's middle
> Keyboard-operable: the row takes focus on Tab, shows a focus ring, and toggles on Space, Return
> or Enter. Accessible.Button, its expanded state exposed as checked (QML has no expanded state of
> its own) and in the description.
> …

- **Properties:** `bool expanded`, `bool autoToggle`, `Component detail`, `real detailIndent`, `real detailRightInset`, `string chevronSide`, `bool showChevron`, `real chevronCentreY`, `string accessibleName`, `string pressObjectName`, `real rowHeight`, `alias secondLine`, `alias padH`, `alias columnOffset` … (+9 more)
- **Signals:** `toggled()`
- **Functions:** `columnX(i)`, `columnWidth(i)`, `activate()`
- **Uses:** `PpTableRow`
- **Used by (3):** `HmAreaRow`, `HmFocus`, `HmSwingSummary`
- **Exercised by tests/tools:** `components_gallery`, `tst_expand_row`

### PpFact
`src/Gui/components/PpFact.qml` · 63 lines · root `Item`

> A label and its value, in an opened row: a micro label in a fixed column, then the value's
> words. Marks placed inside it (pips, a key) go in a row before the words; beside them the label
> sits on their centre, otherwise on the words' first baseline. Empty, it takes no room.

- **Properties:** `string label`, `string text`, `int labelWidth`, `alias extra`
- **Uses:** `PpMicro`
- **Used by (1):** `HmFaultDetail`

### PpFilePicker
`src/Gui/components/PpFilePicker.qml` · 391 lines · root `Popup` · 2 hard-coded colour(s)

> THE IN-APP FILE PICKER — a sheet in the app's own card style for choosing one file, because this
> application opens no native dialogs (no FileDialog, no FolderDialog: every action is an in-screen
> control). Built for the studio's logo and the coach's photo (session report §7.1), and generic:
> the caller sets `title`, `nameFilters` and where it starts, and listens for chosen(path).
> ┌ CHOOSE A LOGO ────────────────────────────────────── ✕ ┐
> │ Home  Desktop  Downloads  Pictures        ↑ Up one level │
> │ /Users/mark/Pictures                                     │
> │ ┌ folders, then matching files ┐ ┌ preview ───────────┐ │
> │ │ ▸ Holidays                    │ │                    │ │
> │ │   logo.png            12 KB   │ │      (image)       │ │
> │ └───────────────────────────────┘ └ logo.png · 12 KB ──┘ │
> │                                     Cancel   [Choose]    │
> └──────────────────────────────────────────────────────────┘
> Press a folder to open it; press a file to select it (a second press, or Choose, picks it).
> Probes drive it through goTo(path), select(path) and choose(); the controls carry objectNames.

- **Properties:** `string title`, `var nameFilters`, `string hint`, `bool previewImages`, `string startFolder`, `string selectedPath`, `readonly string currentFolder`, `readonly var roots`
- **Signals:** `chosen(string path)`, `cancelled()`
- **Functions:** `goTo(path)`, `up()`, `select(path)`, `choose()`
- **Local components:** `PlainLink`
- **Uses:** `PpButton`, `PpMicro`, `PpPopoverCard`
- **Used by (1):** `StudioPanel`

### PpFlowIndicator
`src/Gui/components/PpFlowIndicator.qml` · 444 lines · root `Item` · 1 hard-coded colour(s)

> PpFlowIndicator — the progress indicator of a multi-step flow (session_wizard_refactor_design.md
> §4.9). It replaces the session wizard's tab strip, and is generic: it knows nothing of session
> setup, so the toolbar calibration and the trunk ceremony can reuse it (`compact`).
> SESSION      CAMERAS                      SENSORS                              READY
> ●───────────●──────────●──────────◉━━━━━━━○──────────○──────────○──────────○
> Focus ✓     Cameras ✓  Ball ⚠      IMUs       Calibrate   Check       Ready
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
- **Used by (3):** `HmFaultDetail`, `PpChartSummary`, `PpSwingEditPanel`

### PpLink
`src/Gui/components/PpLink.qml` · 51 lines · root `Text` · 1 hard-coded colour(s)

> A text link in the accent: words that go somewhere. It brightens under the pointer where
> `hoverTint` is on. A caller with a larger press target of its own sets `interactive: false`
> and binds `hovered` to it; `pressMargin` grows the link's own target past its words.

- **Properties:** `bool interactive`, `bool hoverTint`, `bool hovered`, `int pressMargin`
- **Signals:** `clicked(var mouse)`
- **Used by (9):** `HmFaultDetail`, `HmRigCapability`, `PpDeviceFilterBar`, `PpReportSheet`, `PpSessionsSelectionBar`, `PpSessionsTableRow`, `PpShotFilter`, `PpSwingEditPanel`, `StudioPanel`

### PpMeter
`src/Gui/components/PpMeter.qml` · 49 lines · root `Row`

> How often, as ten segments: a share of 0..1 lights round(share × 10) of them in the tone; the
> rest are Theme.colorMeterUnlit. The segment count carries the reading, so colour is never the
> only channel. Geometry is the theme's (meterSegW × meterSegH, meterSegGap apart, meterSegRadius
> corners) — a segment is part of the mark, so its gap scales with sp(), not with gap().

- **Properties:** `real share`, `color tone`, `readonly int filled`
- **Used by (2):** `HmFocus`, `HmSwingSummary`
- **Exercised by tests/tools:** `components_gallery`, `tst_pp_meter`

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

- **Used by (39):** `HmFaultDetail`, `HmFocus`, `HmSwingAreas`, `HmSwingSummary`, `PpCameraFrame`, `PpCameraPanel`, `PpCameraTiles`, `PpCard`, `PpChartSummary`, `PpChartsBody` (+29)
- **Exercised by tests/tools:** `components_gallery`

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
- **Used by (2):** `HmFaultDetail`, `PpPips`

### PpPips
`src/Gui/components/PpPips.qml` · 59 lines · root `Row`

> A row of session pips, oldest first. `marks` are the summary's seen/unseen bools; `ticks` are
> PpTickRun's { state } and win when there are any. `maxPips` keeps the latest N of them (0 = all),
> so a caller no longer slices its own; a card that lines its rows up still sizes its pip column to
> the widest row. Size and gap are the theme's pipSize / pipGap.

- **Properties:** `var marks`, `var ticks`, `color tone`, `color cleanTone`, `int size`, `int gap`, `int maxPips`, `readonly bool useTicks`, `readonly var shown`
- **Uses:** `PpPip`
- **Used by (2):** `HmFocus`, `HmSwingSummary`
- **Exercised by tests/tools:** `components_gallery`, `tst_pp_pips`

### PpPopoverCard
`src/Gui/components/PpPopoverCard.qml` · 29 lines · root `PpCardShell`

> A popup's background in the card family: the standard shell with a stronger hairline, so it
> lifts off whatever it is drawn over, and a quiet rule. For a Popup's `background:`.

- **Uses:** `PpCardShell`
- **Used by (12):** `PpAnnotationToolbar`, `PpDataViewer`, `PpExportOptionsSheet`, `PpFilePicker`, `PpMarkupPanel`, `PpMetricChart`, `PpReportSheet`, `PpSessionDrawer`, `PpSessionRowMenu`, `PpSessionToolbar` (+2)

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
- **Used by (66):** `AppearancePanel`, `AthleteClubsSection`, `CamerasPanel`, `DiagnosticModel`, `GeneralPanel`, `ImusPage`, `ImusPanel`, `IntentPage`, `Main`, `MetricDetail` (+56)

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

### PpRowAction
`src/Gui/components/PpRowAction.qml` · 74 lines · root `Rectangle`

> A row's hover action: a small chip of micro caps words that appears on the row under the pointer
> (or the one with focus) and does that row's one obvious thing — the swing model's "Graph"
> (ModelTable), the Sessions table's "Review" (PpSessionsTableRow). colorBg3 at rest, the accent
> and colorAccentLight under the pointer; the shortcut rides in the tooltip, so the mouse path
> teaches the keyboard one.
> Where it sits and when it shows are the caller's: set x/anchors and `visible`. It takes no press
> while hidden.

- **Properties:** `string text`, `string tooltip`, `readonly bool hovered`
- **Signals:** `clicked()`
- **Uses:** `PpPressable`
- **Used by (2):** `ModelTable`, `PpSessionsTableRow`

### PpSegmentedControl
`src/Gui/components/PpSegmentedControl.qml` · 83 lines · root `Rectangle` · 2 hard-coded colour(s)

> Segmented "pill" selector: one recessed capsule track, equal-width segments,
> the active one filled. Used for the View popup's layout presets. Width-driven
> (fills its parent); set `options` (string list) and `selected`.

- **Properties:** `var options`, `string selected`, `bool solid`
- **Signals:** `activated(string value)`
- **Used by (8):** `DiagnosticModel`, `PpDataViewer`, `PpMarkupPanel`, `PpMetricChart`, `PpSessionToolbar`, `ScreenSessions`, `StudioPanel`, `SwingViz3DView`

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

### PpTableRow
`src/Gui/components/PpTableRow.qml` · 146 lines · root `Item`

> A table row (the §2.5 visual language): a fixed-height row laid out on its card's column template.
> height       Theme.rowH, or Theme.rowH2 with `secondLine`; a caller whose content wraps may set
> its own height
> padding      Theme.rowPadH each side (`padH`); the cells live in the box between
> hover        a colorBg2 wash (colorBg3 on a table card, whose ground is already colorBg2), drawn
> under the cells; `highlighted` holds it on (PpExpandRow's press area owns the
> pointer, so it says when the row is hovered)
> cells        the row's children, in column order: cell i takes column i + `columnOffset` of the
> template — its x by the column's align, centred vertically (`centreCells`). A cell
> keeps its own width; bind it to columnWidth(i) to fill or elide in its column (and
> give a right-aligned Text horizontalAlignment: Text.AlignRight). A cell with no
> column (no template, or more cells than columns) is left where the caller put it.
> Cells are the direct children: lay a repeated cell out inside one Item, not with a
> Repeater in the row.
> The template is the nearest PpCard { table: true } up the parent chain (`card`, or set it), so
> the rows and the card's captions share one set of column edges. `layout` may be set instead: a
> …

- **Properties:** `bool secondLine`, `int padH`, `int columnOffset`, `bool centreCells`, `bool topRule`, `bool hoverEnabled`, `bool highlighted`, `int hoverBleed`, `color hoverColor`, `var card`, `var layout`, `bool roundTop`, `bool roundBottom`, `readonly bool hovered` … (+3 more)
- **Functions:** `columnX(i)`, `columnWidth(i)`
- **Used by (4):** `HmFocus`, `HmSwingSummary`, `PpExpandRow`, `PpSessionsTableRow`
- **Exercised by tests/tools:** `components_gallery`, `tst_table_card`, `tst_table_row`

### PpTextField
`src/Gui/components/PpTextField.qml` · 56 lines · root `TextField`

Themed single-line text input (a styled TextField) with a hasError state.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `bool hasError`
- **Used by (19):** `AppearancePanel`, `ArchivingPanel`, `AthleteClubsSection`, `CamerasPanel`, `DiagnosticModel`, `GeneralPanel`, `ImusPanel`, `LaunchMonitorPanel`, `MetricLibrary`, `ModelCausePicker` (+9)

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
- **Used by (6):** `CamerasPanel`, `DiagnosticModel`, `PpDataViewer`, `PpShotCarousel`, `ScreenResourceMonitor`, `ScreenSessions`

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
- **Used by (1):** `ScreenHome`

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
