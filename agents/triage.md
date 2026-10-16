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
