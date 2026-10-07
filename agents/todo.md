# Todo
Written by the insights of 2026-10-07 (after the tenth window); `vision.md` "Course correction" says why. **Ten
sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the numbers). A plan
that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run (count the commits
since this file was written), the next session is insights, whatever is left here. New work goes into plans or
triage.md, not ahead of the insights entry. Every entry ends with `./fasttest.sh -s`, the corpus numbers
(`./realworld.py --corpus X` for every pinned corpus), `./sweep.py` and `./flagflip.py` after checker changes, `./instrcount.sh` and a
commit.

**This window retargets tttt from TypeScript 5.8 to TypeScript commit 4d4f005c (6.0.3 + stable type ordering, what
tsgo 7.0.2 implements; plan 48).** Pass counts aren't comparable across the switch (13540 -> 14969 tests). Until entry 6 regenerates
them, the sweep / `$ExpectType` / flagflip / mutation caches are tsc 5.8's: skip those tools in entries 2-5, and corpus
numbers are only meaningful against the commit's tsc (`node thirdparty/tsc/built/local/tsc.js -p ...`).

1. Done (planning, 2026-10-07): plan 48 rewritten from four prototypes. Switch prototype 92.3% against 6.0.3,
   stable-ordering prototype 13220 -> 13790 against the commit; both saved in `agents/plans/48_prototypes/`.
2. **Execution: plan 48 stage 1**, the switch: pin `thirdparty/tsc` at the commit (built with hereby), libs, apply
   `48_prototypes/switch`, cheap buckets (messages, `import("./x")`), `48_lost.txt`, saved results regenerated.
3. **Execution: plan 48 stage 2 session A**, stable type ordering productionized (`48_prototypes/ordering`, a per-type
   sort key to get the +4-11% instructions back, member ordering).
4. **Execution: fan-out** (plan 48 stage 3: four sub-agents on `48_lost.txt` and failstats buckets, disjoint lists).
5. **Execution: plan 48 stage 2 session B**, delete the creation-order machinery piece by piece, then `48_lost.txt`.
6. **Execution: plan 48 stage 4**, the other oracles' caches on the new tsc (sweep ~40 min: commit first), drain cheap.
7. **Corpora: plan 48 stage 5**, re-pin every corpus at current upstream (clean under the commit's tsc and tsgo 7.0.2).
8. **Corpora: stage 5 drain** with sub-agents.
9. **Perf session** (plan 48 stage 6): instruction baselines on the re-pinned corpora, `./bench.sh` against tsgo 7.0.2.
10. **Triage.**
11. **Insights.**
