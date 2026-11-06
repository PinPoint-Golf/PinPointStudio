# Home

*Directory:* `src/Gui/home/`  
*13 components · 3,831 lines of QML*

The home screen: onboarding, athlete/club/session-type selection, the coaching tabs (focus, your swing, matchups) with faults opening in place, and PPCP phone pairing/offers.

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [HmAreaRow](#hmarearow) | 361 | `PpExpandRow` | 1 | 5 |  |
| [HmFaultDetail](#hmfaultdetail) | 162 | `Column` | 3 | 5 |  |
| [HmFocus](#hmfocus) | 401 | `Item` | 1 | 10 | 3 local comp. |
| [HmGoesTogether](#hmgoestogether) | 239 | `Column` | 1 | 4 |  |
| [HmHomeTabs](#hmhometabs) | 156 | `Column` | 1 | 5 |  |
| [HmRigCapability](#hmrigcapability) | 233 | `Column` | 1 | 6 |  |
| [HmSwingAreas](#hmswingareas) | 182 | `Column` | 1 | 3 |  |
| [HmSwingSummary](#hmswingsummary) | 401 | `Column` | 1 | 10 | 3 local comp. |
| [PpQrGlyph](#ppqrglyph) | 126 | `Canvas` | 1 | 0 |  |
| [PpcpGuidedPairDialog](#ppcpguidedpairdialog) | 377 | `Popup` | 0 | 1 | ⚠ unreferenced |
| [PpcpOfferList](#ppcpofferlist) | 215 | `Column` | 1 | 1 |  |
| [PpcpPairDialog](#ppcppairdialog) | 383 | `Popup` | 1 | 3 |  |
| [ScreenHome](#screenhome) | 595 | `Item` | 1 | 9 |  |

### HmAreaRow
`src/Gui/home/HmAreaRow.qml` · 422 lines · root `PpExpandRow`

> One area of "Your swing" (HmSwingAreas): a PpExpandRow on the area table's column template.
> header   chevron · area · habits (a PpBandBar of the four counts and "N habits") · trend ·
> consistency ("Loose P6-P7", the word alone when Steady, "-" with no judgement) ·
> biggest fault · work-ons ("1 active · 1 new")
> detail   the area's spans and their words on one line (wrapped on a narrow table), when it has
> any; then every one of the area's items at Theme.itemIndent, one Theme.itemRowH row each: the
> condition, how many swings it fired on of those that assessed it, the typical value
> with its unit and corridor, the grade word, and an action — Practise on an active or
> easing work-on, a "New" tag on a newly listed one, nothing otherwise. Every item opens
> in place (HmFaultDetail, its chevron at the right), one at a time.
> `area` is one of WorkOnsController.areaItems (swing_areas.h AreaSummary, every string final):
> { group, label, measurable, measured, habits, occasional, clean, unclear, habitTrend, hasTrend,
> topFault, topFaultId, workOnsActive, workOnsNew, items: [{ conditionId, label, golfer, band,
> share, fired, assessable, measureId, hasTypical, typical, corridorLo, corridorHi,
> corridorShape, tail, unit, grade, workOn, sessionsListed }],
> consistencyWord, consistencyLoosest, consistencySpans: [{ span, word }] }
> The consistency keys are patched in after the catch-up, so a row reads "-" until they land.
> An area the rig cannot measure says so in place of the bar, and does not open: no chevron, no
> press, no focus.
> Colour is never the only channel: the bar carries the count beside it, the trend its sign, the
> …

- **Properties:** `var area`, `var controller`, `string openItem`, `readonly string group`, `readonly string areaLabel`, `readonly bool measurable`, `readonly var items`, `readonly int habits`, `readonly string habitsText`, `readonly bool hasTrend`, `readonly int trend`, `readonly string trendText`, `readonly string faultText`, `readonly int newCount` … (+5 more)
- **Signals:** `practiseRequested(string conditionId, string label)`, `reviewSessionRequested(string sessionDir)`
- **Functions:** `faultFor(conditionId)`, `count(key, signed)`, `figure(v)`, `withUnit(s, unit)`, `corridorText(it)`, `typicalText(it)`, `practisable(it)`, `itemLayout(w)`
- **Uses:** `HmFaultDetail`, `PpBandBar`, `PpButton`, `PpChip`, `PpExpandRow`
- **Used by (1):** `HmSwingAreas`

### HmFaultDetail
`src/Gui/home/HmFaultDetail.qml` · 162 lines · root `Column`

> A FAULT, OPENED — the detail under a fault row on Home, in place (Mark, 28 Oct: a fault opens where
> it is, never on another page). Used by Coaching's focus ("Right now") and "Next on your list", and
> by an open area's items on Your swing:
> what it costs   the pack's consequence
> Now · Latest    where it stands, and the newest session that judged it
> Swings          fired of every swing, and where it could not be measured
> Sessions        the pips' key, counted over every session
> Linked          what it may follow from
> Try             the drill, where the pack authors one
> Review the … session →
> `item` is WorkOnsController.faultDetail(conditionId): any condition a session measured, a listed
> work-on or not (Mark, 28 Oct: every fault on Home opens). Every string is the controller's. This
> file positions and paints.

- **Properties:** `var item`, `readonly color tone`, `readonly color toneClean`, `readonly int pipSize`
- **Signals:** `reviewSessionRequested(string sessionDir)`
- **Functions:** `tickCount(ticks, state)`, `str(key)`
- **Uses:** `PpFact`, `PpInset`, `PpLink`, `PpMicro`, `PpPip`
- **Used by (3):** `HmAreaRow`, `HmFocus`, `HmSwingSummary`

### HmFocus
`src/Gui/home/HmFocus.qml` · 401 lines · root `Item`

> YOUR FOCUS — the one thing to practise, first on Home's Coaching tab, as a table card in the
> §2.5 visual language (docs/design/ux_refactor_away_from_4_modes/03_home_coaching.png):
> header      focus mark · YOUR FOCUS · the drill's title · the short reason · Practise
> captions    Aim for            | Right now      How often                     Sessions
> rows        ✓ the ideal        | the fault      meter · frequency words + trend   pips
> expand row  ▸ Why it matters · how to practise   (closed: the two paragraphs inside)
> The aims and the faults share one row grid, split by a hairline: each AIM FOR line faces the
> RIGHT NOW line it answers, and a row is as tall as the taller of the two. Where one list is
> longer, the other side of its extra rows is left empty.
> Every string is WorkOnsController's (swingSummaryView's focus, docs/design/home_themes_design.md):
> focusItem { present, title, aimFor [string], rightNow [{ text, share, frequency, trend,
> sessions: [bool…] }], why, practiseLabel, practise, reason, reasonShort,
> focusConditionId }
> The title is the drill's label (for a fault with no drill, its ideal); practiseLabel repeats it
> and is not drawn twice. Not drawn when present is false — nothing on the list yet. Positions and
> paints only; Practise is passed up (practiseRequested) for the screen to open the wizard with.

- **Properties:** `var controller`, `int gridGap`, `string openId`, `readonly var focusItem`, `readonly var aimFor`, `readonly var rightNow`, `readonly string conditionId`, `readonly string reasonShort`, `readonly bool hasWhyHow`, `readonly color tone`, `readonly color toneGood`, `readonly color toneFault`, `readonly int maxPips`, `readonly int markW`
- **Signals:** `practiseRequested(string conditionId, string label)`, `reviewSessionRequested(string sessionDir)`
- **Functions:** `faultFor(conditionId)`, `capitalised(s)`
- **Local components:** `Caption`, `Line`, `Prose`
- **Uses:** `HmFaultDetail`, `PpBadge`, `PpButton`, `PpCard`, `PpChip`, `PpExpandRow`, `PpMeter`, `PpMicro`, `PpPips`, `PpTableRow`
- **Used by (1):** `HmSwingSummary`

### HmGoesTogether
`src/Gui/home/HmGoesTogether.qml` · 239 lines · root `Column`

> MATCHUPS — the swing themes (docs/design/home_themes_design.md), the body of Home's Matchups tab
> (HmHomeTabs): today the faults that travel together; later also pairs that work together (Mark,
> 28 Oct). One card in YOUR SWING's family,
> a row per pair: a signal-strength mark for how firmly it holds, the two halves joined by a
> connector, and a six-stop swing timeline showing where the pair first shows.
> Every string is WorkOnsController's (swingSummaryView):
> togetherItems  [{ tier "firm"|"probably"|"possibly", first ("When …", or the one part),
> second ("" for one part), startStop 0..5 (−1 unplaced: no timeline),
> startWords, trend -1|0|1 }]
> togetherNote   "" or why nothing can be said yet

- **Properties:** `var controller`, `readonly var together`, `readonly string note`, `readonly color toneTogether`, `readonly int cardPad`, `readonly int cardGap`, `readonly int badgeSize`, `readonly int textIndent`, `readonly int headlineSize`, `readonly int itemGap`
- **Functions:** `capitalised(s)`
- **Uses:** `PpCard`, `PpChip`, `PpStrengthMark`, `PpSwingTimeline`
- **Used by (1):** `HmHomeTabs`

### HmHomeTabs
`src/Gui/home/HmHomeTabs.qml` · 156 lines · root `Column`

> Home's three views of the swing, under one title-size tab bar
> (docs/design/ux_refactor_away_from_4_modes.md §2.3, 03_home_coaching.png):
> Coaching     HmSwingSummary — what to do next, in the golfer's words (the default, D6)
> Your swing   HmSwingAreas — how the whole swing stands, area by area
> Matchups     HmGoesTogether — what travels together: today faults that move together; later
> also pairs that work together (Mark, 28 Oct)
> A fault on Coaching or Your swing opens its detail in place (HmFaultDetail); there is no page
> behind Home. The bar carries the evidence line (controller.summarySubtitle) straight after the
> tabs. The tab last chosen is remembered per athlete in appSettings.homeViewByAthlete[uuid].tab
> ("coaching" | "swing" | "matchups"); HmSwingAreas keeps its own keys in the same entry. The map is
> read once per write and a modified copy written back (a QVariantMap property read is a deep copy).
> Signals go up to ScreenHome: Practise, and an open fault's "Review the … session →".
> objectNames: homeTabs (this), homeTabBar (the PpTabs), homeTabCoaching, homeTabSwing and
> homeTabMatchups (the three pages).

- **Properties:** `var controller`, `var settings`, `string athleteUuid`, `readonly var tabKeys`, `string current`
- **Signals:** `practiseRequested(string conditionId, string label)`, `reviewSessionRequested(string sessionDir)`
- **Functions:** `select(key)`
- **Uses:** `HmGoesTogether`, `HmSwingAreas`, `HmSwingSummary`, `PpTabs`, `PpTypeset`
- **Used by (1):** `ScreenHome`
- **Exercised by tests/tools:** `tst_home_tabs`

### HmRigCapability
`src/Gui/home/HmRigCapability.qml` · 233 lines · root `Column` · 1 hard-coded colour(s)

> WHAT THIS RIG CAN MEASURE — the right pane of Home's Start card (ux_refactor_away_from_4_modes
> §2.3, A.1). One row per capability family, from the kit connected NOW (D10: cameras enabled and
> connected, sensor roles assigned, enabled and connected, the HackMotion connected, the launch
> monitor configured; no calibration gate). The rule is capability_facts.js in "home" mode — the
> wizard's closing page uses the same helper in "wizard" mode — and the rows and their words are
> setup_capability_rows.js, the ONE place that wording lives: describe, never instruct.
> Below a hairline, what today's DEVICES section carried that still belongs on Home: the PPCP pair
> entry (pairPhoneButton), the sessions a connected phone is offering (PpcpOfferList) and the
> first resource warning (RmWarningNotice).
> The device readings come through a SetupContext with no draft: it is the one object that reads
> cameraManager / imuManager / appSettings / launchMonitor with their reactive dependencies
> written once, so Home does not re-derive the role ladder.

- **Properties:** `readonly var facts`, `readonly var rows`
- **Signals:** `studioSettingsRequested()`
- **Functions:** `dotKind(state)`, `rowWords(r)`
- **Uses:** `PpLink`, `PpQrGlyph`, `PpcpOfferList`, `PpcpPairDialog`, `RmWarningNotice`, `SetupContext`
- **Used by (1):** `ScreenHome`

### HmSwingAreas
`src/Gui/home/HmSwingAreas.qml` · 196 lines · root `Column`

> YOUR SWING, as areas (docs/design/ux_refactor_away_from_4_modes.md §2.3, §5.2 D1): one table card,
> a row per area (HmAreaRow) rated by counting its habits, in the controller's display order
> (habits, then occasional, then the worst share; an area this rig cannot measure last).
> chevron · Area · Habits · Trend · Consistency · Biggest fault · Work-ons
> An open area lists every one of its conditions, and each opens its detail in place (HmAreaRow,
> HmFaultDetail).
> Paint-only: every row is WorkOnsController.areaItems. Before the first areas land it says it is
> reading; with no session to read it says "Nothing yet"; a recompute after that leaves the table
> standing and says nothing (HmSwingSummary's states).
> WHICH AREAS ARE OPEN is the golfer's, per athlete, in appSettings.homeViewByAthlete[uuid]:
> expanded       [group…]  the open areas, any number
> expandedSeen   bool      set by the first write; until then the weakest measurable area (the
> first measurable one, since areas arrive in display order) opens on
> first show. After it, a table the golfer closed stays closed.
> The map is read once per write and a modified copy written back (a QVariantMap property read is a
> deep copy); the entry's other keys (the tab, ScreenHome's) are kept. With no map on the settings
> …

- **Properties:** `var controller`, `var settings`, `string athleteUuid`, `readonly var areas`, `readonly bool ready`, `readonly bool catchingUp`, `var openGroups`
- **Signals:** `practiseRequested(string conditionId, string label)`, `reviewSessionRequested(string sessionDir)`
- **Functions:** `isOpen(group)`, `toggle(group)`, `sync()`
- **Uses:** `HmAreaRow`, `PpCard`, `PpMicro`
- **Used by (1):** `HmHomeTabs`
- **Exercised by tests/tools:** `tst_swing_areas`

### HmSwingSummary
`src/Gui/home/HmSwingSummary.qml` · 401 lines · root `Column`

> COACHING — the current athlete's swing as a short coaching page: the body of Home's Coaching tab
> (HmHomeTabs), in the §2.5 visual language (docs/design/ux_refactor_away_from_4_modes/
> 03_home_coaching.png; the golfer's rules are docs/design/home_themes_design.md's). Drawn for a
> typical golfer, to be LOOKED at before it is read, with one thing to the fore:
> Your focus         HmFocus: the one drill to practise, the ideal it aims at, the faults it
> covers, and — one click down — why it matters and how to practise it
> What you do well   a table card: a strength dot, the phrase with what backs it beneath, the
> ten-step meter of how often it holds, one pip per session
> Next on your list  a table card in the same grammar: a fault ring, the phrase with how often
> in words beneath (and a trend tag when there is one), the meter, the pips;
> the first wears a "Next" tag. A fault opens in place (HmFaultDetail): what
> it costs, where it stands, the drill, the way into its session
> No metrics, no figures, no P-positions, no claims about cause — a golfer reads this. Colour is
> never the only channel: the meter carries a count of segments, and each has words beside it.
> The evidence line ("From N swings over M sessions") and the way into every work-on ("All
> work-ons ▸") are the tab bar's, not this page's.
> …

- **Properties:** `var controller`, `string openId`, `readonly var doWell`, `readonly var needsWork`, `readonly bool ready`, `readonly bool updating`, `readonly bool catchingUp`, `readonly int maxPips`, `readonly bool stacked`, `readonly color toneWell`, `readonly color toneWork`, `readonly int cardGap`, `readonly int markW`, `readonly var columns`
- **Signals:** `practiseRequested(string conditionId, string label)`, `reviewSessionRequested(string sessionDir)`
- **Functions:** `faultFor(conditionId)`, `capitalised(s)`
- **Local components:** `Caption`, `CardHeader`, `Headline`
- **Uses:** `HmFaultDetail`, `HmFocus`, `PpBadge`, `PpCard`, `PpChip`, `PpExpandRow`, `PpMeter`, `PpMicro`, `PpPips`, `PpTableRow`
- **Used by (1):** `HmHomeTabs`

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
- **Used by (1):** `HmRigCapability`
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
- **Used by (1):** `HmRigCapability`

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
- **Used by (1):** `HmRigCapability`
- **Exercised by tests/tools:** `tst_ppcp_pair_dialog`

### ScreenHome
`src/Gui/home/ScreenHome.qml` · 595 lines · root `Item` · 1 hard-coded colour(s)

> HOME (ux_refactor_away_from_4_modes §2.3, Appendix A.1). No golfer yet: the first-run
> onboarding. A golfer selected, top to bottom:
> 0. The header — the identity row (avatar, name and details, Switch →) and the "PinPoint Studio"
> banner (D12, kept);
> 1. Start — one table card: left, a new session (Start session, the club, the last session);
> right, what this rig can measure (HmRigCapability), with the phone pairing entry, a phone's
> offered sessions and the first device warning;
> 2. HmHomeTabs — Coaching | Your swing | Matchups; a fault opens its detail in place.
> The golfer's sessions are the rail's Sessions (ScreenSessions), not repeated here.
> The contour background (PpTopoBackground) sits behind all of Home, first run and golfer alike.

- **Properties:** `string selectedClub`, `var athMap`, `readonly var clubModel`, `readonly bool hasGolfer`, `readonly var lastSession`, `readonly string lastSessionText`
- **Signals:** `addAthleteRequested()`, `athletePickerRequested()`, `editCurrentAthleteRequested()`, `startSessionRequested(var request)`, `reviewSessionRequested(string sessionDir)`, `studioSettingsRequested()`
- **Uses:** `ClubFormat`, `HmHomeTabs`, `HmRigCapability`, `PpButton`, `PpCard`, `PpComboBox`, `PpDisplayText`, `PpPressable`, `PpTopoBackground`
- **Used by (1):** `Main`
- **Exercised by tests/tools:** `setup_walk`
