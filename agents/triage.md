# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-14 (triage session, todo entry 10 of the fifteenth window): AGENTS.md is a symlink to CLAUDE.md,
the human's "100% green" became plan 66 (plan 56's content mappers un-shelved), the conditional simplification order
dependence went to bugs/type_core.md, the scenarios item was stale (210 / 0 failed). The commit messages have the rest.

From the triage session (2026-10-14, late-bound member merging):
- Late-bound members (ttttLateBoundMemberMerge) left over: an object literal method merged with a late-bound one
  (`{ mm() {}, [m](x: number) {} }`) is typed by the last alone, tsgo's has both signatures (getTypeOfSymbol of the
  merged symbol); `ObjectLiteralProp.methods` is filled but never read, the natural place for it. Several copies of
  "which declarations make one member symbol" remain (class_decl.c's bind_member_symbols/computed_symbol_first, the
  checker's MembersBuilder, check.c's MemberTables, the symbols baseline's computed_member_symbol_items): the symbols
  baseline should ask the checker's Prop decl spans everywhere, as late_member_declarations() and the object
  literal's ty_object_literal_member_symbol() now do.

- (2026-10-14, content mapper probe) `const __VERSION = "1.0.0"; const __VERSION: number = 1;` in one file with
  noUnusedLocals: tsgo reports TS2451 twice and no TS6133; tttt also reports TS6133 for one of them. Check a plain TS
  file (`scripts/filetest.sh`) before blaming the mapper.

- **For the human** (plan 62 stage 3, 2026-10-14): tttt's `--checkers` now defaults to one per core (at most one per
  64 KB of checked source), so on the 12-core machine the corpora check with 12 checkers where they had 4. Please run
  `scripts/bench.sh --checkers "4 8 12 16"` there and paste the output into measurements.md (next to the bun table):
  the per-K wall times and RSS decide whether the default should be capped below the core count (this 4-vCPU box can't
  measure past 4; the 2026-10-11 bun session saw 12 checkers 30-50% faster than 4 at +13..40% RSS).

- (2026-10-15, todo entries 7-8) /tmp holds ~990 entries (770 MB) of scratch from the 2026-10-07..12 sessions (cg-*,
  cgv.*, b-x9, bsmoke*...): nobody's current work; a triage session can delete what isn't a script's cache
  (`/tmp/mutate` was deleted this session by mistake: mutate.py regenerates it, ~minutes).
- A RuneError right after an identifier (`v\x80;` or `v\uFFFD;`, the rest of the file binary trivia after TS1490): tsgo
  also reports TS1434 "Unexpected keyword or identifier." at `v`, tttt doesn't (found 2026-10-15 probing invalid UTF-8).
