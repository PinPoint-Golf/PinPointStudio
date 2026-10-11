# Session diagnostics: model defects found during the typeset redesign

Found on 11 October 2026 while redesigning and rebuilding the Session diagnostics panel (design
system §15). They are defects in what the MODEL publishes, not in the panel; the panel has been
built not to depend on them, but it cannot fix them. To be taken up in a separate session.

All four were seen on the same data: the session of 4 July 2026 (7 iron, 15 shots), shot 8.
To see the model's raw output for a shot, dump `sdBody.readout`, `sdBody.source` and `sdBody.detail`
as JSON from a `--probe-qml` probe (the typeset harness in `tools/typeset-harness/` does this).
Always work on a scratch COPY of the session: loading one re-grades it and writes into it.

## 1. Early extension: the details page opens on a different measure from its own card

**What you see.** Early extension's card quotes pelvis thrust toward the ball — 9.4 cm on shot 8,
pass −5 to 5, fault outside −7.5 to 7.5 cm. Open its details page and the figures chart spine
forward bend lost (in degrees), with a fault line at −7.5°.

**What is really happening — and a correction.** During the redesign several of us (and the first
write-up of this list) read the −7.5° as the centimetre fault number reused as degrees. It is not.
`m_spineBendLossDown` has its own authored norm in `src/Resources/diagnostics/norms.json`
(μ 0, σ 2.5°: it fires from 5° of bend lost and Action begins at 7.5° — "Mark's line, 2026-10-08").
The two −7.5s are a coincidence.

Early extension is deliberately read by TWO measures — the pelvis toward the ball, or the spine
standing up (`session_diagnostics_model.cpp`, "ONE TAB PER MEASURE", around line 3850). The page
opens on the measure that fired on most shots; the card shows the measure that decided most shots.
Those can differ, and on this session they do. The page now has measure tabs (one per measure,
"fired N of M"), so the reader can switch — but the opening state still contradicts the header.

**Why it matters.** The header and the figures under it describe different movements in different
units, with nothing at the top saying "this page is reading the second of two measures". The new
design's "distance to travel" line (°) sits under a header quoting centimetres.

**Fix direction.** Make the page agree with itself: either open on the card's measure, or make the
header card (value, corridor, unit) follow the selected measure, and name the measure in the page's
kicker. Then check every other condition read by more than one measure for the same split.
This is a presentation rule in the model, not an analysis change — no re-analysis needed.

## 2. The "likely driver" is not on any drawn cause path

**What you see.** Over the top's causes section opens "Likely driver: Across the line at the top —
would explain 1 of your pattern." None of the eight cause paths drawn below contains Across the
line at the top (they run through Reverse pivot, Hanging back, Too much tilt away at the top, Pelvis
drifts to the lead side, back to the physical limitations). The model marks the path list as capped.

**What is happening.** The headline comes from the resolver's ranked roots (`m_explanation.roots`,
`session_diagnostics_model.cpp` around line 3756); the drawn paths come from the authored graph's
upstream paths, ranked and CAPPED separately (`railsOf(upPaths)`). Nothing guarantees the driver's
path survives the cap, and here it does not. The sentence also reads oddly: "would explain 1 of
your pattern" means one of your patterns.

**Why it matters.** The first sentence about WHY the fault happens is not supported by anything on
the page beneath it — the page appears to contradict itself.

**Fix direction.** When a ranked root accounts for the condition, its path must be among the drawn
paths, first and marked primary; the cap gives way for it. Word the coverage properly ("would
explain one of your patterns" / "N of your patterns"). No re-analysis needed (it is how the page is
assembled), but add a test: the headline's root is always on a drawn path.

## 3. Clean conditions whose "distance" puts them past the fault line

**What you see.** Every condition carries `strengthExcess` — how far this shot sat from the corridor
— which the new dot plot uses to place all conditions on one scale. On shot 8, nine conditions are
correctly CLEAN yet carry an excess past the fault line. Two clear cases:
- Ball too far forward: clean at 57.3 % stance width, excess 2.43 — identical to Ball too far back,
  which fired on the same reading. The ball was back, so "forward" is clean; the distance is the same.
- Backing away from the ball: clean at 9.4 cm, excess 3.75 — the early-extension reading in the other
  direction.

**What is happening.** These conditions can only fire one way, but the ledger records their corridor
as two-sided (`corridorShape: "twoSided"` on the row — see any session's diagnostics.json), and
`rowStrength()` (`src/Analysis/diagnostic_ledger.h` around line 568) takes |z| for a two-sided
corridor. A reading far out on the side that can NEVER fire therefore scores as far out. The
corridor sentence repeats the flaw ("fault outside −7.5 to 7.5 cm" for backing away).

**Why it matters.** It undermines an honest comparison. The panel works around it today (those rows
sit at the corridor centre with a "‹ out on the other side, so clean here" mark), but anything else
reading the number — rankings, the pulse, future screens — inherits it.

**Fix direction.** A condition's graded side should come from the condition (the direction it fires
in), not from the measure's corridor: record the shape the condition uses (floor or ceiling) on the
row, or compute the excess on the graded side only (0 on the open side), and write a one-sided
corridor sentence. This CHANGES STORED SESSION DATA: bump the relevant stage version so the library is
re-analysed (house rule: version-gated re-analysis), test on the one swing that shows it first, then
re-run the library and check the corpus counts. Highest reach of the four — do it with the most care.

## 4. A closing line that describes a view that no longer exists

**What you see.** The session's closing line: "Counts are session totals; this shot is the wide tick.
Recurrence is a count, not a rate at this n." (`session_diagnostics_model.cpp` around lines 2397–2429.)

**What is happening.** "The wide tick" was how the old card strip marked the picked shot; the new
design marks it with a shaded column or a ring. "At this n" is statistician's shorthand. The new
panel does not print the line until it is fixed.

**Fix direction.** Describe the picked shot without depending on how it is drawn, and say "at this
number of shots" in plain words. A wording change; update any test that pins the string.

## Suggested order

4 (wording) → 1 (presentation rule) → 2 (assembly rule) → 3 (analysis change, re-analysis, corpus
check). Each tested on the swing that shows it before anything wider.
