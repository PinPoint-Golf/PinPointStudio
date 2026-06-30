# Validation — document index

The pipeline validation, tuning & refinement programme. Read the reference first (the *why*), then the
protocol for the corpus you are capturing (the *how*).

| Document | Role | Audience |
|---|---|---|
| [`pipeline_validation_and_tuning.md`](pipeline_validation_and_tuning.md) | **Reference / methodological backbone** — validity hierarchy, reliability≠agreement≠accuracy, per-stage V&V&T along the pipeline, the three-corpus progression, and **sample-size / statistical power** | analysts, reviewers, anyone deciding *what counts as proof* |
| [`tunable_parameters_reference.md`](tunable_parameters_reference.md) | **Parameter catalog + developer guide** — every tunable knob (`seg./shaft./assembly./score./sampler./rules./bands./filter.*`), what each one moves, the statistic + N that validates it, and **how the injection + sweep machinery works in code** | analysts (Part I) + developers wiring a knob or running a sweep (Part II) |
| [`corpus1_collection_protocol.md`](corpus1_collection_protocol.md) | **Corpus 1** capture protocol — IMU-only; internal consistency, reliability, known-groups (HackMotion = pilot) | data collection team |
| [`corpus2_collection_protocol.md`](corpus2_collection_protocol.md) | **Corpus 2** capture protocol — IMU + HackMotion concurrent; criterion validity (locks signs, `A·M`, bands) | data collection team |
| [`corpus3_collection_protocol.md`](corpus3_collection_protocol.md) | **Corpus 3** capture protocol — calibrated cameras + IMU + HackMotion verification, multi-golfer; external validity + camera metrics + coach/outcome | data collection team |
| [`shaft_validation_protocol.md`](shaft_validation_protocol.md) | **Shaft tracker** V&V&T — *standalone, IMU-independent* (2D image-plane θ/head/length): capture spec, blinded markup, IMU-less SwingLab run, `shaft.*`/`assembly.*` sweep + K-flag A/B | analysts running the vision-only shaft programme |

**The three corpora build on each other** (tests are supersets): Corpus 2 runs Corpus 1's suite as a
regression net and adds criterion agreement; Corpus 3 runs both and adds external validity + the camera
branch. Each protocol has a *"Changes from Corpus N−1"* delta near the top and **completion-tracking
tables to fill in as you capture.**

**Operational runbook (Corpus 1):** `../implementation/corpus_v1_validation_plan.md` ·
**Harness:** `../developer/swinglab_developer_guide.md`.
