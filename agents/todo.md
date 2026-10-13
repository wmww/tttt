# Todo
Written by the insights of 2026-10-13 (after the fourteenth window); `vision.md` "Course correction" says why. **Ten
sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the numbers). A plan
that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run (count the commits
since this file was written), the next session is insights, whatever is left here. New work goes into plans or
triage.md, not ahead of the insights entry. Every entry ends with `scripts/fasttest.sh -s`, `scripts/oracles.py`
(every oracle) after checker changes, `scripts/instrcount.sh` and a commit. **If TypeScript 7.1.0 stable is on npm
when an entry starts, do plan 58 (the re-pin) first** and push the rest down (it takes one entry's place).

Starting point: suite **13458 / 13583** (tsgo's 13440 / 13568 + 18 own tests), corpora 0 spurious (missing: webpack 1,
vue 3; effect 3 spurious), DT sweep 8594 / 8595, `$ExpectType` 68631 / 68640, flagflip 46 / 8 / 3, scenarios 209 / 213,
source sweep 3307 / 3363. perfsweep: types median 0.66x tsgo, worst 1.56, RSS never above tsgo's. Instructions:
measurements.md "Triage (2026-10-13)".

**This window: scale and memory against bun, plus the last core-rule plan.** Single-threaded we are 2-6x tsgo; what we
lose is many cores (checking stops at 4 checkers) and memory (bun 2-3x below us). Correctness keeps one fan-out and
plan 61.

1. **Planning: plan 62, checking that scales with cores.** The question: can the checker count stop deciding the
   output? Measure first: `scripts/checkerdiff.py -k 8` and `-k 16` (and the suite's `--checkers K --dump`) against
   K=4 on every corpus, the sweep and a source sweep sample; classify every difference (union order from type-id ties,
   once-per-program diagnostics, anything else). Read how tsgo 7.1 itself behaves (does its output depend on its
   checker count? `--checkers` on tsgo against its default on the corpora). Then decide: (a) output independent of K
   (finish `ty/order.c`'s id tie-breaks) so K can follow cores, (b) keep K=4 and get parallelism inside a checker's
   work elsewhere (balance: deal_files()' cost model; serial phases: declare/link, perf_leads.md "Parallelism"), or
   (c) neither is worth it. Include the K4 arena duplication (+17..55%) since more checkers multiply it. Write plan 62
   with stages and validation, or a "no" with the numbers in vision.md. Prototype where it's cheap.
2. **Plan 62 stage 1** (or, if plan 62 said no, plan 61 stage 3 moves up and the rest shifts).
3. **Plan 61 stages 1-2** (constraint functions: getConstraintOfType / getBaseConstraintOfType, the callers migrated;
   mikro-orm's TS2345s). The hacks it names are deleted, not left next to the new functions.
4. **Plan 61 stage 3** and its hacks.md entries closed.
5. **Planning: plan 63, the AST's allocation.** vscode's AST is 1.9 GB after parsing in ~10M malloc'd boxes (half their
   words zero); bun's whole run is 1.6-2.1 GB. Measure what malloc itself costs (headers, size-class rounding: memtrack
   and a counting LD_PRELOAD or glibc's `mallinfo2`), then plan per-file bump arenas for the parser (they'd also delete
   `reexec_with_huge_pages` and `shrink_list()`'s realloc, hacks.md) and the remaining struct leftovers of plan 60
   stage 2 (IdentifierExpr, PropertyExpr, CallExpr rare parts) and stage 3 (binder). Target and validation in the plan;
   if the measurement says malloc overhead is small, say so and plan only the struct work.
6. **Plan 63 stage 1.**
7. **Fan-out: source sweep + suite tail** (4 agents, like entries 6 and 9 of the last window; disjoint lists, 0 lost at
   merge, full source sweep match-set diff before and after). bugs/sourcesweep_checker.md and the failing suite tests.
8. **Perf session**: perfsweep and bench re-run (wall times only with nothing else running), instruction creep since
   this file was written bisected if any corpus is up more than 1%, the worst types ratios (perf_leads.md).
9. **Planning: plan 64, scope `.d.ts` emit.** The human left it to us (vision.md). tttt now has tsgo's pseudochecker
   (plan 55) and declaration diagnostics (plan 40): what does tsgo's declaration emitter need beyond them (the
   transformer, the printer, `--emitDeclarationOnly`/`--declarationDir`/maps, `-b` writing outputs)? Size it against
   the codebase, find an oracle (the suite's `.js` baselines carry the emitted `.d.ts`:
   `thirdparty/typescript/tsc/testdata/baselines/reference/**/*.js`, 13k files),
   and recommend yes/no/when. Don't build it in this entry.
10. **Triage.**
11. **Insights.**

After the insights entry (for the next window): plan 58 if not done; plan 62/63's later stages; plan 56 (content
mappers) only if a corpus or sweep starts using them.
