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
