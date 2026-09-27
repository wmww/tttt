# Plan 33: a fifth corpus (a checkJs project), and optionally a sixth

Written 2026-09-27 by the insights session. One execution session (it picks, pins, runs and starts fixing), like
plan 31 was.

## Why
Four of five corpora are at 0 and have stopped finding bugs. The one front end no corpus exercises is **JS/JSDoc**
(plan 26): its stages 1-2 are driven only by the suite's JS tests (299 / 1049 pass), and stages 3-4 (`@typedef`,
`@template`, the other tags) are next. A real project that tsc checks with `allowJs` + `checkJs` and JSDoc types is
the oracle plan 26 lacks, and it ranks stage 3-4 work by what real code needs rather than by test counts.

## What to pick
Criteria: clean under tsc 5.8 (`tsc --noEmit -p <dir>` gives 0 with its own tsconfig/jsconfig and the lockfile's
dependencies; verify this first, drop candidates that aren't), mostly `.js` with JSDoc types, `checkJs: true`,
10k-200k lines. Candidates, in order of preference (check each; the session decides):
- **webpack** (`lib/**/*.js`, JSDoc-typed and checked by tsc in its CI via `tsconfig.json`; `types.d.ts` is
  generated). Big: if the full project is too slow to iterate, pin it anyway and note the time.
- **svelte** (`packages/svelte`, JS with JSDoc, checked by `tsc` in its repo).
- **preact** or **eslint** if the above are not tsc-clean at a pinned commit.
Pin it in `thirdparty/ensure-corpora.sh` with a lockfile in `corpora-locks/`, add it to `realworld.py --corpus` and
`bench.sh`, measure tsc/tsgo/tttt once (`measurements.md`).

Optional, if the session has budget left after the first run: an **inference-heavy application library**
(tRPC's server+client packages, or zod v4, or drizzle-orm), because valibot is the only one of that kind and it's
at 0. Only pin it if it's tsc-clean; just record its first-run count by root cause.

## The session
1. Pick and pin (above). First run: record the count, then rank the diagnostics by root cause (plan 19's rule: one
   root cause can be hundreds of diagnostics) into a new section of plan 19 (or of plan 26 when the cause is a
   missing JS feature: that's the expected bulk, and it's stage 3-4's input: rewrite plan 26's stage 3-4 order
   from it).
2. Fix the cheap causes the way plan 31 did (tsc rules, not corpus special cases), each commit with the suite
   unchanged or better.
3. If most of it is "stage 3/4 not built", don't start stage 3 here: write the ranking and stop.

## Validation
The corpus pinned and reproducible from a clean `thirdparty/corpora/` (`ensure-corpora.sh`), its count and ranking
in plan 19/26, `./fasttest.sh -s`, the other four corpora unchanged, `measurements.md` updated.
