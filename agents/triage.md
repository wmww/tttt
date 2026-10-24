# Triage
Uninvestigated items: dump here what you don't want to deal with now. Whoever picks an item up drains it into a fix, `bugs/*.md`, or `todo.md`.

Last drained 2026-10-20. mikro-orm's `string[]` against
`OperatorMap<T> & FilterObject<T>` was IntersectionState.Target leaking into the generic mapped target's template
comparison (fixed; the fallback cut is wide again, mikro-orm's probe 258k types against tsgo's 393k, its chain
identical; the general stickiness is fixed since, 2026-10-21). never_reduced_literal_prop() honours
HasNeverType (fixed). The combined import's member symbols went to bugs/binder_modules.md, the apparently-any
intersection's property symbol to bugs/type_core.md. 


- sequelize (the root config) varies at the defaults (2026-10-23, big; agents/measurements.md "Large projects at the
  defaults"): 1 run in 3 reports TS7022 on `models` at packages/core/src/sequelize-typescript.ts:335 and then loses the
  TS2322 at :1053 and TS2488 at :1080 that tsgo and the other runs report (the circularity makes `models` any). tsgo's
  own sequelize/packages/core TS7022 depends on its checker count too (kdiff.py's doc), so the circularity is
  order-dependent in both; tsgo's one-checker answer is ours at K1. Find which file checked first in the same checker
  makes `models` circular (`TTTT_FILE_TIMES=1` on a varying run, then `TTTT_CHECK_ONLY=that,sequelize-typescript.ts
  --checkers 1 --threads 1`, `TTTT_DEBUG_CYCLE=1`) and compare with tsgo at the same order. [open]
- scripts/reduce.py on openclaw (2026-10-23, corB): `scripts/reduce.py --project
  /home/ai/tttt/thirdparty/sources/openclaw --config test/tsconfig/tsconfig.core.test.gateway-other.json --keep
  "placement-projector.test.ts.*TS2345" --tttt-has` printed "the copy does not reproduce" with no differences: the
  copy's generated config had `"files": []` (reduce.json's cfg had all 629, so something rewrote it empty before the
  probe), and with the files put back by hand the copy's workspace packages don't resolve
  (node_modules/@openclaw/session-url-contract/src/*.ts: TS2307 for `@openclaw/normalization-core/*`, pnpm workspace
  links). What worked instead: a /tmp tsconfig extending the sub-config with one file in `files`, `rootDir: "/"`,
  `typeRoots` pointing at the checkout's @types, a node_modules symlink and a package.json with type=module. [open]

- tttt -b on TypeScript6/src reserves ~12 GB of address space on 16 cores (ast_alloc.c:83 panic under ulimit -v 12 GB; realworld.py's and clidiff.py's caps raised to 48: typescript-eslint's -b reserves 22-28 GB, the same at 4eadfcc74, varying run to run with the load): look at the per-thread/per-builder AST and arena reservation sizes, they scale with the core count. [note]

- Missing TS2322 (2026-10-21, IntersectionState audit): `function f<X extends string>(m: Uppercase<X>) { let y:
  Uppercase<X> & { z?: 1 } = m; }`: tsc fails `Uppercase<X>` against the weak member (its chain: `Uppercase<string>`
  has no properties in common with `{ z?: 1 }`), tttt accepts it (before and after the audit). [open]
- Chain (2026-10-21): `C<X>` (conditional) to `D<X> & { z?: 1 }` where D<X> fails: tsc's chain goes through the
  default constraint's union members against the whole intersection and repeats `C<X>` to `D<X>` inside it; tttt
  reports the D<X> member directly (test/cases/ttttIntersectionStateNotPassedToConditionalBranches.ts, as an
  assignment instead of the overload) [text]

- Source sweep policy (2026-10-08 idea, unexplored): if checking every `tsconfig*.json` turns out noisy, mine each
  repository's own `tsc` invocations from its package.json scripts instead. [note]

- Loop partial evaluations, leftovers after the loop-pass diagnostics (2026-10-22, worker cor): (1) tsc's types
  baseline prints `someFrom(isSome(null) ? result.some : makeSome())` in typeGuardsAsAssertions' null-arg mutant as
  `{ some: a; }` (an uninstantiated return: the nested resolution under the flow loop leaves resolvedSignature as it
  was), tttt `{ some: any; }`; (2) `while (cond) { const q = result.some; result = someFrom(q); }` is TS7022 on q in
  tsc, nothing in tttt (the circularity through the loop's assigned value). (3) Call argument errors (TS2345...) of a
  nested call resolution under a loop in progress aren't reported from the partial pass (call.c resolves without
  errs under flow_uncached): wire them like expr_property_access_errors() if an oracle shows one. [open]

- K-dependence hole of the loop-pass diagnostics (`ty_flow_partial_reports()`, 2026-10-22): a partial evaluation
  reports only in the checker checking the expression's file. If another file's check evaluates that loop first (an
  inferred return type: `export function f(a: number[]) { let y; while (a.length) { y = a[y] } return y }` in a.ts and
  `import { f } from "./a"; const r = f([])` in b.ts checked first) its partial pass is silent, and at K1 the same
  checker later reads finished memos for a.ts (`flow_expr_type_in` returns a done memo, no rerun), so a.ts's TS2538 could
  be missing at K1 and present at K16. Not seen by checkerdiff/kdiff yet; if it shows, rerun the partial pass for the
  own file (or record foreign partial errors per file and replay them when that file is checked).
  (That exact two-file shape matches at K1 and K2: the walk's own evaluation of the loop still runs uncached. A real
  repro needs the loop's assigned value memoized as done by the foreign check.) [note]

- Stored variances differ from tsgo's on drizzle (2026-10-22, `scripts/variancetrace.py
  thirdparty/corpora/drizzle/drizzle-orm/tsconfig.corpus.json`: 621 measured by both, 37 differ). Mostly the
  unreliable flag (16): the views (`GelView`, `PgView`, `MySqlViewBase`...) are tsgo `[17 17 16]` against tttt
  `[0 0 0]` (tsgo covariant-unreliable, tttt invariant), the tables `[16]` against `[17]`, `AnyColumn` 24 against 8,
  `PgColumnBuilder`'s last 4 (independent) against 3. drizzle's output matches anyway; `--block NAME` shows one side by
  side. Unexamined: which side's measurement order differs first. [open]

- Types baselines: import type specifiers differ from tsgo's. With `moduleResolution: bundler` tttt prints `import("./x.js")` where tsgo prints `import("./x")`; with nodenext + allowImportingTsExtensions tttt keeps `.ts`, tsgo drops it. Seen in test/cases/ttttTypeofLocalFreeParams.ts while its options were being chosen (repro: that file with `// @module: esnext` / `// @moduleResolution: bundler`). [text]
- (fe3, 2026-10-22) The debug build's suite aborts at `ttttJsDeclarationMapsTemplateTags`: `parser.c:518: check failed:
  !parent->has_panding_child` (release builds don't check). So `./debug/baseline_tests` can't run the whole suite;
  owner: the parser. [open]
- (fe3, 2026-10-22) Checker threads grow their glibc malloc heaps 4 KB at a time: ~13k `mprotect()` calls on vscode
  (T16, strace -f -k: 11.6k of 4 KB), each taking the mm lock for writing while other threads fault pages, 3-7% of
  the checker threads' timeline samples (`mprotect<lazy_type_state`, `mprotect<declare_class_members`). Sites by count:
  lazy_type_state 7141, declare_class_members 1816, declare_symbol_flags 1435, literal_builder_add 911,
  unused_register 619, builder_sort_stable_from 190, builder_find_prop 183, check_object 153. M_TOP_PAD during
  checking doesn't change the count (128 KB / 1 MB / 4 MB tried). For a checker-speed worker: move those allocations
  into the checker's arena.
  Fixed (ck2, d1c9ff999): a non-main glibc arena's heap grows by exactly what the allocation at its top needs
  (sysmalloc's grow_heap ignores M_TOP_PAD there, hence no effect), so every long-lived checker-thread malloc past the
  high-water mark cost an mprotect; nearly all were lazy_type_state()'s memo pages (the other sites only happened to
  allocate at the top), now in the checker's arena: vscode 18231 -> 22 mprotects. The temporaries stay malloc. [fixed d1c9ff999]

- Bun-only repositories (2026-10-22, source sweep refresh 4): 16 GitHub search hits for the application-code rule had
  only a `bun.lock`/`bun.lockb` and were skipped, because thirdparty/ensure-sources.sh has no bun install (the bun topic
  was almost all of them; also Hikari, SveltyCMS, better-svelte-email, gramio, StreamBot, mcp-linker: repos.txt's
  "Refresh 4" comment lists them). Adding `bun install --frozen-lockfile --ignore-scripts` (a pinned bun next to the
  pinned node) would open that slice: modern application code, where private projects live. [harness]

- The oracle's recycle mode (`scripts/oracles.py` -> `scripts/kdiff.py -k 1 -m 0`, 300 s per side) times out
  dokploy/apps/dokploy under load (2026-10-22, two oracles runs at load ~25-30): alone with the oracle's flags
  (`--threads 1 --checkers 1 --checker-memory 0`) it takes 310-322 s at load 26 and exits 0, slow not stuck (kdiff.py's
  docstring: ~270 s quiet). Either raise that mode's cap to 600 s in oracles.py/kdiff.py or accept the "1 timed out"
  noise in the recycle line. [harness]

- The perfsweep tsgo cache (`test/perfsweep/tsgo.jsonl.gz`) lacks the source sweep refresh 4's 40 repos (~71 projects):
  the oracles run at d5d4cb2fc measured them at load ~25 (a slow tsgo side flatters tttt), so it wasn't committed; that
  loaded copy is at /tmp/wt-merge-scratch/perfsweep-tsgo-d5d4cb2fc.jsonl.gz (scratch, may be gone). Regenerate the new
  projects' lines from the main tree when `uptime` shows load < 4 (`scripts/perfsweep.py --tsc` or its per-project
  refresh) and commit that file alone. [harness]

- Missing TS2536 on an indexed access of a recursive conditional alias inside its own declaration (cor4, 2026-10-22):
  `type A<T, D extends unknown[]> = T extends unknown ? { self: A<T, [...D, 1]>; next: A<T, [...D, 1]>["self"]; tag:
  D["length"] extends 8 ? "x" : number } : never;` is TS2536 at `A<T, [...D, 1]>["self"]` in tsgo ('"self"' cannot be
  used to index type 'A<T, [...D, 1]>'), nothing in tttt; with `extends 2..6` tsgo also reports one more TS2322 on
  `const r: R<0> = a` against `interface R<X> { next: R<[X]>; self: R<[X]>; tag: number }` (2 vs 1, 3 vs 2). [open]

- Per-checker memory is the fault/RSS lever on small projects (2026-10-23, fe5; `--time` prints faults per phase):
  react peak RSS ~300 MB at the defaults against ~169 at --threads 1 (nest ~195 / ~136), typecheck 16.8k minor faults
  against 7.7k, libs 2.9k against 0.2k. Suspects: checker arenas starting on huge pages (Arena.huge_first, 2 MB+
  populated per checker for 1-4 MB used), 8 sets of intern tables, per-thread malloc heaps. For a checker worker. [note]

- The indexed-access recursion identity rule (tsc's getRecursionIdentityTarget: an indexed access takes its object
  type's recursion identity) is right in principle, but its tttt version (wt-cor4's d8a78552b, landed as 20fa22369,
  reverted on 2026-10-22) blew up youtube-enhancer: a spurious TS2589 + TS5114 ('NonNullable'
  appears infinitely circular) at src/validation/constraints.ts:6, an object literal against
  ConfigurationNumericConstraints, with 1.16M types against tsgo's 211k. It needs a cut-down repro from that file
  (ConfigurationNumericConstraints, NonNullable) before re-landing; the probe where it changed 0 -> 2 errors is in
  cor4's notes and commit d8a78552b's message. [note]

- **--shared-graph: a TSan group in ty_reduced_slow()** (sg2, 2026-10-22): `*intern_slot(me, &my links, {KEY_REDUCED_UNION, ret->id}) = ret` (checker.c) races with another worker's intern_get() of that key (excalidraw T4, flag + content order). A write of a slot that may be another worker's published block; flag-only, uninvestigated. [open]
- **--shared-graph's drizzle residue C is back at ~35% of loaded K12 runs** (sg2): 5f2cd6996 20/20, first exposed by d42a3c0e0 (a parser inlining change: timing only); gone under TTTT_NESTING_ORDER=content (40/40). agents/notes/sharing_design.md §4. [open]

- Laptop item: nest's libs-phase sys time with the 4 KB-page AST slab (wt-fe6's f4c2d3d25, `TTTT_AST_SMALL_KB`). The
  worker measured `--time`'s load+parse libs sys *down* (13.5 -> 8.7 ms, huge pages forced on); the merger measured it
  *up* (median of 7: 12.6 -> 25.9 ms, minor faults ~2.7k -> ~7.7k in that phase; typecheck sys 10.1 -> 7.8 ms) at load
  46 with whatever huge pages the box had. Neither box was quiet. Measure on the laptop: nest `--time` libs sys and
  wall, default against `TTTT_AST_SMALL_KB=0`. The RSS win stands either way (nest 210 -> 169 MB, react 293 -> 255). [note]

- (cor6, 2026-10-22) A missing TS2502 through a signature resolved while it is being resolved: tsc reports
  `extra` (1,38) and (4,35) TS2502 plus TS7023 on h, tttt only a TS2345 against parameter type 'undefined'
  (signature_resolve_slow()'s SW_LOOK_MINE hands the re-entry the partial parameters; tsc resolves each parameter's
  type lazily, so the cycle lands on the parameter symbol). No real repository needed it yet (dev-3.0's spurious
  "Expected 1-0 arguments" was the same re-entry, avoided by tsc's applyToReturnTypes order):
  `function g(a: number, b: number = 1, extra?: ReturnType<typeof h>) { return a; }`
  `function h(a: number, b?: number, extra?: Parameters<typeof h>[2]) { return g(a, b, extra); }  h(1, 2, 3);` [dup]
- (cor6, 2026-10-22) `./debug/tttt -p thirdparty/sources/emailthing/apps/api --noEmit` aborts with `free(): invalid
  pointer` at the default threads (every run; HEAD 8a20b3d3c's relate.c too), not at `--threads 1`; release builds don't.
  A debug-only heap misuse on some thread path: run it under valgrind/ASan. [open]

- dtsdiff at the 7.1.0-beta cache (2026-10-23) lists b4run/packages/postgres-storage as "CRASH (no output)" in its
  emit-mode sandbox run, while `tttt -p ... --noEmit` on it exits 2 with five TS2307 (`@b4run/sdk`,
  `@b4run/permissions`: workspace packages without built declarations; the beta's `-b` sweep write-up in
  bugs/sourcesweep_build.md mentions b4run too) and `dtsdiff.py --show` prints nothing for it. Find out what the
  sandboxed emit run does there (exit code, stderr) and whether tttt should still write the project's declarations as
  tsgo does (1503 projects emit; tsgo wrote this one). [open]
- **Types baseline: a nested package's module named by the bare specifier** (2026-10-23, corC): with
  `r/node_modules/foo/node_modules/nested` reached only through `foo`, tsgo's types baseline prints
  `Promise<typeof import("foo/node_modules/nested")>` for `foo`'s `load()`, tttt `typeof import("nested")` (the
  specifier as `foo`'s declaration file writes it). Declaration emit's TS2883 already matches (it computes the specifier
  from the entry file); the types baseline's import type printing doesn't. Repro: test/cases/ttttDeclarationUnsafeImportModuleName.ts
  without its `@noTypesAndSymbols`. [text]
- **Symbols baseline: a CommonJS export assigned inside parentheses** (2026-10-23, corC): `})(Kind = exports.Kind ||
  (exports.Kind = {}));` gives `exports.Kind : Symbol(Kind, Decl(enums.js, 4, 25))` in tttt, `4, 27` in tsgo (the
  inner `exports.Kind`'s start; tttt's column is two before it, the space after `||`). Repro:
  test/cases/ttttDeclarationCommonJsExportedAliasMap.ts without its `@noTypesAndSymbols`. [text]
- **`import("pkg/dist/x.js")` against tsgo's `import("pkg/dist/x")` in a printed type** (corF 2026-10-23): the tools
  worker's kebo case (scratchpad briefs/kebo-ts7022-case.ts, mobx-state-tree under node_modules with `typings`,
  module preserve / bundler) prints `types.model`'s return type with the `.js` extension where tsgo drops it. Uninvestigated. [text]
- bun's 54-L02a (2026-10-23): a misplaced `export { x as y }` in a function or block (TS1233) declares an alias
  in the block's locals that nobody resolves while binding (port_statement_lookup_names returns early for
  wrong_context). A use of `y` inside a deferred body then resolves it from a checker: symbol_resolve_alias on the
  frozen binder, scope.c's "Alias resolved while typechecking" log_error, a shared-state write during checking (a
  race between checkers in principle). Output also differs with --no-defer-bodies: deferred, the checker-time
  resolution reports tsc's TS2304 for the block cases (`{ export { bbb as ccc }; ccc; }`'s bbb, a script's b1);
  without deferral nothing does. Resolving the aliases in the walk (`symbol_resolve_alias` for each item, exempting
  the deferred body's resolve shard from the log) removes the log and makes both modes agree, but on the poorer
  output: the TS2304s come from resolve_alias_target's unreported_errs path, which needs understanding. tsc reports
  the specifier's TS2304 only when the alias is used (www -> xxx, a type use sss -> ttt), and module exports'
  aliases through checkExternalModuleExports presumably (bbb). tsgo's output for the case is in test/buncases. [open]
- `thirdparty/ensure-typescript.sh`'s Go build cache cap (clean when over 4 GB) ran while another process was linking
  from the same GOCACHE (tsgotrace/overlaycheck builds share it): the link failed with "cannot open file .../cache/..:
  no such file" and the rerun succeeded. Take the script's flock around the clean, or clean only when no `go` process
  uses the cache (2026-10-23). [open]
- A gate run during /tmp quota exhaustion (oracles at 03bf32424, 2026-10-23) produced 487 false kdiff suite
  differences (dump writes failed; 0 differ at 03bf32424 and at 85b7c7520 on a free /tmp) and 9 openclaw SIGKILLs:
  oracles.py should check free space and write failures before comparing dumps. Also: the suite's K1/K16 kdiff runs
  at `--threads 1`, where checker 0 apparently takes every other checker's unstarted share (deal_files / check_share), so it
  compares little; `--threads 16` would deal for real. [open]
- `collect_free_params` (src/ty/lower.c) gives up (`FreeParams.unknown`) past MAX_FREE_PARAMS (32) outer type parameters
  or MAX_BOUND_PARAMS (64) `infer`s, and a conditional type whose outer parameters are unknown takes
  instantiate_conditional's uncached path (the composed mapper). That path got inference wrong: with the limit of 32
  true branches remembered (lifted on 2026-10-23, ttttConditionalChainManyBranches), `mk<T, M extends MapOf<T>, K
  extends keyof M>(t, "ev", h)` over a 33-branch MapOf had K's constraint `keyof M` come out never (TS2345), though
  `<T, M extends MapOf<T>>(t: T): M` inferred M right. So the unknown fallback is not just conservative: a chain of 65
  `infer`s or 33 outer type parameters still takes it. [open]
- `export const g = (h = g) => 1` (declaration emit on): tsgo reports TS7022 on g (the parameter's default reads g
  while g is in flight); tttt reports nothing (before 2026-10-23 it reported TS5088 instead). A circularity through a
  parameter initializer of the variable's own function expression. [open]
- Text only (opencode, 2026-10-23): an optional property declared `never` (`{ readonly name: string; readonly
  service?: never }` in a union) prints as `service?: undefined` in tttt's elaborations and as `service?: never` in
  tsc's (the second line of test/cases-style probes relating `Omit<Identity & ...>`); tsc prints the declared type. [text]
- scripts/reentrytrace.py's tsgo side passes `--incremental false`, which a composite config rejects (TS6379), so it
  reports "tsgo: none" on composite projects (opencode's packages/core). Running the instrumented tsc by hand with
  `--noEmit` checks, but it writes the config's tsBuildInfoFile (node_modules/.ts-dist/*.tsbuildinfo) into the
  checkout even with `--tsBuildInfoFile` elsewhere: delete it after. [tooling]
- Spurious, K-dependent TS2615 on a circular mapped alias (det3, 2026-10-23): `export type Loop<T> = { [K in "a"]:
  Loop<[T]> }["a"];` in lib.ts, with `const x: Loop<1> = null!` in a.ts and in b.ts. tsgo, at any K, reports TS2456
  lib.ts(1,13) plus TS2315 at lib.ts(1,37), a.ts(2,17) and b.ts(2,17). tttt reports the same, plus a TS2615 "Type of
  property 'a' circularly references itself" at lib.ts(1,37) at K1, and at a.ts(2,17) and lib.ts(1,37) at K2. Two
  bugs: the TS2615 shouldn't be there once the alias is circular (tsgo resolves it as the error type first), and its
  site depends on the checker. The site is K-independent since the history rerun (det3, 2026-10-23). The spurious one:
  tsgo's resolveMappedTypeMembers resolves the declared template eagerly (getTemplateTypeFromMappedType), so the alias
  cycle is found there, before any getTypeOfMappedSymbol pushes. tttt's members.c lowers the template lazily, under
  the property's RESOLVING_PROP (a comment there says so). Lowering it eagerly at the members (tried: the root
  MappedInfo's ty_mapped_template() before `Prop* template`) fixes this case, but breaks bun92-R16b (its TS2615 pair
  goes) and bun93-R01d. tttt's `keyof` of a mapped type resolves the members, which tsgo's getIndexType doesn't, so
  the eager template meets the other mapped type's members in flight. Needs keyof's laziness first. [open]
- **Circularity codes in the one-checker recheck trigger** (corH, 2026-10-23, counting stopped by the npm priority):
  tsgo's caches report TS2615/TS7022/TS7023/TS7024/TS2456/TS2502 in 111 of 6773 source-sweep configs (77 of them
  nest's TS7023, 18 repos), 0 of 8595 DT packages, 138 of 2411 bun cases. TTTT_DEBUG_CYCLE=1 (it now prints each
  step's file) at K1 printed a LazyType cycle in 11 of the 111, all within one file; the other 100 are return-type
  cycles the trace doesn't show (TS7023). kdiff -k 16 on those 18 repos' 1274 configs, twice: 0 differ. So no flake in
  the sweep from these codes yet; the known ones are bun 92-R16b and sequelize. Since det3's commit after
  ca2787123 the rerun triggers on these codes outright (documentation.md). [note]
- A class type parameter's default isn't substituted in a printed method constraint (det3, 2026-10-24):
  `declare class D<F, X = ETR<F>> { m<N, Z extends ByName<X, N>>(): void }` with `declare const d: D<typeof s>`.
  tttt prints `d.m : <N_1, Z extends ByName<X, N_1>>() => void`, tsgo `ByName<ETR<{ ... }>, N_1>`. Types component
  only. [open]
- Missing TS2415 for a class extending a type parameter's instance (cor10, 2026-10-24): `const NoopBase = class {} as
  new <t extends object>() => t; class CastableBase<t extends object> extends NoopBase<t> {}` is TS2415 in tsgo
  ("Class 'CastableBase<t>' incorrectly extends base class 't'. 'CastableBase<t>' is assignable to the constraint of
  type 't', but 't' could be instantiated with a different subtype of constraint 'object'."), nothing in tttt
  (arktype silences it with @ts-ignore; test/cases/ttttClassBaseTypeParamDynamic.ts too). [open]
- Extra elaboration line on a named-union intersection (corB, 2026-10-24): assigning `(As<any> & As) | undefined`
  (chakra-ui's `as` prop, As an imported alias of React's ElementType) to `number` gives tttt a second line "Type
  'undefined' is not assignable to type 'number'." that tsgo doesn't print (header line identical). Text only, seen in
  a probe while fixing the 0xsonic TS2590. [text]
- Instantiated indexed access through an optional property (corB, 2026-10-24): `type I<C extends { g?: string[][] }>
  = C["g"][number]` instantiated with C = `{ g?: string[][] }` is `unknown` in tsc (getIndexedAccessType without an
  access node: nothing found on `string[][] | undefined`, unknownType) and likely `string[]` in tttt
  (ty_access_without_node skips nullable members; only the constraint path uses ACCESS_NULLABLE_FAILS since the
  seal-hub TS2344 fix). Unverified on real code. [open]
