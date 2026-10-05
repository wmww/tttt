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
Each stage: `./fasttest.sh --summary` 0 lost; `baseline_tests --threads 4 --dump` byte-identical to `--threads 1`;
`./threadcheck.sh` and `./checkerdiff.py` identical; every corpus unchanged; a TSan build
(`meson setup /tmp/tsan --buildtype release -Db_sanitize=thread -Dc_args=-g`, `TTTT_NO_REEXEC=1`) clean on the
corpora at 4 threads; `./instrcount.sh` (single-threaded must not regress beyond ~0.5%); `--time` resolve numbers.

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
`./threadcheck.sh` identical; TSan 0 warnings on every corpus but drizzle and effect (not run: slow) and on the whole
suite forced parallel; sweep 8072 match, 0 crash (unchanged). Numbers (best of 3, `--time` resolve names, 4 threads,
2 cores x SMT): **vscode 593 -> 283 ms** (walk 510 -> ~200; wall 2.47 -> 2.23 s), tsc 77 -> 39, webpack 72 -> 63
(its JS is serial). Single-threaded callgrind: tsc +0.2%, nest +0.6%, react +0.2%, vscode +0.2% (shard bookkeeping;
a quadratic sort of the files by size was +2.8% on nest before `qsort`).

### Stage 2: JavaScript files in the parallel lane
Needs JS's nested assignment declarations to stop writing other files' symbols during the walk: classify each
`add_this_property()` / `add_prototype_property()` / `add_expando_member()` host as file-private or shared and defer
the shared ones (the flow container they need is on the `PropertyExpr`'s context, set the same walk: copy it at replay).
`binder_require_symbol()` made up front for JS programs. webpack's walk is 62 ms: worth it after vscode-sized wins.

### Stage 3 (maybe never): declare and link
40 + 28 ms on vscode. Declare writes `Binder.globals`, ambient modules and namespace exports shared by files; it would
need per-file tables merged in program order (tsc's mergeSymbolTable order). Measure after stage 1: if the serial
remainder is < 10% of the wall at vscode scale, stop here.
