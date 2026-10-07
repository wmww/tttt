# Plan 48: retarget from TypeScript 5.8 to TypeScript 6.0 with stable type ordering (what tsgo 7.0 implements)
Written by the planning session of 2026-10-07 (todo entry 1 of the eleventh window), from four prototypes/measurements
(sub-agents; both prototypes are applied now: the switch as 75cb7ee6, the ordering as a8ac7023).

## Decisions
1. **The oracle is TypeScript commit `4d4f005c8541e0255a9d8791205fdce326e462bc`** (2026-06-29, branch `tsgo-port`,
   version string `6.0.0-dev`): the commit tsgo 7.0.2's `_submodules/TypeScript` pins. It is **v6.0.3 plus
   `stableTypeOrdering` defaulting to true** plus three one-line fixes (TS1344's stray apostrophe, a scanner
   parenthesization, lib comments). `tests/cases` is byte-identical to v6.0.3's; the baselines are 6.0.3's except
   654 `.types` (650 member order only), 100 `.errors.txt` (~61 order inside quotes, ~8 a different member elaborated
   first, ~28 TS1344) and 17 `.symbols` (union property `Decl` order). So: 6.0's semantics (the last JavaScript tsc,
   runnable for every cached oracle), with the type order of 7.0, the language's future.
   - Not tsgo's own baselines: tsgo skips 1861 configurations (es5, amd/umd/system, node10/classic, baseUrl, outFile,
     esModuleInterop false...), drops the `^^^` lines, keeps source quote style, reworked JS/CommonJS/JSDoc (370
     `.types` differ), dropped the 10-suggestion limit (TS2304 -> TS2552 in 193 files). A poor exact-match oracle.
   - tsgo 7.0.2 (`tsc` on PATH) stays the perf competitor and a cross-check on the corpora.
2. **Adopt the stable type ordering** (stage 2). tsc 6's `compareTypes`/`compareSymbols` (checker.ts) and tsgo's
   `CompareTypes` (internal/checker/utilities.go) are the same rule, line for line; a probe gives identical output.
   It replaces tttt's creation-order mimicry (`sort_by_id`/`insert_by_id`/`sorted_contains` in `src/ty/checker.c`,
   `union_has_member` in `src/ty/relate.c`), which then becomes dead weight to delete: `argorder.py`, plan 35 item 3
   ("creation order"), the union-order hacks (hacks.md: "Union order depends on who asks first", creation order of
   intrinsics/globals, the TS2488 `once_key`), the union-order buckets in displaydiff/expecttypes/errtextdiff, and
   checkerdiff's "may differ inside quotes" (multi-checker output can become byte-identical). See "Stage 2" for the
   prototype's measurements.
3. **6.0's defaults everywhere**, the CLI included (they are tsc's defaults, not the harness's): `strict` true,
   `alwaysStrict` true and outside the strict family, target es2025 (es3 = unset + TS5108), module from target,
   moduleResolution bundler (node16/nodenext/classic by module), esModuleInterop/allowSyntheticDefaultImports true,
   `types: []` (`"*"` = every @types package), `noUncheckedSideEffectImports` true (TS2882), `libReplacement` false.
   Default lib for es2015 = `lib.es2015.full` (es2015 + dom + dom.iterable + scripthost).
4. **Deprecated options stay supported** (6.0 still runs them under `ignoreDeprecations: "6.0"`, and ~1950 suite
   baselines carry TS5107/TS5101 for them): es5, amd/umd/system/none, node10/classic, baseUrl, outFile,
   downlevelIteration, esModuleInterop false. Deleting them waits for a 7.x retarget. Nothing of 5.8-only behaviour
   is kept: no dual-version mode, no 5.8 suite side by side (the prototype showed the switch is cheap enough to do in
   one step).
5. **Pass counts are not comparable across the switch.** The suite grows 13540 -> 14969 tests (option variations:
   1132 names gone, 2561 new). Report both and define losses as below.

## Measurements (prototype of the switch against v6.0.3's baselines)
| step | full | errors | symbols | types |
|---|---|---|---|---|
| tttt as is, 5.8 libs and defaults | 9598 | 10636 | 14357 | 13032 |
| + 6.0 libs (`thirdparty/lib` from 6.0's `src/lib`) | 9664 | 10655 | 14495 | 13067 |
| + 6.0 option defaults | 11686 | 12286 | 14589 | 14024 |
| + 6.0 deprecations (TS5107/TS5101/TS5102/TS5108, "Visit https://aka.ms/ts6") | 13701 | 14442 | 14589 | 14024 |
| + harness splits at `\r?\n` only, `types: []`, uplevel iteration needs a global `Iterable` | 13791 | 14554 | 14650 | 14078 |
| + `noUncheckedSideEffectImports` default (TS2882) | **13817 / 14969 (92.3%)** | 14580 | 14650 | 14078 |

No crashes, hangs or log_errors. Of the 5.8 passes that fail at the last step: 501 had test file and baselines
changed, 123 only baselines, 12 only the file, 9 neither. 286 new test names fail.
Top buckets left (by tests): `import("./t1")` vs our `import("t1")` in printed types under bundler resolution (304
`.types`, 153 tests alone), other printing ~400 tests unclassified, reworded messages 118 tests (TS1323/1343/1378/
2821/2823/1355/2725 + 28 new codes: cheap), spurious TS2454 82 in 17 tests and TS2564 ± from strict-by-default,
TS7005 98 in one test, new checks TS2880 (6 tests), TS2883 (14), TS1540 (`module` keyword is an error, 2 tests but
all real code), TS2871, parser TS1005/1109/1434 (38 tests), spurious TS2304 (20 tests), TS2307 under bundler (11),
TS1286/TS1295 (verbatimModuleSyntax), TS2742 (14), TS2591 missing (8). The harness also stopped printing omitted
array elements (`[, x]`) in `.types` and dropped `@libFiles`/`@includeBuiltFile`.

**Corpora as pinned today**, tsc 6.0.3 vs the prototype (`-p`, counts and top codes):
| corpus | tsc 6.0.3 | prototype |
|---|---|---|
| type-fest, valibot | 1 (TS5107) | same |
| react, drizzle, vue | 1 (TS5101) | same |
| effect, excalidraw | 2 (TS5107+TS5101) | +TS2578 x3 / +TS2846 |
| zod | 0 | 0 |
| nest | 4 (TS2322, TS18046: strict-by-default finds real ones) | +TS6500 x2 |
| tanstack-router | 28 (TS2591 x27: `types: []`) | 27 (TS2307 x2 for TS2420) |
| xstate | 30 (TS2578 x15, TS2322 x14) | **0: missed** |
| webpack | 307 (TS2300, TS2451: @types/node twice?) | 10514 (TS6203/TS2300/TS2451) |
| v6.0.3's own src/compiler | 0 | 87 (TS2591 x43, TS2724, TS2307) |
Upstreams today: vue (~6.0.3), webpack (^6.0.3), xstate/vscode/tanstack-router (`@typescript/typescript6`), effect
(^7.0.2), typescript-eslint (catalog); zod (5.5), drizzle (5.6), type-fest/valibot/excalidraw (5.9) haven't moved.
DefinitelyTyped is pinned at 2026-10-02 (already TS6-era configs: `module: node16`, `types: []`): keep the pin.

## Stages
### Stage 1: the switch (1 session, todo entry 2)
**Done 2026-10-07: 13683 / 14969.** Left from it: TS2738 related info, `(Missing)` in TS7010 and the other strict
exposures in `48_lost.txt` (stage 3); an outFile bundle's import types still use the file path (tsc: non-relative
specifiers from the common source directory, see `program_module_display_name()`); corpora (below) not chased. The ordering
prototype patch still applies cleanly on top. tsc's own `src/compiler` and tsc-src are 0 under the commit (the
prototype's 87 were v6.0.3's sources). The v5.8.2 clone (for classifying losses) is at `/home/ai/tsc58`, outside the
repo (may be gone: `git clone --depth 1 --branch v5.8.2`).
1. `thirdparty/ensure-tsc.sh` pins the commit, not a tag: `git init`, `git fetch --depth 1 origin <sha>`, checkout;
   the version check is `git rev-parse HEAD`. Keep building tsc with hereby (needed by `tscbaseline.sh`, `filetest.sh`
   and every cached oracle: they must use `built/local/typescript.js` / `tsc.js` of *this* commit, not the repo's
   `lib/` LKG, which is a 6.0 release build without stable ordering by default). Then `thirdparty/refresh.sh`.
   `thirdparty/lib` = the commit's `src/lib` (refresh.sh's copy function, as the prototype did).
2. Apply the switch prototype patches (`git am`; written against 3ec44902, conflicts should be small) except the
   prototype's `TTTT_BUILD_TSC` opt-in in ensure-tsc.sh (it only existed to keep the prototype from building inside a
   shared clone). Regenerate `src/lib_features.c`. Review the `src/ty/iterate.c` change (uplevel iteration needs a
   global `Iterable`): check it against tsc's `getIterationTypesOfIterable` rather than trusting it.
3. Parse `stableTypeOrdering` (true is the default; `@stableTypeOrdering: false` is one test configuration, which may
   stay failing until stage 2 decides whether to keep creation order as an option: recommendation no).
4. The cheap buckets: the reworded/new messages (diff diagnosticMessages.json between v5.8.2 and the commit, update
   every changed text tttt emits), `import("./x")` module specifiers under bundler resolution, omitted array elements
   in the harness's `.types` printing.
5. **Saved results and "lost"**: `./fasttest.sh -s` regenerates `test/baseline_results.ts` under the new names. Before
   that, write the list of tests that passed under 5.8 (old saved results) and fail now, split into (a) test file and
   all three baselines byte-identical between v5.8.2 and the commit (a true regression: must be 0 or explained in the
   commit) and (b) re-baselined: `agents/plans/48_lost.txt`, the queue for stages 3-4 (delete it when drained). The
   other tools comparing against saved results (`resultdiff.py`) work from the next commit on.
6. Docs: CLAUDE.md's TypeScript version mentions, `documentation.md` (defaults, lib), `agents/vision.md`, and every
   script docstring that says "tsc 5.8" (`grep -rn '5\.8' *.py *.sh test/*.js`).
Expected: the prototype measured 13220 / 14969 against the commit's baselines (13817 against 6.0.3's: the difference
is member order, stage 2), plus whatever item 4 recovers.
Corpora numbers are recorded but not chased here (their oracle moves in stage 5).

### Stage 2: stable type ordering (2 sessions, todo entries 3 and 5)
**Prototype** (a8ac7023, on top of the switch prototype): `src/ty/order.c` `ty_compare()`, a port of
`compareTypes`/`compareSymbols`, replacing id order in `sort_by_id`, `insert_by_id`, `sorted_contains`,
`sorted_list_contains`, `union_has_member`, the constrained-type-variable re-insert and the optional-marker scan;
object literal and spread properties sorted by declaration (`builder_sort_stable` in `members.c`); `[]` (a bare tuple
target in tttt) counts as a reference. Against the commit's baselines: **13220 -> 13790 / 14969**, 3 lost. Of the 576
order-only tests that passed against 6.0.3, 568 pass again. tsc's key order: TS 6's TypeFlags rank (any < unknown <
undefined < null < void < string < number < bigint < boolean < symbol < literals < unique symbol < enum < `object` <
never < type parameter < object type < keyof < template < string mapping < substitution < indexed access <
conditional < union < intersection), then the name (alias, type parameter, class/interface/reference; named before
unnamed; same alias -> alias arguments), then per kind (objects: symbol = first declaration's file order then
position, references first, tuples by readonly/arity/flags/labels, type arguments, deferred node + mapper; literals
by value, false < true; type parameters by declaration; composite types by their parts; conditionals by root node
then mapper), type id last. Intersections stay unsorted; `typeof` keys, `getNamedMembers` (own members before
inherited for classes/interfaces) and spelling-suggestion candidates are sorted too.

**Session A done 2026-10-07: 13683 -> 14269 / 14969** (0 lost, `stableTypeOrdering(stabletypeordering=false)` passes
too: the key is the type id there and the member sorts are off). What it did: `Ty.order_key`, a cached u64 prefix of
compareTypes (rank, named bit, 57 bits of name / literal value / declaration position) with an inline fast path
(`ty_compare()` in ty_internal.h) and `ty_compare_tie()` for equal keys; `union_with_origin`'s named-union membership
is a merge walk; `builder_sort_stable()` (merge sort on precomputed positions) on object literals, mapped types,
namespaces and merged values; the static side's `prototype` last; the evolving-array losses fixed for real
(`KEY_AUTO_FLOW_ISSUE`: tsc reports TS7005 from the in-loop incomplete pass). Instructions vs the stage 1 end
(callgrind): tsc +0.9%, react +2.1%, valibot +1.9%, drizzle +1.6%, excalidraw +1.6%, tanstack-router +1.3%, zod
+1.2%, others <= 0.7%, vue -2.0%; the left cost is `sort_by_id`/`ty_compare_tie` (big string literal unions with
long common prefixes) and mapped types over big interfaces (keys come in value order, tsc's sort makes them
declaration order: iterating the modifiers type's properties instead would make the sort a check). Checker arena
+1% (8 bytes per type). Corpora: valibot +4 TS2344 that the commit's tsc reports too (with `--ignoreDeprecations 6.0`;
without it tsc skips semantic diagnostics); vue +1 TS2345 with `--checkers 1` only (variance measurement order,
bugs/type_core.md); multi-checker output still differs inside quotes on webpack (tsc's per-program
undefinedProperties cache, see checkerdiff.py: the allowance stays). Left for session B: the id tie-breaks hacks.md
lists under "Stable type ordering" (noImplicitAnyUnionNormalizedObjectLiteral1, instantiationExpressions), and
`getNamedMembers` for class/interface members (tttt's own-then-inherited order matched tsc on every probe).

**Session A (entry 3), productionize** (the original brief): apply the patch; **a sort key computed once per type** (rank, then a value /
name prefix / declaration position: most comparisons one integer compare). The prototype costs +11.5% instructions on
the tsc corpus (`ty_compare` 9.5% inclusive), +6.1% drizzle, +4.5% type-fest, +1-3.5% elsewhere: the session must
end within ~1% of the switch's instruction counts. `stableTypeOrdering: false` (one test configuration; keep the
old id order behind it only if it's free, otherwise leave that test failing and say so). The two lost evolving-array
tests (`selfReferencingSpreadInLoop`, `yieldExpressionInControlFlow`: `ty_array()` relied on the auto array having
the lowest id among arrays). `getNamedMembers` ordering for every member kind (class static side's `prototype` after
own members: the prototype's attempt broke a declared `static prototype`, 7 tests; mapped type properties,
namespaces). Validation: union-order buckets in displaydiff/expecttypes gone, `./threadcheck.sh` and
`./checkerdiff.py` identical (and remove checkerdiff's "inside quotes / union order" allowance if output is
byte-identical now).
**Session B done 2026-10-07** (deletion part): the check walk no longer makes types early (references, type literals,
tuples, unions, alias bodies, function-type parameters, type parameters, class value types, field annotations without
initializer, literal property annotations), no early parameter typing in `signature_resolve` (getMinArgumentCount
order), no tuple length unions, no `""`/`0` or Array/autoArray at checker start, no boolean id swap, no Omit-key
literals; `argorder.py` deleted. Suite unchanged (14711: only `stableTypeOrdering(stabletypeordering=false)` lost,
accepted: creation order is no longer reproduced), corpora byte-identical, instructions -0.1..-0.7% (none up). Kept,
measured: `0n` at checker start (tsc's compareTypes has no bigint value order: ids decide, `0n | 1n`), the
`KEY_UNION_NODE` memo plus the fresh twin of a written literal (removing both cost tsc +0.85%, more
`union_with_origin` work; each alone nothing: unexplained, a HACK note in lower.c), the type-literal index-constraint
gate (pure cost filter). Union-order bucket in displaydiff: 4 tests, all explained (ty_compare id ties, JS member
order). Left: the id tie-breaks of hacks.md "Stable type ordering", class/interface getNamedMembers order.
Fan-out part (4 agents, ~80 picks): 14711 -> **14804 / 14969**, 0 lost; `48_lost.txt` 22 -> 13. Ordering ties
ported where tttt has the node (conditional roots by node position, deferred references by node, fresh after regular,
unpositioned unique symbols); hacks.md lists the ties still left to ids. Mechanisms: tsc's symbolDepth printer
elision, the predicate/alias/property re-entry rules behind TS2589 vs TS2502, TS2563/TS2590/TS2859 placement,
`require()` of ESM (`"module.exports"` export), parse-error deduplication order, tsc 6's getNamedMembers for type
literals with late-bound names, a harness use-after-free (class field declarations). Leftovers: triage.md "fan-out
agent A/B/C/D" sections.
The original brief, for reference: **delete the creation-order machinery**, one piece at a time, measuring each removal (suite,
corpora, instruction counts): `argorder.py`; plan 35 item 3 and its leftovers; the walk-order code that exists to
create types in tsc's order (check-walk and node memoization in `type.c`, `lower.c`'s `KEY_UNION_NODE` and property
literal order, `several_types.c`, `generic.c`, `function_impl.c`, `class_decl.c`, `interface_decl.c`,
`callable_type.c`, `checker.c`'s eager Array/autoArray/`typeof` setup and `swap_twin_ids`, `members.c` ~3914,
`call.c` ~1284, `flow.c` ~2080); hacks.md's `interface_member_typecheck()` literal lowering, early
`ty_check_index_constraints`, "Union order depends on who asks first", the TS2488 once-key caveat. Ids still break a
few ties (fresh vs regular literals, intrinsic variants, conditional roots, deferred references); subtype reduction
and inference iterate in member order, so removals can move results: measure, don't assume. Some of this code may
also be doing laziness/perf work: keep what still earns its place. The rest of the session is fan-out-style work on
`48_lost.txt`.

### Stage 3: fan-out on what the switch broke (1 session, todo entry 4; leftovers go to session B and after insights)
**Done 2026-10-07: 14269 -> 14712 / 14969 (98.3%, the target), 0 lost.** Four agents (A semantic errors, B parser /
new 6.0 checks / text, C types harness and import printing, D other types + symbols), ~95 picks, two duplicates
(discriminateTypeByDiscriminableItems, const-context returns). The two big mechanisms were harness-side (6.0's
typeWriter skips omitted elements; no `/.src/` underline widening: 133 tests); the rest were real 6.0 semantics
(import defer, TS2880/TS2883/TS1540/TS7027-in-checker, `??` precedence, common source directory = config dir, the
6.0 inference tweaks, discriminant rules, bundler conditions by emit format, namespace-import clones) and
strict-by-default exposure (TS2454/TS2564/TS7022 corners). **xstate (30) and valibot (31) now match the commit's tsc
exactly** (file, line, code; valibot's 31st is the TS5107 tsc drops under `--ignoreDeprecations 6.0`): the corpora
are pinned for 5.x, so a rising count can be a fix: compare with tsc before reverting. `48_lost.txt`'s "true" section
is empty; ~25 rebaselined tests left, all in triage.md. Leftovers (triage.md, "Plan 48 stage 3 agent ..."): mostly
pre-5.8 failures, parser recovery (plan 27), elaboration order, TS2589/TS2590/TS2563 limits, enum member canonical
names, T_1 renames from mapped-type key cloning, libReplacement in the harness.
Four Opus sub-agents per session in worktrees, disjoint test lists cut from `48_lost.txt` and `./failstats.py`
buckets, 0 true losses enforced at merge (vision.md's fan-out recipe). Candidate buckets from the prototype: other
printing differences; strict-by-default exposure (TS2454, TS2564, TS7006, TS2322: these are latent checker bugs the
5.8 suite's non-strict default hid, so expect real fixes, not 6.0 specifics); new 6.0 checks (TS2880, TS2883,
TS1540, TS2871, TS1286/TS1295, TS2882 corners, the unreachable-code/unused-label move from binder to checker:
TS7027/TS7028); bundler/`types: []` resolution (TS2307, TS2591); parser diffs. The 6.0 inference tweaks (functions
not using `this` aren't context sensitive, yield counts for context sensitivity, return-type filtering by constraint,
`createOuterReturnMapper`, `getSingleCommonSupertype`, const-context return literals, void -> undefined unions,
mixin accessor flags, `shouldDeferIndexType`; each 1-5 new tests) are one agent's bucket. `import defer` (14 tests,
TS18058-18061) and `--module node20` semantics (79 tests, mostly in module lists) are one agent's bucket if they're
not done by then. Target: >= 98% of 14969 (~14670) by the end of stage 2's session B, or note what's left in the plan.

### Stage 4: the other oracles on the new tsc (done 2026-10-07, todo entry 6)
Sweep oracle regenerated (31 min), `$ExpectType` oracle regenerated (6 min), mutation cache rebuilt by `mutate.py`
itself, `tscbaseline.sh`/`filetest.sh` work against the new harness. Sweep 8585 -> **8594 / 8595**, `$ExpectType`
68574 -> **68620 / 68640**, scenarios 200 / 16 / 90 skipped -> **240 / 0 / 66** (TS6 deprecates outFile/AMD, so those
skips went), mutation seed 1 (2000) 4 and seed 2 (8000) 34 mismatches (0.8% of the mutants that change tsc's codes;
that seed was drained to 6 under 5.8: a drain for later). Numbers in measurements.md, leftovers in triage.md.
**Flagflip moved to stage 5**: its cache is keyed by corpus pin and every corpus re-pins there; the current pins aren't
clean under tsc 6 either. When regenerating: the `es5` cell's `--target es5` is deprecated in 6.0 (TS5107 gates the
semantic diagnostics) and `sloppy`'s `--alwaysStrict false` too: give those cells `--ignoreDeprecations 6.0` (or drop
`es5`), and check every cell's tsc output for TS5xxx before trusting it.

### Stage 5: corpora re-pinned (2 sessions, todo entries 7 and 8)
**Done 2026-10-07 (entry 7)**: all corpora at upstream heads (type-fest v5.10.0, valibot v1.5.0, webpack, zod,
xstate, vue, vscode, excalidraw, tanstack-router, typescript-eslint) or unchanged where upstream hasn't moved (react,
nest, drizzle, effect's v3 branch; Effect 4 is TypeScript 7 only), each 0 under the commit's tsc through upstream's
config or a minimal overlay (`"baseUrl": null` and other deprecated options unset, valibot minus 7 tests 6.0 changed,
vscode minus 3 tests tsc 6 and tsgo 7 disagree on). tsgo 7.0.2 isn't clean on webpack (JS), vue, drizzle, effect:
recorded in ensure-corpora.sh. Flagflip regenerated. The drain (entry 8) has triage.md's "Corpora re-pin" sections.
After re-pinning, regenerate flagflip (`./flagflip.py --tsc --force`, see stage 4 for the deprecated flags). Every corpus at a current upstream commit, checked by the commit's tsc and by tsgo 7.0.2 (both must be clean: effect
uses 7.0, most others 6.0 now). Upstreams still on 5.x (zod, drizzle, type-fest, valibot, excalidraw) re-pin at
their current commit with an overlay adding `"ignoreDeprecations": "6.0"` where tsc 6 reports only deprecations
(their CI hasn't moved; record that in ensure-corpora.sh). Drop the 5.9 lib shims (grep corpora-locks and the
overlays for them). `tsc`/`tsc-src` follow the commit automatically (its `src/`: the prototype gave 87 spurious on
src/compiler, mostly TS2591 = `types` handling). Drain with sub-agents in the second session; note the xstate gap
above (tsc 6 finds 30 that tttt doesn't: strict-by-default `@ts-expect-error`/TS2322) as a missing-diagnostic lead.

### Stage 6: perf (1 session, todo entry 9)
New instruction-count baselines on the re-pinned corpora (`./instrcount.sh`), `./bench.sh` against tsgo 7.0.2 on
every corpus (now that no config is rejected for baseUrl), the stable-ordering comparator's cost in the profile,
anything strict-by-default made slower (strict checks run on corpora that didn't set strict before).

## Not in this window
A second suite fan-out (after insights), `import defer` / node20 if stage 3 didn't take them, tsgo-only behaviour
(7.0 removes the deprecated options: a later retarget, when 7.x has its own JS-free test oracle).

## Validation
- Suite >= 98% of the new total with 0 true losses (stage 1's definition) by the end of entry 5.
- Stable ordering: union-order buckets in displaydiff/expecttypes empty or explained; `--checkers 4` output
  byte-identical to `--checkers 1` on every corpus (checkerdiff's "inside quotes" allowance removed).
- Every corpus re-pinned and at 0 against the commit's tsc (and tsgo 7.0.2 clean on the same configs).
- Sweep and `$ExpectType` at or above the last 5.8 match rates against the new caches; flagflip and mutation
  re-measured.
- `./bench.sh` against tsgo 7.0.2 on every corpus.
