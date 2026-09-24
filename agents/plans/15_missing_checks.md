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
