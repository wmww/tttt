# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-16 (triage session, todo entry 10 of the sixteenth window): the unique symbol name in
TS2540 was fixed (`ttttReadonlyUniqueSymbolName`), TS5088 through `typeof this` (root cause found) and the
content-mapped outputs went to bugs/dts_emit.md, the human's stop request was honoured (`claude-stop`).

- **The 12-core bench is in** (2026-10-16, measurements.md "The 12-core machine"): past K12 checkers buy 0-11% wall
  for ~170 MB each on vscode (6.5 GB at the default K24); `--checker-memory 64` at K24 is -1.75 GB for +2.4%. Plan 75
  question 1 has its numbers (a K cap and/or a total budget); plan 72's declare+link is 5% on vscode, 15% on webpack.
- **For the human, when next on the 12-core machine** (2026-10-16): plan 72 stage 1 cut the loader thread's own work
  (~40% at T4 here); the 24-thread vscode load phase was 389 ms with workers 59% busy. Rerunning
  `TTTT_LOAD_POOL_STATS=1 ./release/tttt -q --keep-going --noEmit --time -p thirdparty/corpora/vscode/src/tsconfig.corpus.json`
  there says whether plan 72 needs a stage 2 (its stage 1 section).
- (2026-10-16, plan 73) TS2676/TS2808 (class_decl.c, "a getter and setter pair agree on abstract") skip every computed
  name (`!i->method.computed_key`); tsc's checkAccessorDeclaration checks all but non-bindable dynamic names, so
  `get ["x"]()` / `set ["x"](v)` and late-bound pairs (now `ty_class_accessor_getter()`) are unchecked. Probe and fix.
- (2026-10-16, plan 73) The members builder groups a class's method signatures by source text (`class_methods_same_name`):
  `[a]() {}` and `[b]() {}` with b of `typeof a` are one member under tsc with two overloads; tttt joins the spans but
  drops the second signature. Also a late-bound accessor merged into an early accessor's symbol isn't typed by both.
- (2026-10-16, plan 76) **No oracle times tttt at its default checker count**: perfsweep runs `--checkers 1`, the sweeps
  `--threads 1`. dokploy/apps/dokploy took 55 s at the defaults (2.9 s at K1): each checker's warm state (~150 MB of
  trpc/drizzle types) was over its 128 MB recycle budget, so it was recycled after nearly every file. Fixed by
  `check_share()`'s segment_base (a recycle only once the checker has doubled since its segment's first file). An
  old-build scan of the 150 slowest source sweep projects found no other case. perfsweep (or a cheap variant) should
  also time the defaults, or report recycle counts.
- (2026-10-16, plan 76) `tttt -b` rewrites every `.d.ts` even when its bytes are unchanged (new mtimes; tsgo `-b` then
  "updates output timestamps"). tsc skips unchanged writes? Check tsgo's writeFile and match it.
- (2026-10-16, plan 76) `lazy_slot_new()`'s process-wide counter (2^30) is never reset: a watch mode re-parsing
  vscode per rebuild would run out after an unmeasured number of rebuilds. Only matters if plan 76's watch is built.
