# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-07 (triage session, todo entry 10 of the eleventh window): four Opus sub-agents (type core,
binder/modules, display/text, parser/missing checks) took every item, then second tasks (fresh mutation seeds 16
multi/js and 17, the last vscode error, flagflip leftovers, errtextdiff). ~70 fixes; the rest written up in
bugs/type_core.md, binder_modules.md (new), display.md, harness.md, missing_checks.md, tsgo_divergences.md (new),
plans 26/27/39 Leftovers and plan 50 (done 2026-10-08); the corpus re-pin configs moved
to plan 19. The commit message has the numbers.

- Perf (2026-10-08 perf session): kysely-style code makes 5-12x tsc's types (function expression types keyed by the
  whole mapper; bugs/perf_leads.md "The source sweep finds pathologies" has both open leads). Worth a plan if the
  source sweep shows more of it. vscode's AST bytes per line (~600) weren't looked at.

- (2026-10-08, plan 51 prototype A) vue at `--checkers 1` reports one TS2345 (vModel.ts:446), also at a7003eac; 0 at
  the default 4 checkers: a checker-order dependence (`scripts/checkerdiff.py vue` should show it). The same TS2345 appears at every checker count once root
  files are in tsc's include-pattern order (agents/bugs/include_order.md): fixing it unblocks that port.

- (2026-10-08, plan 52 drain) Config level leftovers: no TS5023/TS5025 at all (unknown names inside `compilerOptions`,
  tttt has no table of tsc's option names: only unocss/examples/astro in the source sweep, 7 diagnostics); a root
  file's TS6305 lacks tsc's "The file is in the program because" chain (program_load()); a `/// <reference path>` to
  a referenced project's unbuilt source says TS6053 where tsc says TS6305 (program_load_reference_path()).

- Message from human: it seems you've bumped up the typescript version, but maybe not all the way? ideally I'd like this project to be a drop-in replacement for the latest tsc/tsgo (thus implement typescript 7). make sure it implementes and is tested against the latest features of the language.

- Message from human: minor cleanup, please organize all your scripts into a toplevel scripts/ directory instead of keeping them at the repo top level

- (2026-10-08, plan 52 stage 2 agent ss-a, spurious relation keys, second round) Left after the drain:
  discord.js root project `setInterval(...)` typed bun-types' `Timer` where @types/node's `Timeout` is expected (TS2322
  REST.ts:100, TS2345 SpeakingMap.ts:57): which global overload comes first is the program's file order (libs, root
  files and their imports, automatic types), a program-level question, not reduced. solid component.type-tests.ts:100
  (`test1.prop = "a"` through mergeProps' Override/Simplify mapped types, write type `("a" & M4Type[T]["prop"]) | ...`).
  mikro-orm EntityManager.ts:287 `{ ...where }` with where: FilterQuery<Entity> (TS2698): tsc's base constraint of
  `NonNullable<ExpandScalar<Primary<E>>>` is `{}`; probe /tmp-style: `type ES<T> = null | (T extends string ? T | RegExp
  : T)`, `f<E>(w: NonNullable<ES<E>>, v: ES<E> & {}, u: NonNullable<E extends string ? 1 : 2>)` spreading each: tsc
  errors on v only, tttt on u only (constraints of conditional types inside intersections with {}). Many remaining
  +TS2344 are `expectTypeOf`-style assertion failures caused by other wrong types (react-navigation static.check.tsx,
  query *.test-d.ts): look at them per test after the other keys move. Not looked at: svelte's JS sources (JSDoc
  generics, `Value<unknown>` to `Source<V>`), dokploy's react-hook-form `Control<...>` mismatches.
