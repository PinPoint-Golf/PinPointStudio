# Trust stage 6: every per-swing gate recorded (plan, 16 October 2026)

Stage 6 of `trust_and_gates_design.md`, done as one pass instead of one session per area (Mark, 16 Oct). The
audit of the census (`docs/research/data/gates/census_20261016.md`) against today's code drives the plan.
Session 1 is **record everything**; session 2 is **show it everywhere** (a gated "-" says the gate's words).

## Decisions (Mark, 16 Oct)

- A gate that acts on stretches of samples many times per swing leaves **one summary record per reading**
  (effect `masked`, evidence: fraction of the reading's frames and the stretch). Filters that only widen σ
  (the pose smoother's coasting, an assumed club length) leave no record.
- Session and on-read rules (diagnostics ledger minimums and tests, Charts sample-size rules, launch monitor
  session minimums) get **catalogue entries now** (`rule: true`: explanation and threshold, `PP_GATE` at the
  code site, no per-swing record).
- The launch monitor import **keeps a list of the fields it dropped** from now on; analysis turns it into
  records. Older swings stay unexplained (the values were never kept).
- The code is **pushed once green locally**, so the studio can re-analyse the library; Mark judges the per-area
  firing table afterwards.

## Decision rules (applied without asking)

1. **A record is written where the decision is made**, in a stage that runs on every analysis. Where the
   decision lives in a reused product, the record is derived from what that product stores, on both paths.
   No stage version is bumped: every producer below either re-runs or stores its outcome (refused club
   tracks are never reused, so their refusals re-run).
2. **A cascade is one record.** A whole-track or whole-reference refusal is recorded once, on the reading it
   refused first; what it took down is listed in `blocks`, never re-recorded.
3. **Subjects.** A metric key (`pelvisSway`) when a gate acts on one reading; `track.club`, `track.clubDtl`,
   `track.club3d`, `track.impact`, `track.skeleton3d` for a whole track; `shot` for the whole swing;
   `lm.<field>` for a launch monitor field; the sequence keeps `ks.*` / `route.*`.
4. **Not recorded, and why** (listed in the design doc's §10): capture-time and device checks with no swing
   to attach to (phone-shot corroboration, the sensor impact detector, filter seeding, calibration axis test,
   launch monitor pairing and standalone refusals); provenance labels (sample tier, timing class); the
   phase-domain mask (a metric's defined span, not a refusal); σ-only filters; the phase retiming and fusion
   abstentions (silent on reuse, no reader-facing change); the impact anchor (reused, not stored); bridged
   detector gaps (a hold, not a refusal); the clubhead speed past the club's impact position (its defined span).
5. **Thresholds stay where they are and keep their values.** A hard-coded threshold is catalogued with its
   literal value and named for a later move into `pp_tuned_constants.h`; no value changes.
6. `kGatesVersion` → 2, stamped on every analysis (not only when the sequence runs).

## Gates recorded (by area)

| Area | Gates |
|---|---|
| Body | no rotation stream; triangulated address gap; segmented Top missing (X-factor stretch); no usable lower / upper body at address; stance span floor; shoulder span floor; hip line, shoulder line, elbow span foreshortened (summary) |
| Down-the-line posture | ball side undecided; shoulder ruler implausible; loop too small; foot / shoulder too small; shaft lie refused |
| Club and shaft | coverage below floor; the four whole-track refusals; ball suspect; head confidence (summary); attack-angle synth fallback; low point (too few samples, edge vertex, no ball ruler); club peak lead too short; follow-through demotion (summary); plane window needle; fused plane scatter; DTL top out of view; DTL quarantine and late escape (summary) |
| Impact camera | path too short; head offset assumed |
| Shot | capture holes; sensor re-fusion parity |
| Skeleton, ball | skeleton refused (by reason); ball position refused |
| Phases | tempo refused (event missing, confidence, order) |
| Wrist | HackMotion binding refused; cell too few samples / gimbal (summary); finding low confidence; strength without a fault |
| Launch monitor | field no-data, out of range; face-to-path inputs missing |

## Verification

Gate catalogue test both ways (records and rules); a unit case per area on a fixture; the two test sessions
(18 Aug Wrist_02, 4 Jul Wrist_01) re-analysed locally and read with the trust readings; then the library on
the studio and a per-area firing table (gate × shots fired / shots) for Mark.

## Conventions for the producers (what every area follows)

- **Firing.** A producer fires with `GateLog::fire(PP_GATE("area.name"), subject, effect, evidence)`, or
  `fireCovering(…, covers, evidence)` for a cascade (`covers` = the metric keys it took down). The id is
  spelled `PP_GATE("…")` at the firing site, in the file the catalogue entry names as `producer`
  (`gate_catalogue_test` reads that file both ways). A producer result struct carries its records
  (`std::vector<GateRecord> gates`); the stage hands them on with `ctx.addGates(std::move(…))`, never by
  writing `ctx.detail->gates` directly (the parallel executor buffers per stage).
- **Evidence is numbers and short codes, never prose**, keyed with the unit in the name (`spanPx`, `minPx`,
  `frac`, `sightLostMs`). Times are **ms relative to impact, + after** (the Charts' clock). The threshold the
  gate compared against goes in the evidence too (`minPx`), so a record reads correctly if the value is
  later tuned.
- **A mask is one summary record per reading**: `maskEvidence(gatedUs, ofFrames, impactUs, fromUs, toUs)`
  (gate_record.h) → `{ frames, ofFrames, frac, fromMs, toMs }`; no record when it is empty. Effect `masked`.
- **Subjects**: a metric key for one reading; `track.club`, `track.clubDtl`, `track.club3d`, `track.impact`,
  `track.skeleton3d`; `shot`; `lm.<field>`.
- **Catalogue entries** go in the area's own header (`src/Metrics/gate_catalogue_<area>.h`, inside
  `namespace pinpoint::gates::detail`, one `append<Area>Gates(std::vector<GateEntry> &c)` function) written
  with `detail::add(c, id, area, title, shortText, explain, evidenceText, effects, producer, design)` and
  then `.tuning`, `.lift`, `.codes` set on the returned entry. Template codes: `{k}`, `{k:t}` (instant from
  impact), `{k:ms}`, `{k:deg}`, `{k:px}`, `{k:pct}` (a 0..1 fraction), `{k:cm}`, `{k:in}`, `{k:mph}`,
  `{k:code}` (looked up in `.codes`), `[ … ]` optional. Words for a golfer: short, plain, no engineer's
  terms (no "IMU", no role enums, no "σ" in titles). `title` is the short form on a figure ("Hips edge-on"),
  `shortText` lower-case for a figure slot ("hips edge-on"), `explain` one paragraph on why the gate exists.
  Thresholds go in `.tuning` with their compiled value (from `pp_tuned_constants.h`, or the literal where
  the code hard-codes one — no value changes).
- **Rules** (`detail::rule(…)`, `rule = true`) mark their code site with a comment `// gate rule:
  PP_GATE("id")` beside the decision, in the file named as producer.

## As built (16 Oct 2026, session 1)

50 per-swing gates and 36 rules catalogued; `gate_catalogue_test` ties every id to its producer both ways.
Records reach `analysis.gates` on every path: per stage through `ctx.addGates` (buffered per stage under the
parallel executor, spliced in stage order); the whole-swing records (capture holes, sensor parity) rebuilt from
the manifest on every write (`manifest_gates.h`); the launch monitor's from the stored drop list
(`lm_gates.h`); a HackMotion lane that would not bind, from the loader. Masks share one "Partly drawn" line in
the trust summary and stay out of the closed fold's words. Test sessions: 4 Jul Wrist_01 (15 shots) and
18 Aug Wrist_02 (6) re-analysed; library sweep on the studio next, its firing table for Mark.
