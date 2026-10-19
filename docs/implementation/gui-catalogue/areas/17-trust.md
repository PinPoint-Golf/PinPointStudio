# Trust and gates

*Directory:* `src/Gui/trust/`  
*7 components · 702 lines of QML*

'Readings and their limits': the Trust panel, inline reasons, gate notes and the TrustFocus routing singleton.

**C++ in this area:** `TrustPresenter`, `TrustWords`  (see [cpp-seam.md](../cpp-seam.md))

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [PpGateNote](#ppgatenote) | 75 | `Column` | 0 | 2 | ⚠ unreferenced |
| [PpTrustInline](#pptrustinline) | 75 | `Item` | 2 | 4 |  |
| [PpTrustInlineFor](#pptrustinlinefor) | 63 | `Item` | 12 | 1 |  |
| [PpTrustPanel](#pptrustpanel) | 197 | `Item` | 2 | 3 |  |
| [PpTrustReading](#pptrustreading) | 123 | `Column` | 2 | 0 |  |
| [PpTrustSummaryFold](#pptrustsummaryfold) | 122 | `Column` | 5 | 2 |  |
| [TrustFocus](#trustfocus) | 47 | `QtObject` | 16 | 2 | singleton |

### PpGateNote
`src/Gui/trust/PpGateNote.qml` · 75 lines · root `Column` · ⚠ **no app file instantiates this**

> PPGATENOTE — what a surface shows where a gate refused a reading, instead of a bare "-" (trust stage 6
> session 2; docs/design/trust_and_gates_design.md §7.0, §7.1). The gate's short words, quiet, in the
> link colour with " ›"; a click opens the reading in the Trust panel (TrustFocus), or, with the panel
> off, expands the same reading here, under the words (PpTrustInlineFor). In print it is the words alone.
> gate      TrustWords.gateFor(gates, key) — { subject, gate, words, … }; nothing is drawn when empty
> swingDir  the swing the records are on (the in-place block reads its session through it)
> prefix    words before the gate's ("Club speed: ") when the surface has no label of its own
> roles     a PpTypeset (fonts, colours), or null for the theme's small text
> printed   paper: words only, no link, no fold
> objectNames: gateNote, gateNoteWords.

- **Properties:** `var gate`, `string swingDir`, `string prefix`, `QtObject roles`, `bool printed`, `bool inlineOpen`, `font font`, `readonly bool has`, `readonly color linkColour`, `readonly color quietColour`
- **Uses:** `PpTrustInlineFor`, `TrustFocus`

### PpTrustInline
`src/Gui/trust/PpTrustInline.qml` · 75 lines · root `Item` · no direct `Theme.*` refs

> PPTRUSTINLINE — the in-place home of a reason when the Trust panel is off (TrustFocus): the same readings
> (PpTrustReading) expanded directly under the claim that was clicked, full width, until closed. A quiet
> left rule ties it to the claim above; "Close" and "Show in the Trust panel" sit on its head line.
> objectName: trustInline.

- **Properties:** `QtObject roles`, `var readings`, `string swingDir`
- **Signals:** `closed()`
- **Uses:** `PpTrustReading`, `SessionMode`, `TrustFocus`, `ViewLayout`
- **Used by (2):** `PpChartsShotView`, `PpTrustInlineFor`

### PpTrustInlineFor
`src/Gui/trust/PpTrustInlineFor.qml` · 63 lines · root `Item` · no direct `Theme.*` refs

> PPTRUSTINLINEFOR — the in-place reason (PpTrustInline) for a surface that holds no readings of its own:
> given a swing and a subject ("ks.pelvis"), it reads the swing's session through a SessionSeriesModel
> (the process-wide cache the Charts panel fills, so usually a stat per swing) and a TrustPresenter, and
> shows that one reading. Built only while open. Session diagnostics uses it for a finding a gate kept
> from being judged, when the Trust panel is off (TrustFocus).
> objectName: trustInlineFor.

- **Properties:** `QtObject roles`, `string subject`, `string swingDir`, `readonly bool open`
- **Signals:** `closed()`
- **Uses:** `PpTrustInline`
- **Used by (12):** `PpCameraFrame`, `PpChWristShotSection`, `PpDataViewer`, `PpDiagAnalyse`, `PpDiagReplay`, `PpFigureRow`, `PpGateNote`, `PpLedgerTable`, `PpLmGraphicsBody`, `PpSwingEditPanel` (+2)

### PpTrustPanel
`src/Gui/trust/PpTrustPanel.qml` · 197 lines · root `Item` · no direct `Theme.*` refs

> PPTRUSTPANEL — "Readings and their limits": the home of every reason a reading was refused, bounded or
> withheld (docs/design/trust_and_gates_design.md §7). A stage panel like Charts or Session diagnostics,
> turned on in View; it follows the picked shot.
> head      the shot, and one sentence of what it could not measure (§7.4, the shot half)
> readings  one PpTrustReading per reading a gate touched, grouped by area, each foldable
> session   how often each gate fired across the session's shots, per reading (§7.5's count, per session)
> A click on a claim's mark anywhere (TrustFocus.open) opens that reading here and scrolls to it. Every word
> comes from TrustPresenter → trust_readings.h → the gate catalogue; nothing is decided here.
> objectNames: trustPanel, trustHead, trustSession, trustFlick.

- **Properties:** `int sessionType`, `bool framed`, `readonly string sessionDir`, `readonly var shot`, `readonly var session`
- **Uses:** `PpTrustReading`, `PpTypeset`, `TrustFocus`
- **Used by (2):** `ScreenSessionMode`, `ScreenWrist`

### PpTrustReading
`src/Gui/trust/PpTrustReading.qml` · 123 lines · root `Column`

> PPTRUSTREADING — ONE READING a gate touched, and every gate behind it (trust_readings.h READING), in the
> catalogue's words. The one block both homes use: a section of the Trust panel, and the in-place
> fallback under a claim (PpTrustInline). Typeset: the reading's label and its trust level on the head
> line with the figure the claim shows; under it, per gate, the title and what it compared — always
> shown — and, folded, why the gate exists, its threshold and which setups do not have it.
> `open` folds the whole reading; `highlight` washes it briefly when a click elsewhere brought you here.
> objectName: trustReading:<subject>; each gate trustGate:<gate>; each gate's fold trustWhy:<gate>.

- **Properties:** `QtObject roles`, `var reading`, `bool open`, `bool foldable`, `bool highlight`
- **Used by (2):** `PpTrustInline`, `PpTrustPanel`

### PpTrustSummaryFold
`src/Gui/trust/PpTrustSummaryFold.qml` · 122 lines · root `Column` · no direct `Theme.*` refs

> PPTRUSTSUMMARYFOLD — "How far can I trust this?" (docs/design/trust_and_gates_design.md §7.4): one quiet
> line under a head, CLOSED by default (Mark, 16 Oct 2026: the panels are busy enough for a novice), that
> opens to the summary trust_readings.h says once — the routes that timed the sequence, what passed in the
> Setup page's words, and per gate the readings it acted on, what it kept from being judged and what would
> lift it. A gated line's "Why ›" opens that reading in the Trust panel, or in place when the panel is off
> (TrustFocus, §7.0). Reads the session through its own SessionSeriesModel (the process-wide cache) and a
> TrustPresenter, as PpTrustInlineFor does.
> scope     "shot" (swingDir picks the shot) | "session"
> objectNames: trustSummaryFold, trustSummaryToggle, trustSummaryLine:<kind>.

- **Properties:** `QtObject roles`, `string scope`, `string sessionDir`, `string swingDir`, `bool open`, `bool printed`, `readonly bool atom`, `string openSubject`, `var summary`, `readonly var lines`
- **Uses:** `PpTrustInlineFor`, `TrustFocus`
- **Used by (5):** `PpChartsSessionView`, `PpChartsShotView`, `PpDiagAnalyse`, `PpDiagPrint`, `PpDiagReplay`

### TrustFocus
`src/Gui/trust/TrustFocus.qml` · 47 lines · root `QtObject` · **singleton**

> TRUSTFOCUS — where a click on a claim's mark goes (docs/design/trust_and_gates_design.md §7.0).
> THE PLACEMENT RULE. A reason sits where the claim is made, at the claim's weight (a quiet mark:
> "out of sight", "not judged"), and opens on demand. Its HOME is the Trust panel (PpTrustPanel), a
> stage panel like Charts or Session diagnostics. A surface asks `open(subject, swingDir)`:
> • the Trust panel is on → it opens and scrolls to that reading, and the surface does nothing more
> (`open` returns true);
> • it is off → `open` returns false and the surface expands the same reading IN PLACE, under the claim
> (PpTrustInline). A reason is never more than one click away, and never behind a setting.

- **Properties:** `string subject`, `string swingDir`, `int nonce`, `readonly bool panelOn`
- **Functions:** `open(subject, swingDir)`
- **Uses:** `SessionMode`, `ViewLayout`
- **Used by (16):** `PpCameraFrame`, `PpChWristShotSection`, `PpChartsShotView`, `PpDataViewer`, `PpDiagAnalyse`, `PpDiagReplay`, `PpFigureRow`, `PpGateNote`, `PpLedgerTable`, `PpLmGraphicsBody` (+6)
- **Exercised by tests/tools:** `charts_ks_gates`, `diag_not_judged`, `trust_summary`
