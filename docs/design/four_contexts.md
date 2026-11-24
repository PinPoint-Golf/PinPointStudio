# PinPoint Studio: the four contexts (retired)

**Status:** **retired.** Ruled on 24 Nov 2026 by the Solution Architect and the Product Owner (L1–L8), and merged
into `docs/developer/information_model.md` v1.10.0: §2 (the four contexts, R-CTX-1, the results contract), §4 (the
Rules row and the Coaching folder), §4.1 (the Context column), §1.4 (lint A9) and the glossary (GL-37, GL-43, GL-69,
GL-70, GL-71). The information model is canonical; read it, not this page.

The platform refactor's part (Stage B's moves, renames and lint A9; L6's libraries by ring; the three guards for
Stages C and D) is in `docs/implementation/platform_refactor_assessment.md`, "Rulings of 24 Nov".

**AM11's wording call** is ruled by the Design Lead (24 Nov 2026): until the coaching track builds adoption, the
panel that shows the session's priorities says "Priorities", not "Work-ons". The heading changes in code with the
priorities' type renames at Stage B, Part 1.

**L7 (AM7, the packs)** is in the information model's §2, "The packs": drills are Coaching's intervention library,
screens stay in Diagnostics, the link from a condition to its drill moves to Coaching, and the cross-pack health check
moves to `RuleData`'s level.

The full ruled text, with the amendments AM1–AM11 and the context table, is in commit `898b4892` (this file before
its retirement).
