# Plan 00 - measurement tooling (execution)

We are flying blind. Build two tools, keep them cheap to run, document them in CLAUDE.md's script list.

## A. Failure aggregation
A mode of `baseline_tests` (or a script over its output; pick whichever is less code) that, over the whole
suite, reports:
- count of tests that panic / crash / time out, grouped by panic message + source location (top N);
- for the errors component: histogram of diagnostic codes (TSxxxx) that tttt failed to produce, and that tttt
  produced spuriously; plus "tests that would pass errors if code X were fixed alone" if cheap;
- number of tests whose errors component fails *only* because of differing message text vs differing
  code/position (tells us how much is type-printing);
- for types/symbols: fraction of lines matching per test, and a list of "nearly passing" tests (>=95% lines).
Output should be compact text an agent can read in one screen, plus optionally a full dump file (gitignored).

Validation: run it, paste the top-of-report into `agents/measurements.md` with the date and commit.

## B. Perf benchmark
- Pick a corpus tttt can at least *parse and not crash on*: start with the `thirdparty/lib/*.d.ts` set (checking
  the stdlib alone) and the largest passing tests concatenated; look for a real project later.
- Script `./bench.sh`: release build, runs tttt, `tsc --noEmit` and tsgo if available (`npx tsgo` /
  `@typescript/native-preview`; if there's no network, record that and skip) with wall time + peak RSS
  (`/usr/bin/time -v`), several runs, prints a table.
- Also report tttt phase split (load / parse / resolve names / typecheck) - add timing behind a flag in main.c if
  not present.
Validation: numbers recorded in `agents/measurements.md`. Note anything surprising in `triage.md` (e.g. parse
slower than expected), don't fix it in this session.
