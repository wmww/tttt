# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-18 (triage session, todo entry 9 of the twentieth window), three worktree agents alongside. The
message-text blind spot: webpack's text was inference (the visited pair set ignored the variance mode) and drizzle's
three relation divergences (recursion identity of written class references, deferred references normalized, identity
conditional instantiation): both corpora now print tsc's bytes; tsc's `--pretty false` text is cached and clidiff is
in `scripts/oracles.py` (see its commit), the source sweep's `text` buckets are in bugs/display.md. The reused node's
`import("./routers.js")` was really the alias body twin (`Readonly<{...}>`) not being instantiated. dokploy's recycle
"difference" is a 300 s timeout at `-m 0` (kdiff now reports timeouts apart). xstate's TS5115 flicker is the accepted
divergence in bugs/tsgo_divergences.md. Berry lockfiles install (algoliasearch back, 24 / 24). Refresh 2's checkJs
dtsdiff numbers went to bugs/dts_emit.md. The human's two directives and tsc-rs went to todo.md's insights entry.


- **supabase studio: 3 spurious TS2322** (2026-10-19, human conversation; supabase b3757692 `apps/studio`, install
  with `pnpm@11.13.1 install --ignore-scripts --filter 'studio...'`): `Logs.utils.otel.ts(397,3)` (tttt's spread
  type prints two `[x: string]` index signatures), `UnifiedLogs.tsx(429,7)` (TanStack `Table<Row>` to `Table<unknown>`
  through `getAutoFilterFn()`: variance or method bivariance), `CreateCustomOidcDialog.test.tsx(19,5)` (msw resolver
  return types, `HttpResponse<...>` against `DefaultUnsafeFetchResponse`). Unreduced. Also a candidate repo for the
  source sweep (Next.js app, recycle thrasher: measurements.md "supabase studio").

- **Missing TS5115 on a synthetic zod v4 shape** (2026-10-18, clidiff fan-out agent B): this file, cut down from zod
  v4's `$ZodUnionInternals`/`$ZodRecordInternals`/`ZodJSONSchema`, gets tsgo's `(22,18) TS5115 Instantiations of the
  following types appear infinitely circular: 'IsOpt', 'Internals', 'RecOutput'.` (`--strict`); tttt reports nothing:
  its runaway emulation (members_reentered_unguarded(), hacks.md "Eager-base runaway model") doesn't fire here,
  though it does on the real zod (crawlee, unocss, webpack). Also the regression test the TS5115 order fix
  (props_related_impl's missing-properties scan untyped) lacks.
  ```ts
  type OptSchema = { _z: { opt: "o" | "d" } };
  type IsOpt<T extends Some> = T extends OptSchema ? true : false;
  interface BaseInternals { version: 1; opt?: "o" | "d" | undefined; pat: RegExp | undefined; }
  interface Internals<out O = unknown, out I = unknown> extends BaseInternals { output: O; input: I }
  type Some = { _z: BaseInternals };
  interface Type<O = unknown, I = unknown, N extends Internals<O, I> = Internals<O, I>> { _z: N }
  interface UnionInternals<T extends readonly Some[] = readonly Type[]> extends BaseInternals {
      output: unknown; input: unknown; pat: T[number]["_z"]["pat"];
      opt: IsOpt<T[number]> extends false ? "o" | "d" | undefined : "o" | "d";
  }
  interface Union<T extends readonly Some[] = readonly Type[]> extends Type<any, any, UnionInternals<T>> { _z: UnionInternals<T> }
  type RecInput<V extends Some = Type> = [V] extends [OptSchema] ? Partial<Record<string, V>> : Record<string, V>;
  type RecOutput<V extends Some = Type> = V extends OptSchema ? Partial<Record<string, V>> : Record<string, V>;
  interface RecordInternals<V extends Some = Type> extends Internals<RecOutput<V>, RecInput<V>> { def: V }
  interface Rec<V extends Some = Type> extends Type { _z: RecordInternals<V> }
  type _J = Union<[Rec<J>]>;
  type _JI = _J["_z"];
  interface JI extends _JI { output: unknown }
  interface J extends _J { _z: JI }
  ```
- 2026-10-18 (clidiff fan-out): one oracles.py run reported dtsdiff tanstack-table/packages/table-core at 0 / 198
  files; three reruns of `dtsdiff.py -f` matched 198 / 198 at the same commit. Load (clidiff/perfsweep running
  alongside) or a sandbox flake; if it recurs, look at what dtsdiff records for that project (timeout? empty tree?).
- **PGO + LTO build: -8-9% wall on every corpus, trained or not** (measurements.md "Many-core accounting",
  2026-10-19). Decide whether `scripts/` builds release with `-Db_lto=true -Db_pgo=use` from a checked-in or
  regenerated profile (train on a few corpora at K1 and the defaults; profiles must match the compiler version;
  `-Wno-missing-profile`; the LTO link hits `-Werror=stringop-overflow` "writing 128 bytes into a region of size 0" at
  `src/ty/ty.h:96`: look whether that is a real overflow before silencing it). instrcount.txt baselines would change.
- **Many-core perf framing** (same section): on the 12-core box all-core clocks cost 1.62x per checker before any
  tttt overhead; the checker-side ceiling on vscode is ~1.5x wall, the thrashers aside. Future perf plans for many
  cores should price work by per-file sums under real parallel runs, not serialized CPU (plan 85's census did).
- 2026-10-19 (graphql-inspector, `tttt -b tsconfig.build.json --no-write --dts-dump`): `packages/core/src/diff/changes/directive.d.ts`
  has an extra `reason?: undefined;` at line 145 against tsc's emit (`tsc -p ... --emitDeclarationOnly` in a bwrap
  overlay). Every other file of the project is byte-identical. Undiagnosed (an optional-property of a union of object
  literals, likely: tsc's normalized literal union member lists).
- (2026-10-19, agent A) A script file declaring `interface Map { ... }` (no type parameters): tsc reports TS2428 "All
  declarations of 'Map' must have identical type parameters" at the lib declarations too (lib.es2015.collection.d.ts,
  .iterable, .symbol.wellknown, es2026.collection), tttt only in the user's file.
- (2026-10-19, agent A) TS2590 for an intersection written in a declaration's type annotation (`declare const x: U1 &
  ... & U17`): tsc reports it at the type node, tttt at the declaration's name (ty_error_at_current_node()).
