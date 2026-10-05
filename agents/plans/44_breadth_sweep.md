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
  otherwise what `tsc -p` reports). `./sweep.py --tsc [-f regex]` regenerates `test/sweep/DefinitelyTyped.tsc.jsonl`
  (committed: per package, tsc's ms and its diagnostics as (file, line, col, code)). ~60 minutes on 4 cores for all
  packages; rerun only after changing the pin or the dependencies.
- **Decision: compare against tsc's diagnostics, don't drop the packages tsc rejects.** Under tsc 5.8 ~15-25% of the
  packages aren't clean (newer-TS lib names DT's CI accepts, `@types` deps on an older major or on removed packages,
  npm deps not installed). Their tsc diagnostics are the expected set, as in the suite: the comparison is by (file,
  line, code) multiset, like scenarios.py, so those packages still check everything else. A **missing check** under a
  `@ts-expect-error` (DT has 10.4k of them) shows up as a spurious TS2578.
- **Tool**: `./sweep.py` runs tttt (`--threads 1 --checkers 1`, `ulimit -v` 4 GB, 60 s timeout) on every package at
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
filetest, the fix, `./sweep.py` before/after (0 newly mismatching packages), the usual suite/corpus gates. Track the
open causes in plan 19's style in this file's "Open root causes" below (or bugs/ for big ones).

## Stage 3: widen the oracle (one session each, in order of value)
1. **Versioned dependencies**: done (test/sweep_links.py, run by sweep.py; 1241 links, 387 versioned packages of
   their own, oracle regenerated for the 584 affected with `./sweep.py --tsc -f` on `test/sweep_links.py --affected`).
   After a DT pin change: rerun the links (sweep.py does) and `--tsc` for everything.
2. **`$ExpectType`** (69.6k assertions in 2639 packages): a *types* oracle at breadth. dtslint compares
   `typeToString` of the expression on the assertion's line (no truncation, union order normalised) with the comment.
   tttt needs a mode printing the type of the expression on given lines (the baseline harness already prints every
   expression's type); compare with what tsc 5.8 prints (`test/sweep_tsc.js` can record it), not with the comment,
   since DT's CI uses a newer TS. Plan 35's machinery (displaydiff kinds) applies.
3. **More real npm dependencies** for the packages tsc rejects for TS2307/TS2688 (webpack 5, postcss, moment...):
   cheap, each widens coverage by tens of packages; regenerate the lock and rerun `--tsc` for the affected packages.
4. Beyond DT: the `.d.ts` of the top-N npm packages (types shipped in the package) with a generated test importing
   each, if DT runs dry.

## Perf side-product
`./sweep.py` prints tttt's summed single-threaded wall next to tsc's summed ms (tsc warm, libs parsed once: generous
to tsc). Many small programs stress startup and lib loading, which the big corpora don't: if tttt's per-package cost
is dominated by lib loading, a lib cache (parse the default libs once per process, or an mmapped pre-parsed lib) is
the lever, and `tttt -b` over many projects would share it.

## Validation
`./sweep.py` runs in about 5 minutes and is in CLAUDE.md's script list; measurements.md has the numbers; the top root
causes have repros in triage.md / bugs/ / the list below. Every later session that changes checker behaviour runs it
next to the corpora (0 packages newly mismatching).

## Open root causes
Diagnosed 2026-10-05 by four sub-agents (diagnosis only, no fix tried unless said). Repros in `test/probes/sweep/{A,B,C,D}/`
(single files: `FAST=1 ./filetest.sh FILE -d`; directories: `release/tttt -p DIR` vs `node thirdparty/tsc/lib/tsc.js
-p DIR`). Packages = sweep packages affected. Ordered by value for effort within each group; **do the small ones first**,
one commit each, `./sweep.py` before/after.

### From the versioned packages (2026-10-05)
Most mirror a latest package's open cause: node/v22, v24, v25 (node's TS2722/TS2345/TS2769/TS7006; v22/v24 add
`stream.pipeline(function*(){...}, async function*(source){...})` overloads where `source` stays unknown, TS18046,
then TS2339 'then' on never: generic overloads with context-sensitive generator arguments, the inference owner's), react-redux/v5, v6 (like react-redux's TS2741), ember/v3 (observable-tests TS2615/TS2578
like ember's), d3-array/v2 (d3-array's TS7006 was the skip-round anyFunctionType, fixed e207f80c: re-check), chai/v4 (TS2769 at chai-tests 1777, 1787: not investigated).

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
- **Rest `TArgs` inferred from the callback's own parameters** (immediate, macrotask, node; D/restTArgs.ts): tsc drops
  a self-referential tuple candidate (isTupleOfSelf) and keeps `TArgs[0]` where tttt has never. Tried (fan-out session): adding isTupleOfSelf to
  infer_from's candidate push is easy but doesn't help, because the callback's parameter isn't `TArgs[0]` in tttt
  (it's `never`, from the first pass's `[]` candidate): tsc types it through the nonFixingMapper when the contextual
  signature's rest type is a type parameter (contextuallyCheckFunctionExpressionOrObjectLiteralMethod) and ends up with
  `TArgs[0]`; how exactly (the `[]` candidate is there from the first pass) wasn't pinned down. Needs that first.
- **Generic overloads skip the whole context-sensitive literal** (async, steed, chai, mithril's `m(Comp2, { title,
  onremove(vnode) {} })` against `<A, S>(component, ...args: Children[])` first; C/2769 B): hacks.md
  `first_unfit_skipped_literal`; needs side-effect-free speculative checks (plan 38).
- **Perf**: carbon-components-react, d3-geo (bugs/sweep_perf.md).
- **d3-tip**: a `declare module "d3"` augmentation isn't visible through d3's UMD global in tsc (its
  getTargetOfNamespaceExportDeclaration -> resolveExternalModuleSymbol returns the *unmerged* module symbol when there
  is no `export =`: `getMergedSymbol(undefined) || moduleSymbol`), so `d3.Tooltip` is TS2694 there; tttt merges
  augmentations into the module's own exports. Would need the UMD view to skip names only augmentations add.
- **dynmap**: tsc's TS2317 for jquery's 1-parameter `interface Iterable<T>` (es5 lib) comes from typeToString in an
  elaboration calling getGlobalIterableType: incidental, not worth chasing.
- **react-relay** (TS2322/TS2344/TS2339 on never): `EntryPoint<C, P>` isn't assignable to `EntryPoint<any, any>`
  (repro: import EntryPointTypes, `const r: Record<string, EntryPoint<any, any> | undefined> = { a: e }` with
  `e: EntryPoint<EntryPointComponent<{}, {}, { onClick: () => void }, {}>, { author: string }>`). The chain ends in
  the alias `EntryPointComponent` (= `ComponentType<EntryPointProps<...>>`): TTTT_DUMP_VARIANCES gives its #2/#3
  (TRuntimeProps, TExtraProps) reliable invariant (0) while #0/#1 and ComponentClass's P are invariant+unreliable (8),
  so `{ onClick }` vs `{}` fails without a structural fallback. Suspect: the unreliable marker of ComponentClass's
  variance doesn't propagate into the alias's measurement for those parameters. Not confirmed against tsc.
- Not investigated: google-apps-script (property named `''`), fabric's second `Image`, kefir; d3-tip and dynmap as
  above. `./sweep.py --last -k ...` lists each.
