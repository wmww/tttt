# Todo
Written by the insights of 2026-10-11 (after the thirteenth window); `vision.md` "Course correction" says why. **Ten
sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the numbers). A plan
that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run (count the commits
since this file was written), the next session is insights, whatever is left here. New work goes into plans or
triage.md, not ahead of the insights entry. Every entry ends with `scripts/fasttest.sh -s`, `scripts/oracles.py`
(every oracle) after checker changes, `scripts/instrcount.sh` and a commit.

Starting point: suite **13316 / 13571** (98.1%), corpora 0 spurious (missing: webpack 5, drizzle 3, vue 3; effect 3
spurious), DT sweep 8592 / 8595, `$ExpectType` 68631 / 68640, flagflip 132 / 16 / 6, scenarios 209 / 213, source
sweep 3119 / 3363. Instructions: measurements.md "Triage (2026-10-09..11)".

**This window is perf-first** (the project's actual goal, unmeasured at breadth since the 7.1 switch). Wall times on
this 4-vCPU box are only comparable within one session with nothing else running; type counts and callgrind
instructions are the stable numbers.

1. ~~Plan 59 stage 1~~ done: the gate (tiptap 3.9 -> 0.2 s), `scripts/perfsweep.py` (1779 projects tsgo checks:
   tttt K1's median check time 0.21x tsgo's, types 0.76x, RSS 0.71x; worst types ratios date-fns/pkgs/docs 16.4,
   feathers typebox 7.0, nitro 5.2, rxjs 2.6). measurements.md "Perf at breadth".
2. ~~Plan 59 stage 2~~ done (plan 59 deleted): ~20 fixes from four agents, all tsgo caches/lazinesses (types
   keyed by the whole mapper, eager intersection props/erased/canonical signatures, recursion identity, message
   truncation). perfsweep: worst types ratio 16.4 -> 1.55, total 96.0M -> 71.3M (tsgo 127.9M), no project above
   tsgo's RSS (was 28); drizzle -17%, xstate -9% instructions; suite 13317. Check time median 0.21 -> 0.16x tsgo.
3. ~~Planning: plan 60~~ done: plans/60_memory.md (where every corpus' peak goes; vscode: AST 2.17 GB, binder
   0.6, checker malloc 0.45 (LazyMemo pages, intern tables), arena 1.8). Landed the big lever: tuple targets share
   element type parameters and element props a read-only declaration: type-fest K1 2057 -> 797 MB RSS (Bun 835),
   every oracle unchanged. Bun's remaining 1.5-3x is representation (compact AST): not worth chasing; plan 60 aims -20%.
4. ~~Plan 60 stage 1~~ done: LazyMemo 24 -> 12 bytes (type ids), node links (two-word keys inline), CallResolution,
   PropDecl, Members rare parts, interned lists without copies. vscode K1 5189 -> 4839 MB, K4 5966 -> 5438 (targets 4.7
   / 5.3 not quite), every corpus -1..-11%, instructions flat. Leftovers with reasons in plan 60.
5. ~~Plan 60 stage 2 + perf session~~ done: Span 24 -> 16 (file table), Expr 88 -> 72; vscode K1 4839 -> 4387 MB, K4
   5438 -> 4992 (plan 60 total -15%/-16%), every corpus -2..-9%; load+parse target (1750) not reached, leftovers in plan
   60. Threads uncapped (checkers stay 4, vision.md). bench.sh re-baselined: tttt 2.6-10.6x tsgo at defaults, 2.0-6.1x
   single-threaded, 0.4-0.8x its RSS (measurements.md "Bench on tsgo 7.1").
6. ~~Source sweep fan-out~~ done: 4 agents, ~85 picks; source sweep 3132 -> 3293 / 3363 (repos fully matching 93 ->
   129), every other oracle unchanged, suite 13318; instructions +0.0..+0.8% (valibot -2.0%). Leftovers per agent in
   bugs/sourcesweep_checker.md; plans/61_constraint_functions.md written (stage 1 derisked: +1 suite test). triage.md:
   the lib-file-checking decision. measurements.md "Source sweep fan-out".
7. ~~Plan 55 stage 1-2~~ done, with most of stage 3: `src/ty/pseudo.c` (tsgo's pseudochecker), printed where
   equivalent at all of tsgo's serializeTypeForDeclaration/serializeReturnTypeForSignature sites (object literal
   members, parameters, return types, namespace variables; messages too), the display/expr.c slices it replaced
   deleted. Suite 13318 -> 13328, every other oracle unchanged. plans/55 says what's left.
8. ~~Plan 55 stage 1b~~ done: isolatedDeclarations reported from the pseudo tree (tsgo's ReportInferenceFallback and
   createGetIsolatedDeclarationErrors), TS 6.0's builder deleted. Suite 13328 -> 13330; flipped-flag probe against tsgo
   on 400 source sweep projects 4944/7352 -> 568/1 missing/spurious; other oracles unchanged; tsc corpus +1.8% instr.
9. **Suite/JS tail fan-out**: four Opus sub-agents on the 255 failing tests (`scripts/failstats.py`,
   `scripts/jsstats.py`, `scripts/displaydiff.py` buckets; bugs/js.md, display.md, missing_checks.md), flagflip's 132
   missing, the DT sweep's 3. Mechanisms only, no per-test special cases.
10. **Triage.** Also: a build with the newest GCC and clang (`-Werror` off, warnings counted) since GCC 16 found a
    false positive; readme.md's numbers.
11. **Insights.**

After the insights entry (for the next window): **plan 58** (re-pin to TypeScript 7.1.0 stable, once it's on npm,
expected after 2026-11-24); **plan 61** (constraint functions, three stages; mikro-orm's
last TS2345s and the base-vs-immediate distributive constraint hack); plan 56 (content mappers) only if a corpus or sweep starts using them.
