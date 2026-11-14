# Adversarial pass 1 — High findings of A, B, C, H

Pinned at `37289e33` (working tree = HEAD for every cited file; only `docs/implementation/swe-audit/` is untracked).
I tried to disprove each finding. One read-only check was made on the share (`/mnt/swingdata`, JSON reads only) for H-04.

**Tally (18):** 12 stand · 3 stand at a lower severity (A-03, A-04, C-05) · 2 partly wrong (C-02, which is also downgraded, and C-04) · 1 stands at a higher severity (H-04) · 0 wrong.

---

## A · Analysis + Metrics

### A-01 · two job builders, already disagreeing — **Stands** (the scenario is narrower than written)
- The input difference is real. `swing_reanalyzer.cpp:839` `if (st.valid) job.standardClubLengthM = …` is the only writer. It is read at `analysis_pipeline.cpp:2959` (DtlPosture) and `:3369` (Skeleton3D). `shot_analyzer.h:69` "Only the 3-D skeleton reads it" is false.
- `docs/design/ball_reach_by_club.md:62-64` says that, without a club record, L "is the standard length of the club the swing names". Live does not do this, so the code has drifted from the design.
- **Mitigation the finding missed:** the live gap is rarely hit. Default club records are seeded with the standard length (`athlete_controller.cpp:676-680` `rec["lengthMm"] = st.lengthMm;`), so live usually has `lengthMm > 0`, which makes `clubLengthKnown` true (`shot_processor.cpp:1278-1280`). The exporter writes `capture.club.lengthMm` from the same record (`shot_processor.cpp:1610`, `swing_exporter.cpp:989`), and re-analysis then reads it as known (`swing_reanalyzer.cpp:795-796`). The two paths differ only in three cases: a record whose length is 0, an active club that is not in the bag, or no active club with `review.club` set afterwards.
- The handedness and perspective duplicates are as cited. High stands on the structure: every job field has to be added to two builders.
- To settle the size: re-analyse one swing that has a club record with length 0, and diff `ballReachExcess` and the σ.

### A-02 · one writer, four projections — **Stands**
- (a) `disk_replay_source.cpp:344` `an["score"].toInt()` gives 0, because `score` is an object. No reader of `overall` exists in QML. The only hit is the comment at `shot_list_model.h:260`, so this is latent, as stated.
- (b) `readSwingJson` (`swing_doc.cpp:2266-2330`) inserts no `kinematicSequence`, `dtl` or `addressMarks`. The presenter patch is at `charts_traces_presenter.cpp:3484`.
- No typed `fromJson` exists in Export or Analysis. `swing_window_parity_test.cpp:726` compares typed `SwingAnalysis` objects after re-analysis, not the QVariant projections, so no test covers the projections.
- This is the same finding as H-01. Count it once.

### A-03 · two time domains, guessed by magnitude — **Stands, lower severity: Medium**
- Persisted documents have had one domain since `717d4273` (2026-07-07, "normalise analysis t_us to window-relative"). `swing_doc.cpp:437` writes phases through `rel()`.
- The three reader heuristics (`disk_replay_source.cpp:68-72`, `session_series_model.cpp:189`, `report_stills_pure.h:121`) therefore act only as legacy shims. Their comments are stale: "Live captures write ABSOLUTE … analysis t_us" has been false for the writer since July.
- The claimed misfire (a relative time ≥ `t0`) cannot happen in practice. `t0` is the boot-relative buffer clock, so it is far larger than any window length. The synthetic case is `t0 = 0`, where `rel` is the identity.
- What remains is real but contained:
  - In-memory live detail is absolute and reload detail is relative. This is A-02(e).
  - The reanalyzer's phase-impact fallback is dead for current documents (`swing_reanalyzer.cpp:742` "phases t_us are ABSOLUTE", then `:777` subtracts `t0`). It fires only when `capture.impactUs` is −1, and the exporter always writes that key (`swing_exporter.cpp:963`). Old (pre-July) documents still take the fallback correctly.
- This is a cost and risk of change, not something every change must work around.

### A-04 · route precedence stated four ways — **Stands, lower severity: Medium**
- The structure is as described. Precedence passes run at the tail of `runProfile` (`analysis_pipeline.cpp:4659-4683`). `findSeriesByLadder` is a fixed `hm.` rung (`swing_analysis.h:336-343`). The catalogue ladder is consulted only for availability and "needs" (`metric_provider.h:156-212`).
- The finding missed three things:
  - The analysis-layer ladder is a documented decision not to reuse `preferKeys`: `swing_analysis.h:329-334` "deliberately NOT the same mechanism … the analysis layer has exactly one question".
  - The `hm.` key is a documented choice: `metric_providers.cpp:38-41`.
  - The passes are tied to the ladder by test: `metric_catalogue_test.cpp:782` "The bends: the sensor rung FIRST (it beats the camera, applyTrunkSensorPrecedence)", and `:859`, `:911-930`.
- No disagreement between the copies was shown. `swing_doc.cpp:1662` says "Today nothing does". The hazard arises only when a second producer of an existing key is added, so it does not hit every change.
- Minor: the cited `metric_descriptor.h:556-559` is actually at `:563-566`.

## B · Diagnostics

### B-01 · saved corridor edit never reaches the app — **Stands**
- `ModelBrowser::refresh()` (`model_browser.cpp:1017-1024`) is the only caller of `resetSharedNormProvider()`. No QML calls `browser.refresh()`: the `refresh()` hits in QML are other objects.
- `save()` (`:5149-5232`) resets screens, drills and gate words but not the norms.
- `libraryChanged` has one QML listener, `DiagnosticModel.qml:97`, which only bumps its own revision.
- The editor is reachable at Settings → slot 10 (`ScreenSettings.qml:568`).
- Even with a reset, the holders captured at construction (`lm_session_model.cpp:689`, `session_diagnostics_model.cpp:703`, `postureSharedEnv` statics) would keep the old set, so a reset alone is not the fix.
- To settle the runtime claim: edit one corridor, save, and read a Charts or This shot grade before and after a relaunch.

### B-02 · ≥10 independent pack assemblies — **Stands**
- Every site is as cited (grep of `makeCharacteristicPackProvider`).
- `makeCharacteristicPackProvider()` (`merged_pack_provider.cpp:293-305`) builds a fresh merged provider with no reload or mtime logic. That confirms the frozen copies: `main.cpp:442-446` (captured lambda), `session_posture.cpp:857` (static), `consistency_env.cpp:54`.
- The goal-edit split between the fresh report (`session_report_model.cpp:1163`) and the frozen list follows from the code.

### B-03 · unsynchronised lazy singletons touched from workers — **Stands** (suspected race)
- `reference_pack.cpp:470, 503` (`if (!g_refs) g_refs = new …`) and `merged_norm_provider.cpp:243-252` are plain unsynchronised checks.
- `postureSharedEnv()` (`session_posture.cpp:855-859`) is reached from `computePosture` on the pool (`charts_posture_presenter.cpp:1593`, `:1806`). Its function-local statics are thread-safe for their own initialisation, but the calls inside them (`loadPack` → `sharedReferenceSet()`, `sharedNormProvider()`) are not.
- I found no startup warm-up of these caches on the GUI thread in `main.cpp`. The work-ons first pass is deferred by 3 s (`main.cpp:385`), so a Charts → Posture open in that window is a real interleaving.
- This needs a TSan run, or an assert that the first touch is on the GUI thread, to confirm.

### B-05 · norm standing in prose, misread by the dashboard — **Stands**
- The decisive pair is in `norms.json`:
  - `m_takeawayPlane`: "Mark's decision 13 Nov 2026, provisional". The release dashboard lists it as decided.
  - `m_clubSlotTop`: "Mark approved the pair 13 Nov 2026, provisional". The release dashboard lists it as plain heuristic (`release_status.md:256`).
- These are the same standing with different words, so one row is mislabelled under the rule at `gen_release_status.py:83`.
- One sub-example is weak. `m_leadElbowFoldAfterImpact` ("Mark decides the final value") is correctly *not* counted as decided. The point stands only in the sense that its "pending a decision" standing has no field to live in.

### B-07 · "club assumed" demotion never runs — **Stands** (deterministic from code)
- The chain is complete:
  - `measure_sample.cpp:1000` `grid.club = swingDocDeclaredClub(root)` (no stub)
  - → `live_measure_source.cpp:341` `contextIdForClub(m_grid.club)`
  - → `context_tree.cpp:278` an empty club becomes `"full_swing"` (`context_tree.h:49-52`)
  - → `detectForSwing` passes that non-empty id into `NormMeasureSource` (`live_measure_source.cpp:360-364`)
  - → `norm_measure_source.h:179` `r.contextInferred = m_contextId.isEmpty()` is always false.
- `session_diagnostics_model.cpp:1034-1038` repeats the false "DRIVER stub" premise.
- `norm_measure_source.h:75-78` and `gate_catalogue_rules.h:138-141` promise the demotion.
- The reach is now mostly pre-`capture.club.name` camera swings. Those are the library's oldest swings (58 of 123 corpus shots in Sept, per `club_vocabulary.h`).

## C · Coaching

### C-01 · no owner per level; outcomes recomposed per consumer — **Stands**
- `tendencies.h:26-28` (re-composed per reduction).
- Two `ShotOutcomeSessionFacts` builders: `session_diagnostics_model.cpp:1779` and `tendencies.h:537`.
- Tendencies read the evidence from `work_ons.json` (`work_ons_controller.cpp:168-173`), which says "Absent on a record not re-derived yet". That supports the staleness scenario.
- Recomposing from rows is a stated design ("so a correction takes effect at the next reduction"). It is not drift, but that does not answer the ownership point.

### C-02 · session verdict persisted into shot rows — **Partly wrong; lower severity: Medium**
- **Wrong part 1:** "A shot's persisted row state is no longer that shot's reading". The row keeps its value, corridor and evidence, and is flagged `overruled` (`strike_from_flight.h:531-533`). The check reverses every earlier overrule before judging again (`:561-567`). The measured reading is therefore recoverable from the file, and the overlay the finding proposes is already encoded per row.
- **Wrong part 2:** "a rule version disguised as a schema". Schema 14 added the `ConditionRow::overruled` field (`diagnostic_ledger.h:224-229`), which is a genuine row-shape change.
- **What stands:**
  - Nothing stamps the flight rule. `computeContentStamp()` (`session_diagnostics_model.cpp:929-944`) hashes the pack, norms, contexts, schema, words and grid. It does not include `flightNorms()` or the `strike_from_flight.h` thresholds.
  - So a threshold change leaves stored verdicts stale for the readers that do not re-run the check (`work_ons_controller.cpp:196/1396`, Charts).
  - The persistence was Mark's 10 Nov choice, made so that every reader counts one verdict (`strike_from_flight.h:519-523`).
- This is a missing version input, which is Medium.

### C-04 · golfer English built and persisted in the domain — **Partly wrong** (High stands on the rest)
- The persisted words are as cited (`tendencies.h:1209-1231`, `strike_from_flight.h:542-545`).
- **Wrong part:** "a pack wording edit changes `mixKey` and so splits one shot combination into two groups". Tendencies re-compose *every* shot with the current pack on each reduction (`tendencies.h:26-28`), and they group within that one pass (`:1126-1128`). A wording edit therefore moves every key together, and nothing splits.
- "Contract D" (`tendencies.h:29`, `shot_outcomes.h:32`) is about left-hander glossing, not a decision to persist words, so it is not a mitigation.

### C-05 · versions bumped by hand; changelogs lost entries — **Stands, lower severity: Medium**
- The five bumps in `770b9fce` are confirmed (`git show 770b9fce`).
- The missing changelog entries are confirmed: `tendencies.h:71` lists 2–4 and 9–17, and `work_ons.h:84-93` has no entry for 5, which was bumped in `398a6c48`.
- **Weak sub-claim:** not bumping `kThemeRuleVersion` is correct, not a near miss. The themes' own rule did not change, and the themes fingerprint hashes the work-on fingerprints (`work_ons_controller.cpp:1486-1494`). That is the "key from declared dependencies" the fix asks for.
- No stale cache was shown, so this is cost of change (Medium). The missing entries are Low hygiene.

### C-06 · five projections of kind/hold; themes ignore `reportHold` — **Stands** (the disagreement is suspected)
- `swing_themes_pack.h:156` `info.fault = c.kind == ConditionKind::Fault`, and there is no `reportHold` in either themes file.
- Themes count ledger rows directly (`swing_themes.h:1437-1453`).
- `chicken_wing` is active, kind fault, held, and its measure `m_leadElbowFoldAfterImpact` is `live`. It can therefore fire and reach the themes' needs-work list while it stays off Work-ons. The scenario is reachable.
- Whether Your swing should honour `reportHold` is Mark's call.

## H · Persistence

### H-01 · four `analysis` converters, drifted — **Stands** (duplicate of A-02)
- `shot_processor.cpp:395-399` phases lack `timing`.
- `toPlaneChannelDetail` (`:203-226`) lacks the σ and `pNeedle` that `swing_doc.cpp:749-753` adds, despite its comment at `:197` "identical shape in both parity writers".
- The `overall` defect is as in A-02. It is latent; I found no reader.

### H-02 · header built twice; `athlete.heightM` only on one path — **Stands** (suspected runtime)
- The exporter's athlete block (`swing_exporter.cpp:922-926`) has no height.
- The only writer of the height is `shot_processor.cpp:2099`. The only reader is `swing_reanalyzer.cpp:788`.
- No app-side override of the height exists (grep of `athleteHeightM`). Only `swinglab_run --height-m` (`swinglab_run.cpp:1153`) supplies one.
- `docs/reference/swing_json_schema.md:194-196` documents the gap as a gap, not as a decision.
- This is broader than A-01: it affects every camera swing that is re-analysed.
- To settle it: re-analyse one camera swing with a profile height set, and diff `analysis.skeleton3d`.

### H-03 · several writers of `diagnostics.json` — **Stands** (lost updates suspected)
- `persist()` merges only when `m_baselineAuthority == Always` (`session_diagnostics_model.cpp:3116-3130`). The default is `Live` (`.h:990`).
- `correctOutcome` (`:2388-2434`) writes the model's own map and persists. I found no file watcher and no cross-model sync (grep of `QFileSystemWatcher`).
- The cast window hosts a full interactive panel (`PpSessionDiagnosticsWindow.qml:84-89, 132`), so (b) is reachable.
- To settle (b): correct a shot in the cast, take the next shot, then read `corrections` in the file.

### H-04 · user input in the same file as a cache rebuilt by deletion — **Stands, higher severity: Critical candidate (observed loss)**
- `regrade_ledger.cpp:86` renames the file aside, and `:99` rebuilds it with no envelope carry-over. `session_diagnostics_model.cpp:1465` parses with no error check.
- **Evidence of an actual loss:** in `/mnt/swingdata/corpus/swings/2026-07-10_Mark-Liversedge_Wrist_02/`:
  - `diagnostics.json.pre-79ffef1` holds `intent.focusConditionId = "excessive_shaft_lean"`.
  - Every later file has `focus = ''`: `pre-batch14`, `aa-sweep`, `pl-sweep`, `pre-6c990076` and the live `diagnostics.json`.
  - `excessive_shaft_lean` still exists in `core.json`, and the only setter of `m_focusConditionId` is `declareFocusFrom` (`:1660`), so nothing else would have cleared it.
- Across all 100 `diagnostics.json.*` backups on the share (corpus plus the Mark and Jess library sessions), this was the only loss. No `shotIntent`, `corrections`, `intentFrom`, `declaredMiss` or `screens` were lost.
- **Mitigations:**
  - The data survives in the backup, because the tool refuses to overwrite one (`regrade_ledger.cpp:80-85`).
  - The session is corpus test material, and the focus may have been probe input.
- Mark should say whether that focus was his.
