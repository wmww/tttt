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
(Entry 5, planning parser recovery, is done, 2026-09-26: `plans/27_parser_recovery.md`. Verdict: no rewrite, no
scanner; one list/commit-point framework (tsc's parsingContext rules) as stage 1, then permissive grammar and lexical
diagnostics as independent patching stages. `./syntaxstats.py` measures it: 639 TS tests, only ~1/3 are recovery.)
(Entry 6, plan 26 stage 1, is done, 2026-09-27: JS tests 160 -> 247, suite 8258 -> 8345; the plan's Status says
what's left and what stage 3 needs first.)
(Entry 7, plan 27 stage 1, is mostly done, 2026-09-27: parser-differing tests 687 -> 583, suite 8345 -> 8408; the
plan's Status lists what's left of stage 1. Stage 2, permissive grammar, is the next parser session whenever the
insights schedules one.)
(Entry 8, suite checker bugs, is done, 2026-09-27: suite 8408 -> 8480, nine commits; what it found and left is in
triage.md "From the suite checker bugs session". The `other` displaydiff bucket turned out to be one-offs; the
single-code errors buckets and missing features (JSON modules, noUncheckedIndexedAccess, write types, async returns)
moved more.)
(Entry 9, perf check-up, is done, 2026-09-27: tsc corpus 6.92 G -> 5.91 G instructions, 0.93 -> 0.87 s; most of the
"drift" was the machine. Plan 28 (resolve calls once, ~8%) written for the insights to schedule.)
(Entry 10, triage, is done, 2026-09-27: suite 8481 -> 8579 from eight small fixes found while re-probing triage.md
and in failstats' single-code buckets (measurements.md); triage.md re-probed and pruned; plans 29 and 30 written,
both with measured prototypes. triage.md "Single-code error buckets" is a ready-made list for a "suite checker bugs
2" session.)
11. **Insights.** Ready to schedule:
    - plan 29 (lazy mapped templates, deferred interface references, TS2589 at the right site; ~2-2.5 sessions;
      prototype: suite +20, type-fest 53 -> 46, valibot 60 -> 54; no new architecture needed);
    - plan 30 (tsc's indexed-access simplification, then substitution types; ~4 sessions; stage Q + stage 1 first,
      stage 1 prototype +8; stage 2 must coordinate with plan 29's lazy lowering);
    - plan 28 (resolve calls once, perf ~8%), plan 26 stage 2 (expandos, JS and TS), plan 27 stage 2 (permissive
      grammar), plan 19 corpus sessions (R15, R20, R21, re-samples), suite checker bugs 2.
