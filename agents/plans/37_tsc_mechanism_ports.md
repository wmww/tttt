# Plan 37: port the tsc mechanisms the long tail keeps working around
Written by the insights of 2026-09-28 as a brief for a **planning session**; that session replaces this file's
"Candidates" with ranked, derisked sub-plans (in this file or `37a_*.md`...).

## Why
Four windows of small-fix sessions have moved the suite from 8202 to 10215. More and more of those sessions end
with "the real fix is tsc's X, which tttt doesn't have", and add another approximation around X (hacks.md "Type
core", "Calls and inference"). Each approximation costs a little every time a later fix runs into it, and some
block whole buckets outright. Plans 29-30 (deferred references, substitution types) showed that porting a named
tsc mechanism in place pays: suite up, corpus counts down, approximations deleted.

## Candidates (unranked; the planning session measures each)
- **Missing type** (tsc's `missingType`, exactOptionalPropertyTypes): `bugs/type_core.md` "exactOptionalPropertyTypes"
  (23 configurations, TS2375/TS2379/TS2412 absent, zod-root's one missing diagnostic), hacks.md "No missing type",
  `exact_missing_flag()`. Also optional tuple element display (`string?`) and `'a' in x` narrowing.
- **Non-widening null/undefined** (tsc's `undefinedWideningType`/`nullWideningType`, non-strict only): plan 35 queue
  item 1 (~25 tests, 11 alone), the `widen_nullable()` HACK, inference widening.
- **Evolving arrays and `let x;`** (tsc's `autoArrayType`/`autoType` with flow): HACKs in `binding.c`, `ty/flow.c`,
  `ty/expr.c` (grep "evolving"). ~100 failing tests contain `let x = [];` / `let x;` (an upper bound, not the count).
- **Decorators checked as calls** (tsc's `checkDecorator`, getEffectiveCallArguments for decorators, TS1238/TS1241/
  TS1270, contextual types of decorator arguments): plan 35 queue item 2; 52 failing tests use decorators; nest
  (the decorator-heavy corpus) can't show what isn't checked.
- **Union and keyof origins** (display: `bugs/display.md`; also the relation budget's HACK in hacks.md).
- **Context-free return-only signatures** for sensitive arguments (`bugs/type_core.md` "Inference": the
  `SkipContextSensitive` first round) and **fixing without candidates** (call.c's gate).
- **Lazy reverse mapped types** (eager, cut at depth 4 today), **permissive instantiation** leftovers,
  **intersection reduction by constraint** (`bugs/type_core.md` "Relations").

## What the planning session does
1. For each candidate: which tests (and corpus diagnostics) it blocks, measured, not estimated from greps (e.g.
   prototype the rule, or list tests whose diff mentions it); which HACKs it deletes; what it risks (the relation
   and inference paths it touches, perf: check zod/valibot instructions).
2. Rank by (tests + corpus + hacks deleted) / (sessions x risk). Prototype the top two or three far enough to know
   the shape (the plan 36 approach: measurement code, thrown away).
3. Write a sub-plan for each of the top ones with stages and validation (suite no losses, corpora unchanged or
   better, instruction counts on zod/valibot/tsc), and a short verdict line for the ones not worth it now.
4. Update todo.md's entries 4-5 to name the chosen ports.
