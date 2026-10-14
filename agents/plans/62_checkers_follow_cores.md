# Plan 62: the checker count stops deciding the output, then follows the cores

Written 2026-10-13 (todo entry 1 of the fifteenth window, planning). Decision: **(a)**, output independent of K, then
K grows with the machine. It is cheaper than vision.md assumed: tttt is already K-independent almost everywhere, and
what is left are bugs that already hit today's default of 4 checkers.

## What was measured (the 2026-10-13 tree, 16d0052d)
- **tsgo 7.1 doesn't depend on its checker count.** On the 19 source sweep projects where tttt differs (below), tsgo
  `--checkers 1/4/16` prints the same bytes, except inbox-zero at 4: the `x?: undefined` property order of widened
  object literals (tsgo's per-checker undefinedProperties cache, the one known K-dependence tttt shares).
  tsgo's union order is its stable `CompareTypes` (checker/utilities.go); its default stays at 4 checkers on any machine.
- **tttt at K=16 against K=1** (raw CLI output, `scripts/kdiff.py`, `scripts/checkerdiff.py -k 16`,
  `baseline_tests --checkers 16 --dump`):
  - suite: 0 of 39636 dumped files differ (most tests have too few files for K to matter);
  - corpora: all 16 byte-identical;
  - DT sweep: 0 / 8595;
  - source sweep: **19 / 3363**. **13 of them already differ at K=4**, tttt's default, which no oracle runs: the
    sweeps and the suite run `--checkers 1`, and only the corpora run at 4. trpc at K=4 prints 201 spurious
    diagnostics that K=1 doesn't.
- **What the 19 are** (all tttt bugs, tsgo is K-independent on every one):
  1. *Once-per-program emulation that tsgo does per file*: the JSX runtime module diagnostic
     (`report_runtime_module()` in ty/jsx.c, `once_key` `0x5a`, "the first checked file's") suppresses a TS7026 at a
     file's first tag before `error_ctx_keep_first_once()` drops it. tsgo's `getJsxNamespaceContainerForImplicitImport`
     works per file (`links.firstJSXTagInFile`). query/examples/react/{offline,rick-morty,star-wars,suspense},
     qwik/packages/{insights,devtools/ui}: TS7026/TS7016/TS2322 lost at K>1. Audit the other `once_key` users
     (`ty_check_emit_helpers` TS2343/TS2807) against tsgo the same way.
  2. *Evaluation-order dependences* (another checker sees another file first):
     - trpc (`trpc`, `packages/react-query`, `packages/tests`): konn's builder chain
       (`konn().beforeEach(...).afterEach(...).done()`, test/queryOptions.test.tsx) infers `NoContext` once K>=3:
       TS2339 `Property 'close' does not exist on type 'Partial<NoContext>'`, TS7006s. 209 diagnostics at K=4, 540 at K=16.
     - mikro-orm, mikro-orm/tests, dexie/samples/dexie-cloud-todo-app: an extra TS2615 ("Type of property 'toJSON'
       circularly references itself in mapped type") at K>1. dexie also has a TS2502 at K=1 that tsgo hasn't: same
       circularity family.
     - better-auth/demo/nextjs (TS7006 lost, TS2578 gained), better-auth/demo/oidc-client (TS18046 lost at K=4).
     - mermaid: a TS2345 (`UncheckedJSONSchemaType<MermaidConfig, false>` against `<Record<string, any>, false>`)
       lost at K=16: a relation answer depending on what was measured first (variances: bugs/type_core.md
       "Variance measurement order").
  3. *Display that depends on the checker*: babel-traverse's TS2322 union shows another first member (`... 754 more`:
     an `order.c` id tie, hacks.md "Stable type ordering"); openapi-typescript's TS2425 prints `__@iterator@92` vs
     `@73` (a symbol id in a name: compare with what tsgo prints there); redux-toolkit's elaboration says
     `getRunningQueryThunk(...)` at K=1 and `()` at K=16 (some display state left by an earlier check).
- **What more checkers cost** (types and arenas are machine-independent; wall times above K=4 mean nothing on this
  4-vCPU box). Total types / arena MB / peak RSS MB:

  | corpus | K=1 | K=4 | K=8 | K=16 |
  |---|---|---|---|---|
  | tsc | 104k / 54 / 227 | 184k / 82 / 278 | 258k / 103 / 309 | 328k / 121 / 349 |
  | type-fest | 226k / 609 / 709 | 280k / 710 / 825 | 310k / 732 / 849 | 340k / 755 / 897 |
  | effect | 537k / 363 / 706 | 681k / 427 / 795 | 751k / 457 / 840 | 824k / 485 / 893 |
  | drizzle | 394k / 275 / 454 | 685k / 431 / 643 | 881k / 534 / 768 | 1173k / 674 / 948 |
  | webpack | 315k / 180 / 550 | 429k / 239 / 643 | 505k / 278 / 702 | 598k / 326 / 774 |
  | vscode | 2.40M / 1539 / 4411 | 2.95M / 1822 / 5035 | 3.31M / 2009 / 5307 | 3.75M / 2226 / 5642 |

  K 4 -> 16 costs +7% (type-fest) to +47% (drizzle) RSS; vscode +12%. On the 12-core machine (measurements.md, bun
  table) `--checkers 12` was 30-50% faster in wall time than 4 on every corpus, at +13..40% RSS.
- **tsgo 7.1's checker association** (compiler/checkerpool.go: weighted FENNEL over the import graph, source files
  first by weight) was prototyped in `deal_files()`, with bytes in place of node counts: **worse** than tttt's
  contiguous program-order runs on 8 of 9 corpora. Duplicated types +2..21% at K=4 and K=16 (drizzle K=16 1.17M ->
  1.42M, type-fest arena +23%), the largest checker's share no better on balance. `deal_files()` stays. Don't
  retry without node counts and a reason.

## Stages
### Stage 1: the oracles run at K, the order bugs fixed (one session) — done 2026-10-13
Result: kdiff 19 -> **5** / 3363 at K=16 (inbox-zero, the three class 3 displays, mermaid), **2** at K=4 (inbox-zero,
redux-toolkit); the DT sweep, corpora and suite dumps stay K-independent. Source sweep at the default K 3309 / 3363
(K=1 was 3307), DT sweep 8594 / 8595. Fixed: the JSX runtime module per file (the once_key tagging also caught a later
TS7026: errors are inserted sorted, the tag went by index range); trpc and better-auth (`outer_inference` leaking into
variable initializers and predicate inference, hacks.md); dexie and mikro-orm's TS2615 (callable_definitely_related(),
bugs/type_core.md). Left: mermaid (bugs/type_core.md "Relation answers that depend on what was related before"), so
stage 2 starts with it or with babel-traverse. `TTTT_CHECK_ONLY=substring` (program.c deal_files) reproduces an order
dependence at one checker: only the matching files are checked, by a fresh checker. The sweeps now run at the default
K; `scripts/oracles.py --only kdiff` is the K oracle (suite dumps + source sweep at 16, ~9 min).

- Make K>1 part of the routine oracles: `scripts/sourcesweep.py` and `scripts/sweep.py` run tttt at its default
  checker count (with `--threads 1`, so `-j` still means processes), since tsc's caches are tsgo's 4-checker output and
  that is what users run. Add `scripts/kdiff.py` (source sweep at K=16, ~7 min) and the suite's K=16 dump diff to
  `scripts/oracles.py` (the full run, not `--quick`).
- Fix class 1 by porting tsgo's per-file semantics (then delete `once_key` users that don't need it, and hacks.md's
  "Once-per-program diagnostics" entry shrinks to what still is once per program in tsgo).
- Reduce and fix class 2. An order dependence at K usually reproduces at K=1 with another file order: write each
  `test/cases/` repro as files in the order that fails (the harness checks in file order at one checker), so it runs
  in the suite without harness changes. Start with trpc (the biggest), then the TS2615 family, then the rest; if one
  turns out to be an inherent tsgo-style order dependence (tsgo would show it too in some order), say so in
  bugs/tsgo_divergences.md instead of forcing it.
- Validation: `scripts/kdiff.py -k 4` and `-k 16` list only inbox-zero (undefinedProperties) and the class 3 display
  differences; the source sweep's match count at the default K is at least today's K=1 count (3307); suite 0 lost.

### Stage 2: display independent of K — done 2026-10-14
Result: kdiff at K=16 **4 -> 2** / 3363 (inbox-zero's undefinedProperties order, which tsgo shares; mermaid, see
below), DT 0 / 8595, suite dumps 0 / 39870 differing, every corpus byte-identical at 1 and 16 (checkerdiff compares
exactly now). babel-traverse had already gone. openapi-typescript: checkKindsOfPropertyMemberOverrides' messages
printed a member's escaped name (`__@iterator@<type id>`), now symbolToString's `[Symbol.iterator]`; redux-toolkit:
signature_return_code() read the parameter count of a not yet resolved (lazily instantiated) signature, so
`get(...)` became `get()` unless something had resolved it first.
MERMAID_RESULT

- Finish `ty/order.c`'s id tie-breaks against tsgo's `CompareTypes` (hacks.md "Stable type ordering" lists the
  remaining ones: roots without `outer`, deferred references without a cheap position, same-symbol anonymous types with
  equal mappers, intrinsic variants), babel-traverse first.
- Symbol ids printed in names (`__@iterator@NN`) and the redux-toolkit elaboration: find what state they read.
- Validation: `scripts/kdiff.py -k 16` and `--dt`, `scripts/checkerdiff.py -k 16`, suite dump: identical but for the
  undefinedProperties class. Then delete checkerdiff's "may differ inside quotes" allowance (its `modulo` column
  becomes `exact`).

### Stage 3: the default follows the machine — done 2026-10-14 (but the 12-core numbers)
Result: `--checkers` defaults to `auto_checker_count()`: one per thread, at most one per 64 KB of checked non-lib
source (a twelfth of the source sweep at K 1/2/4: below 64 KB the extra checkers saved a few ms and cost 4-9% RSS;
bytes are a rough proxy, nest checks 240 KB in 110 ms). On this 4-vCPU box only react (4 -> 2 checkers, 196 -> 165 MB,
0.11 -> 0.12 s) and nest (4 -> 3) change among the corpora. The cap above 4 cores waits on the human's
`scripts/bench.sh --checkers "4 8 12 16"` on the 12-core machine (triage.md): if 12 checkers cost more RSS than they
save time there, cap the default (e.g. at 8) rather than at the cores.
- `--checkers` defaults to `--threads` (cores), capped by program size so small projects don't pay K copies of lib
  types for nothing: e.g. at most one checker per N bytes (or files) of checked non-lib source, N chosen from the
  kdiff/perfsweep data so that ~95% of the source sweep projects keep today's speed. Keep `--checkers` explicit
  values as they are; `--singleThreaded` stays 1.
- Measure on this box: types/arena/RSS per K on the corpora (the table above) and perfsweep's RSS column at the new
  default. Wall-time scaling needs many cores: write the human item (triage.md, "for the human") asking for
  `scripts/bench.sh` at `--checkers 4/8/12/16` on the 12-core machine, and set the cap from it.
- Update vision.md "Thread defaults", CLAUDE.md's `--checkers` sentence, documentation.md.
- Validation: every oracle at the new default unchanged; bench (this box) no slower at the default; RSS growth
  recorded in measurements.md.

### Stage 4 (measurement only): what the duplication is
More checkers multiply the per-checker arena (vscode +45% at K=16 against K=1). Attribute it: of the types K checkers
make beyond one checker's, how many are lib/global types (every declaration in a default lib or `@types` global) and
how many come from the program's own modules? If lib/global types are most of it, write a follow-up plan for a shared,
frozen lib type layer built once before the checkers fork (hard: type ids, interning and LazyMemo are per checker,
hacks.md "LazyMemo holds type ids"). Otherwise record "no" with the numbers here and in perf_leads.md.

## Not in scope
- tsgo's FENNEL partitioner (measured worse, above).
- Parallelism inside one checker; the serial declare/link phases (perf_leads.md "Parallelism": below 10% of the
  wall at vscode scale).
