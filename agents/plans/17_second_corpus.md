# Plan 17: a second real corpus (modern, type-heavy TypeScript)

## Why
`./realworld.py` (tsc's `src/compiler`) is at 0 diagnostics and is the only evidence behind "faster than tsgo". It is
old-style code: no template literal types, no mapped `as` clauses, few conditional types, which the type core
doesn't have (`hacks.md` "Type core: known gaps"). A second corpus in modern style tells us how far "the language
works" really is, and whether the perf result holds where relations/instantiation dominate. This session is
planning + setup: pick, pin, wire, measure. Don't start fixing the corpus in this session beyond trivial crashes.

## Pick
Criteria: clean under tsc 5.8 with its own tsconfig (verify with `thirdparty/tsc/built/local/tsc.js -p ... --noEmit`,
and tsgo); heavy on type-level code (conditional/mapped/template literal types, generic inference chains); no JSX
(absent feature, would drown the signal; note it for later if the best candidate has some); no project references
(or pick a single leaf project); 20k-150k lines incl. what it type-checks from `node_modules`; permissive licence.
Candidates to evaluate (network works, npm/git available): zod (v3 or v4 source), drizzle-orm, trpc, effect (one
package), valibot, type-fest's tests. A combination of a small very type-heavy library and a larger one is fine if
one alone doesn't cover both.

## Setup
- A fetch script next to `thirdparty/ensure-tsc.sh` (pinned commit/tag, `npm ci --ignore-scripts` for the `.d.ts`
  deps) and `thirdparty/refresh.sh` support; the checkout itself is not committed if tsc's isn't.
- `./realworld.py --corpus NAME` (default stays tsc) and a third table in `./bench.sh`. Keep invocations to
  `tttt -p <dir>` so tsconfig handling is exercised.

## Measure and deliver
- Diagnostics by code and by root cause (sample ~30, group them like plan 08 did), parse errors first (they are
  shapes plan 14 may already own), crashes/`log_error`s/runaways (`./findcrash.py`-style bisection if needed).
- `./bench.sh` + a callgrind profile if tttt completes: record in `measurements.md`.
- Write the follow-up plan(s): most likely **plan 18, type-core features** (template literal types incl. inference
  and `Uppercase<>` & co., mapped `as` clauses, substitution types, whatever else the ranking puts on top), and a
  "corpus 2 to zero" loop like plan 08 after it. Update `todo.md` and vision.md's State with the numbers.

## Validation
`./realworld.py --corpus NAME` runs end to end and reports a count; tsc/tsgo report 0 on the same invocation;
`./bench.sh` has the table; the follow-up plan exists with a ranked root-cause list.
