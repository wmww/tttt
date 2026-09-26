# Measurements
Recorded output of `./failstats.py` and `./bench.sh`. Replace (don't append forever) when re-measuring; keep the
date and commit. History is in git.

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

