# Plan 47: side-effect-free speculative checks (stub by the insights of 2026-10-05, to be planned in todo entry 5)

## Why
Several open bugs share one missing mechanism: checking an expression under a *trial* contextual type without its
memos outliving the trial. tsc gets this from checkExpression not caching under a pushed contextual type
(checkExpressionCached only caches the unconstrained case, and inference contexts are discarded per overload); tttt's
per-node LazyType memos (and members' memos) are written on the first check, so a trial check leaves state behind.
The workarounds:
- `first_unfit_skipped_literal()` (hacks.md "Calls and inference"): tsc's SkipContextSensitive round relates a
  context-sensitive object literal / JSX attributes to the parameter; tttt skips the whole argument, and only checks
  the literal for non-generic overloads against a parameter without type variables. Sweep causes: async, steed, chai,
  mithril (plan 44 "Generic overloads skip the whole context-sensitive literal"), node/v22/v24's `stream.pipeline`
  generator overloads (plan 44 "From the versioned packages").
- Plan 38's leftover (line ~169): an argument's member memos under another contextual type.
This is the "almost works, demands a refactor" kind: each new overload case adds a narrower exception.

## For the planning session
1. Inventory what a check of an expression memoises (LazyType slots, member memos, literal freshness, inference
   sites, reported errors) and which of it depends on the contextual type.
2. Decide between: (a) a speculation scope that records memo writes and rolls them back (an undo log in the checker:
   slots, member memos, error contexts), (b) not memoising context-dependent results while a trial contextual type is
   pushed (tsc's way), (c) keep the hack. Prototype the cheapest that makes mithril's and steed's repros pass with
   `first_unfit_skipped_literal` deleted; measure instructions on effect/drizzle/vscode (memo misses cost).
3. Also decide the neighbouring approximation from vision.md: contextual instantiation is push-down where tsc pulls
   leaf-wise (bugs/type_core.md). Same mechanism or not?
The plan may say no. Validation: the hack deleted, the sweep causes above match, suite 0 lost, instructions within
~1%.
