# Plan 45: parallel name resolution
Written by the planning session of 2026-10-05 (todo entry 7), which also executes stage 1.

## Where the time is (vscode `src`, K4, idle machine, temporary timers in `program_resolve_names()`)
| step | ms |
|---|---|
| options checks, stdlib order (`pre`) | 12 |
| declare (`block_add_names()` of every file, exports checks) | 40 |
| globals, TS7016 hints | 1 |
| link (module refs, `binder_link()`) | 28 |
| **resolve walk** (`block_lookup_names_container()` + `unused_mark_jsdoc_links()`) | **510** |
| remaining aliases, comment directives | 2 |
Webpack: declare 8, link 2, walk 62 of 73. type-fest: walk 5 of 7. **The walk is 85-90% of the phase**, so stage 1 only
parallelizes the walk; declare and link stay serial (stage 3 says when they'd be worth it).

## What the walk writes that isn't its file's (the audit)
The walk makes function/block scopes, their symbols, flow nodes, and writes identifiers' `symbol`/`target`, all of
its own file. What else it touches:
1. **One binder arena** (`Binder.arena`: scopes, symbols, `SymbolRare`, tables, flow nodes via `FlowBuilder.arena`),
   and `scope_release()` pops the arena's latest allocation.
2. **`Binder.symbols`** (every symbol, appended by `binder_new_symbol()`; `binder_resolve_remaining_aliases()` walks the
   tail made after link).
3. **The program's `ErrorCtx`** (passed in, and `binder->errs` in ~40 places). `error_ctx_find()` looks into it.
4. **`Binder.suggestion_count`**: tsc's first-10 spelling suggestions, an order dependence across files
   (`scope_report_not_found()`, `report_misspelled()` and its two callers with a fallback error, the pending
   not-found reports, the `await` count).
5. **Binder lists**: `pending_not_found`, `pending_type_as_value`, `pending_export_merges` (a namespace in a function,
   recovered), and `scope_pending_mark()` reads their counts for `scope_release()`.
6. **Writes to other files' symbols**: `Symbol.referenced |= meaning` (`unused_mark_referenced()`, any symbol of the
   program, under noUnused*), `Binder.has_assignment_flow_containers`.
7. **Expando declarations in nested scopes** (`scope_add_expandos()` from `block_add_names()` of a function body):
   `add_expando_member()` declares into `host->exports`, and the host may be shared (a global merged across files, an
   export other files read through aliases). JavaScript files do much more of this (`this.x =`, prototype
   assignments, `binder_require_symbol()`, `push_global_expando()`), and their walk reads what it declared
   (`PropertyExpr.declares` -> `SymbolDecl.js->flow_container`).
8. **Lazily made binder singletons**: `binder_require_symbol()`, `binder_discarded_errs()`.
Not shared: `ModuleRef`/`SourceFile` const-cast flags (own file), identifier memo fields, `lazy_slot_new()` (atomic, and
the parser's), `name_cooked()` (mutex). Aliases were resolved in link; block-level ones are the file's own.

## Design: deterministic by construction
Output must not depend on the thread count, so the order is the same at `--threads 1`:
- **Two lanes.** TypeScript files are walked in any order by a pool (`--threads`); then a **serial pass in program
  order** replays each TS file's deferred work and walks each JavaScript file. JS files go to the serial pass whole
  in stage 1 (item 7: their walk declares into other files' symbols and reads it back).
- **A shard per file** (`ResolveShard`, thread-local `resolve_shard` while a file is walked): the thread's arena
  (one per worker thread, kept by the binder until it dies; thread 0 uses `Binder.arena`), the file's new symbols,
  its pending lists, its own `ErrorCtx` (parent: the program's, frozen meanwhile), and its **deferred records**.
  `binder_arena()`, `binder_errs()` and the pending lists' accessors go to the shard when there is one.
- **Deferred, replayed in program order**: every event that consumes a suggestion (item 4: a not-found report, a
  misspelling with its fallback error, a pending not-found's count, the `await` count) and every expando declaration
  whose host isn't a local all of whose declarations are in this file (item 7). Per shared host the declaration order
  stays program order then walk order, as before. Records keep their scopes alive (`scope_pending_mark()` counts them).
- **Merged in program order** after the serial pass: symbols into `Binder.symbols`, pending lists, the shard's errors
  into the program's (whose order is decided by `error_ctx_sort_and_dedupe()` anyway).
- `Symbol.referenced` is an atomic OR, `has_assignment_flow_containers` an atomic store; the require symbol and the
  discarded ctx are made before the pool starts.

## Stages
Each stage: `scripts/fasttest.sh --summary` 0 lost; `baseline_tests --threads 4 --dump` byte-identical to `--threads 1`;
`scripts/threadcheck.sh` and `scripts/checkerdiff.py` identical; every corpus unchanged; a TSan build
(`meson setup /tmp/tsan --buildtype release -Db_sanitize=thread -Dc_args=-g`, `TTTT_NO_REEXEC=1`) clean on the
corpora at 4 threads; `scripts/instrcount.sh` (single-threaded must not regress beyond ~0.5%); `--time` resolve numbers.

### Stage 1: the TypeScript walk on threads: **done** (2026-10-05, todo entry 7)
As designed above, plus what the audit missed and TSan/the suite found:
- **Merged type parameters**: `class C<T>` in a.ts and `interface C<T>` in b.ts share T's symbol, which a.ts's walk
  makes and b.ts's walk adds to (`scope_new_generic_merged()`). Files with such a later declaration walk serially
  (`binder_mark_serial_resolves()`): 18 files / 0.9 MB of vscode's 60 MB, 13 of webpack's TS, 13 of type-fest's.
  Making those symbols in declare (tsc does it in its binder) would let them go parallel too.
- **`Symbol.flow_start`** (the declaring flow container) was set by any walk of a binding and read by every walk
  referencing the symbol (`outer_reference`): only the first declaring file's walk writes it now, and a reference to a
  symbol first declared elsewhere is outer without reading it (tsc's answer; the old serial walk said "not outer" when
  the referencing file came first: no test or corpus changed).
- **`flow_unreachable`**'s flags were written by every walk (also racy under `-b` before): constant now.
- A pre-existing **check-phase race**: `class_decl_typecheck()` wrote `FunctionImpl.private_ambient`, a bitfield
  neighbouring what other checkers read: set in the walk now.
- Deferred reports into the walk's two scratch `ErrorCtx`s (dropped ones) go to the shard's discarded context.
Validation: suite 12932 unchanged; `baseline_tests --dump` byte-identical old vs new and, with
`TTTT_PARALLEL_RESOLVE=1 --threads 4`, vs one thread; every corpus's `--all-errors` byte-identical to the old binary,
`scripts/threadcheck.sh` identical; TSan 0 warnings on every corpus but drizzle and effect (not run: slow) and on the whole
suite forced parallel; sweep 8072 match, 0 crash (unchanged). Numbers (best of 3, `--time` resolve names, 4 threads,
2 cores x SMT): **vscode 593 -> 283 ms** (walk 510 -> ~200; wall 2.47 -> 2.23 s), tsc 77 -> 39, webpack 72 -> 63
(its JS is serial). Single-threaded callgrind: tsc +0.2%, nest +0.6%, react +0.2%, vscode +0.2% (shard bookkeeping;
a quadratic sort of the files by size was +2.8% on nest before `qsort`).

### Stage 2: JavaScript files in the parallel lane: **done** (2026-10-06)
Classifying hosts as private or shared (the original idea) doesn't hold for JavaScript: "a local all of whose
declarations are in this file" is also what other files' aliases resolve to (`module.exports = Foo`, then
`require()` elsewhere reads Foo's flags through `symbol_for_meaning()`), and the declarations read each other
(`ns.C = function () { this.x = v }`: the expando sets the function's `class_symbol`, which the `this.x` reads; `a.b = {}`
then `a.b.c = v` looks a.b up), so deferring only some reorders them. Instead a JavaScript shard defers **all** of its
assignment declarations, in walk order, and replays them back to back (`js_shard()`, `DEFERRED_JS_*` in scope.c):
`add_expando()` (nested `f.x = v`, the declare step's chains in function bodies; `op->expando_bound` is still set in the
walk, which reads it), `add_define_property()`, `add_global_this_chain()` (a script's `{ this.x = v }` declares a
global), `add_this_property()` (both `this.x = v` and `/** @type */ this.x;`), and `scope_mark_jsdoc_class()` unless the
symbol is a function's local (`is_local && function`, which the file's walk made). What the audit found besides:
- **The flow container read-back** (`expr_link_assignment_declaration()`) happens when the walk reaches the target: a
  declaration made at a block's start was linked, one made after its left side was resolved (`this.x = v`, a nested
  expando) never was. `DeferredEvent.link` records which (`walk_unvisited()`: no `PropertyContext` yet) and the replay
  links only those; stage 1's TypeScript expandos linked always, now the same rule.
- **`binder_require_symbol()`** is made before the walks when the program has a JavaScript file (a `log_error` if a
  walk would make it).
- `push_global_expando()` is only reached from deferred events now (and is dead after `binder_link_global_expandos()`
  anyway); `scope_note_module_exports_access()` writes the file's own `module` local, the CommonJS exports are declared
  before the walk (`add_nested_commonjs_exports()`), hoisted `var`s too: nothing else of the JS walk writes another
  file's state.
- The cost: the rest of a JS file's walk doesn't see its own declarations (SYMBOL_CLASS of a constructor function,
  members) where the serial walk did after them. Nothing in the suite or the corpora read them (`hacks.md`).
Validation: suite 13232 unchanged; `baseline_tests --dump` byte-identical old binary vs new and new
`TTTT_PARALLEL_RESOLVE=1 --threads 4` vs one thread; `--all-errors` of webpack (695 lines, unchecked JS), react,
vscode, tsc-src byte-identical to the old binary; every corpus 0 diagnostics (effect its 7); `scripts/threadcheck.sh`
identical on webpack, react, vue, excalidraw; TSan 0 warnings on webpack at 4 threads and on the 1051 JS suite tests
forced parallel; sweep 8588 match / 6 spurious / 1 missing / 0 crash. Numbers: webpack `--time` resolve names, best of
5, default threads (4 on 2 cores x SMT, the machine shared): **63.3 -> 36.1 ms** (the walk ~22 ms on 4 threads, the
replay and merge ~3 ms; walking JS in parallel without any deferral measured 34.4, so deferral costs ~2 ms), wall
0.30 -> 0.27-0.28 s. Callgrind K1 (`scripts/instrcount.sh`) HEAD -> end: tsc 4.628 -> 4.629 G, type-fest 3.104 -> 3.104,
valibot 4.221 -> 4.219, react 1.518 -> 1.520, nest 1.260 -> 1.263 (+0.24%), webpack 3.866 -> 3.873 (+0.18%), zod 4.565
-> 4.569, xstate 2.961 -> 2.962, drizzle 10.783 -> 10.783, vue 7.956 -> 7.958, effect 9.812 -> 9.808, vscode 32.447 ->
32.485 (+0.12%), excalidraw 5.040 -> 5.041, tanstack-router 4.627 -> 4.627.

### Stage 3 (maybe never): declare and link
40 + 28 ms on vscode. Declare writes `Binder.globals`, ambient modules and namespace exports shared by files; it would
need per-file tables merged in program order (tsc's mergeSymbolTable order). Measure after stage 1: if the serial
remainder is < 10% of the wall at vscode scale, stop here.
