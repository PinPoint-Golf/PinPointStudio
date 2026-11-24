# 06a · The judgement-call log (appendix to 06)

*The refactor's decision log, as kept during the work. It holds two series, each numbered from J-1:*
- ***Series 1*** *is step 2's 69 calls (15 Nov; `build/refactor/step-2/JUDGEMENT-CALLS.md`), with the Product Owner's answers of the same day (`JUDGEMENT-ANSWERS.md`). 06 writes these "S2 J-n".*
- ***Series 2*** *is steps 3 to 7 and the close: J-1 to J-112, 5 "DEC (…)" entries recording role decisions, and the rulings of 24 Nov (`build/refactor/step-3/JUDGEMENT-CALLS.md`). A bare J-number in 06 means this series.*

*Entries are calls the coordinator made and logged for review, or rulings the roles gave. In entries written before the roles rule (22 Nov), the owner's name has been replaced mechanically with "the Product Owner"; where the owner acted as Solution Architect, Design Lead or Coaching Lead, the entry's own text says so. Other golfers appear as golfer A, B and C, and athlete ids are removed.*

# Series 1 · Step 2 (S2 J-1 to J-69)

## Step 2 — judgement calls made unattended (for the Product Owner's review at step 2's end)
The Product Owner, 15 Nov 2026: "make judgement calls and then we can review them at the end of step 2."
Each row: the call, the options, what was chosen and why, and how to undo it. Library/corpus writes, allowlist additions
(other than S1-g's approved lines), design-system changes and freeze breaches are NOT made here — they wait for the Product Owner.

| # | Item | Call | Options | Chosen, and why | Undo |
|---|---|---|---|---|---|
| J-1 | v1.8 docs | Which step widens lint A1 (S1-g; DEC-146 names none) | step 1 follow-up now / step 2 / step 8 | **Step 2**, as its own small item after 2.0 (it edits the same lint file; your S1-g answer approved the allowlist lines). The reference keeps its "(proposed, for the Product Owner)" mark until you confirm | move it to any later step |
| J-2 | v1.8 docs, MD-01 | A club correction moves a shot from one club's pool to another | recompute "that club" / both clubs | **Both clubs whose pool changes** — your rule is "any event that changes a club's pool"; a correction changes two | n/a (follows the rule) |
| J-3 | v1.8 docs, MD-01 | Does a re-analysis (EVT-02) recompute the usuals? The auditor's event list omitted it | omit / include | **Include**: a re-analysis can change a shot's part-swing classification (`classifySwingLength` reads the analysed P4 shaft angle, `club_distances.h:139`), and a part swing leaves the pool — so it changes the pool | drop EVT-02 from the chain |
| J-4 | v1.8 docs | The OS emptying its own trash (not covered by MD-08) | decide now / open question | **New open question Q-29, for step 3** — not decided by me | — |
| J-5 | v1.8 docs, T7/T8 | Your words: norms are kept with the findings "because they are from the model at the time of the session and may change later". The reference says the stamp records the norm sets / grade policy "in force when the findings fired". The two differ only after a re-fire (DEC-100: a norm edit re-fires a session's findings) — then the stamp shows the NEW norms, not the session-time ones | (a) "in force when fired" (re-fire takes new norms); (b) the session freezes its norms like its club baseline, and a norm edit re-fires only on request | **Left as (a)**, matching DEC-100's re-fire, and flagged for you: if you meant (b), it's a change to DEC-100 | reword T7/T8 + DEC-100 |
| J-6 | v1.8 docs (reviewer note) | report_meta.json now holds source facts (the coach's note, the send log) beside derived ones (fingerprint, PDF name) | split the file now / leave to step 5 | **Leave to step 5** (the report's step; no code touched now) — noted in 04 step 5 | — |
| J-7 | 2.0 | `atomic_write` header-only (brief said .h/.cpp) | .h+.cpp on every link line / header-only | **Header-only** (implementer's call, accepted): there's no Core library; ~97 test targets + app + tools compile a call site | add a .cpp when a Core library exists |
| J-8 | 2.0 | pack_io keeps a forwarding call (no I/O of its own) so the packs' error wording "No writable application data location." is unchanged | forward / rewrite every pack | **Forward** (no user-visible change) | remove in step 7's rules-down |
| J-9 | 2.0 | `AtomicFile` (held open across calls) added beside the callback form, for the PDF writer and `.raw` writer | one form only / two | **Two forms** — both writers stream across calls | — |
| J-10 | 2.0 | Two small behaviour differences: a failed export no longer leaves a partial `.raw`; the trunk ceremony's set-aside copy gets a new modification time (same bytes) | keep old / accept | **Accept** — both are the point of atomic writes (R-PERS-2). In full (reviewer): if a new trunk ceremony fails to write, `trunk_ceremony.json` now still holds the earlier ceremony (also copied to `trunk_ceremony_<n>.json`) where before the file was absent; a `.raw` whose final commit fails is now dropped, where before it could be recorded complete while truncated. Not covered by a test (needs capture hardware): on your "to try" list | revert those two sites |
| J-11 | 2.0 review | QLockFile calls a lock older than 60 s stale even if its holder is alive; after a takeover the first holder's unlock deletes the new holder's file. §4.9 had a tool hold the lock for its whole work on a folder | hold for the whole run / re-take per write / refresh while working | **Re-take per write** (D11 already says "short locks per write"); every later item (2.2 repositories, 2.8 tools) holds a lock only around one write, never across a run. The InProgress refusal (session.json status) is what keeps a tool off a live session | refresh the lock in a long run instead |
| J-12 | 2.1 | **D3's exact recipe — fixed for ever** (changing it later re-keys every old session): `uuid5(namespace 98a52a15-71c0-40f2-b377-f9a758154d28, "<athlete uuid, lower case, no braces>/<folder name, Unicode NFC>")`, UTF-8 | any recipe | **This one**: identical to Python's `uuid.uuid5`, so the migration and any script reproduce it; NFC so a folder name the Mac stored decomposed is still one session. **Please look at this one first** — it's the only call that can't be cheaply undone once M2 runs | before M2 runs on the library: change freely |
| J-13 | 2.1 | A legacy session's day and type | name's date / start time | Start time's local day when known, else the name's date; type from the name's token; whether `capture.sessionType` overrides the token is left to M2 (2.8) | — |
| J-14 | 2.1 | n-of-day when some start times are missing | — | Per day: by start time if every session that day has one, else by `_NN` (today's order); ties by folder path | — |
| J-15 | 2.1 | The session label's words ("1st 5 Jun", "Session" when untyped, "GRF") | — | Same words as today's labels, plain English; 2.7 may move them to a presenter | — |
| J-16 | 2.1 | `SwingPaths` "shrunk to names" | delete the allocator now / split | Split only: the allocator's callers are 2.2's files, so 2.2 deletes it | — |
| J-17 | 2.1 | Minor: ids accept braced/upper-case uuids from other tools (ShotRef only its own text); SessionIdentity holds its SessionId; header-only `identity.h`; the new tests compiled by the Analysis suite | — | as listed (implementer's NOTES §4 J1, J8–J10) | — |
| J-18 | 2.2 | A session ended by the next Start after a crash: its `endedAt` | now / last swing's time / absent | **Absent** ("end not known"): the real end is unknown, and P1 forbids a made-up fact | write the last swing's time instead |
| J-19 | 2.2 | Resuming a pre-step-2 session writes its first `session.json` | refuse / write with an inferred start | **Write**, `origin: backfilled` (by the app), start = earliest swing folder's creation time; M2 may correct it from capture times | — |
| J-20 | 2.2 | ~~The session folder is now made at Start~~ — **corrected by the reviewer**: today's Start already makes the folder; the only new thing on disk is `session.json` in it | — | no call needed | — |
| J-21 | 2.2 | "Extend" finds today's session of the same type whatever its folder's naming pattern (today: by folder name match) | name match / day + type | **Day + type** — your MD-05 answer is by day and type. Reviewer: this also decides which session the Wrist screen loads into its carousel (`ScreenWrist.qml:84`) — same rule, so the Wrist screen picks today's session of its type whatever its folder name. On your "to try" list | — |
| J-22 | 2.2 | A held folder lock is reported as "failed, held by <who>" (no separate "Locked" result until 2.4 owns that type) | — | as described | 2.4 adds `Locked` |
| J-23 | 2.2 | Minor (implementer's J4–J16): a corrupt `session.json` is read-only, never overwritten; a refused resume starts a new session; re-pointing the library discards the old session if it's empty and carries the declared focus from the new session's first shot; swings with no session start one on demand (today's lazy folder); folder spelling as composed today | — | all keep today's behaviour | — |
| J-24 | 2.2 review | The swing counter file (`.pinpoint-last-swing`) has two call sites: the session store and the trash (one function, as before step 2) | fix now / with the trash | **With 2.6** (the session trash behind `SessionRepository`) **and step 3** (a swing's trash, `trashPath` on a `swing_*` folder, moves with `Session::trashShot`); the reviewer asks that the swing trash not be forgotten — it's on 2.6's brief to check and, if it can't move before step 3, to say so | — |
| J-25 | 2.2 round 2 | New wording in the existing notifications: a failed resume says "Today's session could not be continued, so a new one started" (`session.resume.failed`); a held lock at swing allocation still reads "check the library path" | word it now / later | **As written**, surfacing a failure per DEC-126 through the existing notification centre (no new UI); the held-lock wording is left for 2.4 when `Locked` becomes its own result | reword the notice |
| J-26 | 2.3 | **The athlete picker's "Delete athlete" had no confirmation**, and after DEC-79 it moves the athlete's WHOLE folder, every session, to the library trash (recoverable; trash kept forever by default, `storage/trashRetentionDays` = 0). Before step 2 it only removed the settings entry | leave as one click / a confirmation with the existing PpConfirm (tone "error"; its design page lists destructive prompts) / toast + undo (needs a restore path: new functionality) | **Add the existing PpConfirm**, naming the golfer and how many sessions go to the trash. No new component, variant or token, so not a design-system change; I judged it part of building DEC-79 safely, not a new feature — **please confirm, it's the closest call to the freeze line so far** | remove the PpConfirm (one QML block) |
| J-27 | 2.3 | Athletes created before step 2 stay in settings until M1 runs, so until then their folder still follows their name (a rename still starts a new folder, as today) | — | Keeps today's behaviour until M1; M1 is the fix | — |
| J-28 | 2.3 | A deleted athlete's trash path: `<lib>/.pinpoint-trash/<athlete>/<stamp>_<athlete>` (brief: `<lib>/.pinpoint-trash/<stamp>_<athlete>`) | brief's / nested | **Nested**: with the brief's form housekeeping would treat each trashed session as its own entry and purge them one at a time | — |
| J-29 | 2.3 | Until 2.7b the session report finds an athlete's folder by name, so for a RENAMED athlete it looks in the wrong folder (as it already did before step 2) | fix in 2.3 / 2.7b | **2.7b** (its file list) — no regression against today | — |
| J-30 | 2.3 review | **Found by the reviewer:** two athletes can resolve to the same folder (renaming a settings athlete to another's name, E-02's look-alike case). Deleting either would have moved BOTH golfers' sessions to the trash | refuse the delete / remove only the record and keep the folder | **Keep the folder, remove only that athlete's record** (exactly today's pre-step-2 delete), with the existing notice naming the other athlete it is shared with; if the folder's `athlete.json` names a different id, refuse the delete. M1's dry run already reports folders holding two athletes for you to split | refuse in both cases |
| J-31 | 2.3 review | Three AthleteController properties (current name, initials, handedness) now announce a change on a rename too, so the screen updates | — | Accept (compatible; the QML surface's names are unchanged) | — |
| J-32 | 2.3 review | Open, not blocking: (a) the delete prompt says "the library's trash", but a library on a local disk sends the folder to the system Trash (the Sessions prompt has the same gap; your libraries are on the share, where it is right); (b) a hand-named athlete folder resolves to its sanitised name; (c) `moveToTrash` on a Windows network drive is untested — **don't delete an athlete on GOLFSIMPC until it is tried on a scratch athlete there** | — | left as is; (c) added to the cabin-PC checks | — |
| J-33 | 2.5 brief | Golfer input before migration M3 runs: the app rewrites `diagnostics.json` when it regrades, which could drop the legacy golfer keys once the code no longer "owns" them there | drop them (lost until M3) / move them lazily on open (an unapproved migration) / keep them | **Keep**: read new home first, legacy keys as fallback; carry legacy keys over unchanged on every rewrite; new edits only to the new homes. Only M3 removes them, after its field-by-field check (H-04) | — |
| J-34 | 2.3 commit | The clerk could not link the app in the shared tree (2.4's unfinished `swing_reanalyzer.cpp`); every 2.3 object, `main.cpp` included, compiled; its 8 tests passed; the lint passed on the staged files alone | wait for 2.4 / commit | **Committed `640c1bf5`**: the commit does not contain the failing file, so HEAD + 2.3 does not include it. The next full app build re-checks the link — **confirmed 10:3x: the app builds and links clean with 2.3 committed** (2.4's build-app-1c.log) | `git revert 640c1bf5` |
| J-35 | 2.4 | A folder not named `swing_NNNN` now gets a visible error from the tools, the converter and the archiver (before: processed anyway); a legacy session whose swings carry no `athlete.uuid`, reached without an athlete from its caller, cannot be given an id | keep processing odd folders / visible error | **Visible error** (show, never hide; a shot's identity is its swing number, DEC-91). Reviewer checked read-only: **every swing records `athlete.uuid` (284/284 Mac library, 164/164 corpus) and every folder is `swing_NNNN`**, so neither edge reaches a golfer on the Mac (GOLFSIMPC not checked). Side effect: `swinglab_run` now refuses the corpus specimen `capture_defects/…_swing_0010` (#53) for its folder name | accept odd names again |
| J-36 | 2.4 | Swing-file writes take no folder lock yet | lock now / with the tools in 2.8 | **2.8**, with the tools' locks (the InProgress refusal already keeps tools off live sessions) | — |
| J-37 | 2.4 | `SwingStore::save` stays public (≈30 test fixtures use it) | hide now / later | **Later** (the brief's "deleted when done" list keeps it on 2.8's close) | — |
| J-38 | 2.4 | 2.4 also edited files outside its §6 list: `session_store.*`/`session_repository.h` (thread-safe swing lookup, `Locked`), `swing_doc_writer.cpp`, `swing_reanalyzer.*`, `analysis_service.*`, `shot_processor.*`, the archiver's three door calls | — | Accepted as needed to re-key the door; the reviewer checks each | — |
| J-39 | 2.4 review | **Which athlete id a legacy session's id is derived from (D3/DEC-150 says "its athlete")**. Found: no swing's `athlete.uuid` matches the Mac's athlete records (they were minted on the cabin PC, H-09), and your swings carry two different uuids. So the tools (reading the swing header) and the app (using the athlete who owns the folder) would derive DIFFERENT ids for the same session | the swing header's uuid / the owning athlete's id | **The owning athlete's id** — from `athlete.json` once M1 has run (M1 settles one id per athlete across both machines), from this machine's record before that; the swing header only when no owner can be found. Nothing persists these ids before M2 except the Wrist ★ (never set on the Mac); M2 and M4 run after M1, so every persisted id comes from the reconciled athlete. Marked proposed for you, since it reads DEC-150 | use the header's uuid |
| J-40 | 2.4 review | `AthleteFiles::ownerOf` (J-39's lookup) is a second reader of `athlete.json` beside `AthleteStore` | fold now / later | **2.8's close** folds it into the one reader (R-PERS-1); two `--help` strings in `swinglab_run` still name the old function (cosmetic) — same | — |
| J-41 | 2.5 | A cleared shot answer (intent or correction) is written to the shot's review as `""` / `[]` — only while the old copy in `diagnostics.json` still holds an answer for that shot, so the old one can't reappear through the fallback (J-33) | — | A reading of IM-S2-3's keys (an empty value = "cleared"), not a new key | drop when M3 has run |
| J-42 | 2.5 | The diagnostics files layer takes a session folder, not a `SessionId`, until step 3 adds the id-keyed methods | — | as described | step 3 |
| J-43 | 2.5 | Behaviour edges: a ledger row with no swing document can no longer take a shot intent (fails with the existing notice); a `work_ons.json` / `tendencies.json` written by a NEWER app version is not read (schema check, R-PERS-3) | — | as described | — |
| J-44 | 2.5 | Shot-level input is now read from each swing's review — including once per session on the GUI thread (the club-baseline freeze). Your memory note: GUI freezes come from the 28 MB swing document | accept / measure first | **Measured**: only each document's root is read (not the 28 MB), but the club-baseline freeze spends **809 ms on the GUI thread** reading golfer input (once per session) and each session open adds 3–208 ms. **Decision: move the freeze's reads to a worker before commit; accept the per-open cost** (recorded) | — |
| J-45 | 2.5 | For M3 (2.8): the new homes win field by field; rewriting reviews doesn't touch `diagnostics.json`, so M3 also drops `tendencies.json` (a cache) to force a rebuild. **None of the Mac library's 23 `diagnostics.json` files holds any golfer input** — M3's real cases are in backups (H-04's 10 Jul focus) or on GOLFSIMPC | — | noted for 2.8 | — |
| J-46 | 2.6 | A session a crash left InProgress can't be archived or deleted until that athlete's next Start ends it (DEC-73) — the repository refuses InProgress sessions | refuse / allow with a warning | **Refuse** (follows D11: InProgress keeps everyone off); the athlete's next Start closes it | allow archive of crash-left sessions |
| J-47 | 2.6 | H-10 (the restore window) is closed only against writers that take the session lock; the app's own review edits don't take it until 2.8 (J-36). When 2.8 adds it, the archiver's merge/stub writes need a "lock already held" path or they'd block on themselves | — | **Carried to 2.8's brief**; step 1.4's three-way merge still protects the review meanwhile | — |
| J-48 | 2.6 | SET-14 ("archive keeps raw frames" off = permanent delete): MD-08 asked for a warning. No existing component shows one there; the setting's subtitle already warns | build a warning (design-system work → needs you) / subtitle is enough | **Subtitle is enough for now; your call** whether it needs more (a new warning would be a design-system change) | — |
| J-49 | 2.6 | Emptying the trash and the retention pass now share ONE purge function (MD-08's "one purge") | — | as built | — |
| J-50 | 2.6 review | **Found by the reviewer:** an interrupted archive could leave stubs with no marker; archiving again would then copy the stubs over the archive's full documents — pose and tracks lost for good | — | **Fix at the cause, twice over**: the marker is written as "archiving in progress" before any swing is thinned, and a re-archive never copies a stub over the archive's full copy; plus the marker-under-own-lock bug. Tests for the interrupted run | — |
| J-51 | 2.6 round 2 | The early "archiving in progress" marker is the same `archived.json` keys written twice (before thinning, then finalised) — no new "state" key, because a new key would change the file's information model and needs you. An interrupted archive therefore reads as archived (restore brings every swing back whole) | add a state key (needs you) / same keys | **Same keys**; your call whether an explicit state key is worth an IM change | add the key with your approval |
| J-52 | 2.6 review | Remaining gap (not blocking): a restore started from ANOTHER computer while this one is thinning could still leave stubs without a marker; no data is lost (a stub is never copied over the archive's full copy); 2.8's per-swing lock re-check closes it. Order of commits: 2.5 (main tree) first, then 2.6 rebased onto it, because they share four files | — | carried to 2.8 | — |
| J-53 | 2.5 round 2 | Per-open review reads run 8 in parallel (50 swings: 25–28 ms instead of 183–208 ms on the share); the club-baseline freeze's reads moved to a worker (GUI thread back to HEAD's ≈229 ms; ordering guaranteed: nothing grades until the freeze lands) | serial / parallel | **Parallel** — faster, same results (tests B1–B8) | set the pool to 1 |
| J-54 | 2.7a | After a gap (a trashed or unanalysed swing) the diagnostics badge read "REVIEWING · shot 8 of 7": swing number over a count | keep / drop "of M" / show both | **Show both when they differ: "shot 8 · 6 of 7"** (swing number · place · count); a session with no gap keeps today's "shot 3 of 6". The swing number names the shot (E-06), the place keeps "how far through" honest. Model text only, no component change | revert to "shot 8 of 7" |
| J-55 | 2.7a | **Needs you (design system):** This shot's heading still numbers by position (`PpTypeset.shotNo` = index + 1, documented on its design-system page), so after a gap the header says "SHOT 9" while This shot says "Shot 7 of 7". Changing `shotNo` is a design-system change | change it now (needs you) / wait | **Not changed** — HARD rule; recommend you approve it with 2.7b so every surface uses the swing number. Reviewer's correction: before, the diagnostics panel agreed with itself and disagreed with the carousel; now its header says "shot 9" while This shot says "Shot 7 of 7" — a different disagreement, only after a gap | — |
| J-56 | 2.7a | A day with an untimed (legacy) session now orders by folder name, not by the `_NN` counter (changes 2.1's J-14) | — | Needed for parity run A to show 0 differences; it fixes the upgrade-day order only for sessions of the SAME type (reviewer: a legacy morning `Wrist_01` still sorts after a new afternoon `Swing_01` — mis-ordered before too; M2 closes it by giving every legacy session a start time). Every live Mac session today is date-name-type with no `session.json`, so the old and new rules order them identically | revert to `_NN` |
| J-57 | 2.7a | Display-only label changes ("Shot N" = swing number) in: golfer A's 20 Oct Wrist_01 and Wrist_03, golfer B's 20 Oct Wrist_03, golfer C's 24 Oct Wrist_01. Parity run B's expected set (sessions whose order changes) is EMPTY on the Mac | — | on your "to try" list | — |
| J-58 | 2.8a | **Needs you before M1 runs: which id each athlete keeps.** No swing records the Mac's athlete ids (they were created on the cabin PC), and your own swings carry two different uuids. Session ids (J-39) follow the id M1 settles | the Mac's record ids / the ids the swings record / per athlete | **Recommend the ids the swings record** (swings are source facts and stay unrewritten; the Mac's records re-key to them — J6 re-keys `currentAthleteUuid` and the club-length prior). For you: pick one of your two uuids. Decided after the cabin PC's dry run (needs this item committed first) | — |
| J-59 | 2.8a | Settings on the Mac are an INI file, not a plist (the brief was wrong); the dry run reads a private copy and was proved read-only by hashing and diffing it | — | as done | — |
| J-60 | 2.8a | M3 drops the empty `intent`/`screens` keys from the 23 `diagnostics.json` files (no golfer input in any of them on the Mac); M2 writes no `session.json` for a folder no athlete record owns | — | as built (implementer's J4, J7) | — |
| J-61 | 2.8a review | **Found by the reviewer before any real run:** (a) M1 could silently ignore your id pick; (b) the archive-mirror write had no scratch check, backup or lock — a scratch run on an archived session would have written into the LIVE archive; (c) a symlink under scratch pointing at the live library passed the scratch check. Plus: an MSVC failure of the new tool would have broken the cabin PC's default build | — | **All fixed before commit**, each with a test; the tool gets its own build mode on the cabin PC. (The live dry runs themselves were re-verified read-only: 356 + 234 files re-hashed identical, the settings INI untouched) | — |
| J-62 | 2.8a | The migration tool writes `athlete.json` and `session.json` itself rather than through `AthleteRepository`/`SessionRepository` (a one-off migrator, R-REC-1's exception; it reuses the same file-name owners and atomic writes) | through the repositories / its own writes | **Its own writes**, as a one-off migrator that never runs alongside the app (it refuses a held lock). The cabin PC's MSVC build of the tool is unproven until `studio-build.cmd migrate` runs there after commit | route through the repositories |
| J-63 | 2.7b | Charts' prose (not just its kickers) also names shots by swing number, with the same "Shot 8 · 5 of 6" form after a gap | kickers only / everywhere in Charts | **Everywhere in Charts**, one rule (`charts::words::shotNumber`) | kickers only |
| J-64 | 2.7b | Outside the brief's list: the report's swing grid (`PpSwingGrid.qml`) highlighted the WRONG swing as the reference after a gap; fixed so that comparison goes by position (a logic fix, no look change) | leave / fix | **Fixed** (found on the review sheet) | revert |
| J-65 | 2.7b | On the delivery scatter, another session of the same day is named "session N" by its order in the day (start time), not its folder's `_NN` | — | as built (F-06) | — |
| J-66 | 2.7b review | The session report still tries a folder made from the athlete's NAME before the Athlete's own folder (older than 2.7b) | leave / fix in 2.8b | **Fix in 2.8b**: Athlete's folder only (E-02) | — |
| J-67 | 2.8b | **Found at the close:** the tools' InProgress refusal (your D11/DEC-158) had not been built in 2.2–2.8a; only the migration tool refused. J-36 had assumed the others did | build now / defer | **Built now in 2.8b** (approved scope, H-14): regrade_ledger, swinglab --write-back, lm_repair, pps_convert_library and the backfill tool refuse an InProgress session, `--allow-in-progress` overrides | — |
| J-68 | 2.8b | The last step-2 allowlist line, `athlete_controller.h A6 9` (too many slots/signals), isn't removed by step 2: the controller stays a full adapter until step 3 replaces it with the Athlete object | lower / re-tag | **Re-tag to step 3** in S1-g (a tag change, no new line) | — |
| J-69 | 2.8b | Two "deleted when done" entries stay: `SwingStore::save` public (test fixtures plant documents the door refuses) and `refreshActivity` (the QML picker calls it) | — | stay, each with its reason in NOTES | step 3 |

## The Product Owner's answers to step 2's judgement calls (15 Nov 2026, afternoon)
| # | Answer |
|---|---|
| J-58 | Each athlete keeps the id their swings record; the Product Owner's own record keeps its recorded id (168 swings). Confirmed against the cabin PC's dry run before the real run. |
| J-12 | Accepted: the v5 recipe is fixed for ever. |
| J-26 | Keep the Delete athlete confirmation. |
| J-55 | Prepare before/after sheets for changing `PpTypeset.shotNo` to the swing number; the Product Owner approves before anything is built. |
| J-1 | Step 2, as S1-g — the reference's "(proposed, for the Product Owner; J-1)" mark is removed. |
| J-2 | A club correction recomputes both clubs' usuals — mark removed. |
| J-3 | A re-analysis is in the usuals chain — mark removed. |
| J-5 | "I think the current approach is the best approach": the stamp records the norms in force when the findings fired (DEC-100 unchanged). |
| J-18 | **Changed:** a crash-left session ended by the next Start gets `endedAt` = its last swing's capture time, **marked as inferred** (the Product Owner chose the option that said so). This adds a key to session.json (e.g. `endInferred: true`) — approved by this answer as an IM change to FILE-SESS-01's keys; built in 2.8b with a test, and M2 uses the same rule for old sessions. |
| J-21 | Day + type, as built. |
| J-35 | Visible error for a folder not named swing_NNNN, as built. |
| J-39 | The owning athlete's id (with J-58 it equals the swings' id), swing-file id as the pre-M1 fallback — mark removed. |
| J-46 | Refuse archive/delete of a crash-left session until the athlete's next Start, as built. |
| J-48 | The setting's subtitle is enough; no warning built. |
| J-51 | Same keys; no state key. |
| J-62 | The migration tool's own writes accepted as a one-off exception. |
| J-54 | "shot 8 · 6 of 7" after a gap; "shot 3 of 6" otherwise, as built. |
| J-41 | Empty value = cleared, as built. |
| J-63 | Swing number everywhere in Charts, as built. |
| J-64 | Keep the PpSwingGrid reference fix. |
| Records | All 44 record rows accepted as they stand (J-4, 6, 7–11, 13–17, 19, 20, 22–25, 27–34, 36–38, 40, 42–45, 47, 49, 50, 52, 53, 56, 57, 59–61, 65). |

All 65 rows answered. Follow-ups: J-18's inferred end (2.8b); remove the "(proposed, for the Product Owner; J-n)" marks for J-1, J-2, J-3, J-39 in information_model.md (2.8b's docs); J-55 before/after sheets for `PpTypeset.shotNo` (design proposal, nothing built until the Product Owner approves).

### New decision (15 Nov 2026, afternoon)
**P11 approved, with the lint.** the Product Owner: "its not really about charts per se- its more the persistence around an object needs to be their concern- we can't have their persistency interfered with by gui and functional elements (or other objects really, although the relationship between sessioin / shot / finding / diagnostic is a bit more complex." Text: "An object's persistence is its own concern. Only the owning object, through its repository, reads or writes its files. Views, rules and other objects reach its facts through the object, never its files or its repository. The one sanctioned crossing: a shot's findings and derived data, which the Shot computes and its Session keeps (DEC-135)." Lint A5 widened to any file/directory access or repository include outside the repositories and adapters; today's hits allowlisted, each tagged with the step that removes it (the Product Owner's approval of exactly those lines). Built in S1-g beside A1.

**Step 3.0 approved** (the Product Owner, 15 Nov 2026): "yes, put it in the plan at the start of step 3- so long as we know the migration works and no data is lost". Text in build/refactor/step-3/PLAN-ADDITION-3.0.md; added to 04 after 2.8b commits.

### P11 lint (S1-g), the Product Owner 15 Nov 2026
1. The 36 non-domain lines stay allowlisted, tagged "not domain (§8.3)" — no directory exemption.
2. Only the stores (Export's repositories/adapters), Platform helpers, tools and tests are exempt — not every Adapters directory.
3. Placements accepted: PPCP ledger/imports/pairings, the studio profile and the trunk ceremony → platform refactor (capture) (05 worklist); `main.cpp` → kept (composition root); the JSON-era library converter → step 3.0; the analysis services' RecordingReader reads → step 3 (through the Shot); Ball/Pose pinned inputs and model fingerprints → step 7.

# Series 2 · Steps 3 to 7 and the close (J-1 to J-112, and the rulings of 24 Nov)

## Step 3A — judgement calls made unattended (for the Product Owner's review when 3A stops for the Product Owner to try the app)
The Product Owner, 15 Nov 2026 evening: option 1 — start 3A now, unattended, stop at the end of 3A. Library/corpus writes, allowlist
additions, design-system changes, freeze breaches and IM changes beyond build/refactor/step-3/ANSWERS.md are NOT made here.

| # | Item | Call | Options | Chosen, and why | Undo |
|---|---|---|---|---|---|
| J-1 | 3.1 | A result's address is text ids plus numbers (the pool is in Platform, which can't include `src/Identity`) | typed ids / text + numbers | **Text + numbers** (ring rule) | — |
| J-2 | 3.1 | D24's "low priority" means both run order and the OS thread priority | order only / both | **Both** — firing never competes with capture on the cabin PC; measured in the parallel run | order only |
| J-3 | 3.1 | Pool size max(2, physical cores / 2), not measured | — | as built; measured in 3.5's parallel run | — |
| J-4 | v1.9 review | ~25 "(proposed, for the Product Owner)" design details in the brief that no answer covered | stop 3A for the Product Owner / build the implementation-detail ones as logged calls | **Build them as logged calls** — the reviewer sorted all 25: none is an information-model change any 3A item relies on. Conditions kept: `RecordingSource` only wraps today's reader; SET-06/SET-15 stay where stored and old `user.norms.json` load. **For you at the 3A stop:** the §4.4 re-fire design (04 item 7 and DEC-141 reserve it for you; only 3.1's generic pool is built); a persisted "unread" mark in 3.5 would be a new `diagnostics.json` key | — |
| J-5 | commit | v1.9 and 3.1 edited the same lines of information_model.md (the R-CON-5 row, RING-PLATFORM) | hand-build a v1.9-only file (loses v1.9's own R-CON-5 wording) / commit the whole file with v1.9 | **Whole file with v1.9**; both commit bodies say 3.1's three status lines went in with v1.9 | — |
| J-6 | scope | Your 18:50 go ("no need to wait on me till the end of step 3") | stop at 3A as planned / run on | **Run on through 3B**; one review of every call at step 3's end. Still stops: 3.0's migration gate; the §4.4 re-fire design (yours by DEC-141) — prepared, built only after your answer; any unapproved IM change | stop at 3A |
| J-7 | 3.2 | The implementer's own calls J1–J11 (in `3.2/NOTES.md`): interim `RuleData::current()` accessors, SET-06 recorded not applied, the "decided, provisional" dashboard wording (5 rows), a newer user pack never rewritten, etc. | — | accepted subject to the review; the stamps' sha1s move with IM-S3-6's keys (no grade, row or word changes; each session regrades once at next open, to the same grades) | — |
| J-8 | 3.3 | §6 gives 3.3 "the stamp's inputs" but run A expects 0 differences | change the written stamp now / define and test the stamp's inputs, keep today's stamp on disk | **Keep today's stamp on disk**; the new stamp lands with 3.5's parallel run, where it is already listed (run P case 1) | write it in 3.3 |
| J-9 | 3.2 review 1 | **For you:** `main.cpp` gets RuleData's store through a factory header (`rule_data_install.h`) that lint A5 does not count as a repository include, so main.cpp's A5 count stays 8 | (a) count it: main.cpp 8 → 9, an allowlist change only you can make / (b) accept a factory header as the composition root's door, a lint rule for you / (c) leave it, recorded as a known hole | **(c) for now, built as is**; your pick at step 3's end. **Since 3.8-i a second case:** to archive, `main.cpp` takes the session store's archive job from the Archiving tab's controller rather than naming `session_archiver.h` — the honest form is the count 8 → 9. The reviewers recommend (a) for both | (a) or (b) |
| J-10 | 3.2 review 1 | The reviewer's other four changes: SET-06/15 get one door (AppSettings becomes a view over RuleData); RuleData installed in the two UI test binaries, with a log warning if read uninstalled; the corridor editor no longer copies "decided by the Product Owner" onto your nudged figure; the app's RuleData outlives the worker threads | — | **All made** (round 2). Behaviour change to note: the session report list's goals now follow a pack edit without a relaunch | — |
| J-11 | 3.4a | 3.4a is the largest 3A item (new Session/Shot/App objects plus re-pointing capture, the LM controller, the clip filer and AthleteController) | one run, one commit / two runs: **3.4a-i** the objects with their tests, no caller moved; **3.4a-ii** the callers | **Two runs, two commits** — the callers' move is where a golfer-visible slip could happen, so it gets its own review and probe | one run |
| J-12 | 3.3 | The implementer's calls J1–J… (in `3.3/NOTES.md`): moved helpers rather than copies; inputs still read on the worker as today (the live grade-policy read stays until 3.5, parity P case 6); the pack passed as a non-owning alias until 3.5 | — | accepted subject to the review | — |
| J-13 | 3.3 (N2) | C-05's other half — the session/athlete reductions' rule versions (`work_ons.h`, `tendencies.h`, `outcome_scope.h`, `club_distances.h`, `swing_themes.h`) are bumped by hand and unchecked, and the brief placed them nowhere (every finding must be scheduled) | 3.5 / **3.6** / step 7 | **3.6**, which takes those reductions over (`AthleteDiagnostics`); a golden like 3.3's | 3.5 |
| J-14 | 3.3 (N1) | **For you, before 3.5:** today a shot's outcome (its labels, its story, carry against the usual) is composed *after* the findings, from the session's diagnosis (links and tiers). The information model says a shot's derived data comes *before* its findings and never from the session's result | (a) amend the model: layer 3 has two parts; (b) move the outcome's "because" (the links) into the session's diagnosis so the outcome depends on the shot alone | **Kept today's behaviour, built nothing.** Not yours after all (your 15 Nov correction): your three-level principle — each level gives context back down — already allows a shot to read its session's links and tiers, so this is a design choice I make in 3.5 and log (leaning to (b)) | — |
| J-15 | 3.4a-i (N1) | **Wording, for you:** LC-SHOT and DEC-142 say `addShot` "creates the folder through `RecordingRepository::create`"; the code (since step 2) makes the folder when the swing number is allocated and writes the document through `create` | change the code to match / keep the code and correct the wording | **Kept the code; the wording correction is proposed, not made** — it is your decision's text | change the code |
| J-16 | 3.4a-i (N2) | Who holds a loaded Session — **answered by you, 15 Nov evening**: the Athlete keeps them (as built), page amended as DEC-190; releasing unused sessions is 3.5's (Q-18, T1) | — | applied | — |
| J-17 | 3.4a-i review 1 | `session_test`, `session_repository_test` and `swing_paths_test` put throwaway folders in your real Trash on every run | leave / a test seam | **Seam:** they now trash into the temporary library's own `.pinpoint-trash`; the app is unchanged | — |
| J-18 | 3.4b | 3.4b is as large as 3.4a (nine Shot commands + every old writer forwarding, then the carousel's rows and the analysis readers) | one run / two | **Two runs, two commits:** 3.4b-i the commands and their forwarders; 3.4b-ii the carousel rows, `applyAnalysis` and the readers through `RecordingSource` | one run |
| J-19 | 3.4b | §4.12's "the old model's six golfer commands forward and write nothing" was "(proposed, for the Product Owner)" | — | **Built** under J-4 and your shot-owns-shot-facts principle (one writer per fact) | — |
| J-20 | 3.4a-ii | The Product Owner's N1/N2 answers (ANSWERS.md): a no-golfer session keeps today's path (a warning in 3.7); E-12's QML half → step 4 | — | applied — **these two should have been logged calls, not an overnight blocking question (my error, ~8 h lost)** | — |
| J-21 | 3.4b-i | A rating no longer rewrites the note and club (today it could save the fallback club or a "DRIVER" stub as your declared club); the tuning line is written only for a correction that saved | — | **bug fixes, built** (reviewer: nothing you typed is lost) | — |
| J-22 | 3.4b-i | Wording lags settled by today's behaviour: EVT-10's row names focus and miss but a screen answer emits it too; OBJ-TUNING-LOG doesn't name the "flag for review" lines | edit now / at step 3's end | **Listed for the 3.10 docs pass**, citing the existing behaviour | — |
| J-23 | 3.5 | 3.5 is the largest item (the owner, the re-fire with its four race tests, the reduction, the typed summary and its growth, the parallel run) | one run / two | **Two runs, two commits:** 3.5-i the owner + re-fire (shadow only, tests); 3.5-ii the diagnosis, typed summary (DEC-192) and run P | one run |
| J-24 | FIELD-LIST | Missing carry shows an em dash (rule: "-"); "session ended" and start/end times have two writers; older `work_ons.json` read by overriding its version; D7 window keeps total/offline; unread keys | — | **3.5-ii:** "-" fix; the Session the one writer (the diagnostics copy written from it, no key change); tolerant read kept; total/offline kept; unread keys kept and listed | — |
| J-25 | 3.4b-ii | A failed carousel save on a held shot now shows the shot's real (saved) value, not the unsaved one HEAD showed as if saved; still logged only until 3.7's notice | keep HEAD's / as built | **As built** (reviewer: HEAD's was the misleading one); the on-screen warning is 3.7's | HEAD's |
| J-26 | 3.4b-ii review 1 | The A5 lines of `shot_list_model.cpp` and `session_review_controller.cpp` are tagged "# step 3" but those files still parse documents themselves (and serve the Product Owner's N1 no-golfer path) | force into 3.4b / **re-tag to step 4** (the views stop reading documents when they reach shots through `app.session`) | **Step 4**, re-tagged in 3.10 (a re-tag, no line added) | 3.4b |
| J-27 | 3.4b-ii review 1 | **For you (no action needed):** a camera shot whose video export fails gets a device-only document, but the Shot's kind was fixed as captured. Your rule says a shot's kind never changes (CLS-SHOT); the document's own classification (R-REC-8) says device-only | — | **Today's behaviour kept:** re-analysis decides from the document, as HEAD did | — |
| J-28 | 3.5-i (J-14) | Where a shot outcome's "because" lives | (a) a second layer-3 pass after the diagnosis / (b) the session's reasons | **(b)**: the session's reasons carry it; the shot's outcome depends on the shot and its usuals; screens join them (built in 3.5-ii). Settled by your three-level principle | (a) |
| J-29 | 3.5-i (F2) | The norm cohort (D6/DEC-171, approved) will change findings for athletes with a sex or birth date on record — today firing passed no cohort | — | **Built as approved**; added to run P's advance list as case 8, so every such difference is expected and named before the run | — |
| J-30 | 3.5-ii review 1 | Two Home/athlete screens still show "—" for a missing value (your rule: "-") | carry to 3.7 / fix now | **Fixed now** — a literal text change in two QML files, no component, style or token change (design lint must pass) | 3.7 |
| J-31 | 3.5-ii review 1 | With the comparison switch on, the new owner also wrote its cache files beside the old model's | note it / stop it | **Stopped:** in comparison mode the new owner writes nothing, so your live check has one writer, as §4.9 says | note only |
| J-32 | 3.6 (F1) | Tendencies, themes, consistency and intent consistency still need per-shot facts the grown summary lacks | ask you / grow it further | **Grow it further** — your "(a), let the summary grow" was so the athlete level reads summaries alone; DEC-192 amended to list the extra fields, older files still load | — |
| J-33 | 3.5-ii / D22 | The old model words a missing measure "pelvisrotation not measured", the new owner "pelvis rotation not measured" (the new owner has the metric catalogue) — hundreds of comparison lines per session on LM-only shots | rebuild the installer first / filter | **Filter it as one known wording difference** in your D22 log (the cabin brief says so); the new wording is the right one and is what you'll see after 3.7 | rebuild |
| J-34 | 3.6 (F7) | The consistency grader lives in the screen code (`src/Gui/variability`), so the athlete owner can't use it | 3.8 / step 7 | **3.8** (the leftovers item), moved to the Rules ring as text surgery, parity-checked | step 7 |
| J-35 | order | 3.7's cut-overs wait for your live check (D22); 3.8 was planned after 3.7 | idle until D22 / **start 3.8's independent parts now** | **3.8-i now** (housekeeping, the library size, the consistency grader's move, the dead wrist model — none of 3.7's files); **3.8-ii after 3.7** (fault_profile, the wrist assessment into findings) | wait |
| J-36 | 3.8-i review 1 | **For you, at step 3's end (no change now):** today's housekeeping scan can reach `corpus/` and `scratch/` inside the Mac's library folder; it's harmless only because the storage limits are unset on the Mac | — | **Left as today** (your N1 "keep today's path"); listed so you can decide whether housekeeping should skip non-athlete folders | — |
| J-37 | order | While 3.7 waits for your live check: 3.9 (the pack and corridor editor) touches none of 3.7's files | wait / **start 3.9 now** | **3.9-i now** (reads and writes through RuleData), **3.9-ii next** (the QML API typed, proven by pixel-identical before/after renders); only its "an edit re-fires every loaded session" test waits for 3.7 | wait |
| J-38 | 3.9-i reviews 1–2 | The pack/corridor editor's undo after someone else's save: in theory an undo then save could revert the other save; the fix tried in round 2 lost drafts instead | fix (loses drafts) / **keep today's undo** | **Today's undo kept** — the case can't happen in the app (one editor, no in-app reload); the editor only adopts a newer snapshot while clean, comparing content. Likewise unreachable: a draft undone back to clean keeps showing its older saved layers until the next publish | fix |

### J-39 — D22 failed on a fault in the parallel run, not in the new engine (20 Nov)
- **What:** all five live sessions disagreed (10.9k–19.8k differences each). Cause: the parallel run's reader was built with `writeSidecars=false` (main.cpp:384, 3.5-ii round 2), and that flag also stops the document being parsed, so during a live session every shot was fired with no measures, no LM data and no club. It was reproduced exactly offline: same counts, same first lines (3.7/D22-DIFF-NOTES.md). The stored-session parity passed because sidecars already exist there. Hidden behind it, about 550 club-baseline differences: the new owner never reloads its baseline (nothing calls `reloadClubBaseline`).
- **Options:** (a) set `writeSidecars=true` (one line, but it brings back two writers of one cache, which review 1 rejected); (b) split the flag into "may parse" and "may write", and have the parallel hook reload the baseline before comparing; (c) skip D22 and cut over on offline parity.
- **Chosen:** (b), as item 3.7-0 with its own review and a live-shaped ctest that fails on the old wiring. Then D22 is re-run on the cabin PC with a new installer, because the brief requires a clean live check before any cut-over.
- **Why:** (a) breaks the one-writer rule; (c) throws away the only live check, and this one has just found two real gaps.
- **Undo:** revert the 3.7-0 commit.

### J-40: 3.7-0's review leftovers go on the carry list, not into another round (20 Nov)
- **What:** review 2 passed 3.7-0 and left two items for later.
  - A crash-left session's stage, cold vs closing: harmless until the stage panel's cut-over (3.7-a).
  - Optional: matching "not compared" by folder path.
- **Chosen:** both go on CARRY: 3.7-a must show Closing (with a test); 3.7-e matches by session id. Commit 3.7-0 now.
- **Why:** neither can affect the coming live run's verdict. The first only reaches the log. The second is checked by hand against the sessions ended. A third round would delay the Product Owner's run for no gain.
- **Undo:** do either item now as a small reviewed fix.

### J-41: the stall after each shot is mostly older than the refactor; the run gets log lines, not fixes (20 Nov)
- **What:** the 40-shot bench on the cabin PC (3.7/NOTES-S.md) puts the screen thread's C++ work after a shot at 0.5 s (shot 1) to 1.7 s (shot 40). That is two full rewrites of the shot's file, three re-reads, and the old model's rebuild and save, called in the same order as before the refactor. The refactor's added cost was ~85 ms, mostly the parallel comparison, which grew; bebea523 removed it while a session is live. The rest of the live stall, around 1.6 s that grows per shot, is screen-side work the bench can't see.
- **Chosen:**
  - Build no stall fix now. 3.7-I adds `[PostShot]` per-step timings and a `[GpuMem]` line, so one live run splits the time by step.
  - Placement: the timer header is in the Platform ring (a logging utility, not an information-model change).
  - The 3.7 cut-overs retire the old model's settle on the screen thread, which removes the rebuild and save cost.
  - The other fixes go to the Product Owner, below.
- **Options for the Product Owner (not built):**
  - Move the two rewrites onto the session's worker lane. This changes the Shot command's contract: it would finish later.
  - Refresh the carousel row from the summary, and drop its duplicate refresh. This is a QML change.
  - A stable model for the ledger's Repeaters, if the run confirms they are the cost.
- **Why:** the run decides which fix pays. Building a fix now would cost a second hour if it were the wrong one.
- **Undo:** remove the log lines.

### J-42: D22 is cleared on the 21 Nov run, with three explained differences; 3.7-a starts (21 Nov)
- **What:** the 21 Nov combined run left 0–3 differences per comparison, down from 10,941–19,800 on 20 Nov. Three kinds are outside the README's expected list. All three were reproduced offline from scratch copies of Wrist_02..07, and every final comparison matches the live log (3.7/NOTES-D22b.md).
  1. `reasons[…].cost → null` is G-08, the rule 3.5-ii built on purpose: cost only against an earned usual. GAP WEDGE n=9 and 7 IRON n=8 fall short of 10. 3.5-ii listed it as case 4a; the 3.7 README left it out.
  2. A just-ended live session's End line shows `stage established → closing`. The old panel model lags: `SessionDiagnosticsModel::closeSession()` has no caller in the app. The new owner is right (case 9 / J-24).
  3. Two lines per End is expected. Two old models hold the session (the panel's and the catch-up's), and `writeLine` remembers one line per folder, not per model.
- **Chosen:** D22 cleared by the brief's "or explained" clause. 3.7-a starts. No product fix: the cut-overs retire the old models that cause items 2 and 3.
- **For 3.7-a's before/after sheet:**
  - a reason's carry cost shows "-" until the club's usual is earned (today, every reason on the cabin PC), and the order of reasons can change;
  - the panel shows Closing right after End;
  - the newest shot is graded with its Premier reading, because the new owner regrades when the reading is written.
- **Not done:** the harness's latent wrong-session wait (`diag_parallel_run.cpp:570`). It matters only if D22 is run again; it is on the carry list.
- **Undo:** stop 3.7 and re-run D22 after harness fixes.

### J-43 (the Product Owner's decision, 21 Nov): ViTPose inference gets one long-lived thread (the GPU arena growth)
- **What:** the cached ViTPose session's CUDA arena grows by 1 GiB about every 4 shots, because `Run()` is called from a fresh `std::thread` each shot (`analysis_dag.h:384-388`). Reproduced headless on GOLFSIMPC (3.7/NOTES-L.md); an independent Fable read agrees (NOTES-L-fable.md). It predates the refactor: both causes landed on 6 Oct (`134eac67`, `8f91bbdd`).
- **The Product Owner:** "I have absolutely no problem with a long running thread to manage pose inference, in some ways it is the more elegant solution" … "go ahead, build it once the bench is in".
- **Chosen:**
  - One long-lived thread owned by PoseRunner, the only caller of the cached ViTPose session(s). Pose is already serial: DtlPose reads kPose2d, so nothing is made to take turns.
  - It is stopped before the sessions and the ORT env are torn down at exit. Tools get it too.
  - This amends R-CON-5 for this case (C11, an IM change approved by the Product Owner), to be registered as a DEC with the fix.
  - It is the first instance of the platform assessment's proposed long-lived-thread helper, private to PoseRunner for now.
- **Undo:** revert the fix commit.

### J-44 (the Product Owner, 21 Nov): the leak fix runs separately and never blocks the plan; the remaining cut-overs overlap
- **The Product Owner:** "I'm keen that the leak fix should not block the refactor plan … can we treat that as a separate non-blocking task and keep the plan moving. we have been in step 3 for many days and need to crack on."
- **Chosen:**
  - The GPU fix (`7b2445e8`) and its confirmation run (scratch/step3/confirm-run) sit outside step 3. A failed run becomes its own task.
  - The cut-overs are pipelined, per the "Pipeline issue fixes" rule. 3.7-n+1's implementer starts when 3.7-n is committed (3.7-c builds on 3.7-a's pieces), or as soon as 3.7-n goes into review when the two share no files. Each works in its own worktree under build/. Every piece is still reviewed before its commit.
  - 3.7-b is folded into 3.7-a.
  - The stall fixes stay as J-41's options for step 3's end.
- **Undo:** go back to one cut-over at a time.

### J-45 (the Product Owner, 21 Nov): the on-screen Charts tab follows the launch monitor board's annotations, like the report
- **What:** 3.7-d makes the report's pages honour the board (a re-labelled shot counts under the board's club; an excluded reading is left out). The on-screen Charts tab (`SessionSeriesModel`) does not, so the same session's charts disagree between screen and print.
- **The Product Owner:** "Yes- we need to be consistent".
- **Chosen:** the same `boardAnnotated` rule goes into `SessionSeriesModel`, in 3.7-d's fix round, so the two never disagree in a commit. It is a visible change to the Charts tab, so it goes on 3.7-d's before/after sheet.
- **Undo:** revert that part of 3.7-d.

### J-46 (the Product Owner, 21 Nov): drop the carousel's duplicate row refresh after a late launch monitor reading
- **What:** J-41's option "drop its duplicate refresh". After a late reading the carousel refreshes the row a second time, which cost 515–763 ms a shot on the GUI thread in the 21 Nov run (SUMMARY §5.2, `lm.rewritten`). Since 3.7-c, removing it is a one-line change (NOTES-c "For the Product Owner").
- **The Product Owner:** "100%".
- **Chosen:** done in 3.7-c's fix round. A test or bench must show the row still shows the late reading.
- **Undo:** restore the refresh.

### J-47 (me, 21 Nov): 3.7-e's summary fingerprint goes back to covering every rule and input R-STAMP-1 names
- **What:** REVIEW-e R1-B2. 3.7-e's one shared fingerprint (`summaryFingerprintOf`) dropped `kSessionReductionVersion`, the outcome and derived rule versions, the flight norms, the grade policy and the cohort, all of which 3.6's owner fingerprint covered. J-e1 said this "matches FIELD-LIST", but FIELD-LIST §1.8 only describes today's hash.
- **Chosen:** R-STAMP-1/2 already settle it (approved rules), so it isn't a new information-model decision. The one rule covers every version and input both sides can compute, takes a rules struct so a test can move each input on its own, and `work_ons_design.md:40` and FIELD-LIST §1.8 are corrected to match. Done in 3.7-e's fix round.
- **Undo:** none wanted; this restores the approved rule.

### J-48 (me, 21 Nov): Home's tendencies, themes and work-ons and the report's Tendencies page move to AthleteDiagnostics as their own item, 3.7-f, straight after 3.7-e
- **What:** REVIEW-e R1-B3 / NOTES-e J-e5. The step-3 brief (§4.6, §6) and 3.6's hand-over give 3.7 the Home read from `AthleteDiagnostics` (tendencies, themes, athlete work-ons), and CARRY 3.7-d M5 gives it the report's Tendencies page. 3.7-e didn't build it, and nothing later took it.
- **Chosen:** it is its own item, 3.7-f, so each agent run does one task. It is pipelined straight after 3.7-e commits, in the same worktree and before 3.8-ii. It also takes REVIEW-e R1-m8 (J-a3 records dropping out of Home without a word: show with a warning) and, if 3.7-e's round doesn't close it, R1-m4 (Charts and Consistency not refreshing with no panel up). 3.7-e re-points its header comment and IM §11.2 row at 3.7-f.
- **Undo:** fold it back into 3.7-e.

### J-49 (me, 21 Nov): the live club baseline must be there at Start, and say so when it isn't
- **What:** REVIEW-e R1-B1. 3.7-e deleted the adapter's live freeze (J-a2), and the owner's freeze from summaries isn't computable until the newest predecessors are summed up. The catch-up sums those up last (oldest first) and pauses while a session is live. So after this build, and after every work-on rule bump, a live session can run with no usual numbers, with nothing to tell the golfer, and its findings change at End.
- **Chosen:** the reviewer's (a) and (b) together. The catch-up sums up first the sessions a baseline depends on, then the rest oldest first. At Start, if the baseline still can't be computed, it sums up just those predecessors before the live pause. If it still can't, an existing notice component says so (new words in an existing component; no design-system change). A test covers old-form predecessors rewritten and the freeze at Start, with the panel up and off.
- **Undo:** restore the adapter's live freeze until the summaries are the new form.

### J-50 (me, 21 Nov): 3.7-f's scoped records stay in the adapter for now; how they move is the Product Owner's call (an information-model change)
- **What:** DUTIES-f D1. The themes and work-ons under a club scope or shot filter (`scopedSessionRecord`, which feeds the fault details and Your swing areas) need the full judged rows. DEC-192 "b" keeps those out of the summary, so BRIEF-f deliverable 3 can't be built from the summaries. Moving them needs one of two things, each an information-model change (§1.2): (a) the athlete level reads `diagnostics.json` (C6), or (b) the summary grows (C3, amending "b").
- **Chosen:** option (c): no change now. The scoped job stays in WorkOnsController, reading ledgers as today, with a carry line. The Product Owner picks (a) or (b) at step 3's end; the build goes in the first step after his answer.
- **For the Product Owner:** my recommendation is (a), the athlete level reading the session's own `diagnostics.json` through the Session. The summary stays small, and the scoped view is drill-down detail, which is what the per-shot file is for.
- **Undo:** n/a (today's behaviour kept).
- **The Product Owner, 21 Nov: "Option a please".** This is DEC-195 (C6). The athlete level gets the scoped views' judged rows from each Session's own `SessionDiagnostics`, which is the only reader of `diagnostics.json` (P11). `AthleteDiagnostics` never opens the file itself. It is built in 3.7-f now, since deliverable 3 was always 3.7-f's.

### J-51 (me, 21 Nov): sessions with no current summary are counted on Home with a warning, not read from their ledgers
- **What:** DUTIES-f D2 and D5. Reading summaries only (approved in 3.6, DEC-192) means a session whose summary is stale, blocked or a cabin session (J-a3) drops out of the tendencies, themes, marks and the report's "Earlier today". Today they read its ledger.
- **Chosen:** no information-model change. Such sessions are counted and shown with a warning in an existing component (HARD: show with a warning, never hide). The catch-up still rewrites the stale ones. "Your distances"' stale check (`athlete_controller.cpp:713-719`) is re-pointed at the summaries, so that duty isn't lost. Reading their ledgers instead would be C6, for the Product Owner.
- **Undo:** read the ledgers (needs the Product Owner).

### J-52 (me, 21 Nov): the three marks helpers move out of the Gui ring as text surgery, like J-34
- **What:** DUTIES-f D3. `measureScaleType`, `isOutcomeMeasure` and `normCorridorHalfWidth` (`src/Gui/variability/variability_measures.h`) are pure rules the marks need.
- **Chosen:** move them to the Metrics or Rules ring unchanged and parity-checked, as J-34 did for the grader in 3.8-i. Callers are unchanged.
- **Undo:** move them back.

### J-53 (me, 21 Nov): the athlete-level jobs run on the one pool in the athlete owner's own lane
- **What:** DUTIES-f D4. §11's R-CON-5 status reads "a serial lane per owner"; the pool has session lanes only.
- **Chosen:** the athlete owner gets its own lane (one owner, one lane). This is within R-CON-5's wording, so no information-model change. The pool is narrower than today's all-cores themes reduction, so the bench measures Home's recompute time before and after; a slowdown is reported, not hidden.
- **Undo:** a dedicated reduction outside the pool (would need the Product Owner: R-CON-5).

### J-54 (me, 21 Nov): keep 3.7-f's AthleteLevel interface
- **What:** NOTES-f J-f1. `AthleteLevel` is a stateless pure interface that only `AthleteDiagnostics` implements. `WorkOnsController`, the report model and `AthleteController` use it, through doors set in `main.cpp`. Those three files are QML-module C++ that the build deliberately keeps free of the domain owners. The alternative was about 20 CMake lines adding the domain sources to six targets.
- **Chosen:** keep it. The adapters depend on an interface of the owner, not on the owner's sources, which is the rings rule (dependencies point inward). It isn't a new owner or new state, so it isn't an information-model change (C1).
- **Undo:** link the domain sources into the six targets and drop the interface.

### J-55 (for the Product Owner, 21 Nov): Home's swing summary takes about 1 s longer on the one pool
- **What:** NOTES-f round 0b. On the corpus, the athlete level's themes reduction takes 3930 ms against 2873 ms before (+37%), off the GUI thread. The athlete lane already fans its work over all 5 of the pool's workers; the old reduction used 10 threads of its own. Peak memory with the scoped views is +88 MB at most (the tool re-fires every shot; the app doesn't).
- **Chosen for now:** keep it on the one pool (R-CON-5), as built.
- **For the Product Owner:** accept it, or widen the one pool. Widening means more workers in the same pool, and it would also compete with live analysis on the cabin PC. My recommendation is to accept it, and look at the pool's width once with live-run numbers from the cabin PC, where it matters.
- **The Product Owner, 21 Nov:** "Accept it for now and log an issue". Accepted; filed as #196 (priority: low).
- **Undo:** widen the pool (the Product Owner).

### J-56 (me, 21 Nov): the PINPOINT_WORKONS_DIR dev folder's Athlete takes the folder's real athlete id, never one made from its name
- **What:** REVIEW-f R1-m-dev. 3.7-f's dev and probe seam gives the folder an Athlete whose AthleteId is made up from the folder name. That's a second way of identifying an athlete (C5), even if only in dev.
- **Chosen:** use the id in the folder's athlete record. With no record, don't create an Athlete: Home shows its normal empty state, and the app log says why. Dev and probe only; the golfer never sees this path.
- **Undo:** the made-up id (would need the Product Owner: C5).

### J-57 (me, 21 Nov): Home's summary re-read moves off the GUI thread
- **What:** REVIEW-f R1-m9a. Home's full re-read of the session summaries, at launch and at every athlete switch, takes 61 ms for 18 sessions on the GUI thread, over DUTIES-f's 50 ms gate. The Product Owner's 24-session library would be about 80 ms.
- **Chosen:** read on the athlete owner's lane and apply on the GUI thread (R-CON-5's idiom). Home fills in a moment later, consistent with J-55, which the Product Owner accepted. A third build in 3.7-f's fix round.
- **Also:** REVIEW-f R1-B1's freshness check is not C6. The session level already reads its predecessors' summaries for the baseline (approved in 3.6); checking their fingerprint reads the same data.
- **Undo:** read on the GUI thread.

### J-58 (me, 21 Nov): 3.8-ii's decisions from its duties inventory (DUTIES-ii)
- **IM-ii-1, cleared as settled:** each shot's findings gain a `wrist` block in diagnostics.json (schema 15 → 16). DEC-123 makes the wrist result "part of the shot's findings (OBJ-SHOT-RESULTS) through the one diagnostic model", and DEC-135 puts each shot's derived data in diagnostics.json. Together they name both the place and the owner, so this is the build of an approved decision, not a new one (feedback: apply settled principles). Recorded in the IM as built under DEC-123/DEC-135, with the schema bump.
- **W3 and W4, changed: show, never hide.** A session whose diagnostics file can't be written, and a picked shot whose findings haven't landed, show words in the cells ("not graded yet" / the reason), never empty cells.
- **J-ii3, the ★ reference ghost:** keep today's behaviour (its own swing's read, Δ only, no grading), as the agent recommended. For the Product Owner at step 3's end.
- **DS-1, the Cold "Usually yours" block** (never draws anything): retiring it is a design-system change, so it stays as it is. For the Product Owner at step 3's end: retire it?
- **R1-m6:** fixed in Export with no `FiringReader` change. A sidecar is written only under the session's lock, and only when there's no archive marker.
- **Undo:** each item separately.

### J-59 (me, 22 Nov): 3.10's decisions from its inventory (3.10/INVENTORY.md)
- **The old engine stays to step 5, re-placed from step 3's "Deleted when done".** That covers `detectShot`'s body, the model's own stores, three of its pools and the three baseline authorities. Three things still run it:
  - J-a3: the cabin's sessions on the Mac, until M1;
  - `regrade_ledger`'s default;
  - about 8,900 lines of tests.

  Deleting it now would leave cabin sessions on the Mac with no diagnostics, and re-pointing the tests is far over budget. Its deletion goes after M1, with the step-5 adapter work. For the Product Owner at step 3's end.
- **`regrade_ledger` defaults to `--engine new`.** `--engine old` stays while the old engine does. The parity harness's diagnostics half moves to the new engine, with a fresh baseline from one scratch run ("step3.10-baseline"). Why: parity and the corpus sweep must check the engine the app shows (R-HL-2: tools go through the same owners).
- **The allowlist: re-tags only.** Every count already equals what the code needs. The 15 A5 lines tagged `# step 3` are re-tagged as the inventory proposes: `# step 4`, `# step 5`, `# kept (N1)`, or "not domain", each with its reason. No line is added and none is removed without its code going. For the Product Owner to check the tags at step 3's end.
- **J-22 is an information-model change** (EVT-10's trigger and the tuning log's contents), not a wording lag. Today's behaviour is kept. For the Product Owner.
- **The cabin re-fire is not run.** A dry run needs a Windows build of `regrade_ledger` (outside the budget), and a write-back would rewrite `work_ons.json` with no backup. The app re-judges every stale session on load anyway, so what the re-fire would prove arrives the first time the Product Owner opens the new build at the cabin. It moves to the Product Owner's "try the app" list.
- **The Q-18 memory run, "step3.10-mem",** runs on scratch copies, with no build if the test build already has what it needs. It gives parity P the measured figure the brief promised.
- **Wording:** d1–d3, REVIEW-ii m2's two comments, and the stale comment at `PpSessionDiagnosticsPanel.qml:370` (a comment only) are fixed. d4 (the word "waiting" for a newer build's file) is behaviour, so it is carried with m5.
- **Amended after 3.10's review (B1):** flipping `regrade_ledger`'s default to `--engine new` was wrong. Without `--athletes-ini`, the tool names the athlete after the parent folder, so the documented corpus sweep would pool every golfer into one athlete, and freeze each session's `club_baseline.json` from other golfers' shots. A frozen baseline is never rewritten. The reviewer reproduced this on scratch copies. So the default stays `--engine old`, as it was before 3.10, and `--engine new` without `--athletes-ini` refuses to run, with a message saying why. The parity harness keeps `--engine new` and passes its ini. The baseline run is checked for an ini, and retaken if it had none.

### J-60 (me, 22 Nov): step 4-a's decisions from its inventory (step-4/DUTIES-a.md)
- **E-12's QML half goes through `AppState`.** It gets `app.state.startSession(extend)` and `endSession()`, which keep today's call order in C++ through the capture service. QML does not call the Athlete directly: that would leave capture not knowing its target (R-APP-2).
- **`src/App` takes callables or a `QObject*` from main.cpp** (the DEC-167 pattern), never a Gui include.
- **`SessionMode.mode`'s 69 readers are re-pointed to `app.state.mode`,** and no alias is kept unless a reader truly can't move.
- **The replay's 141 QML lines are re-pointed mechanically.**
- **Only the athlete controller's A6 members are typed.**
- **The switch guard keeps today's "a session is running" check.** R-APP-1 says "capture runs". For the Product Owner.
- **Q-07 (the cast follows the selection today) and the cast-path replay keep today's behaviour.** For the Product Owner, with step 4's question of which shot the cast shows.

### J-61 (me, 22 Nov): the migration rehearsal's two open points (step-2/MIGRATION-READY.md)
- **`regrade_ledger --engine new` (and `parity_baseline.py`) read athletes from the INI,** which M1 empties. After the migration they must read the athletes from the `athlete.json` files, the records' one owner since step 2.3 (FILE-ATH-01, P11). The INI stays as a fallback for a library that hasn't been migrated. Principle-settled; this is a tools-only fix, queued as item 4-r after 4-a, in the same worktree.
- **Home's remembered tab and filters on the Mac are keyed by the old Mac ids,** so they reset once after the migration. Accepted: the reset is one time, and widening the migration's re-keying would mean another tool change and another rehearsal. For the Product Owner, as a note.

### DEC (the Product Owner, 22 Nov): the carousel becomes a view over the information model, in step 5
The Product Owner: "the carousel should be a viz on top of the information model ideally"; asked whether to add it to step 5 with a read-only Session for sessions that have no golfer, he said "yes lets add it to step 5". So:
- `ShotListModel` stops building and reloading its own list. It shows `app.session`'s shots and follows the Session's events (shot added, changed, trashed).
- N1 changes: a session with no golfer (the `unknown` folder) gets a lightweight read-only Session, so every session on screen has one.
- This is an information-model change, approved here. The IM is updated in step 5 before it is built.

### J-62 (me, 22 Nov): 4-b's look, approved by the Product Owner with one condition
The Product Owner: "carousel update looks good, so long as we see it when the carousel is minimised too."
- It goes in the band, and carry comes from the Shot's launch-monitor reading.
- "-" shows for both "not read" and "not judged yet": the sheet showed it, and the Product Owner said "looks good".
- Q3 is NOT taken as answered (advisor, 22 Nov). The Product Owner's request said "when a shot has just been selected or analysis has been completed for it", and the sheet never showed auto-replay off. Re-asked in chat. Until he answers, the summary follows the selected shot.
- His new condition: the summary also shows when the carousel is minimised. If that needs anything beyond adding the summary to the minimised strip, it comes back to him before it is built.

### DEC (the Product Owner, 22 Nov): 4-b's Q3, and sessions with no golfer
- **4-b, auto-replay off:** "with auto replay off yeah we want to see the outcome." The summary shows the selected shot. When a new shot's analysis lands and the selection doesn't move, it shows that shot, named by its swing number, until the golfer selects a shot or the next shot lands.
- **Sessions with no golfer:** the Product Owner chose **(b)**: "I am ambivalent, since I don't know how that happens but b seems like the better option to me."
  - **How it happens:** capture with no current athlete, for example on a fresh install, or after the current athlete is deleted. None exist in the library today.
  - **Step 5:** Start and capture refuse until a golfer is chosen, with words that say so (show with a warning, never hide).
  - **A no-golfer folder found on disk:** it is listed read-only with a warning, and can be moved to a golfer.
  - **The N1 special paths go** (the five `# kept N1` A5 lines). This replaces this morning's read-only Session for those sessions, so the carousel's view needs a Session only for a golfer's sessions.
  - This changes N1 (the Product Owner's 3.4a-ii answer) and is approved here. The information model is updated in step 5, before anything is built.

### J-63 (me, 22 Nov): 4-b's "newest outcome" rule is derived, not stored
The 4-b agent found that the rule as I first phrased it ("until the golfer selects a shot or the next shot lands") needs a new AppState member, an information-model change (§1.2 C1). The Product Owner's words were only "with auto replay off yeah we want to see the outcome". So the rule is derived from existing state:
- the summary shows the selected shot;
- when nothing is selected, which is the case during capture with auto-replay off, it shows the newest shot of the session on screen whose diagnostics have landed.

**Edge case, for the Product Owner:** a golfer who has clicked an older shot sees that shot's outcome until they clear the selection. If he wants the newest shot to win over a selection, that needs `app.state.outcome`, an IM change for his approval.

**Also accepted:**
- the band shows with nothing selected, with only the summary and no actions (on the sheet for the Product Owner);
- carry lands with the factors, from the diagnostics' copy of the launch-monitor reading, not a beat before.
- **Superseded (the Product Owner, 22 Nov, after J-63):** "yes- that change makes sense please go ahead". I read this as approval of `app.state.outcome`, an information-model change: R-APP-7 and the §6 tree. So a newly landed shot's outcome shows even over an older selection, named by its swing number, until the next pick or the next shot. The derived rule above is dropped.

### DEC (the Product Owner, 22 Nov, later): no golfer → "Unassigned"; the local copies folded into the plan
- The Product Owner: "in the event there is no golfer selected then unassigned is fine". This replaces his earlier (b). Capture with no golfer goes to an Unassigned owner, and its sessions are full Sessions. It is an information-model change, approved; the reference is updated in step 5 before it is built.
- The Product Owner: "please do fold those into our plan". All 14 components from LOCAL-COPIES.md are now in 04's step 5 (`3dce352a`), and the survey is re-run at step 5's close.

### J-64 (me, 22 Nov): 4-d's two rulings
- **`Session::readSessionIntent()` is not an information-model change.** It is a read of data the Session already owns and persists, with no new object, owner, field, file or identity (§1.2's list). It is how "through `app.session`" (information_model.md:536) is reached in C++. Accepted.
- **`sessionController.focusConditionId` stays only as the wizard's draft pick, until Start hands it to the Session and clears it.** That is input taken before its owner exists, like any form field. The Session is the one store from then on. Accepted.
- **Carried to step 5:** a live session with no golfer can show an open assessment until a panel is pointed again. The Unassigned owner (step 5) gives those sessions a Session, which ends it.

### DEC (the Product Owner, 22 Nov): step 5's three questions
1. **Yes:** a golfer file in the corpus (a corpus write, approved), so the corpus sweep runs the new engine.
2. **(a):** "Unassigned" appears in the athlete picker only when it holds sessions.
3. **Yes:** split report_meta into source facts (the coach's note, the send log) and derived facts (the fingerprint, the PDF name).

### J-65 (me, 22 Nov): 4-e's newer-file guard, widened and finished
4-e's review blocked on two gaps, both seen in a headless probe:
- the club-scoped theme files were checked only at the top level;
- `diagnostics.json` compared only its envelope version, not `ledger.schemaVersion`.

Also, 4-e's queue reloaded a newer build's sessions on every launch and rewrote their `diagnostics.json`.

Decided: one more round fixes all three, and a newer build's session is no longer queued. R-PERS-3 becomes "a file with a newer schema OR newer rule version is read-only to this build". That widening rests on the Product Owner's "ok keep it" (22 Nov) and is recorded in the information model with that citation. It is the last round for 4-e; anything still open is carried.

### J-66 (me, 22 Nov): 5.0's open details, settled from the Product Owner's decisions and settled principles
- **Unassigned:** a fixed well-known uuid, and a folder name `sanitise()` can never produce (`unknown` could collide with a golfer whose name has no ASCII letters). Its `athlete.json` holds only its id and name.
- **No norm cohort and no club baselines for Unassigned.** It isn't one golfer, and pooling golfers into a baseline is the fault J-59's amendment found.
- **`report_input.json` is the source file.** It holds the coach's note, the send log, the options and the make history: anything no rebuild can recover (the H-04 lesson).
- **The old `report_meta.json` is split lazily on first open.** The source file is written and read back first; only then is the derived file rewritten. A crash in between leaves both readable.
- **The library re-point to a library that lacks the golfer** is an edge case, filed with the `edge case` label and parked.
- 5.0 follow-up (J-66): an old `unknown/` no-golfer folder stays where it is, held by Unassigned (none exist today).

### J-67 (me, 22 Nov): 5.1's rulings, from its inventory (step-5/DUTIES-5.1.md)
1. **`App::sessionAt` finds a session through whichever athlete's folder holds it.** Otherwise a late phone clip, launch-monitor reading or re-analysis that arrives after End and a golfer switch would be dropped once N1 goes. A session belongs to its athlete whoever is current.
2. **A D7 re-point to a library without the golfer starts under that library's Unassigned.** Deleting N1 forces this. #200 is closed by it.
3. **An Unassigned capture records no golfer in the swing header,** as today.
4. **Partial A5 removal accepted** (3 of 5 lines to 0). No include swap to dodge the lint on `shot_processor.h`: its line stays, re-tagged with its true reason.
5. **Moving a session to a golfer:** rename the folder, rewrite `session.json`'s athlete, and leave the swing headers alone.
6. **Unassigned is held in memory;** its `athlete.json` is written at the first capture or move (reads never write).
7. **No pooled athlete-level figures for Unassigned** (wrong data, the same principle as no baselines). Its Home shows a notice instead; its sessions still list, open and play.
(This J-66/J-67 numbering is the coordinator's; step 2 also had a J-66.)
- **The Product Owner, 22 Nov:** "i agree with your rulings J-67".

### J-68 (me, 22 Nov): 5.1's review
- **B1:** an Unassigned session's report must not compare it with other Unassigned sessions (J-67 7). It has no athlete level and no siblings, and says why in words.
- **B2:** the LC-SESSION wording is aligned with J-67 2.
- **B3:** after a move empties Unassigned while it is current, the app switches to the golfer the session moved to, so Unassigned hides (R-UNASSIGNED-3). This supersedes NOTES' E-4 "deliberate".
- The review's six EDGE findings are filed as #204. The next build builds all targets, the migration tools included.

### J-69 (me, 22 Nov): 5.2's rulings, from its inventory
- **5.1 blocker found by 5.2's inventory:** the report's carousel got no door, so reports would show no launch-monitor figures. It is fixed in 5.1 before commit, with a real-path test.
- **The "live" folder and the session on screen:** `AppState` holds it itself (the latest session at launch and on an athlete switch, today's after End), instead of reading it back off the carousel's first row. This is J-a2's rule (4-a, accepted with step 4) moved into its owner. R-APP-3's text is brought in line by 5.D, with the provenance as a footnote. No new decision.
- **The review header's club mix** sums the view's rows, so it looks identical to today. It does not take the effective club from the summary.
- **Accepted:** rows drop the launch-monitor slice (the board reads it itself); the review load keeps its parallel indexing pass (#173); tests use one scratch-Session helper; three instances stay.
- **Rating and note edits send no event:** an edge case, filed as #206.

### J-70 (me, 22 Nov): 5.2's two proposals
- **The carousel reads its Session through a small port** (`App/shot_source.h`, implemented by `App/session_shot_source.cpp`). Several QML suites and test targets compile the carousel without linking a Session. The port holds no facts, and is the adapter boundary R-QML-5 implies, so it is not an information-model change. Accepted.
- **Edits from the carousel go to the row's Shot through that port,** not through `app`'s folder door. Same owner, same commands. Accepted.
- Edge cases 1–5 are filed as #207. The stale names (7) and the unrun probes (8) go into the review round as test and comment minors.

### J-71 (coordinator, 22 Nov): 5.3c
- **`aliases` in the corpus golfer file (`corpus/swings/athletes.json`) is not an information-model change.** That file is a corpus tool input that the app never reads. It sits outside the model's objects and files (§1.2 covers what the app owns and persists). If the app ever reads athlete aliases, that is a change for the Solution Architect.
- The parity reference is now `step5-baseline`, taken on the migrated library. 3.10's baseline predates the migration's ids.
- The four edge cases are filed as #212.

### J-72 (coordinator, 22 Nov): 5.4's rulings, from its inventory
- **Only the detection and freeze pools go.** The review pool is the door path's save lane (DEC-183), and the history pool keeps its own later bullet.
- **A folder naming another athlete's id (J-a3) gets the empty adapter,** shown "not judged". The wrong wording is an edge case, filed as #213. The library has none since the migration.
- **The no-door fallback writes and both stores are deleted.** The adapter refuses and says so.
- **The four QML sites change in the same build as the deletion,** and the probe covers a live shot path.
- **The .cpp allowlist line is lowered, not removed:** the history read keeps its file access.

### J-73 (coordinator, 22 Nov): 5.4x, the stale driver footer
- After End (Closing), the footer is worked out again from every new diagnosis. The reloaded footer is the correct one.
- While the session is live, the footer still moves only when the pattern set changes, so the root doesn't churn under the golfer's eyes. This is §A5's deliberate rule, kept.
- **For the Coaching Lead, when convenient:** should the live footer also follow every diagnosis?
- The stale footer has been in the app since 3.7-a (`cc563466`).

### J-74 (coordinator, 23 Nov): 5.5's rulings, from its inventory (step-5/DUTIES-5.5.md)
- **`SessionSeries`** is attached to every Session through a hook, so it is created and ended with the session, as the information model says. Its contents are computed lazily.
- **No information-model change for:**
  - the reduction's uncapped order, kept in memory only;
  - the owner's `fileState()`, a read of state it already holds;
  - the series following EVT-04 and EVT-21 (a consumer-list status edit).
- **For the Solution Architect: DEC-74's day-level reads.** Charts and the report would read other sessions through session results, which means growing the summary file with delivery and earlier posture readings. That is an information-model change (C3). It moves to its own later item, with a proposal. Today's reads stay, and their allowlist lines are lowered, not removed.
- **5.5 is split in three:**
  - 5.5a: the series, the presenter base, threading, the pack holders and the parity tools;
  - 5.5b: typing Trust, Consistency and Delivery;
  - 5.5c: typing Traces, Relations (F-10), Wrist and Posture.
- **Threading:** the app's WorkPool, with a lane per presenter instance (R-CON-5). `PresenterPool` and the stills' global pool go.
- **The three diagnostics QML rules** go to 5.8.

### DEC (Solution Architect, 23 Nov): grow the session summary for day-level reads (DEC-74)
"You can grow the summary file to support this." The session summary (`work_ons.json`, the Session's typed summary) gains the delivery readings and the posture readings that the Charts delivery presenter and the report need when they read OTHER sessions of the day. Those day-level reads then go through `SessionIndex` session results, not other sessions' `swing_phasegrid.json`, and the delivery and posture A5 lines go.

This is item **5.5d**, after 5.5a. The information model is written first. The summary's schema version is bumped, so the catch-up re-summarises once, and the newer-file rule (R-PERS-3) protects other machines. Only the fields those readers need are added.
- **Design Lead, 23 Nov:** C1, the grade words, stays in QML for now. Issue #219 looks at it after the refactor.

### J-75 (coordinator, 23 Nov): 5.5c's three calls
1. **The ★ reference is read through its own session's series,** that is, through that session's owner (P11). This is not an information-model change. Opening Charts with a ★ in another session loads that session, and its owner may re-summarise stale files: the same rule the catch-up already follows.
2. **Two transient wordings are accepted under show-with-warning:**
   - a live shot's Posture picture says "not on disk" until its series lands;
   - a first ★ read says "still being read".

   The first wording is filed for polish (a live shot is being written, so "not on disk" misleads for a moment).
3. The four presenters are typed, and A6/A5 go to 0 for them.

### J-76 (coordinator, 23 Nov): 5.6a's fold rule
After the split, an older build may still write the note, sends, options and history into report_meta.json. They are folded into report_input.json before the strip: sends and history as a union, with exact duplicates dropped. report_meta.json's note and options win only when it holds a later make, because an older build writes every key on every save, and a blank note it read back must never replace the golfer's. Not covered: a note typed on an older build with no remake afterwards. That is rare, and the install-latest rule at the cabin covers it.

### J-77 (coordinator, 23 Nov): 5.7c+d's two calls, accepted
- **J-1:** at load, the replay's analysis slice still comes from the one parse its streams need. Only the refreshes read through the Shot's `RecordingSource`.
- **J-2:** markup's path for a swing no Session holds is gone, as in 5.1. The panel only ever targets the focused shot of the session on screen, and nothing calls `loadSwings`.

The coordinator checked the truth-write diff itself, in place of a full review: the write moves onto the Shot command that held swings already used.

### J-78 (coordinator, 23 Nov): 5.6b's consistency badge stays the report's own rule (for morning review)
The Solution Architect's 5.6b answer listed C10 ("Home's grade asked again") as a second copy to remove. 5.6b found that routing the badge through the athlete level would overwrite Home's last grades. The review confirmed it is not a second copy: `ledgerConsistencyGrade` is defined once, in `Coaching/consistency_grade.h`, and the report grades this session while Home grades the athlete's latest. **Call:** the badge stays the report's own call of that one rule. Nothing is computed twice, so DEC-81 holds.

### J-79 (coordinator, 23 Nov): process
Four questions were asked live on 23 Nov (the restamp, the report's sources, the PDF staging, the data viewer's Properties). The Product Owner reminded the coordinator that the run is unattended. From now on every call is logged here for morning review, and information-model questions keep today's behaviour until answered.

### J-80 (coordinator, 23 Nov): 5.6c, for morning review
- 5.6c was committed on a coordinator self-check, with no review: it moves reads, has parity 0 of 78,507 values and 180 identical pages, and writes nothing.
- **Proposed, for the Solution Architect (not built):** `SessionDiagnosis` carries its own sequence-consistency reading, so B6 is computed once. Today the report re-runs the same rule over the owner's rows. That is still one rule, but run twice.
- B6 is not covered by parity, because no parity session has a session-level work-on. Q9 (one pack load per make) has no unit test, because no test drives a make. Both go to the 5.9 review.

### J-81 (coordinator, 23 Nov): 5.8a, committed on a self-check; its IMU route gets its own test
- 5.8a moves reads only (no writes). Its sheets are identical over three sessions and 32/32 suites pass, so it was committed on a self-check with no review. The rebase onto 5.6c was built and its tests run before the push. The A2 line stays at 1, because QML creates the presenter, as Charts and Trust do. `session_review_controller.h` A6 stays at 1, because the report's export reads it.
- **The new route for IMU (motion-sensor) lanes had no fixture.** It is normal use for HackMotion and trunk sessions, so a follow-up item, 5.8a-imu, adds a test with a synthetic IMU recording before 5.9.
- J-6 in NOTES-5.8a.md is for the Solution Architect (morning review).

### J-82 (Product Owner and Solution Architect, 23 Nov, reviewed live): today's calls approved
- **Approved as made:** J-76, J-77, J-78, J-79, J-81. In NOTES-5.8a.md: J-6 (the read accessors sit on `LoadedRecording`, not on the Recording contract).
- **J-80's proposal is approved:** the session's diagnosis carries its own sequence-consistency reading, so the report stops re-running the rule. It is a new item, 5.6d.
- **Step 6 brief:** the re-fire happens through the version-gated catch-up on the cabin's next install, with no separate sweep. Stored `"DRIVER"` stubs are read as today, with no migration.
- **EVT-12/13:** the information model's text is corrected to match the code (a corridor edit is EVT-12). This happens at 5.9.
- **5.7a:**
  - `Session::shotChanged` is approved (a Qt signal that passes on the Shot events; no new domain event);
  - **P1 is approved:** `SessionIndexEntry` gains the swing roll-up, the size on disk and the lead work-on, read through `SessionRepository`. It is a new item, 5.7a-P1;
  - Archiving stays library-wide, and R-QML-6's wording is corrected at 5.9.

### J-83 (coordinator, 23 Nov): 6.1a's four proposals, logged and not built (for morning review)
6.1a keeps today's behaviour for each of these:
- **D-1 (Solution Architect):** HackMotion readings are written by capture, as readings (R-INS-2, P1 per kind), not by the analysis as today.
- **D-2 (Solution Architect; the editor is the Design Lead's):** retire the 27 authored `preferKeys` ladders that equal the default order. That is a pack format change.
- **D-3 (Solution Architect):** the route erasers (`applyTrunkSensorPrecedence`, `applyLeadElbowSensorPrecedence`, the 3-D step-aside) consult the one precedence rule. That changes stage output, so it needs version bumps and a re-fire.
- **D-4 (Coaching Lead):** the board's IMPACT low point skips `lowPointFromAttack`, which the pack ranks between the device and the camera. Under the one rule, 31 shots would show the LM-derived figure instead. Today's board choice is kept as the one exception until the Coaching Lead rules.

**My recommendation:** D-2 and D-3 go to #224 (hygiene, after the refactor). D-1 is approved for step 6 if cheap. D-4 is the Coaching Lead's call.

### J-84 (coordinator, 23 Nov): 6.1a's Windows path mismatch is treated as blocking
The review called it an edge case: on Windows, a path-spelling mismatch now refuses a launch-monitor reading, where before it was written silently. Windows is the primary platform (the studios), and a reading is source data, so a refusal there would lose golfer input in normal use. It is fixed in the fix round, matching by Shot id or by a normalised path, with a test.

### J-85 (Product Owner, 23 Nov): the step 6 brief is approved as written, with its judgement calls

### J-86 (Product Owner, 23 Nov): the step 7 brief is approved as written

### J-87 (coordinator, 23 Nov): 6.1a's board evidence
The fix round's focused-shot CONTROL dump could not be made, because the CONTROL app had been deleted after round 1. The 85 whole-graphics dumps match every field the CONTROL holds (per-shot readings, the low point, the headline shot's strike, flight and shape), with 0 differences, and the 106 earlier dumps also show 0 differences. That is accepted as enough, without a 4th build. The board's look is covered again by the step-close sheets at 5.9 and step 6's close.

### J-88 (coordinator, 23 Nov): 5.7a-P1, committed on a self-check
- It writes only derived data that can be rebuilt (the index entry's summary). 345/345 pass on the merged tree, and the four list dumps match the control.
- **For the Solution Architect (morning):** the new store file `Export/session_index_summary.h`, like 5.7a's E4 door, is not counted by lint A5. Either the lint widens to count Export store doors, or they are listed as known doors. The proposal is to widen the lint at 5.9, removal-only otherwise.
- `session_list_reads` keeps the folder's mtime only (the drawer's order), because putting mtime on the entry goes beyond P1.

### J-89 (coordinator, 23 Nov): 5.7b, committed on a self-check
- The Wrist ★ write was checked by the coordinator: the record's ShotRef resolves through the Athlete's `session()`, which loads on demand, so a reference in an older session still resolves. A folder outside the current athlete is refused with a log line.
- **Launch:** every athlete's summaries are now read at launch, which is more launch work. They are read off-thread through `deferredSummary` (5.7a-P1). This is to be watched in the 5.9 cabin check.
- `clubsFor` stays C++-only, because the LM controller reads it.

### J-90 (coordinator, 23 Nov): 6.1b, committed on a self-check after a merged build
- **Interim owner.** The LM board is owned by `SessionReviewController` (`lmBoard`), not by the Session, because it shows unsaved live rows and must say "No launch monitor connected" with no session. That is better than today, where the panel creates its own model, but short of "models belong to the session". **For the Solution Architect (morning):** D-6, the board as a presenter over `SessionSeries`, owned by the session. D-5 is the device-only create's writer.
- **Not typed:** the board's `graphics` map and LmClubArt's anchors (A6 2→1 and 3→2). That is hygiene, so no builds are spent on it, and the lines are re-tagged at the close.
- **For the Design Lead (morning):** the printed report's LM appendix takes its names from the words table, so "Path" becomes "Club path", "Face" becomes "Face angle", and so on. One club-marks row wraps one more line on one page; no page is added.
- **Flaky under load:** `shared_norm_provider` and `charts_traces` failed once under the full parallel run and passed when re-run alone. The first is a known flake (0.4b); the second is new. Both are filed with the edge cases.

### J-91 (coordinator, 23 Nov): 6.3a, committed on a self-check
- It writes only derived data that can be rebuilt. Parity 0 over 92 shots; 108 dumps identical; 345/345.
- **Visible once (for the Product Owner, morning):** on the new build's first launch, every earlier PDF report reads "out of date" once, because `contentStamp` now carries the flight norms. Every shot is also re-derived once; nothing is re-fired.
- 6.3a's own calls J-1 to J-5 (Solution Architect) and D-1 (persist the verdict, a C3 change, not built) are in `step-6/NOTES-6.3a.md` for the morning review.

### J-92 (coordinator, 23 Nov): 5.8b, the QML globals are 18, not 4
- R-QML-1's "devices, settings, logging and app" is read as four *kinds* of global, not four objects, because inventing `devices`, `settings` and `logging` objects would be an information-model change. 23 globals moved under `app` (a new Adapters-ring `GuiApp`), and `ppcpImport` (no reader) was deleted. **For the Solution Architect (morning):** whether to wrap the kept globals into those three objects; and three kept "devices" that may not be devices (`launchMonitor`, `trunkCeremony`, `liveWrist`).
- **Coordinator check:** a grep of product QML for bare uses of the 24 removed names found none. Some rewritten QML (dialogs, the report's bulk jobs, AI Coach) is drawn by no probe, so it gets a check at 5.9's step-close sheets and the cabin check. It was committed on a self-check after a merged build and a full run.

### J-93 (Product Owner, 23 Nov): the QML globals
J-92 stays as built (18 globals). The question of wrapping the kept globals into devices, settings and logging objects is settled after the refactor ends: the final count is taken at the close, and the wrapping may fold into the platform refactor that follows. **The final report must call this out** (the globals before and after, what is left and why, and where it goes next).

### J-94 (coordinator, 23 Nov): a process gap, for the final report
Every item's "full run" excluded `qml_ui` (`-E '^qml_ui$'`), because it is one 7-minute binary (the owner's 28 Oct rule: run single tst files, never the whole suite). So 5.8b, which rewrote 980 QML references, was pushed without its QML tests being run, and 10 of 555 checks went red. 5.6d's agent ran the suite and found them. **Change from now:** an item that changes QML runs the `tst_*.qml` files covering what it touched, through the lock. The 5.9 close runs the whole `qml_ui` once.

### J-95 (Product Owner, Solution Architect and Design Lead, 23 Nov, reviewed live): every recommendation accepted
- J-83: D-1 is approved as a small step-6 item; D-2 and D-3 go to #224. **D-4 is open:** the Product Owner asked for it to be explained.
- J-84, J-87, J-89, J-94 are kept. J-88: lint A5 is widened at 5.9.
- J-90: (a) D-6 goes to the platform refactor; (b) the report's new LM names are accepted.
- J-91: the old PDFs reading "out of date" once is accepted.
- 6.3a: J-1 to J-4 are kept; its D-1 goes to the platform refactor.

### J-96 (Solution Architect and Coaching Lead, 23 Nov, live): ranking versus estimation; D-4 settled
- **The principle (information model `dd71f2d8`):** R-INS-3 is amended, so a metric a device reports is ranked. **R-INS-5** is new: a metric no device reports is estimated from every input through a stated model, with σ, a biased input only once calibrated, and a ⚠ on disagreement. **What it exposed:** R-INS-3 as first written said "one rule decides which source wins" for every metric, so 6.1a built pick-one even for derived metrics. The principle of using multiple devices was never written where agents read.
- **The low point:** no supported LM (GC Quad, Foresight) reports it. The estimator is #229 and the audit of every derived metric is #230, both after the refactor.
- **D-4 interim (in 6.2):** the board follows the pack and shows `lowPointFromAttack`, with its ⚠, so the board and the findings agree until #229.
- **For the final report:** a missing principle, found by a ruling, not by a test.

### J-97 (coordinator, 23 Nov): J-94 corrected
`qml_ui` had not run since step 4's `4baa30dd` (22 Nov), not just since 5.8b. Of the 10 failures, only 2 came from 5.8b (test_67, a fixture gap, and SessionMode's guard). The rest came from 4baa30dd, 8ce167e5 and 6e9cb3a9. 5.8c fixed all of them but one in test code; the one left, ChartsPanel test_15, needs a real-Session harness and has its own issue. So the gap is wider than J-94 said: the QML suite went unrun for about 30 items, across two steps.

### J-98 (coordinator, 23 Nov 21:30): 6.3b, before commit
- The agent's blocker fix (a left-hander's "Not right?" pick flipped twice) was built and tested by the coordinator: 28/28 targeted tests pass.
- It goes to review, because it changes what the golfer sees, and its handedness and Sky fixes were proven only by unit tests: the parity set has no left-hander and no skied shot. The reviewer builds a scratch left-hander and a skied shot.
- **For the Product Owner (morning):** 44 of 73 LM shots' direction words change, so the card matches Tendencies at last; every earlier PDF reads "out of date" once; three dormant left-hander Club delivery branches now run.
- **For the Coaching Lead (morning):** 6.3b's D-1 (the stored compound miss keeps 1° halves), D-2 (curvature no longer turns a fade into a slice on reads with no axis) and D-3 (the fade and draw thresholds ignore the grade policy). Logged, not built.

### J-99 (Solution Architect, 23 Nov, live): the Analysis vocabulary gets its own folder
7.3 had listed its 11 moved headers by file as Vocabulary inside `src/Analysis`, following four existing per-file lint entries. The Solution Architect ruled for a folder instead, so developers can find them and it is more elegant. All 15 files move to `src/Analysis/types/`; the lint gets one folder rule; the information model names the folder. The coordinator first recommended the folder, then the listing after finding the existing entries; the Solution Architect chose the folder. The follow-up costs 1 build, and its include rewrite is scripted for lane B's rebase.

### J-100 (Solution Architect, 23 Nov, live): one root folder for the Vocabulary ring (supersedes J-99's folder name)
- `src/Vocabulary/` is the single root for every Vocabulary-ring type, with an area folder inside it for each functional area. 7.3's 15 Analysis files go to `src/Vocabulary/Analysis/` now.
- **MANDATORY in the platform refactor (not optional):** `Metrics/`, `Recording/`, `Identity/` and Core's vocabulary files move under `src/Vocabulary/<area>/`, and the lint ends with one Vocabulary rule. Recorded on #224, in the platform refactor assessment, and in the final report.

### J-101 (coordinator, 23 Nov 22:07): 6.3b's B-3, handedness for LM-only shots
LM-only (device-only) shots record no handedness, so for a left-hander the LM card mirrored and This shot did not (12 of 25 shots disagreed). **Call:** the one gloss function falls back to the athlete's recorded handedness when the shot carries none. That reads an owner's existing fact and stores nothing new, so it needs no approval. **For the Solution Architect (morning):** record handedness in device-only recordings (R-REC-8), so the shot carries its own.

### J-102 and J-103 (coordinator, 23 Nov 22:46): 6.3b's fix round
- **J-102:** the shot's derived stamp includes the athlete's hand, and a hand edit re-derives the session. This is the settled R-STAMP-1 (a stamp covers every input), so it is built, and the lagging OBJ-SHOT-DERIVED and EVT-14 rows are corrected.
- **J-103:** the resolved hand is written into existing derived caches (the summary's `hand`, `tendencies.json`), so "Right" is stored where "" was. That is derived and can be rebuilt; it is checked by parity with expected differences at landing.
- **For the Coaching Lead (E-11, morning):** body conditions whose sentences name a side (alignment, aim, shank) now read right-handed for a left-hander by design. Only the start, curve, path and face-to-path conditions mirror. Is that the coaching intent?
- **For the Solution Architect (morning):** P-6.3b-1, record handedness in device-only recordings (J-101).

### J-104 (coordinator, 23 Nov 23:32): 7.1's norm fallback goes through a hook, not a link-time edge
7.1's J-2 is refused. It declared `makeNormProvider()` in the rules and defined it in `src/Export/rule_data_reads.cpp`: a rules-to-adapter dependency the A1 lint cannot see, which hides a breach rather than removing one. It is replaced by the existing hook pattern (`installSharedNormSource`): the rules declare a factory, and the composition root installs it. One extra build is authorised over 7.1's budget for it. `measure_sample.cpp`'s A1 and A5 lines are re-tagged to the platform refactor (P-3, for the Solution Architect in the morning: the model browser scans the phase grid by folder with no Shot door).

### J-105 (coordinator, 24 Nov 23:56): 7.1 committed, and three loose ends
- 7.1 was committed on a self-check: parity 0, and 345/345 on the merged tree with both lints.
- **The allowlist re-tag of lines 28 and 59 is held for the step close:** the lint accepts only `platform refactor (capture)` or `(settings)` as tags. The close decides one tag for everything leaving the plan (for example `platform refactor (#224)`), widens the lint's tag rule once, and re-tags all such lines together. That is a tooling change, with no line added.
- **The empty norm-factory slot gives no warning** when nothing installs a factory. The product always installs one, so this is EDGE and goes into the step's edge-case issue.
- **For the Product Owner (morning):** a 7.1 agent ran `lldb`, which raised a macOS Developer Tools permission dialog. It may still be on the Mac's screen; dismiss it ("Deny" is fine). No lldb process is left running. Agents are told not to use debuggers.

### J-106 (coordinator, 24 Nov 00:53): 6.2, stored "DRIVER" stubs on the LM board
J-82 promised that stored stubs read as today. 6.2 deleted the board's private evidence relabel, which used to show a stub shot as the iron its readings suggested, so a stub would now show as a flagged driver and the board's default scope moves. **Call:** keep today's board relabel for legacy stub shots only (a stored `"DRIVER"` with no declaration), as one narrow read-time exception beside the one club rule, done in 6.2's fix round. **For the Solution Architect (morning):** read a legacy stub as *undeclared* everywhere (R-CLUB-4). That is a read-time rule with no migration write; findings would fire "club assumed" for those shots, and the exception would go. Golf context: the Product Owner never hit driver with PPS, so the stored stubs are other clubs.
- **Also logged (morning):**
  - 6.2 marks 40 parity-set shots stale by input stamp, so re-analysis will use their popover club, as R-REC-6 intends. Expect a re-analysis on the cabin's first launch.
  - **For the Design Lead (DL-4):** the popover shows "Driver" for an undeclared shot, and a popover pick is shadowed on a board-corrected shot.
  - **D-4's board interim** was missing from BRIEF-6.2 (the coordinator's omission). It is in 6.4.

### J-107 (coordinator, 24 Nov 01:02): 7.2 committed after a sign-off
- The pose model fingerprint is proven identical: the same expression and path, and the app's install shown in the built binary. The reviewer corrected the coordinator's premise: `versions.pose.model` is provenance, not a reuse key (`ReuseGate::mayReloadPose`, DEC-64), so even a wrong string could not re-pose the library.
- **For the Solution Architect (morning), J-2:** the fingerprint goes through a hook (an installed reader). The reviewer recommends a direct call, `modelFileFingerprint` beside `modelFilePath` in `Core/pp_model_path.h` (Platform ring, so A5 exempts it). That removes the slot, its three install sites and the `@?` failure mode. As built, the hook stays until ruled.
- 3 EDGE items: the `@?` branch untested; a future tool forgetting the install; re-analysis reads about 150 KB more per swing.

### J-108 (coordinator, 24 Nov 01:59): 6.4
- **The board's low point follows the pack's one ranking (the D-4 interim):** 63 shots change, not about 31. 31 swap the camera's figure for the attack-angle one, and 32 that read "not measured" now show a figure. **J-6 call:** Charts Delivery and the report's delivery lines follow the same ranking, or they would disagree with the board and the findings on those 63 shots. That is the settled R-INS-3. The ⚠ and σ match the findings' presentation (J-3).
- **For the Product Owner (morning):** the 63 shots, and the report appendix's caption in 6 of 7 sessions. The list is in `step-6/NOTES-6.4.md`.
- **D-1 deferred:** the `hm.*` values are curves the analysis derives from the HackMotion streams that capture already writes, so moving the writer is a format change. **For the Solution Architect (morning):** tag the `hm.*` values *derived* (R-INS-5), not readings, which would reverse 6.1a's J-2.
- **J-108 note (J-3):** the "from attack angle" low point carries no ⚠, because in this app ⚠ means a gate touched the reading and no gate covers this one, as in the findings. Its uncertainty shows as ± σ ("Low point (from attack angle) 3.1 ± 0.6 in"), on the board and in Delivery alike.

### J-109 (coordinator, 24 Nov 02:39): the platform refactor assessment, revision 2 (drafted; counts filled at the close)
- **For the Product Owner (morning): confirm the three scoping calls** that Gemini's review reports were agreed with the Product Owner. Our own sources record only the Vocabulary root-folder move as mandatory (J-100). Until confirmed, #224 as a whole is marked "reported by the review; confirm". The draft splits #224 into the part Stage B depends on and a hygiene track, because D-3 needs a re-fire.
- **For the Solution Architect (morning):** J-100's Vocabulary ruling names Metrics, Recording, Identity and Core. Should Buffer's stream types, LaunchMonitor's reading types and `frame_decode` also move under `src/Vocabulary/`? "One Vocabulary rule" needs them. Buffer's would wait for the platform refactor's Stage D.
- **Gemini's review:** each point carries a verdict in the assessment's new §10 table. The rejections include "#224 is the ring splits" (it isn't), the stale "all eight dirs span" (fixed by 7.3), and a "mandatory" segmenter binding for a leak nobody has observed.

### J-110 (coordinator, 24 Nov 03:16): the close's survey left two unowned copies
- **2d** (a no-document card's golfer edits may be dropped when its document lands) is treated as blocking under the standing rule: anything that can lose golfer input is blocking, however unusual the path. Item **5.10** proves or disproves it and fixes it before the cabin installer.
- **6c** ("live" from the carousel) is not golfer data, so it is filed as an issue.
- **The close's figures:** allowlist debt 424 → 428 findings, because A5 now counts the store doors (J-88). The QML globals end at 18. `qml_ui` passes 555 of 556 (the 1 is #231). `rule_data_reads.cpp` is not yet counted by A5 (for the Solution Architect, morning).

### J-111 (coordinator, 24 Nov 03:28): the platform refactor assessment, revision 2, committed
Re-measured at `44ae9230`. Five figures the re-measure could not confirm, for the Solution Architect (morning):
- the "about 5,000 lines" of domain code in Gui cannot be reproduced (F-11's ten files are 8,529 lines);
- all eight directories still span rings, so IM §4.1's Analysis row is stale after 7.3;
- `ppcpOffers` is behind `HAVE_PPCP`, not `HAVE_PPCP_TRANSPORT`;
- a count went 102 → 103 because a comment line had been counted;
- the PPCP test count reproduces only without the folder's `.qml`.

### J-112 (coordinator, 24 Nov 03:45): the end
- **6.2's delta check, done after its commit** (an advisor caught that it was missing): the legacy-stub predicate (`swingDocIsLegacyClubStub`: DRIVER declared, no captured or device club, not a launch-monitor shot) matches only shots that predate 6.2, because new captures carry the session club. Its one shadowed case is #233. The 3-D synthesis change is the eight lines the fix described. The `.ppsw` path rests on `swing_doc_test` alone, so it goes into the live check.
- **The report:** an advisor review found Part A contradicting Part B on the pace ("55 items, more than the eight days before": wrong) and claiming full delivery, so both were corrected. The name scrub was redone against the library's real athletes and ids.
- **The ⚠ on 6.4's low point** departs from J-96's "with its ⚠", and is put to the Product Owner.
- **The pre-approval and the freeze ended with step 7.** The coordinator stops committing after the report's fixes.

### Rulings of 24 Nov (08:37): the morning review of J-101 to J-112, and the second outside review
Every ruling took the recommendation put to it. Recorded in `docs/implementation/platform_refactor_assessment.md` ("Rulings of 24 Nov").
- **Product Owner:** #224 in three groups (#224-U, the step-5 presenter residue, is a plan of its own straight after Stage B); #222 stays out (confirmed); GPU segmentation out of the platform plan (#240); #235's half (2) is a Stage A bug fix; the low point shows ± σ with no ⚠, as built (J-108 note, J-112); the 63 shots that 6.4 changed are accepted.
- **Solution Architect:** R-REC-8 approved (Stage D); `hm.*` re-tagged as derived, 6.4's D-1 dropped (#239); the fingerprint becomes a direct call (Stage B); Buffer's stream types, LaunchMonitor's reading types and `frame_decode` move under `src/Vocabulary/`; P-3 is a Shot-level reader with the scans on `SessionIndex`; A5 counts `rule_data_reads.cpp`; legacy DRIVER stubs read as undeclared (#238); J-111's stale figures corrected (IM v1.9.53, 02's F-11 footnote).
- **Design Lead:** the popover shows "Not declared" (DL-4, on #233).
- **Coaching Lead:** side-naming conditions mirror for a left-hander (#236); the compound miss starts at 4° and fade/draw follow the grade policy (#237); 6.3b's D-2 accepted as intended.
- **Correction:** the second outside review was right that re-analysis' A1 line is `recorded_products.h`, an Adapters-ring file inside `src/Analysis`; the coordinator's first "correction" of it was wrong and has been withdrawn.
