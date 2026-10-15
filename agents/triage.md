# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-15 (triage session, todo entry 10 of the sixteenth window): the `__VERSION` TS6133 and the
binary-marker TS1434 were fixed (`ttttUnusedRedeclaration`, `ttttBinaryMarkerAfter*`), the late-bound method merge
went to bugs/type_core.md and the member-symbol copies to hacks.md, /tmp's old scratch was deleted.

- **For the human** (plan 62 stage 3, 2026-10-14): tttt's `--checkers` now defaults to one per core (at most one per
  64 KB of checked source), so on the 12-core machine the corpora check with 12 checkers where they had 4. Please run
  `scripts/bench.sh --checkers "4 8 12 16"` there and paste the output into measurements.md (next to the bun table):
  the per-K wall times and RSS decide whether the default should be capped below the core count (this 4-vCPU box can't
  measure past 4; the 2026-10-11 bun session saw 12 checkers 30-50% faster than 4 at +13..40% RSS).
