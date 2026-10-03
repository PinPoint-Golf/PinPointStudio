# Work-ons

*3 October 2026. Status: built, uncommitted, not yet run against the library.*

A **work-on** is a swing fault a golfer keeps producing and can practise their way out of.
Every session records its top three to five; the home screen lists what those records add up
to for the current athlete. This is the first, deliberately quiet, step towards the
diagnosing / improving split: it lets the concept be lived with before the UI is rebuilt
around it.

## 1. What was already there, and what was missing

| | |
|---|---|
| `diagnostics.json` per session | The ledger's rows — one per shot per condition. Evidence only, no verdicts. All seven library sessions have one. |
| `SessionDiagnosticsModel::closeSession()` | Freezes a session and folds it into `fault_profile.json`. **Nothing calls it.** Every ledger on the share reads `closed: false` and no fault profile has ever been written. |
| `fault_profile.json` | An incremental tally (`sessionsSeen`, `sessionsPattern`). It cannot be rebuilt, cannot survive a re-analysis without double counting, and treats "not a pattern" as "absent". |

So the catch-up is not a migration of old data; there is no "ended" state anywhere to migrate.
The design below does not introduce one.

## 2. The model

```
<athlete>/<session>/diagnostics.json   rows (existing)           the evidence
<athlete>/<session>/work_ons.json      this session's record     a stamped derivation
home screen WORK ONS                   aggregate of the records  never stored
```

**A session's record is stale whenever what it was derived from has moved** — there is none
yet, a swing arrived / left / was re-analysed, the pack or norms changed, or the selection rule
changed. `WorkOnsController::refresh()` finds stale sessions and re-derives them. Ending a
session is simply the moment one becomes stale, so "end of session" and "catch-up" are the same
code path, and a session the app quit out of is picked up at the next launch.

The record carries a fingerprint of its inputs (`kWorkOnRuleVersion`, the model's content stamp,
and each swing document's name, size and mtime). A matching fingerprint means the file is read
and nothing is derived.

Derivation goes through a private, panel-less `SessionDiagnosticsModel`. Its
`activateSession()` already back-fills shots the ledger never reduced and regrades rows whose
swing document or content has changed, so a record is always cut from a current ledger and
cannot disagree with the panel opened on the same session. It does **not** call
`closeSession()`: the stage ratchet and the fault profile are left exactly as they were.

The aggregate is a pure function of the records. Trash a session and its contribution is gone
at the next refresh.

## 3. Which faults a session records (`reduceSessionWorkOns`, `src/Analysis/work_ons.h`)

1. **Only session patterns.** The ledger's own gate (≥ 3 assessable shots, Wilson lower bound
   ≥ 0.30). Selection chooses among them; it never promotes a Watching condition.
2. **Only things a golfer can work on.** `Fault` and `Setup` conditions. `Outcome`, `Capacity`,
   `Intent` and `Equipment` are never listed. `Delivery` numbers are listed only to bring a
   session that measured fewer than three movements up to three.
3. **Order:** faults still firing before ones the golfer fixed inside the session
   (`resolving`); roots before symptoms (a pattern with another of the session's patterns
   authored as its cause); then how far outside the corridor it goes when it goes
   (`firingExcess`, the card row's own key), the Wilson bound settling near-ties.
4. **At most five.** No minimum: a session with two patterns records two, a three-ball session
   records none.

The record also stores, for every condition the session assessed at all, what the session
could say about it — needed by §4.

On the library (`2026-…_Mark-Liversedge_Wrist_NN`):

| Session | Shots | Patterns | Recorded |
|---|---|---|---|
| 07-04 _01 | 15 | 9 | over_the_top, flying_elbow, reverse_spine_p4, hips_closed_at_impact, pelvis_thrust_backswing |
| 09-09 _01 | 7 | 3 | reverse_spine_p4, sway, lead_knee_drifts_in_at_top |
| 09-15 _01 | 13 | 4 | ball_back, stance_narrow, trail_elbow_deep, reverse_spine_p4 |
| 09-15 _02 | 13 | 5 | club_short_of_parallel, ball_back, trail_elbow_deep, reverse_spine_p4, stance_narrow |
| 09-16 _01, _02, _03 | 3, 3, 1 | 0 | — |

On 07-04 the rule dropped `lie_steep_at_impact` (a delivery number), `early_extension` (a
symptom of two listed patterns) and the two mildest roots.

## 4. What the records add up to (`aggregateWorkOns`)

Per session, per condition, one of four statements:

| | |
|---|---|
| **Pattern** | the ledger's Pattern tier |
| **Quiet** | ≥ 3 assessable shots and the Wilson *upper* bound on the firing rate is under 0.50 — 0 of 4, 1 of 8 |
| **Inconclusive** | assessed, neither gate reached — 0 of 3, 2 of 7 |
| **Not measured** | no assessable shot |

**Absence is not improvement.** Only Pattern and Quiet sessions move a work-on's status:

- **Active** — a pattern in the latest session that could tell.
- **Easing** — quiet in the latest such session, a pattern before.
- **Cleared** — quiet in two such sessions running; leaves the list (counted, "1 cleared").
  A pattern in a later session makes it active again.

A fault no later session could measure stays active and says "Not measured since 4 Jul" —
which is what happens to every down-the-line fault once the second camera is not there.

**Order:** active before easing, then the ledger's own question asked across sessions: how
often does the fault show on the swings where it could be judged, at the cautious end of what
the evidence supports. Concretely, the Wilson lower bound on (fired, assessable) counted in
SWINGS, each session's swings counting 0.75^k (k = substantive sessions since; a three-ball
warm-up is not one).

- A swing the capture could not judge is in neither count, so a fault only the down-the-line
  camera can see is not marked down for sessions without one — it may have been there throughout.
- Age shrinks the evidence, not the rate: 15 of 15 three sessions ago is 6.3 of 6.3 today
  (bound 0.62), 0.55 after one more session that cannot look, 0.43 after three. It may equally
  have gone. A row placed this way is drawn with a hollow dot and "last seen 4 Jul".

Every row quotes "N of M swings" against the SAME M — every swing in the golfer's sessions (55
on the library). The swings a fault could not be judged on are said in the opened row.

| | Fired / judged swings | Bound |
|---|---|---|
| Reverse spine angle at P4 | 51 / 54 | 0.83 |
| Over the top · Hips under-rotated at impact · Pelvis moves toward the ball (4 Jul only) | 15 / 15 | 0.62 |
| Ball too far back | 23 / 33 | 0.57 |
| Stance too narrow | 31 / 53 | 0.54 |
| Trail elbow behind the body | 19 / 40 | 0.41 |
| Club never reaches the top | 26 / 53 | 0.38 |

Sway and Flying trail elbow are easing; Lead knee drifts in is cleared.

Every number in §3 and §4 is injected through `WorkOnOptions`.

## 5. The home screen

A `WORK ONS` section between the session launcher and `DEVICES`, in the device list's own
language: micro heading, one 40 px row each — dot, name, "51 of 55 swings". Five rows, then
"N more". While sessions are being read the heading says "reading 3 of 7 sessions".

Clicking a row opens it in place (one at a time):

- the pack's consequence sentence;
- one tick per session, oldest first (pattern / quiet / could not tell);
- the latest listing: date, "13 of 13 measurable shots", and the typical reading against its
  corridor — stated only when every firing was graded against the same corridor;
- "Follows from …" when the session had a pattern authored as its cause;
- the pack's first drill for the condition, where it authors one (two of the ten today);
- "Review the 15 Sep session →", which loads the newest session in which it was a pattern.

## 6. Files

| | |
|---|---|
| `src/Analysis/work_ons.h` | new — both reductions and `work_ons.json` (de)serialisation; header-only |
| `src/Analysis/tests/work_ons_test.cpp` | new — 46 checks |
| `src/Gui/diagnostics/work_ons_controller.{h,cpp}` | new — scan, fingerprint, derive queue, home list strings; context property `workOns` |
| `src/Diagnostics/tests/work_ons_controller_test.cpp` | new — catch-up, freshness, staleness, pause, athlete switch |
| `src/Gui/diagnostics/session_diagnostics_model.{h,cpp}` | `sessionWorkOns()`, `contentStamp()` — pure reads |
| `src/Gui/home/HmWorkOns.qml`, `ScreenHome.qml`, `shell/Main.qml` | the list, its placement, the review link |
| `src/Gui/main.cpp` | wiring: athlete folder, grade policy, paused while a session is live |
| `src/Gui/tests/probes/work_ons_home.qml`, `build/run-me/verify-workons.sh` | end-to-end probe |

Reproduce §3/§4 without touching the library: copy each session's `diagnostics.json` and its
empty `swing_*` folders somewhere, then `build/run-me/verify-workons.sh <that athlete dir> 5`.
`PINPOINT_WORKONS_DIR` points the running app's list at any folder of sessions.

## 7. Not done, and known limits

- **The first real launch writes** a `work_ons.json` into every session folder, and — as
  opening each session with the panel on already would — regrades any ledger whose swing
  documents or content have moved since it was written, which reads those documents.
- The review link is wired but was probed only as far as offering the right session; the
  scratch copy has no swing documents to load.
- The fault profile is untouched and still never written. The records here supersede it; the
  Cold stage's "usually yours" expectations could be fed from them.
- Dismissing a work-on, choosing one as the focus of a session, and monitoring it live are the
  improving mode, not this.
- Eight of the ten listed conditions have no drill authored.
- One golfer, seven sessions: the 0.75 fade, the 0.50 quiet gate and "two quiet sessions" are first
  guesses, not tuned.
