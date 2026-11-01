# Athlete

*Directory:* `src/Gui/athlete/`  
*6 components · 2,472 lines of QML*

Athlete picker, athlete create/edit form and per-athlete club records.

**C++ in this area:** `AthleteController`  (see [cpp-seam.md](../cpp-seam.md))

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [AthleteClubsSection](#athleteclubssection) | 535 | `Rectangle` | 1 | 5 |  |
| [PpAthleteCard](#ppathletecard) | 134 | `Rectangle` | 1 | 1 |  |
| [PpAthleteRow](#ppathleterow) | 184 | `Item` | 1 | 1 |  |
| [ScreenAthleteForm](#screenathleteform) | 1084 | `Item` | 1 | 9 |  |
| [ScreenAthletePicker](#screenathletepicker) | 272 | `Item` | 1 | 4 |  |
| [ScreenSessions](#screensessions) | 263 | `Item` | 1 | 8 |  |

### AthleteClubsSection
`src/Gui/athlete/AthleteClubsSection.qml` · 535 lines · root `Rectangle` · 1 hard-coded colour(s)

> Per-athlete club records — a card group for ScreenAthleteForm (edit mode).
> Records live under athletes/<uuid>/clubs keyed by the canonical club name
> (the same id markup writes to truth.json meta.club). Club length feeds the
> shaft-tracker search radius; the band fields describe retroreflective tape
> (optional, high value in dark rooms) for the instrumented-club pipeline —
> docs/validation/instrumented_club_protocol.md. Band positions are the
> CENTRES of each tape band, measured from the butt end of the grip, in mm.
> Unlike the scalar athlete fields (buffered until Save), club edits commit
> IMMEDIATELY via athleteController — the section is only shown in edit mode.

- **Properties:** `string athleteUuid`, `string selectedClubId`, `readonly var clubs`, `readonly var configuredIds`, `readonly var unconfiguredIds`
- **Functions:** `recField(id, field, dflt)`, `commitField(id, field, value)`, `parseCenters(text)`, `bandPattern(centers)`
- **Uses:** `ClubFormat`, `PpButton`, `PpComboBox`, `PpPressable`, `PpTextField`
- **Used by (1):** `ScreenAthleteForm`

### PpAthleteCard
`src/Gui/athlete/PpAthleteCard.qml` · 134 lines · root `Rectangle` · 9 hard-coded colour(s)

Tile for one athlete in the picker grid: avatar initials on a colour derived from the athlete, name and a 'last seen' line (today / yesterday / N days ago / no sessions yet); selected and hover states. Emits clicked.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `var athleteData`, `bool isSelected`, `readonly var avatarColors`, `readonly string avatarColor`
- **Signals:** `clicked()`
- **Uses:** `PpPressable`
- **Used by (1):** `ScreenAthletePicker`

### PpAthleteRow
`src/Gui/athlete/PpAthleteRow.qml` · 184 lines · root `Item` · 9 hard-coded colour(s)

Compact list-row form of the athlete tile for the picker's list layout (same avatar/last-seen data as PpAthleteCard, 'Nd ago' format); adds a pencil-on-hover edit affordance (editRequested) beside the selectable row.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `var athleteData`, `bool isSelected`, `int rowIndex`, `readonly var avatarColors`, `readonly string avatarColor`
- **Signals:** `clicked()`, `editRequested()`
- **Uses:** `PpPressable`
- **Used by (1):** `ScreenAthletePicker`

### ScreenAthleteForm
`src/Gui/athlete/ScreenAthleteForm.qml` · 1084 lines · root `Item`

Full-screen create/edit form for an athlete (name, DOB, sex, handedness, height/weight with unit toggles, primary club). Buffers scalar fields until Save; hosts AthleteClubsSection in edit mode. Emits saved / savedAndStarted / cancelled.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `bool nameError`, `bool dobError`, `string handedness`, `string heightUnit`, `string weightUnit`, `string primaryClub`, `string sex`, `bool emailError`, `bool photosApproved`, `string photosApprovedOn`, `readonly bool isJunior`, `readonly var sexOptions`, `readonly var sexLabels`, `string editUuid` … (+1 more)
- **Signals:** `cancelled()`, `saved(string uuid)`, `savedAndStarted(string uuid)`
- **Functions:** `emailShapeOk(text)`, `normaliseEmails(text)`, `firstWord(text)`, `approvedWords(iso)`, `sexLabelFor(value)`, `sexValueFor(label)`, `dobOk(text)`, `resetForm()`, `loadForEdit()`, `validate()` (+1)
- **Uses:** `AthleteClubsSection`, `ClubFormat`, `PpButton`, `PpChipGroup`, `PpComboBox`, `PpDisplayText`, `PpPressable`, `PpTextField`, `PpSegmentedControl` (style `unit`)
- **Used by (1):** `Main`

### ScreenAthletePicker
`src/Gui/athlete/ScreenAthletePicker.qml` · 272 lines · root `Item`

Full-screen athlete chooser: search, a 'Recent' section and 'All athletes' as PpAthleteCard grid or PpAthleteRow list; new athlete, import roster, edit and delete actions. Emits athleteSelected / newAthleteRequested / editAthleteRequested.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `string selectedUuid`, `readonly bool sessionRunning`, `var filteredAthletes`
- **Signals:** `athleteSelected(string uuid)`, `newAthleteRequested()`, `editAthleteRequested(string uuid)`
- **Uses:** `PpAthleteCard`, `PpAthleteRow`, `PpButton`, `PpDisplayText`, `PpSearchField`
- **Used by (1):** `Main`

### ScreenSessions
`src/Gui/athlete/ScreenSessions.qml` · 263 lines · root `Item`

> SESSIONS — the rail's Sessions (Main.qml, AppScreen.Sessions): the current golfer's sessions, every
> one, each with its report (docs/design/session_report_design.md §8.0), as a full-width table
> (docs/design/sessions_screen_redesign.md §2.1). The rail mutes Sessions with no golfer, so the list
> is always the current golfer's (main.cpp points sessionReportList at their library folder).
> Sessions  10 sessions · 202 swings · 14.6 GB                       [Search work-ons, clubs]
> RECORDED WITH (chips)   All reports | None 7 | Out of date 2 | Ready 1   Club: All ▾   ☐ Show archived (3)
> the table (PpSessionReportList)
> Click a row to select or deselect · … (until the first selection: appSettings.sessionsHintSeen)
> [3 selected · 3.6 GB · 2 reports out of date · Clear   Make / remake reports  Email  Export…  Archive  Delete…]
> floating, while anything is selected or a bulk job runs
> Every filter is the model's (SessionReportListModel); this screen only draws their controls. The
> list owns the rows, the selection and the ⋯ menu; its requests leave through this screen's signals,
> which Main.qml handles. The selection bar (PpSessionsSelectionBar, §2.4) acts through the list too; a
> bulk job's outcome comes back as the list's notice(), shown here in a toast.

- **Properties:** `readonly var model`, `readonly int sidePad`, `readonly bool hintSeen`
- **Signals:** `reviewSessionRequested(string sessionDir)`, `studioSettingsRequested()`, `golferDetailsRequested()`
- **Functions:** `toTop()`, `bytesWords(b)`, `plural(n, one, many)`
- **Uses:** `PpCheckRow`, `PpComboBox`, `PpDeviceFilterBar`, `PpSegmentedControl`, `PpSearchField`, `PpSessionReportList`, `PpSessionsSelectionBar`, `PpToast`
- **Used by (1):** `Main`
