# Briefs: how the design agents were briefed

The four briefs used on 11 October 2026 to redesign Session diagnostics, kept as worked examples.
Copy their shape for another panel; don't copy their content. Paths in them are historical: `$D` was
the session scratchpad's `diag/` folder, `variant.sh`, `variant3.sh` and `render_variant3.qml` are now
`render.sh` and `render_overlay.qml` here, and `RULES.md` (the house rules) is now section 15 of
`docs/design/pinpoint_qml_design_system.md`.

| File | Round | Purpose |
|---|---|---|
| `round1_three_variants.md` | 1 | Three agents, three typeset directions (a, b, c), in parallel. Gives Mark's own words, a review of what made the old panel ugly, the shared principles, the data shapes (from the JSON dump) and the harness contract. Each agent writes one `Variant.qml` and a `NOTES.md`. |
| `round2.md` | 2 | One design, every space: each variant keeps its identity but must hold up at full, stage, half, third and in print. Its real job was to surface the **rules** that make a design hold up; the agents, working separately, agreed on them, and they became the house rules. |
| `round3.md` | 3 | One aesthetic, three registers (Replay, Analyse, Print), with interaction (`demoState`) and fault-forward figures, built from a shared block library. Lists the binding rules and the register contract. |
| `round4.md` | 4 | Pre-port, unattended: close the gaps the inventory found (states S1–S42, features F1–F68) in the scratch library before anything moves into `src`. Decisions are taken for Mark in the brief, and every new decision is written down. |

What every brief has, and a new one should too:
- Mark's words, quoted, and his verdict on the previous round;
- the real sizes (panel width and height at each layout) and the fixture (4 July session, shot 8);
- the data shapes, from a dump (`render.sh --dump`), so agents design for real data;
- the harness commands, their own slot, and the guard;
- the rules: no repo edits, no builds, no commits, no sound, look at every render;
- what to hand back: final renders under a fixed tag, and notes of bounded length.
