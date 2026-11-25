# Product controls: diagnostics and coaching

What the product must do with faults, misses, tendencies and the words a golfer reads: the Coaching Lead's domain ([`roles.md`](../../roles.md)). The approval controls for norms and edges, DC-1 and DC-2, are in [`decide.md`](../decide.md). Each control is stated once, in the first stage where it applies; later stages list it by id. A listed id marked "(agents)" binds only agents. The control format and the terms are defined in [`README.md`](../../README.md).

## Already stated elsewhere

- There are no warm-up shots: every shot counts in full in every reduction (`docs/developer/information_model.md` §2; `docs/design/session_diagnostics_design.md` §5.4).
- Recorded intent never excuses, suppresses or re-grades a finding (`information_model.md` §2).
- Diagnostics have three levels, shot, session and athlete, each owned at its own level (`information_model.md` §7.1).
- A shot's tendency has three factors (`docs/design/shot_outcomes_design.md` §3.8, §7.0; `docs/design/ball-flight-classification.md`).
- A condition's "draft" state gates nothing; only `reportHold` keeps a condition out of a report (`docs/developer/diagnostics_developer_guide.md`, `detect()`; `docs/design/session_report_design.md` §5).
- The diagnostic model is complete ahead of its producers on purpose: never report a model item as deficient for lacking a producer, a norm or a signal (`docs/design/swing_fault_ontology.md`, "What the prevalence field made visible on day one").
- Diagnostics content never cites a commercial organisation, product, certification body or brand (`docs/design/diagnostics_content_extension.md`, "Provenance discipline"). *Check:* `core_pack_test`, `diagnostics_catalogue_integrity_test`.
- The kinematic sequence is judged relative to impact: out of order and peaking after impact are both sharp findings (`docs/design/trunk_imu_sequence_grf_design.md` §10.9).
- How work-ons rank (`docs/design/work_ons_design.md`).
- A coaching term keeps its established meaning ("slot" is a downswing term; `docs/design/plane_stack_design.md` §3.3).

## Controls

**DC-3.** Miss families must not be ordered or weighted against each other in anything that counts or chooses (the usual miss, the shot mix, the primary miss, a club's mix): strike, direction and flight misses are equally penal.
Which miss leads is decided by how often the golfer hits it. Word order in a phrase ("fat slice") is only English.
*Why:* a fat shot is as penal as a push, and for low-handicap golfers pushes and pulls are the commoner miss; an unreviewed strike-first default hid them behind thin strikes.[^dc-3]
*Owner:* Coaching Lead · *Stage:* Build; checked at Review · *Compliance:* a review

**DC-4.** A fault's firing edge, never its amber band edge, must be quoted as the line where it fires; the firing thresholds are the grading policy (`docs/developer/diagnostics_developer_guide.md` §5).
Correlations of camera readings between sessions are camera artefacts: rank within a session.
*Why:* an agent told the Coaching Lead the amber edges were the firing edges.[^dc-4]
*Owner:* Coaching Lead · *Stage:* Build; checked at Review · *Compliance:* a review

**DC-5.** Authored weights in the diagnostics model must lie in 0..1, so they compose along causal paths.
"It preserves today's behaviour" decides nothing until its cost is measured; say plainly what such a number still is not (it is not a probability).
*Why:* a weight outside 0..1 can't be multiplied or chained without renormalising; measuring the cost of a rescale changed its design.[^dc-5]
*Owner:* Solution Architect · *Stage:* Build; checked at Review · *Compliance:* a review

**DC-6.** New readings must grade faults straight away, with σ and their warnings, never withheld; but no blanket "not yet validated" warning may go on every reading or fault row.
*Why:* the readings must be producing to be verified in use, with trust shown so nobody takes them as final; a blanket warning would leave the app "awash with warning signs".[^dc-6]
*Owner:* Coaching Lead · *Stage:* Build; checked at Review · *Compliance:* a review

**DC-8.** A summary surface must report what its chart shows, plainly, with every number; for the kinematic sequence: each peak as the maximum before P8, in ms from impact, with σ on every number, and the expected order beside the actual one.
A peak after impact is a finding, not a failure. When a role states the rule, implement exactly that, with no edge cases it didn't ask for.
*Why:* three review rounds hid values, dropped σ and invented a trim the rule didn't ask for.[^dc-8]
*Owner:* Coaching Lead · *Stage:* Build; checked at Review · *Compliance:* a review

**DC-9.** Every instruction to the golfer must use the swing's own terms (address, backswing, top, downswing, follow-through, finish), never design vocabulary ("turn away", "turn through").
Where a direction matters, the avatar shows it unmistakably before the golfer moves.
*Why:* abstract words left the golfer guessing which way to turn, and a wrong-way verdict then reads as the app being broken.[^dc-9]
*Owner:* Coaching Lead · *Stage:* Build; checked at Review · *Compliance:* a review

**DC-10.** Alignment must never be graded from a face-on reading: aim is rotation about the vertical axis and is not measurable from the face-on camera.
A corridor that fires on competent golfers by construction is a model defect.
*Why:* an independent read of a session's video against the ledger found a face-on tilt graded as aim, and a corridor that fires on a normal release.[^dc-10]
*Owner:* Coaching Lead · *Stage:* Build; checked at Review · *Compliance:* a review

**DC-11.** A ruling that a concept doesn't exist must apply on every surface, not only the one it came up on.
*Why:* the "no warm-up shots" ruling was written up as one surface's exception and left in the code elsewhere.[^dc-11]
*Owner:* Coaching Lead · *Stage:* Build; checked at Review · *Compliance:* a review

**DC-12.** A condition's draft state must never be described as a safeguard: draft gates nothing.
*Why:* an agent said new conditions "stay draft" as if that held them back, and they became a golfer's first two work-ons.[^dc-12]
*Owner:* Coaching Lead · *Stage:* Build; checked at Review · *Compliance:* a review

**DC-13.** A commercial coaching programme, organisation or brand must not be named in diagnostics content or anything a golfer reads; internal documents may cite where a number came from.
The metric catalogue's golfer-facing text is cleaned of such names.
*Why:* a reference shape is an exemplar, not an endorsement; internal docs still need their sources.[^dc-13]
*Owner:* Coaching Lead · *Stage:* Build; checked at Review · *Compliance:* `core_pack_test`, `diagnostics_catalogue_integrity_test` (diagnostics content); a review elsewhere

**DC-14.** A measure that nothing reads yet must never be treated as a candidate for deletion:
the model is the specification of what producers to build.
*Why:* the diagnostic model is complete ahead of its producers on purpose.[^om-13]
*Owner:* Product Owner · *Stage:* Build; checked at Review · *Compliance:* a review

**DC-15.** A reference's bibliographic fields must be re-resolved from the source, never filled in from memory.
*Why:* a remembered citation can be wrong in every field.[^om-13]
*Owner:* Product Owner · *Stage:* Build; checked at Review · *Compliance:* a review

[^dc-3]: Coaching Lead, 10 Nov 2026.
[^dc-4]: Coaching Lead, 13 Nov 2026 (the plane stack review).
[^dc-5]: Solution Architect, 2 Aug 2026.
[^dc-6]: Coaching Lead, 12–13 Nov 2026 (the planned-metrics promotion).
[^dc-8]: Coaching Lead, 2 Oct 2026.
[^dc-9]: Coaching Lead, 27 Oct 2026.
[^dc-10]: Coaching Lead, 14 Sep 2026 (the independent video review).
[^dc-11]: Coaching Lead, 9 Nov 2026.
[^dc-12]: Coaching Lead, 22 Oct 2026.
[^dc-13]: Coaching Lead, 25 Oct 2026, ruled in scope 25 Nov 2026 (#195, decision D9).
[^om-13]: Product Owner, 25 Nov 2026 (#195, OM-13).
