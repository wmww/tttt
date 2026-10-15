# Plan 71: hacks.md audit (one session)
Written by the insights of 2026-10-15. hacks.md grew 804 -> 908 lines in the sixteenth window, mostly from the suite's
last fan-outs. It mixes three things that need different treatment, and at 900 lines nobody reads it whole.

## What to do
1. Classify every entry (a sub-agent can do the first pass; check its verdicts on a sample):
   - **Note**: describes how something works and isn't wrong (a design choice, a tsgo rule ported faithfully). Move
     it to documentation.md (or delete it if the code comment already says it).
   - **Fidelity to a tsgo artifact for one or two tests** (runaway models, cache collisions, order emulation). For
     each, run the suite and the oracles with it removed (or reduced to the general rule): if only the tests it was
     written for move, and they're types/symbols-only or argued-shaped, prefer deleting it and arguing the tests in
     bugs/tsgo_divergences.md. Keep it if real code (corpora, sweeps, flagflip) needs it.
   - **Debt**: a real approximation of a rule. Leave it, but make sure each one says what the correct rule is and
     how it would be found wrong (the oracle that would show it).
2. Two specific questions to answer in the entry itself:
   - "Which declarations make one member symbol" has four copies (class_decl.c, MembersBuilder, check.c's
     MemberTables, the symbols baseline's computed_member_symbol_items). Consolidate if it fits the session, else size
     it and file a plan.
   - "Two passes where tsc has one" (relation reporting): is one reporting run (tsc's shape) a plan worth writing?
     Measure what the second run costs (callgrind, the corpora) and how many hacks/patches hang off it.
3. Target: hacks.md under ~600 lines, every remaining entry debt or a deliberate artifact model with its reason.

## Validation
Suite 0 lost except tests explicitly argued in tsgo_divergences.md (each listed in the commit message), every oracle
unchanged (`scripts/oracles.py`), instructions flat or better.
