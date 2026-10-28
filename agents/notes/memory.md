# Peak RSS: attribution, the budget sweep, cheap levers (2026-10-23, worker "memfree")

Question (the human, via the coordinator): the cheapest mechanisms that cut peak RSS substantially (vscode -30% or
better, drizzle likewise) at < 2-3% wall at the defaults. This box: 16 vCPUs (8 physical cores), 60 GB (so the
default `--checker-memory` is the 8 GB maximum, split between the checkers: 512 MB each at vscode's 16, never
reached), shared with other agents' builds (load 10-35 during these runs: walls are noise, CPU less so).

## Tool: `scripts/rsstimeline.py -- TTTT-ARGS`
`TTTT_VMA_NAMES=1` (prelude.c `vma_name()`, PR_SET_VMA_ANON_NAME) names tttt's anonymous mappings by kind in
/proc/PID/smaps: `checker` (checker arenas: Arena.checker), `arena` (every other Arena: the binder's), `table`
(big_table(): intern, link, node link and relation tables of 2 MB and more), `ast` (the AST segments: parsed files and
materialized deferred bodies), `text` (read() source text chunks), `stack`, `span`. Unnamed anonymous memory is malloc's
thread arenas (LazyMemo pages, small tables, the rest), `[heap]` the main heap, `*.ts` mappings the mapped source text.
The script samples smaps every 20-25 ms and prints the timeline with --time's phases and the breakdown at the peak.

## Attribution (base 9894ae8e5, fast/tttt)
**vscode, defaults (16 checkers, no recycle): peak 5168 MB at the very end of the check** (5311 of 5475 ms):
| kind | MB | |
|---|---|---|
| checker arenas | 2332 | 16 x 110-172 MB (`--time`'s arena totals), growing linearly through the check |
| ast | 1684 | 490 parsed (after load) + ~1190 materialized deferred bodies, held by the checkers that read them until their checker_drop() |
| table | 442 | grows with the checkers (0 at resolve) |
| arena (binder) | 267 | constant from resolve on |
| anon (malloc arenas) | 223 | ~210 of it before the check |
| mapped text + read text | 160 | |
| heap, stacks, binary | 60 | |
So ~1.2 GB is the program (budget-independent: AST 490, binder 267, malloc 210, text 160, rest), ~4 GB is what the
checkers hold: their arenas, tables, and the bodies they materialized. The checkers finish within ~3% of each other
(stealing), so the peak is at the end and **releasing a checker's memory when its share ends (lever 3) buys nothing at
the defaults**: there are no idle tails to free into. It matters only when the peak moves earlier (below).

**vscode at `--checker-memory 1536` (96 MB each, 27 recycles): peak 3016 MB at 938 of 1484 ms, mid-check**: checker
arenas 1101, ast 1063 (~570 materialized), tables 260, binder 267, malloc 214, text 55. A recycle drops the checker's
materializations too: that is why recycling cuts the AST as well.

**drizzle, defaults (8 checkers): peak 920 MB at the end**: checker arenas 630 (8 x 68-103 MB), tables 98, ast 77,
malloc 47, binder 33. At K1 the one checker holds 292 MB (432k types) against K8's 8 x ~72 MB (1.03M types in all):
**drizzle's peak is the per-checker warm state times the checker count**, which no budget touches (no checker
reaches 96 MB). K1 492 / K2 603 / K4 695 / K6 765 / K8 912 MB (wall K6 = K8 here, under load). effect likewise
(checker arenas 381 of 860, ast 258 parsed).

## The budget sweep
`--checker-memory M` at the defaults otherwise (an explicit M has a 96 MB per-checker floor; the default's cap path
64 MB), medians of 5 interleaved runs, load 25-38 (wall unusable, CPU = user + sys with TTTT_SYNC_EXIT=1):
| vscode K16 | CPU | peak | recycles |
|---|---|---|---|
| default (8 GB: 512 each) | 19.99 s | 5168 | 0 |
| off | 19.77 (-1.1%) | 5200 | 0 |
| 4096 (256 each) | 19.42 (-2.9%) | 4430 (-14%) | 12 |
| 3072 (192) | 19.28 (-3.6%) | 4229 (-18%) | 12 |
| 2048 (128) | 20.49 (+2.5%) | 3777 (-27%) | 17 |
| 1536 (96) | 20.62 (+3.2%) | 2872 (-44%) | 27 |
| 1024, 512 (96: the floor) | 20.8-21.1 (+4-6%) | 2900 (-44%) | 29 |
drizzle, effect, excalidraw: flat down to 1536 (no checker reaches its budget); drizzle 1024 -13% / 3 recycles, 512
-21% / 6, CPU within noise.

**vscode single-threaded (`--checkers 1 --threads 1`), 3 runs each: recycling is free there.** off 11.63 s CPU /
4288 MB; 96 MB 11.31 / 1175 (-73%, 29 recycles); 128 11.35 / 1230; 192 11.49 / 1331; 256 11.35 / 1458; 384 10.90 /
1683. No cost within noise at any budget (smaller tables and arenas offset the re-derivation): the single-threaded
default (the 8 GB budget: never recycles) holds 3 GB it doesn't need.

**But a small fixed budget is a no-go in general: what a recycle costs depends on the project, by two orders of
magnitude.** The 138 source sweep projects whose K1 arena reaches 96 MB, K1 T1, off against `--checker-memory 96`
(best of 2 each, -O3 build, 4 at a time): CPU from -17% (ng-bootstrap, quill: smaller tables win) through ~0 (vscode,
inbox-zero, openapi-typescript) to +40..+165% (monkeytype, dokploy, folo, mikro-orm, kebo, better-drizzle); median
about +8%, a third above +10%, for RSS -10..-50%. One recycle can cost a quarter of the check (monkeytype/frontend
at K1 192 MB: one recycle, +27%: its next file, Nav.tsx, 1.5 -> 72 ms, rebuilding ~70 MB of solid-js/zod types).
At the defaults: dokploy 1536 +186% CPU, 2048 +79%; monkeytype 1536 +16%.
- The recycle guard `held > 2 * segment_base` never fires: segment_base is what the checker holds after its segment's
  first file, 0-5 MB everywhere (the warm state is built over many files, not by the first).
- No cheap online predictor found: the segment's types-per-KB concavity (excess types of the first half of its source
  over the second half's rate) is 0.47-0.82 on monkeytype but -0.9..0.9 on dokploy and -0.1..0.4 on vscode, per
  segment; source KB per MB of checker growth separates vscode (75) from dokploy/monkeytype (11-15) but not nest
  (50-60 KB/MB, +12..25%). (Per-file `TTTT_FILE_TIMES` lines now carry the file's types made and source bytes.)

## The default since 1e71cfc86: a process-wide pool
`main.c memory_pool_default()`: the process's resident set at start plus 80% of what is available (MemAvailable, cgroup
v2 headroom up the hierarchy, `TTTT_MEM_AVAILABLE_MB` for tests); past it after a file, a checker holding at least the
average and 64 MB recycles (`program.c pool_recycle_due()`). On a machine with free memory nothing recycles (as with
the old default on every measured project); it bounds a small machine or a container. vscode with 3000 MB available:
peak 2.5 GB instead of 5.3, CPU +3-8% (1 s more user: the rebuilds), 44 pool recycles. `--checker-memory` is the old
per-checker split, the explicit override.

## Reusing a recycled checker's arena chunks (declined, 2026-10-23)
What a recycle spends in the kernel (munmap of the old arena with its TLB shootdowns, faulting and zeroing the new
one's pages), vscode at the defaults, 3 interleaved runs each: no recycle 0.62-0.63 s sys (one outlier 1.05), 183-188k
minor faults; 44 pool recycles (`TTTT_MEM_AVAILABLE_MB=3000`) 0.77-0.81 s sys, 212-216k faults, while user grows
22.7-23.3 -> 24.0-25.1 s. So keeping the chunks for the next incarnation could save at most ~0.15 s of ~24 s CPU
(0.6%), only when recycles happen, which by default is only under memory pressure, where holding the old chunks
(still resident, and memset before reuse since arena memory is assumed zero) would defeat the recycle's purpose.
The cost of a recycle is the rebuild (user time), not the memory traffic: not built.

## vscode's per-file census, and the memo/scratch region priced (declined, 2026-10-23)
`scripts/filecensus.sh -p vscode --checkers 1 --threads 1 --checker-memory off` (177 s, 5.9 GB): arena 1556 MB
(checker_memory 1894), 9929 files. Kept (made for a file, needed later) 369 MB (24%), droppable and still referenced
871 MB (56%), dead 317 MB (20%). By category (kept / droppable MB): "other" (members, props, signatures, mappers,
call resolutions) 253 / 718; types 86 / 192 (anonymous 36 / 112, literals 28 / 44); intern identities 24 / 116;
LazyMemo pages 0.3 / 132 (expression memos 7.86M of 10M); node-keyed memos 0 / 20. Tables outside the arena: node links
3.75M (117 MB, 3.44M droppable: call resolutions 1.19M, flow reachability 377k, literal contexts 248k...), types and
links 64 MB each, relations 1.6M (30 MB, 1.0M naming a droppable type). Later files re-derive 293k intern entries
and 48k node links if everything droppable went (of 2.4M / 3.9M looked at).
**Like zod, the bulk is instantiated type structure** (prop_new_declared via ty_object_literal_alternative 177 MB,
instantiate_members ~170, builder_finish ~50), reachable only through a per-file region with promotion (the copying
recycle, declined 2026-10-17). What a placement-by-kind region (no promotion) could take: expression memos (~94 MB of
the 132 MB of LazyMemo pages; but memo pages are per declaring file and mix in declaration memos other files read, so
it needs per-kind paging), call resolution data in the arena (call_return_type 54, resolve_with_signatures 15, call
anchors ~21, contextual signatures 6, call effects 3: ~100 MB), node-keyed memo blocks 20: **~210 MB, 13.5% of the
arena** (plus ~60 MB of node-link table), under the 15% bar, and at the defaults ~13% of each checker's ~145 MB is
~6% of the 5.2 GB peak. Not built.

## What would cut the peak (for a next round)
1. **Cheaper rebuilds after a recycle** (the cost is user time: lib and library type structure made again), so that a
   lower pool or the per-checker split pays everywhere: a kept region for what declarations of non-checked files
   (`.d.ts` in node_modules, the libs) derive, made there from the start rather than copied out (the copying recycle's
   cost was the copy). Needs every type made while resolving such a declaration to be placed by its inputs, not by its
   site: the interning makes that a design question first.
2. **Fewer checkers when they only duplicate** (drizzle: K6 = K8 wall here, -17% RSS): auto_checker_count() by
   measured scaling rather than bytes; machine-dependent, the laptop judges.
3. **Materialized bodies** (1.2 GB of vscode's peak at the defaults): dropped only with a checker incarnation because
   types and memos point into them; a body whose function no other file reaches (no exported inferred return type, no
   expando) could go at its file's end. Unmeasured: count functions whose bodies' nodes are referenced by kept types.

# Per-file release of checker results: census (PARTIAL, 2026-10-23, worker "memc")

Question (the human): vscode is 5.5 GB at the defaults against bun's 2.2 GB (drizzle 1.15 vs 0.43). Can most of it go at
little or no speed cost, by dropping what each file's check made once that file is done (bun frees task-local results
at task end)? **Status: the tool exists and one corpus is measured (zod, K1). vscode, drizzle, effect, the defaults'
K, the design costing and the recycle budget sweep (deliverables 1-3 of the brief) are still to do.**

## The tool
`scripts/filecensus.sh -p P [--checkers K --threads T --checker-memory M]` (a `-DTTTT_SURVIVORS` build in
/tmp/tttt-filecensus, `TTTT_FILE_CENSUS=1`; code in ty/survivors.c `survivors_file_census()`, analysis in
`scripts/filecensus_an.py`). Default build unchanged (everything under `#ifdef TTTT_SURVIVORS`).
- Every checker-arena allocation, node link write (last writer) and LazyMemo (first touch) records an **owner**: the
  checker's sequence number of the file being checked (`check_file()`; the segment's after pass, `check_segment_after()`,
  counts as its file's), 0 = outside any file (the warm-up). The arena recorder is per arena now (`Arena.rec`), so it
  works with several checkers and threads (the census itself is serialized by a lock).
- At the end of each checker's share (and at each recycle, instead of survivors.sh's report) an allocation is **kept**
  (needed after its file) if a root reaches it: the Checker's fields, owner-0 allocations, FATE_ROOT entries (survivors.c
  `kind_fate()`, non-local symbols), declarations' memos (non-local bindings, class fields/accessors); or if anything
  owned by a *later* file points at it (that file obtained it after its file was done: it was reused, not re-derived);
  then everything a kept allocation reaches. Intern identities are kept while their value is, caches while key and value
  are. Everything else owned by a file is **droppable at its file's end**, split into referenced and unreferenced (dead
  outright: outgrown lists, popped scratch).
- Transient reuse (a later file looks up an entry it doesn't store a pointer to) isn't seen by reachability: intern hits,
  node link gets and LazyMemo touches by a later file are logged (`survivors_hit()`, deduplicated per (entry, file)) and
  the census counts those landing on a droppable entry: what a per-file release would compute again.
- Conservative/approximate: words are scanned conservatively (any word pointing into a recorded allocation), memo pages
  aren't scanned (ids, not pointers); ids in other structures (relation keys) aren't references; LazyMemo sizes are
  reported at the real 12 bytes; non-arena tables (intern/node-link/relation tables, type directory chunks) are reported
  by capacity, not apportioned.

## Numbers so far
**zod (`tsconfig.test.json`), K1, `--checker-memory off`, 575 files**: arena 275 MB (checker_memory 314 MB):
| | MB | share of arena |
|---|---|---|
| warm-up (owner 0) | 0.0 | 0% (lib types are made lazily inside the first file) |
| kept: made for a file, needed later | 50.4 | 18% |
| droppable, referenced | 195.1 | 71% |
| droppable, unreferenced (dead) | 29.3 | 11% |
- By category (kept / droppable MB): "other" (members, props, signatures, mappers, lists) 35 / 169; types 10 / 31
  (anonymous 6 / 18, references 1.7 / 5.6); intern identity blocks 3.8 / 16; caches 1.2 / 3.4; LazyMemo pages 0 / 4;
  node-keyed memo blocks 0 / 0.5. Top droppable sites are `instantiate_members` (members.c, ~73 MB), `builder_finish`
  (~18), `expand_mapped`/`prop_decl_rare_mut` (~13), `signature_resolve_slow` (~7), mappers (~9): **the bulk is
  instantiated type structure made for one test file**, not expression memos (those are small on zod).
- Tables (outside the arena): types 404k / 1M slots (16 MB), links 212k / 524k (8 MB), node links 137k / 299k (4.6 MB:
  124k droppable, KEY_CALL_RESOLUTION 49k the largest), relations 220k entries (5.2 MB, 156k naming a droppable type).
- Transient reuse: later files looked at 196k distinct intern entries and 140k node links; **15.2k intern hits and
  1.8k node-link gets landed on droppable entries** (about 4% of the 376k types made): the re-derivation a per-file
  release would cost on zod, an upper bound on extra types (most are cheap identities: unions, references,
  anonymous instances by kind 4/21/52/121).
- Consistent with survivors.sh's earlier "declaration roots reach 13-35%" (measurements.md "A copying checker
  recycle"): the new information is that later files' references add little (kept 18% here).

## Design sketch (not costed yet)
- A per-file region in the checker (a second Arena, `file_arena`), reset when the file's check, its deferred checks and
  its after pass (unused, declaration walk: run per file instead of per segment; `--checker-memory 0` shows output is
  identical, kdiff `-m 0`) are done. What goes there must be decided **at allocation time**, since the census shows
  droppable and kept bytes come from the same sites (instantiate_members: 10 kept / 57 droppable on zod).
- Two shapes: (1) **placement by kind**: only per-node memos, flow memos, call resolutions, builders/scratch per file,
  the type graph always global: safe and simple but on zod that is only ~5-35 MB of the 225 droppable (expression
  memos are small here; vscode's 10M LazyMemos and its node links may differ: measure). (2) **everything per file plus
  promotion at file end** (a minor GC: copy what is reachable from roots/later files into the graph, fix intern
  tables): reaches the 80%, but measurements.md declined a precise copying recycle (~20 MB copied costs about the
  re-derivation it saves) and it needs exact pointer maps of every arena structure. (3) **recycle more often** is the
  existing approximation of (2): `--checker-memory` lower budgets; the brief's deliverable 3 measures it.
- Cross-file references into a file region that must be handled: intern table entries (identity of unions, references,
  instantiations) must be removed at reset (log per file the slots inserted); lazily filled slots of graph types (a
  Prop's type, a type's resolved members) written during a file must not point into the region (the census counts
  those as kept via "warm/earlier owner points at it" only when the referrer is kept); relation cache entries by id.

## Next (for the following worker)
1. Run `scripts/filecensus.sh` on drizzle, effect, vscode at K1 `--checker-memory off` and at the defaults (defaults
   print one census per checker per recycle: sum them), plus peak RSS of a plain run, to fill the table per corpus.
   vscode K1 needs ~35M allocation records (~850 MB) plus the marking: fine on this box, minutes.
2. Split "other" sites into graph structure vs scratch (by function name in filecensus_an.py) to price design (1).
3. Deliverable 3: `--checker-memory 2048/3072/4096` on vscode, instructions and check-phase CPU, recycles.
Doubts: owner 0 is nearly empty because the warm-up is lazy, so lib types made in the first file count as that file's
(kept only if a later file stores them, else droppable and counted as re-derivation in the reuse numbers); the
"later file points at it" rule counts references from a later file's garbage too (conservative for dropping).
