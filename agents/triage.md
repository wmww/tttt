# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-09 (triage session, todo entry 10 of the thirteenth window): the TS 7.1 window's leftovers moved
to bugs/display.md, type_core.md, missing_checks.md, binder_modules.md, harness.md, perf_leads.md and the new
bugs/js.md (plan 26 retired into it); plans 53/54 retired, plan 58 (7.1.0 stable re-pin) written; three Opus
sub-agents worked the CLI/harness, parser and real-world checker items (the commit message has the outcome).

Finished 2026-10-11: the triage session above was cut off (usage limit) before merging its agents; the next session
merged their 7 commits and 3 uncommitted diffs (suite 13316, every oracle same or better) and filed what was left
into bugs/harness.md, sourcesweep_checker.md, js.md and display.md.

## From the bun check comparison (2026-10-11, measurements.md)
- **Perf vs `bun check`** (Bun 1.4.3, a Rust tsgo port): tttt's default caps at 4 threads; at 12 it beats bun
  everywhere but bun uses less memory on 12 of 14 corpora (up to 3.6x, vscode). Worth a perf/insights look: raise the thread cap (or scale with
  cores), and memory (AST share of the peak, the checkers' duplicated types, plan 36). Bun's correctness numbers
  (conformance suite, 72-project config sweep, fuzzing, mutation of effect) are a bar to compare our oracles against.
- `release/` failed under GCC 16.2 (a `-Wmaybe-uninitialized` false positive in `intersect_unions`, fixed with
  `calloc`); newer compilers may find more: a CI-ish build with the newest GCC/clang is cheap insurance.
- npm 12 defaults to `allow-remote=none`: a corpus lock entry resolved outside registry.npmjs.org breaks
  `thirdparty/ensure-corpora.sh`. bulletproof-react's (inherited from upstream's yarn.lock, npmmirror) was rewritten;
  `--relock` may bring it back.
