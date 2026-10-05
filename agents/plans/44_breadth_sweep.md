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
(bugs/sweep_perf.md).

## Stage 2: drain the ranking (corpus sessions)
Work the table top-down like plan 19 does for the corpora: a root cause that spans packages first, single-package
causes by how many packages they're the only mismatch of. Per root cause: a small repro under `test/probes` or a
filetest, the fix, `./sweep.py` before/after (0 newly mismatching packages), the usual suite/corpus gates. Track the
open causes in plan 19's style in this file's "Open root causes" below (or bugs/ for big ones).

## Stage 3: widen the oracle (one session each, in order of value)
1. **Versioned dependencies** (~370 packages): DT packages that depend on an older major (`@types/react@^16`,
   `webpack@^4` -> `types/webpack/v4`) get the latest today. Do what pnpm does: before a run, a small node script
   writes `types/X/node_modules/@types/Y -> ../../../Y/vN` for each dependency the latest doesn't satisfy (semver), and
   the versioned directories (`types/*/vN`, 537 of them) become packages of their own. Rerun `--tsc` for the affected.
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

### Small (1-10 lines each)
- **-TS2497/-TS2305 at a circular or unresolved `export =`** (~65 webpack-plugin packages; A/cycle, A/cycle2):
  `check_es_module_symbol` returns on a placeholder target, `resolve_alias_target`'s PORT_IMPORT_SPECIFIC returns on
  one. tsc's unknownSymbol is neither Module nor Variable (TS2497) and has no members (TS2305).
- **A namespace re-exporting an `import x = require()` alias is non-instantiated** (gulp-run, iarna__toml; C/2339/b, c):
  `statement_instance_state_of` (statement.c) must count PORT_IMPORT_GATHER with `is_require` as instantiated.
- **Augmentation applied to the file instead of the ambient module** (yeoman-assert 21; C/2339/h): `augmented_module()`
  (scope.c) should take `binder_module_of_ref()` (ambient module first, tsc's resolveExternalModule) before `ref->file`.
- **`unknown[K]` with generic K stays deferred** (dynamodb; C/2339/i): `try_resolve_indexed` (checker.c) returns the
  object for any but not for unknown in the generic branch (tsc's getIndexedAccessTypeOrUndefined: both).
- **Empty member name `""` stored as the text `""`** (gettext.js, cookieclicker; C/2769 D): parser.c's missing-name
  recovery fallback (`cstr_is_empty(&name_string)`, ~line 7410) also fires for an empty string literal: add
  `&& !name_is_value`. Also removes a spurious TS1360.
- **anyFunctionType lacks Function's members** (routie, browser-harness, benchmark; C/anyctx.ts): `ty_get_object_prop`
  falls back to GLOBAL_FUNCTION for GLOBAL_ANY_FUNCTION (tsc ~15223).
- **Union excess-property check ignores template/`string & {}` index keys** (dom-css, create-keyframe-animation;
  C/unionidx*.ts): `prop_or_index_type` (relate.c) should use `ty_applicable_index_info`.
- **A module nested in a `declare module` block is an augmentation** (i18next-sprintf-postprocessor; D/augm):
  statement.c treats it as a global ambient module (`augmentation = scope->kind == SCOPE_MODULE` only).
- **JSX factory from a UMD global gets no TS2686** (mdx 69; D/jsxumd): `jsx_reference_factory()` (expr.c) should call
  `check_umd_global_use()`.
- **Literal-keyof inference object is indexable** (rrdtool; D/literalKeyof.ts): `empty_object_from_string_literals()`
  (infer.c) builds an anonymous type that `has_inferable_index` accepts; tsc's has no symbol, so no inferable index.
  The wider "every anonymous type has an inferable index" rule is a hacks.md entry to file.
- **`declare enum` member types lack Number's methods** (dymo-label-framework; C/2339/l): not pinned down, enums.c.

### Medium (15-100 lines)
- **three +TS2415** (8 packages; B/three3.ts): an intersection base's merged `this`-typed member gets `this` rebound to
  the intersection (`find_prop_in` always `ty_with_this_arg`s; `builder_add_intersection_members`). tsc's
  createUnionOrIntersectionProperty keeps each part's `this`. ~20 lines.
- **Interface extending `any`** (koa family 5; B/koa8.ts, C/2339/k): `non_augmenting_base_uncached` (members.c) gates
  on type params and on object bases, so `interface DS extends AnyAlias {}` isn't normalised to any; and an any base
  gives no `[x: string]: any` (tsc's anyBaseTypeIndexInfo). Could replace the class-side `ty_has_unresolved_base` hack.
- **Augmentation-only references load the target file** (ember-data + 4; A/augonly): tsc never adds a file to the
  program for a module augmentation alone. Split resolution from loading in `program_load_module_ref` (program.c).
  20-40 lines.
- **Named import of a type-only member through `export =` used as a value: no TS2693** (node; D/typeAsValue).
- **Construct signatures compared bivariantly under strictFunctionTypes** (react, react-redux; D/ctorOverloads.ts):
  relate.c exempts every SIGNATURE_CONSTRUCT target; tsc only class `constructor` declarations. Expect suite movement.
- **jsonld +TS2411** (9 packages; B/jsonld2.ts): tsc publishes an interface's declared properties before resolving its
  index signatures, so a re-entrant `keyof C` sees the properties; tttt's in-flight members are empty. Publish partial
  Members before lowering index signatures. 50-100 lines.
- **Generic rest `[...AX, ...A]` implied arity** (call-bind; C/arity.ts): infer_to_tuple's "two variadic elements"
  TODO (tsc ~26690) plus impliedArity. ~60 lines.
- **Rest `TArgs` inferred from the callback's own parameters** (immediate, macrotask, node; D/restTArgs.ts): tsc drops
  a self-referential tuple candidate (isTupleOfSelf) and keeps `TArgs[0]` where tttt has never.
- **Annotated parameters of nested context-sensitive functions aren't inferred from** (create-subscription, treeverse;
  D/annotatedOrder.ts): `infer_from_annotated_params()` (call.c) only for a direct function argument; tsc does it in
  contextuallyCheckFunctionExpressionOrObjectLiteralMethod.
- **UMD global + `declare global` function merge** (namespace-js; B/nsjs): tsc merges the global function into the
  module symbol (mergeSymbol through the alias); `scope_add_port` PORT_EXPORT_AS_NAMESPACE doesn't.
- **Class + interface member conflicts give no TS2300** (selenium-webdriver 22; D/dupMerge*.ts); plus a spurious TS2687
  for an interface accessor merged with a property.

### Large / elsewhere
- **Generic overloads skip the whole context-sensitive literal** (async, steed; C/2769 B): hacks.md
  `first_unfit_skipped_literal`; needs side-effect-free speculative checks (plan 38).
- **Perf**: carbon-components-react, d3-geo (bugs/sweep_perf.md).
- Not investigated: mapbox, rangy, strophe (`declare global` namespace + UMD), prismjs (`"x/components/"` with a
  trailing slash resolving to the file), stampit, alpinejs, google-apps-script (property named `''`), d3-dispatch,
  ember (access-modifier, observable), function-bind, oidc-provider, d3-array, fabric's second `Image`, enzyme-async-helpers,
  kefir, chai, fs-extra (only "the last overload" reported). `./sweep.py --last -k ...` lists each.
