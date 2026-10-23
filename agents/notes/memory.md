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
