# B1.9 review (AG-21): a fresh reviewer agent, given the brief, COMMON, MOVES, the excerpt, NOTES-B1.11 §8, the diff and NOTES-B1.9 §6 on

**SIGNED OFF** (the Mac half; the studio PC run was pending at review time). No blocking finding. Re-ran
`architecture_lint_test`, `report_studio_test`, `isolated_stores_test` through the lock: 3/3. Diff = `B1.9-diff.patch`.
Accepted deviations: the accept count 2 -> 1 (the brief's 4 -> 3 predates B1.11); `context_check.py:66` left re-keyed.

1. edge: commit message line 17 claimed a studio PC result before it existed. Fixed: the real result.
2. edge: "beside the app state that owns them": `AppState` does not hold it (`main.cpp:791` constructs it; IM owner "itself;
   application"). Fixed: "beside the app state, in the application layer".
3. edge: area `App:` is new in the log (GIT-8). Fixed: `Platform:`, as B1.11.
4. edge: NOTES §6-§9 had no EDGE list or studio PC summary. Fixed (§10, §11).
5. edge: `context_check.py:65-66` sits under "to place" though Q11 ruled it: delete in the Core-flip step.
6. edge: `docs/implementation/platform_e_host_settings.md:201` (proposed Stage E page) greps `src/Core/studio_profile.h`:
   re-point when the Stage E brief is written.
7. edge: `docs/research/context_map/check_includes.py:56` keeps the old key (research copy; B1.11 accepted the same).
8. edge: IM `:435` RING-PLATFORM and `[^sa24-r1-4]` still say the Core rule waits for B1.9; true until the flip: update
   in the Core-flip item.
