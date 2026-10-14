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

1. Done (2026-10-13): plan 62 written, decision (a). tsgo's output doesn't depend on its checker count; tttt's is
   identical at K=16 on the suite, corpora and DT sweep, but 19 / 3363 source sweep projects differ (13 already at
   the default K=4, trpc +201 spurious): bugs, not a design limit. tsgo 7.1's FENNEL partitioner prototyped: worse
   than `deal_files()`. `scripts/kdiff.py` added.
2. Done (2026-10-13): plan 62 stage 1. kdiff 19 -> 5 projects at K=16 (2 at K=4: inbox-zero's known
   undefinedProperties order and redux-toolkit's display), sweeps at the default K (source 3309, DT 8594), suite 13460.
   mermaid left for stage 2 (bugs/type_core.md).
3. Done (2026-10-13): plan 61 stages 1-2. base_constraint() is getResolvedBaseConstraint (circularity, identity
   stack, cached at every depth), one immediate distributive constraint, keyof target steps push the target only.
   Suite 13463 (+1 tsc, +2 own), oracles unchanged, mikro-orm/tests' TS2345/TS2322/TS2615 gone, react +0.5% instr.
4. Done (2026-10-13): plan 61 stage 3, plan finished and deleted. computeBaseConstraint ported literally (its
   getNextBaseConstraint depth accounting was what made tttt call FromSchema circular), one getConstraintOfType with
   every gate, TS2313 from the base constraint (members.c's walk deleted), apparent types through it. Suite 13464 (+1
   own), oracles unchanged, source sweep sample: socket.io fixed. Left in hacks.md: conditional simplification and the
   relation's conditional gate (both reach things tttt resolves eagerly), triage.md has an order dependence.
5. Done (2026-10-14): plan 63 written (plan 60 folded in and deleted). malloc's per-block overhead is ~5% (vscode
   105 MB of 2.0 GB, no fragmentation), half the AST's words are zero: stage 1 is the struct diet (PropertyExpr,
   CallExpr, InitializedBinding, import/export, IdentifierExpr, binder), stage 2 per-file arenas as a time lever
   (malloc is 11% of parsing's instructions) behind an audit. Side find: `jsdoc_before_ex` is 3.9% of webpack's run.
6. Done (2026-10-14): plan 63 stage 1, targets missed. vscode K1 resident after parse / names / peak 1917 / 2517 /
   4403 -> **1724 / 2277 / 4158** MB (targets 1650 / 2150 / 4000), other corpora -3..-5%, instructions vscode -1.2% (react/nest +0.6/0.8%: boxed lib annotations)
   (PropertyExpr's dot name is a Span, rare parts for calls, object items, imports, module refs, boxed binding
   annotations, Scope 96 bytes). What's left, led by Expr 72 -> 64 (an IdentifierExpr design), is plan 63 "Stage 1b";
   stage 2 (arenas) is unaffected.
7. Done (2026-10-14): fan-out, 71 picks, 0 lost. Suite 13464 -> 13515, source sweep 3310 -> 3332 (0 lost),
   `$ExpectType` 68637, flagflip 42 / 6 / 3, scenarios 210 / 0 failed. Plan 65 written (the TS5115 bucket, not done).
   Instructions +0.2..0.6% (type-fest +1.2%, accepted): measurements.md.
8. **Perf session**: perfsweep and bench re-run (wall times only with nothing else running), instruction creep since
   this file was written bisected if any corpus is up more than 1%, the worst types ratios, `jsdoc_before_ex`
   (perf_leads.md "Time").
9. **Planning: plan 64, scope `.d.ts` emit.** The human left it to us (vision.md). tttt now has tsgo's pseudochecker
   (plan 55) and declaration diagnostics (plan 40): what does tsgo's declaration emitter need beyond them (the
   transformer, the printer, `--emitDeclarationOnly`/`--declarationDir`/maps, `-b` writing outputs)? Size it against
   the codebase, find an oracle (the suite's `.js` baselines carry the emitted `.d.ts`:
   `thirdparty/typescript/tsc/testdata/baselines/reference/**/*.js`, 13k files),
   and recommend yes/no/when. Don't build it in this entry.
10. **Triage.**
11. **Insights.**

After the insights entry (for the next window): plan 58 if not done; plan 62/63's later stages (63 stage 2: arenas); plan 65 (the TS5115 eager-base runaway, 13 sweep projects); plan 56 (content
mappers) only if a corpus or sweep starts using them.
