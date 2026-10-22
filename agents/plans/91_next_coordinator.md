# Plan 91: the fourth coordinator session (written 2026-10-21/22 by the third coordinator)

Read `agents/coordinator.md` (how to run a window), then CLAUDE.md, plan 89 §1 (the workflow, still verbatim), then
this file, then `agents/vision.md`. Plan 90 is superseded: its mandate (the shared graph on by default) was withdrawn
by the human on 2026-10-21 after the laptop bench (measurements.md "at 5f2cd699") showed no wall win on the corpora.

## 1. What the third session decided and did (2026-10-21/22; `git log ee9501eb8..`)
**The human's mandate (2026-10-21):** a fully correct and faster checker as soon as possible; 1.5x on an ordinary
big project (vscode) would be a big deal; multiples are not required; a cache across runs is out of scope; the shared
graph only with a genuinely new insight; parallel workers are fine, waste is not.
- **The shared graph is parked**, in the tree behind `TTTT_SHARED_GRAPH=1`, off by default. One diagnosis session
  (worker sg) wrote plan 86 "Stage 4f: the perf accounting (2026-10-21)": at K12 the flag makes each instruction 1.2-1.6x
  more expensive than plain (inferred: cache lines written by one core and read by others; this VM has no counters),
  which outweighs the duplicated work it saves on ordinary projects; what extra K12 work remains is mostly variances
  re-measured per worker after deep-nesting cuts taint their groups (correctly, by tsc's semantics) plus the sharing
  machinery itself (43-61% of the extra; 4.3 G on vscode); races under 1%; the shared relation cache doesn't pay. It
  wins 2.2-2.5x only where plain recycling thrashes (dokploy, monkeytype), and can't ship even adaptively while its
  output is nondeterministic (effect K4 under load: 2-5 of 12 runs differ; the Schema family). Two flag-only fixes
  landed (a half-finished hash in the shared tables: most of vue's 2x; a leader after graph-wide recycles).
  **Do not reopen it without hardware counters** (`perf c2c`/`perf stat` on the laptop, drizzle K12 flag vs plain) and a
  design for the variance groups; both are written there.
- **Defaults (worker fe):** checkers go past the physical cores for big programs (one per 4 MB of checked source past
  the cores, up to the threads: vscode gets every thread, nothing else measured does); the 16 MB growing start per
  checker is gone; `--checker-memory` defaults to a quarter of RAM (256 MB..8 GB) and a stolen file starts a fresh
  checker only when the thief holds half its budget. Recycles are zero on everything measured; vscode's peak is ~5 GB
  (2.3x bun), the source sweep's median 1.2x tsgo's and nothing above 2.1x. **The laptop bench of these defaults is done** (measurements.md "at f2ad73c9": vscode 2.45 -> 1.93 s, nothing lost,
  the defaults stay; memory vscode 5.4 GB).
- **Checker speed (workers k1, k2):** callgrind instructions at K1, PGO + LTO, byte-identical (measurements.md "Stage 2
  session 2"; k1's base ee9501eb8 -> 859a7a6a2): vscode -5.3%, excalidraw -10.9%, drizzle -5.7%, effect -5.6%, zod
  -5.4%, tsc -3.3%, nest -1.4%. From structural commits (type-param instantiation and intersection cases out of big
  frames, fewer allocations on call/infer/relate paths, string literal types without a Value, flow memos, inference
  setup, relate frames, small member-table name filters, subtype reduction's discriminating key, tables at most 5/8
  full) plus k2's flow and atom work (~0.4% on vscode). **Instructions only; walls are noise on the shared box** (k1's
  quiet A/B: drizzle 0.86 -> 0.82 s, tsc flat; needs the laptop). The recycle-free memory default moved
  `test/instrcount.txt` by another -12% on drizzle and -6% on zod at K1: comparisons against the saved file are now
  at a recycle-free K1, and a jump must be attributed (policy or code) before it is celebrated.
- **Front end (fe):** frees off the critical path, the load pool starts during the config glob (vscode config+libs+
  load+parse 221 -> 189 ms at T16 here), path normalization in one pass, a TSan thread-leak fix. Decided and recorded in
  plan 86 stage 3: no import pre-scan (<= 15 ms), no two-phase link (43 -> 25 ms for the binder's riskiest code),
  parallel body parse of TypeScript6's checker.ts doesn't pay (one 2.9 MB body), the glob's slowdown beside the pool
  is clocks/SMT, not locks.
- **Correctness (workers cor, cor2):** suite 13962/13996 -> 13972/14006 (saved file; 0 lost; the new `test/cases` count in both numbers) with 10 real fixes, each with a
  `test/cases` regression: one-member intersections of any (re-landed after the full oracles caught a `$ExpectType`
  and a clidiff regression: tttt builds one-member intersections tsc never builds), exactOptionalPropertyTypes'
  TS2375 head on intersection targets, declaration-emit member order measured from the leading comment, TS4109 on
  alias cycles through deferred type arguments, classes extending `any` normalize to `any`, tuple indexing by a
  numeric name the tuple lacks, annotated const keys naming nothing, TS2708 for unresolved `import x = A.B` roots,
  misplaced import/export aliases reporting their resolution's error when a spelling suggestion visits them, template
  literal type text escaping. Confirmed tsgo artifacts, recorded in bugs/tsgo_divergences.md: vue's two TS7006 (the
  TS5115 runaway), the three kdiff projects (rxjs too: tsgo K1 vs K16 differ the same way), es-toolkit, monkeytype's
  TS2589, all 5 `-b` sweep failures, the flagflip tail.
- **Oracle totals at the end state:** measurements.md "Oracle totals (2026-10-22: the merger's final run of the third coordinator session, 8a2bb4b38)" (suite 13972 / 14006, every other oracle as on 2026-10-23 but dtsdiff 1328 / 1350).

## 2. What the numbers say about the next 1.5x on vscode
This box (16 vCPUs) at the session's end: vscode defaults wall ~2.3 s = front end ~0.4 (floor ~0.25) + typecheck ~1.9
(K1 typecheck 10.9 s before k1's work; checkers finish within 10 ms of each other, so wall is per-file work / checkers
x the all-core clock penalty). Levers left, by size, all design-independent:
1. **Single-threaded checker work**: the flat profile has no hot spot; k1's session got ~5% from frames, allocation and
   memos. Plan 86 stage 2's remaining ranked items (instruction footprint by outlining cold arms, judged by wall on a
   quiet box; intern table footprint; big-union walks are done) are each a few percent. k1's final report lists what
   it left (LiteralBuilder's first realloc, object literal item atoms on the AST, LazyMemo pages).
2. **The parser**: ~12% of vscode's K1 instructions and structural (no token stream, re-scans, a Parser copy per
   speculative attempt: 74M child creations with no dominant site). A real token per position and node arenas (plan 86
   stage 3, perf_leads.md "Time") is a plan-sized rewrite of src/parser.c with the whole suite as its oracle: the one
   10%-class item left on the critical path, and it parallelizes already (helps CPU and 4-core boxes most).
3. **Policy on the laptop**: 24 threads, the memory default, `--checker-memory` per project size; judged only by the
   human's bench.
4. **Not worth it (measured):** the front end's remaining serial pieces (glob 30 ms, link aliases 43 ms, loader ~80 ms
   CPU with no hot spot); flow narrowing (tsc's algorithm already); hardening flags (4-5% instructions, 0-3% wall).

## 3. Correctness items that are plan-sized (don't start them as fillers)
- **Errors tsc reports from a loop's unfinished flow analysis** (five sightings: vue TS2538/TS4111 in flagflip,
  selfReferencingSpreadInLoop TS2488, typeGuardsAsAssertions TS2339, arrayFrom): tsc reports against an intermediate
  type during loop iteration; tttt reports only against final types. A plan: how tsc's checkExpression inside
  getFlowTypeOfReference's loop pass reports, and whether tttt's compute can report once per node at that point.
- **Instantiation identity**: hono's TS2769 (tsgo keeps a this-instantiated method type apart from the declared one;
  tttt merges them) and xstate's eopt TS2322 (tttt makes a second instantiation tsgo reuses) are the same root.
- **The satisfies elaboration inside the initializer** (TS7022/TS7023), **the error pass's first check of a callback
  argument** (TS2339), **the zod getter case** (plan 88 C/D), **the binding-element family**: bugs/missing_checks.md.
- Mutation seeds yield less each time (5 fixes at seed 1021, 2 at 1022); parser-recovery mutants (58-77 per seed,
  `--syntax`) are untouched and belong to plan 27.
- Plan 58 (re-pin) when `npm view typescript dist-tags` shows 7.1.0 stable (2026-10-22: latest 7.0.2).

## 4. First hour
1. Plan 89 §5's checks (tree clean, worktrees, disk, dist-tags). The laptop bench is done (the defaults stay).
2. Spawn the merger with plan 89 §1's loop (the third session's scripts are gone with /tmp/merge3; 10 minutes to
   rewrite; `$ExpectType` belongs in every check batch now, it caught the one regression the batch missed).
3. Two or three workers: the parser rewrite as a spike first (a token stream for one construct family, measured on
   vscode's parse CPU and the suite), one checker-speed worker from k1's leftovers, one correctness worker on the loop
   flow family. Nothing on the shared graph.
