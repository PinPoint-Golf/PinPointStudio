# Platform refactor, Stage F: the phone link (PPCP). One question that sizes the stage, twelve more, and one scope lever, classified

**Status: proposed, 24 Nov 2026. Nothing here is ruled.** Each row ends with an empty **Ruled** line: the
coordinator writes the ruling, the role and the date there, and the briefs that wait read it. Until a row is ruled,
today's behaviour stays and the items that wait on it are not built[^hard]. Every recommendation is **(proposed)**
for the role its **Class** line names. Each row also says what happens **if nothing is ruled**.

**Why this page exists.** Stage F of the platform plan gives the phone link's files one owner, splits its
4,900-line host service, and fixes the listed defects of #78 and #79[^plan]. Its item plan (23 items, about 53
hours) is provisional, and its size hangs on one answer from the Product Owner: Q3, how much phone capture matters
over the next six months[^planf]. That question is first. The stage's other questions follow, and the forecast's
lever 6 is put as a proposal with its counts[^fore].

**A lost pairing is a blocking loss**, however unusual the path[^common]. Every row that touches a pairing says
so, and "What touches the phone pairings" gathers them.

**How each row is classified.** By the information model's definition of a change, §1.2 (C1–C11), read through
`tools/method/im_excerpt.py`[^imc]. A row that only makes code match what the information model already says is not
a change (§1.2's own exclusion). A row that changes a persisted file or its one writer, an identity, or a lint
rule's scope, is. Where the model is silent, §1.2's last sentence applies: "When in doubt, it is a change."

| Class | Meaning | Count | Rows |
|---|---|---|---|
| **HARD: the Solution Architect** | An information-model change, or a question the plan keeps for that role. Nothing that depends on it is built before the ruling | **7** | F1, F2, F3, F4, F5, F6, F7 |
| **HARD: the Design Lead** | A design-system change | **0** | none: the one the plan listed is already built (N1) |
| **Coordinator's call** | A build-order or evidence choice with no C1–C11 change. Decided and logged as a judgement call | **3** | N1, D1, S1 |
| **Product Owner** | Scope and order | **4** | Q3, V6, P1, G1 |

Fourteen rows. Q3 and V6 are one decision seen from two sides: Q3 asks the fact, V6 is the cut that follows from
it. Three of the plan's questions are already asked in the other two packs and are not asked twice: Q7 (the
pairing key stays in settings: Stage E's K5), Q17 (the globals: Stage D's V1 and Stage E's H5) and Q5 (lint A6:
Stage D's V2 and Stage E's H4)[^sd0][^se0]. F4, G1 and F7 say only what each ruling must settle for the phone link.

**Eight places where this page differs from the item plan**, each explained in its row:
1. **There is no design-system row.** Notification coalescing was built on 1 Sep 2026, seven weeks before #79's
   list was gathered: stable ids, a count shown as "(×N)", cause suppression and the session-start preflight (N1).
2. **Two more of the six listed defects look already answered**, read in the code and not run: the "still
   processing" message now gives one of three reasons, and the wired drop has a documented cause outside the app
   (D1).
3. **Under lint A6's narrower wording, the split closes no count.** The allowlist calls `ppcp_host_service.h` "a
   device / capture object's QML API". If Q5 is ruled as the other two packs recommend, its six findings leave the
   rule, and the split's four items (11.5 h) and two inventories (3 h) are left with no lint line or issue to
   close (F7, S1).
4. **The app never writes FILE-SHOT-11.** No product code gives the annotation store a folder, so it holds
   annotations in memory, and no view reads them (F3).
5. **Six `ppcp` settings keys, not five.** `ppcp/captureProfile/<phone camera>` is written from `Gui/cameras`,
   and neither the Rig design nor the item plan names it. Three of the other keys are keyed by the pairing's id,
   which changes on every re-pairing; the Rig's key for a phone does not (F5, F6).
6. **The pairing store's lint line is one `chmod` of the settings file.** It can close with no repository and no
   change to what is stored (F4).
7. **The "parked" path drops the only pin on pairings, while Stage E still re-homes the phone link.** This page
   proposes that three small things are kept whatever Q3's answer (P1). That makes "parked" about 6 hours, not 3.
8. **"One repository" does not close the filer's lint line, and a repository the phone link includes would break
   lint A5's wording.** The filer's 2 findings close with Stage D's allocation command, and the files' door is
   proposed as callables handed in by the composition root (F1).

Two smaller corrections, for the briefs: `foldIn` has two call sites, not one (`ppcp_host_service.cpp:235` and
`ppcp_import_controller.cpp:85`); and with no library set, the ledger is kept in the app's own data folder
(`ppcp_host_service.cpp:225`).

**How to read the counts.** Taken on main at `b5b59cbf`, each with its command, run from the repository root in
`bash`. "Product files" leaves out `tests/` directories. The briefs re-take every figure at run time. "Read, not
run" marks a finding taken from the code alone: no test or app run confirms it here.

**Include check, as the method asks before a dependency rule is proposed**[^inc]. F1 proposes one: the phone link
reaches its library files only through a file door that the composition root hands it. Today `src/Ppcp`
includes four `Core` headers and three `Video` headers, and nothing from `Gui`, `Export`, `DomainModel` or `App`
(`grep -hoE '^#include "\.\./[A-Za-z]+/[^"]*"' src/Ppcp/*.h src/Ppcp/*.cpp | sort | uniq -c`). Five product files
outside it include a phone-link header: `main.cpp`, `camera_instance.cpp`, `shot_controller.cpp`,
`ppcp_clip_filer.cpp` and `VideoInputPpcp.cpp`. The directory is Adapters, in the Acquisition context
(RING-ADAPTERS)[^imc]. No file in `src/Export` includes a phone-link header
(`grep -rlE '#include ".*Ppcp/' src/Export | wc -l`: 0). So the rule holds today in both directions, and F1's
option (a) keeps it only if the file door speaks in plain types: a store that took the ledger's own record type
would add an `Export` → `Ppcp` include that does not exist today.

**What can start with no ruling.** The pins on today's code (SF-2), which need nothing. Everything else waits for
Q3, and then for the row that names it below.

## The question that sizes the stage

### Q3 · How much does phone capture matter over the next six months?
- **Today.**
  - **The code.** The phone link is 24,871 lines in 47 files, with 19 test files and a conformance suite
    (`find src/Ppcp -maxdepth 1 -type f \( -name '*.h' -o -name '*.cpp' \) | xargs cat | wc -l`;
    `ls src/Ppcp/tests/*_test.cpp | wc -l`). Its host service is 3,709 + 1,191 lines with 53 QML-facing members
    (`wc -l src/Ppcp/ppcp_host_service.*`; `grep -c Q_PROPERTY` 30 and `grep -c Q_INVOKABLE` 23 on its header).
  - **What the lint holds against it.** A5 (file access outside a repository): 12 findings in 7 files, and 2 more
    in the phone-clip filer. A6 (an untyped map or list at the QML boundary): 6 findings in the host service's
    header (`grep -nE '^Ppcp/|ppcp_clip_filer' src/Gui/tests/architecture_allowlist.txt`: 9 lines).
  - **How much it has been worked on.** Commits that touch `src/Ppcp`, by month: August 83 of 321, September 15
    of 228, October 2 of 357, November 13 of 321
    (`git log --oneline --since=2026-08-01 --until=2026-08-31T23:59 -- src/Ppcp | wc -l`, and so on). Of
    November's 13, two change the phone link (#144, #145, both 2 Nov); the rest are the domain refactor, tests
    and a moved document.
  - **What works.** #78: "works for one phone; several phones and late-arriving clips are not finished". Its core
    case, two phones on one ball, waits for the shared arbiter, which Stage D builds (SD-9c)[^pland].
  - **Not measured here:** how many stored sessions hold a phone clip. This page reads no library. It is the
    figure that would answer the question best, and the Product Owner knows it without a count.
- **The three paths.** Hours are the item plan's[^planf]; the last column is what this page's findings do to them.

  | | (a) Stage F in full | (b) Files only | (c) Parked |
  |---|---|---|---|
  | Hours, as planned | **53** (37–69), 23 items | **about 25**, 11 items | **about 3**: this page |
  | Hours, with this page's findings | about 48: N1 and D1 turn SF-6e, SF-6f and half of SF-6c into checks | about 22 if the two inventories wait with the split they serve; less under F2 and F4 | about 6, with P1's three kept things |
  | Built | the files' one owner; the service split; the listed defects; the phone as a rig device | the files' one owner (SF-2, SF-3a to SF-3e); #208 | nothing in Stage F. **PPCP's four statics still get their owner, in Stage E's SE-2a, whose hours are Stage E's**[^plane] |
  | Lint lines closed | A5 12 findings → 0 in the phone link; the filer's 2 once Stage D's SD-8d lands (F1); A6 6 → 0 | A5 12 → 0, and the filer's 2 after SD-8d | none |
  | Left open, on an existing issue | the rest of #78 and #79: the stage takes five defects and the coalescing out of their 19 checklist lines | #78, #79, #241 | #78, #79, #208, #241 (less #208 and #241's check, under P1) |
  | Left open, **on an issue to be filed at the ruling** | none | the service's split and its A6 line; the phone as a rig device and the `ppcp` keys the Rig design left for this stage | those two, and the files: the 9 allowlist lines, FILE-LIB-03, -04, -05, FILE-SHOT-11 |
  | Hands-on visit | visit 5, about 60 min, two phones and a cable, seven parked patches | none of its own: the pairing check rides on visit 3 | none of its own: the pairing check rides on visit 3 |
  | Pairings touched | SF-3d, SF-4a, SF-5 | SF-3d only, and not at all under F4's option (b) | none by Stage F; Stage E's SE-2a still re-homes the phone link |

  The 3 hours of (c) are this page, and are spent when it is read. Nothing more is "kept" in Stage F for the
  statics: SE-2a holds them whatever is decided here.
- **What (b) and (c) need written down, so nothing is lost by a tag alone**[^rule10]. The allowlist's phone-link
  lines are tagged `# platform refactor (capture)` and `# unplanned`. Under (b) or (c) each line left is re-tagged
  with its issue's number, which edits a tag and adds no line. IM §8.1's four rows keep "design: the platform
  refactor (capture)" and gain the issue's number. The Rig design named its two `ppcp` keys for this stage "so
  they are not lost": they go on the same issue as the phone's device key.
- **What else the answer moves.** Stage D's SD-9c (the one arbiter for every phone, 3 h, parked for visit 3) is
  phone work too, and is the reason visit 3 needs two phones. It is not in the figures above. If the answer is
  "not a main path", whether SD-9c is built now is the same question, and is put here so it is not missed.
- **Recommendation: (b), unless a phone is the main camera in sessions that matter this winter.** Three reasons.
  1. The files are the refactor's own subject: P11 says only the owner reads and writes its files, and the phone
     link breaks that in seven files. That work is testable on the Mac against the conformance suite.
  2. The split is the risky half and has the weakest case. It moves the pairing and live-session state out of a
     class with 53 QML-facing members, needs the hands-on visit, and under F7 closes no count.
  3. Of the six listed defects, three look answered already, and the rest need real phones to reproduce. A defect
     no agent can reproduce is fixed only with evidence from a visit[^evid].

  If a phone is the main camera for a golfer this winter, then (a), with #78's two-phone case first. If no phone
  session is planned at all, then (c) with P1.
- **Class: Product Owner.** It is a fact about use and a choice of scope. No C1–C11.
- **If nothing is ruled:** nothing of Stage F is built except SF-2's pins. Stage F is last and no other stage
  builds on it, so the answer is not needed before Stage E's SE-2a lands.
- **Waits:** every item but SF-2, and P1's three kept things.
- **Ruled:** (c), parked: important, but not urgent, so a follow-up[^q3r]. Stage D's SD-9c is parked with it. Everything this page lists waits on issue #272.

### V6 · The forecast's lever 6: Stage F parked, or files only
- **Today.** The forecast gives lever 6 as 50 h saved and lever 6b as 28 h saved, each with "value lost: 1" (an
  Accept count or a known defect stays open, on an issue, and nothing depends on it)[^fore].
- **Proposal.** Take 6b: Stage F is its files (SF-2, SF-3a to SF-3e, and the close, SF-7b). The split (SF-1a,
  SF-1b, SF-4a to SF-4d), the rig work (SF-5), the defects (SF-6a to SF-6g) and visit 5 (SF-7a) go to named
  issues. P1's three things are kept in any case.
- **Corrected saving.** 6b saves about 28 h as the forecast says, about 31 h if the two inventories go with the
  split, and 1.5 h more with visit 5's pack. Lever 6 saves about 47 h with P1 kept, not 50.
- **What is given up.** Under 6b: `PpcpHostService` stays 4,900 lines; the Accept row's "A6 findings 6 → 0" is
  met only if F7 takes them out of the rule; the Rig does not list phones, and a phone clip still reads as "not in
  the rig" in each Recording. Under 6 as well: the Accept row's "PPCP files with A5 lines 7 → 0" is not met, and
  P11 stays broken for the phone link's files.
- **The visits.** Five become four under either lever. A paired phone is still checked on latest main at visit 3.
- **Class: Product Owner.** It follows Q3 and cannot be taken apart from it.
- **If nothing is ruled:** as Q3.
- **Waits:** as Q3.
- **Ruled:**

### P1 · Three things kept whatever Q3's answer
- **Today.** The item plan's "parked" path keeps only this page. Three things are then lost that do not depend on
  how much phones are used.
  1. **No pin holds a stored pairing while other stages move the phone link.** Stage E's SE-2a moves the four
     phone-link statics of `main.cpp` (lines 1778, 1795, 1812, 1839) into the composition root and sets their
     teardown order. Stages C and E both rewrite settings code, and the pairing's key is in the settings file. The
     pin "a phone paired before still pairs" is part of SF-2, which the parked path drops.
  2. **#208 is not phone work.** A late part of a shot leaves its card's video flag and thumbnail stale until the
     session is reloaded; a phone clip after End is the example the issue gives. The issue says the fix is one
     line in the carousel's change signal.
  3. **#241 fails a test in a full parallel run whether or not a phone is ever used**: `ppcp_host_service_test`
     failed in 2 of 3 runs. Its suspected cause, an inline name lookup on the GUI thread, would also stall the
     app. It is not confirmed.
- **Proposal.** On every path: the pairing pin, on a scratch settings store, before SE-2a; #208's one line with
  its test; and #241's check, with its fix (SF-6g, 2 h) only if the check shows the block.
- **Hours.** About 3, a guess: the pin 1, #208 1, #241's check 1. Under (a) and (b) all three are already in
  SF-2, SF-3e and SF-6g.
- **Class: Product Owner** (scope). None is an information-model change: each makes code match what the model or
  a test already says.
- **If nothing is ruled:** SF-2 runs anyway, so the pin and both checks exist. #208's fix waits for Q3.
- **Waits:** nothing; SE-2a should wait for the pin.
- **Ruled:** yes, all three kept with Stage F parked: the pairing pin before SE-2a, #208's one line with its test, and #241's check[^p1r].

## The files (SF-3a to SF-3e)

### F1 · One door for the phone link's library files (the plan's "one repository")
- **Today.** The import ledger is `<library>/ppcp-ledger.json` (FILE-LIB-03), written by `PpcpImportLedger`.
  Imported clips land in `<library>/PPCP Imports/<peer>/<session>/` (FILE-LIB-04), written by the import sink.
  Five files hold the 7 A5 findings: the ledger 3, and one each in the sink, the import controller, the host
  service and the bundle transport. The host service and the import controller share one ledger object so that
  two copies cannot overwrite each other; when none is handed in, the controller makes its own
  (`ppcp_import_controller.cpp:78–86`). IM §8.1 gives both files' owner and repository as "design: the platform
  refactor (capture)"[^imc]. Once a clip is filed into its shot's folder it is the `Shot`'s, written once through
  `RecordingRepository::create` by the filer in `Gui/shot` (R-REC-1): that part already matches the model.
- **What lint A5 allows, which decides the options.** A5 counts two things: file access, and an include of a
  repository. File access is allowed only in `src/Export` and the Platform helpers. A repository include is
  allowed only there and in `src/DomainModel`, "the owning objects"[^imc]. The lint knows a repository by a list of
  18 named headers (`isRepositoryHeader`, `src/Gui/tests/architecture_lint_test.cpp:389`), all in `DomainModel`
  or `Export`. So a repository interface that the phone link includes from its own directory would pass the lint
  only because the list does not name it, and would break the rule's words.
- **Options.**
  (a) The shape R-HL-2 gives the application services: the phone link includes no repository. The ledger object
  stays the owner of its records and keeps its JSON. It loads and saves through callables that the composition
  root hands it, over a file door in `src/Export` that speaks in paths and bytes; the imported clips are written
  through the same door. No lint change, and no new include in either direction.
  (b) A repository interface in `src/Ppcp`, its store in `src/Export`, and the lint taught that `src/Ppcp` owns
  it: the interface joins the lint's list and A5's "owning objects" grows by a directory. That changes A5's scope.
  (c) A domain owner for phone imports, with its repository interface in `src/DomainModel` beside
  `RecordingRepository`. It fits the lint as written.
- **Recommendation: (a).** It is a rule the model already has, it needs no change to the lint, and it adds no
  include. The ledger is bookkeeping for an import, not a golf fact, so it needs no domain owner: (c) builds one
  for a file no view reads. (b) is the item plan's wording, "the PPCP repository", and is what should be ruled if
  the Solution Architect wants the phone link's files to have a named repository; it costs a lint-scope change.
- **What the ruling must also say.**
  - The ledger's level: the library's, so application.
  - Whether the "no library set" fallback to the app's data folder stays (proposed: it stays, behind the door).
  - That the controller's own fallback ledger goes once the root always hands one in.
  - **How the phone-clip filer's 2 findings close, because F1 does not close them.** One is its include of
    `RecordingRepository`, to write a phone shot's first document; the other is its folder-to-shot translation.
    The allowlist says why they are there: "capture's first write for every shot, for which the Shot has no
    command". That command is Stage D's S2: `Session::addShot` allocates and creates the document (SD-8d,
    parked for visit 3)[^sd0]. Proposed: the filer asks the session for the shot through that command, so SF-3e's
    lint half follows SD-8d, and the filer itself moves with the capture code in Stage D. The item plan's SF-3e
    names SF-3a and SE-2a before it, and not SD-8d.
- **Class: HARD: the Solution Architect.** The door that FILE-LIB-03 and FILE-LIB-04 are written through (C4), and
  the owner that the model's rows do not yet name (C1). (b) is also C9, by the rule on lint scope (DEC-120).
- **If nothing is ruled:** SF-3a is not built, and SF-3b, SF-3c and SF-3e wait behind it.
- **Waits:** SF-3a, then SF-3b, SF-3c, SF-3e. SF-3e's lint half also waits for Stage D's SD-8d.
- **Ruled:**

### F2 · The legacy ledger's end (FILE-LIB-05)
- **Today.** `<library>/PPCP Imports/ppcp-import.json` is folded into the new ledger at every launch, from two
  call sites. A second fold adds nothing, the old file is left in place, and a test covers it
  (`grep -rn foldIn src --include='*.cpp' --include='*.h'`; `ppcp_bundle_import_test.cpp:1002–1045`). So the
  plan's "migrate it once" is done. What is left is whether the read is ever retired.
- **Options.**
  (a) Keep the fold, at one call site where the ledger loads, through F1's door. The old file is never written and never removed.
  (b) Fold, then remove or rename the old file once the new ledger is saved.
  (c) Retire the read now.
- **Recommendation: (a).** The read costs a file check at launch. Removing the old file gains nothing a golfer
  sees, and going back to an older installer is planned: a build from before the new ledger would find no ledger
  and offer every clip again. Not checked here: which release first wrote `ppcp-ledger.json`; SF-3b's brief finds
  it. Under (a) SF-3b is a few lines inside SF-3a.
- **Class: HARD: the Solution Architect.** Under every option the model's row changes: its writer today is "the
  PPCP host service and import controller" (C4), and (b) and (c) remove a persisted file (C3).
- **If nothing is ruled:** today's behaviour: folded at every launch, from two places.
- **Waits:** SF-3b.
- **Ruled:**

### F3 · The annotations (FILE-SHOT-11): the app does not write them today
- **Today.** `PpcpAnnotationStore` can keep each annotation under `<root>/<session_id>/<shot_id>/annotations`,
  where both ids are the phone protocol's own. It writes only when it has been given a root, and "memory-only is a
  legal configuration" (`ppcp_annotation_store.cpp:273`). **No product code gives it one**: `setRoot` is called
  only by its test (`grep -rn 'setRoot(' src/Ppcp`). No file in `src/Gui` reads the store
  (`grep -rn 'annotations()' src --include='*.cpp'`: no product caller). So in the app an annotation from a phone
  lives for as long as the link does, and FILE-SHOT-11 names a file the app does not make. The store's 4 A5
  findings are that unused file code. Read, not run.
- **Options.**
  (a) The `Shot` owns them, as it does the markup tool's ground truth (FILE-SHOT-12, DEC-187): a `Shot` command,
  written through `RecordingRepository`. The app would then start keeping annotations, which is new behaviour.
  (b) A peer cache: the file code moves behind F1's door unchanged and stays switched off in the app. The model's
  row says so.
  (c) The file code and its test cases are deleted until a view shows annotations.
- **Recommendation: (b).** As built it is a peer cache already: it is keyed by the protocol's ids, not by a shot's
  folder. (a) is the likely answer on the day a view shows a phone's markup, and should be ruled then, with that
  view; today it would build a command for a file nothing writes, inside a freeze on new behaviour. (c) removes
  tested code that the conformance page's Markup row leans on (`docs/conformance/ppcp-conformance.md`, CT-I37);
  whether the protocol requires persistence was not checked here.
- **Class: HARD: the Solution Architect.** Every option changes what FILE-SHOT-11's row says: its owner and
  placement (C1), its writer (C4), or the file itself (C3).
- **If nothing is ruled:** SF-3c is not built and the 4 findings stay on the allowlist, on an issue.
- **Waits:** SF-3c.
- **Ruled:**

### F4 · The pairing store's lint line, and the pairing key (touches pairings)
- **Today.** Each pairing's key is `ppcp/pairings/<pairing id>` in the settings file, and nothing else is stored
  there. The reasons are written where it is stored: in a keychain, Windows and Linux could keep no pairing, and
  macOS asked for permission on every relink. The store's one A5 finding is not a file of its own: it is a
  `chmod` that narrows the settings file to its owner on the first pairing write
  (`grep -nE 'QFile|QFileDevice' src/Ppcp/ppcp_pairing_store.cpp`: lines 251–253). Whether the key moves to a
  secure store is Stage E's K5, which recommends that it stays[^se0].
- **Options.**
  (a) The pairing store behind F1's door or repository, its storage unchanged (the item plan's SF-3d, parked).
  (b) Move the `chmod` into the settings helper (`pp_settings`, ruled Platform[^r3], where the lint allows file
  access) as
  "narrow the settings file to its owner". The pairing store calls it. Nothing about what is stored, where, or
  by whom changes.
  (c) Leave the line on the allowlist, on an issue.
- **Recommendation: (b).** It closes the finding with about ten changed lines and no new path to a pairing. (a)
  puts a credential's store behind a repository built for library files, and adds a parked patch to do it.
- **What the ruling must also say.** The Rig design's answer 8 says the key bytes "follow Stage E's secrets
  work"[^rig]. K5 recommends they do not move. One ruling on K5 settles both sentences.
- **Guard.** The pin of P1 comes first, on a scratch settings store. No test or agent run reads or writes the real
  `ppcp/pairings`.
- **Class: the Solution Architect's question; HARD under (a)** (C4: the repository a SET-09 key is written
  through). (b) is no change: no key, writer or ring moves.
- **If nothing is ruled:** (c). The pairing store is not touched.
- **Waits:** SF-3d.
- **Ruled:**

## The phone in the Rig (SF-5)

### F5 · The phone as a rig device (touches pairings)
- **Today.** The Rig design is approved, and it leaves four things to this stage: a phone's device key
  (`peer_id + source_id`: "the type has room for it; Stage F builds it"), `ppcp/studioName` as the Rig's label,
  "which phones are paired" as rig state, and whether a phone-only document carries a rig reference[^rig].
  Until then a phone clip has no entry in a Recording's rig block and reads as "not in the rig". Two identities
  are in play, and they are not the same thing:
  - **the pairing's id**, a fresh random handle at every pairing; it keys the stored key, and three more settings
    maps (F6);
  - **the phone camera's id**, `ppcp:<peer_id>/<source_id>`, which `ppcp/captureProfile/` already uses
    (`camera_instance.cpp:641–647`) and which outlives a re-pairing.
- **Options, for "which phones are paired".**
  (a) Copied into the rig's state, so the Rig holds its own list.
  (b) Derived: the Rig lists phones by asking the pairing store, through an interface. Nothing is copied, and it
  is not part of the versioned state.
- **Recommendation: (b), and no stored key re-spelled.** (b) departs from a sentence the Solution Architect has
  approved: the Rig design's §4.3 gives "which phones are paired" as "the rig state". Ruling (b) amends that
  sentence; ruling (a) keeps it.
  - With (a) two stores can disagree about one phone: listed with no key, or a key with no listing. The first
    looks like a lost pairing to the golfer. "Forget this phone" would also have to clear both, or an older
    build would still accept the phone.
  - With (a) every pairing and every forgetting makes a new rig state, for a fact that says nothing about where a
    camera stands.
  - The device key follows the Rig design's own rule for sensors: the type names each persisted spelling, and no
    stored spelling is rewritten. A re-spelled key orphans the alias and the capture profile stored under it,
    which is a lost setting.
  - `ppcp/studioName` moves to the Rig by the copy-once pattern of the Rig design's §4.4, with the phone link
    reading it as a typed input.
  - The phone-only document gets a rig block only when it is written through the repository (SF-3e) and the phone
    has its key; until then it reads as `Unknown`, as today.
- **If the stage is cut to files only**, this row's work goes to the Rig's own follow-up issue, as the item plan's
  note N5 proposes[^planf].
- **Class: HARD: the Solution Architect.** A new key representation and a translation between two ids (C5); two
  settings keys that hold rig facts change their home (C3) and their writer (C4); a block in one more kind of
  document (C3). The Rig design's approval covers the direction, not these details.
- **If nothing is ruled:** SF-5 is not built. The two `ppcp` keys stay in settings, and a phone has no device key.
- **Waits:** SF-5.
- **Ruled:**

### F6 · The four `ppcp` keys the Rig design did not place
- **Today.** Six key families begin `ppcp/`
  (`grep -rnoE '"ppcp/[A-Za-z]+' src --include='*.cpp' --include='*.h' | grep -v /tests/`). The Rig design places
  two (`studioName`, `pairings`: F5) and leaves three "not decided here: Stage F". The sixth is in neither page.

  | Key | Keyed by | What it holds | Written in |
  |---|---|---|---|
  | `ppcp/phoneAlias` | the pairing's id | the coach's name for the phone | `ppcp_host_service.cpp` |
  | `ppcp/phoneNames` | the pairing's id | what the phone called itself, its peer id, when last seen | `ppcp_host_service.cpp` |
  | `ppcp/torchDuringCapture` | the pairing's id | whether the phone's light is on while capturing | `ppcp_host_service.cpp` |
  | `ppcp/captureProfile/<id>` | the phone camera's id | the capture profile chosen for that camera | `camera_manager.cpp:707`; read in `camera_instance.cpp:647` |

  Forgetting a phone removes its alias and its name on purpose; re-pairing the same phone starts both again.
- **Proposal, by the Rig design's own test** ("does the fact say where a device is and what it is for, or how a
  stream was captured?"):
  - the alias is a label: the Rig's, not versioned, as the camera and sensor aliases are. It moves with F5 or not
    at all;
  - the torch setting and the capture profile are device configuration: they stay in settings;
  - `phoneNames` is the phone link's own memory of what a phone said: it stays with the phone link. Its row is
    the only stored link between a pairing's id and a phone's peer id, which F5's translation needs.
  No key is re-spelled, and none is removed by Stage E's clean-up of the Rig's copied keys.
- **Class: HARD: the Solution Architect.** The model's SET-09 names none of the four, so "when in doubt" applies:
  which of them is a domain fact, and where the alias lives (C3).
- **If nothing is ruled:** all four stay where they are.
- **Waits:** the alias's half of SF-5.
- **Ruled:**

## The host service (SF-1a, SF-1b, SF-4a to SF-4d)

### F7 · Q5 for the phone link: are the host service's six A6 findings in the rule?
- **Today.** The same question as Stage D's V2 and Stage E's H4, already before the Solution Architect[^sd0][^se0].
  One ruling answers all three. `ppcp_host_service.h` has 6 A6 findings: four list properties (`qrRows`,
  `outstandingCodes`, `phones`, `guidedWindows`) and two calls that return a map (`phoneHealth`, `ppcpStats`)
  (`grep -nE 'Q_PROPERTY\(QVariantList|Q_INVOKABLE QVariantMap' src/Ppcp/ppcp_host_service.h`). The allowlist
  describes the file as "a device / capture object's QML API".
- **Options.** (a) A6 narrowed to P9's wording, "a presenter's or a domain type's public API": the host service
  is neither, so its 6 findings leave the rule. (b) A6 on every header: they stay, and SF-4c types them.
- **Recommendation: (a)**, as in V2 and H4. **One line for the ruling to settle:** the model's close record lists
  `ppcpHost` among the device globals (§11's R-QML-1 row: `grep -n 'ppcpHost' docs/developer/information_model.md`),
  which reads as "not a presenter". Proposed: it is out of
  scope as it stands, and comes back into scope if a typed presenter is ever cut from it, because that would be a
  presenter by name.
- **What follows.** Under (a) the split closes no lint line (S1). `phones` is read in five QML files
  (`grep -rlE '\bppcpHost\.phones\b' src --include='*.qml' | wc -l`); if any of the six is worth typing whatever
  the ruling, it is that one, as a list model. Its cost is not measured.
- **Class: HARD: the Solution Architect.** A change to a lint rule's scope is an information-model change (C4,
  C9)[^imc].
- **If nothing is ruled:** A6 stays wide and the 6 findings stay on the allowlist, tagged `# unplanned`.
- **Waits:** SF-4c; S1's case.
- **Ruled:**

### S1 · The split's shape, if it is built at all
- **Today.** `PpcpHostService` builds on a headless host already: it holds one peer, engine and link for each
  phone (`ppcp_host_service.h:776–781`), and `makeHostEngine` is defined in `ppcp_host_engine.cpp:173` and reached
  through the peer (`ppcp_host_peer.cpp:558`) and the import controller. So the split should be an extraction of
  state the service keeps beside those objects, not a re-plumb. That is read from the code, not proven; the two
  inventories settle it[^planf].
- **Options.** (a) Split as planned: the link half, then the import and preview half, then a typed presenter
  (SF-4a to SF-4d, 11.5 h after 3 h of inventories). (b) Not now: the split goes to an issue, with the
  inventories.
- **Recommendation: (b), unless Q3 is answered "a main path".** Its reasons are in Q3. If it is built, two
  conditions keep it free of an information-model change: the presenter stays in `src/Ppcp` and `ppcpHost` keeps
  its name (G1). A presenter in another directory, or an interface that needs a ring, goes to the Solution
  Architect first (C9).
- **Touches pairings.** SF-4a moves the pairing and live-session state. It is parked behind the hands-on visit,
  and the pin of P1 is its first test.
- **Class: Coordinator's call**, within Q3's answer and under the two conditions above.
- **If nothing is ruled:** not built.
- **Waits:** SF-1a, SF-1b, SF-4a to SF-4d.
- **Ruled:** as recommended (the coordinator's call, 24 Nov 2026; `build/platform/JUDGEMENT-CALLS.md` PR-43, for the Product Owner's review).

### G1 · Q17 for the phone link: `ppcpHost` and `ppcpOffers` stay
- **Today.** The same question as Stage D's V1 and Stage E's H5, before the Product Owner[^sd0][^se0]. `ppcpHost`
  is used 16 times in 7 QML files, none of them a test
  (`grep -rnoE '\bppcpHost\.[A-Za-z_]+' src --include='*.qml' | wc -l`). `ppcpOffers` has one reader, behind a
  `typeof` guard (`src/Gui/home/PpcpOfferList.qml:42`). R-QML-1 allows devices as context globals[^imc], and the
  model's close record lists both names among them (the same §11 row as in F7).
- **Proposal.** Neither moves. The rule already allows them, and V1's reasoning holds: a renamed global moves a
  name, not an owner.
- **Hours.** None of its own: it removes the globals' part of SF-4d.
- **Class: Product Owner**, with V1 and H5.
- **If nothing is ruled:** neither moves.
- **Waits:** part of SF-4d.
- **Ruled:**

## The defects (SF-6a to SF-6g)

### N1 · Notification coalescing (#79) is already built
- **Today.** #79 asks for "a notification model with stable ids, coalescing and cause suppression, and a
  session-start preflight (one unmounted share once produced 10 identical toasts)". All four are in the code, from
  one commit of 1 Sep 2026 (`git log -1 --format='%ad %h' --date=short -S'Coalesce on' -- src/Gui/app/notification_center.h`:
  `f595eb9b`, "Ten failures were one toast showing the tenth…"). #79's list was gathered from notes on 21 Oct.
  - The model refuses a notification with no id, and a repeat of an id updates in place and adds to its count
    (`notification_center.cpp:130–141`; rules R1 to R4 in `notification_center.h:48–58`).
  - The host shows the count in the title, "(×N)" (`src/Gui/components/PpNotificationHost.qml:74`).
  - The preflight is at `main.cpp:1482`, and it raises the same id that later failures name as their cause.
  - `notification_center_test.cpp` exists. Read, not run.
- **Proposal.** No design-system change is proposed, so nothing goes to the Design Lead and no before and after
  picture is drawn. SF-6f becomes one check inside SF-6a: ten posts of one id give one card with "(×10)". If it
  passes, #79's line is ticked with that evidence.
- **One thing seen and not judged.** Six screens place a `PpToast` of their own
  (`grep -rlE '\bPpToast *\{' src/Gui --include='*.qml' | grep -v -E 'tests|components/'`). Those do not pass
  through the model, so they cannot coalesce. No fault is observed in any of them. If one is ever to change, that
  is a change of behaviour for the Design Lead[^ds].
- **Class: Coordinator's call** (closing an issue's line on evidence). **HARD: the Design Lead only if the check
  fails and a change to `PpNotificationHost`, `PpNotificationCard` or `PpToast` is then wanted.**
- **If nothing is ruled:** SF-6f is not built; the line stays unticked on #79.
- **Waits:** SF-6f.
- **Ruled:** as recommended (the coordinator's call, 24 Nov 2026; `build/platform/JUDGEMENT-CALLS.md` PR-43, for the Product Owner's review).

### D1 · Which of the listed defects are still defects
- **Today.** The plan lists five defects from #78 and #79, and the item plan adds #241[^plan][^planf]. Each was
  read against the code; none was run.

  | Defect | Item | As read on `b5b59cbf` |
  |---|---|---|
  | The "still processing the previous shot" message misleads | SF-6e | Looks answered. The refusal now names one of three reasons, each with its own sentence and id (`shot_controller.cpp:560–580`), in the code since 1 Sep. The line #79 cites, `:577`, is now the "saved session is open for review" case |
  | The wired preview channel drops about every 50 s | SF-6c | A cause is documented outside the app: macOS's "iPhone USB" tethering service resets the cable's whole connection, and turning that service off stops it (`docs/user/phone_capture.md:24–31`). Not observed on the cabin PC |
  | A cabled phone connects twice | SF-6c | Open. Nothing read that suppresses the Wi-Fi advert before the cable dials |
  | MP4 timestamps not rebased | SF-6d | Unclear. The filer rebases a clip's frame times on its own first frame (`ppcp_clip_filer.cpp:438`), but #79 names the container's timestamps |
  | A disabled camera still gets a preview | SF-6b | Open, as far as read |
  | #241: the GUI thread blocks 2.5 to 3 s | SF-6g | Open and not confirmed; its check is in P1 |

- **Proposal.** SF-6a runs first and gives each defect a verdict with evidence: reproduced on the test peer, not
  reproducible without phones, or already answered. A defect with no observed failure is not fixed on a
  guess[^evid]. If SF-6e's wording is ever changed, that sentence is one the golfer sees and goes to the Design
  Lead first.
- **Hours.** If SF-6a confirms the table: SF-6e (1 h) goes, and SF-6c (3 h) is about half. With N1 that is about
  5 h off path (a).
- **Class: Coordinator's call** (order and evidence; no C1–C11).
- **If nothing is ruled:** SF-6a is not run and the defects stay on #78, #79 and #241.
- **Waits:** SF-6b to SF-6e. SF-6e also waits for Stage D's SD-9c, which changes the states the message reports.
- **Ruled:** as recommended (the coordinator's call, 24 Nov 2026; `build/platform/JUDGEMENT-CALLS.md` PR-43, for the Product Owner's review).

## What touches the phone pairings

A pairing is the key under `ppcp/pairings/<pairing id>` in the settings file, and nothing else. A golfer who has
to pair a phone again has lost it. Everything in the three stages that can reach it:

| What | Where | The risk | The guard |
|---|---|---|---|
| The four phone-link statics move to the composition root | Stage E, SE-2a (on every path of Q3) | construction or teardown order changes; a pairing written late at quit could be cut short | P1's pin before SE-2a; Stage E's quit pins |
| The settings code is rewritten | Stage C (the Rig's keys) and Stage E (SE-5c, SE-5f) | a clean-up of "copied rig keys" sweeps a `ppcp/` key | F6: no `ppcp` key is in that list; the pin |
| The API keys move to a secure store | Stage E, K1 and K5 | the pairing key is moved with them and Windows keeps none | K5 recommends it stays; F4 asks that one ruling settle the Rig design's answer 8 too |
| The pairing store's lint line | F4, SF-3d | a new layer put in front of the key | F4's option (b) changes no stored thing |
| The link half leaves the service | S1, SF-4a | pairing state moved between classes | parked behind the visit; the conformance suite; the pin |
| The Rig lists paired phones | F5, SF-5 | two stores disagree; a key re-spelled | F5's option (b): derived, nothing copied, no key re-spelled |
| Going back to an older installer | every visit | a newer build's change that an older build cannot read | storage unchanged under every recommendation here |
| Tests and agent runs | every item | a test writes the real settings store | scratch settings only; the app only through the headless probe[^common] |

At each hands-on visit with a phone, latest main is installed first and a phone paired on the build before it
must still pair. Under (b) and (c) that check rides on visit 3.

## What the design must also answer

**The conformance suite** (`src/Ppcp/tests/run-conform.sh`, `docs/conformance/ppcp-conformance.md`) passes before
and after every code item, and its baseline is taken in SF-2.

**The design system.** No row changes a look or a behaviour (N1). An item that finds it needs a new component,
variant or state, or new wording a golfer sees, stops and asks the Design Lead[^hard].

**Already ruled, so not asked here.** The shared arbiter for every phone is Stage D's (SD-9c)[^pland]. Stage E's
composition root owns the phone link's four statics[^plane]. The Rig design is approved, with its answers 8 and
12 leaving the phone's part to this stage[^rig].

**The walkthrough deck.** If F1, F3 or F5 is ruled as recommended, check
`docs/developer/information_model_walkthrough.pptx` when the items land: it may no longer show who owns the phone
link's files, or the phone as a rig device, as built. The Product Owner regenerates it.

## Ownership (IM §10), as proposed

| New or changed thing (ids) | 1. Kind | 2. Owner, level | 3. Created / ended by; identity | 4. If derived | 5. Persisted where; the one writer | 6. Ring of each new file | 7. How a view reaches it |
|---|---|---|---|---|---|---|---|
| The import ledger (FILE-LIB-03) (F1) | source (import bookkeeping) | the phone link's ledger object; application | the first import; one per library | n/a | `ppcp-ledger.json`; the ledger, through a file door the composition root hands it | Adapters: the file door in `src/Export` | the offers list, through `ppcpOffers` |
| An imported clip before it is filed (FILE-LIB-04) (F1) | source (streams) | the phone link's import sink; application | the import sink; its capture id | n/a | `PPCP Imports/…`; the sink, through the same door | Adapters | not reached by a view |
| A filed phone clip | source | its `Shot`; shot | the filer, through `RecordingRepository::create` today and `Session::addShot` after Stage D's SD-8d; the shot's id | n/a | the shot's folder; capture once (R-REC-1), then the `Shot` | no new file | the card |
| The legacy ledger (FILE-LIB-05) (F2) | source (retired; read only) | the ledger object | never written again | n/a | read at load; no writer | no new file | never |
| A phone's annotations (FILE-SHOT-11) (F3) | source (a user artefact) | a peer cache of the phone link; not written by the app today | the phone; the protocol's ids | n/a | none in the app; the annotation store through F1's door, if ever switched on | Adapters (`src/Export`) | none today |
| A pairing's key (SET-09 `ppcp/pairings`) (F4) | application state (a credential) | the pairing store; application | pairing, forgetting; the pairing's id | n/a | the settings file; the pairing store (unchanged) | no new file | never: a view sees paired or not |
| Which phones are paired (F5) | derived | the Rig, read from the pairing store; application | n/a | the pairing store's ids; no rule version; pairing, forgetting | none of its own | Domain model (the Rig), the interface as the Rig design places it | `app.rig` |
| A phone camera's device key (F5) | an identity | the Rig; application | first seen; `peer_id + source_id` | n/a | as today's spelling, `ppcp:<peer>/<source>` | Vocabulary (the type has room) | `app.rig` |
| The studio's name (SET-09 `ppcp/studioName`) (F5) | source (rig) | the Rig; application | the coach | n/a | the Rig's file; the Rig | no new file | `app.rig` |
| The phone's alias; torch; capture profile; what the phone called itself (F6) | as F6 gives each | the Rig (alias); settings (torch, profile); the phone link (names) | unchanged | n/a | unchanged, but the alias, with F5 | no new file | unchanged |

Information-model change? **Yes**, if ruled as recommended: C1 and C4 (F1, F3), C4 (F2), C3, C4 and C5 (F5),
C3 (F6), C4 and C9 (F7). None under F4's option (b), Q3, V6, P1, G1, N1, D1, or S1 within its two conditions.
§11 is updated by SF-7b.

## What waits on what

| Row | Class | Items that wait |
|---|---|---|
| Q3 | Product Owner | every item but SF-2 |
| V6 | Product Owner (one decision with Q3) | SF-1a, SF-1b, SF-4a to SF-4d, SF-5, SF-6a to SF-6g, SF-7a |
| P1 | Product Owner | nothing; SE-2a should follow the pin |
| F1 | **HARD: the Solution Architect** | SF-3a, then SF-3b, SF-3c, SF-3e |
| F2 | **HARD: the Solution Architect** | SF-3b |
| F3 | **HARD: the Solution Architect** | SF-3c |
| F4 | the Solution Architect's question; HARD only under its option (a) | SF-3d |
| F5 | **HARD: the Solution Architect** | SF-5 |
| F6 | **HARD: the Solution Architect** | the alias's half of SF-5 |
| F7 | **HARD: the Solution Architect** (one ruling with V2 and H4) | SF-4c; S1's case |
| S1 | Coordinator, within Q3 | SF-1a, SF-1b, SF-4a to SF-4d |
| G1 | Product Owner (with V1 and H5) | part of SF-4d |
| N1 | Coordinator; the Design Lead only if the check fails | SF-6f |
| D1 | Coordinator | SF-6b to SF-6e |

Not waiting on any row: SF-2. SF-6g waits only for SF-2's check, and SF-7b for whatever is built.

**Effect on the item plan, if every row is ruled as recommended.** Stage F is its files: SF-2 (3 h), SF-3a
(3.5 h), SF-3b folded into it (F2), SF-3c (about 2 h under F3's option (b)), SF-3d (about 1 h under F4's option
(b), no longer parked), SF-3e with #208 (2.5 h), #241's fix if its check shows the block (2 h) and the close
(2 h). About **16 h** for 6 or 7 items, against 53 h for 23, with this page's 3 h already spent. No patch is
parked by this stage and visit 5 goes; SF-3e's lint half follows Stage D's SD-8d. Two issues are filed at the
ruling: the host service's split with its inventories; and the phone as a rig device, with the six `ppcp` keys.
The defects stay on #78, #79 and #241. The larger
effect is on risk: the items dropped are the ones that move pairing and live-session state, and no recommended
row changes anything a pairing is stored as.

[^hard]: `CLAUDE.md`: any information-model change needs the Solution Architect's approval, and any design-system change the Design Lead's, before it is built.
[^plan]: `docs/implementation/platform_refactor_assessment.md`, "Stage F · The phone link (PPCP)": its bullets marked "(proposed, for the Solution Architect)" and "(proposed, for the Design Lead)", its Closes, Test, Accept, Scratch cases and Cabin visit rows, and §9 Q3 ("How active is phone capture over the next six months?").
[^planf]: `build/platform/DEF/PLAN-F.md`, 24 Nov 2026: the items SF-0 to SF-7b, their order and hours, its six corrections, "Effort, at three sizes", and notes N2 (the parked set), N4 (a defect needs an observed failure) and N5 (SF-5 is the first to move if the stage is cut).
[^fore]: `build/platform/DEF/FORECAST.md`, 24 Nov 2026: levers 6 and 6b, the calendar levers ("Four become three"), and "Needs you" 1.
[^common]: `build/platform/COMMON.md`: "Loss is blocking: anything that can lose a shot, golfer input, a setting, a pairing or a key is blocking, however unusual the path"; and "Real stores and devices".
[^imc]: `docs/developer/information_model.md` §1.2 (C1–C11, from §10's template), §1.4 (the lint rules; a change of a rule's scope is an information-model change: DEC-120), §8.1 (FILE-LIB-03, -04, -05, FILE-SHOT-11, FILE-SHOT-12), §8.2 (SET-09), P9, P11, R-QML-1, R-QML-2, R-HL-2, R-REC-1, RING-ADAPTERS, DEC-187 and OBJ-RIG, read as excerpts. §11's R-QML-1 row, which lists the context globals at the close, was found by a grep for `ppcpHost`.
[^inc]: The method's rule: check the include graph before proposing a dependency rule.
[^sd0]: `docs/implementation/platform_d_capture_seam.md`: S2 (when the shot is allocated: `Session::addShot`, SD-8d), V1 (Q17) and V2 (Q5).
[^se0]: `docs/implementation/platform_e_host_settings.md`: H4 (Q5), H5 (Q17), K1 (the API keys' store) and K5 (Q7: the pairing key stays in settings).
[^pland]: `build/platform/DEF/PLAN-D.md`, 24 Nov 2026: SD-9c, "One arbiter for every phone (#78's two-phones case)", parked for visit 3.
[^plane]: `build/platform/DEF/PLAN-E.md`, 24 Nov 2026: SE-2a, "The composition root: one owning object, members in teardown order", function-local static services 7 → 0.
[^rig]: `docs/design/rig_design.md` (approved by the Solution Architect, 24 Nov 2026): §4.3 (`ppcp/studioName` and `ppcp/pairings` wait for Stage F; three more keys "not decided here"), §4.5 (a phone clip has no rig entry before Stage F; the phone-only document), §4.6 (a phone camera's key) and §9's answers 8 and 12.
[^r3]: `docs/implementation/platform_b1_placements.md`, R3: `pp_settings` is Platform (the Solution Architect, 24 Nov 2026).
[^rule10]: The plan's §5 rule 10: work that moves is listed where it lands, not carried by a tag alone.
[^evid]: The project's rule that a bug needs an observed failure, as `PLAN-F.md`'s note N4 applies it.
[^ds]: `docs/design/system/README.md`, "The non-negotiables": any change to the design system needs the Design Lead's approval before it is built. This page also sends to that role any row that changes what the golfer or coach sees.

[^q3r]: The Product Owner, 25 Nov 2026, in the platform refactor's coordination session.

[^p1r]: The Product Owner, 25 Nov 2026, in the platform refactor's coordination session.
