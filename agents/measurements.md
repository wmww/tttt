# Measurements
Recorded output of `./failstats.py` and `./bench.sh`. Replace (don't append forever) when re-measuring; keep the
date and commit. History is in git.

## 2026-09-27, plan 26 stage 3 (JSDoc declarations, todo entry 5)
Suite 9091 -> **9156** (errors 10900, symbols 12249, types 10593); 26 tests lost a component, all JS tests that passed
only because unresolved JSDoc names were silent (plan 26 "Stage 3 leftovers"). JS tests (`./jsstats.py`): **310 ->
375 pass**, 674 fail; cumulative bounds 1+2 134, +3 304, +4 315, +5 417, +3b 523. **webpack 3981 -> 515** (TS7006 115,
TS2322 113, TS2345 106, TS2578 26, TS18048 25, TS2339 25; wall 0.75 -> 0.81 s: more of it is typed now). Other corpora
unchanged (tsc 0, type-fest 11, valibot/react/nest 0). Bench not rerun (JS-only paths; TS corpora walls unchanged).

## 2026-09-27, plan 34 (error text fidelity, todo entry 3)
Suite 8958 -> **9090** (errors 10884, symbols 12218, types 10523), no lost components. failstats' "same
codes+positions, message text differs" 432 -> **302**; `./errtextdiff.py --coarse`: tests fixed by one part: chain 92,
header 77, underline 60, related 44, outside diagnostics 12. Corpora unchanged: tsc 0, type-fest 11, valibot/react/nest
0, webpack 3977 (bench not rerun: reporting-only changes, the relation fast paths are untouched).

## 2026-09-27, plan 30 stage 3
Suite 8953 -> **8958** (errors 10754, symbols 12184, types 10498), no losses. Corpora: tsc 0, valibot/react/nest 0,
type-fest 19 -> **11**, webpack 3981 -> **3977**. Callgrind (instructions) vs the session start: tsc corpus 5.794 G ->
5.830 G (+0.6%), valibot 5.291 G -> 5.376 G (+1.6%, diffuse: `infer X extends C` constraints resolve now, before C
was unresolved and ignored), type-fest 3.405 G -> 3.353 G (-1.5%: free-param collection memoises per true branch
instead of per chain, which that same fix had made thrash, +5%).

## 2026-09-27, plan 33 (fifth corpus: webpack, checkJs)
Suite 8952 -> **8953** (errors 10749, symbols 12176, types 10498). Corpora: tsc 0, type-fest 19, valibot/react/nest
0, **webpack 6146 -> 3981** (nearly all JSDoc: plan 26 stage 3). `./bench.sh` (best wall; the machine was loaded
this session: every tsc/tttt time is ~1.3-2x the previous entry's, compare ratios only):

| corpus | tttt | tsc 5.8 | tsgo | tsgo 1 thread |
|---|---|---|---|---|
| webpack (1188 files, 377k lines) | 1.07 s / 408 MB | 17.3 s / 739 MB | 1.22 s / 499 MB | 1.05 s / 307 MB |

tttt phases on webpack: parse 310 ms of files, resolve 144 ms, typecheck 498 ms, over an incomplete check (typedefs
are any, so much of the JSDoc typing isn't done yet); tsgo exits at 3 JSDoc TS1003 it parses differently, so its
number may not include checking either. This is the first corpus where single-threaded tsgo matches tttt: parsing
(JS + JSDoc comments) is a third of tttt's time; plan 26's stored tags must not make it worse.

## 2026-09-27, triage (todo entry 10)
Suite 8948 -> **8952** (errors 10748, symbols 12174, types 10497); corpora identical (tsc 0, type-fest 19,
valibot/react/nest 0). displaydiff: `other` 387+45 tests alone, `we-any` 121+43, `underlines/order only` 109+20,
`line-extra` 113+6, `tsc-any` 99+16 (TS+JS tests fixed by one kind).

## 2026-09-27, perf: plan 28 + valibot regression + check-up (todo entry 9)
Suite **8948** unchanged (no component moved); corpora identical (tsc 0, type-fest 19, valibot/react/nest 0).
Callgrind (`valgrind --tool=callgrind ./release/tttt -q --keep-going -p DIR`):

| corpus | before | plan 28 | + valibot fix |
|---|---|---|---|
| tsc | 6.171 G | 5.792 G | 5.792 G |
| valibot | 8.147 G | 7.295 G | **5.637 G** |
| type-fest / react / nest | | | 3.277 / 2.211 / 1.390 G |

- Plan 28 (calls resolve once): `ty_call_typecheck` inclusive 7.3% -> 1.1% on the tsc corpus. What's left is calls whose
  last resolution happened during an enclosing call's inference (not recorded, so resolved again).
- Valibot's +51% since plan 31 part 3 (abe4481; bisected in worktrees: 5.41 G -> 8.77 G, all in instantiate.c) was
  not the extra distribution: `instantiate_mapped_over_list` instantiated every mapped instance's modifiers type
  eagerly to see whether it became a union/list, ~263k times on mapped instances over mapped instances.
  `may_map_specially()` answers that without instantiating. The 4% left over 6ad4573 is the tsc-rule work since.

`./bench.sh --runs 2` (best wall / peak RSS; tsgo 7.0.2 on 4 cores; the machine is slower than on the last recording:
tsgo's tsc corpus 1.27 -> 1.54 s, tsc 5.8 7.9 -> 8.5 s):

| corpus | tttt | tsc | tsgo | tsgo 1 thread |
|---|---|---|---|---|
| synthetic (969 tests x 200) | 6.15 s / 4903 MB | 44.6 s | 12.2 s / 5399 MB | 13.8 s |
| tsc src/compiler | **0.90 s** / 423 MB | 8.49 s | 1.54 s / 425 MB | 1.90 s |
| type-fest | 0.43 s / 456 MB | 6.41 s | 2.35 s / 794 MB | 2.60 s |
| valibot | 0.81 s / 467 MB | 10.45 s | 2.27 s / 833 MB | 2.66 s |
| react | 0.34 s / 257 MB | 4.47 s | 1.32 s / 548 MB | 1.07 s |
| nest | 0.31 s / 261 MB | 3.06 s | 0.58 s / 255 MB | 0.59 s |

Phase split on the tsc corpus: parse 210 ms, resolve 122 ms, typecheck 565 ms. On nest: parse 173 + 22 ms libs,
resolve 73 ms, typecheck 35 ms (parsing decides applications with skipLibCheck; plan 27's backtracking).

## 2026-09-27, suite checker bugs 2 (todo entry 8)
Suite 8703 -> **8948 / 13540** (errors 10746, symbols 12174, types 10491), no lost components. Corpora unchanged:
tsc 0 (0.92 s / 423 MB, was 0.89 s / 413 MB), valibot 0 (0.94 s / 468 MB), type-fest 19 (0.41 s), react 0 (0.34 s),
nest 0 (0.31 s): within noise of entry 7 (wall times from `realworld.py`, best of 2, no callgrind).

## 2026-09-27, plan 19 corpus session (todo entry 7)
Suite 8686 -> **8703 / 13540** (errors 10463, symbols 12168, types 10455), no lost components. Corpora: tsc **0**
(0.89 s / 413 MB), valibot 49 -> **0** (0.91 s / 464 MB, was 0.95 s / 514 MB), react 1 -> **0** (0.33 s), nest **0**
(0.30 s), type-fest 47 -> **19** (0.39 s). Wall times from `realworld.py` (best of 2, no callgrind): no slowdown from
the wildcard/permissive conditional check (user time on the tsc corpus 1.24 -> 1.13 s, noise).

## 2026-09-27, plan 26 stage 2 (expandos, JS classes, JS optional parameters)
Suite 8631 -> **8686 / 13540** (errors 10445, symbols 12168, types 10441); lost: argumentsReferenceInMethod3/5_Js
types, jsEnumCrossFileExport errors (triage.md "From plan 26 stage 2"). JS tests (`./jsstats.py`): **260 -> 299
pass**, 750 fail; by category (only / involved): CommonJS 61 / 173, expandos & JS classes 54 / 217, basic JSDoc 98 /
351, other tags 13 / 123, leniency 2 / 23, syntax 41 / 157, emit 16 / 39, `.ts`-only 17 / 32, unclassified 91.
Corpora unchanged (tsc 0 at 0.89 s, type-fest 47, valibot 49, react 1, nest 0).

## 2026-09-27, plan 31 (fourth corpus: nestjs-boilerplate)
Suite 8622 -> **8631 / 13540** (errors 10417, symbols 12057, types 10384), no lost components. Corpora: tsc 0,
type-fest 47, valibot 49, react 1, **nest 134 -> 0**. `./bench.sh` (best wall; tttt / tsc 5.8 / tsgo 7.0.2 / tsgo
single-threaded; only these three rows were kept):

| corpus | tttt | tsc | tsgo | tsgo 1 thread |
|---|---|---|---|---|
| nest (2394 files) | **0.30 s** / 253 MB | 2.59 s / 402 MB | 0.40 s / 249 MB | 0.51 s / 213 MB |
| valibot | 0.99 s / 603 MB (0.95 s / 514 MB after the fix below) | 8.92 s | 1.63 s / 831 MB | 2.41 s |
| react | 0.36 s / 266 MB | 3.74 s | 1.04 s / 542 MB | 0.93 s |

nest is tsgo's best showing yet (parallel tsgo only 25% behind): its tttt time is parsing (172 ms of files +
22 ms libs, 65 ms resolve) and only 35 ms of checking, because skipLibCheck leaves just the app's own files to check.
Parse speed (plan 27's backtracking, ~20% of the tsc corpus) is what decides this kind of project.
**Valibot regression from plan 31 part 3 (abe4481)**: 0.80 s / 514 MB -> 1.02 s / 618 MB (callgrind 5.41 G -> 8.83 G).
`instantiate_mapped_over_list` composed the instance's whole mapper on every instantiation of a mapped instance
(`ty_compose_mappers` 44% inclusive); now it instantiates the homomorphic variable through both mappers and composes
only when distributing: 8.19 G, 0.95 s / 514 MB. The remaining +19% wall (+51% instructions) over 6ad4573 is instantiation work
(`instantiate_inner` self 0.50 G -> 1.55 G, `ty_instantiate_signature_in_context_of` 0.47 G -> 0.98 G inclusive),
not yet explained: the perf session (todo entry 9) should find out whether it's legitimate extra distribution.
The tsc corpus didn't move (0.87-0.88 s).

## 2026-09-27, plan 30 stage 2
Suite 8613 -> **8616 / 13540** (errors 10403, symbols 12055, types 10366), no lost components. Corpora unchanged: tsc 0,
type-fest 48, valibot 50 (0.81 s / 502 MB), react 1. Callgrind on the tsc corpus 5.974 G -> **5.986 G** (+0.2%; +0.8%
before `lower()` was inlined into its callers and `is_related_to` skipped the normalisation loops for non-deferred pairs).

## 2026-09-27, plan 30 stages Q + 1
Suite 8603 -> **8613 / 13540** (errors 10398, symbols 12054, types 10361), no lost components. Corpora: tsc 0,
type-fest 48, valibot 54 -> **50**, react 1. Callgrind on the tsc corpus 5.945 G -> 5.947 G (stage Q) -> **5.974 G**
(stage 1, +0.46%: `narrowable_type` on every access, `instantiate_mapped_info` for `expanded_from`).

## 2026-09-27, plan 29 stage 3
Suite 8599 -> **8603 / 13540** (errors 10393, symbols 12043, types 10349), no lost components. Corpora: tsc 0,
type-fest 46 -> **48** (misplaced TS2589s gone; array-slice/jsonify/writable-deep/int-range now visible, plan 19 R21),
valibot 54, react 1. Callgrind on the tsc corpus 5.920 G -> **5.945 G** (+0.42%; +0.9% before `compute()` stopped
computing its error span up front). valibot 0.84 s / 525 MB (544 before: the deferred-reference shortcut is gone).

## 2026-09-27, plan 29 stages 1+2
Suite 8579 -> **8599 / 13540** (errors 10389, symbols 12043, types 10345), two components lost (stage 3's
diagnostics). Corpora: tsc 0, type-fest 53 -> **46**, valibot 60 -> **54**, react 1. Callgrind on the tsc corpus
5.917 G -> 5.920 G (+0.05%). valibot 0.87 s / 544 MB (realworld.py, same as before; 598 MB before deferred
references were keyed by the node's outer type parameters).

## 2026-09-27, triage after plans 26/27 (todo entry 10)
Suite 8481 -> **8579 / 13540** full (errors 10274 -> 10388, symbols 12030 -> 12042, types 10273 -> 10319), no lost
components. All from small fixes found while re-probing triage.md and in failstats' single-code buckets: call error
spans (+15), string escape errors (+30), triple-slash reference spans (+11), untyped IIFE parameters optional (+15),
a function's own `arguments` (+11), TS2371 (+8), TS2309 (+7), TS2303 (+1). Corpora: tsc 0, type-fest 53, valibot 60, react 1.

## 2026-09-27, perf check-up (todo entry 9)
**The machine is ~15% slower than when the 09-25/09-26 entries were taken**: 49760b2 (recorded 0.76 s on the tsc
corpus) and 132f9ee (recorded 0.78 s) both run at 0.89-0.91 s today. Compare wall times only within one session;
callgrind instruction counts (tsc corpus, `valgrind --tool=callgrind ./release/tttt -q --keep-going -p
thirdparty/tsc/src/compiler`) are the stable measure: 49760b2 6.48 G, 132f9ee 6.56 G, session start (8ee3905)
**6.92 G**, session end **5.91 G** (-14.6%). The code drift since 09-25 was +6.7%, spread thinly (typecheck and
parse both +7%): the cost of the correctness work, no single regression.
What went: enum literal checks in `is_simple_related` behind one flag test; filtered unions skip `ty_union()`'s
reductions (`ty_union_filtered`); subtype reduction only tries structured members (tsc's removeSubtypes); key
properties of big unions (tsc's getKeyPropertyName: `===`/`switch`/predicate narrowing and union targets go straight
to the member, -3.9%); top-level reverse mapped types cached (tsc's createComputedCompilerOptions call alone was 5%);
containsType binary searches in relations (-2.7%); `T | undefined` of optional parameters cached (-1.1%). Suite 8480
-> 8481 (narrowingUnionWithBang), nothing lost, corpora unchanged.
`./bench.sh --runs 5` at the end (best wall; tttt / tsc 5.8 / tsgo 7.0.2 / tsgo single-threaded):

| corpus | tttt | tsc | tsgo | tsgo 1 thread |
|---|---|---|---|---|
| synthetic (962 tests x 200) | 5.90 s / 4582 MB | 39.6 s | 9.64 s / 5410 MB | 12.25 s |
| tsc `src/compiler` | **0.87 s** / 405 MB (0.93 at the start) | 7.68 s | 1.27 s / 434 MB | 1.77 s |
| type-fest | 0.48 s / 430 MB | 5.49 s | 1.93 s | 2.36 s |
| valibot | 0.81 s / 541 MB | 9.24 s | 1.64 s / 827 MB | 2.46 s |
| react | 0.30 s / 242 MB | 3.83 s | 1.06 s / 553 MB | 0.96 s / 317 MB |

tsc corpus phases: parse 204 ms, resolve 105 ms, typecheck 634 -> **561 ms**. Synthetic phases: parse 1.59 s,
resolve 0.79 s, typecheck 3.40 s (not comparable with older entries: the corpus grows with the passing tests).
What's left in the profile (5.91 G): parsing ~20% (speculative sub-parsers: `parse_fail` 7 M calls, keyword
probes), the typecheck walk re-resolving every call 7.7% (plan 28), `is_simple_related` 6.3% self (enum literal
pairs from `SyntaxKind` comparisons), `ty_union` 4.5% self, `infer_from` 5%.

## 2026-09-27, suite checker bugs session (todo entry 8, at the session's last code commit)
Suite 8408 -> **8480 / 13540** full (errors 10233 -> 10274, symbols 11977 -> 12032, types 10208 -> 10273); three
tests lost one component each (mappedTypeGenericIndexedAccess, typeVariableConstraintIntersections: their errors had
passed on an `any`, triage.md; asyncGeneratorGenericNonWrappedReturn's was won back). Single-code errors buckets:
spurious TS2322 72 -> 57 tests, missing TS2322 62 -> 55, missing TS2339 59 -> 53. Biggest movers: JSON modules (+28),
divergent accessors and write types (~12), noUncheckedIndexedAccess (~6), async/generator returns (~7). Corpora: tsc
0, type-fest 53, valibot 61 -> 60, react 1.

## 2026-09-27, plan 27 stage 1 (parser list recovery and commit points)
Suite 8345 -> **8408 / 13540** full (errors 10157 -> 10233, symbols 11930 -> 11977, types 10151 -> 10208), no
component lost. `./syntaxstats.py`: **687 -> 583** tests whose parser diagnostics differ (48 JS). TS tests: A 183 ->
175, B 111 -> 64, C 103 -> 58, D 67 -> 61, E 78 -> 81, M 97 -> 96. Clusters: recovery list-context 56 -> 25, object
literal 18 -> 6, TS1011 15 -> 0; other:B 23 -> 8, other:C 21 -> 8; lexical and permissive about unchanged (permissive
modifiers 68 -> 60, index signatures 26 -> 24). Corpora unchanged (tsc 0, type-fest 53, valibot 61, react 1, zero
syntax errors). Parse time (`--time`, 6 runs, vs the session's start): tsc corpus load+parse 199 -> 201 ms, react
115 -> 116 ms (noise; the first version of the lookahead cost +10% / +7%, see the part 10 commit).

## 2026-09-27, plan 26 stage 1 (JSDoc core + CommonJS, at the stage's last commit)
Suite 8258 -> **8345 / 13540** full (errors 10029 -> 10157, symbols 11851 -> 11930, types 10035 -> 10151); 5 types
components lost (typedef tests, hacks.md "JavaScript"). JS tests (`./jsstats.py`): **160 -> 247 pass**, 802 fail;
by category (only / involved): CommonJS 63 / 177, expandos & JS classes 72 / 254, basic JSDoc 98 / 365, other tags
13 / 128, leniency 5 / 43, syntax 42 / 158, emit 16 / 39, `.ts`-only 27 / 41, unclassified 90. Cumulative bounds:
CommonJS + expandos 153, + basic JSDoc 370, + leniency 396, + syntax 504, + other tags 632. Spurious TS2580 255 (139
tests) -> ~30 (4 tests, `Object.defineProperty(exports, ...)`). Corpora unchanged (tsc 0, type-fest 53, valibot 61,
react 1). tttt alone vs the session's start: tsc corpus 0.93 s both, RSS 408 -> 412 MB; react 0.30 s both, 238 ->
241 MB (new AST fields).

## 2026-09-26, parser baseline for plan 27 (`./syntaxstats.py`, at 2b74f47)
687 tests' parser diagnostics differ from tsc's (48 JS). TS tests by divergence: A (tsc's parser silent) 183, B (ours
first) 111, C (same position, other code) 103, D (tsc's first) 67, E (recovery diverges later) 78, M (only tsc) 97.
Clusters: lexical literals 92, identifiers 37; permissive modifiers 68, yield/await 28, index signatures 26, heritage
22, `with` 17, private names 17, decorators 9; recovery list-context 56, object literal 18, TS1011 15; other 234
(A 47, B 23, C 21, D 30, E 30, M 83). 224 of the 639 match tsc on every non-parse diagnostic.

## 2026-09-26, JS baseline for plan 26 (`./jsstats.py`, at d8ab92a)
1049 JS tests, **160 pass**, 889 fail. Failing tests by category (only / involved): CommonJS 87 / 211, expandos & JS
classes 72 / 255, basic JSDoc 113 / 421, other JSDoc tags 13 / 145, JS leniency 5 / 87, JS syntax diagnostics 42 / 162,
emit 16 / 39, `.ts`-only 27 / 38, unclassified 90. Cumulative upper bounds: CommonJS + expandos 181, + basic JSDoc
408, + leniency 464, + syntax 577, + other tags 722.

## 2026-09-26, plan 19 session 2 (TS7053, R16, type-fest reductions)
Suite 8234 -> **8258 / 13540** full (errors 10011 -> 10029, symbols 11849 -> 11851, types 10025 -> 10035), no
component lost. Corpora: tsc 0, **react 3 -> 1**, **type-fest 116 -> 53**, **valibot 98 -> 61**. `./bench.sh` not
rerun; `./realworld.py` walls: tsc corpus 0.95-1.4 s (noisy, same range before and after each change), valibot
0.84-1.2 s / 521 MB, react 0.32 s / 232 MB, type-fest 0.5 s / **415 MB** (was 96 MB: `IntRange<0, 999>` now
evaluates, tail-recursive tuples up to 999 elements, 276 MB of it; it used to be cut short silently).

## 2026-09-26, plan 19 session (R10-R14 and a re-sample)
Suite 8219 -> **8234 / 13540** full (errors 9992 -> 10011, symbols 11842 -> 11849, types 10006 -> 10025), no
component lost. Corpora: tsc 0, **react 80 -> 3**, **type-fest 168 -> 116**, **valibot 147 -> 98**. `./bench.sh` not
rerun; `./realworld.py` walls in the usual noise (tsc corpus 0.93-1.05 s, valibot 0.79-0.9 s / 484-529 MB, react
0.32 s, type-fest 0.37 s). The perf check-up (todo) should rerun bench.

## 2026-09-26, plan 25 (React corpus)
Suite 8202 -> **8219 / 13540** full (errors 9980 -> 9992, symbols 11832 -> 11842, types 9990 -> 10006); one errors
component lost (inferredRestTypeFixedOnce, triage.md). Corpora: tsc 0, type-fest 168, valibot 164 -> **147**, and
the new **react 362 -> 80** (bulletproof-react's Vite app: 128 own files, 1511 with dependencies and libs).
`./bench.sh --runs 3` (best wall; tttt / tsc 5.8 / tsgo 7.0.2 / tsgo single-threaded):

| corpus | tttt | tsc | tsgo | tsgo 1 thread |
|---|---|---|---|---|
| synthetic (200 copies) | see below | 37.7 s | 9.19 s | 11.96 s |
| tsc `src/compiler` | 0.89 s / 398 MB | 7.54 s | 1.27 s / 412 MB | 1.73 s |
| type-fest | 0.37 s / 98 MB | 5.44 s | 1.96 s | 2.33 s |
| valibot | 0.82 s / 530 MB | 9.08 s | 1.62 s / 822 MB | 2.43 s |
| react | 0.31 s / 232 MB | 3.80 s | 1.06 s / 545 MB | 0.94 s / 319 MB |

(The synthetic tttt line scrolled out of the captured output; phase split 1.49 s parse, 0.77 s resolve, 3.20 s
typecheck.) React phases: parse 113 ms, resolve 49 ms, typecheck 115 ms: it is dependency-parse heavy (skipLibCheck,
1511 files), which is where tsgo's threads help least (its single-threaded run is faster than its multi-threaded
one). tttt's react check is still incomplete (80 diagnostics, most from R10's missing inference), so the typecheck
share will grow. The tsc corpus is at 0.89 s here vs 0.78 s in the plan 21 entry: the drift the perf session in
todo.md is for (a noisier machine may account for part of it; tsgo also measured 1.27 vs 1.10).

## 2026-09-26, plan 23 (symbol merging)
Suite 8139 -> **8202 / 13540** full (errors 9901 -> 9980, symbols 11791 -> 11832, types 9980 -> 9990), nothing lost.
Tests failing on TS2300: 60 -> 51 (the rest: `@pretty` output, parser gaps), TS2528 13 -> 2, TS2323 10 -> 2, TS2813/TS2814
16 -> 0 (the remaining ones are JS). Corpora unchanged: tsc 0, type-fest 168, valibot 164; `./bench.sh` not rerun
(binder-level changes only, realworld wall times unchanged).

## 2026-09-26, plan 24 (binding pattern types)
Suite 8066 -> **8139 / 13540** full (errors 9884 -> 9901, symbols 11787 -> 11791, types 9883 -> 9980), nothing lost.
Corpora unchanged: tsc 0, type-fest 168, valibot 164 (the new destructuring TS2339 found two checker bugs in the tsc
corpus first, overload order and object literal union widening, both fixed). `./realworld.py` wall times unchanged
(0.81 s tsc corpus, 0.71 s valibot); `./bench.sh` not rerun.

## 2026-09-26, plan 21 stage 7 (sampling `other`; plan 21 closed)
Suite 7877 -> **8065 / 13540** full (errors 9857 -> 9884, symbols 11781 -> 11787, types 9652 -> 9882), nothing lost.
tsc corpus still 0 diagnostics. `./bench.sh`: tsc corpus 0.78 s (typecheck 282 ms in the phase run), valibot 0.69 s /
511 MB, type-fest 0.31 s, synthetic 4.97 s / 4294 MB vs tsgo 8.11 (exit 1, TS1540): unchanged. `./displaydiff.py`, TS
tests fixed by one kind: other 556 -> 378, we-any 147, tsc-any 87, line-extra 97, underlines/order 96, line-missing
81, union-order 48, our-typeof 35, tsc-typeof 21, names-only 17, undefined 11, structure-lost 11, tsc-import() 10.

## 2026-09-26, plan 21 stage 6 (symbol lines and declarations)
Suite 7655 -> **7877 / 13540** full (errors 9857, symbols 11218 -> 11781, types 9629 -> 9652), nothing lost. tsc
corpus still 0 diagnostics. `./bench.sh`: valibot 0.69 s / 511 MB, unchanged (the checker-side changes are
`import.meta` typing and 8 bytes per SymbolDecl). `./displaydiff.py --symbols`, TS tests fixed by one kind:
line-missing 110 (+90 JS), decls-differ 52, decls-missing 50, name 39, line-extra 24, other 21, underlines/order 20,
decls-extra 13, decl-order 7, decls-missing-lib 3.

## 2026-09-26, plan 22 stage 4 (JSX runtime import, factories, moduleDetection)
Suite 7567 -> **7655 / 13540** full (errors 9857, symbols 11218, types 9629), nothing lost. Biggest single piece: the
harness's unit contents now end like tsc's (+~40, mostly node16 multi-file tests). `./failstats.py --jsx`: **pass 313**,
errors 386, symbols 407, types 357 of 457. The stage 3 numbers below undercount types: failstats.py compared tsc's
Performance Stats block, which the runner drops (fixed now; with the fix, stage 3's end would have read ~280 / 350).
tsc corpus still 0 diagnostics; bench not rerun (nothing on the plain path changed).

## 2026-09-26, plan 22 stage 3 (JSX components) and what it turned up
Suite 7310 -> **7567 / 13540** full (errors 9718, symbols 11182, types 9601); 2 accepted losses (plan 22 Progress).
General fixes found through JSX carried most of it: bare names in messages (+48), `extends Base<T>` in the types
baseline (+94), tsc's Performance Stats block no longer compared (+45), binding-pattern parameter types (+16), TS2344
on type references (+8). `./failstats.py --jsx`: pass 188 -> 233, errors 311 -> 329, symbols 399 -> 405, types 253 ->
303 of 457. Corpora: tsc 0; type-fest 142 -> 162, valibot 161 -> 164: all of it TS2344 from the new type-reference
constraint check (type-fest's unused-`@ts-expect-error` TS2578 went 10 -> 6, i.e. some are right); the rest are
conditional/relation evaluation bugs the check exposes (triage.md). `./bench.sh`: tsc corpus 0.77 s (typecheck 503
ms), type-fest 0.31 s, valibot 0.69 s / 511 MB: unchanged. **The synthetic corpus is not comparable with earlier
entries**: it's built from fully passing tests and the newly passing ones are heavy (927 tests x 200, 2.48M lines):
tttt 4.73 s / 4065 MB, tsc 28.8 s, tsgo 7.97 s (exit 1: it rejects something in it), tsgo single-threaded 10.35 s.

## 2026-09-25, plan 22 stages 0+1 (JSX harness + parser)
Suite 7152 -> **7162 / 13542**, nothing lost. `./failstats.py --jsx`: 457 tests, errors 17 -> 243, symbols 51 -> 125,
types 37 -> 39 (all 20); JSX is `any` until stage 2. tsc corpus 0 diagnostics. `./bench.sh`: synthetic 3.02 s, tsc
corpus 0.76 s (typecheck 503 ms), type-fest 0.30 s, valibot 0.66 s / 506 MB: unchanged.

## 2026-09-25, same box upgraded to Ubuntu 26.04 (GCC 15.2, node 22.22 from Ubuntu, tsgo = `typescript@7.0.2`)
Suite **7152 / 13542**, unchanged, without the 64 MB stack-limit constructor. Corpora unchanged: tsc 0, type-fest 142,
valibot 161. `./bench.sh --runs 3` (tttt / tsgo / tsgo single-threaded), all within noise of the 24.04 numbers:
synthetic 2.96 s / 3.65 / 6.34 (tttt 3450 MB vs 2689); tsc corpus 0.76 s / 1.10 / 1.51 (typecheck 514 ms); type-fest
0.31 s / 1.69 / 2.02; valibot 0.66 s / 506 MB vs 1.44 / 825 MB.

## 2026-09-25, plan 21 stage 2 (symbol accessibility: `symbol_chain.c`)
Suite **6918 -> 7152 / 13540** full (errors 9300, symbols 10513 -> 10812, types 8969 -> 9125; nothing lost). Corpora:
tsc 0, type-fest 142, valibot 161 (unchanged; the binder fix of the session, ambient export contexts no longer
exporting imports, didn't move them). `./bench.sh`:
tsc corpus 0.79 s (typecheck 512 ms), type-fest 0.32 s, valibot 0.70 s / 505 MB: unchanged (the chain is only used
with an enclosing scope, i.e. by the baselines). `./displaydiff.py` TS tests fixed by one kind: other 541, we-any 125,
our-typeof 97, line-extra 90, underlines/order only 83, tsc-any 70, line-missing 69, union-order 50, tsc-typeof 28,
tsc-import() < 10 (was 51). Symbols: line-missing 334 (+98 JS), other 74, decls-missing 51, decls-differ 44,
underlines 26, name 19 (was 231).

## 2026-09-25, new dev box (AMD EPYC-Turin, 4 cores, 31 GB; Ubuntu 24.04, GCC 13.3, node 22, tsgo = `typescript@7.0.2`)
Not comparable with the entries below (different machine, and tsgo is now the 7.0.2 release rather than a preview).
Suite **6918 / 13542**, unchanged. tsc corpus 0 diagnostics; corpora: type-fest 142, valibot 161. `./bench.sh --runs 3`
(tttt / tsgo / tsgo single-threaded): synthetic 3.02 s / 3.69 / 6.22 (tttt 3413 MB vs 2593); tsc corpus 0.79 s /
1.00 / 1.46 (typecheck 516 ms); type-fest 0.33 s / 1.66 / 1.99; valibot 0.70 s / 505 MB vs 1.36 / 821 MB. **tsgo
7.0.2 exits 1** on the synthetic corpus (TS1540, `module X {}` namespaces are now an error) and on valibot (TS5108,
`moduleResolution=node10` removed), so it may do less work there than a clean run (triage.md "Perf").

## 2026-09-25, alias variances (tsc's getAliasVariances)
Suite 6914 -> **6918 / 13542** (+4: circularContextualReturnType, consistentAliasVsNonAliasRecordBehavior,
unionTypeErrorMessageTypeRefs01, mappedTypes3; nothing lost). tsc corpus still 0 diagnostics. `./bench.sh`: tsc corpus
2.57 s (typecheck 254 ms in the phase run), type-fest 0.29 s, valibot 0.58 s / 506 MB vs tsgo 0.86 s: unchanged, so
the shortcut is a correctness rule here more than a perf one. valibot gained 2 wrong TS2578s (triage.md "Perf").

## 2026-09-25, `.tsx` tests in the suite (harness change, no checker change)
The harness now collects `.tsx` test files: **13163 -> 13540 tests** (+377 configurations of 328 files). Suite 6912 ->
**6914 / 13540** full (errors 9298, symbols 10512, types 8967): 2 of the new ones pass. `./failstats.py --jsx` (the
`.tsx` files plus `.ts` ones with a `.tsx`/`.jsx` unit or `@jsx`): **457 tests, 17 pass** (errors 19, symbols 51,
types 37); 419 have a spurious TS1xxx (225 of them expect no errors at all). Spurious there: TS2304 3306 in 375 tests,
TS1005 2617 / 348, TS1109 2141 / 384, TS1128 1507 / 164, TS1110 664 / 206, TS2362 634 / 214, TS2749 500 / 147, TS6053
(`/.lib/` references) 106 / 84. Missing: TS2322 133 / 57, TS1005 56 / 14, TS1003 49 / 11, TS2769 36 / 12, TS2874 31 /
17, TS2741 27 / 18. Plan 22 is the JSX plan.

## 2026-09-25, plan 21 stages 4 and 5 (class expression names, T_1 renaming)
Suite **6810 -> 6912** full (errors 9296, symbols 10494, types 8817 -> 8960; lost duplicateTypeParameters3 types).
Corpora unchanged (tsc 0). `./bench.sh`: type-fest 0.29 s, valibot 0.58 s / 511 MB (unchanged; the new display state
is only allocated by the types baseline). `./displaydiff.py` TS tests fixed by one kind: other 594, we-any 123,
our-typeof 86, underlines/order only 75, line-extra 72, tsc-any 69, line-missing 65, tsc-import() 51, union-order 49,
tsc-typeof 28, T_1-rename 1 (was 71), class-expression 11 tests anywhere (was 88). Symbols: line-missing 311 (+97
JS), name 231, other 71, decls-missing 49.

## 2026-09-25, plan 21 stage 3 (annotation reuse by reprinting the node)
Suite **6505 -> 6810** full (errors 9289, symbols 10449, types 8416 -> 8817; nothing lost). Corpora unchanged (tsc 0,
type-fest 144, valibot 165). CLI cost unchanged: tsc corpus 0.65 s best of 8 before and after (`./bench.sh` said 0.70,
noise; typecheck 434 ms). `./displaydiff.py` TS tests fixed by one kind: other 573, underlines/order only 72 (was
182), we-any 122, our-typeof 83, T_1-rename 71, line-extra 71, tsc-any 68, line-missing 65, tsc-import() 51,
union-order 47, tsc-typeof+class-expression 45, tsc-typeof 40 (was 98), Array<>-reuse 0 (was 16). Types component:
4345 failing, 999 of them at >= 95% of lines.

## 2026-09-24, perf session after plan 15
Suite unchanged (6505), corpora unchanged (tsc 0, type-fest 144, valibot 165). `./bench.sh`: **tsc corpus 0.67 s**
(typecheck 480 -> 424 ms) vs tsgo 0.72 (single-threaded 1.23); type-fest 0.28 s vs 1.23; valibot 0.59 s / 511 MB vs
tsgo 0.78 s / 834 MB. Callgrind of the tsc corpus 7.35 G -> 6.00 G Ir: `narrowed_type` cached by (type, candidate,
flags) like tsc's getNarrowedType (-11%; `isIdentifier(node)`-style guards re-narrowed the same declared type
every time), `ty_union` sorts with a natural merge sort instead of qsort (its input is a few sorted runs back to
back; -5%), arena chunks straight from mmap (no calloc memset) and no calloc of `MemberTables`, the binary
operator parser as a switch (no measurable wall change for the last two). Profile of what's left: triage.md "Perf".

## 2026-09-24, plan 15 session 3 (missing checks, plan retired), end of session
Suite **6373 -> 6505** full (errors 9289, symbols 10449, types 8416). Corpora unchanged: tsc 0, type-fest 144,
valibot 165. `./bench.sh`: tsc corpus 0.75 s vs tsgo 0.72 (single-threaded 1.23), typecheck 501 ms; type-fest 0.30 s
vs 1.24; valibot 0.60 s / 511 MB vs tsgo 0.81 s / 834 MB. The session's checks (emit helpers, member override kinds,
the static side) cost nothing measurable. Of the +132: importHelpers/tslib 68 (17 of them a harness file-order fix
for tsconfig tests), member override kinds 27, regex flags 15, TS1042 10, TS2417 7, TS2506 5.

## 2026-09-24, plan 15 session 2 (missing checks), end of session
Suite **6016 -> 6373** full (errors 9140, symbols 10418, types 8396). Corpora: tsc 0, type-fest 144 (165: the
ts-expect-errors that wanted TS2314, fewer weak-type false positives), valibot 165 (240: 73 ts-expect-errors now
used, mostly TS2352). `./bench.sh`: tsc corpus 0.74 s vs tsgo 0.69 (single-threaded 1.21) — **tsgo is ahead there
now**; type-fest 0.29 s vs 1.22; valibot 0.59 s / 510 MB vs tsgo 0.77 s / 831 MB. The session's checks cost ~3.7% of
typecheck on the tsc corpus (min of 8 runs: 461 ms at 2376aa6, 478 ms now; callgrind +6.5% instructions, spread
over relations: comparable in both directions, TS2352's comparable checks, weak types, intersection targets'
combined properties). Worth a perf look (is_simple_related is 9.5% of the corpus' instructions).

## 2026-09-24, plan 21 execution session 1 (display fidelity), commit e98b6fb
Suite **5471 -> 6016** full (errors 8653, symbols 10403, types 8377). Corpora unchanged (tsc 0, type-fest 165,
valibot 240). No perf-relevant changes (traversal only, plus the binder's merged type parameters).

## 2026-09-24, plan 15 session 1 (missing checks), commit 228ccb7
Suite **5164 -> 5471** full (errors 8644, symbols 9502, types 7946). Corpora: tsc 0 (also with `--noUnusedLocals`; 2
with `--noUnusedParameters`, both real), type-fest 165, valibot 240. `./bench.sh` (a slower machine today: valibot
typecheck at the session's first commit measured 545 ms here, 505 ms after): synthetic 2.31 s / 2676 MB vs tsgo 1.83
(single-threaded 4.17); tsc corpus 0.84 s vs tsgo 0.87 (single-threaded 1.48); type-fest 0.35 s / 90 MB vs tsgo 1.36;
valibot 0.66 s / 489 MB vs tsgo 0.86 s / 826 MB (single-threaded 2.03).

## 2026-09-24, plan 19 R9 (implied constraints of `infer`)
Suite **5163 -> 5164** full (errors 8262, symbols 9492, types 7905; nothing lost). Corpora: tsc 0, **type-fest 173 ->
167**, **valibot 311 -> 245** (TS2578 119, TS2322 32, TS2344 30, TS2339 24, TS2345 15). `./bench.sh`: valibot tttt
0.58 s / 456 MB (typecheck 469 ms; more of it evaluates now) vs tsgo 0.85 s; type-fest 0.33 s / 88 MB vs tsgo 1.33.

## 2026-09-24, relations under assumptions (signature rules, TS2430/TS2320 on)
Suite **5125 -> 5163** full (errors 8262, symbols 9489, types 7905; 3 types components lost to union order). Corpora:
tsc 0, type-fest 173, **valibot 328 -> 311**. `./bench.sh`: **valibot tttt 0.57 s / 413 MB (typecheck 440 ms, was
836)** vs tsgo 0.82 s / 823 MB (single-threaded 1.79): tttt now beats multi-threaded tsgo on every real corpus. Almost
all of it is inferFromSignatures erasing the target signature's type parameters (bogus inferences into them were
cascading). type-fest 0.32 s / 88 MB vs tsgo 1.29; tsc corpus 0.74 s vs tsgo 0.76; synthetic corpus 1.92 s vs tsgo
1.70 (single-threaded 3.73).

## 2026-09-24, plan 19 R4 (const type parameters) and R1 (identity relation)
Suite **5120 -> 5125** full (errors 8218, symbols 9488, types 7896; nothing lost). Corpora: tsc 0, **type-fest 197 ->
173** (R4 -> 195, R1 -> 173), **valibot 739 -> 328** (R4 -> 620, R1 -> 328). `./bench.sh`: type-fest tttt **0.30 s** /
86 MB (was 0.53; typecheck 270 ms) vs tsgo 1.29 s (single-threaded 1.78), tsc 3.38. **valibot tttt 0.97 s / 496 MB**
(was 1.16 s / 615 MB; typecheck 836 ms) vs tsgo 0.84 s / 836 MB (single-threaded 1.75), tsc 6.24. Most of the time
saved is expect-type's `MismatchInfo`/`DeepBrand` fallback no longer being evaluated once the identity check holds.
valibot is still the one corpus where multi-threaded tsgo wins, now by 15%.

## 2026-09-24, plan 19 R5, R3, R2
Suite **5116 -> 5120** full (errors 8210, symbols 9479, types 7885; nothing lost). Corpora: tsc 0, **type-fest 450 ->
197** (R5 -> 295, R3 -> 198, R2 -> 197), **valibot 1871 -> 739** (R5 -> 1853, R3 -> 1628, R2 -> 739).
`realworld.py` wall: type-fest 0.73 s, valibot 1.17 s (was 1.16; R3 made more of it evaluate, +0.3 s of that was
an uncached getBaseSignature, now cached), tsc 0.73 s. `./bench.sh` not rerun.

## 2026-09-23, plan 13 done (+ `super` calls and legality, `in` checks, duplicate packages), commit 5d9ff4c
Suite **4931 -> 5116** full (errors 8206, symbols 9480, types 7879; lost components: taggedPrimitiveNarrowing and
contextualOverloadListFromArrayUnion, from TS2339 on `never` exposing wrong narrowing/contextual types, triage.md).
The biggest single gains: `super(...)` resolved and `void` (+91 with untyped rest parameters as `any[]`), `super`
legality/this-before-super/TS2377 (+38), private names (+63 with `in` checks), nominal privates (+22). Corpora: tsc 0,
type-fest 450 (was 424 before plan 16's per-property elaboration), valibot 1883 -> 1871. `./bench.sh`: type-fest
0.53 s / 74 MB (tsgo 1.23), valibot 1.16 s / 615 MB (tsgo 0.83 s / 833 MB, single-threaded 1.62), typecheck 1043 ms.

## 2026-09-23, plan 20: conditional instantiation cache
Suite **4690** (unchanged, no component gained or lost). Corpora unchanged: tsc 0, type-fest 424, valibot 1835 (a few
valibot messages print a union once where they printed `InferObjectOutput<X> | InferObjectOutput<X>`: two instances
that used to be distinct types are one now; one type-fest TS2589 became the TS2345 it hid, see triage.md).
`./bench.sh`: tsc corpus tttt **0.70 s** / 371 MB vs tsgo 0.75 (single-threaded 1.29), tsc 4.88. type-fest tttt
**0.59 s / 73 MB** (was 0.68 s / 200 MB) vs tsgo 1.30 s / 807 MB. **valibot tttt 1.19 s / 607 MB** (was 4.38 s /
1834 MB) vs tsgo **0.88 s** / 834 MB (single-threaded 1.72), tsc 6.27; typecheck 1072 ms. Callgrind of valibot
51 G -> 12.2 G Ir. What's left: `infer_from` 20% self (half of everything under `infer_from_signature_lists` ->
`infer_from_returns`), `instantiate_inner` 11%, `ty_is_deeply_nested` 5.5%, arena calloc/memset 5%, display of the
wrong diagnostics 5%. Repeated calls are free now (50 `v.pipe(...)` in one file cost what 1 does). type-fest's
remaining time is ~90% `test-d/opaque.ts` hitting TS2589 (a correctness bug, triage.md).

## 2026-09-23, plan 18: template literal types, string mappings, mapped `as` clauses
Suite **4690** full (4604 before; errors 7834, symbols 9264, types 7522; lost stringMappingReduction's errors, see
triage.md). Corpora: tsc 0, **type-fest 598 -> 424** (TS2345 225, TS2339 75, TS2322 48, TS2578 35, TS2344 14, TS2589 13;
nothing left in `source/`), **valibot 2169 -> 1835** (1965 before the literal widening fix).
`./bench.sh`: tsc corpus tttt **0.72 s** / 372 MB vs tsgo 0.76 s (single-threaded 1.42), tsc 4.87 (this machine runs
faster than the plan 17 entry). type-fest tttt **0.68 s** (typecheck 626 ms; 0.28 before, when most of it wasn't
evaluated) vs tsgo 1.36, tsc 3.37. valibot tttt **4.38 s** / 1834 MB vs tsgo 0.92 s / 824 MB (single-threaded 1.84),
tsc 6.74. Callgrind of type-fest (12.6 G Ir): the valibot shape, conditional instantiation without a cache
(`instantiate_inner` 31% self, `intern_get` 19%, `ty_intersection` 10%, `ty_realias` 4%; `conditional_from_root` ->
`ty_conditional_resolve` -> `ty_cond_branch_type` recursion): plan 20 covers both corpora now.

## 2026-09-22, plan 14: syntax gaps (`using`, import attributes, import/export forms, decorators, new.target)
Suite **4595** full (`using` +314, import attributes +4 and 45 tests with more components, import/export forms and
jump targets +36, decorators/`new.target` +20; no losses; errors 7803, symbols 9235, types 7418). Tests with a
spurious syntax error 1242 -> 746. Corpora unchanged (tsc 0, valibot 2169, type-fest 598).

## 2026-09-22, plan 19 stage 0: module augmentations merged in the link phase
Suite **4221** full (+2, +27 components, no losses). tsc corpus 0. valibot **4473 -> 2169** (TS2344 1166, TS2345 489,
TS2578 185, TS2322 141, TS1360 86; TS2349 1742 -> 4, TS2339 608 -> 24). type-fest 598 (unchanged).

## 2026-09-22, plan 17: two modern corpora (type-fest, valibot), first numbers
Suite: **4219 full of 13163** (+8: `@ts-expect-error`/`@ts-ignore`, bigint literal types and unary results). Lost
promiseTry (errors: now that an unused `@ts-expect-error` is TS2578, a missing TS2554 shows; triage.md). tsc corpus: 0.
`thirdparty/ensure-corpora.sh` pins type-fest v4.26.1 and valibot v1.0.0 (`library/`), both 0 under tsc 5.8.
- `./realworld.py --corpus type-fest`: **598** (TS2345 274, TS2304 87, TS2339 75, TS2322 39, TS2578 35, TS2589 26, TS2349
  26, TS2344 23; one parse error: a nested template literal type in `get.d.ts`).
- `./realworld.py --corpus valibot`: **4473** (TS2349 1742, TS2344 1166, TS2339 608, TS2345 489, TS2578 167, TS2322 141,
  TS1360 86, TS2769 28; parse errors: expect-type's nested template in a mapped `as` clause).
- Root causes: plan 18 (template literal types, string mappings, `as` clauses) and plan 19 (module augmentation of an
  aliased export: the ~2400 TS2349/TS2339 on vitest's `expect`, then the loop).

`./bench.sh` (same machine as the plan 08 entry: tsc 6.19 s on the tsc corpus): tsc corpus tttt **0.74 s** vs tsgo
1.02 (single-threaded 1.72); synthetic 1.67 vs 1.78. type-fest: tttt 0.28 s (incomplete check) vs tsgo 1.37, tsc 3.90.
**valibot: tttt 4.01 s, 1672 MB vs tsgo 1.29 s / 825 MB (single-threaded 2.79), tsc 6.84**: typecheck 5.4 s, the
first corpus where tttt loses to tsgo, even with the checks it skips. Callgrind (51 G Ir): 93% under `resolve_call` ->
`infer_from_signature_lists` -> `infer_from_returns` -> `ty_instantiate` of conditional types (`conditional_from_root`
83% incl., `compose_mappers` 73% incl., `instantiate_inner` 18% self, `infer_from` 19% self, `ty_is_deeply_nested`
4.4% self). tttt has no instantiation cache for conditional types (tsc's `root.instantiations`, keyed by the outer type
parameters' mapped values) and composes mappers on every step: plan 20.

## 2026-09-22, plan 08 done: real corpus 31 -> 0, the first honest perf comparison
Suite: **4211 full of 13163**, errors 7328, symbols 9180, types 7382; nothing lost over the session. Real corpus (`./realworld.py`): **0 diagnostics**, clean
in several check orders (a relation bug once showed up only when the files were checked in another order).
`./bench.sh` (machine ~1.65x faster than for the plan 12 entry: tsc 6.36 s on the real corpus, was 10.52):
real corpus tttt **0.88 s** vs tsgo **1.02 s** (tsgo single-threaded 1.72, tsc 6.36), load+parse 192 ms, resolve
102 ms, typecheck 567 ms, RSS 370 MB (tsgo 425). Synthetic corpus 1.58 s vs tsgo 1.87 (single-threaded 3.68), RSS
2121 MB (tsgo 1798). Against tsgo the ratios are what they were with 31-1751 diagnostics: finishing the checks that
used to fail early cost next to nothing. **Single-threaded tttt is ~2x faster than single-threaded tsgo on real code,
and ~15% faster than multi-threaded tsgo.**

Callgrind of the real corpus: 6.6 G Ir (6.2 G at plan 09 with 109 diagnostics), typecheck 77%, parse ~15%. Same
shape as the plan 09 profile below: `type_at_flow_node` 44% inclusive (narrowing), 4.3 M top-level `ty_is_related`
calls (22% inclusive, `check_related` 26% with `ty_is_comparable_either_way`), `is_simple_related` 9.4% self,
`ty_union` 15% inclusive (qsort 3.3%), `ty_call_return_type` 27% inclusive. The candidates listed under plan 09 still
stand.

## 2026-09-22, plan 12 done (fresh literals, excess property checks): real corpus 44 -> 31
Suite: **4200 full of 13163**, errors 7308, symbols 9173, types 7367. Lost 4: jsdocAccessEnumType (JSDoc `@type`, printed
right by accident before), destructuringWithLiteralInitializers, mappedTypeMultiInference,
typeParameterConstModifiersWithIntersection (spurious TS2353 over pre-existing wrong types, triage.md). Real corpus:
**31** (TS2345 15, TS2322 11, TS7022 4, TS2339 1), no TS2353. `./bench.sh` on a machine ~1.24x slower again than the
plan 11 entry (tsc 10.52 s on the real corpus): real corpus tttt **0.99 s** vs tsgo 1.20 (tsgo single-threaded 1.96),
typecheck 872 ms, RSS 370 MB; synthetic corpus tttt 2.35 s vs tsgo 2.40 (single-threaded 5.22), RSS 2175 MB. Scaled
by tsc, no cost from the fresh twins (one intern lookup per literal expression) or the lazy regular object twins.

## 2026-09-22, plan 11 done (higher-order generic inference): real corpus 91 -> 44
Suite: **4164 full of 13163**, errors 7285, symbols 9171, types 7317. Lost: badInferenceLowerPriorityThanGoodInference
(tsc's context-free type, triage.md). Real corpus: **44** (TS2345 27, TS2322 11, TS7022 4, TS2678 1, TS2339 1; TS2558
38 -> 0). `./bench.sh` ran on a machine ~1.7x slower than for the entries below (tsc 8.51 s on the real corpus, was
4.87), so compare ratios: real corpus tttt **0.90 s** vs tsgo 1.18 (tsgo single-threaded 2.00, tsc 8.51), typecheck
707 ms, RSS 366 MB; synthetic corpus (746 files) tttt 2.17 s vs tsgo 2.18 (single-threaded 4.74), RSS 2137 MB.
Against the plan 11 stage 0 build back to back on the same files: real corpus 0.91 vs 0.91 s, synthetic 2.15 vs
2.09-2.15 s (typecheck 828 vs 838 ms): no cost from inference in relations or fresh type parameters.

## 2026-09-22, plan 10 done (stages 2-3 + TS2526): real corpus 95 -> 91
Suite: **4129 full of 13163**, errors 7259, symbols 9171, types 7291. Real corpus: **91** (TS2558 38, TS2345 31,
TS2322 16, TS7022 4, TS2678 1, TS2339 1): skipLibCheck dropped the 4 TS2370 in @types/node. `./bench.sh`: real
corpus wall **0.65 s** (tsgo 0.77, tsgo single-threaded 1.33, tsc 4.87), typecheck 437 ms (424: class heritage checks
relate every class to its base), RSS 366 MB. Synthetic corpus 1.48 s (tsgo 1.66).

## 2026-09-22, plan 10 stage 1: `this` types: real corpus 109 -> 95
Suite: **4107 full of 13163** (was 3935), errors 7234, symbols 9168, types 7233 (+336). Lost: typedArraysCrossAssignability01
(a pre-existing overload relation bug `this` now reaches, triage.md). Real corpus: **95 type diagnostics** (TS2558 38,
TS2345 31, TS2322 16, TS2370 4, TS7022 4, TS2678 1, TS2339 1): the resolutionCache.ts/builder.ts `this` cascades are
gone. `./bench.sh`: real corpus wall **0.64 s** (tsgo 0.78, tsgo single-threaded 1.33, tsc 5.07), typecheck 424 ms
(414: the this-only sharing of thisless members keeps it small), RSS 360 MB. Synthetic corpus 1.43 s (tsgo 1.56; it
grew to 737 tests because more of them pass now, not comparable to the 1.34 s before).

## 2026-09-22, plan 09: iteration protocol, unique symbols, generators: real corpus 203 -> 109
Suite: **3935 full of 13163** (was 3714), errors 7227, symbols 9135, types 6896. Lost: generatorTypeCheck45/46 (a
pre-existing inference gap generators now reach, triage.md). Real corpus: **109 type diagnostics** (TS2558 38,
TS2345 32, TS2322 16, TS7022 13, TS2370 4, TS7006 4, TS2678 1, TS2339 1). `./bench.sh`: real corpus wall **0.64 s**
(tsgo 0.76, tsgo single-threaded 1.27, tsc 4.81), load+parse 149 ms, resolve 78 ms, typecheck 414 ms (399: iteration
now does the work that used to fail early, the fast path keeps it cheap), RSS 358 MB. Synthetic corpus 1.34 s (tsgo
1.55).

Callgrind of the real corpus (6.2 G Ir, typecheck 76%): iteration doesn't show up. The top is relations reached
from narrowing: `ty_flow_type` 43% inclusive; 4.3 M top-level `ty_is_related` calls (19.5% inclusive), 2.7 M of them
from `narrowed_type` (tsc's getNarrowedType relates every union member to the candidate), 0.8 M from
`assigned_is_assignable`; `is_simple_related` alone is 10.5% self (6.4 M calls, ~100 Ir each: worth a look, it runs
`ty_is_enum_union`/`compiler_options()` on every pair); `ty_union` 15% inclusive with qsort 3.3% (member sorting).
Candidates for a perf session: a per-(member, candidate) cache or tsc's `narrowedTypes` cache in narrowing, a cheaper
`is_simple_related` prefix, sorted-merge instead of qsort for unions built from sorted inputs.

## 2026-09-22, plan 07: types baseline underlines
Suite: **3496 full of 13163** (was 1971), errors 6907, symbols 8787, **types 6364** (3974). 0 tests lost a component.
`TTTT_IGNORE_UNDERLINES=1` now gives 3537 full / types 6437 (it was 3350 / 6334 before plan 07: other fixes since),
so 97% of the types gap the underlines caused is closed; the remaining 73 tests are a tail of 1-3 test shapes
(`hacks.md`, "Types baseline underlines"). Two rules did it: tsc's node reuse for signatures/properties (1716 ->
66 tests differing only in underlines), and no underline for `any` when tsc's baseline has no errors (+655).
Real corpus unchanged (1751 diagnostics, 0.73 s wall): the change is harness-only.

## 2026-09-21, triage after plan 06
Suite: **1971 full of 13163** (was 1849), errors 6907, symbols 8787, types 3974. What moved: JS gate + fileless
diagnostics + option validation (+54), harness typechecking next to parse errors (+8, -7 honest), property symbols
through unions/apparent types (+54, -6 honest: intersection reduction), string escapes (+54). No perf change.
**`TTTT_IGNORE_UNDERLINES=1`: 3350 full, types 6334**: the `^^^` lines of the types baseline hide 1445 passing tests
(`plans/07_underlines.md`).

Real corpus: **0 parse, 0 name, 1751 type diagnostics** (`typeof o[K]` was the last name error), 0.70 s wall in
`realworld.py` (bench not rerun: nothing perf-relevant changed). By code: TS7006 852, TS2345 317, TS2769 103, TS2322
92, TS2339 79, TS18048 46, TS2495 25.

History summary (full passes / corpus diagnostics): plan 03 (corpus first parses) 1576 / 9699; plan 04 (binder) 1684 /
7754; plan 05 (flow) 1756 / 1750; plan 06 (modules) 1849 / 1752; this triage 1971 / 1751. Per-stage detail is in git
(`git log -- agents/measurements.md`).

## 2026-09-20, plan 06: module resolution, tsconfig
Suite: **1849 full of 13163** (was 1756), errors 6814 (6561), symbols 8514 (8475), types 3897 (3816). ~300 tests gained
a component, 1 lost one honestly (`selfReferentialDefaultNoStackOverflow`, see `triage.md`). Spurious TS2307: 348 tests
-> 44 (what is left: `@libFiles`, outDir input mapping, Windows paths). TS1479/TS1471 (143 + 68 missing) -> 0,
TS2834/TS2835, TS5097, TS6142, TS6053, TS2792 mostly gone; still missing: TS2688 without a file, TS5xxx option checks.

Real corpus: now `tttt -p thirdparty/tsc/src/compiler`, ie the corpus' own tsconfig: 223 files with `@types/node` and
its dependencies found by resolution instead of by hand. **1752 diagnostics** (1750 before), the `source-map` name
error is gone. `./bench.sh`: wall **0.59 s (tsgo 0.72, tsc 4.72)**, load+parse 143 ms (120-144 before: no measurable
cost from resolution, which stats a few hundred paths and reads ~10 package.json files), resolve 77 ms, typecheck
373 ms, RSS 338 MB.

## 2026-09-19, plan 05 stage 4: TS2339 ungated, parameter properties, expression bases, the profile
Suite: **1756 full of 13163** (was 1742), errors 6561 (6536), symbols 8475, types 3816 (3814). 73 tests gained a
component, 48 lost their errors component honestly (spurious TS2339 / TS18046-18048 the gate used to hide, see
`triage.md`). Missing TS2339 937 -> 536, spurious 0 -> 434 in 119 tests (37 tests where it is the only difference).

Real corpus: 1626 -> **1750 diagnostics** (79 TS2339, ~55 "possibly undefined"/`unknown`: real bugs, `triage.md`).

`./bench.sh`: synthetic wall 0.36 s (tsgo 0.48), typecheck 86 ms, RSS 545 MB. Real corpus wall 0.98 -> **0.57 s (tsgo
0.79, tsc 4.71)**, parse 120 ms, resolve 74 ms, **typecheck 778 -> 352 ms**, RSS 317 MB. The profile (14.6 G -> 7.2 G
instructions) had nothing to do with the flow engine itself: N*N unions when widening enum literal unions, no visited
set in `infer_from`, literal pairs through the full relation machinery. What is left on top is in `triage.md`.

