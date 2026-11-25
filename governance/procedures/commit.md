# Procedure: commit a change

1. Check that the commit is allowed: approved for this change ([GIT-1](../controls/commit.md)), or covered by a refactor's pre-approval ([GIT-2](../controls/commit.md)) or an unattended session the Product Owner asked for ([GIT-4](../controls/commit.md)). Within a plan, commit at the end ([GIT-3](../controls/commit.md)).
2. Update the tests and the docs the change touches, in the same commit ([GIT-6](../controls/commit.md)); run the affected tests through ctest ([BT-3](../controls/test.md), [BT-4](../controls/test.md)).
3. Stage everything except `build/` (`git add -A -- . ':!build'`), read `git status`, and leave out anything the change didn't write ([GIT-7](../controls/commit.md)).
4. Write the message: `Area: one-line summary`, a blank line, a plain-words body in the author's voice, no tool attribution, and the tests and docs touched ([GIT-8](../controls/commit.md), [GIT-9](../controls/commit.md), [GIT-10](../controls/commit.md), [GIT-6](../controls/commit.md)). Use `git commit -F <file>` so the blank line is real.
5. Commit on `main` and push `origin main` ([GIT-5](../controls/commit.md)).
