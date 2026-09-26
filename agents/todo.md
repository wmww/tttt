# Todo
Written by the insights after plan 23 (2026-09-26); `vision.md` "Course correction (insights after plan 23)" says why.
**Ten sessions, then insights.** Delete entries as they complete. A plan that overruns its entry takes its extra
sessions out of this budget: when 10 sessions have run (count the commits since this file was written), the next
session is insights, whatever is left here. New work goes into plans or triage.md, not ahead of the insights entry.

3. **Execution: plan 19**, second session: its "After the R10-R14 session" section, TS7053/TS7052 first (a missing
   check: valibot's biggest bucket and 71 suite diagnostics), then R16. (Entries 1 and 2 are done, 2026-09-26: react
   80 -> 3, type-fest 168 -> 116, valibot 147 -> 98, suite 8219 -> 8234. That session made 9 commits; count it as one.)
4. **Planning: JS/JSDoc** (write plan 26). ~1050 tests (157 pass; 17% of all failing tests), most of the spurious
   TS2304/TS2580. The design question is the binder: CommonJS `module.exports`/`require`, expando assignments
   (`f.x = 1`, `this.x = 1` in constructors, prototype assignments) are declarations in tsc's binder, and JSDoc
   types (`@param`, `@type`, `@typedef`, `@template`, `@callback`) must lower into the existing `Ty` machinery rather
   than a parallel one (TS function expandos, triage.md, share the binder side). Stage it so the first stage is the binder side plus `@type`/`@param`, and measure on the
   `@checkJs` tests. triage.md's "JS files" item is the starting point.
5. **Planning: parser recovery** (write plan 27, or decide against it). 613 TS tests (4.5% of the suite) have a
   spurious TS1xxx, so they aren't typechecked at all and fail every component; ~550 of them are error-recovery
   fidelity (tsc also reports syntax errors there). Cluster them (`./failstats.py --syntax`, then look past the first
   error), and decide: can tsc's list recovery (parsingContext, isListElement/isListTerminator,
   abortParsingListOrMoveToNextToken, skipped tokens) be expressed in our backtracking no-tokenizer parser, or does
   the parser need a tsc-shaped scanner? Weigh in the parser's ~19% of corpus instructions and the unbounded
   recursion depth (triage.md "Parser"). A "keep patching" verdict is fine if the clusters say so; say how many tests
   each cluster is worth.
6. **Execution: plan 26** stage 1.
7. **Execution: plan 27** stage 1, or plan 26 stage 2 if plan 27 said not to restructure.
8. **Execution: suite checker bugs**, one session, plan-19 style but on the suite: sample `./displaydiff.py`'s
   `other` bucket (378 TS tests, mostly checker bugs the types baseline shows first) and failstats' single-code
   buckets (missing TS2322 74 tests, spurious TS2322 71, missing TS2339 56), reduce, fix the tsc rule. Not display
   polish: skip clusters that are only printing.
9. **Perf check-up**: `./bench.sh` with the React corpus; tsc corpus wall has drifted 0.67 -> ~0.8 s since the last
   perf session. Profile the corpus tsgo is closest on, fix what's cheap, update `measurements.md`.
10. **Triage.**
11. **Insights.**
