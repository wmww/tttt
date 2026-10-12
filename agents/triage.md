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
- Perf and memory vs `bun check` are now plans 59/60 (todo.md); the thread cap is in vision.md (needs a many-core
  machine). Bun's correctness numbers (conformance suite, 72-project config sweep, fuzzing, mutation of effect) are a
  bar to compare our oracles against.
- The newest-compiler build is in todo entry 10.
- npm 12 defaults to `allow-remote=none`: a corpus lock entry resolved outside registry.npmjs.org breaks
  `thirdparty/ensure-corpora.sh`. bulletproof-react's (inherited from upstream's yarn.lock, npmmirror) was rewritten;
  `--relock` may bring it back.

## From the insights of 2026-10-11
- `scripts/sourcesweep.py`'s timing lines are meaningless when other agents run (its 10-09 full run said "tttt 2801 s,
  tsc 2811 s"; idle, a 1/8 sample is 84 s vs 214 s): print the machine's load average next to them, or warn.

## From human
- Not sure how important emitting .d.ts files are. for now emitting js should def be out of scope. there are already plenty of fast emitters (eg esbuild), so for now anything covered by them doesn't need to be a focus. otherwise up to you what's important.
- I'll test on various configurations in the future. for now I don't see why we would cap at a particular number of threads. maybe tsgo has it's reasons but cargo culting from that seems silly. idk, you're the boss. just make it sensible and fast.
- btw if at any point you come across situations where typecheckers can diverge in behavior that are not covered by existing tests, please write new tests for them. For example if you find a false error in a large existing project you test against, or a way to introduce an error tsc catches that tttt misses that isn't covered anywhere in tsc's tests, add it somewhere checked into this repo so we don't regress. I was originally putting such cases in test/cases/ (I only have a few). you can move them around if you want, and you might need to change the test suite to actually check against them, idk
