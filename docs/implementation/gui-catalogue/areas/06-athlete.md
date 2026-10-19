# Athlete

*Directory:* `src/Gui/athlete/`  
*5 components · 1,914 lines of QML*

Athlete picker, athlete create/edit form and per-athlete club records.

**C++ in this area:** `AthleteController`  (see [cpp-seam.md](../cpp-seam.md))

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [AthleteClubsSection](#athleteclubssection) | 535 | `Rectangle` | 1 | 5 |  |
| [PpAthleteCard](#ppathletecard) | 134 | `Rectangle` | 1 | 1 |  |
| [PpAthleteRow](#ppathleterow) | 184 | `Item` | 1 | 1 |  |
| [ScreenAthleteForm](#screenathleteform) | 806 | `Item` | 1 | 8 |  |
| [ScreenAthletePicker](#screenathletepicker) | 255 | `Item` | 1 | 4 |  |

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
`src/Gui/athlete/ScreenAthleteForm.qml` · 806 lines · root `Item`

Full-screen create/edit form for an athlete (name, DOB, sex, handedness, height/weight with unit toggles, primary club). Buffers scalar fields until Save; hosts AthleteClubsSection in edit mode. Emits saved / savedAndStarted / cancelled.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `bool nameError`, `bool dobError`, `string handedness`, `string heightUnit`, `string weightUnit`, `string primaryClub`, `string sex`, `readonly var sexOptions`, `readonly var sexLabels`, `string editUuid`, `readonly var clubModel`
- **Signals:** `cancelled()`, `saved(string uuid)`, `savedAndStarted(string uuid)`
- **Functions:** `sexLabelFor(value)`, `sexValueFor(label)`, `dobOk(text)`, `resetForm()`, `loadForEdit()`, `validate()`, `doSave()`
- **Uses:** `AthleteClubsSection`, `ClubFormat`, `PpButton`, `PpChipGroup`, `PpComboBox`, `PpDisplayText`, `PpTextField`, `PpUnitToggle`
- **Used by (1):** `Main`

### ScreenAthletePicker
`src/Gui/athlete/ScreenAthletePicker.qml` · 255 lines · root `Item`

Full-screen athlete chooser: search, a 'Recent' section and 'All athletes' as PpAthleteCard grid or PpAthleteRow list; new athlete, import roster, edit and delete actions. Emits athleteSelected / newAthleteRequested / editAthleteRequested.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `string selectedUuid`, `var filteredAthletes`
- **Signals:** `athleteSelected(string uuid)`, `newAthleteRequested()`, `editAthleteRequested(string uuid)`
- **Uses:** `PpAthleteCard`, `PpAthleteRow`, `PpButton`, `PpDisplayText`
- **Used by (1):** `Main`
