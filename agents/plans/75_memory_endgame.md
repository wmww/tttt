# Plan 75: the memory endgame (planning, with a prototype gate)
Written by the insights of 2026-10-16. A planning session: measure, prototype, decide, and either write the compact
AST plan proper (into this file) or close it with the numbers. Runs after plan 63 stage 2 (per-file parse arenas),
which variable-size nodes need anyway. Context: plan 67 ("Where it stands", stage 2 item 3) and measurements.md.

## Why
Memory is the one axis where tttt loses to bun (1.6-2.3x at the defaults, vscode 1.75x single-threaded). Two parts:
the program (vscode 2.19 GB, the AST 1.74 GB of it with 38% zero words; bun's whole run is ~1.4 GB) and the check
(each checker's 128 MB floor and its own lib types: K x 128 MB, so 1.5 GB of budget at 12 checkers).

## Question 1: the per-checker floor at many cores (first; probably the cheaper win)
- Simulate K = 8, 12, 16 on this box (`--checkers K --threads 4`; the memory doesn't depend on the threads much) on
  vscode, effect, drizzle, zod, valibot: peak RSS against bun's at its default, and with budgets that don't scale
  with K (a total budget split down to 32-64 MB per checker, or fewer checkers than threads for the check phase).
- How much of a fresh checker is the libraries' and globals' types (`--time`'s arena right after warm-up, per K)?
  Re-ask plan 67's "no" to a shared frozen lib layer with that number at K = 12 (the costs it named: per-checker type
  ids, interning, LazyMemo).
- Output: a default policy (budget formula, checker-count cap) with its wall-time cost; implement it if it is small
  (it is a `program_set_checker_memory()` / `deal_files()` policy change), else a stage below.

## Question 2: the compact AST, prototyped on one family
- From `scripts/memtrack.sh --phase resolve_names` on vscode, pick the node family with the most zero words (expected:
  `Expr`, its inline union sized to the largest variant). Prototype exact-size allocation for it in the per-file arena
  (the variant's own size, not the union's), whatever churn the prototype needs, on a scratch worktree.
- Measure: heap after names, K1 peak, `scripts/instrcount.sh` (tsc, vscode, react, effect), and how many sites had to
  change (the extrapolation to all families is the cost estimate).
- Decide with this rule: go if the extrapolated saving is >= 400 MB on vscode at <= +1% instructions and the site
  churn is mechanical (a constructor per variant, no walk rewritten); otherwise close with the numbers. 8-byte spans
  and 32-bit node references are separate, smaller items: estimate each.
- If go: write the stages (one family per session, the suite and oracles byte-identical after each) here, and tell
  the next insights how many sessions it is.

## Validation
The numbers in measurements.md, the prototype's branch dropped (said in the commit), plan 67 "Not doing" updated.
