# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-19 (triage session, todo entry 9 of the twenty-first window), three worktree agents alongside.
supabase studio's three TS2322 fixed (spread intersection index signatures, JSX attribute generic calls skipped in
the first pass, `T[never]`, `||`/`??` operand context; one unreduced leftover in bugs/type_core.md): its whole output
now equals tsgo's. The zod v4 TS5115 shape fixed (a deferred base's arguments for recursive_bases), TS2590 at the
type node, TS2428 at lib declarations, graphql-inspector's `reason?: undefined` (pseudo type of a widened literal
union), ty_buf_push()'s PGO+LTO warning (GCC's, reshaped), dtsdiff's 0/N flake now reported as "no output". The
widened-literal member order item went to bugs/dts_emit.md "Member order"; PGO+LTO and the many-core framing were
already plan 86's. Plan 19 condensed into agents/corpora.md.


- (insights 2026-10-19) mmapped sources SIGBUS when a file is truncated mid-run (an oracles.py run lost
  typescript-eslint once, entry 9's commit; hacks.md `source_text_map()`). A drop-in tsc shouldn't crash when a
  formatter rewrites a file during a check: decide between a SIGBUS handler that falls back to a read() copy (and
  re-reads/reports like tsc would) and reading files into memory where mapping isn't worth it. Measure the memory
  cost of either on vscode (mmapped text was plan 80's win).
