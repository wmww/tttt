# Plan 19: the corpora to zero (loop, like plan 08)

**Status 2026-09-28 (sixth window's corpus session, todo entry 1)**: tsc 0, valibot 0, react 0, nest 0, type-fest 0,
zod 0, zod-root = tsc's own 55, **webpack 18** (was 107; checkJs), **xstate** (new, see below). The history of R1-R21 (identity relation,
homomorphic mapped modifiers, const type parameters, skip-sensitive inference, the permissive instantiation, ...) and
of the zod and type-fest items is in git (this file before the corpus sessions) and in the commits.

## The loop
`./realworld.py` (tsc's `src/compiler`, must stay 0) and `./realworld.py --corpus X` for valibot, type-fest, react,
nest, webpack, zod, xstate (`thirdparty/ensure-corpora.sh` pins them; all are clean under tsc 5.8). Every diagnostic is a
tttt bug. Rank by root cause, not by code: sample the biggest groups, reduce each to a probe in
`test/probes/plan19/<corpus>/`, fix the tsc rule behind it. Record the counts in the commit message and
`measurements.md`; the suite and the other corpora are the regression checks.

Corpus notes worth keeping:
- `./tsctypeat.js TSCONFIG FILE LINE|* NAME` prints what tsc says an identifier's type is inside the corpus, where a
  reduction doesn't reproduce (webpack's `any`s from JSDoc fixing were found that way).
- Grep `./filetest.sh` output for diffs only with `-d` (`FAST=1 ./filetest.sh f.ts -d`): without it no diff is
  printed and "no `^[-+]` lines" means nothing.
- expect-type's `toEqualTypeOf` (valibot, zod test-d files) checks tttt's identity relation: a jump there is real.
- nest: nestjs-i18n's shipped `.d.ts` imports `Path`/`PathValue`/`IfAnyOrNever` from a module that doesn't export
  them (hidden by skipLibCheck), so those are error types; a regression there would look like an i18n bug. A clean
  corpus finds only spurious diagnostics: decorator checks (TS1238/TS1241/TS1270) are a suite item.
- webpack: tsc is clean only with ensure-corpora.sh's pins (watchpack 2.5.1, `node_modules/webpack -> ..`); ~70 s
  wall for tttt at the time of pinning, worth a perf look. `/// <reference lib>` works in program files, the harness
  still ignores it (TODO in `program_handle_statement`).
- zod: a pnpm workspace, root devDependencies from `corpora-locks/zod.package.json`; `tsconfig.test.json` (not
  tsconfig.json, whose noUnusedLocals only biome satisfies upstream). The heaviest corpus (9.1 G instructions, 1.3 GB:
  deep-partial.test.ts alone is 745 MB, `bugs/perf_leads.md`).

## What is left
### webpack (18)
Fixed 2026-09-28 (probes in `test/probes/plan19/webpack/`, one commit each): JSDoc `[T?]` optional tuple elements,
overload groups of merged global functions (node's setTimeout before DOM's), parameter defaults removing undefined
(contextual/JSDoc-typed parameters, closures), JSDoc casts as narrowable references, a declaration's `@type` tag
signature, lazy conditional branches (TS2456), `require("m").X` value-only aliases, generic array-like spreads,
conditional-vs-conditional with `infer`, inference from optional parameters, getter `@type`, JSDoc through comments.
Sixth window's corpus session (30 -> 18): unique symbols kept in declaration unions, any/unknown defaults removing
undefined, `{}` initializers with expandos unchecked, keyof `keyof T`, string-index-only element access, the whole base
constraint for TS2731, description backticks, constructor-declared JS members by flow type, `@this` types unchecked.
Left, every one reduced:
- **Defaulted type parameters leaking into context-sensitive callbacks** (~9: PackFileCacheStrategy's LazyFunction and
  `Promise<PackContainer | Pack>` chains, IdleFileCachePlugin:121, maxAge TS2339): bugs/type_core.md "Inference" first
  bullet, probe `contextual_return_default_leak.ts`. Plan 38.
- **A JSDoc `@template` fixed by a nested JSDoc function type** (MultiCompiler:645 x2 + 649, FileSystemInfo:3859): tsc
  infers `any` there. Plan 38 "JS: a JSDoc @template is fixed by any nested JSDoc function type".
- `@template` on a variable statement (`@template T` + `@type {Record<string, (items: T[]) => X<T>>}`,
  DefaultStatsFactoryPlugin:2733, TS2304 x2): plan 26 "Stage 3 leftovers".
- The module-federation example's TS2322: the arrow's return type comes from its variable's `@type` in tsc (plan 26
  leftovers, "TS6210 related info").
- cleverMerge.js:355 `newFn[DYNAMIC_INFO] = ...`: late-bound expandos (plan 26 leftovers).
- ModuleFilenameHelpers.js:353 TS2540 `matchPart` read-only: only in the whole program (a copy of the file with
  another requiring it is clean), so some other file's view of the CommonJS export comes first. Not reduced.

### zod-root (tsc 55, tttt 55: the same set)
`./realworld.py --corpus zod-root` is zod's root tsconfig (all packages, noUnusedLocals, customConditions). It is
**not clean under tsc**: 55 diagnostics of its own (26 TS2307 for bench deps the workspace doesn't install, 14 TS6133,
8 TS6196, ...), so compare, don't count: `node thirdparty/tsc/lib/tsc.js --noEmit --pretty false -p
thirdparty/corpora/zod` and diff `file:line code` sets with `comm`. tttt's set now equals tsc's. The arktype (bench)
ones were three causes: interfaces extending a type parameter (`DynamicBase<t> extends t`, Members.bases), `const`
type parameters behind conditional/mapped types (ty_is_const_type_variable), and `infer X extends keyof $ & string`
constraints not instantiated (ty_conditional_resolve); execa's `result.stderr` was an instantiation expression in an
alias (`typeof CommonResult<false, O>`) left uninstantiated. Probes in `test/probes/plan19/zod/ark_*.ts` and
`instantiation_expression_in_alias.ts`.

### HACKs the zod fixes left (hacks.md "Inference" and "Mapped types")
- Uninferred type parameters stay generic in the second pass's contextual type only where they'd be `unknown`
  (contextual types don't look through type parameters to constraints like getApparentTypeOfContextualType).

### Seen on the way, not corpus-visible now
- `g(() => 1)` with `g<T>(t: T): T` infers `() => number` (tsc `() => 1`: a return literal isn't widened when the
  contextual return type is a type parameter).
- Display: `X<string | number>` for a distributive alias over a union keeps the alias (tsc only aliases a distributed
  union for a new alias symbol, probe `distributed_union_alias.ts`); `P<U>` where tsc prints the indexed access it
  resolves to (`indexed_prop_is_indexed.ts`); a recursive `RD<J>` prints 15 levels deep where tsc stops at 10 with
  `any` (`typefest/rd_2589_min.ts`).
- Padded properties (`a?: never` from widening) get symbol declarations only from the sibling that has the property;
  tsc lists both (`typefest/exact_missing_padding.ts`). Goes away with the missing type.
- Suite: spurious TS7053 where tttt lacks the rule, not the check: index signatures keyed by tagged strings
  (indexSignatures1), `in` narrowing (inKeywordTypeguard, narrowingMutualSubtypes).

## Lessons and tooling
- Reducing a corpus type bug: inline the library's types into one file (`/tmp/x/a.ts`), then delete and rename until
  it flips. A `log_info` of check/extends/result in `ty_conditional`, or of the operands where something turns
  unknown, finds the step fast.
- Multi-file probes against a corpus: put them in `/tmp/x/` with relative imports into `thirdparty/corpora/...`, run
  `./release/tttt -q <options>` and `node thirdparty/tsc/lib/tsc.js --noEmit <options>` (filetest.sh copies the file
  away, so relative imports break).
- `/tmp/realworld.txt` is shared: parallel agents overwrite each other's; copy it right after a run.
- A diagnostic hidden behind `any`/the error type surfaces when its cover is fixed: counts can go up after a real fix
  (expect-type mismatches were all silent until call arity was rechecked; zod's DeepPartial behind the getter fix).
- Estimates by grouping codes were often off by 5x (R2: ~190 estimated, 890 fixed): rank by sampled root cause.
