# Session report

*Directory:* `src/Gui/report/`  
*22 components · 6,542 lines of QML*

The printable session report (cover, profile, per-fault work-on pages, wrist/posture/delivery pages, launch-monitor appendix) and the Sessions screen's table, selection bar, row menu and bulk jobs, plus the mailer and stills C++ behind them.

**C++ in this area:** `ReportMailer`, `ReportStills`, `SessionReportListModel`, `SessionReportModel`, `SmtpClient`  (see [cpp-seam.md](../cpp-seam.md))

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [PpBallTiles](#ppballtiles) | 119 | `Column` | 1 | 0 |  |
| [PpDeliveryPage](#ppdeliverypage) | 306 | `Column` | 1 | 4 |  |
| [PpDrillCard](#ppdrillcard) | 70 | `Rectangle` | 1 | 0 |  |
| [PpLmAppendix](#pplmappendix) | 352 | `Column` | 1 | 1 | 2 local comp. |
| [PpNumbersTable](#ppnumberstable) | 214 | `Column` | 2 | 0 |  |
| [PpOrderPage](#pporderpage) | 270 | `Column` | 1 | 0 | 1 local comp. |
| [PpPosturePage](#ppposturepage) | 440 | `Column` | 1 | 4 |  |
| [PpReportAbout](#ppreportabout) | 149 | `Column` | 1 | 1 | 1 local comp. |
| [PpReportCover](#ppreportcover) | 268 | `Item` | 2 | 3 | 1 local comp. |
| [PpReportPrintHost](#ppreportprinthost) | 189 | `Item` | 1 | 2 |  |
| [PpReportProfile](#ppreportprofile) | 213 | `Column` | 2 | 1 |  |
| [PpReportSheet](#ppreportsheet) | 301 | `Popup` | 1 | 4 |  |
| [PpSessionBulkJobs](#ppsessionbulkjobs) | 308 | `QtObject` | 1 | 0 |  |
| [PpSessionReport](#ppsessionreport) | 446 | `Item` | 1 | 13 |  |
| [PpSessionReportList](#ppsessionreportlist) | 782 | `Column` | 1 | 7 |  |
| [PpSessionRowMenu](#ppsessionrowmenu) | 241 | `Popup` | 1 | 2 | 3 local comp. |
| [PpSessionsSelectionBar](#ppsessionsselectionbar) | 228 | `Rectangle` | 1 | 2 | 1 local comp. |
| [PpSessionsTableRow](#ppsessionstablerow) | 449 | `PpTableRow` | 1 | 5 |  |
| [PpStill](#ppstill) | 66 | `Column` | 2 | 0 |  |
| [PpSwingGrid](#ppswinggrid) | 341 | `Column` | 3 | 1 | 1 local comp. |
| [PpWorkOnPage](#ppworkonpage) | 293 | `Column` | 1 | 3 | 2 local comp. |
| [PpWristPage](#ppwristpage) | 497 | `Column` | 1 | 3 |  |

### PpBallTiles
`src/Gui/report/PpBallTiles.qml` · 119 lines · root `Column` · no direct `Theme.*` refs

> THE BALL — the cover's launch-monitor block (design §3 page 1, §3.1): each figure a typical value (the median)
> in large type with its range beneath. Only with a launch monitor (the map's `ball` is null without one, and the
> cover then draws nothing here: empty means absent).
> · one club: up to 8 figures, four to a row (the mock's look);
> · several clubs: medians across clubs mean nothing, so the block becomes a small table, one row per club
> (4 at most; the map words "N more clubs") and its first four figures, the full set being in the numbers.
> Widths are shares of the text width; a club column is sized from the widest club name.

- **Properties:** `QtObject ro`, `var ball`, `readonly var groups`, `readonly bool multi`, `readonly var tiles`, `readonly var heads`
- **Used by (1):** `PpReportCover`

### PpDeliveryPage
`src/Gui/report/PpDeliveryPage.qml` · 306 lines · root `Column` · no direct `Theme.*` refs

> CLUB DELIVERY — the Club delivery page of the session report (docs/design/club_delivery_chart_design.md §4.8, stage 4):
> the Charts Club delivery section's SESSION reading, printed. After the Posture and Wrist pages and before "Between now
> and next time"; absent when the map has no `delivery` (report_delivery.h: no swing of the session reads the swing
> plane). Blocks, in the Session view's order:
> · the head: the kicker, "Club delivery", the session's sentence;
> · "The plane at each position": one box per P-position, P1…P7, over the session's swings on one shared scale
> (PpChBoxCell → PpBoxPlot, the ONE box plot, in the swing plane curve's OWN metric colour), the median and ± half
> the middle half under each ("-" where nothing was read), then where it varies most. Under five swings with the
> curve: the sentence saying so, no boxes;
> · the usual set-up: address, shoulders, the hands' steepening;
> · side by side: "Plane and your ball flight" — the day's fit as a sentence (and the attack's when its own fit
> holds), the scatter (PpChScatter, the one scatter, in the Charts print roles: the day's swings with both readings,
> this session's in ink, the day's others faint; the fitted line only when the fit holds; its axis from the payload's
> ticks, as paper has no ticker) and its legend — and "Shot shape" (PpChShotShapeGrid, every swing; what it was read
> from, and the commonest shape);
> · "How far can I trust this?": the trust lines, open (paper has no fold), said once — in two columns across the full
> …

- **Properties:** `QtObject ro`, `var dl`, `readonly var m`, `readonly bool on`, `readonly var band`, `readonly var positions`, `readonly var usual`, `readonly var fl`, `readonly var sc`, `readonly var sh`, `readonly var trust`, `readonly real labelW`, `readonly real gap`, `readonly real shapeW`
- **Uses:** `PpChBoxCell`, `PpChRoles`, `PpChScatter`, `PpChShotShapeGrid`
- **Used by (1):** `PpSessionReport`

### PpDrillCard
`src/Gui/report/PpDrillCard.qml` · 70 lines · root `Rectangle` · no direct `Theme.*` refs

> A DRILL CARD — what to do (design §3 work-on pages, §5): the first drill in full (what you do, what it is for,
> and if it doesn't work), and at most one more as a compact "Also" (§9.1 rule 4: 1 in full + 1 "Also"; the pack
> may hold more, they are not printed). One atom: a card is never cut. Its height is its text's.
> The words are the pack's (drills.json); the labels ("Drill", "What it is for.", …) are the card's headings.

- **Properties:** `QtObject ro`, `var drill`, `var also`, `bool dense`, `bool atom`
- **Used by (1):** `PpWorkOnPage`

### PpLmAppendix
`src/Gui/report/PpLmAppendix.qml` · 352 lines · root `Column` · no direct `Theme.*` refs

> THE LAUNCH MONITOR APPENDIX — after the client pages, before the technical appendix. Three parts, each from a fresh
> page: (a) the launch monitor panel's Session figures for each club, as the appendix's headline — the panel's own
> blocks (PpLmGraphicsBody, embedded, Folio light), fed the map's `graphics`, which is the panel's model over this
> session; (b) every shot's readings, a Ball table and a Club table, grouped by club; (c) the summary statistics per
> club: shots, average, SD, lowest, highest, range.
> Every sentence is the map's (rule 7); only fixed headings are set here. The tables flow (rule 4): each is a
> printGroup whose first atom is its column heads, repeated on a page that opens inside it; a club's heading row keeps
> with its first shot. Columns are sized from the widest value in the session (rule 5); when the widest values do not
> fit the text width at the table figure size, the table drops to the small figure size rather than overflow.
> No launch monitor in the session: the map has no `lmAppendix` and nothing here is drawn.

- **Properties:** `QtObject ro`, `var lm`, `readonly var m`, `readonly var figures`, `readonly var graphics`
- **Local components:** `ShotTable`, `StatsTable`
- **Uses:** `PpLmGraphicsBody`
- **Used by (1):** `PpSessionReport`

### PpNumbersTable
`src/Gui/report/PpNumbersTable.qml` · 214 lines · root `Column` · no direct `Theme.*` refs

> BETWEEN NOW AND NEXT TIME — the practice steps (studio text, §7.1) and YOUR NUMBERS: every swing from the launch
> monitor, a Typical row (the median; one per club when there were several) and the Habits column (design §3 page 8).
> THE TABLE FLOWS (§9.1 rule 4: all swings, repeating its heads). It is a printGroup whose head is the column heads,
> so a page that opens inside it draws them again; each swing is an atom. Columns are sized from the widest value
> in the session (rule 5) and their heads wrap onto two lines rather than widen the table; the spare width is
> shared between the gaps. No launch monitor: the map has no `numbers` and only the practice steps remain (and
> with neither, the section is not drawn at all).
> `fresh`: the section starts a page (the report); false when the note carries its few swings on the cover.

- **Properties:** `QtObject ro`, `var practice`, `var numbers`, `bool fresh`, `readonly var steps`, `readonly var tab`, `readonly var cols`, `readonly var rows`, `readonly var typical`, `readonly bool habits`, `readonly var widths`
- **Functions:** `xOf(c)`, `align(c)`
- **Used by (2):** `PpReportCover`, `PpSessionReport`

### PpOrderPage
`src/Gui/report/PpOrderPage.qml` · 270 lines · root `Column` · no direct `Theme.*` refs

> WHY THIS ORDER — page 3 (design §4.2): the ordering rules as applied, each with what it did TODAY (generated
> from the work-ons' `reasons` in C++; nothing on this page is written here), the bars of the quantity that set the
> order, a swing timeline placing each work-on where it happens, then up to three short columns — Linked, not
> proven · Not on the list, and why · Worth checking next time — each drawn only when it has something in it
> (rule 6), the survivors sharing the width.
> Atoms: the head; each rule; each bar; the timeline with its caption; the columns as one block (they are capped:
> worth checking 5 per kind). The section may run on to a second page and says so.

- **Properties:** `QtObject ro`, `var rep`, `var host`, `readonly var ord`, `readonly var items`, `bool tight`, `readonly real goalW`, `readonly var lists`, `real pageH`, `readonly bool side`
- **Functions:** `gap(n)`
- **Local components:** `ColsRow`
- **Used by (1):** `PpSessionReport`

### PpPosturePage
`src/Gui/report/PpPosturePage.qml` · 440 lines · root `Column` · 1 hard-coded colour(s)

> YOUR POSTURE — the Posture page (docs/design/posture_chart_design.md §5.7, stage 3b): the Charts Posture section's
> SESSION reading, printed. After the work-on pages and before "Between now and next time": it is a client page about
> this session's swings, like the work-ons, and the back matter (practice and numbers, About, the appendices) follows it.
> · the head: the session's head line and its pattern line;
> · the session profile: one row per posture reading, P1…P8, the pattern's band as a wash in the reading's OWN metric
> colour (identity, not judgement), the fault line as a hairline, the session's box per position (PpChBoxCell →
> PpBoxPlot, the ONE box plot, with box_stats' numbers from the presenter), and under each column the median ("-"
> where nothing was read), how wide the middle half is, or — a median outside the band — the side it falls on,
> in warn and underlined (a word, never colour alone); a reading under a ⚠ keeps its figure and says the ⚠'s
> few words, and is never ringed; under trunk bend at P3–P5 a second, smaller figure — the back line, the session's
> median of it — with its own ⚠ words (decision 16 of session_posture.h: not matched, no band of its own);
> · at address (§7.4.2): each rig-sensitive address reading, the session's box against the good-player band, the
> golfer's earlier sessions from the same set-up, and the ⚠ "depends on where the camera stands" under each;
> · what moved, at the top and at impact;
> · the pattern by shot (a mark per shot, ringed where it is not the session's pattern), the departures as counts,
> how repeatable it is;
> …

- **Properties:** `QtObject ro`, `var po`, `readonly var m`, `readonly bool on`, `readonly var prof`, `readonly var rows`, `readonly var positions`, `readonly var moved`, `readonly var strip`, `readonly var marks`, `readonly var deps`, `readonly var trust`, `readonly real labelW`
- **Functions:** `fin(v)`
- **Uses:** `PpChBoxCell`, `PpChPostureFrame`, `PpChRoles`, `PpReportProfile`
- **Used by (1):** `PpSessionReport`

### PpReportAbout
`src/Gui/report/PpReportAbout.qml` · 149 lines · root `Column` · no direct `Theme.*` refs

> ABOUT THIS REPORT — the back page (design §3 page 9, §10 rule 7): the dates (§8.4), how we measured, what a
> target range is, what we could not check (said ONCE, here, in golfer words), the habits held back from today's
> list because their reading is in doubt (`held`, one line each), a short glossary, and the studio's sign-off.
> Every sentence is the map's; the labels are the page's headings. Each paragraph is an atom, so the page can run on.

- **Properties:** `QtObject ro`, `var rep`, `bool photos`, `readonly var a`, `readonly var brand`, `readonly var held`, `readonly real labelW`
- **Local components:** `Para`
- **Uses:** `PpReportCover`
- **Used by (1):** `PpSessionReport`

### PpReportCover
`src/Gui/report/PpReportCover.qml` · 268 lines · root `Item` · no direct `Theme.*` refs

> THE COVER — the session on one page (design §3, page 1). A golfer who reads nothing else reads this.
> THE ONE FITTED PAGE (§9.1 rule 3). Everything else in the report flows; the cover must stand alone, so it is
> fitted, not flowed: every list is hard-capped (3 strengths, 5 habits, 5 work-ons, 8 ball figures; several clubs:
> 4 clubs × 4 figures), every line stops at two lines and ends in an ellipsis (the full text is always inside:
> the work-on pages and the grid say it again), the coach's note is capped by the sheet at about 80 words and
> here at eight lines, and the space between the three columns and the ball block takes up whatever is left.
> So the cover is ONE atom exactly a page tall, with the ball block at its foot. If even within the caps the cover
> would overflow (a test proves it does not), the ball block leaves for a page of its own after the cover, rather
> than the columns being squeezed. Only Texts marked `fitted` may elide: tst_session_report fails any other.
> The masthead itself is furniture (PpSessionReport draws it above the pager's window); the cover leaves room under
> it (mastheadH) so a two-line studio name pushes the page down instead of colliding.
> The note (`kind` "note": fewer than 3 swings, §3.1) is the cover alone, flowed: the kicker, the name, the
> headline, the ball and the swings' numbers, on one page.

- **Properties:** `QtObject ro`, `var rep`, `var host`, `real pageH`, `real mastheadH`, `readonly bool isNote`, `readonly var brand`, `readonly var meta`, `readonly bool hasBall`, `readonly real gapToBall`, `readonly bool ballFits`
- **Local components:** `Mark`
- **Uses:** `PpBallTiles`, `PpNumbersTable`, `PpSwingGrid`
- **Used by (2):** `PpReportAbout`, `PpSessionReport`

### PpReportPrintHost
`src/Gui/report/PpReportPrintHost.qml` · 189 lines · root `Item` · no direct `Theme.*` refs · 1 hard-coded colour(s)

> THE SESSION REPORT'S PRINTER (docs/design/session_report_design.md §8.1; CONTRACTS §5). Instantiated
> once in Main.qml. It answers sessionReport.printRequested(report, path): a white A4 sheet (794 × 1123,
> 96 dpi logical) is made in the window, PpSessionReport is laid on it with the map, the technical
> appendix is fed from the report model's private diagnostics model (sessionReport.printModel — the same
> four reads diag_print_pdf.qml's feed() takes off the panel's body), and PpPdfPrinter prints it: client
> pages at 300 dpi, appendix pages at 200 (the sheet relays the register's printPageDpi), photographs as
> JPEG and everything else as a palette ("auto"). Then printFinished, and the sheet is destroyed.
> THE SHEET IS NEVER SEEN, AND NEVER WAITS FOR THE APP WINDOW. It is made here and moved at once into an
> OffscreenSheet — a window of its own that is never shown, laid out and drawn on demand — and the printer
> draws each page through it. A sheet left in the app window would be laid out and grabbed only when that
> window draws, and a window out of sight draws nothing (covered, minimised or on another Space on a Mac;
> minimised elsewhere): the print stalled until the app was looked at again. Where Qt Quick has no RHI
> (the offscreen test platform) the sheet stays here instead, outside the window's left edge and under
> everything, and is grabbed with grabToImage as before. Either way the OffscreenSheet keeps App Nap off
> until the print is done. Nothing here touches Theme — the register draws in Folio light from its own
> palette, whatever the app's theme is.
> …

- **Properties:** `Item sheet`, `string path`
- **Functions:** `start(report, path)`, `done(ok, path, pages, error)`
- **Uses:** `PpPdfPrinter`, `PpSessionReport`
- **Used by (1):** `Main`

### PpReportProfile
`src/Gui/report/PpReportProfile.qml` · 213 lines · root `Column` · no direct `Theme.*` refs

> THE REPORT'S PROFILE — one row per reading across P1…P8, printed (the session report's Posture and Wrist pages,
> PpPosturePage / PpWristPage; docs/design/session_report_design.md). Lifted out of PpPosturePage unchanged, so the
> Posture page draws exactly as it did; the Wrist page reads the same row keys (report_wrist.h maps the wrist rows onto
> them). Three kinds of pager atom, in one printGroup (its head, the column heads, repeats on a page that opens inside
> the list):
> · the column heads: the position tags (ink3 where no swing reached it) and their names;
> · one atom per reading: the label (the reading's own metric colour as a dot, its words, an optional style line —
> posture's trail-knee style — and the camera / source with the unit); the plot — the zero line (address), the
> band per position as a wash in the reading's OWN metric colour (identity, not judgement), the fault lines as
> warn hairlines where the row has them, the session's box per position (PpChBoxCell → PpBoxPlot, the ONE box
> plot, with box_stats' numbers from the presenter) on the row's own scale (yLo…yHi); the pole word (which way is
> plus); and under each column the median ("-" where nothing was read), then the side of the band it falls on in
> warn and underlined (a word, never colour alone) or how wide the middle half is, then a ⚠'s few words where the
> cell has them; under a cell with an alt reading (posture's back line under trunk bend at P3–P5), a second,
> smaller figure with its own ⚠ words;
> · the caption, kept with the last row.
> …

- **Properties:** `QtObject ro`, `QtObject roles`, `string prefix`, `var rows`, `var positions`, `string caption`, `bool on`, `readonly var keyKinds`, `string printGroup`, `readonly real labelW`, `readonly real poleW`, `readonly real plotX`, `readonly real plotW`, `readonly real colW` … (+1 more)
- **Functions:** `cx(i)`, `fin(v)`
- **Uses:** `PpChBoxCell`
- **Used by (2):** `PpPosturePage`, `PpWristPage`

### PpReportSheet
`src/Gui/report/PpReportSheet.qml` · 301 lines · root `Popup`

> THE REPORT SHEET (docs/design/session_report_design.md §8.1) — Make report / Remake report open it. An
> in-app sheet over the window, in the popover card's family; no native dialog.
> the coach's note     optional, prefilled from this session's last report, about 80 words at most —
> it sits on the cover, which has room for that and no more
> the photo line       whether swing photos go in, and the reason when they do not ("Nick is under 18 —
> no parent or guardian approval recorded"), linked to the golfer's page
> missing details      each as a link to where it is entered ("No studio name yet — add it in
> Settings → Studio")
> Make the PDF         sessionReport.make(dir, {note}); then "Page 4 of 10" while it prints, and on
> made() either close (the row shows the new state) or the error, in place
> The technical appendix is always included (Mark, 20 October), so there is no option for it; the sheet
> says so in one quiet line. Everything said here comes from sessionReport.preflight(dir) — the sheet
> makes no judgement of its own about what the report can hold.

- **Properties:** `var reports`, `string greeting`, `string sessionDir`, `var sessionRow`, `bool thenEmail`, `var preflight`, `string error`, `bool requested`, `readonly int maxWords`, `readonly int words`, `readonly bool busyHere`, `readonly bool working`, `readonly bool canReport`, `readonly var photos` … (+2 more)
- **Signals:** `studioSettingsRequested()`, `golferDetailsRequested()`, `makeStarted(string dir, bool thenEmail)`
- **Functions:** `openFor(dir, row, thenEmail)`, `make()`
- **Uses:** `PpButton`, `PpLink`, `PpMicro`, `PpPopoverCard`
- **Used by (1):** `PpSessionReportList`

### PpSessionBulkJobs
`src/Gui/report/PpSessionBulkJobs.qml` · 308 lines · root `QtObject` · no direct `Theme.*` refs

> THE SESSIONS SCREEN'S BULK JOBS (docs/design/sessions_screen_redesign.md §2.4, §2.6, §8) — one queue
> over a list of session dirs, for one verb at a time:
> make      sessionReport.make(dir, {note: the last note})               → made(dir, ok, pdf, error)
> send      make first when the PDF is missing or out of date, then ONE email per session (S-D3):
> reportMailer.send(…) → sendFinished(dir, ok, method, error), or the mail-app hand-off
> (compose) until sending is set up; each recorded with sessionReport.recordSend(dir, method)
> export    swingExporter.exportShots(swingDirsForSession(dir), videos, json) → exportFinished(ok, zip, error);
> one ~/<session>.zip each
> archive   archiveController.archiveSession(dir)   } no ok signal: the queue waits for `busy` to fall,
> restore   archiveController.restoreSession(dir)   } then reads `status` ("… was not archived: why")
> trash     sessionReviewController.trashSession(dir) → bool (the library's .pinpoint-trash)
> Every backend is a single-job controller already, so this runs STRICTLY ONE ITEM AT A TIME, newest
> session first. cancel() stops BETWEEN items: the one in flight finishes (the exporter has no cancel,
> and half an archive is worse than a whole one). A row that cannot take the verb is SKIPPED, counted
> apart from the failures: make/send — not canReport, live or archived; archive — archived already or
> live; restore — not archived; trash and export — live. The live session is never queued to be
> …

- **Properties:** `var reports`, `var mailer`, `var exporter`, `var archiver`, `var review`, `var listModel`, `var rowFor`, `var compose`, `bool running`, `string verb`, `int done`, `int total`, `int skipped`, `string current` … (+5 more)
- **Signals:** `finished(string verb, int okCount, int failCount)`
- **Functions:** `canTake(verb, dir, f)`, `start(verb, dirs, options)`, `cancel()`
- **Used by (1):** `PpSessionReportList`
- **Exercised by tests/tools:** `tst_sessions_bulk`

### PpSessionReport
`src/Gui/report/PpSessionReport.qml` · 446 lines · root `Item` · 1 hard-coded colour(s)

> THE SESSION REPORT REGISTER (docs/design/session_report_design.md §3, §9; the map it binds is specified in
> build/report-build/REPORT_MAP.md). A PDF a coach sends a golfer: the client pages, the launch monitor appendix
> (PpLmAppendix, when the session has readings; it is part of the galley), then the technical appendix.
> ONE GALLEY, CUT BY THE LIBRARY PAGER (§9.1 rule 1). Every block below is laid out once at the text width and
> PpPager cuts it into A4 pages between atoms. Nothing has a fixed height but the page, with ONE exception, the
> cover (rule 3), which PpReportCover fits to exactly one page. Sections start a fresh page (`breakBefore` on
> their first atom) and may run on: a continued page says "Work-on 3, continued" and a table repeats its heads
> (printGroup). Every list is capped in the map AND here (rule 4: the map words the "N more"; the register only
> slices, as a guard). Empty means absent (rule 6): a block with nothing to say is not drawn, heading and all.
> The register writes no sentence of its own (rule 7): every sentence is a map string; only fixed headings
> ("Going well", "What to do") and cross-references ("p. 4") are set here.
> FOLIO LIGHT, WHATEVER THE APP'S THEME (§7.1). A client document has one look, and printing must not re-theme the
> app. So nothing here reads a LIVE theme token (Theme.colorText follows the app; in a dark theme it is near-white).
> `rr` below is the report's own palette and type, read from the Folio entry's LIGHT block of Theme's theme table
> (Theme._themes.folio — data, not the current theme) and Theme.serifFamily (Georgia, else Gelasio: the system's,
> not the theme's). The studio's accent replaces Folio's accent in the report's roles only; faults stay Folio's
> …

- **Properties:** `var report`, `var appendixBody`, `string medium`, `int printPage`, `readonly int clientPageCount`, `readonly int appendixPageCount`, `readonly int printPageCount`, `readonly bool inAppendix`, `readonly bool printPageHasPhoto`, `readonly int printPageDpi`, `readonly var rep`, `readonly string kind`, `readonly var brand`, `readonly var meta` … (+9 more)
- **Functions:** `sectionPage(name)`, `woPage(rank)`, `pageInfo()`
- **Uses:** `PpDeliveryPage`, `PpDiagPrint`, `PpLmAppendix`, `PpNumbersTable`, `PpOrderPage`, `PpPager`, `PpPosturePage`, `PpReportAbout`, `PpReportCover`, `PpSwingGrid`, `PpTypeset`, `PpWorkOnPage`, `PpWristPage`
- **Used by (1):** `PpReportPrintHost`
- **Exercised by tests/tools:** `report_delivery`, `report_posture`, `report_wrist`, `tst_session_report`

### PpSessionReportList
`src/Gui/report/PpSessionReportList.qml` · 782 lines · root `Column`

> THE GOLFER'S SESSIONS, EACH WITH ITS REPORT (docs/design/session_report_design.md §8.0;
> sessions_screen_redesign.md §2) — the table of the rail's Sessions screen (ScreenSessions): one
> PpSessionsTableRow per session, over SessionReportListModel (`sessionReportList`), in a PpCard table.
> THE SELECTION (§2.3) lives here: `selection` holds session dirs, `_anchor` the range anchor.
> click                 toggles the row and makes it the anchor        toggleRow(dir)
> shift-click           ADDS every shown row from the anchor to here   extendTo(dir)
> Ctrl/⌘-click          toggles (as a click)
> Space                 toggles the focused row;  Up / Down move the focus
> Ctrl/⌘-A              every row the filters show                     selectAllShown()
> Esc                   clears                                         clearSelection()
> double-click / Enter  opens the row in Live and leaves the selection as it was
> A row the filters hide (shownRank < 0) is pruned from the selection, so the screen's count is always
> what can be seen selected. The ⋯ menu (PpSessionRowMenu, one for the list) acts on its row only:
> openRowMenu(i) / closeRowMenu(), i the row's place among those shown. Each row is "sessionsRow_<i>".
> SELECTION DIFFERS FROM THE SWING MODEL TABLE ON PURPOSE (S-D1, Mark 28 Oct). In
> diagnosticmodel/ModelTable.qml a plain click replaces the selection and shift-click replaces it with a
> …

- **Properties:** `var model`, `var reports`, `var mailer`, `var studio`, `var athletes`, `var exporter`, `var archiver`, `var reviewer`, `var settings`, `readonly bool archiveReady`, `alias jobs`, `readonly int total`, `readonly int shown`, `readonly var details` … (+8 more)
- **Signals:** `reviewSessionRequested(string dir)`, `studioSettingsRequested()`, `golferDetailsRequested()`, `notice(string glyph, string message, string severity, string copyText)`, `selectedOnce()`
- **Functions:** `fill(template, words)`, `composeWords(sessionMs, nowMs)`, `composeFor(dir, sessionMs)`, `sendEmail(dir, pdfPath, sessionMs)`, `emailPressed(dir, pdfPath, sessionMs)`, `sendReport(dir, pdfPath, sessionMs)`, `openPdf(path)`, `openSheet(dir, thenEmail)`, `rowAt(i)`, `facts(dir)` (+12)
- **Uses:** `PpCard`, `PpConfirm`, `PpExportOptionsSheet`, `PpReportSheet`, `PpSessionBulkJobs`, `PpSessionRowMenu`, `PpSessionsTableRow`
- **Used by (1):** `ScreenSessions`
- **Exercised by tests/tools:** `tst_sessions_bulk`, `tst_sessions_selection`

### PpSessionRowMenu
`src/Gui/report/PpSessionRowMenu.qml` · 241 lines · root `Popup`

> A SESSION'S ⋯ MENU (docs/design/sessions_screen_redesign.md §2.5, §8 S-D5) — one for the whole
> Sessions table, opened by PpSessionReportList on the row whose ⋯ was pressed. It acts on THAT ROW
> ONLY, never the selection.
> Review in Live                     ("Go to Live" on the running session; "Restore and review")
> ─
> Make report | Open report + Remake report | Remake report + Open old report
> Email to Nick                      or why not: "make report first" (inactive) / "add Nick's
> email" (the golfer page)
> Copy address
> ─
> Export…   Show in Finder   Archive | Restore
> ─
> Delete…                            the warn colour
> Export, Archive / Restore and Delete go to the list's askBulk with this row alone: its confirm, then the
> bulk queue (PpSessionBulkJobs) with one dir. The live session is never offered to archive or delete —
> the item stays, inactive, and says why; so do Archive / Restore with no archive folder set, and all
> …

- **Properties:** `string dir`, `bool isLive`, `bool archived`, `bool canReport`, `string reportState`, `string pdfPath`, `bool making`, `bool hasSwings`, `bool archiveReady`, `bool jobRunning`, `string greeting`, `string email`, `bool sendBusy`
- **Signals:** `reviewRequested()`, `makeReportRequested()`, `openReportRequested()`, `openOldReportRequested()`, `emailRequested()`, `golferDetailsRequested()`, `copyAddressRequested()`, `revealRequested()`, `exportRequested()`, `archiveRequested()`, `deleteRequested()`
- **Local components:** `MenuLine`, `Rule`, `Gap`
- **Uses:** `PpPopoverCard`, `PpPressable`
- **Used by (1):** `PpSessionReportList`

### PpSessionsSelectionBar
`src/Gui/report/PpSessionsSelectionBar.qml` · 228 lines · root `Rectangle` · 1 hard-coded colour(s)

> THE SESSIONS SELECTION BAR (docs/design/sessions_screen_redesign.md §2.4) — floats at the foot of the
> Sessions screen while anything is selected, or while a bulk job runs:
> 3 selected  3.6 GB · 2 reports out of date  Clear     [Make / remake reports] Email to Nick  Export…  Archive  Delete…
> Making reports · 2 of 5  Cancel                                                 (while a job runs)
> The summary is the list's selectionInfo. Each button is live when ANY selected row can take it, and
> otherwise says why in its tooltip; the rows that cannot are skipped by the job, never failed. Archive
> reads Restore when every selected row is archived. Everything goes through the list (askBulk), which
> owns the confirms, the export sheet and the queue (PpSessionBulkJobs).

- **Properties:** `var list`, `readonly var info`, `readonly var jobs`, `readonly bool running`, `readonly string who`, `readonly int n`, `readonly int reportable`, `readonly int liveN`, `readonly int archivedN`, `readonly bool allArchived`, `readonly bool archiveReady`, `readonly bool reportsBusy`, `readonly bool archiverBusy`, `readonly string whyNoReport` … (+6 more)
- **Functions:** `bytesWords(b)`, `plural(n, one, many)`
- **Local components:** `BarButton`
- **Uses:** `PpButton`, `PpLink`
- **Used by (1):** `ScreenSessions`
- **Exercised by tests/tools:** `tst_sessions_bulk`

### PpSessionsTableRow
`src/Gui/report/PpSessionsTableRow.qml` · 449 lines · root `PpTableRow` · 3 hard-coded colour(s)

> ONE SESSION, ONE TABLE ROW (docs/design/sessions_screen_redesign.md §2.1, §8) — a row of
> PpSessionReportList on the rail's Sessions screen, on its PpCard's column template:
> Date [Review]   Club   Swings   Recorded with   Work-on…   Report · action   ⋯
> Date        the day in the body face, emphasised; "· 2" for a second session that day. The Review
> chip (PpRowAction) rides at the cell's right edge on hover, focus or previewHover:
> "Review" (Enter), "Live" on the running session, "Restore and review" on an
> archived one (opening it restores it first, main.cpp archivedSessionOpened)
> Work-on     work-on 1's goal, elided; "No habits to work on" with three swings or more; "—"
> Report      the short state ("No report" muted, "Out of date" in the attention colour, "Ready",
> "Sent 21 Oct") and its ONE next action: Make / Remake / Email. The full words
> (stateWords: "Sent 21 Oct 18:40", "Opened in Mail…") are the cell's tooltip. No action
> on a session that cannot report (under three swings, or the live one). An archived row
> is dimmed and reads "Archived". Making, the send confirm, sending and a refusal each
> REPLACE the cell in place, as does a bulk job's state for the row (jobWords: "Archive
> queued", "Archiving… 40%", or why it failed, in the warn colour, until the next job);
> the rest is on the ⋯ menu (PpSessionRowMenu).
> …

- **Properties:** `string dir`, `string dateLabel`, `int sessionOfDay`, `string clubs`, `int swings`, `var devices`, `var deviceTokens`, `var selectedDevices`, `string workOn1`, `bool workOnKnown`, `bool isLive`, `bool archived`, `bool canReport`, `string reportState` … (+23 more)
- **Signals:** `rowPressed(int modifiers)`, `rowDoubleClicked(int modifiers, point scenePos)`, `reviewRequested()`, `menuRequested()`, `openReportRequested()`, `makeReportRequested(bool thenEmail)`, `emailRequested()`, `sendConfirmed()`, `sendCancelled()`, `studioSettingsRequested()`
- **Functions:** `sizeWords(b)`
- **Uses:** `PpDeviceTags`, `PpLink`, `PpPressable`, `PpRowAction`, `PpTableRow`
- **Used by (1):** `PpSessionReportList`

### PpStill
`src/Gui/report/PpStill.qml` · 66 lines · root `Column` · no direct `Theme.*` refs · 1 hard-coded colour(s)

> A STILL — one frame of the golfer at a swing position, captioned (design §6). The JPEG is made by ReportStills,
> already cropped to the golfer (nobody else leaves the studio) with the body line drawn on it; `src` is its
> file:// URL. Here it is only framed: a portrait frame of fixed aspect, the picture filling it (centred crop).
> A still whose file is missing (a fixture away from the share, a still that failed) is an EMPTY FRAME of the same
> size, so the page lays out identically with or without it, and it is not counted as a photograph.
> `isPhoto` (a loaded picture) is what PpSessionReport reads to mark the page as one the PDF writer stores as JPEG.

- **Properties:** `QtObject ro`, `string src`, `string caption`, `string value`, `string note`, `real w`, `real aspect`, `readonly bool isPhoto`
- **Used by (2):** `PpSwingGrid`, `PpWorkOnPage`

### PpSwingGrid
`src/Gui/report/PpSwingGrid.qml` · 341 lines · root `Column` · no direct `Theme.*` refs

> SWING BY SWING — page 2 (design §3): one grid, a row per habit and a column per swing in the order hit; carry as
> bars under the swings; the reference swing at four positions; Earlier today.
> ANY SESSION LENGTH (§9.1 rule 4). Columns come in BANDS of at most 20 swings (60 swings: three bands, each with its
> own swing numbers and carry bars, the pitch the same in every band). Each band is a printGroup, so a band cut by a
> page repeats its swing numbers. Rows are capped per group — habits 5, seen 4, ball 4 — and the map words the rest
> ("and 2 more seen on fewer swings"). The name column is sized from the widest name (10–18 em) and wraps beyond
> it; the count column from the widest count. Earlier today shows the newest six sessions at most.
> Photos (§7.3): without approval there is no reference swing, here or anywhere.

- **Properties:** `QtObject ro`, `var rep`, `bool photos`, `readonly var g`, `readonly var swings`, `readonly int n`, `readonly var groupCaps`, `readonly var groups`, `readonly var todayRows`, `readonly real nameW`, `readonly real countW`, `readonly int perBand`, `readonly var bands`, `readonly real pitch` … (+7 more)
- **Functions:** `mx(i)`
- **Local components:** `SwingMark`
- **Uses:** `PpStill`
- **Used by (3):** `PpReportCover`, `PpSessionReport`, `PpWorkOnPage`

### PpWorkOnPage
`src/Gui/report/PpWorkOnPage.qml` · 293 lines · root `Column` · no direct `Theme.*` refs

> A WORK-ON IN DEPTH — one section per work-on (design §3 pages 4–8): kicker · the goal as the title · the coach's
> term · What we saw (with the run of the swings against the target range) · the pictures · Why it matters ·
> What came with it · What might be behind it (every authored cause in three groups, §3.3) · What to do (the feel,
> a drill card) · Check it yourself · Next time.
> FLOWED, NOT FITTED (§9.1 rules 1, 2). The section starts a fresh page and may run on ("Work-on 3, continued");
> it is cut only between atoms: the head (which keeps with what follows), the saw-and-pictures block, each labelled
> paragraph, the causes' lead and each cause group, a drill card. Every block is drawn only when the map has its
> words (rule 6: a work-on with no feel has no "The feel" line; no cause spotted, no "What we spotted").
> Pictures (§6): two stills at most, for the first three work-ons only, and none without photo approval — then the
> run chart takes the picture's place at full width.

- **Properties:** `QtObject ro`, `var wo`, `int rank`, `bool photos`, `readonly var stills`, `readonly bool hasStills`, `readonly var causes`, `readonly var drills`, `readonly bool hasToDo`, `readonly bool denseByContent`, `int squeeze`, `readonly bool tight`, `real pageH`, `readonly bool dense` … (+3 more)
- **Functions:** `gap(n)`
- **Local components:** `Para`, `RunChart`
- **Uses:** `PpDrillCard`, `PpStill`, `PpSwingGrid`
- **Used by (1):** `PpSessionReport`

### PpWristPage
`src/Gui/report/PpWristPage.qml` · 497 lines · root `Column` · no direct `Theme.*` refs

> YOUR WRIST — the Wrist page of the session report (docs/design/session_report_design.md "The Wrist page";
> docs/design/wrist_motion_into_charts.md §8.2): the Charts Wrist section's SESSION reading, printed. After the Posture
> page and before "Between now and next time"; absent when the map has no `wrist` (report_wrist.h: no swing of the
> session reads a wrist DOF). Blocks, in the Posture page's order:
> · the head: the kicker, "Wrist", the head line, the Style line (the resemblance index's clause; absent with no
> index: the head already says the style counts) and the Score line;
> · "Checkpoints across the session": PpReportProfile (the Posture page's profile, shared): one row per wrist DOF,
> the DOMINANT style's band as a wash in the DOF's own metric colour, the session's box per checkpoint, under each
> the median ("-" where too few swings), how wide the middle half is or — outside the band — the side it falls on,
> in warn and underlined; the caption says whose band is shaded;
> · "Wrist score, swing by swing": PpChRunBox in the Charts print roles, non-interactive — a mark per swing, a stub
> where a swing has no score, the session's box beside, the value axis from the payload's ticks (paper has no
> ticker), the swing numbers the report's;
> · "Wrist style by swing": the resemblance index per swing and style, fifteen swings a line (each line one atom),
> the style each swing was graded against in bold, then the counts; with no index, the presenter's note alone;
> · "What keeps firing": one atom per finding — its name and count words, then a mark per swing in the Posture
> …

- **Properties:** `QtObject ro`, `var wr`, `readonly var m`, `readonly bool on`, `readonly var prof`, `readonly var rows`, `readonly var positions`, `readonly var sr`, `readonly var st`, `readonly var stCols`, `readonly var stRows`, `readonly var rec`, `readonly var mw`, `readonly bool mwOk` … (+5 more)
- **Functions:** `chunks(list, n)`, `fin(v)`
- **Uses:** `PpChRoles`, `PpChRunBox`, `PpReportProfile`
- **Used by (1):** `PpSessionReport`
