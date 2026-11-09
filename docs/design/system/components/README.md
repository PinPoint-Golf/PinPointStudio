# Components

The shared Qt Quick controls in `src/Gui/components/`. Use these rather than redrawing one; each page lists its properties, the Theme tokens it reads and its states.


## Actions

- [PpButton](PpButton.md) — THE app's button: every button look is a `style` and every size a `size`, so a restyle is this one file.
- [PpLink](PpLink.md) — A text link in the accent: words that go somewhere.
- [PpMoreButton](PpMoreButton.md) — THE ⋯ tile that opens a row's menu: a square `radius` tile with a ⋯ in the data face, washed while hovered or open.
- [PpPressable](PpPressable.md) — The standard PinPoint hover-grow / press-dip motion as a drop-in replacement for a MouseArea: it drives its parent's `scale`.
- [PpRowAction](PpRowAction.md) — A row's hover action: a small chip of micro caps words that appears on the hovered row and does its one obvious thing.
- [PpScopedAction](PpScopedAction.md) — One tile of a device panel's scoped action row (Scan / Connect / Calibrate): a glyph over a short label, outlined at rest and attention-framed when `primary`.
- [PpToolPill](PpToolPill.md) — THE standard toolbar item: a glyph tile, a micro label, the current value and a ▾, with the shared hover/press motion.

## Inputs

- [PpCheckRow](PpCheckRow.md) — A square checkbox and its words, the whole row pressable.
- [PpComboBox](PpComboBox.md) — The shared dropdown selector: a thin styling layer over the Basic ComboBox so every drop-down shares one look and one height.
- [PpFilePicker](PpFilePicker.md) — THE in-app file picker: a modal sheet in the app's card style for choosing one file — the app opens no native dialogs.
- [PpSearchField](PpSearchField.md) — The app's search field: a PpTextField with a search's chrome — a box, or a ⌕ glyph over a rule.
- [PpSegmentedControl](PpSegmentedControl.md) — THE app's segmented selector: a row of joined choices, one active.
- [PpSwitch](PpSwitch.md) — The app's on/off switch: a 34 × 18 pill, accent when on, with a 12 px knob that slides across.
- [PpTabButton](PpTabButton.md) — One tab of a stock TabBar drawn the app's way: body-face label, `colorText` when checked and `colorText3` otherwise.
- [PpTextArea](PpTextArea.md) — A multi-line text field in PpTextField's look, with word wrap.
- [PpTextField](PpTextField.md) — The app's one-line text field: a surface box, radius, a 1 px border (strong at rest, warn on error, accent on focus), body type.

## Selection

- [PpChipGroup](PpChipGroup.md) — A row of `PpChoiceChip`s built from a list, one picked (or several with `multiSelect`).
- [PpChoiceChip](PpChoiceChip.md) — THE app's choice chip: one or two words the user picks, alone or one of a row — every chip look the app draws is a `style` of this one component.
- [PpMetricSwatches](PpMetricSwatches.md) — The metric palette as a row of round swatches — one per colour NAME, drawn in the current theme's value for it; picking one emits its name.
- [PpSelectTile](PpSelectTile.md) — A selectable tile: a card or row the user picks one of (a layout card, a preset, a club, an input device, an athlete).
- [PpStarRating](PpStarRating.md) — A row of ★ stars: read-only on shot cards, interactive in the review panel, and the threshold picker in the filter.

## Status

- [PpChip](PpChip.md) — A small pill of mono words in a tone ("NEXT", "last seen 4 Jul"), or — given a `trend` — the trend tag "↑ growing" / "↓ easing".
- [PpQualityPill](PpQualityPill.md) — A shot-quality score pill, its fill graded by quartile (red → rust → amber → green) via `Theme.qualityColor`.
- [PpStatusPill](PpStatusPill.md) — A status pill: a state said in a word or two ("Connected", "Complete", "✓  Enabled"), in its tone on the tone's wash with a hairline in the tone.

## Marks

- [PpBadge](PpBadge.md) — A leading round mark at the head of a coaching line or table row — one shape per meaning, so colour is never the only channel.
- [PpBandBar](PpBandBar.md) — Counts by category as one stacked horizontal bar: each category's share of the total in its tone, 1 px apart, rounded ends, square joins.
- [PpMeter](PpMeter.md) — How often, as ten segments: a share of 0..1 lights round(share × 10) of them in the tone.
- [PpPip](PpPip.md) — One session pip: a dot where a session saw something, an empty ring where it did not — or, in tick-run terms, a dot where it fired, a dash where it was clean, a ring where it could not tell.
- [PpPips](PpPips.md) — A row of session pips (`PpPip`), oldest first.
- [PpStrengthMark](PpStrengthMark.md) — How firmly something holds, as a signal-strength mark: 3 / 2 / 1 bars for firm / probably / possibly, with the word under it.
- [PpTrace](PpTrace.md) — A declarative polyline trace drawn from normalised (0..1) points — the shot card's IMU-only fallback and the review panel's chart line.
- [PpVariabilityBadge](PpVariabilityBadge.md) — A variability mark and its word, Icon or Cell: the word's line as the golfer layer's tooltip, the grader's basis string after it on the technical layer, "Checking" for a measurement-limited grade.
- [PpVariabilityFunnel](PpVariabilityFunnel.md) — The spread funnel: a measure's spread across sessions as a band that narrows as the golfer tightens, labelled with the grade and the trend ("Repeatable · ↑ Tightening").
- [PpVariabilityMark](PpVariabilityMark.md) — How much something varies, as a grade in geometry: Dispersion (scalar) and Overlay (time series) for consistency, the Needle for measurement quality, and the two ungraded typicality variants (this swing against your usual).

## Cards

- [PpCard](PpCard.md) — A card in one of two looks: the coaching card (shell, mono title in the card's tone, an aside, a padded column of content) or the table card (a hairline box of full-bleed rows on a shared column template).
- [PpCardShell](PpCardShell.md) — The coaching card's background and nothing else: the surface, a hairline, and a rule in the card's tone along the top edge that follows the rounded corners.
- [PpInset](PpInset.md) — Something set into a card: a faintly darker panel with a hairline for the part a reader acts on (a drill, what to try); with `bar`, a short tone bar down its left edge marks it as the card's own.
- [PpPopoverCard](PpPopoverCard.md) — A popup's background in the card family — the standard shell with a stronger hairline so it lifts off what it is drawn over; for a Popup's `background:`.
- [PpStageCard](PpStageCard.md) — A card for a session stage's panel: the coaching card's shell, tighter padding, and a body that fills (and clips to) the card rather than sizing it.

## Rows & tables

- [PpDivider](PpDivider.md) — A hairline rule — horizontal by default, vertical on request.
- [PpExpandRow](PpExpandRow.md) — A PpTableRow with a drawn chevron that turns 90° when the row opens, revealing a detail block in place under it.
- [PpFact](PpFact.md) — A label and its value in an opened row: a micro label in a fixed column, then the value's words (marks such as pips may sit before them).
- [PpFactStrip](PpFactStrip.md) — A strip of read-only facts across the foot of a Settings device card (cameras, IMUs, phones): equal-width cells, each a micro caps key over its value.
- [PpTableRow](PpTableRow.md) — A fixed-height table row (§2.5 visual language) laid out on its table card's column template.

## Menus

- [PpMenuItem](PpMenuItem.md) — One line of a popup menu: a 34 px hover row the menu's width, with its words, an optional quiet note, a leading glyph and a right-hand count; inactive or destructive.
- [PpMenuRule](PpMenuRule.md) — The hairline between groups of PpMenuItem lines in a popup menu — it sets the destructive line apart in its own group.

## Text

- [PpCaption](PpCaption.md) — A caption: the quiet sentence-case line a table card heads its columns with, and a row's own quiet words under its headline.
- [PpCardNote](PpCardNote.md) — The app's quiet body line: an empty card saying so in one line, a page saying what it is for, an empty list saying why.
- [PpDisplayText](PpDisplayText.md) — A large display title filled with the brand's warm→cool gradient sweep, falling back to flat ink when gradient titles are off.
- [PpMicro](PpMicro.md) — A micro heading: the app's caps micro type, quiet by default — the eyebrow over a page or group, a field's head, a pill's small label, a card's title.
- [PpPageHeader](PpPageHeader.md) — A page's head: its eyebrow, its display title and the quiet line that says what the page is for.

## Feedback

- [PpConfirm](PpConfirm.md) — The house confirmation prompt: modal, centred, dimmed, Esc cancels, display-font title, a right-aligned outlined primary followed by a neutral Cancel.
- [PpConnectingFrame](PpConnectingFrame.md) — A travelling-light "connecting" frame overlaid on a button: a dim track around the rounded border with one bright comet of stroke chasing round it while a connection is in flight.
- [PpNotice](PpNotice.md) — A notice: words in a tone, on the tone's wash inside a hairline in the tone.
- [PpNotificationCard](PpNotificationCard.md) — The notice card both notification surfaces draw: an elevated fill with a hairline in the notice's tone, a glyph, the words, and actions after hairlines.
- [PpNotificationHost](PpNotificationHost.md) — The window's notification centre: a bottom-up stack of PpNotificationCards, one per live notice in the `NotificationCenter` model.
- [PpToast](PpToast.md) — A transient snackbar — PpNotificationCard in its `toast` style — that places itself, centred, and hides itself on a timer.

## Devices

- [PpAvatar](PpAvatar.md) — An athlete's initials in a small circle, in that athlete's own colour.
- [PpDeviceCard](PpDeviceCard.md) — The frame of one device card in Settings (cameras, IMUs, phones): the surface, a state-coloured border drawn over everything, and a 54 px header row that opens with a status dot.
- [PpDeviceFilterBar](PpDeviceFilterBar.md) — "RECORDED WITH" — the device filter over a list of sessions: one filter chip per device any saved session used, multi-select with AND semantics.
- [PpDeviceTags](PpDeviceTags.md) — What recorded a session — one PpChip per device: grey outlines, and the ones the device filter asked for in the accent.

## Timeline

- [PpFlowIndicator](PpFlowIndicator.md) — The progress indicator of a multi-step flow: a row of numbered pips under group labels, joined by connectors, over a thin progress bar with "N of M done".
- [PpSwingTimeline](PpSwingTimeline.md) — Six stops through the swing, Address to Finish; where something first shows is marked, and the track runs on from there in the tone.

## Backgrounds

- [PpTopoBackground](PpTopoBackground.md) — A theme-reactive, slowly drifting topographic-contour background: contour lines of a noise terrain, coloured along the brand sweep, with hover lift, tap ripples and an occasional light sweep.

## Typeset

The typeset controls live in `src/Gui/typeset/` and serve both screen and print: each reads its fonts, inks and sizes from the panel's one PpTypeset (passed as `roles`), whose `medium` is "screen" or "print".

- [PpTypeset](PpTypeset.md) — The per-panel helper: the scale (`u()` size, `g()` space, `gutter()`), the medium, size classes and tiers, type roles, inks, marks and the figure helpers every block draws with.
- [PpBrush](PpBrush.md) — A register's shared focus state — picked and hovered shot, open condition, highlights, the after-shot pulse — so a thing is shown once in every block that holds it.
- [PpTabs](PpTabs.md) — The view toggles: label + quiet count, the current one ruled; an optional note and chip; Home's title-size bar.
- [PpViewFlick](PpViewFlick.md) — One register view's own named Flickable under the tab row, built on first show and never torn down.
- [PpInlinePicker](PpInlinePicker.md) — A choice that opens in place, never a pop-up: a quiet question, one row per choice, the clearing row.
- [PpStandfirst](PpStandfirst.md) — A view's head block: kicker, display head, a standfirst sentence from the data, and quiet filter links.
- [PpSectionHead](PpSectionHead.md) — THE typeset crosshead: a numbered or plain title with a quiet sub-line, count, caret or note; names the section print repeats.
- [PpNotes](PpNotes.md) — Small print: a footnote paragraph, optionally under a kicker and a short rule, held to 1.3 × the prose measure.
- [PpPageKey](PpPageKey.md) — The key: each mark drawn by the component the figures use, with its word — one per view on screen, one per page in print.
- [PpMark](PpMark.md) — THE point mark of every chart: one shot, fired (warn) or clean (ink3), in the theme's one shape, with the picked or hovered ring.
- [PpStub](PpStub.md) — The not-measured mark: a short flat ink3 bar on the foot of a figure or strip — present, never a gap.
- [PpShotStrip](PpShotStrip.md) — The session strip: a cell per shot in the order struck — warn bar, ink3 tick or stub — narrowing then wrapping, never dropping a shot.
- [PpChMatrix](PpChMatrix.md) — A table of short cells under one heavy rule: a lower triangle of figures, or named columns with a one-hue wash and a short form that never drops a column.
- [PpBoxPlot](PpBoxPlot.md) — THE box plot: middle half, median, whiskers, the shots, an optional corridor, and its values on the plot while examined.
- [PpBoxReading](PpBoxReading.md) — A stood-up box's full reading beside it — whisker ends, quartiles, median and count line — each ticked from its true height.
- [PpFigureScale](PpFigureScale.md) — The value axis a figure row's panels share: f → y, the zone edges named once, fault-forward bands behind drawn breaks.
- [PpZonePlot](PpZonePlot.md) — The ground under a value panel: corridor wash, zone edges and baseline through one PpFigureScale.
- [PpCarryWaterfall](PpCarryWaterfall.md) — Where the yards went on one shot: the usual, each part taken off it, the residual, the actual.
- [PpBreakSlash](PpBreakSlash.md) — The pair of slashes that marks a broken value axis, at each break.
- [PpPulseWash](PpPulseWash.md) — The after-shot pulse: one soft ink wash over a row whose condition changed on the shot just surfaced.
- [PpPager](PpPager.md) — Print: one document on A4, laid out once as a galley and cut into pages only between atoms, with repeated heads and no stranded pages.
- [PpRunningHead](PpRunningHead.md) — Print furniture at the head of a page: the title left, golfer · date · club · shot right, a rule under it.
- [PpFolio](PpFolio.md) — Print furniture at the foot of a page: the page's key, a hairline, the footer and "Section · Page n of N".

`ClubFormat` (a singleton that turns club ids into sentence-case labels) draws nothing and has no page. Nor do `src/Gui/typeset/figure_format.js` (the figure formatter, covered on the PpTypeset page) and `src/Gui/typeset/box_stats.h` (the box statistics, covered on the PpBoxPlot page).

The four variability vocabularies, `ConsistencyVocabulary`, `MeasurementVocabulary`, `TrendVocabulary` and `TypicalityVocabulary` (`src/Gui/components/`), draw nothing and have no page either. They are singletons that hold every variability word, line and accessible name; the PpVariability* components read them, and [variability.md](../variability.md#words) lists their words. The C++ singletons `Variability` (the constants a surface may need: `nMin`, `nWindow`, `trendMinSessions`) and `VariabilityGeometry` (the marks' shapes), and the `variabilityResult` value type, live in `src/Gui/variability/` and are covered on the PpVariabilityMark page.
