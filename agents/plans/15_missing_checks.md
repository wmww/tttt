# Plan 15: checks that don't exist yet (a loop)

## Why
The type core, binder, flow, modules and the big type features are in, and the corpus is nearly clean. But a large
part of the missing suite diagnostics is not wrong *answers*, it's checks tttt never runs. On 2026-09-22 these codes
had **no mention anywhere in `src/`** while tsc's baselines want them (missing count, tests):

| area | codes |
|---|---|
| unused (needs `noUnusedLocals`/`noUnusedParameters` options + reference tracking) | TS6133 314/126 (111 tests fail on it alone), TS6196 20/12, TS6192, TS6198, TS6199, TS6138 |
| declarations | TS2411 244/58 (property vs index signature), TS2369 100/31 (parameter property outside ctor), TS2391 79/48 (overload without implementation), TS2393 59/24, TS2323 58/19, TS2374 66/9, TS2428 64/11, TS2395 40/10, TS2449 109/29 + TS2448 87/41 (used before declaration), TS2729 70/31, TS2373 60/25, TS2699 75/4, TS2818 92/2 |
| expressions | TS2356 201/22 + TS2357 64/7 (arithmetic/increment operands), TS2703 121/22 (delete), TS2872/TS2873 124+106 (always truthy/falsy), TS2774 59/5, TS1345 38/6, TS2464 71/28 (computed name type), TS7053 71/23 (implicit any index), TS17006 169/5, TS17009 62/24 (`super` before `this`), TS2537 41/10 |
| grammar (checker-side in tsc) | TS1183 88/19, TS1036 38/28, TS1121 77/16 (octal), TS1210/TS1212/TS1213 ~160 (strict-mode reserved words), TS1359 69/28, TS1117 59/17 (duplicate object literal props), TS1107, TS1100 |
| regex literal checking (tsc 5.5) | TS1487 116/4, TS1508, TS1535, TS1530, TS1501 64/24, TS1517, TS1518, TS1507 |
| name suggestions | TS2552 101/54 ("did you mean", needs tsc's spelling suggestion algorithm, which TS2561/TS2820 want too: related info TS2728 is ready, see plan 16's `error_add_related_d`), TS2583/TS2580/TS2585/TS2550 (lib/`@types` flavours, `triage.md` "Target-gated") |
| JS-only | TS8002 132/12, TS8006 49/11, TS8010 34/14 ("TS-only syntax in a .js file": cheap, grammar-like; the rest of JS is its own plan) |
| emit helpers | TS2343 153/59 (`importHelpers` + a `tslib` without the helper) |

Plans 13 (TS2341/TS2445/TS2540/...) and 12 (TS2353 excess properties, its stage 4) own their codes; don't
duplicate. Numbers drift: re-derive the table first (below).

## Progress
- **Session 1 (2026-09-24), suite 5164 -> 5471.** Done: noUnusedLocals/noUnusedParameters (`unused.c`: TS6133,
  TS6196, TS6192, TS6198, TS6199, TS6205, TS6138, private members), TS2411/TS2413 index constraints (+ static index
  signatures), TS2448/2449/2450 used before declaration, TS2372/2373 parameter references (+ tsc's parameter scope),
  TS2729, TS2369, overload checks (TS2391, TS2389, TS2390, TS2393, TS2516, TS2387/2388), TS2464, TS2374, TS2428,
  TS2403 (+ identity/widening/intersection fixes), TS2300/TS2804/TS2717 member duplicates. Left in triage.md ("Found
  by plan 15 session 1") and hacks.md ("Missing checks").
- **Next (session 2), re-derived after session 1** (missing count / tests; "alone" = tests that would pass on it):
  TS2394 overload compatibility (22 alone, the flag agreement codes TS2383/2385/2386 with it), TS2356/TS2357
  arithmetic and increment operands (201/22, 64/7), TS2872/TS2873 always truthy/falsy (124/40, 106/22), TS2703
  `delete` operand (121/22), TS1183/TS1036 ambient grammar (88/19, 38/28), TS2323/TS2395 export merges (58/19,
  40/10), TS1117 duplicate object literal properties (59/17), TS2774 (59/5), TS17006 (169/5, one file), TS2552
  (spelling suggestions, 101/54: needs tsc's getSpellingSuggestion, a small algorithm; also feeds TS2561/TS2820),
  strict-mode reserved words TS1210/1212/1213/1359. TS2343 (59 alone) needs `importHelpers` + tslib semantics.
  22 tests also fail on a *spurious* TS2403 whose causes are type-core bugs (triage.md): worth a session of its own.
- **Session 2 (2026-09-24), suite 6016 -> 6267.** Done: TS2394 (+ mapped-to-mapped relation), TS2356/2357/2777,
  TS1345/TS2869-2873/TS5076 (+ `??` precedence, `<T>x` operand, arrow functions end an expression, for-in/of iterables
  now typechecked), TS2703/2704/2790/18011 (+ object rest types, `object_rest_type()`), TS1036/TS1183, TS2395, TS2323
  (not default exports), TS1117/1118/1119 (blocked by parser gaps in most tests), TS2552 + TS2662/2663 (spelling
  suggestions, `spelling_suggestion()` is reusable), the name-specific not-found messages (TS2580-TS2585, TS18004),
  TS2695, TS2314/2707/2315 (+ class property annotations now typechecked), TS2551/TS2576/TS2550 (`lib_features.c`,
  generated), TS18050 and the operand non-null checks, tsc's operator rules (TS2365 comparisons/bigint, TS2447, TS2791),
  TS17006, TS2352 (+ conditional sources deeply nested at 10: a hang), diagnostics ordered by code at one span.
- **Next (session 3), from `./failstats.py` after session 2:** TS2561/TS2820 (suggestions in excess property and
  string literal errors: reuse `spelling_suggestion()`), TS2774, TS17006 (one file), strict-mode reserved words TS1210/1212/1213/1359, TS2311
  (`await` as a callee, needs the parent), TS2323/TS2528/TS2813/TS2814 for default exports (make `default` one merged
  symbol like tsc), TS1117 on literal-typed computed names. The big single-code buckets left are not missing
  checks: spurious TS2580 (77 tests alone) is CommonJS in JS files (the JS plan), missing TS2322/TS2339 (68/66 alone)
  are type-core answers, TS2343 needs importHelpers/tslib.

## How (a loop like plan 08)
1. Re-derive: `./failstats.py`, then `./unimplemented_codes.sh` (missing codes `src/` never mentions, with their
   messages; a code that is mentioned can still be half-done, e.g. TS18050 is a TODO in `ty/check.c`). Also check
   `./failstats.py --no-run --code TSxxxx` for how many tests would pass on it alone (the "single code" table).
2. Pick by tests-that-pass-on-it-alone, then by count. Find the check in `thirdparty/tsc/src/compiler/checker.ts`
   (grep the message's key in `diagnosticMessages.json`, then `Diagnostics.<Key>` in checker.ts) and port the *rule*,
   including its exemptions (ambient contexts, JS files, `any`, error types): exemptions are where spurious errors
   come from.
3. Put it where tttt already walks that construct (declaration checks in `ty/check.c`/class heritage code, operator
   checks in `ty/expr.c`, grammar checks in the checker's statement walk, not the parser, like tsc: grammar errors
   from the checker don't suppress type errors). Unused-locals needs a "referenced" bit per symbol set by name
   resolution and a report pass per file at the end: design it once, it's the biggest single item.
4. `./fasttest.sh --summary` + `./resultdiff.py`: a new check that fires spuriously anywhere is worse than no check.
   `./realworld.py` must stay where it is (the corpus is clean under tsc: any new diagnostic there is a false
   positive). One commit per code or family, numbers in the message.

## Validation
- Each code taken leaves the "no mention in src" list; `./failstats.py` missing count for it drops by most of its
  total and its spurious count stays ~0; suite gains; corpus unchanged.
- Stop when the remaining codes are each < ~20 tests, or belong to a feature plan (JSX, JS/JSDoc, decorators).
