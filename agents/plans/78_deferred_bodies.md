# Plan 78: deferred function bodies (the memory lever)
Planned 2026-10-16 (todo entry 1 of the nineteenth window) from the insights brief, measured at cb3c529b on the
24-thread machine (a Blender build was running for part of it: RSS numbers are what count, walls are indicative).
**Go**: the gate (>= 600 MB off vscode's program) passes with -1263 MB. Three execution stages = todo entries 2-4.
The probe that measured all of this is `agents/plans/78_probe.patch` (`git apply` it on a scratch tree;
`TTTT_SKIP_ALL_BODIES=1|2|3|5`, `TTTT_PROBE_NO_CHECK=1`, `TTTT_BODY_PROBE=1`, see "Probe" at the end). Tables are in
measurements.md "Plan 78 planning".

## The idea
Function bodies are most of the AST and of the binder's flow nodes and scopes, and nearly every body is needed only by
the checker checking its file. The parser brace-skips a body; the checker *materializes* it (parse, bind, resolve)
when it needs it; it is dropped when no checker incarnation that touched it is alive. It is also the restructuring a
real watch mode needs (plan 76 W3: resolve scoped per body).

## What the gate measured (TypeScript files; `--threads 4`, no check, RSS MB)
| corpus | after parse eager -> skipped | after names eager -> skipped | delta after names |
|---|---|---|---|
| vscode | 1644 -> 671 | 2161 -> **898** | **-1263** |
| effect | 187 -> 126 | 266 -> 194 | -72 |
| drizzle | 101 -> 91 | 143 -> 128 | -15 |
| type-fest | 49 -> 47 | 74 -> 68 | -6 (types only) |
| webpack | 220 | 309 (unchanged) | 0 (JavaScript: eager) |
- **Bodies are ~14 bytes of AST + binder per byte of body source** (vscode: 98.8k outermost bodies, 89.6 MB of their
  source; the parse also gets 2x faster: 655 -> 345 ms at T4, single-thread parse 1550 -> 800 ms and names 1220 -> 390).
- **The skipper is right**: verified against the parser (`=3`: skip, then parse and compare the end) on vscode (419k
  bodies, nested ones included), effect, drizzle, tsc, vue, zod: **0 mismatches**, and on the suite 12 mismatches,
  all in bodies with syntax errors (parser recovery tests, `class C { constructor() { static f = 3 } }`): see
  "Syntax errors". Program file lists and syntax diagnostics identical skipped and eager.
- **Give-ups** (vscode, outermost attempts, 100.7k): 402 (0.4%): `import` 384 (dynamic `import()`: it adds module
  names), `require` 18, one regex scan that met a newline (a division taken for a regex: safe). The old skipper gave
  up on `/` after `)` (467 more on vscode, 21 effect); tracking whether a `(` opens an `if`/`while`/`for`/`with` head
  makes `) /` a division otherwise (485 resolved on vscode, the probe's mode 2). Effect: 39 `require`; drizzle 0.
- **Parameters with default values** (`=5`, such a function's body stays eager, see stage 1): vscode 898 -> 928 (+30),
  effect 194 -> 193, drizzle 128 -> 134: cheap enough for stage 1. Not decorators: vscode's constructor parameter
  decorators are everywhere (a rule that included them kept 520 MB).
- Projected peaks (stage 2): vscode K1 ~= program 0.9 GB + checker 256 MB + the live incarnation's bodies (~1/10 of
  1.26 GB) + malloc'd tables ~= **1.4-1.5 GB** (today 2.55 GB, bun 1.4). T4: program 0.9 + 4 x (128 + ~55) ~= 1.7 GB
  (today ~2.8).

## Cross-file demand (TTTT_BODY_PROBE: every read of a FunctionImpl's body by a checker not checking its file)
Kinds (vscode K1 reads / outer bodies): inferred return type 4161 / 1765, contextual anchors 695 / 171, context
sensitivity 1706 / 133, inferred type predicates 504 / 236; declaration emit, display scopes, pseudo types: ~0.
| | distinct outer bodies | their source | files they are in (their bodies) | needed by >1 incarnation |
|---|---|---|---|---|
| vscode K1 | 1856 of 98.8k | 962 KB of 89.6 MB | 591 (9.5 MB) | 1781 (after a recycle) |
| vscode K4 | 1965 | 1029 KB | 631 (10.2 MB) | 1893 (1604 another checker) |
| vscode K24 | 2092 | 1121 KB | | 2031 |
| effect K1 / K4 | 87 / 147 | 12 / 50 KB | 18 / 37 (243 / 389 KB of 4 MB) | 60 / 143 |
| drizzle K1 / K4 | 58 / 85 | 30 / 44 KB | 36 / 52 (142 / 167 KB of 645 KB) | 45 / 81 |
| webpack | 0 (JavaScript) | | | |
About a tenth of cross demands come *before* the owning file is checked in the same incarnation (vscode K1: 136).
**Decision: re-materialize**, never keep past the incarnations that touched it: cross demand is ~1% of body bytes
per body, ~11% at whole-file granularity, and the incarnation boundary is the only safe drop point anyway (below).

## The finding that shapes stage 2: types point into bodies
A checker's types, links and LazyMemos hold pointers into the bodies it walked (class expressions, object literal
PropDecls, arrow functions' signatures, decl spans for related info and display), and later files' checks reach them
(inferred returns). **A body can't be dropped when its file's check ends; only when every checker incarnation that
touched it is gone** (recycled or destroyed). So a materialization is refcounted by incarnations: acquired on first
use by an incarnation, released by `checker_recycled()`/`checker_drop()` after the segment's after-check pass. This
also settles address reuse (the human's point 2): a checker never sees a materialization dropped while it lives, so
nothing per-checker (links, node_links, LazySlots memo pages) needs purging. What does need an audit is
program-level state keyed by node addresses (`Error.once_key`, anything in Program/Binder built during check).

## Design
**Unit: the file's deferred bodies, materialized together** (stage 1 and 2; per-body is stage 3's question). The
resolve walk writes cross-body state that checkers read (`Symbol.referenced` for unused, `is_assigned*` for
narrowing, `flow_start`, the unused registrations, misspelling budget), so whole-file materialization gives exactly
today's per-file state. Cost of cross demand at file granularity: vscode 591 files / 9.5 MB of body source per round.
- **Parser** (`skip_typed_body()` -> `skip_body()`): every `{` body of a TypeScript (not .tsx, not JavaScript, not
  JSON) function is skipped when `skip_balanced_braces()` is sure, except: a parameter has a default value (its
  lookup sees the body's names: `parameters_see_body()`, `block_add_names(&body, function_scope)` before the
  parameters), the function is an IIFE (the parser learns it at the `(` after it: parse the body then, from the
  recorded span; its flow is the container's), give-ups (`import`, `require`, unsure `/`, too deep). The skipper must
  also record `@ts-expect-error`/`@ts-ignore` comments it steps over (`source_file_add_comment_directive()`, same
  rules as `hop_whitespace_slow()`), or directives inside bodies are lost (TS2578, suppression). Add the paren-head
  rule from the probe. Nested functions are never deferred on their own (a materialized outer body is parsed whole).
  The FunctionImpl keeps `has_body`, the brace span and a `deferred` bit; `Block body` stays empty.
- **Resolve walk** (`function_impl_lookup_names_as()`): for a deferred function the outer walk does everything up to
  the body (generic scope, function scope with the parameters, decorators, return type, flow start node) and stores
  a `DeferredBody` in the binder arena (~64 B x 100k = 6 MB on vscode): the function scope (kept: not
  `scope_release()`d), a snapshot of the FlowBuilder fields `flow_builder_init()` copies from the outer builder
  (dead, has_flow_effects, initializing, parameter_initializing, parameter_function_scope, container_start, outer
  reference), strict mode, `contextual_depth`, and **the outer walk's event sequence number** at that point. The tail
  of the function (`block_add_names` + `block_lookup_names_as` of the body, end_flow, yields, return_flow,
  `flow_builder_finish()`) becomes `function_impl_lookup_body()`, run at materialization.
- **Materialization** (`program_materialize_file(file)`, under a per-file once-lock: deferred -> materializing ->
  ready, with a refcount; a second demander waits like the load pool's in-flight parse): parse each deferred body
  with a parser at its offset (the file's `ParserShared` flags rebuilt: function context async/generator from the
  FunctionImpl), then run their body walks in source order as a `ResolveShard` of its own (thread-local
  `resolve_shard`: `binder_arena()` and `binder_scope_arena()` are the materialization's arena, `binder_errs()` its
  own ErrorCtx, new symbols listed, not-found and misspelling events deferred with their order key). It never
  demands another file's materialization (JavaScript is eager, declarations are bound), so no lock cycle. The binder
  stays `frozen` for everything else.
- **Diagnostics of a materialization** (parse and resolve errors) belong to the file: the owner's check (the one
  checking the file) moves them into its ErrorCtx exactly once; a copy materialized for a cross demand keeps them and
  drops them with it (a later re-materialization produces the same ones). Output is sorted per file anyway.
- **The misspelling budget** (tsc's first ten "Did you mean" suggestions, today replayed in program order by
  `resolve_shard_merge()`): bodies are walked at check time, so the cut can't be made at names any more. Every
  not-found event (names phase and materializations) computes its suggestion (only the first ten per file can ever
  qualify: cap there), keyed by (file program order, outer walk sequence of the enclosing deferred function or of
  the event, sequence inside the body walk): that is exactly today's walk order, because the eager walk visits a body
  as a unit. `program_typecheck()`'s merge strips the suggestion from all but the first ten. Check what else the
  deferred events carry (`DeferredKind`: NOT_FOUND, MISSPELLED, EXPANDO; JS ones don't apply).
- **Syntax errors**: real code has none in bodies, but a body with one can't be told from its skip (the suite's 12
  mismatches change what follows the body). Rule: if any materialization reports a syntactic diagnostic, or the
  program is gated before checking (syntax or options errors elsewhere, where the bodies' syntax errors must still be
  reported), **the run restarts with deferral off** (program-level: reparse everything eagerly). Real code pays
  nothing; error runs pay a second parse. `has_parse_errors` (grammar errors suppressed per file) stays exact.
- **Accessor**: `FunctionImpl.body` is renamed and read through `function_impl_body(f)` (20 readers outside the
  parser, found by renaming: function.c infer_return_type, expr.c collect/anchors/context-sensitive, flow.c error
  and predicate, pseudo.c, declaration_emit.c search_function, ty_display.c name_accessible/statement_scope_at,
  check.c elaborate_arrow_function, class_decl.c check_super_call_root, engine.c, statement.c
  function_collect_local_value_names, ast_walk.c walk_function, display.c, function_impl.c typecheck_body /
  use-strict check / drop / the walk). For a deferred function it makes sure the current checker incarnation holds
  the file's materialization (fast path: the checker's last file; else a small per-incarnation set) and returns the
  body; nested functions inside a materialization need no check (reachable only through it). Outside a checker
  (harness traversal, `--expect-types`, emit) it materializes into a program-held ref.
- **Checks**: `check_share()` acquires the file's materialization before `block_typecheck()` (whole file up front:
  simplest, and the walk needs it all anyway); the segment's after-check pass (unused, declaration diagnostics and
  emit) runs before the recycle, so it sees the bodies; refs are released at `checker_recycled()`/`checker_drop()`.
  The declaration-only loop after the share (unchecked files' declaration diagnostics) acquires on demand.
- **K1 wall**: at `--checkers 1 --threads 4` the bodies' parse+resolve (vscode ~1.6 s single-threaded) moves from the
  parallel phases into the one checker (+~5% net). Stage 1 measures it; the fix is a **materialize-ahead** helper:
  idle threads materialize the next files of each share (a window bounded in bytes), like the load pool. At the
  defaults (K = threads) the work spreads over the checkers and the parse/names phases halve: net faster.

## Stages
### Stage 1: done (2026-10-17, todo entry 2)
As designed, with these differences (documentation.md "Deferred bodies" has how it works now, hacks.md the debts):
- **No misspelling budget**: tsgo has none (no maximumSuggestionCount), tttt never had one; the scope.h comment about
  "tsc's first ten" was wrong. Not-found events of a materialization are reported at its end (all globals are final).
- **Eager after all** (decided in the parser, each a body reader of the names phase): IIFEs (`undefer_body()` at the
  call), constructors of classes with instance fields (`constructor_locals()`; under useDefineForClassFields at ES2022+
  only with a field without type and initializer: `field_type_from_assignments()`), bodies starting with a string,
  expressions with a `(` in the signature after the parameter list's own (a function type's walk asks
  `ty_function_is_context_sensitive()` of its enclosing function: effect logged it). vscode: 5.3k of 96k.
- **Signature collisions**: the names the signature's walk looks up through the function's scope are recorded; a body
  declaring one (or any name, when the signature had a not-found name) reruns the program without deferral, like syntax
  errors. 0 on the corpora, 111 bodies in the suite's error tests.
- **Writes outside the body**: besides FunctionImpl's own fields (`function_impl_walked()`), a local's assignment marks
  (`symbol_assignments()`: kdiff caught hono's module `let` assigned in a deferred body and read by an arrow function
  walked up front from another file's check), a field's `ts_assigned`, `has_assignment_flow_containers` (atomic).
  `has_explicit_return`, `has_bare_return` and `contains_this` moved out of FunctionImpl's bit-fields (TSan: a
  materialization's writes next to other checkers' reads of other bits).
- **Materialize-ahead**: threads the checkers leave idle materialize in check order, unbounded (vscode K1 T4 12.6 ->
  11.3 s, eager 11.6). A 32-file window behind the slowest checker was 14.2 s: helpers running next to the checker the
  whole time evict its caches. Stage 2 needs a bound that doesn't overlap that way (bursts? memory instead of files?).
- Materializations allocate in per-thread arenas (`binder_materialize_arenas()`) and the file's AST arena; a helper's
  pending reports (they need a checker) wait in `DeferredBodies.unfinished`. `DeferredBody` is 144 bytes (vscode ~14
  MB): the block and the snapshot could shrink in stage 2.
### Stage 2 (todo entry 3): drop at the last incarnation, O(1)
- **One arena per materialization** holding everything it makes: the bodies' AST (`ast_malloc` bumps it while
  materializing, as parse_file()'s thread does for the file arena), scopes, flow nodes, symbols, `AccessSite`s, rare
  structs made by the walk, the yields arrays; the malloc'd leftovers of plan 63 (cooked string and template cstrs,
  `Value` strings) become arena `csview`s for bodies at least. Then the drop is an arena release, no `block_drop()`
  walk; at drop, the outermost FunctionImpls' `body` and DeferredBody state are reset (no reader holds it).
- **Per-thread chunk pool** (no mmap/munmap or fresh faults per body; no prefault or huge-page budget): dropped
  chunks go back to the dropping thread's pool, sized from the file's body source (~14 B per source byte). Addresses
  *are* reused: safe for per-checker state (above); audit program-level node-keyed state (`Error.once_key`, the
  binder's pending lists, Program caches written during check) and purge or re-key it. Chunk reuse must not assume
  zeroed memory (check `ast_new()`/arena users that rely on fresh zero pages; memset if so).
- **LazyType slots**: a materialization's LazyTypes get a slot range fixed per file at its first materialization
  and reused by every later one (same parse, same count), so the process-wide counter (hacks.md) stops growing with
  re-materializations and memo pages for a dropped incarnation go with its checker.
- **Churn on vscode** (to measure): ~1.26 GB of materialized body memory per run once, plus cross-demand rounds
  (591 files / 9.5 MB of source per round, ~130 MB) per incarnation that demands them (K1: up to 10): ~1.3-1.6 GB of
  arena traffic. Fresh pages would be ~400k faults (~0.2-0.4 s, 2-3% of K1); with the pool, faults only up to the
  pool's high-water mark (K1 ~130 MB of live bodies, ~33k pages).
Validate: stage 1's oracles again (byte-identical, `TTTT_AST_POISON=1` for dropped materializations: every reader
must hold a ref), **the memory table** (measurements.md "Plan 78": RSS after parse / after names / peak at K1 and T4,
`/usr/bin/time`, vscode, effect, webpack, drizzle, type-fest, before and after, next to bun's), instrcount +3%.
### Stage 3 (todo entry 4): policy, breadth, closing
- Cross demand: measure re-materialization cost at file granularity (count rounds per run); if it shows in
  instructions, materialize single bodies when the demand is a return type (the 95% kind) and the resolve walk's
  cross-body writes are proven not to matter for it (audit `is_assigned*`/`referenced`/`flow_start` readers), or keep
  cross-demanded files alive across one recycle.
- **TSX**: a JSX-aware skipper (JSX text with quotes and braces, `<` in expression position) or eager .tsx: measure on
  excalidraw, bulletproof-react, tanstack-router (app corpora are mostly .tsx). JavaScript stays eager (expandos,
  `this.x =` declarations inside bodies).
- **Dynamic `import()`**: the skipper records the module name literal of `import("m")` (adds the ModuleRef) instead of
  giving up, if vscode's 384 eager bodies are worth it.
- `-b`: `skip_typed_bodies` folds into deferral (referenced projects' bodies are never materialized); `TTTT_CHECK_ONLY`;
  `--expect-types`; the harness's default (deferral on once byte-identical?).
- kdiff/recycle oracles, perfsweep types ratio unchanged, scaleprobe (one huge function body).
- Close: documentation.md "Deferred bodies" (Parse, Resolve, Typecheck, Memory sections), hacks.md (whatever stayed
  eager and why), delete this plan and the probe patch.

## Validation (whole plan)
Every oracle byte-identical at every stage; the suite 0 lost with deferral on and off; instrcount within +3% on every
corpus; vscode K1 peak <= ~1.5 GB and T4 <= ~1.8 GB (`/usr/bin/time`), effect/drizzle/type-fest not worse, webpack
unchanged.

## Probe (`agents/plans/78_probe.patch`, applies to cb3c529b)
`TTTT_SKIP_ALL_BODIES=1` every TypeScript body the old skipper is sure of, `=2` with the paren-head rule, `=3` verify
(skip, parse, compare ends; counts at exit), `=5` as 2 but eager with parameter defaults; prints `skipprobe:` give-up
counts after names. `TTTT_PROBE_NO_CHECK=1` stops after names. `TTTT_BODY_PROBE=1` counts body reads by kind (own
file, cross TS, cross JS) and the cross-demanded outermost bodies by incarnation, at exit.
