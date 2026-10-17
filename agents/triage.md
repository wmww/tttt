# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-17 (triage session, todo entry 10 of the eighteenth window): the summary/passcount totals
disagreed over three "did not run" tests. Two (checkJsFiles6, jsFileCompilationWithoutJsExtensions: a `.js` root
without allowJs, nothing in the program) have no symbols/types baselines because tsgo writes none for NoContent; the
harness now runs them and renders nothing there. instanceofOperator is UTF-16: the collector now decodes it before
reading its `@target` variations. All three pass; passcount.py counts undefined components as failing and a filtered
run's "total" is the filtered count. The 24-thread machine note was already in measurements.md ("Machine and
caveats", the 12-core sections); this session ran on the 4-vCPU box again (`nproc` 4).

- sourcesweep.py's "slower than tsc" list trusts tsc's cached time, which is wrong for a project with a committed
  `.tsbuildinfo` (`incremental: true`, outDir checked in): webcmd's `dist/tsconfig.tsbuildinfo` makes tsc skip checking
  (85 types, check 0.000 s; 0.95 s with `--incremental false`, tttt 0.34 s). perfsweep measures cold; the sweep's
  oracle (tttt-oracle) should too, or the list should drop projects whose tsc side built < 1000 types.
- **A project reached through a symlinked directory loads files twice** (plan 81 planning, 2026-10-17): in a worktree
  whose `thirdparty/corpora` is a symlink to the main checkout (CLAUDE.md's worktree recipe), `tttt --noEmit -p
  thirdparty/corpora/effect/packages/effect/tsconfig.corpus.json` lists 303 effect files under the symlink's target
  (`/home/ai/tttt/...`) next to the same 950 under the worktree path: 3165 errors, 14 MB of checked source instead of 9
  MB. tsc 7 run the same way gets 5 such files and 5 errors (its own). Some resolution (workspace package
  self-reference, `paths`, or `node_modules/effect` -> realpath) realpaths where tsc keeps the symlink path or maps
  it back onto the root file. A user with a symlinked project directory sees the same thing. Also check whether
  sub-agents' worktree oracle runs (realworld.py on effect, the sweeps) were affected; running with absolute
  main-checkout paths avoids it.
- **inbox-zero (and its apps/web), webcmd, solid are slower than bun at the defaults** (2026-10-17, plan 81 stage 1):
  inbox-zero 1.45 s vs bun 0.86 at 4 checkers, already before stage 1's budget (not recycles). Find where its K4
  time goes (`TTTT_FILE_TIMES=1`, per-checker times in `--time`): checker balance (plan 72) or a per-checker warm-up.
  2026-10-17 (work stealing): not balance (4 checkers at 382-384 ms each, still 1.51 s). apps/web **restarts without
  deferral** (`TTTT_DEFER_STATS=1`: "signature collided 1", "restarts 1"; `--time` shows load+parse and resolve
  twice, and typecheck 754 ms against 384 per checker): one deferred body's lookup (`function_impl_lookup_deferred_body()`
  in `deferred_bodies_materialize`) failed and the whole program is redone. Find the body (plan 78's collision case)
  and why its signature's walk saw its names; that is likely ~0.5 s of the 1.5.
- **TS5115/TS2589 depend on the checker's history** (2026-10-17, plan 81 stage 1): the instantiation depth limit
  reports where a checker first hits it, then the error type is cached (tsgo's `instantiateTypeWithAlias` does the
  same). Each fresh checker reports it again: flagflip's xstate sloppy/nosnc cells show 1 at K1 (tsc's 1), 2 at the
  old 96 MB defaults, 3 at stage 1's 16 MB (State.ts:395, graph/utils.ts:91, test/meta.test.ts:80; `--checker-memory
  0`: 5). kdiff/recycle don't see it (default flags). A fix would make the first report program-wide (an atomic
  "reported" per circular type set, kept across recycles), which still depends on which checker gets there first;
  or accept it as tsgo's own K-dependence (tsgo at 4 checkers can do the same). Since work stealing (2026-10-17) which
  checker gets there first depends on timing: flagflip's spurious count flickers 8 / 9 between identical runs.
- **The harness's leak detector reports 157 tests since it defers bodies** (found in the hacks audit, 2026-10-17;
  bisected to 26914074, "Harness defers function bodies by default"; 0 with `baseline_tests --no-defer-bodies`): a
  materialized body's AST is in an arena dropped without a walk, so what its parse malloc'd outside the arena leaks
  (valgrind on aliasBug: `parse_identifier_type`'s `vec_span` of a qualified type name, parser.c; also the cooked
  strings hacks.md's deferred-bodies entry counts). Real cost is small, but the "N net allocations!" list now hides any
  new leak. Fix the class (those vecs/strings from the AST arena, plan 80's direction) or, cheaper, free the bodies
  with a walk at program teardown in debug-allocator builds only and keep the drop walk-free.
