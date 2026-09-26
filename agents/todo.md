# Todo
Written by the insights after plan 23 (2026-09-26); `vision.md` "Course correction (insights after plan 23)" says why.
**Ten sessions, then insights.** Delete entries as they complete. A plan that overruns its entry takes its extra
sessions out of this budget: when 10 sessions have run (count the commits since this file was written), the next
session is insights, whatever is left here. New work goes into plans or triage.md, not ahead of the insights entry.

(Entry 3, plan 19's second session, is done, 2026-09-26: suite 8234 -> 8258, react 3 -> 1, type-fest 116 -> 53,
valibot 98 -> 61. Its 9 commits count as one session. Plan 19's newest section ranks what's left; R18/R19 want a plan
of their own, deferred alias/type-reference resolution: see triage.md.)
(Entry 4, planning JS/JSDoc, is done, 2026-09-26: `plans/26_js_jsdoc.md`, four stages; `./jsstats.py` measures it.
Stage 1 is JSDoc core + CommonJS; expandos and JS classes are stage 2, so "binder side" is split across entries 6/7.)
5. **Planning: parser recovery** (write plan 27, or decide against it). 613 TS tests (4.5% of the suite) have a
   spurious TS1xxx, so they aren't typechecked at all and fail every component; ~550 of them are error-recovery
   fidelity (tsc also reports syntax errors there). Cluster them (`./failstats.py --syntax`, then look past the first
   error), and decide: can tsc's list recovery (parsingContext, isListElement/isListTerminator,
   abortParsingListOrMoveToNextToken, skipped tokens) be expressed in our backtracking no-tokenizer parser, or does
   the parser need a tsc-shaped scanner? Weigh in the parser's ~19% of corpus instructions and the unbounded
   recursion depth (triage.md "Parser"). A "keep patching" verdict is fine if the clusters say so; say how many tests
   each cluster is worth.
6. **Execution: plan 26** stage 1.
7. **Execution: plan 27** stage 1, or plan 26 stage 2 (expandos, JS classes) if plan 27 said not to restructure.
8. **Execution: suite checker bugs**, one session, plan-19 style but on the suite: sample `./displaydiff.py`'s
   `other` bucket (378 TS tests, mostly checker bugs the types baseline shows first) and failstats' single-code
   buckets (missing TS2322 74 tests, spurious TS2322 71, missing TS2339 56), reduce, fix the tsc rule. Not display
   polish: skip clusters that are only printing.
9. **Perf check-up**: `./bench.sh` with the React corpus; tsc corpus wall has drifted 0.67 -> ~0.8 s since the last
   perf session. Profile the corpus tsgo is closest on, fix what's cheap, update `measurements.md`.
10. **Triage.**
11. **Insights.**
