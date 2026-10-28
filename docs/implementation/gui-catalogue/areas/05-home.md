# Home

*Directory:* `src/Gui/home/`  
*11 components · 3,756 lines of QML when catalogued; `HmTypeCard` (the four session-type cards) has since been deleted — see `docs/design/ux_refactor_away_from_4_modes.md` §2.3*

The home screen: onboarding, athlete/club selection and the Start card, the 'your swing' summary (focus, work-ons, goes-together), swing-diagnostics page, and PPCP phone pairing/offers.

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [HmFocus](#hmfocus) | 355 | `Item` | 1 | 7 | 1 local comp. |
| [HmGoesTogether](#hmgoestogether) | 240 | `Column` | 1 | 4 |  |
| [HmSwingSummary](#hmswingsummary) | 428 | `Column` | 1 | 9 |  |
| [HmWorkOns](#hmworkons) | 459 | `Column` | 1 | 10 |  |
| [PpQrGlyph](#ppqrglyph) | 126 | `Canvas` | 1 | 0 |  |
| [PpcpGuidedPairDialog](#ppcpguidedpairdialog) | 377 | `Popup` | 0 | 1 | ⚠ unreferenced |
| [PpcpOfferList](#ppcpofferlist) | 215 | `Column` | 1 | 1 |  |
| [PpcpPairDialog](#ppcppairdialog) | 383 | `Popup` | 1 | 3 |  |
| [ScreenHome](#screenhome) | 782 | `Item` | 1 | 11 |  |
| [ScreenSwingDiagnostics](#screenswingdiagnostics) | 121 | `Item` | 1 | 4 |  |

### HmFocus
`src/Gui/home/HmFocus.qml` · 355 lines · root `Item`

> YOUR FOCUS — the one thing to practise, first on the home screen and drawn as its hero: the
> summary's card with a heavier amber rule, a faint amber wash on the surface and an amber
> hairline, and the drill's name as a display-size headline.
> AIM FOR          the ideal, ticked green            | RIGHT NOW   the faults it covers, each with
> |             the summary's meter, frequency
> |             words and session pips
> WHY IT MATTERS   a sentence or two
> HOW TO PRACTISE  the drill, set in
> why this one     one quiet line: the honest reason it was chosen
> The two columns sit on the same split as the two cards below the focus, so the page keeps one
> grid; each AIM FOR line faces the RIGHT NOW line it answers. Narrow, they stack.
> Every string is WorkOnsController's (swingSummaryView's focus, docs/design/home_themes_design.md):
> focusItem { present, title, aimFor [string], rightNow [{ text, share, frequency, trend,
> sessions: [bool…] }], why, practiseLabel, practise, reason }
> The title is the drill's label (for a fault with no drill, its ideal); practiseLabel repeats it
> and is not drawn twice. Not drawn when present is false — nothing on the list yet. Positions and
> …

- **Properties:** `var controller`, `int gridGap`, `readonly var focusItem`, `readonly var aimFor`, `readonly var rightNow`, `readonly color tone`, `readonly color toneGood`, `readonly int padX`, `readonly int padY`, `readonly int badgeSize`, `readonly int textIndent`, `readonly int innerWidth`, `readonly bool stacked`, `readonly real colW` … (+6 more)
- **Functions:** `latestPips(marks)`, `capitalised(s)`
- **Local components:** `Prose`
- **Uses:** `PpBadge`, `PpCardShell`, `PpChip`, `PpInset`, `PpMeter`, `PpMicro`, `PpPips`
- **Used by (1):** `HmSwingSummary`

### HmGoesTogether
`src/Gui/home/HmGoesTogether.qml` · 240 lines · root `Column`

> WHAT GOES TOGETHER — the swing themes (docs/design/home_themes_design.md), in the Swing
> diagnostics screen (ScreenSwingDiagnostics.qml) under the faults: one card in YOUR SWING's family,
> a row per pair: a signal-strength mark for how firmly it holds, the two halves joined by a
> connector, and a six-stop swing timeline showing where the pair first shows. It lived on the home
> screen until the home became focus-led; what moves together is for the golfer who wants the
> detail, and for their coach.
> Every string is WorkOnsController's (swingSummaryView):
> togetherItems  [{ tier "firm"|"probably"|"possibly", first ("When …", or the one part),
> second ("" for one part), startStop 0..5 (−1 unplaced: no timeline),
> startWords, trend -1|0|1 }]
> togetherNote   "" or why nothing can be said yet

- **Properties:** `var controller`, `readonly var together`, `readonly string note`, `readonly color toneTogether`, `readonly int cardPad`, `readonly int cardGap`, `readonly int badgeSize`, `readonly int textIndent`, `readonly int headlineSize`, `readonly int itemGap`
- **Functions:** `capitalised(s)`
- **Uses:** `PpCard`, `PpChip`, `PpStrengthMark`, `PpSwingTimeline`
- **Used by (1):** `ScreenSwingDiagnostics`

### HmSwingSummary
`src/Gui/home/HmSwingSummary.qml` · 428 lines · root `Column`

> YOUR SWING — the current athlete's swing as a short coaching page, on the home screen
> (docs/design/home_themes_design.md). Drawn for a typical golfer, to be LOOKED at before it is
> read, with one thing to the fore:
> Your focus         HmFocus: the one drill to practise, the ideal it aims at, the faults it
> covers, why it matters, how to practise it, and why it was chosen
> What you do well   green card: a tick, the phrase, what backs it, one pip per session
> Next on your list  amber card, quieter than the focus: what comes after it, in order, the
> first marked NEXT; each with the ten-step "how often" meter, a trend chip
> when there is one, one pip per session that saw it
> Swing diagnostics  one link row into the technical layer (ScreenSwingDiagnostics): every
> fault, how it was measured, and what goes together
> No metrics, no figures, no P-positions, no claims about cause — a golfer reads this. Colour is
> never the only channel: the meter carries a count of segments, and each has words beside it.
> Every string is WorkOnsController's (swingSummaryView over the pack's golfer phrases):
> summaryReady, summaryUpdating, updating, athleteDir, summarySubtitle
> focusItem      see HmFocus.qml
> …

- **Properties:** `var controller`, `readonly var doWell`, `readonly var needsWork`, `readonly string subtitle`, `readonly bool ready`, `readonly bool updating`, `readonly bool catchingUp`, `readonly int maxPips`, `readonly int pipCount`, `readonly bool stacked`, `readonly color toneWell`, `readonly color toneWork`, `readonly int cardPad`, `readonly int cardGap` … (+5 more)
- **Signals:** `practiseRequested(string conditionId, string label)`
- **Functions:** `latestPips(marks)`, `capitalised(s)`
- **Uses:** `HmFocus`, `PpBadge`, `PpCard`, `PpCardNote`, `PpChip`, `PpLink`, `PpMeter`, `PpMicro`, `PpPips`
- **Used by (1):** `ScreenHome`

### HmWorkOns
`src/Gui/home/HmWorkOns.qml` · 459 lines · root `Column`

> WORK ONS — the faults the current athlete keeps producing, by their technical names: the
> detailed layer under YOUR SWING (docs/design/work_ons_design.md), shown as FAULTS in the Swing
> diagnostics screen (ScreenSwingDiagnostics.qml). One card in the summary's own family
> (HmSwingSummary.qml): surface, hairline border, a 3 px rule in the card's tone, a mono heading in
> that tone, round badges, ten-segment meters, session pips.
> The card's tone is colorWarn, the coral between the summary's amber and colorError: these are
> the faults themselves, one level more technical than the home screen's golfer words, so they
> read as their own card without the alarm a red would raise. Nothing here is an alarm.
> A row, closed, is
> a status badge   active (a target, in the tone) · easing (a down arrow, colorGood) ·
> unconfirmed (a dashed ring, grey: placed on evidence no recent session
> could renew) — a shape each, so colour is never the only channel
> the name         the pack's label, the headline
> a chip           "↓ easing", or "last seen 4 Jul" for an unconfirmed one
> a meter          swingsFired of swingsTotal in ten segments, the count in words beside it
> session pips     one per session, oldest first, in PpTickRun's vocabulary: a dot where it
> …

- **Properties:** `var controller`, `int collapsedCount`, `string title`, `readonly var items`, `readonly bool updating`, `readonly int cleared`, `string openId`, `bool showAll`, `readonly bool catchingUp`, `readonly color tone`, `readonly color toneEasing`, `readonly color toneQuiet`, `readonly int cardPad`, `readonly int badgeSize` … (+13 more)
- **Signals:** `reviewSessionRequested(string sessionDir)`
- **Functions:** `latestTicks(ticks)`, `share(it)`, `tickCount(ticks, state)`
- **Uses:** `PpBadge`, `PpCardShell`, `PpChip`, `PpFact`, `PpInset`, `PpLink`, `PpMeter`, `PpMicro`, `PpPip`, `PpPips`
- **Used by (1):** `ScreenSwingDiagnostics`

### PpQrGlyph
`src/Gui/home/PpQrGlyph.qml` · 126 lines · root `Canvas`

> The "Pair to my phone" icon: a QR code that is deliberately not one.
> ⚠ IT IS UNSCANNABLE ON PURPOSE, AND THAT IS THE WHOLE DESIGN.  It has the
> three finder squares a reader looks for and nothing else a reader needs —
> no timing pattern, no format-information stripe, no alignment block, no
> error correction, no data.  A scanner that locks onto the finders finds
> nothing behind them and gives up, which is the correct outcome: an icon that
> decoded to something would be an icon that could be photographed and acted
> on, and this one says "pairing lives here" and nothing more.
> ⚠ AND THE PATTERN IS DRAWN ONCE.  `Math.random()` picks a seed at creation
> and a tiny LCG expands it, so every launch gets a different arrangement but a
> given icon holds still.  Seeding per-paint would make it flicker on every
> hover, which reads as a rendering fault rather than as decoration.
> Not a glyph in Theme.fontSymbol: fontSymbol resolves to Apple Symbols on
> macOS and Segoe UI Symbol on Windows, and neither has a QR or a phone
> character.  A drawn one is identical on all three platforms and needs no
> entry in Theme.symbolScale.

- **Properties:** `int modules`, `real density`, `color color`
- **Used by (1):** `ScreenHome`
- **Exercised by tests/tools:** `tst_ppcp_pair_dialog`

### PpcpGuidedPairDialog
`src/Gui/home/PpcpGuidedPairDialog.qml` · 377 lines · root `Popup` · ⚠ **no app file instantiates this**

> PPCP-RV §11 — RV-6 guided pairing, the screen a person actually reads.
> Work package H10.
> ⛔⛔ THIS FILE IS TWO OF THE PROTOCOL'S REQUIREMENTS AND THEY ARE NOT CODE.
> CR-01 §4 lists nine implementation traps and then adds: "two more that are
> UX, not code, and are MUSTs anyway".  Both of them live here, and an
> implementation can satisfy every byte of §11 and still fail them.
> (1) 11.7d — THE AFFIRMATIVE CONTROL IS NOT THE DEFAULT AND NOT WHERE A
> STRAY TAP LANDS, AND THE PROMPT ASKS WHETHER THE NUMBERS *MATCH*.
> "A dialogue whose default is *Continue* is a dialogue that
> authenticates whatever is on the other end."
> ⚠ THIS DELIBERATELY INVERTS THIS APPLICATION'S OWN FOOTER CONVENTION,
> AND THE INVERSION IS THE POINT.  Everywhere else — ModelMint,
> PpExportOptionsSheet, PpAboutDialog — the pattern is a right-aligned
> row with Cancel first and the affirmative last carrying `primary: true`.
> That is correct for "Export" and catastrophic here: it puts the button
> that authenticates a stranger exactly where the operator's thumb
> …

- **Properties:** `var controller`, `readonly var windows`, `readonly string phase`, `readonly string digits`, `readonly string message`, `readonly bool mayRetry`, `readonly bool offerCode`, `readonly bool comparing`, `readonly bool failed`, `readonly bool paired`, `readonly bool busy`
- **Signals:** `pairingCodeRequested()`
- **Uses:** `PpButton`

### PpcpOfferList
`src/Gui/home/PpcpOfferList.qml` · 215 lines · root `Column`

> The sessions a connected capture device is offering.  Work package H5.
> ⚠ THIS IS WHERE H3'S "File -> Import Session..." WENT.  This application has
> no menus and no native dialogs, and the user's intent was never to import a
> file: a connected device says what it holds and the host picks from the list
> (plan §9, 22 August 2026).  So there is no file picker here, no format
> chooser, and no "import" verb — one row per offered Session and one control on
> each, in the DEVICES area of the home screen where the device it came from is
> already listed.
> ⚠ AND IT DEGRADES TO NOTHING.  `ppcpOffers` is a context property installed
> only when libppcp is linked (H0: a build without it must still run), so every
> reference here is guarded.  A build with no PPCP shows no heading and no rows,
> not an empty section with a title.

- **Properties:** `var controller`, `readonly int offerCount`
- **Uses:** `PpButton`
- **Used by (1):** `ScreenHome`

### PpcpPairDialog
`src/Gui/home/PpcpPairDialog.qml` · 383 lines · root `Popup` · 3 hard-coded colour(s)

> "Pair to my phone" — PPCP-RV §4, the primary rendezvous path.  Work package
> H6, second pass.
> This replaces PpcpPairPanel.qml, which sat inline in the DEVICES area of the
> home screen and was visible whenever the listener was up — which, in a build
> with libppcp and OpenSSL, is always.  So every user saw a heading, an
> engineer's status line, a paragraph of narrative, a raw IP:port list and a
> table of 16-hex pairing handles, on every launch, whether or not they had a
> phone.  One button now opens this, and the button is the only thing the home
> screen carries.
> ⚠ A QML Popup AND NOT A NATIVE DIALOG.  The house rule is no menus and no
> native dialogs; an in-app modal over Overlay.overlay is neither, and it is
> what PpAboutDialog and the quit-confirm in Main.qml already are.
> ⚠ THE URI IS NEVER EXPOSED TO QML.  RV 4.4c and 7.2b forbid a payload
> reaching a log, a crash report or a diagnostic export, and a QML property
> carrying it is one console.warn away from all three.  `qrRows` — one string
> of '0'/'1' per module row — is the only form of the code that leaves C++.
> …

- **Properties:** `var controller`, `readonly bool showing`, `readonly bool connected`, `readonly int connectedCount`, `readonly string peerName`, `readonly int secondsLeft`, `readonly string countdownText`, `readonly int failureCount`, `readonly string failureText`, `readonly var stage`
- **Uses:** `PpButton`, `PpDivider`, `PpPressable`
- **Used by (1):** `ScreenHome`
- **Exercised by tests/tools:** `tst_ppcp_pair_dialog`

### ScreenHome
`src/Gui/home/ScreenHome.qml` · 782 lines · root `Item` · 4 hard-coded colour(s)

The home screen. First run: welcome/onboarding (add an athlete, connect a camera, pair IMUs, read the docs). Thereafter, as `docs/design/ux_refactor_away_from_4_modes.md` §2.3 lays it out: a Start card (Start session, club picker, last session, and what this rig can measure), the Coaching | Your swing tabs over the work-ons, and the session list. There are no session-type cards any more: every session starts the same way, with an optional focus.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `string selectedClub`, `var athMap`, `readonly var clubModel`, `readonly bool hasGolfer`, `readonly var lastSession`, `readonly string lastSessionText`
- **Signals:** `addAthleteRequested()`, `editCurrentAthleteRequested()`, `startSessionRequested(var request)`, `swingDiagnosticsRequested(string group)`, `studioSettingsRequested()`
- **Uses:** `ClubFormat`, `HmHomeTabs`, `HmRigCapability`, `PpButton`, `PpCard`, `PpComboBox`, `PpDisplayText`, `PpLink`, `PpSessionReportList`, `PpTopoBackground`
- **Used by (1):** `Main`
- **Exercised by tests/tools:** `setup_walk`

### ScreenSwingDiagnostics
`src/Gui/home/ScreenSwingDiagnostics.qml` · 121 lines · root `Item`

> SWING DIAGNOSTICS — the technical layer under YOUR SWING, reached from the home screen's
> "Swing diagnostics →" row: every fault by its technical name with how it was measured (the
> work-ons card, headed FAULTS), then what moves together. For the golfer who wants the detail,
> and for their coach. The home screen's column, on its own screen in the app's stack (Main.qml,
> screenSwingDiagnostics); "← Home" goes back to it.
> A fault row's "Review the … session →" link is passed up as reviewSessionRequested, which Main
> handles exactly as it did when the work-ons sat on the home screen.
> Every string is WorkOnsController's (the context property `workOns`): summarySubtitle for the
> line under the heading, items for the faults, togetherItems / togetherNote for what goes
> together. This file positions only.

- **Properties:** `var controller`, `readonly alias faults`, `readonly string subtitle`
- **Signals:** `backRequested()`, `reviewSessionRequested(string sessionDir)`
- **Functions:** `toTop()`
- **Uses:** `HmGoesTogether`, `HmWorkOns`, `PpLink`, `PpMicro`
- **Used by (1):** `Main`
