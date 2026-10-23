# Session report — design

*20 October 2026. Status: designed and mocked with real data. Built 20–21 October (uncommitted, awaiting Mark's review).*

A **session report** is a PDF that a coach sends to a golfer at the end of a session. It is made
on demand from the session list, from that session's diagnostics. It carries the studio's brand,
so it has to be good enough for a professional to put their name to.

## Deliverables

| | |
|---|---|
| **This document** | What the report says, in what order and why; where every word and number comes from; the rules that keep it honest; how it is generated; the build plan. |
| **The mock** | Rendered to `/mnt/swingdata/scratch/session-report/out/<name>/` by `tools/session-report-mock/render-session.sh` (README there); first made as `build/run-me/session-report/session-report-mock.pdf`: 10 pages of A4 from a real session (Nick Henn, 20 October, session 3, 10 swings with a 7 iron, two cameras, GC Quad). It is printed through the app's own PDF path (`PpPdfPrinter` → `PdfDocumentWriter`) at 300 dpi. To reproduce it: `build/run-me/session-report/render-mock.sh`. |
| **The mock's data contract** | `tools/session-report-mock/assemble.py` produces `report.json`, the single map the report binds to. It is a prototype of the C++ `SessionReportModel` (§11). Every sentence in the mock comes from a template over model and pack fields, except the coach's note, which the coach types. Content that the pack does not hold yet is marked **PROPOSED** in `assemble.py` (§5). |

**Done means:** Mark has judged the mock. The decisions in §14 have answers. The build (§13) then
runs phase by phase, each phase judged in the app before it is committed.

---

## 0. What the report is for

A coach ends a session and wants to send the golfer one thing. The golfer should be able to read
page 1 alone and know three things: what went well, what we found, and what to do first. Pages 2
and 3 are for golfers who want to know why. Pages 4 to 7 hold the detail on each work-on. The
rest is reference material. This follows the Charts page rule: novices stop early, curious
readers go deeper ([feedback_charts_page_pattern]).

These principles come from the diagnostics model and are restated here because a client document
tests them hardest:

1. **One order everywhere.** "Work-on 1" means the same habit in the report, on the home screen
   and in the Swing diagnostics screen. The report explains that order. It never invents a
   second one.
2. **Report what we saw** ([feedback_pps_does_not_model_intent]). A habit can be deliberate. The
   report does not model intent and does not excuse anything.
3. **Seen together is not cause.** A link between two habits is described as "fits the coaching
   model", or as "followed it in the right order on 7 swings". It is never described as cause,
   unless the session supports the link (`linkSupported`), and even then only as "goes away when
   the first one does".
4. **Measured before inferred, and the unmeasured are named, never dropped** (Mark, 20 October). A
   cause the cameras could not see is never a headline. On Nick's session the model's likely driver
   is *S-posture*, labelled "inferred, not measured" (its lumbar-curve measure has no producer). But
   when the evidence for a cause is thin, the causes we **could not check** are the most useful thing
   the report can give the golfer and coach: other things to consider. So every work-on page lists
   every authored cause in three groups: spotted, could be behind it, ruled out (§3.3).
5. **No composite score, no percentages for anything inferred** (design §5.3, A8.6). Counts are
   swings ("9 of 10 swings"). Readings use whole units, or the model's own decimals, and are
   never rounded by σ ([feedback_no_sigma_rounding]). A missing value is shown as "-"
   ([missing-metrics-show-dash]).
6. **The golfer's own swings are the evidence.** Every claim on a work-on page can be checked
   against the dots on that page.

## 1. What already exists, and how the report brings it together

| Existing piece | What the report takes from it |
|---|---|
| `SessionDiagnosticsModel` (cards, watching, chains, driver, bookends, `spreadOf`, `historyOf`, `conditionDetail`, `sequenceConsistency`, `notJudged`) | Every habit, count, value, target range, run of values and link grade. |
| `reduceSessionWorkOns` (`src/Coaching/work_ons.h`) | The list and its order, **derived fresh** from the model, never read from `work_ons.json`. Nick's `work_ons.json` on the share still says 12 measurable swings on a 10-swing session; it was written before two swings were deleted, and is re-derived only at the next catch-up. |
| `aggregateWorkOns` and the sibling sessions' records | "Earlier today" and "across sessions": Active, Easing, Cleared, and the tick per session. |
| The pack (`core.json`, `drills.json`, `screens.json`) | Golfer phrasing (`golfer`, `golferWell`, `golferWhy`), aliases, consequences, drills, screens, causal edges. |
| Launch monitor block per swing | Ball numbers, and the "what came with it" facts where the condition reads an LM measure. |
| `PpDiagPrint` (the analyst's report) | **Kept, and appended as an optional technical appendix** ("For your coach: the full diagnostics"). It is the same register, unchanged, printed after the client pages from a fresh page. The client report is the front of the same document, not a rival to it. |
| `PpTrustSummaryFold` / `notJudged` | "What we could not check", rewritten in golfer words and said once, at the back ([feedback_charts_page_pattern]: trust is said once). |
| `PpPager`, `PpTypeset` print roles, `PpPdfPrinter`, `PdfDocumentWriter` | The page machinery, fonts and PDF/A output. The report is another print register. |

## 2. The reader and the voice

The report speaks to the golfer as "you", in plain English. The analyst's words map to the
golfer's words as follows. The mapping is applied in C++ and never in QML:

| Model | Report | Note |
|---|---|---|
| condition / characteristic, Pattern tier | **habit** | Explained once on the back page. |
| work-on | **work-on** | Kept: it is the product's word, and the home screen uses it. |
| fired / clean / not assessable | **showed / did not / could not be measured** | |
| corridor; pass edge | **target range** | "Coaching judgement, not fitted to you" is said once, on the back page. |
| shot | **swing** | The ball numbers stay "swing" too. |
| Watching | **seen, not yet a habit** | |
| outcome conditions | **what the ball did** | |
| P1…P10 | the position name first ("at the top"), with P4 in small type | |

The words come from the pack: `golfer` for what we saw, `golferWell` for the goal and for
strengths, and `golferWhy` for why it matters. A condition that may appear in a report must
have all three (§5).

## 3. The document, page by page

The mock is the reference for this section. Page numbers refer to it.

**Page 1. The session on one page.** This page is the whole report for a golfer who reads
nothing else.
- Masthead: studio monogram or logo, studio name, coach and title, "Session report".
- Kicker: date · club(s) · number of swings · "Session 3 of 3 today", when there were sibling
  sessions that day. Then the golfer's name in display type.
- **Headline:** one sentence from a template: *"{N} habits showed on {every/almost every} swing.
  We start with {part of the swing}: {goal of work-on 1}."* There are variants for a session
  with one habit, for a clean session, and for a session with too few swings (§3.1).
- **The coach's note:** optional, typed at generation time (§8), and signed.
- Three columns:
  - **Going well**: three strengths (§3.2).
  - **What we found**: the work-ons in golfer phrasing, with counts.
  - **What to work on**: goals in work-on order, with work-on 1 emphasised, each with its first
    drill and a page reference.
- **The ball** (only when the session has launch-monitor data): eight figures, each a typical
  value (the median) with its range. The figures are carry, ball speed, club speed, smash,
  launch, spin, club path and widest offline.

**Page 2. Swing by swing.** One grid: each row is a habit, each column is a swing in the order
hit. A dot means the habit showed on that swing; a stub means it could not be measured. Rows are
grouped into the five work-ons, *seen, not yet habits* (watching rows that fired on at least 3
swings), and *what the ball did* (outcome rows). Under the grid, carry is drawn as bars aligned
to the swings. Then come the **reference swing** (§6) as four stills (address, top, impact,
finish) and **Earlier today**: the same habits across that day's sessions, with "habit 8/8",
"4/10" and one generated sentence about what changed.

**Page 3. Why this order** (§4). The three ordering rules, each with what it did today. Bars
show the quantity that actually set today's order. A swing timeline places each work-on where it
happens in the swing. Three short columns follow: *Linked, not proven*, *Not on the list, and
why*, and *Worth checking next time*.

**Pages 4 to 6. One page per work-on, for the first three.**
- Kicker: work-on n of N · position · count.
- Title: the goal (`golferWell`, as an instruction). The coach's term and its aliases are in
  small type.
- **What we saw:** template — *"{golfer} — on {count}, {trend}. Typical {measure, golfer name}:
  {median}, against a target of {range}."* A run chart sits beside it: one dot per swing, the
  target range as a wash, in the golfer's sign (§5).
- **Pictures** (§6).
- **Why it matters:** `golferWhy`.
- **What came with it:** the condition's authored effects that the session judged. The ones
  that showed are given with counts and "fits the coaching model, not proof". The ones that did
  not show are given as their `golferWell` ("on every swing your hips turned in place"). This is
  where the ball comes in, when an effect is an LM-measured outcome.
- **What might be behind it:** every cause the pack authors for the habit, in three groups, with
  a lead sentence for the case (§3.3). A cause that is itself a work-on appears under "spotted",
  with its work-on number.
- **What to do:** the feel (`cue`, §5), then one drill card in full (what you do, what it is for,
  and *if it doesn't work*) and any further drill in compact form.
- **Check it yourself:** how to see it without the studio (`check`, §5).
- **Next time:** the target in golfer words and today's value. For example: *"Trail-knee bend
  lost by the top: less than 12°, on most swings. Today: 22°."* This is the pass edge on the side
  the golfer misses. It is the sentence the next session's report answers.

**Pages 7 and 8. Work-ons 4 and 5.** The same page as work-ons 1 to 3, without pictures. (The
mock first put both on one page; with the causes block they no longer fit, and the real register
flows on `PpPager` anyway.)

**The Posture page** (added 22 October, Mark: the Charts Posture section prints as a page of the report,
`posture_chart_design.md` §5.7). After the work-on pages and before "Between now and next time": it is a client
page about this session's swings, so it sits with the work-ons, and the back matter (practice and numbers, About,
the appendices) follows it. It starts a fresh page and may run on ("Posture, continued"). It is the Charts Posture
section's *session* reading, printed from the same presenter output (`postureSessionReportPayload`, nothing
picked, the Auto pattern): the head and pattern line; the session profile, one row per reading, P1 to P8, the
pattern's band as a wash in the reading's own metric colour (Folio light), the fault line as a hairline, the
session's box per position (`PpBoxPlot`), and under each column the median ("-" for none), how wide the middle half
is, or the side of the band a median falls on (warn, a word); the ⚠ and its few words; "What moved" at the top and
at impact; the pattern by shot; the departures as counts; how repeatable it is; and the trust note, said once.
The picture strip (address, top, impact) joins it in stage 5, between "Repeatable?" and the trust note. No posture
reading in the session: no page. Not on the cover's contents line (the line shares the folio with the studio's
contact). `SessionReportModel` reads it on a worker while the documents are read: a private `SessionSeriesModel`
(the Charts panel's loader, inline) builds the session snapshot and the presenter's payload is cut to the map's
`posture` block by `report_posture.h` (the strip renumbered to the report's swings, the lists capped).

**The Wrist page** (added 23 October, Mark: the Charts Wrist section's Session view prints as a page, after the
Posture page, mirroring its order). The head ("Graded as a Cupped wrist on every swing"), a Style line and a Score
line; "Checkpoints across the session", one row per banded wrist DOF, P1 to P8, the band of the **dominant style**
(the style most swings were graded as; a tie goes to Neutral, then Bowed, then Cupped, and the trust note says so only
when a tie decided it) as a wash, the session's box
per checkpoint, the median ("-" for none) and "N° wide" under each column, a median outside the band in warn with the
side it fell on ("open", "closed": a word, never colour alone), the caption saying which band is shaded and, in a
mixed session, that each swing was graded against its own style's band; "Wrist score, swing by swing" (the score run
and its box); "Wrist style by swing" (the resemblance table, every swing); "What keeps firing" (each finding, its count
words, a mark per swing); "Repeatable?"; "What moves with the lead wrist at impact" (the Relations ledger, its rows
and chance line verbatim); and "How far can I trust this?", said once (the presenter's `session.trust`, the same
lines the Charts fold shows, plus the wash rule when more than one style was graded). Nothing picked, Auto style,
"swings" everywhere; the paper never names the Auto control ("the style it was graded against", "its own style"), the
screen keeps it. A camera-only session's caption is "Shaded: the Neutral band." and its trust note says why. Gated on DATA: any
wrist DOF read, a camera-only session included (the trail wrist from the face-on camera); no wrist reading, no page.
Not on the cover's contents line. The same worker read builds the snapshot once and returns both payloads
(`readChartsPages`: `postureSessionReportPayload` and `wristSessionReportPayload`); `report_wrist.h` cuts the map's
`wrist` block, its profile rows on the posture row's keys so one QML profile component draws both pages.

**Page 8. Between now and next time.**
- **How to practise a change:** four steps, written as studio text (§7). The coach edits them
  once and every report carries them.
- **Your numbers:** every swing from the launch monitor, with a Typical row (medians) and a
  "Habits" column showing how many of the five work-ons that swing had. This table flows on
  `PpPager` and repeats its heads when a session has many swings.

**Page 9. About this report.** How we measured. What a target range is (coaching judgement
versus data; not fitted to you). What we could not check today, from `notJudged`, in golfer
words, ending with "Nothing in this report depends on them" only when that is true. A short
glossary. Then the studio sign-off box.

**Launch monitor appendix** (added 21 October, Mark: "an appendix which a) reproduces the figures
as a headline then b) lists out every shot's LM data across ball and club then c) provides
summary statistics for the set, including SD"). Drawn whenever the session has launch monitor
readings, after About and before the technical appendix, as part of the client galley
(`PpLmAppendix`), so it flows and repeats its table heads. Like the technical appendix it is not on the
cover's contents line, which shares the folio with the studio's contact and would push it into an ellipsis.
Three parts, each from a fresh page:

1. **The figures.** For each club, in bag order: a heading ("146 yd average carry, 22 yd from
   shortest to longest, 10 shots"), then the launch monitor panel's own Session figures, embedded
   in the panel's print layout in Folio light. Those are the ball figure (dispersion,
   launch × spin, six consistency tiles) and the club figure (the impact map with every strike,
   path × face, eight delivery tiles). No shot is singled out, so the panel's "this shot" ring
   and tick are not drawn and the captions do not mention one.
2. **Every shot.** A Ball table (launch, spin, flight: up to 13 readings) and a Club table
   (delivery and strike: up to 13), one row per swing, grouped by club. A column appears only if
   some swing in the session has that reading. Values are as the device reported them, signed.
   The note gives the sign convention for the golfer's hand, and only the marks the table uses
   ("out", "*"). Swings with no reading are named ("No reading on swings 1–12.").
3. **The summary.** Per club: shots, average, SD (n − 1, from three shots, else "-"), lowest,
   highest and range, for every reading, under Ball and Club sub-heads. Average and SD are in
   bold.

It reads the panel's own model. `SessionReportModel` builds a private `LmSessionModel` over a
private `ShotListModel` of the session, and the figures (the map's `lmAppendix.graphics`) and the
tables come from that one model. So the appendix agrees with the panel on everything: each
shot's effective club (a shot the readings put under another club is listed there, marked *),
the shots left out (shown as "out", in none of the statistics) and every number (`lmSessionStats`).
That is also where it differs from "Your numbers" on the client pages, which groups by the
recorded club and quotes the median.

**Technical appendix** (optional, off by default, §8): the existing `PpDiagPrint` pages, from a
fresh page, for the coach's own file or for a golfer who wants everything.

### 3.1 Sessions that are not like Nick's

| Session | What changes |
|---|---|
| No launch monitor | No ball block on page 1, no numbers table, no ball rows in the grid. "What came with it" keeps its body effects. |
| One camera | Habits that only the missing camera can see are not on the list. The back page says which positions could not be seen. The pictures use the camera there is. |
| Fewer than 3 swings | A one-page **note**, not a report: the swings, the ball numbers, and "too few swings to call a habit" (the panel's Cold rule). |
| No habits (a clean session) | Headline: *"Nothing repeated often enough to call a habit."* Page 1 gives strengths and the watch list. No work-on pages. Page 2 and the numbers stay. |
| A work-on easing during the session (`resolving`) | Its page leads with the change ("it stopped after swing 6"). Rule 1 on page 3 says that it moved this item down. |
| Several clubs | The kicker lists the clubs. The ball block is split per club, because medians across clubs mean nothing. "Typical" values are stated only under the one-corridor rule `work_ons.h` already applies. |
| A declared focus (focus contract) | The page for the focused habit gets a *before / after the focus* split. The Moved-together grade, when earned, is the one place a report may say "when this improved, that followed". |

### 3.2 Strengths: what "Going well" picks

Strengths come from the session's judged conditions that never showed on a swing, with at least
8 measurable swings. They are ranked by:
1. **Being an authored effect of a work-on.** This is the "it has not spread" evidence: Nick's
   trail knee straightens, but his hips still turned in place on every swing.
2. **The condition's prominence:** being clean on a common fault counts for more.
3. Movements before outcomes.

One outcome and two movements are shown. **An outcome counts as a strength only when its whole
axis was clean.** On Nick's session "Nothing off the heel" was clean, but his strikes leaned to
the toe and toe strike showed once. Calling the heel a strength would have been true and
misleading, so the rule drops it and "No slices" takes its place.

### 3.3 What might be behind it: spotted, could be, ruled out

Each authored cause (`edges`, type `causes`, into the habit) is sorted by what this session knows
about it. The session's own evidence comes first: a cause it judged on 3 or more swings is
judged, whatever its measure's status says.

| Group (as printed) | What puts a cause there | How it is shown |
|---|---|---|
| **What we spotted** | It is a session habit itself | With its count and work-on number. The note says *it fits, but isn't proof*, unless `linkSupported` holds, when the lead says the swings point to it. |
| **Could be behind it — we couldn't check** | Any of three reasons, grouped and labelled by **how it could be checked**: *A quick physical test would tell* (a latent capacity with a screen); *Your coach can look* (its measure is planned or has no producer); *Hidden on today's swings* (measurable, but judged on fewer than 3 swings: a gate, a missing camera) | Strongest authored edge first, six at most, then "N more, less likely". Capacity conditions use golfer names ("Lead-hip mobility", not "Limited lead-hip internal rotation"). |
| **Ruled out — measured, and fine** | Judged on 3 or more swings and not a habit | Strongest first, four at most, then "N more". It means *on these swings, with these cameras*; the back page says so once. |

The lead sentence depends on the case:
- **A cause is spotted:** *"One possible cause showed up on the same swings. Because both happened
  on every swing, we can't prove one drives the other — so here is what we spotted, what we
  couldn't check, and what we've ruled out."*
- **Nothing is spotted, but some causes could not be checked:** *"We couldn't pin down what drives
  this one today. Here are the possible causes…"*
- **Everything authored was ruled out:** *"We checked every cause we know of for this one, and
  each was fine — so it may come from something we can't yet put a name to. Worth talking
  through with your coach."* This is Nick's work-ons 1 and 2. Each has **only one** authored
  cause in the pack, and that cause was clean on every swing. The statement is honest, and it is
  also a signal that the pack's causes for those two habits are thin. For example, the trail
  knee straightening has no capacity cause, though a trail-hip mobility limit is the classic one.
  Growing those edges is part of the content phase (§13.1).

Page 3's **Worth checking next time** collects the "could be behind it" causes across all the
work-ons and leads with the ones behind the most work-ons. On Nick's session five physical
tests each sit behind two work-ons ("Lead-hip mobility: could sit behind work-ons 3 and 4"),
which is the most useful sentence the report can give a coach about the next session.

## 4. Why the work-ons are in this order

This is the section Mark asked for, and the report has to explain the **real** rule, not a
nicer-sounding one.

### 4.1 The rule today (`reduceSessionWorkOns`, rule v3)

1. **What is still happening comes first.** A habit that stopped during the session
   (`resolving`) goes to the back.
2. **Causes before the things they cause**, but only when the session **supports** the link
   (`linkSupported`). That needs a cause that did not fire on every swing, at least 8 swings
   where both could be judged, and a one-sided Fisher test at 0.05 or better. An authored link
   that the session cannot test is named (`mayFollow`) but does not demote anything.
3. **Then the biggest miss first:** `firingExcess` (how far outside the target range, measured
   against the range's own width), with the Wilson bound settling near-ties.
4. Five at most. Setup and fault conditions only. Delivery numbers fill in only when there are
   fewer than three.

### 4.2 What the report says about it

The rules are part of the report's data, not just its explanation. `WorkOnEntry` gains a
**`reasons`** record saying which rule placed it:
- `resolving`;
- `rootBecause` (ids it is upstream of, with the 2×2 table);
- `behind` (the supported cause it waits for);
- `mayFollow`;
- `excess`;
- whether the session could test links at all (`linksTestable`, false when every pattern is
  range-restricted).

Page 3 prints each rule with a **"Today"** line generated from those reasons, plus the bars of
the quantity that decided. Nothing on page 3 is written by hand.

### 4.3 What Nick's session shows, and a decision for Mark

On Nick's session, as on most habitual golfers' sessions, **every habit fired on every swing**.
So rule 2 can never apply (no variance to compare), and the order falls through to rule 3,
magnitude. The report says so plainly ("It could not be tested today…"), and that is honest. But
"the biggest miss first" is a weak reason to start a golfer on one habit rather than another. A
degree of knee flex and a percent of hand rise are made comparable only by dividing each by its
range width.

Jess's session (§9.3) makes the same point more sharply: magnitude put chicken wing, a
follow-through habit, first.

**Option A (recommended): rule v4.** When the links among the listed habits cannot be tested,
order by **position in the swing** (setup, then backswing, top, transition, downswing, impact)
before magnitude. The swing's order is a fact, not an inference. It is also how coaches sequence
work, and the pack's own edges mostly run that way. It changes the home screen order as well,
which is the point of principle 1. On Nick's session the order would become: knee (backswing),
reverse spine (top), over the top (coming down), then hips and early extension (impact, by
magnitude). The page 3 timeline already shows the reader why.

**Option B:** keep rule v3. The report keeps saying "the biggest miss first". The timeline
caption then says, when it is true, that work-on 1 is not the earliest in the swing and that
the coach may choose to start earlier. The mock already generates both versions of that
caption.

Either way the explanation is generated from `reasons`. Option A also needs `kWorkOnRuleVersion`
4, a change to `work_ons_test`, and a library catch-up (every `work_ons.json` is re-derived on
the next launch, which is the existing stale-record path).

## 5. "What to do, and why": the content model

A work-on page is only useful if the pack can answer "what do I do about it?" Today it mostly
cannot. Of 110 setup and fault conditions, **16 carry a drill** (12 drills in total), and 86
carry `golferWell`/`golferWhy`. Two of Nick's five work-ons have no drill.
[model-completeness-is-the-point] applies: the build authors the content; the report never
prints "no drill yet".

New pack fields. The mock's PROPOSED blocks show each one in use:

| Field | On | What it is | Example (mock) |
|---|---|---|---|
| `goal` | condition | `golferWell` as an instruction, used as the page title | "Keep the flex in your trail knee" |
| `cue` | condition | One feel or swing thought. Written as a feel, never as a promise | "Keep your backside on an imaginary wall behind you all the way to impact." |
| `check` | condition | How to see it without the studio: camera angle, position, what to look for | "Film from down the line. At the top, your trail knee should look about as bent as it did at address." |
| `picture` | condition | `change` (address vs the position), `pair` (closest vs furthest), `typical`, or `none`; plus `camera`, `phase`, and the overlay `line` (`knee`, `spine`, …) | knee: `change`, DTL, P4, `knee` |
| `golferName`, `golferSign` | measure | The number in the golfer's words, and whether the words carry its sign (so the value and the chart are shown as a magnitude) | "trail-knee bend lost by the top", sign −1 |
| `golfer`, `golferWell`, `golferWhy` | every condition that can appear in a report | Includes delivery and outcome conditions, which have none today | `lie_steep_at_impact`: "the club coming back to the ball toe-up" |
| `golfer` | every capacity condition (named under "could be behind it") | The golfer's name for the body quality the screen tests | `limited_lead_hip_ir`: "Lead-hip mobility" |
| more `causes` edges | habits with one or no authored cause | So that "we checked everything we know of" is rare and means something | trail knee straightens ← trail-hip mobility (to be checked with a citation or marked `practice`) |
| `drills` | every setup and fault condition | At least one, from `drills.json` | new `drill.impactHold`, `drill.topTilt` (mock) |

The authoring rules are those of `drills.json`, applied to the new fields as well:
- a drill states what the golfer does and what it is trying to change;
- `targets` is intent, never a promise;
- `note` says when the drill is the wrong answer (a mobility limit before a practice one).

There are no citations unless they have been verified. Studio-wide practice guidance (how to
practise a change) is **studio text**, not pack text, because it is the coach's method. The
app ships a default that the coach edits (§7).

The **gate** is a test that every condition the work-on rule can list has `golfer`,
`golferWell`, `golferWhy`, `goal`, `cue`, `check`, `picture` and at least one drill. Every
measure such a condition reads must have `golferName`. This joins `core_pack_test`.

## 6. Pictures

- **Which frame:** the swing's own phase instant (`analysis.phases`) for that camera, matched to
  the encoded frame by timestamp. This is the same frame the replay shows at that P-position.
- **Which swing**, chosen by the condition's `picture`:
  - **change**, for a measure read from address (knee flex lost, posture lost): the typical
    swing (median value) at address and at the position, with the line drawn on both. On Nick's
    session this is what makes the knee visible: bent at address, straight at the top.
  - **pair**: the swings closest to and furthest from the target **on the measure the page
    draws**. The model's bookends use the driving measure, which can differ (for early extension
    they disagreed). A pair is shown only when the two values differ by at least half the
    target range's width.
  - **typical**: one swing, with "every swing looked much like this one", when no pair would
    show a difference a golfer could see. On Nick's session, hips and posture both fall back to
    this.
- **The line:** the pose keypoints (`poseDtl` / `pose2d`, `smoothed`, matched by `t_us`) for the
  segment the measure reads. The swing's line is drawn solid in warn; its address line is drawn
  dashed white when the measure is a change from address.
- **Privacy:** every still is cropped to the golfer. The production crop is the pose `cropRect`
  plus a margin. Nick's face-on frames show people sitting in the bay, and nobody but the golfer
  leaves the studio. The back page says so.
- **Reference swing:** the swing with the smallest summed miss across the work-ons, each miss
  measured in units of its own range's width. It is shown at four positions. Its words name what
  was best about it, and say "your longest" only when it was.

## 7. The scaffolding: the studio, its coach and the golfer's contact details

The report needs three sets of details the app does not hold today. All three are entered once,
in the app (no native dialogs, no menus: [no-menus-all-in-app]), and read by `SessionReportModel`
when a report is made. Until they are filled in, the report sheet says what is missing and still
makes the PDF: the studio falls back to the monogram "PinPoint Studio" and no coach line, and the
**Email** link waits for an address.

### 7.1 The studio (Settings → Studio)

One studio, and one coach, per installation.

| Field | Used for | Notes |
|---|---|---|
| Studio name | Masthead, running head, sign-off, email subject | Required to look finished |
| Logo | Masthead, sign-off box | PNG or SVG, chosen with the in-app file browser and **copied** into the app's data folder (`<AppData>/studio/logo.<ext>`), so moving or deleting the original never breaks a report. Without one, a monogram is generated from the name |
| Accent colour | Masthead rule, kickers, rank numerals, work-on 1, the target-range wash | One colour. The page warns when it is too light to read as text on white |
| Address (optional), phone, email, website | Footer and sign-off box | Each line prints only when it is filled in |
| Booking link or sentence | Sign-off box | "Book your next session at …" |
| How to practise (four steps) | Page 8 | Ships with the mock's text as the default; the coach edits it |
| Email subject and message | §8.3 | Templates with `{first}`, `{date}`, `{coach}`, `{studio}`; defaults below |
| The coach | Sign-off, page header, reply-to | §7.2 |

The report **always prints in Folio, light**: Inter, with Georgia (or Gelasio) for display. It
does not follow the app's on-screen theme, because a client document has one look. The accent
colour replaces `Theme.colorAccent` in the report's roles only. **Faults stay
`Theme.colorWarn`**, and metric curves keep their own colours
([feedback_metric_colour_is_identity]). A warn mark always has a word beside it.

### 7.2 The coach (Settings → Studio)

**One coach per studio** (Mark, 20 October). The coach's details sit on the Studio page beside the
studio's own:
- name;
- title ("Head coach");
- email address, used as the reply-to on a sent report;
- phone number (optional);
- optional photo for the sign-off box.

Every report is from that coach. There is no coach picker and nothing coach-specific is stored
per golfer or per report. If several coaches are wanted later, this section becomes a list, and
a report records which coach it came from.

### 7.3 The golfer's contact details (golfer details page)

The athlete record gains a **Reports** group on the golfer details page. It is stored the way
`dob` and `sex` already are: extra fields written through `AthleteController::updateAthlete`,
in the athlete's INI group. No new store is needed.

| Field | Notes |
|---|---|
| `reportEmail` | One address or several, comma-separated. For a junior this is the parent's or guardian's address, and both can be listed. Checked for shape on entry (something@something.tld); never verified. |
| `reportGreeting` | The name the email and page 1 use ("Nick"). Defaults to the first word of the name. |
| `reportPhotosApproved` | **Shown only for a junior** (under 18 on the date of birth): a checkbox, *"A parent or guardian has approved sending swing photos"*. Unticked by default. |
| `reportPhotosApprovedOn` | The date the box was ticked. It is cleared if the box is unticked, and shown beside the box: "approved 20 Oct 2026". This gives the coach a record of when consent was given. |

The group has one line of explanation: "Used only to send this golfer their session reports."

**Photos follow one rule:**

| Golfer | Swing photos in the report |
|---|---|
| Adult (18 or over on the session date) | Yes |
| Junior, with `reportPhotosApproved` ticked | Yes |
| Junior, without approval | **No.** The photo slots show the run chart full-width, and the reference swing is left out. |
| No date of birth | Treated as an adult. The report row says "No date of birth — photos included", so the coach can add one. |

Age is taken **on the session date**, so an old session's report does not change when the golfer
turns 18. There is no per-report override. Changing the answer happens in one place: the golfer
details page.

**Privacy.** The address and the approval stay on this machine, in the app's settings, beside the
rest of the athlete record. They are used for one thing: addressing and composing a report the
coach chooses to send. They never go into a swing document, a session folder or the share. A
report's PDF goes only where the coach sends it. Deleting the athlete deletes them.

## 8. Generating it

### 8.0 Where the coach finds it: a session list with three links

Mark, 20 October: the report lives in **a list of the golfer's sessions**, shown in two places:

1. **At the bottom of the Home screen**, under the current golfer's other sections, as
   **"Sessions"**. It shows the newest ten, then "Show all N sessions", which opens the tab
   below.
2. **On the golfer details page, as a tab.** The page gains two tabs: **Details** (today's form,
   unchanged) and **Sessions** (every session, newest first). The tab appears for a saved golfer
   only; a new golfer has no sessions.

Both places use the same row component, `PpSessionReportRow`, over the same list model. Each row
is one session:

```
Tue 20 Oct 2026 · Session 3      7 iron · 10 swings · Face-on, DTL, GC Quad
Work-on 1: Keep the flex in your trail knee                Opened in Mail 20 Oct 18:40
Open session      Report      Email to Nick
```

- The **first line** gives the date, the session number that day, the clubs, the swing count and
  the devices. The devices are the same tags the session drawer already shows (`92407144`).
- The **second line** gives the session's work-on 1, read from its record so the coach can find
  "the session where the knee came up", and the report state on the right.
- The **three links:**
  - **Open session** loads the session for review and opens it in the app. This is the path the
    work-ons "Review the session" link already uses: `sessionReviewController.loadSession`, then
    navigate.
  - **Report:** if the session has a current PDF, the link opens it in the system's PDF viewer
    (`QDesktopServices::openUrl`; no dialog). If there is no PDF yet, the link reads **Make
    report** and opens the report sheet (below). If the PDF is out of date (§8.2), the link reads
    **Remake report**, and opening the old one is still offered as a secondary link.
  - **Email to Nick** hands the PDF to the coach's mail app (§8.3). It is shown, but inactive with
    the reason in place, when there is no PDF yet ("make the report first") or no address ("add
    Nick's email"). Each reason is a link to where it is fixed. If no PDF exists, Email makes one
    first, then hands it off.
- **Report state words:** "No report" · "Report made 20 Oct" · "Opened in Mail 20 Oct 18:40" ·
  "Out of date — remake". These are plain text in `ink3`, never a badge, and never colour alone.

A session with fewer than 3 swings shows **Open session** only. Its "report" would be the
one-page note from §3.1, which is not worth sending.

**The live session** shows at the top of the Home list while it runs, with **Open session** only.
Its report can be made once the session ends, because a report of a session still in progress
would be out of date as soon as it was sent.

The carousel's session drawer (`PpSessionDrawer`) gets **no report actions**. It stays a
navigation control.

### 8.1 Making it: the report sheet

**Make report** and **Remake report** open a small in-app sheet (`PpPopoverCard`) holding:
- the coach's note (optional, prefilled from the last report of this session);
- "Include the technical appendix" (off);
- "Include earlier sessions" (on);
- a line saying whether swing photos are included, with the reason ("Nick is under 18 — no
  parent or guardian approval recorded"). This line links to the golfer details page;
- **Make the PDF**.

If a detail is missing, the sheet says which, as a link to where it is entered: "No studio name
yet — add it in Settings → Studio".

**What runs:**
- A `SessionReportModel` (C++) owns a private, panel-less `SessionDiagnosticsModel`. This is the
  pattern `WorkOnsController` already uses.
- It calls `activateSession(dir)` and `waitForIdle`, then reads the sibling sessions' records and
  the swing documents' launch-monitor blocks, and builds the `report` map. Studio, coach and
  golfer details go into that map's `brand` and `meta` blocks, exactly as in the mock's
  `report.json`.
- Stills and overlays are made on a worker thread from the swing's video, through the replay's
  own frame source (not ffmpeg).
- The register `PpSessionReport` lies on an A4 sheet that is never shown, in the main window, and
  `PpPdfPrinter` prints it.
- Progress shows on the sheet ("Page 4 of 10"), and the row says "Making report…".

**Where it goes:** `<session>/report/<yyyy-mm-dd>_<Golfer>_session-report.pdf`. It is written
through `QSaveFile` (the writer already does this), so a remake replaces it whole.

Beside it, `<session>/report/report_meta.json` records:
- the note and the options;
- whether photos were included;
- when the report was made;
- the PDF's SHA-256;
- the **fingerprint** of what the report was made from (§8.2);
- the send log (§8.3).

It records no email addresses, because the session folder lives on the share.

**Nothing is written into the session's analysis.** The report reads. It never moves a ratchet,
closes a session or writes `diagnostics.json`.

### 8.2 When a report goes out of date

A report is out of date when what it was made from has moved:
- a swing was deleted or re-analysed;
- the pack or the norms changed;
- the work-on rule changed;
- the golfer's photo approval changed;
- the studio's details changed.

The fingerprint is the work-on record's own (`WorkOnsController`: rule version, content stamp,
each swing document's size and mtime), plus a stamp of the studio and photo inputs. The row
compares it without opening the session, so the list stays fast.

### 8.3 Sending it

A `mailto:` link cannot carry an attachment, so sending needs more than that. The options:

| | How | For | Against |
|---|---|---|---|
| **A. Hand off to the coach's mail app** (recommended for v1) | A new message is opened in the coach's own mail program with everything filled in: to (`reportEmail`), reply-to (the coach), subject, message and the PDF attached. The coach reads it and presses Send. macOS: `NSSharingService` (compose email, with recipients, subject, body and the file). Windows: Simple MAPI (`MAPISendMailW`), which Outlook and other default mail programs answer. Linux: `xdg-email --attach` | It goes from the coach's own address, appears in their Sent folder, and replies come back to them. The app stores no passwords. The coach sees the email before it goes | Needs a mail program set up on the machine. Webmail-only coaches (Gmail in a browser) get no hand-off on Windows or macOS |
| **B. Send from the app (SMTP)** (later, if A is not enough) | Settings → Studio → Email: server, port, user name, and an app password kept in the system keychain (Keychain or Windows Credential Manager), never in the INI. **Send** goes straight from the sheet | One click; works for webmail users via an app password | Credentials to store and protect; Gmail and Microsoft 365 increasingly need OAuth rather than passwords; failures (blocked port, expired password) need clear, in-app explanations |
| **C. Always available** | **Copy email address** and **Show in Finder/Explorer**, so the coach can attach the file by hand | Works everywhere | Manual |

**Decided (Mark, 20 October): A, with C always shown beside it.** B stays designed but unbuilt.

**Changed (Mark, 21 October): B is built, A becomes the fallback.** "The function to email the report
to the golfer should just do it." Set up once per machine in Settings → Studio → Sending reports:
provider (Gmail, iCloud, Microsoft 365, Other; presets fill the server, port and security), the login,
and its password (an app password for Gmail and iCloud) kept in the system's secure store (Keychain,
Windows Credential Manager), never in the INI. The row's Email then opens a one-line confirm in
place, "Send to nick@… · 7.4 MB · Send", and sends straight from the app. Until sending is set up,
Email hands off to the mail app exactly as before.

- **No login, no send.** An ISP's open relay is allowed only as "Other" with the login left empty;
  delivering straight to the golfer's MX is not offered (port 25 is blocked on most broadband,
  broadband IPs are on block lists, and the coach's domain would fail SPF/DMARC).
- **Never in the clear.** A login is never sent without TLS (STARTTLS on 587, or TLS on 465);
  certificates are always verified.
- **From is the login**, because a server sending as anyone else fails DMARC; Reply-To is the coach's
  address when it differs. A copy goes to the coach as Bcc (on by default), since SMTP leaves nothing
  in Sent except on Gmail.
- **The send log says "Sent 21 Oct 18:40"**, meaning the coach's server accepted it, not that it was
  delivered. A bounce goes to the coach's inbox.
- **Microsoft 365** is on the preset list but password sign-in for SMTP is being switched off
  (default-off for existing tenants from the end of 2026, per Microsoft's MC786329); OAuth is not
  built until a studio needs it.
- **Size:** a 7–8 MB PDF is ~10–11 MB once base64-encoded. Gmail (25 MB) and Microsoft 365 take
  it; some hosts cap at 10 MB. The server's advertised SIZE is checked before anything is uploaded.

The message defaults, which the coach edits in Settings → Studio:
- **Subject:** *Your session report — {date}*
- **Message:**

  > Hi {first},
  >
  > Here is your report from today's session. Page 1 has the short version; your first work-on
  > is on page 4.
  >
  > {coach}
  > {studio}

The coach's note is in the PDF, not repeated in the email.

**The send log.** `report_meta.json` keeps one entry per send: the time, the method, and the
SHA-256 of the PDF that went. It stores **no address**. With option A the app cannot know the
coach pressed Send, so the entry and the session row say what really happened: "Opened in Mail,
20 Oct 18:40", not "Sent". With option B the entry records the server's acceptance. If the
report is remade after a send, the sheet says the new PDF differs from the one sent.

**Size.** Attachments over about 10 MB are refused by some mail servers, which is one more
reason for the 3 MB budget in §9.4. The row shows the file size beside the Report link.

### 8.4 Dates: when the session was, and when the report was prepared

Mark, 20 October: every report shows **both** dates, and never confuses them.

- **The session date** comes from the session's swings: the first swing's wall-clock time, in
  the machine's local time zone. The time a report is opened or made does not change it.
- **The prepared date** is when this PDF was made. Remaking the report gives a new prepared
  date, and the session date stays as it was.

| Where | What it shows (mock) |
|---|---|
| Cover, masthead (right) | "Session report" / **"Prepared 20 October 2026"** |
| Cover, kicker | **"Session of Tuesday 20 October 2026"** · club · swings · session of the day |
| Every page, running head | "Nick Henn · **session of 20 Oct 2026** · 7 iron" |
| Every page, folio | "**Prepared 20 Oct 2026** · Work-on 1 · 4 / 10" |
| About this report, first line | "This report covers your session on Tuesday 20 October 2026 and was prepared on 20 October 2026. If a session is re-analysed, the report is prepared again and carries the new date." |
| PDF properties | Title "Session report — Nick Henn — session of 20 Oct 2026"; creation date = prepared (the writer's own); the XMP subject carries the session date |
| File name | `2026-10-20_Nick-Henn_session-report.pdf`: the **session** date, so a coach's folder sorts by session |
| Email | Subject "Your session report — 20 October 2026" (the session date); the message's "today's session" becomes "your session on 20 October" whenever it is sent on a later day |
| Session row | "Report prepared 22 Oct" (the prepared date), beside the session's own date |

`report_meta.json` keeps the prepared time of every remake, so the send log can say which
prepared copy went to the golfer ("Opened in Mail 22 Oct · copy prepared 22 Oct").

## 9. Layout for any session

Every report has different content: one work-on or five, ten swings or sixty, a short drill or a
long one, two causes or twenty, a launch monitor or none. The mock's pages are **fixed A4 boxes
tuned to one session**, and that is exactly how its page 3 ran into the footer the first time
the cause lists changed. The build must not work that way. The layout is a contract that holds
for any content, and it is tested on content chosen to break it.

### 9.1 The rules

1. **Nothing has a fixed height except the page.** The report is a galley laid out at the text
   width, then cut into pages by `PpPager`, which it already does for the diagnostics print.
   Cuts fall only between atoms (a paragraph, a table row, a figure, a drill card, one cause
   group). A heading travels with what follows it, and there are no stranded pages (the pager's
   existing `keepNext`, `minFill` and `minWidow` rules).
2. **Fixed pages start; they do not end.** The cover, "Swing by swing", "Why this order", each
   work-on, practice and numbers, and the back page each start a fresh page (`breakBefore`).
   Any of them may run onto a second page. A continued page repeats the section's name ("Work-on
   3, continued"), and a table repeats its column heads.
3. **The cover is the one exception, so it is built to fit.** Page 1 must stand alone, so it is
   fitted rather than flowed:
   - every list on it has a hard cap (at most 3 strengths, 5 habits, 5 work-ons, 8 ball figures);
   - every line is limited to two lines, then ellipsised; the full text is always inside;
   - the coach's note is limited to about 80 words, with the count shown in the sheet as the
     coach types;
   - the space between the columns and the ball block takes up whatever is left.
   If a cover would still overflow, which a test proves cannot happen within the caps, the
   ball block moves to page 2.
4. **Every list states its cap and how it ends.** No list on any page grows without limit:

   | Block | Cap | Then |
   |---|---|---|
   | Work-on pages | 5 (the work-on rule's own cap) | — |
   | Strengths | 3 | — |
   | Swing grid columns | 20 swings per band | further bands of 20 below, each with its swing numbers; carry bars per band |
   | Swing grid rows | habits ≤ 5, seen ≤ 4, ball ≤ 4 | "and N more seen on fewer swings" |
   | Earlier today / across sessions | 6 sessions | the newest six; "N earlier sessions not shown" |
   | Cause groups | spotted all; could-be 6; ruled-out 4 | "N more, less likely" / "N more" |
   | Worth checking | 5 per kind | the rest on the work-on pages |
   | Drills per work-on | 1 in full + 1 as "Also" | the pack may hold more; they are not printed |
   | Numbers table | all swings | flows, repeating its heads |
   | Posture page | rows 6, positions 8; strip 120 marks, 30 a line; departures 6; trust lines 8 | "N more shots not shown" / "N more" |
   | Wrist page | rows 5, positions 8; style columns and finding marks 120; findings 8; movesWith rows 7 (4 + 3 launch monitor); trust lines 16 | "N more swings not shown" / "N more" |
   | Stills | 2 per work-on (first three only), 4 for the reference swing | — |

5. **Widths come from the text, not from Nick.** Columns use the typeset size classes in em
   (`PpTypeset.columns`), and labels wrap rather than truncate, except where rule 3 says
   otherwise. The number columns are sized from the widest value in the session.
6. **Empty means absent.** A block with nothing to say is not drawn at all, along with its
   heading. Examples: no launch monitor means no ball block and no numbers table; no cause
   spotted means no "What we spotted" row; nothing came with the habit means no "What came with
   it". A block is never drawn empty and never shows "none".
7. **Words come from templates with every case written.** Each sentence template has its variants
   in the C++, not just the case Nick happened to need. That covers singular and plural ("one
   habit", "five habits"), "every" against "almost every", and none, one or several. The tests
   in §11 enumerate the variants.

### 9.2 Proving it: the stress set

`tst_session_report` prints each of the following, and fails if anything crosses the folio, any
text is clipped, or any page is less than a quarter full, except a section's last page:

| Case | Built from |
|---|---|
| Nick, 20 Oct, session 3 (the mock) | library |
| Mark, 7 Oct: 50 swings, 5 work-ons, long cause lists | library |
| Jess, 20 Oct, session 3: 7 swings, a setup habit, a P8 habit, a session-level habit, almost no authored content | library (**run 20 Oct**, §9.3) |
| A three-swing session (the one-page note) | library |
| A clean session: no habits | fixture |
| One work-on only | fixture |
| Five work-ons, each with the longest goal, cue and drill text in the pack | fixture |
| Every cause group at its cap, with the longest condition names | fixture |
| 60 swings, three clubs | fixture |
| A junior without photo approval (no stills anywhere) | fixture |
| The longest studio name, golfer name and coach's note allowed | fixture |

The overflow check already used on the mock (`tools/session-report-mock/overflow_check.py`)
becomes the probe's first test. The visual review for Mark uses annotated sheets of the stress
cases, not just the happy one ([feedback_annotated_review_sheets]).

### 9.3 The first stress test: Jess Ratcliffe, 20 October, session 3 (done 20 October)

The mock's assembler was generalised so that nothing in it names Nick, and run on Jess's session:
7 swings with an 8 iron, two cameras and a GC Quad. Her work-ons are chicken wing, head dips
going back, stance too wide, reverse spine at the top, and sequence not repeatable. Reproduce
with:

`tools/session-report-mock/render-session.sh jess /mnt/swingdata/Jess-Ratcliffe/2026-10-20_Jess-Ratcliffe_Wrist_03 /mnt/swingdata/scratch/session-report/dumps/jess03.json`

Output: `/mnt/swingdata/scratch/session-report/out/jess/session-report-jess.pdf`. Nick's report was re-rendered
from the same code as the regression check (`nick/`). Both now pass the folio check on every
page. What the test found:

**Layout and template bugs (fixed in the mock; each is a §9.1 rule the build must keep)**
- Hard-coded words on page 2: "ten swings", "The same five". Both are now generated, with a
  fallback title when not every habit fired on every swing.
- Counts doubled their noun ("6 of 6 swings swings").
- A session-level habit broke the work-on kicker (", over the session · over the session") and
  drew a meaningless "0 of 0" row in the swing grid. It is now a line under the grid
  ("…is read over the whole session, not swing by swing: see work-on 5").
- An empty "Linked, not proven" column was drawn with its heading (rule 6: empty means absent).
- "Coaches call the habit Stance too wide." repeated the title when the habit has no aliases.
- "125 %" dropped its words; it now reads "125% shoulder width".
- The timing habit's sentence cited the study ("8 ms SD, 19 PGA tour professionals, Cheetham
  2008"); it now reads "Your lead arm's peak timing varied 12.6× ± 4.1 as widely as touring
  professionals' do". The citation belongs on the back page.
- "Came down to 2 of 2 here": a habit judged on two swings was described as having eased. It now
  reads "this session could only judge it on two swings, too few to say".
- Strengths required 8 measurable swings, a number tuned to Nick's 10, so Jess would have had
  none. It is now at least 80% of the swings and never fewer than 3.
- Page 2 overflowed on both sessions once the watch-list rows were chosen by data rather than
  by hand. That is the §9.1 flow case; the mock now caps them at 3, and the build flows instead.

**Findings that need a decision or content, not layout**
1. **The order puts a follow-through habit first.** Chicken wing (P8) leads with a miss nearly
   three times the next one's, so the headline reads "We start with your follow-through". This is
   the clearest case yet for rule v4, swing order when links cannot be tested (§4.3). Under v4
   Jess would start with her stance (P1).
2. **That lead reading looks implausible.** The lead-arm-to-torso angle at P8 is 106° typical,
   with swings from 49° to 163°, against a target of 40° or less. The swing-to-swing spread and the
   dark, turned-away face-on frames at P8 suggest a measurement or norm problem. It should be
   checked before any report headlines it. *For Mark: is chicken wing at P8 trustworthy from
   face-on?*
3. **Content is the biggest gap.** Four of Jess's five work-ons have no drill, feel or
   self-check (the mock prints a red "content gate" note in their place). "Stance too wide" also
   has no `golferWell` (so its title is the coach's term) and no `golferWhy`. This confirms §5's
   gate as the first build phase.
4. **The timing habit claims what was not measured** (issue #58). Its card counts the half that
   was not assessed, and its golfer phrase talks about order when only the timing spread fired.
5. **The words for "what we could not check" are internal.** The gate clauses ("club peak drawn,
   not judged", "swing plane not one plane") need golfer words. That is the gate catalogue's
   `claimWords`, written for golfers, or a report-side table.
6. **Stills need exposure correction.** Jess's face-on frames in the bay are very dark. The stills
   step (§6) lifts levels on the golfer's crop before the overlay.

### 9.4 Rendering and file size


The mock is 9 pages at 300 dpi, lossless: **6.8 MB**. The existing diagnostics print of the
same session is 8 pages and 9.0 MB. That is too big for something a coach emails. The pages are
rasters by design: the print registers are QML, and `PdfDocumentWriter` embeds page images.

**Recommendation:** keep the raster path, because it is one renderer and it shows what the app
shows. Add a per-page encoding choice to `PdfDocumentWriter`:
- a page with no photograph is reduced to an 8-bit palette (text, rules and dots quantise
  without visible loss) and stored with Flate;
- a page with a photograph is stored as JPEG at quality 90.

Target: **3 MB or less** for a 9-page report, measured on this mock before the build is signed
off. A vector PDF would need a second renderer (QPainter or QTextDocument) for every block, so
it is rejected.

## 10. The honesty rules

1. Counts are swings, out of the swings that could be judged, and say so: "7 of the 7 swings we
   could see".
2. "Typical" is the median, and is stated only when every firing was graded against the same
   target range (the existing one-corridor rule).
3. A link is described at the grade the session earned:
   - authored and untestable: "the coaching model links them; today's swings cannot separate
     them";
   - present together: "together on the same swings… not proof";
   - coherent: "followed it in the right order on N swings";
   - conditionally dependent: "it went away on the swings where X did";
   - moved together (focus only): "when X improved, Y followed".
4. Inferred causes appear only as **questions**: "could be behind it", and Worth checking. Every
   authored cause the session could not check is named, with how it could be checked. A cause is
   "ruled out" only on the swings that judged it.
5. Magnitude words come from the corridor grades. "A small miss: just outside the range, never
   far" is used when no swing passed the fault line (Nick's reverse spine: 9 of 10, median −1°
   against 0° to 24°).
6. Strengths follow §3.2, including the whole-axis rule for outcomes.
7. "What we could not check" is said once, at the back, in golfer words.
8. No percentage for any posterior, no composite score, no σ-rounded figure.
9. Everything printed is reproducible from the session folder: the same report map comes from
   the same ledger and pack.

## 11. Architecture and files

| | New or changed |
|---|---|
| `src/Coaching/session_report.h` | **new**, pure and header-only: strengths selection, reference swing, still choice, range words, "came with it" sets, timeline positions, the ordering reasons. Unit-tested like `work_ons.h`. |
| `src/Coaching/work_ons.h` | `WorkOnEntry::reasons` and `SessionWorkOns::linksTestable`. Rule v4 too, if option A (§4.3). |
| `src/Gui/report/session_report_model.{h,cpp}` | **new**: the façade. It owns the private diagnostics model, assembles the `report` map (every string; QML positions and paints), and drives stills and printing. Registered like `WorkOnsController`. |
| `src/Gui/report/report_stills.{h,cpp}` | **new**: frame at a phase, pose overlay, crop to the golfer. Worker thread. |
| `src/Gui/report/PpSessionReport.qml` + blocks (`PpReportCover`, `PpSwingGrid`, `PpOrderPage`, `PpWorkOnPage`, `PpDrillCard`, `PpStill`, `PpBallTiles`, `PpNumbersTable`, `PpReportAbout`) | **new**: the register, on `PpPager` and `PpTypeset` print roles, with page starts as `breakBefore` atoms. The cover suppresses the running head. The appendix is the existing `PpDiagPrint`. |
| `src/Gui/print/pdf_document_writer.{h,cpp}` | Per-page encoding: palette+Flate or JPEG (§9.4). |
| `src/Gui/report/session_report_list_model.{h,cpp}` | **new**: one golfer's sessions with their report state (§8.0): date, session of the day, clubs, swing count, devices (`session_summary.h` already carries them), work-on 1 from the session's record, report fingerprint against `report_meta.json`, last send. Built off the GUI thread; refreshed by the work-ons catch-up. |
| `src/Gui/report/PpSessionReportRow.qml`, `PpSessionReportList.qml` | **new**: the row with its three links, and the list (Home: newest ten + "Show all"; golfer tab: all). |
| `src/Gui/home/ScreenHome.qml` | The **Sessions** section at the foot (§8.0). |
| `src/Gui/athlete/ScreenAthleteForm.qml` | Two tabs, **Details** (the form as it is) and **Sessions**; the Reports group (§7.3) in Details. |
| `src/Gui/report/PpReportSheet.qml` | **new**: the sheet (note, options, photo line, missing-detail links, Make, progress). |
| `src/Gui/settings/PpStudioSettings.qml` + `src/Core/studio_profile.{h,cpp}` | **new**: the studio and its coach (§7.1, §7.2), in the app INI under `Studio/`; the logo copied into `<AppData>/studio/`. A `StudioProfile` QML singleton the report model reads. |
| `src/Gui/athlete/athlete_controller.{h,cpp}` | The Reports fields (§7.3) through `updateAthlete`, exposed as `currentReportEmail`, `currentReportGreeting`, `currentIsJunior`, `currentPhotosApproved(On)`; ticking or unticking stamps or clears the date; address shape check. |
| `src/Gui/report/report_mailer.{h,cpp}` (+ `_mac.mm`, `_win.cpp`, `_linux.cpp`) | **new**: hand-off to the mail app (§8.3, option A): one interface, a platform file each. Option C's copy and reveal. |
| `src/Gui/report/report_meta.h` | **new**, pure: `report_meta.json` read/write, including the send log (no addresses). |
| `src/Resources/diagnostics/core.json`, `drills.json` | Content (§5): new fields on every listable condition, `golferName`/`golferSign` on their measures, drills for every listable condition. |
| Tests | `session_report_test` (C++ pure: every rule in §3.2, §4.2, §6, §10 on fixtures, including the "Nothing off the heel" case and the range-restricted case); `core_pack_test` gate (§5); `pdf_document_writer_test` (encodings, size); `qml_ui` `tst_session_report` (every page renders, every variant in §3.1, no overflow past the folio); a probe that prints a library session to the share and fails above the size budget. |

## 12. What the mock is and is not

It is the real data of one session, assembled the way the C++ will assemble it, and drawn by the
app's own fonts, roles and PDF writer. It is not the build:
- the pages are fixed boxes tuned to this one session rather than flowed on `PpPager` (§9); the
  build follows §9, and the mock is not evidence that the layout holds for other sessions;
- the stills come from ffmpeg;
- the Studio profile is a placeholder ("Northfield Golf Studio", "Alex Morgan" are invented);
- the coach's note is sample text written to show the slot;
- the PROPOSED content in `assemble.py` (goals, cues, checks, two drills, measure names) is a
  first draft for Mark's review, not finished pack content.

## 13. Build plan (when the mock is approved)

Each phase is judged in the app before it is committed ([feedback_results_before_commit]), with
at most 2 to 3 builds per phase. One phase per agent run.

**The tooling to start from** is in `tools/session-report-mock/` (README there). `assemble.py` is the
prototype of `SessionReportModel`; `render-session.sh` renders any session and runs the overflow
check; `dump-session.sh` dumps the diagnostics model for a session. The dumps for Nick's and Jess's
sessions are in `/mnt/swingdata/scratch/session-report/dumps/`.

0. **Before building: the open decisions in §14**, chiefly the ordering rule (§4.3, and Jess's
   case in §9.3), the technical appendix, who writes the content, and the club path question.
   Also check whether chicken wing measured after impact from face-on can be trusted (§9.3, item
   2), because a suspect reading must not headline a client report.
1. **Content.** Add the pack fields, grow thin cause lists (§3.3), then author goal, cue, check, picture and golfer text for
   every listable condition, and drills to cover them. Add the `core_pack_test` gate. Mark reviews
   the wording as a table before it lands.
2. **Pure reductions.** `session_report.h` and the work-on `reasons` (plus rule v4 if chosen),
   with tests.
3. **Model.** `SessionReportModel` builds the map for any session folder. A probe dumps it for
   the library; its output is diffed against the mock's `report.json` for Nick's session.
4. **Stills.** Frame source, overlays, crop to the golfer (pose `cropRect`; no bystanders), and
   exposure lifted on the golfer's crop (Jess's face-on frames are very dark, §9.3). Judged on
   Nick, Jess and Mark sessions, with annotated sheets ([feedback_annotated_review_sheets]).
5. **Register.** `PpSessionReport` on `PpPager` under the §9 layout contract, every variant in §3.1,
   plus `tst_session_report` printing the whole §9.2 stress set. Mark judges annotated sheets of
   the stress cases, not only Nick's.
6. **Writer encoding** and the 3 MB budget.
7. **Scaffolding:** the Studio page (studio and coach), the golfer details page's tabs and its
   Reports group (email, greeting, junior photo approval), the session list on Home and on the
   golfer's Sessions tab, and the report sheet, with missing details shown as links. Probe the
   whole flow from both lists, starting from a fresh settings file. Include a junior with and
   without approval, and a golfer with no date of birth.
8. **Sending:** the hand-off on macOS first, then on Windows (Outlook and the Windows Mail app)
   on the cabin PC; the send log; the "remade since sent" warning.
9. **Windows build** on the cabin PC before anything is pushed.

Alongside, not blocking: **issue #58** (the "Sequence not repeatable" card counts the half of its
test that was not assessed). The report's session-level page reads the model's consistency fields
directly, so it does not depend on the fix, but the panel does.

## 14. Decisions for Mark

1. ~~The order~~ **Decided 20 Oct: rule v4** (§4.3), swing order when links cannot be tested. Built with sharp
   findings kept ahead of swing position (without that, v4 dropped Mark's sharp sequence faults off the list).
2. **The mock's look:** the page plan, the length (10 pages for a 10-swing session), the cover's
   three columns, and the stills.
3. ~~The technical appendix~~ **Decided 20 Oct: always** appended, from a fresh page, printed at 200 dpi.
4. **Brand:** built as proposed (one accent colour, logo or monogram, Folio light only); still Mark's call.
5. ~~Who writes the content~~ **Decided 20 Oct:** drafted for every listable condition in the build, reviewed by
   Mark as a wording table before it is committed.
6. **Club path:** `in_to_out_path` and `out_to_in_path` read `m_clubPathAtImpact` (the camera's
   own estimate, planned, no producer). So they are never judged, even when the GC Quad measured
   path on every swing (`m_lmClubPath` is live). `ball_speed_deficit` is the same, against
   `m_ballSpeed`. Commit `ddb3162a` keeps LM readings *beside* our own, so this may be
   deliberate. But `closed_face_to_path` does read the LM's `lm.faceToPath`. Should the path
   conditions fall back to the LM reading when ours is absent? (On Nick's page 5 this is why the
   hips page has no "what came with it", though path ran in-to-out.)
   **Decided 20 Oct (Mark): "LM is always the primary source for the metrics it delivers — our estimates will
   never be better."** Every measure with an `m_lm*` twin reads the LM first (`preferKeys`); ours is the fallback.
7. ~~Sending~~ **Decided 20 Oct:** hand off to the coach's mail app (option A, §8.3).
   **Changed 21 Oct:** send directly over SMTP (option B), with the hand-off as the fallback (§8.3).
8. ~~Juniors' photos~~ **Decided 20 Oct:** off for under-18s unless the golfer details page records
   parent or guardian approval (§7.3).
9. ~~Coaches~~ **Decided 20 Oct:** one coach per studio (§7.2).
10. ~~Where reports live~~ **Decided 20 Oct:** a session list at the foot of Home and as a
    Sessions tab on the golfer details page, each row with Open session · Report · Email (§8.0).
    The session drawer has no report actions.
11. **Decided 20 Oct:** the document carries both the session date and the date it was prepared
    (§8.4).
