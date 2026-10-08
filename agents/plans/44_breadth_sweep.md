# Plan 44: the breadth sweep (planned and prototyped 2026-10-05, ninth window's todo entry 3)
Every pinned corpus is at or near 0, and each one costs a session to pin. The sweep runs tttt on **every
DefinitelyTyped package** (~8200 small typed projects, each `-p types/NAME` with its own tsconfig and tests) and
compares it with tsc 5.8 per package. It finds crashes, hangs and false errors that come up across many projects, and
missing checks too.

## What exists (stage 1, done in the planning session)
- **Checkout**: `thirdparty/ensure-corpora.sh` pins DefinitelyTyped `ac3977d8` (Oct 2 2026, 578 MB, shallow) as the
  corpus `DefinitelyTyped`. DT's pnpm workspace links each `@types/X` to `types/X`; here `node_modules/@types ->
  ../types` does the same for the latest version of each (new LINK form `alias:A=T` in ensure-corpora.sh). The few npm
  dependencies many packages need (undici-types for node, csstype for react, form-data, tapable...) come from
  `corpora-locks/DefinitelyTyped.package.json`.
- **Oracle**: `test/sweep_tsc.js` runs tsc 5.8's API on many projects in one node process (lib source files shared;
  otherwise what `tsc -p` reports). `scripts/sweep.py --tsc [-f regex]` regenerates `test/sweep/DefinitelyTyped.tsc.jsonl`
  (committed: per package, tsc's ms and its diagnostics as (file, line, col, code)). ~60 minutes on 4 cores for all
  packages; rerun only after changing the pin or the dependencies.
- **Decision: compare against tsc's diagnostics, don't drop the packages tsc rejects.** Under tsc 5.8 ~15-25% of the
  packages aren't clean (newer-TS lib names DT's CI accepts, `@types` deps on an older major or on removed packages,
  npm deps not installed). Their tsc diagnostics are the expected set, as in the suite: the comparison is by (file,
  line, code) multiset, like scenarios.py, so those packages still check everything else. A **missing check** under a
  `@ts-expect-error` (DT has 10.4k of them) shows up as a spurious TS2578.
- **Tool**: `scripts/sweep.py` runs tttt (`--threads 1 --checkers 1`, `ulimit -v` 4 GB, 60 s timeout) on every package at
  `-j nproc`, lists crashes/timeouts/log_errors first, then ranks mismatching diagnostics by **root cause across
  packages**: `+TSxxxx` spurious / `-TSxxxx` missing, the message shape (quoted parts elided), and for a diagnostic
  outside the package's own directory the dependency it is in (`in types/three`): one wrong declaration seen by 50
  dependents is one row. Columns: packages, packages it is the *only* mismatch of, diagnostics. `-k KEY` lists the
  instances, `--names VERDICT` the packages, `--last` reuses the last run. Excluded: the ~460 generated
  `types/gapi.client.*` (one shape, npm-only typings). Results in `/tmp/sweep-<tree>.json`.
- **Fixed on the way**: type reference directives resolved through a type root weren't followed through symlinks
  (tsc realpaths every resolved directive), so a `node_modules/@types` linked into a workspace loaded each file twice
  (thousands of TS2300/TS2451/TS2374 in ~30% of the packages). Any pnpm-style workspace hits this.

## First numbers (2026-10-05, measurements.md "Breadth sweep" has the latest)
8208 packages (6968 tsc-clean): **7969 match**, spurious-only 123, missing-only 105, both 10, crash 1, timeout 0; 114 s
wall at -j4, tttt 456 s CPU vs tsc 5314 s. Slower than tsc: carbon-components-react (25 s vs 5.9), d3-geo
(both fixed since: intersect_unions_of_primitives was quadratic in the unions, the relation maybe stack was scanned).

After the Small and six of the Medium root causes: 8072 match. Latest (2026-10-05, todo entry 8, with stage 3 item 1's
versioned packages): **8575 / 8595 match**, spurious-only 18, missing-only 1, both 1, crash 0, timeout 0.

## Stage 2: drain the ranking (corpus sessions)
Work the table top-down like plan 19 does for the corpora: a root cause that spans packages first, single-package
causes by how many packages they're the only mismatch of. Per root cause: a small repro under `test/probes` or a
filetest, the fix, `scripts/sweep.py` before/after (0 newly mismatching packages), the usual suite/corpus gates. Track the
open causes in plan 19's style in this file's "Open root causes" below (or bugs/ for big ones).

## Stage 3: widen the oracle (one session each, in order of value)
1. **Versioned dependencies**: done (test/sweep_links.py, run by sweep.py; 1241 links, 387 versioned packages of
   their own, oracle regenerated for the 584 affected with `scripts/sweep.py --tsc -f` on `test/sweep_links.py --affected`).
   After a DT pin change: rerun the links (sweep.py does) and `--tsc` for everything.
2. **`$ExpectType`**: done 2026-10-06 (`scripts/expecttypes.py`, see "`$ExpectType` ranking" below).
3. **More real npm dependencies** for the packages tsc rejects for TS2307/TS2688 (webpack 5, postcss, moment...):
   cheap, each widens coverage by tens of packages; regenerate the lock and rerun `--tsc` for the affected packages.
4. Beyond DT: the `.d.ts` of the top-N npm packages (types shipped in the package) with a generated test importing
   each, if DT runs dry.

## Perf side-product
`scripts/sweep.py` prints tttt's summed single-threaded wall next to tsc's summed ms (tsc warm, libs parsed once: generous
to tsc). Many small programs stress startup and lib loading, which the big corpora don't: if tttt's per-package cost
is dominated by lib loading, a lib cache (parse the default libs once per process, or an mmapped pre-parsed lib) is
the lever, and `tttt -b` over many projects would share it.

## Validation
`scripts/sweep.py` runs in about 5 minutes and is in CLAUDE.md's script list; measurements.md has the numbers; the top root
causes have repros in triage.md / bugs/ / the list below. Every later session that changes checker behaviour runs it
next to the corpora (0 packages newly mismatching).

## Open root causes
Diagnosed 2026-10-05 by four sub-agents (diagnosis only, no fix tried unless said). Repros in `test/probes/sweep/{A,B,C,D}/`
(single files: `FAST=1 scripts/filetest.sh FILE -d`; directories: `release/tttt -p DIR` vs `node thirdparty/tsc/lib/tsc.js
-p DIR`). Packages = sweep packages affected. Ordered by value for effort within each group; **do the small ones first**,
one commit each, `scripts/sweep.py` before/after.

### From the versioned packages (2026-10-05; updated 2026-10-06 agent B)
node, node/v22, v24, v25 all match now (fixed: the inference constraint pass, the typeof-operand `as` parse, optional
callee effects, intersection constraints, regular literal arguments, the target conditional's substituted check type).
Left: react-redux/v5, v6 (like react-redux's TS2741, union order), ember/v3 (observable-tests TS2615/TS2578 like ember's),
d3-array/v2 (re-check), chai/v4 (TS2769 at chai-tests 1777, 1787: `deepInclude(new Map([...]), { a: 1 })` infers T =
string for `readonly T[] | ReadonlySet<T> | ReadonlyMap<any, T>`: Map is related to ReadonlySet structurally, whose
forEach/has give K = string, and the first candidate wins (getSupertypeOrUnion): candidate order follows union member
order, i.e. creation order. Same in latest chai at 1418, 1428).

### Small (1-10 lines each)
- **TS2303 on the import instead of the `export =` of a self-importing module** (koa-webpack, react-loadable;
  `types/webpack/index.d.ts`: `import webpack = require("webpack"); export = webpack;`): which alias of the cycle
  reports depends on which one tsc resolves first, and there it's incidental (checking koa's `DefaultState` resolves
  the type of a property koa-webpack's augmentation merged into it; a JSX intrinsic's symbolToEntityName walks every
  file's imports). tttt resolves aliases in program order at link, so the import comes first. Not worth chasing.

### Medium (15-100 lines)
- **Inference candidate order follows union member order, which follows type creation order** (react-redux:
  `MapStateToPropsParam = MapStateToPropsFactory<TS, TO> | MapStateToProps<TS, TO> | ...`; tsc creates the factory's
  alias instantiation first (its function type's members are lazy) so `connect(() => () => ({ bar: 1 }))` infers TS =
  `{ bar: number }` from the factory's return; tttt lowers the factory's return type eagerly, MapStateToProps gets the
  lower id and wins with `() => { bar: number }`: TS2741 at every `<Test foo="bar" />`). Swapping the union's
  declaration order flips tsc's answer too. Part of the union-order family (displaydiff `union order`).
- (Done 2026-10-06, plan 47's planning session: generic overloads now relate a context-sensitive literal in the skip
  round, async/steed/mithril/node match.) **chai** (`assert.deepInclude(new Map([["a", { a: 1 }]]), { a: 1 })`, TS2769,
  also chai/v4) was filed under that cause but has no context-sensitive argument: inference from a `Map` into
  `readonly T[] | ReadonlySet<T> | ReadonlyMap<any, T>` against the `T extends WeakSet<any> ? never : Partial<T>`
  overload. Uninvestigated.
- **Perf**: carbon-components-react, d3-geo (bugs/sweep_perf.md).
- **d3-tip**: a `declare module "d3"` augmentation isn't visible through d3's UMD global in tsc (its
  getTargetOfNamespaceExportDeclaration -> resolveExternalModuleSymbol returns the *unmerged* module symbol when there
  is no `export =`: `getMergedSymbol(undefined) || moduleSymbol`), so `d3.Tooltip` is TS2694 there; tttt merges
  augmentations into the module's own exports. Would need the UMD view to skip names only augmentations add.
- **dynmap**: tsc's TS2317 for jquery's 1-parameter `interface Iterable<T>` (es5 lib) comes from typeToString in an
  elaboration calling getGlobalIterableType: incidental, not worth chasing.
- **react-relay** (TS2322/TS2344/TS2339 on never): `EntryPoint<C, P>` isn't assignable to `EntryPoint<any, any>`.
  Self-contained part (2026-10-05, agent A): `type EP<C> = Rep<C extends Comp<infer N> ? N : never>` with
  `Comp<N extends Record<string, EP<any> | undefined>>`: `EP<any>`'s N is unknown in tttt, tsc gives the constraint
  `Record<string, EP<any> | undefined>` (nothing inferred from any -> the implied constraint). Cause: the HACK in
  ty_conditional_resolve skips a constraint flagged TYF_COULD_CONTAIN_TYPE_VARIABLES (an alias instantiation whose free
  parameters aren't known), though it's concrete. Narrowing the skip to `ty_is_generic_type()` fixes the repro but
  costs valibot +5% instructions (more constraints substituted), and react-relay still fails after it: then
  `ComponentClass<EntryPointProps<.., { onClick }, ..>>` vs `ComponentClass<EntryPointProps<.., {}, ..>>` fails on
  `getDerivedStateFromProps` (strict contravariance), which tsc must avoid through EntryPointComponent's alias
  variances (TTTT_DUMP_VARIANCES: #2/#3 reliable invariant in tttt; tsc's not dumped yet, use scripts/tscinstcount.js
  VARIANCES=1). Repro: /tmp-style dir with a tsconfig `paths` to DT's react/relay-runtime, importing
  react-relay/relay-hooks/EntryPointTypes (see this commit's message).
- **loadable__component** (TS18046 `props` unknown): `loadable(loader, { cacheKey: props => props.foo })` against
  an overload that requires `resolveComponent` first: the arrow is typed for good under the failing overload (tsc's
  first pass skips the context-sensitive member, the literal lacks resolveComponent, the overload is out). Repro:
  two overloads `lf<P, M>(fn: (p: P) => Promise<M>, o: { resolveComponent; cacheKey?(p: P) })` and
  `lf<P>(fn: (p: P) => Promise<{ default: P }>, o?: { cacheKey?(p: P) })`. Same family as the bullet above (plan 38).
- Not investigated: google-apps-script (property named `''`), fabric's second `Image`, kefir; d3-tip and dynmap as
  above. `scripts/sweep.py --last -k ...` lists each.

## `$ExpectType` ranking (stage 3 item 2, built 2026-10-06)
`scripts/expecttypes.py` (~30 s): DT's 69.2k `// $ExpectType` assertions in 2737 packages. tsc's side (`test/sweep_tsc.js
--expect`, 108 s, cached gzipped in `test/sweep/DefinitelyTyped.expect.jsonl.gz`; rerun `--tsc` after a pin change)
picks the node DT's `expect` rule checks (first node in forEachChild pre-order starting on the asserted line; an
expression statement unwrapped, a lone initialized variable statement's initializer) and records its span, kind and
`typeToString(getTypeAtLocation(node), undefined, NoTruncation)`: bare names (no enclosing declaration), unique symbols
as `typeof x`. tttt's side: `tttt --expect-types SPANS` prints the regular type of the expression with each span
(`src/expect_types.c`; a property access's name stands for the access). ~600 assertions on declarations (type aliases,
parameters, binding elements: `DECLARATION_KINDS`) aren't compared: they'd need getTypeAtLocation of declarations.
**Compared with tsc 5.8, not with the comment** (DT's CI runs TS 5.9+/7, the comments often list both orders with `||`).

First run 68312 / 68640 (99.5%); after this session's fixes **68416 (99.7%), 2637 / 2703 packages all match**. After the 2026-10-06 fan-out (agent B's part): 68483 (99.8%), 2640 packages; sweep 8584 / 8595 match, spurious 10. Fixed:
a merged interface's default from a later declaration (node's AsyncIterator), `G` through an import keeps alias
arguments `[]` for unions (ty_realias), an alias of an alias of a homomorphic mapped type (lodash's PartialObject), a
distribution that changes nothing is the union itself (underscore's `keyof X`), assertions through a property of an
intersection (assert.strict.equal), `${Enum}` property annotations made at the interface check (chrome, partly).

### Open after the 2026-10-06 fan-out (68610 / 68640 match); `scripts/expecttypes.py --last -k KIND` lists each, `scripts/expectprobe.py` reduces one
- **Union order, 12 assertions / 9 packages left** (`union order`): creation order. Was 135: the oracle asked
  getTypeAtLocation lazily without checking (test/sweep_tsc.js --expect now checks every non-lib file first, like
  tttt). Fixed 2026-10-06 (agent A): the check walk makes references, unions (memoized per node), tuples and
  function-type parameters' types like tsc's checkSourceElement; union/intersection/tuple alias bodies after their
  walk; discriminant lookups make the name literal when a member lacks the property; inference skips literal
  arguments of type-variable-free parameters; interface heritage/index constraints before the member walk; tuple
  length literals at target creation. Left, all relation side effects or identity:
  - tsc's typeRelatedToSomeType under reportErrors (any check with an error node) computes getBestMatchingType when
    no member relates directly, even if the relation then succeeds another way: findMostOverlappyType's getIndexType
    makes property-name literals (levelup), findBestTypeForInvokable's getSignaturesOfType makes global Symbol
    through getApparentType (node util_types, `keyof T` vs `string | number | symbol` in a mapped type check).
    Emulating needs tttt's relation to know it is a reporting one: plan-sized.
  - Structural comparisons tsc makes that tttt shortcuts (passport-discord: a 2-tuple in a lib signature's return type
    resolved in a heritage relation; postman-collection: UrlMatchPattern made through a base relation).
  - node test.ts `[a: number, b: number] | [a: number]`: tsc's tuple targets are keyed by the named member
    *declarations*, tttt's by name text, so tsc makes a new `[a: number]` where tttt reuses an older one.
  - json-merge-patch, node-red__util: not looked at.
- Done 2026-10-06 (agent B, 68416 -> 68483): chillout (rest arguments re-typed each inference round), node stream
  (the constraint pass resolved D before T: not plan 47 after all), yargs `Color` (distribution maps over the union's
  origin; now only a property-order mismatch), conventional-changelog-config-spec (namespace import further down an
  alias chain; now union order inside), d3-dispatch (contextual rest parameter `never` stays), secure-password /
  webidl-conversions (bare `typeof VALID`), es-abstract / conventional-commits-parser (fixed by then).
- **Alias/display singles left**: core-object `MethodNames<{...}>` and webpack-config-utils `DefinedObjKeys<...>`
  are the instantiate_inner() TY_INDEXED HACK (an indexed access over a union index keeps its alias except for unions
  of unit keys): dropping the exception still costs type-fest +2.4% instructions (measured 2026-10-06), a fix needs
  the aliased key union to share the mapped-type work with the plain one. redux-saga-test-engine: `@redux-saga/types`
  isn't installed, so Effect is unresolved and tsc reprints the annotation's type arguments, expanding the
  inaccessible local alias ThrowErrorKey (the mustache-express class, low value). `[value: number] | [value: number]`
  (node test.ts x4): tsc's tuple targets are keyed by the named member *declarations* (getTupleTargetType), tttt's
  TupleShape by label atoms: a design change for 4 assertions.
- **Inference singles** done 2026-10-06 (agent C, 68610 -> 68619): convict (getTypeFromInference subtype-reduces),
  frida-gum (an array literal's element context from a type parameter's union constraint, unreduced), function-bind
  (a target rest parameter past the source's parameters gets `[]`), raphael (the empty tuple's `this` members return
  `[]`), mustache-express (an unresolved reference's type arguments reprinted), es-abstract `[Symbol.toStringTag]` (a
  unique-symbol property name is looked up from its declaration, never bare). Left: redux-promise-listener
  (`AsyncFunction<unknown>`; tsc 5.8 says `AsyncFunction<AnyAction>` for a TReturn defaulting to
  `ResolveAction["payload"]` with ResolveAction uninferred: not understood), lodash 6087 (`Pick<Dictionary<..>,
  keyof Dictionary<..>>` vs `string | number`: didn't reproduce outside lodash), underscore NonIntersecting.
- react-relay (sweep): a conditional `infer X` that infers nothing is now its constraint without comparing (tsc's
  getInferredType: unknown is assignable to `{}` without strictNullChecks, so tttt kept unknown), but only for a
  constraint not flagged TYF_COULD_CONTAIN_TYPE_VARIABLES: react-relay's `Record<string, EP<any> | undefined>` is
  flagged (a deferred alias reference) and still gives unknown. Taking flagged constraints too (even only those
  mentions_type_variables() says are concrete) fixed react-relay but cost valibot +5% instructions: the valibot
  ValidPath HACK in ty_conditional_resolve. A fix needs the flag to be exact for deferred references or valibot's
  iteration understood.
- chai, chai/v4 (sweep): `d(mk([["a", { a: 1 }]]))` with `d<T>(h: ReadonlySet<T> | ReadonlyMap<any, T>)` and lib es6
  infers T = string, a declared `Map<string, {a}>` argument doesn't; reduced to `/tmp/c-probes/ch3.ts`-like files with
  lib es6 (a self-written S/M pair does NOT reproduce, so it needs the real es2015 collection/iterable lib types).
  Probably the inner call's return-type inference (K, V from `ReadonlySet<T>`) leaking into the outer T.
