# PinPoint Studio: Variability marks implementation plan

**Reference:** `variability-design-spec.md` (the spec). Commit it to `docs/design/variability-mark/spec.md` before session 1.
**Shape:** five sessions, each run as a fresh Claude Code session on one branch (`feature/variability-marks`), merged after session 4. Session 5 runs on a new branch.

## Ground rules (repeat in every session prompt)

- **The spec is the source of truth.** If code, data or this plan contradicts it, stop and ask; don't improvise.
- **Constants.** Every number in spec §8.10 is a named constant marked `// TODO(calibrate)`. Never invent thresholds or mappings that aren't in the spec.
- **Architecture.** Use an abstract base class plus a factory for providers and graders (house rule). Use theme tokens only, with no hex values.
- **Commits.** One logical commit per Done item.
- **Stop at the session's Done list.** Don't start the next session's work.

## Session 1: grader and data model

**Goal:** compute every grade in spec §8, with no UI.

**Read:** spec §1, §3 and §8.

**Survey first, and report before coding:**
- the Qt version (Shapes `CurveRenderer` needs 6.6 or later);
- how norm corridors and their `sigmaLo`/`sigmaHi` are looked up per context;
- what currently drives any consistency indicator (0–100, bands, σ, RMSE…). If it's RMSE against a target, list the callers: they are measuring accuracy;
- whether an outlier or mishit flag exists.

**Tasks:**
1. Add the `MetricDescriptor` optional fields `scaleType`, `variabilityScale` and `measurementTolerance`.
2. `VariabilityGrader`, an interface with implementations for each basis:
   - scalar (§8.2), trace (§8.3), ordinal (§8.4) and measurement (§8.5);
   - the coupling between measurement and consistency (§8.6);
   - typicality z (§8.7), trend (§8.8) and hysteresis (§8.9).
3. **The result type:** grade, basis statistic(s), scale, n, `ungradedReason`, and a basis-string builder (§7.5).
4. **QML façade:** expose results only.
5. **Calibration run.** Run the grader over all locally logged sessions. Write a CSV (metric, context, n, ρ, ρₐ, ρₜ, s′, grade), plus a short report of the ρ distribution per metric and the share of metrics per grade. Propose band changes with the evidence, and **don't apply them**.

**Tests (Qt Test):**
- **Bands:** known SD gives the expected ρ and grade at each band edge.
- **Bias:** consistency is bias-invariant; measurement bias does worsen ρm.
- **Circular wrap:** samples around 179°/−179° grade as tight.
- **Thresholds:** n < `N_MIN` gives 0; no scale gives ungraded.
- **Traces:** a timing shift grades on timing; an amplitude change grades on amplitude.
- **Ordinal chance correction:** 50/50 binary gives s′ = 0, and a repeated wrong order is Grooved.
- **Noise subtraction:** never negative, and an identity when σnoise = 0.
- **Trend:** "Holding" within the noise, and broken runs are handled.
- **Hysteresis:** no flicker at band edges; frozen assessments use the plain bands.

**Done:**
- [ ] Survey reported.
- [ ] Fields, grader, result type and façade in place.
- [ ] All tests pass.
- [ ] Calibration CSV and report in `docs/design/variability-mark/calibration/`.

**Prompt:**
> Read `docs/design/variability-mark/spec.md` and `docs/design/variability-mark/plan.md`. You are doing **Session 1: grader and data model**. Follow the ground rules. Report the survey before writing code, and stop at Session 1's Done list.

## Session 2: the mark, the badge and the rendering spike

**Goal:** the three graded variants (Dispersion, Overlay, Needle) in both forms, the badge, and the vocabularies, proven on real hardware.

**Read:** spec §2–§5, §7, §10 and §11.

**Tasks:**
1. `IVariabilityGeometry` and `CanonicalVariabilityGeometry`, built from the §4 tables, cached and exposed to QML as a singleton.
2. `PpVariabilityMark` (§4) and `PpVariabilityBadge` (§5).
3. The vocabulary singletons for consistency, measurement, trend and typicality (§7), through `qsTr()` with translator comments.
4. Tokens (§10) in `Theme.qml` and the token source.
5. **Spike, before anything else is built on top.** A gallery screen with a `TableView` of 500 rows (`reuseItems`), holding Cell badges for Overlay and Dispersion, plus an icon grid of every variant at 12, 16, 20 and 24 px, at 100% and 150% scale.
   - Measure scroll frame time on Windows D3D11, and delegate creation cost (QML profiler).
   - If thin strokes are poor at 12–16 px, compare `GeometryRenderer` with `antialiasing`. Never use MSAA.
6. **Dark themes and greyscale.** Render every variant × grade in all eight themes and in greyscale.
   - If any grades blur together, introduce the per-theme wash-alpha tokens.
   - Check that greyscale keeps all five grades distinct.

**Tests:**
- Geometry is deterministic.
- Grade 5 converges.
- Spread never increases with grade, for every variant and form.
- Tone mapping is correct; grade 0 and clamping behave.
- QML smoke test: every variant × form × grade under every theme, with no warnings.

**Done:**
- [ ] Geometry provider, mark, badge, vocabularies and tokens in place.
- [ ] Spike numbers and screenshots in `docs/design/variability-mark/spike/`.
- [ ] Dark-theme and greyscale results recorded.
- [ ] Tests pass.

**Prompt:**
> Read the spec and the plan. You are doing **Session 2: the mark, the badge and the rendering spike**. Follow the ground rules. Do the spike (task 5) before anything else, and report the results if they show a rendering problem. Stop at Session 2's Done list.

## Session 3: companion visuals

**Goal:** the spread funnel and the two typicality variants, on real data.

**Read:** spec §6, §7.3, §7.4, §8.3, §8.7, §8.8 and §11.

**Tasks:**
1. `DataVariabilityGeometry`, implementing the same interface and fed per instance (sessions, swings, traces).
2. `PpVariabilityFunnel` (§6.1): breaks, the fewer-than-3-sessions state, labels and the accessible name.
3. The `Typicality` variant (Cell and Icon) and the `TypicalityTrace` variant (Cell), including the clamp chevron and `showCorridor` (§6.2).
4. Add both to the gallery, with real logged sessions where available and synthetic data otherwise.

**Tests:**
- Funnel geometry is monotonic in ρ, breaks render, and fewer than 3 sessions gives no band.
- The typicality ring's x position follows z, with clamping.
- The envelope is built from the window's mean ± SD(t).
- Ghost-dot jitter is deterministic for a given swing id.

**Done:**
- [ ] Data provider, funnel and typicality variants in place.
- [ ] Gallery updated.
- [ ] Tests pass.

**Prompt:**
> Read the spec and the plan. You are doing **Session 3: companion visuals**. Follow the ground rules, and stop at Session 3's Done list.

## Session 4: design-system documentation

**Goal:** the design system documents everything, so that Session 5 can audit against it rather than against the spec.

**Read:** the whole spec.

**Tasks:**
1. Locate the design-system sources: the component READMEs, the system README, the token source behind `tokens.json`, and the sync process to the Design System artifact.
2. **Component cards** for `PpVariabilityMark`, `PpVariabilityBadge` and `PpVariabilityFunnel`, in the existing format: source, properties, variants × forms, states, static previews, and Do/Don't lists from spec §9.
3. **README, Marks:** a concise "Variability marks" entry covering spec §1's principles, §2's family table and the §9 rules.
4. **README, content fundamentals:** all four vocabularies with their lines, the zone labels, the §7.5 basis-string table and the phrasing rules.
5. **Tokens:** the §10 tokens with usage notes.
6. **"Not synced":** list anything that can't be represented (for example the geometry tables) and point to `docs/design/variability-mark/spec.md`.
7. **Run the sync step.** If it's manual, say so.
8. **Decisions record:** write `docs/design/variability-mark/decisions.md` from spec §12.

**Done:**
- [ ] Cards, README sections and tokens documented.
- [ ] Synced, or a manual resync flagged.
- [ ] `decisions.md` written.
- [ ] **Then merge the branch.**

**Prompt:**
> Read the spec and the plan. You are doing **Session 4: design-system documentation**. Follow the ground rules. The aim is that someone could apply the marks correctly from the design system alone. Stop at Session 4's Done list.

## Session 5: audit and retrospective application

**Goal:** apply the system everywhere it belongs.

**Reference: the design system, not the spec.** Any rule the audit needs that the design system lacks is a documentation gap. Fix the documentation first, in a separate commit.

**Tasks:**
1. **Inventory.** Grep QML, C++ display code, Qt Graphs setups, report templates, seed packs and diagnostics narratives for:
   - consistency indicators and copy (`consisten`, `repeatab`, `variab`, `spread`, `scatter`);
   - σ, SD, variance, CV, IQR and box plots;
   - per-swing "vs average" or outlier displays;
   - multi-session spread or trend displays;
   - certainty or confidence displays;
   - the launch-monitor dispersion panel.
2. **Classify** each entry as **replace**, **pair**, **measurement**, **typicality**, **funnel** or **leave** (spec §9.2), with a reason for each. **Post the table before changing anything.**
3. **Copy audit.** List every relevant user-facing string with its replacement from the vocabularies. Old words without a one-to-one mapping get a proposed mapping; never silently drop meaning.
4. **Apply** in logical commits, one per screen or area. Apply the §9.5 rules where their surfaces exist (intent grouping, work-on completion, strength-mark separation, print, roster) and the launch-monitor rules (§9.6).
5. **Record** every **leave** row, every rule with no surface yet, and the remaining Needle surfaces in `decisions.md`.

**Done:**
- [ ] Classification table and copy audit posted.
- [ ] Every row except **leave** applied.
- [ ] No variability copy remains outside the vocabularies.
- [ ] Tests pass, with no new QML warnings.

**Prompt:**
> Read the design-system documentation for the variability marks, and `docs/design/variability-mark/plan.md`. You are doing **Session 5: audit and retrospective application**. Use the design system as your reference. If it's missing a rule you need, fix the documentation first. Post the classification table and the copy audit, then wait for approval before applying.

## Checkpoints for you

| After | Check |
|---|---|
| Session 1 | The calibration report. Are the bands lopsided? Approve or adjust the constants. |
| Session 2 | The spike numbers, and screenshots at 12–16 px. Do the dark themes and greyscale hold up? |
| Session 3 | The funnel and typicality on your own swings. |
| Session 4 | Read the design-system pages cold. Could you apply them without the spec? |
| Session 5 | The classification table, before anything is applied. |

## Out of scope (whole plan)

- New outlier or mishit detection.
- Restyling the tables or panels that host the marks.
- Cohort-specific scales.
- The golfer-layer box-plot replacement (an open item in the spec).
