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
- (plan 86 gate, 2026-10-19) dtsdiff's react corpus once came back "no output": bwrap "Can't bind mount /oldroot/
  ... remount /newroot/run/credentials/serial-getty@ttyS1.service: Invalid argument" (a host mount bwrap can't
  remount read-only). dtsdiff was 1324 / 1350 for it, 1325 otherwise. If it recurs, have dtsdiff/clidiff's bwrap
  call bind only what the probe needs rather than `/`, or retry once.
- (plan 86 gate) oracles.py exceeded the 1-hour background cap with a seeded source sweep running alongside, and
  perfsweep printed only "tttt: release/tttt" as its summary line in that run (perfsweep.py's `--summary` output
  under load, or a parse of oracles.py?). Look at what perfsweep's last line is when tsgo's side is cached.
- (fan-out 2026-10-20) dokploy/apps/dokploy times out under `scripts/kdiff.py -k 1 -m 0` (a fresh checker per file,
  oracles.py's recycle line "1 timed out"): probably just slow (each file re-resolves the program's shared types), but
  check whether `--checker-memory 0` makes some per-checker work quadratic there before dismissing it.
- (fan-out 2026-10-20) perfsweep's wall/CPU ratio: 140 projects show tttt wall > 2.5x its CPU time (nuqs docs 20.6 s
  wall, 1.4 s CPU). One live recheck (faker: 1.25 s wall, 0.65 s CPU at load 6) says load, not a stall, but no one
  has looked at it on a quiet machine: rerun `scripts/perfsweep.py -f 'nuqs|faker|umami'` with nothing else running.
