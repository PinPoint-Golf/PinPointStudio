# Sessions screen redesign

Status: **design + build plan; being built.** Written 28 Oct 2026 against `5ac84d14`; corrected
against `04f84e5b` the same day (§8 — read it first, it overrides the sections above where they differ). Follows
`ux_refactor_away_from_4_modes.md` and uses its §2.5 visual language and shared components.

How to use with Claude Code: one phase per fresh session (§6). **Look at the two PNGs in
`docs/design/sessions_screen_redesign/` before writing any QML** (Read tool). They govern layout,
proportion and hierarchy; `Theme` tokens govern exact values — never copy a hex from the PNGs or
`sessions_mockup.html` into QML.

Visual reference for humans: Design canvas "PinPoint Studio — UX restructure", board
**S1 · Sessions — table with bulk actions** (S2 and S3 beside it were the rejected alternatives).

---

## 1. Why

Sessions became a rail item (`2772f75c`) but the screen is still the list that used to sit at the
bottom of Home: a centred column of three-line rows, each carrying four or five text links
(Open session · Make report · Email to Mark — make the report first · Copy address · Show in Finder).
For a screen of its own it is a wall of repeated links, it scans badly (every row is three lines),
it has no selection, so nothing can be done to several sessions at once, and it has no delete,
archive or export at all.

## 2. Target design

### 2.1 Layout (see `01_sessions_selection_and_hover.png`)

Full-width table, top to bottom:

1. **Header** — "Sessions" (title size), a monospace summary *"10 sessions · 202 swings · 14.6 GB"*,
   and a search field on the right (searches work-on text, clubs, notes).
2. **Filter bar** — one row:
   - **Recorded with** chips: the existing `PpDeviceFilterBar` and its model, unchanged in
     behaviour, restyled to the §2.5 chip.
   - **Report** segmented control: *All reports · None N · Out of date N · Ready N*.
   - **Club** dropdown (All, then the clubs present).
   - **Show archived (N)** checkbox, right-aligned. Archived sessions are hidden by default.
3. **The table** — a `PpCard { table: true }` with a caption row and one `PpTableRow` per session,
   `Theme.rowH` high, hairline dividers. Columns:

   | Column | Content | Notes |
   |---|---|---|
   | Date | "Tue 20 Oct 2026", "· 2" for a second session that day; **Review** hover button right-aligned in this cell | sortable (default newest first) |
   | Club | "7 iron", "7 iron, Driver" | |
   | Swings | count, monospace, right-aligned | |
   | Recorded with | `PpDeviceTags`, one pill per device | |
   | Work-on | work-on 1's goal, elided; "No habits to work on" / "—" as today | flexible column |
   | Report | state word + its one next action: *No report · Make* / *Out of date · Remake* / *Ready · 8.1 MB · Email*; sending progress and errors replace this cell in place | no action when the session cannot report (`CanReportRole`: ≥ 3 swings and not live) |
   | ⋯ | row menu | |

4. **Hint line** under the table, quiet: *Click a row to select or deselect · Shift-click for a
   range · Hover for Review, or double-click, to open in Live · Esc clears*. Hide it once the user has
   selected something once (remember in `appSettings`).
5. **Selection bar** — floating at the bottom of the screen while ≥ 1 row is selected (A.1).

### 2.2 Review (open in Live)

- On row **hover** (and when the row has keyboard focus) a **▶ Review** chip appears,
  **right-aligned inside the Date cell** — the same pattern, sizes and colours as the swing model's
  hover **Graph** chip (`diagnosticmodel/ModelTable.qml` ~l.650–690: `visible: hovered || isCurrent`,
  `Theme.colorBg3` ground, `colorAccentLight` on press-hover, micro caps text, tooltip naming the
  shortcut). Reuse that implementation; if it is worth sharing, extract it to
  `components/PpRowAction.qml` and use it in both places.
- Review, **double-click** on the row, and **Enter** on the focused row all do what "Open session"
  does today: `reviewSessionRequested(dir)` → `sessionReviewController.loadSession(dir)` →
  `navigate(AppScreen.Session)` (the rail's **Live**). Tooltip: "Review in Live · Enter".
- The live session's row (`IsLiveRole`) says **Live** in place of Review and goes straight there.

### 2.3 Selection (no checkboxes)

| Gesture | Effect |
|---|---|
| Click a row (anywhere but Review, a link, or ⋯) | toggles that row in or out of the selection; becomes the range anchor |
| Shift-click | adds every row from the anchor to here |
| Ctrl/⌘-click | same as click (kept so muscle memory from Finder/the swing model works) |
| Space on the focused row | toggles |
| Ctrl/⌘-A | selects every row the filters show |
| Esc | clears |
| Double-click / Enter | opens in Live; does **not** change the selection |

Selected rows: 3-px `colorAccent` bar on the left edge and a `colorAccentLight` wash (hover darkens
it slightly). Selection holds **session dirs**, survives scrolling and re-sorting, and is pruned
when a filter hides a row (the bar's count always equals what you can see selected).

> **Decision S-D1 — click toggles.** In the swing model table a plain click *replaces* the
> selection and Ctrl/⌘-click toggles (`ModelTable.qml` ~l.580). Here a plain click *toggles*, by
> choice (Mark, 28 Oct): Sessions is a list you act on in batches, and there is no "current item"
> pane for a single click to drive. The two tables therefore differ on purpose; record this in
> both files' header comments so nobody "fixes" one to match the other.

### 2.4 Selection bar

Floating card, bottom of the content area, 56 px, `colorBg2` ground, accent hairline, shadow:

*"3 selected · 3.6 GB · 2 reports out of date · Clear"* … **Make / remake reports** (primary) ·
**Email to Mark** · **Export…** · **Archive** (or **Restore** when every selected row is archived) ·
**Delete…** (warn colour, outline).

- The summary is computed from the selected rows: count, total size on disk, report states.
- Buttons disable with a tooltip when nothing selected can take them (e.g. Email when no selected
  session can have a report).
- While a bulk job runs the bar becomes its progress line ("Making reports · 2 of 5 · Cancel") and
  every row it touches shows its own progress in the Report cell.

### 2.5 Row menu (⋯) (see `02_sessions_row_menu.png`)

Review in Live · — · Make report / Remake report / Open report · Email to *first name* (disabled,
"make report first", when there is none) · Copy address · — · Export… · Show in Finder · Archive /
Restore · — · Delete… The menu acts on **its row only**, never the selection.

### 2.6 Actions — what each one calls

| Action | One session | Several | Existing code |
|---|---|---|---|
| Review | as today | — | `ScreenSessions.reviewSessionRequested` → Main.qml |
| Make / remake report | the existing report sheet (`PpReportSheet` in `PpSessionReportList`) | a **queue**: one `sessionReport.make(dir, options)` at a time with the sheet's default options, newest first; skip rows that cannot report | `SessionReportModel::make/prepare/state` |
| Email | the existing hand-off / in-place send confirm (`PpSessionReportList` header comment, §8.3) | one confirm for all ("Send 3 reports to nick@… · 21 MB"), then sends in turn; sessions without a PDF are made first | `reportMailer.compose/send`, `sessionReport.recordSend` |
| Copy address | as today | — | |
| Export… | options sheet: cameras (`swingExporter.camerasForShots`), include JSON | same sheet over the union of cameras; **one zip per session**, queued (the exporter is single-job: `busy`) | `sessionReviewController.swingDirsForSession(id)` → `swingExporter.exportShots(dirs, videos, includeJson)` |
| Show in Finder | as today | — | |
| Archive / Restore | confirm sheet stating what archiving keeps and frees | one confirm with totals; runs in turn | `archiveController.archiveSession/restoreSession` (worker thread, one at a time; `busy`, `progress`, `status`) |
| Delete… | confirm: "Move Mon 5 Oct to the Trash? Its 31 swings and report go with it. You can put it back from the Trash." | one confirm listing count, swings and size | `sessionReviewController.trashSession(id)` (the library's `.pinpoint-trash`, kept for `trashRetentionDays`, 0 = forever). The live session cannot be deleted. |

Never delete without a confirm; never archive or delete the live session; a bulk job stops between
sessions on Cancel.

## 3. Current code (findings)

| What | Where | Notes |
|---|---|---|
| Screen | `athlete/ScreenSessions.qml` (97 lines) | Heading + subtitle + `PpSessionReportList { limit: 0 }` in a centred `Flickable`. Signals: `reviewSessionRequested(dir)`, `studioSettingsRequested()`, `golferDetailsRequested()`. |
| List | `report/PpSessionReportList.qml` (333) | Owns the report sheet, the mail hand-off, the in-place send confirm and progress, the device filter header. Keep all of that logic; it moves, it is not rewritten. |
| Row | `report/PpSessionReportRow.qml` (404) | Three-line row with text links. Replaced by a table row. |
| Model | `report/session_report_list_model.h` | Roles: `DirRole, DateLabelRole, SessionOfDayRole, ClubsRole, SwingsRole, DevicesRole, WorkOn1Role, IsLiveRole, CanReportRole, StateRole, StateWordsRole, PdfPathRole, SizeBytesRole (the PDF), SessionMsRole, WorkOnKnownRole, StateKnownRole, SentDiffersRole, PreparedMsRole, DeviceTokensRole, ShownRankRole`. Two-pass scan; device filter via `ShownRankRole`. |
| Archive | `settings/archive_controller.h` | `sessions` list `{…, bytes, swings, archived}`; `archiveSession`, `restoreSession`, worker thread, `busy/progress/status`. |
| Delete | `review/session_review_controller.h` l.115–121 | `trashSession(id)` → OS trash; `swingDirsForSession(id)`. |
| Export | `Export/swing_zip_exporter.h` | `camerasForShots(dirs)`, `exportShots(dirs, videos, includeJson)`, `exportFinished(ok, zip, error)`; single job. |
| Hover action precedent | `diagnosticmodel/ModelTable.qml` ~l.650 | the Graph chip. |
| Selection precedent | `diagnosticmodel/ModelTable.qml` l.48, ~l.580 | `property var selection: []` of ids; modifier handling in the cell's `PpPressable`. |
| Shared components | `components/PpCard, PpTableRow, PpExpandRow, PpBandBar, PpDeviceFilterBar, PpDeviceTags, PpSegmentedControl, PpChip` | from the four-modes refactor. |

## 4. Reuse, adapt, add

| Component | Verdict | Change |
|---|---|---|
| `ScreenSessions.qml` | **Adapt** | Full-width layout (no `contentWidth` column), header, filter bar, table, hint, selection bar; keeps its signals. |
| `PpSessionReportList.qml` | **Adapt** | Keep the report sheet, hand-off, send confirm and progress logic; draw rows as `PpSessionsTableRow`; own the selection (§2.3) and expose it to the bar. |
| `PpSessionReportRow.qml` | **Replace** with `report/PpSessionsTableRow.qml` | On `PpTableRow`; cells per §2.1; Report cell keeps today's states, words, send progress and error line. Delete the old row in Phase 3. |
| `report/PpSessionsSelectionBar.qml` | **New** | §2.4. |
| `report/PpSessionRowMenu.qml` | **New** | §2.5, on the app's existing popup/menu component. |
| `components/PpRowAction.qml` | **New, extracted** | The hover chip from `ModelTable.qml`; ModelTable switches to it. |
| `report/session_bulk_jobs` (QML `QtObject` or C++) | **New** | One queue for make / send / export / archive / trash over a list of dirs: sequential, cancellable between items, progress `{verb, done, total, current}`. |
| `SessionReportListModel` | **Adapt** | Add `SessionBytesRole` (folder size on disk, measured in the scan pass) and `ArchivedRole` (the archive marker `SessionArchiver` writes); a `clubFilter` and `reportFilter` and `showArchived` alongside the device filter, all feeding `ShownRankRole`; a `search` string over work-on goal + clubs (+ notes when they exist). |
| `PpDeviceFilterBar` | **Reuse** | Restyle only if it is not already on the §2.5 chip. |
| `PpSegmentedControl` | **Reuse** | Report filter. |

## 5. Mockup notes

- Rail in the PNGs is stale: the rail is now Home · Live · Sessions · Settings (`16aa865c`). Ignore it.
- `01_…` shows three rows selected (Mon 5 Oct, Tue 18 Aug · 2, Tue 18 Aug · 1), the pointer over
  Tue 18 Aug · 2 with its Review chip visible, and the selection bar.
- `02_…` shows no selection, Review visible on the hovered Thu 8 Oct row and its ⋯ menu open.
- Sizes on disk, "Ready · 8.1 MB" and the archived count are illustrative.

## 6. Phases

**Phase 1 — model.** `SessionBytesRole`, `ArchivedRole`, club/report/archived filters and search
feeding `ShownRankRole`; counts per report state and per club for the filter labels. Extend
`report_list_test` (sizes, archived marker, every filter combination, search). Accept: the test
passes; the existing screen still works unchanged.

**Phase 2 — table and selection.** `PpRowAction` (extracted; ModelTable uses it, no visual change
there), `PpSessionsTableRow`, the new `ScreenSessions` layout, filter bar, selection model (§2.3),
hint line, Review/double-click/Enter → Live, row menu with today's single-session actions wired
(make/remake, email, copy address, show in Finder). Update `tests/qml/tst_session_report.qml`; add
`tst_sessions_selection.qml` (toggle, shift range from anchor, Esc, Ctrl/⌘-A over the filtered rows,
pruning on filter change, double-click does not select). Accept: screenshot matches `02_…` in
layout and proportion; everything the old screen could do still works.

**Phase 3 — bulk and new actions.** `session_bulk_jobs`; selection bar (§2.4); export sheet; archive
/ restore and delete confirms; single-row menu items for Export, Archive/Restore, Delete. Delete
`PpSessionReportRow.qml`. Tests: bulk make skips rows that cannot report; cancel stops between
items; trash of the live session refused; archive then restore round-trips `ArchivedRole`.
Accept: screenshot matches `01_…`; a dry-run (`PINPOINT_MAIL_DRYRUN`) bulk email records one send
per session.

## 7. Open decisions

- **S-D1** click toggles (decided, §2.3).
- **S-D2** Notes: search mentions notes; sessions have no notes field yet. Keep search to work-on +
  clubs until notes exist.
- **S-D3** Bulk email: one email per session (as the send log assumes) — not one email with several
  PDFs. Revisit if coaches ask for a digest.
- **S-D4** Archived rows when shown: same table, dimmed, Report cell reads "Archived", menu offers
  Restore; Review still works if the archive keeps the kept swings (check `SessionArchiver`).

## 8. Corrections and decisions (28 Oct, audit against `04f84e5b`)

Decisions (Mark, 28 Oct):

- **S-D5 Report cell.** Short state + one action as §2.1, but a sent report reads "Sent 21 Oct" in
  place of "Ready". Today's full `stateWords` ("Sent 21 Oct 18:40", "Opened in Mail…", "Report
  prepared…") become the cell's tooltip. "Open old report", "Remake" on a current report and "add
  *X*'s email" move to the ⋯ menu. Nothing today's row offered is lost.
- **S-D6 Archive keeps the report current.** Archiving rewrites each swing as a stub, which changes
  the `swingsIdentity` hash (`report/report_meta.h`), so a current report used to read "Out of date"
  after archive and again after restore. Archive and restore now re-stamp the identity when the
  report was current before.
- **S-D7 Build.** All three phases in one run, one commit at the end, after Mark has judged the
  result.

Corrections:

- **Live guard.** Neither `trashSession` nor `archiveSession` refused the live session (only `""`).
  Both now refuse it in C++, and the screen never offers it.
- **Delete** goes to the library's `.pinpoint-trash`, not the OS Trash; the confirm says so and names
  the retention.
- **S-D4, archived rows.** Opening an archived session in Review starts a full restore
  (`archivedSessionOpened` → `archiveController.restoreSession`, main.cpp). Review on an archived row
  reads "Restore and review" and its tooltip says so.
- **Archive** needs `appSettings.archiveLocation`; with none set the action is disabled and its
  tooltip points to Settings → Archiving.
- **Tests.** `tests/qml/tst_session_report.qml` is the printed report's layout test, not the list;
  nothing covered the list. Phases 2 and 3 add `tst_sessions_selection.qml` (run by ctest `qml_ui`).
- **No shared menu or confirm existed.** The row menu follows the Popup + `PpPopoverCard` pattern of
  `session/PpSessionDrawer.qml`; the confirm is `diagnosticmodel/ModelConfirm.qml` moved to
  `components/PpConfirm.qml`; Export reuses `shot/PpExportOptionsSheet.qml` (no new sheet); the
  checkbox is its `CheckRow` lifted to `components/PpCheckRow.qml`.
- **Shift-click.** ModelTable's shift-click *replaces* the selection with the range from
  `selectedId`; here it *adds* the range from this table's own anchor. Deliberate (S-D1).
- **Bulk jobs** live in QML (`report/PpSessionBulkJobs.qml`): every backend is already a single-job
  QML-facing controller. `archiveController` has no ok/error signal — the queue watches `busy` and
  reads `status`. The exporter has no cancel; Cancel stops before the next session.
- `PpSessionReportList`'s `limit` / `showAll` path was dead (Home no longer lists sessions); removed.
