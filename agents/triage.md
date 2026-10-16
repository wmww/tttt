# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-16 (triage session, todo entry 10 of the sixteenth window): the unique symbol name in
TS2540 was fixed (`ttttReadonlyUniqueSymbolName`), TS5088 through `typeof this` (root cause found) and the
content-mapped outputs went to bugs/dts_emit.md, the human's stop request was honoured (`claude-stop`).

- **For the human** (plan 62 stage 3, 2026-10-14): tttt's `--checkers` now defaults to one per core (at most one per
  64 KB of checked source), so on the 12-core machine the corpora check with 12 checkers where they had 4. Please run
  `scripts/bench.sh --checkers "4 8 12 16"` there and paste the output into measurements.md (next to the bun table):
  the per-K wall times and RSS decide whether the default should be capped below the core count (this 4-vCPU box can't
  measure past 4; the 2026-10-11 bun session saw 12 checkers 30-50% faster than 4 at +13..40% RSS). Plan 72 asks for
  one more line per corpus there (its last section): `TTTT_RESOLVE_TIMES=1 TTTT_LOAD_POOL_STATS=1 ... --time`.
