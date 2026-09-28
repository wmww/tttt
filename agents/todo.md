# Todo
Written by the insights of 2026-09-28 (after the window of plans 32 B-36); `vision.md` "Course correction" says
why. **Ten sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the
numbers). A plan that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run
(count the commits since this file was written), the next session is insights, whatever is left here. New work goes
into plans or triage.md, not ahead of the insights entry. Every entry ends with `./fasttest.sh -s`, the corpus
numbers (`./realworld.py --corpus X` for every pinned corpus) and a commit.

1. ~~Plan 36 stage 2~~ done: `--checkers K`, per-checker ErrorCtx + tsc's sortAndDeduplicate (+2 tests),
   `./checkerdiff.py`; suite and corpora identical to K=1 modulo quoted text (plan 36 stage 2 section).
2. ~~Plan 36 stage 3~~ done: `--threads` (default cores up to 4) runs `--checkers` (now default 4) on threads;
   `./threadcheck.sh` identical, TSan clean on all corpora; tsc corpus 0.66 -> 0.48 s, webpack 0.56 -> 0.40, zod
   0.73 -> 0.45, valibot 0.62 -> 0.38 (2 cores x 2 SMT here); RSS +2..+47% (react worst, zod 846 MB).
3. **Plan 37 planning**: rank and derisk the tsc mechanism ports (missing type, nullWideningType, evolving arrays,
   decorators as calls, origins, context-free signatures...), write sub-plans for the top ones, fill in entries 4-5.
4. **Plan 37, first port** (named by entry 3).
5. **Plan 37, second port** (named by entry 3).
6. **Corpus session**: webpack 107 (plan 26 leftovers, plan 19's two TS18048s) and zod-root's arktype `never`
   (plan 19). Both are real-world JS/type-level signal the suite lacks.
7. **Suite checker bugs 5, spurious side**: the single-code buckets are now mostly spurious (TS2339 40, TS2322 39,
   TS2304 35, `bugs/missing_checks.md`); sample, group by cause, parallel Opus sub-agents in worktrees like session 4.
8. **Plan 35 queue** (silent type differences) minus what plan 37 took, or plan 34's error-text queue if plan 35's
   head is all plan 37 material.
9. **Perf: memory and types**: `perf_leads.md` "Unreliable variance measurements" first (zod without its `out`
   annotations costs 2x: is tttt marking more measurements unreliable than tsc?), then "Memory" (the `Prop` split, why
   tttt makes more types than tsc), with the K=4 RSS from entry 2 as the baseline (`measurements.md` Perf: zod 846 MB, react 335 vs 228 at K=1); then plan 36 stage 1 (parallel loading) if the session has room.
10. **Triage.**
11. **Insights.**
