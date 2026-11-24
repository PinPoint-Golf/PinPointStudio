# tools/method — the refactor method's tools

Three small tools the refactor method depends on. Each has a header saying how to run it and a `--self-test` that runs on fixtures in a temporary directory; ctest runs the self-tests in the Gui suite (`src/Gui/tests/CMakeLists.txt`).

**`im_excerpt.py`** — prints the part of the information model (`docs/developer/information_model.md`) a brief needs: §10 (ownership) whole, the row or heading that defines each id given, and the footnotes those cite. The coordinator writes it into a brief's `IM-*.md` excerpt, so an implementing agent reads that excerpt and never the whole model (about 100k tokens). `python3 tools/method/im_excerpt.py ID [ID ...] > IM-x.md`; `--im <path>` reads another copy. ctest: `im_excerpt_selftest`.

**`one_at_a_time.sh`** — the machine-wide heavy-work lock. Every build, ctest run, lint run and app probe on the development Mac runs through it, so that only one runs at a time across every lane and worktree (the machine has 16 GB). The lock is a `mkdir` folder at `<main checkout>/build/.heavy.lock`, found from git's common dir, so a copy run from any worktree takes the same lock; a lock whose holder has died is reclaimed. `tools/method/one_at_a_time.sh <command...>`; `--print-lock` prints the path. ctest: `one_at_a_time_selftest`.

**`im_changelog_merge.py`** — resolves information-model changelog clashes when one lane's change is rebased onto another's: where both sides of a conflict are a single changelog row, it keeps both and renumbers the second to the next free version; any other conflict is left in place and reported (exit 1). `python3 tools/method/im_changelog_merge.py docs/developer/information_model.md`, mid-rebase. ctest: `im_changelog_merge_selftest`.
