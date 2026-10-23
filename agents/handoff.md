# Handoff from the fourth coordinator (2026-10-22 .. 2026-10-23)

Read this, act on it, delete it (CLAUDE.md "Agents directory"). Then vision.md and todo.md. The human talks to you
in the conversation; workers run in worktrees from briefs; a merger lands branches onto `main` with gates
(coordinator.md). The human's standing priorities: distinctly faster than bun canary on real projects (they keep a
private project set as the judge and won't share it, not even redacted numbers); speed over memory, but RSS is now
"a lot" and worth cutting where it's cheap; easy, measured wins first; correctness never traded; no permission
prompts (workers must not `rm -rf` variable paths).

## State at hand-off (main at or past 8d4518ecc, plus whatever the merger landed from the branches below)
- Oracles: suite 13999 / 14035 (0 lost all session; 2 by-design failing own tests), every real-code oracle equal or
  better than at the start; source sweep 316 repos / 5464 projects (two refreshes this session: app code and
  bun-lockfile repos, day one 94%, after 97%); kdiff/recycle 3 / 3969 (tsgo-shared). measurements.md "Oracle totals".
- Perf (the human's laptop, measurements.md "The 12-core box at 3a77c827"): the kernel-time round (stack pool, glibc
  mmap/trim thresholds, texts kept mapped, huge-page tables) cut kernel cycles 21-28% -> 6-9% of user, TLB shootdowns
  -95..-99%, walls -13..-23% on every mid/large corpus, vscode 1.70 s median; tttt beats the newest bun canary at the
  defaults on every bench corpus (vscode 1.75 vs 1.93). **Validated by the human on their private set: 330a6eb97 is
  >10% faster than 1c31b0469 on most projects, "the biggest validated win in a long time".** Everything before it
  (BOLT, huge text page, parser -14%, front end) was bench-fitted: their set moved only 0.98 over that period.
  Lesson: the OS layer at the defaults was the bottleneck and this VM can't see it; cycles-level claims need the laptop.
- Memory: vscode 5.5 GB at the defaults vs bun 2.2 (K1 4.3); the kernel round added +4..15% RSS (accepted).

## In flight at hand-off (branches may be landed by the merger after this was written: check `git log main`,
`git worktree list`, `git branch -a | grep wt-`)
1. **wt-ktime2** (worker ktime2): the exit tail. Commit 35130c775: `exit_detached()` in src/main.c hands the address
   space to a reaper child so the parent exits before the kernel frees 1-5 GB of pages (tail 7-66 ms = 3-6% of wall
   -> ~0; drizzle 0.290 -> 0.278, zod 0.204 -> 0.182, vscode 1.93 -> 1.81 here). Caveats in hacks.md: sys time
   accounting moves to the reaper (`TTTT_SYNC_EXIT=1` for honest sys; the measuring scripts were to be adjusted),
   subreaper callers see one extra child. Deliverable 2 (remaining page faults by site; pre-fault checkers' first
   chunks off the critical path) got as far as an attribution (drizzle: checkers 37k faults / load workers 17k /
   main 8k; the main cause: huge-page arena chunks are 8 MB + 4 KB mappings not 2 MB-aligned, so their ends are
   mapped 4 KB at a time) and an UNGATED fix: `agents/handoff-ktime2/arena-huge-align.patch` (2 MB-aligned whole-page
   chunks: drizzle faults 73k -> 44k, vscode 302k -> 187k, sys -10..-20%, RSS +1%, nest +3%). Gate it (suite,
   threadcheck, checkerdiff, instrcount) and land it first thing. Deliverable 3 (does the pre-check malloc_trim still
   pay: RSS and madvise count with TTTT_NO_TRIM=1) and the pre-fault idea (resolve names keeps ~4.5 of 16 cores busy:
   a helper could populate checkers' first chunks then) not started. The laptop should measure the tail with THP
   `always`. Note: MADV_POPULATE_WRITE counts as minor faults on this box's kernel; the laptop's perf page-faults
   counts only real traps (6.7k vs ~72k): compare like with like.
2. **wt-pb** (worker pb): `scripts/perfshape.sh` landed (f903d2e0e: a redacted per-project shape report; the human
   declined to run it on their set). The breadth sweep (tttt at the defaults vs bun canary 1.4.4-canary.20261022.1 on
   the ~500 largest source-sweep projects, ranked by wall ratio, worst 15 dissected by shape) was running; partial
   results and how to resume go into agents/notes/bun_breadth.md. The human's one fact about their losing projects:
   "tttt still uses less total CPU on every project": the losses are critical-path (idle cores), not duplication.
   First data point: trpc/packages/tests keeps 4 of 16 cores busy with 8 checkers, the costliest file is 41% of the
   checkers' span. Expected remedies: policy and scheduling (more checkers when the critical path is long, splitting
   what one dominating file holds, starting checking earlier).
3. **wt-memc** (worker memc): memory census for per-file release: what share of a checker's arena is per-file
   results (expression/flow/node memos, per-file relation work) that nothing reads after the file is done (bun frees
   those at task end; that is most of its 2.2 GB on vscode), the design of a per-file region dropped at file end, and
   the recycle-budget tradeoff; results into agents/notes/memory.md. The human asked whether RSS can be cut
   substantially at little speed cost: three levers in order: the recycle-budget default (laptop sweep pending:
   `--checker-memory` 4096/3072/2048 on vscode and drizzle, wall vs RSS), per-file release (structural), phase-
   boundary trims (small; a trim burst when no checker runs).
4. **Laptop asks pending** (prompts were given in conversation; redo them from here): (a) remaining kernel time:
   `perf record -e cycles:k -g` top symbols on drizzle/vscode at the defaults; (b) the exit tail with THP always;
   (c) thread placement on the hybrid CPU: tttt pinned to the 8 Zen 5 threads vs all 24 vs the 16 Zen 5c vs 12 one-
   per-core (`taskset`), drizzle and vscode, wall and CPU; (d) the recycle-budget sweep above; (e) rerun the bench on
   main after the reaper lands.

## Decisions made this session (with reasons; details in vision.md "Decisions" and the notes)
- Sharing (notes/sharing_design.md, measurements.md "at 48cd9835"): bun's model is work, not incompatible, but on
  real hardware the shared graph wins only on drizzle (-14% wall) and loses on zod/effect/vue (+2/+11/+25%): the IPC
  cost of shared lines outweighs the instructions saved except on drizzle-shaped projects; the content-order nesting
  test makes it deterministic (160/161 loaded runs) but moves 3 drizzle elaboration chains away from tsc's text.
  Verdict: stop spending on sharing; the record is complete. Our per-checker model is at the hardware's ceiling for
  tsc's algorithm; the absolute wall is what matters and we lead bun on it.
- The deep-nesting HACK removed (pi-bolt's 120 wrong diagnostics) at react +36% / type-fest +7% instructions: tsc's
  own work (form.tsx is half of tsgo's react check too). The human hasn't objected; one-commit revert if they do.
- Two documented hacks produced wrong answers this week (the deep-nesting target push; the generic-mapped member
  fallback): when a divergence appears, hacks.md is the first suspect.
- A semantics commit needs a repro or the full sweep, and relation changes are gated at several checker counts
  (kdiff on the affected project): the recursion-identity commit (reverted) and better-drizzle's K-dependent TS2502.
- Checker policy: one checker per 16 KB of source up to the physical cores, more for huge programs, capped by the
  largest file's share only when that file is >= 1 MB (TypeScript6 K4 wins; type-fest must not be capped).
- Declined with write-ups (don't redo): affinity/cost-estimate scheduling, variance sharing, interning names,
  clang, persistent thread pool (design in notes/front_end.md), fork+warm-prefix, batched publication, slot padding,
  directory-listing stats as a wall win (landed but nil), the deferred-body heuristic, arrayFrom.

## What I'd do next, in order
1. Land ktime2's and pb's and memc's branches (merger), rerun the laptop bench, then a docs refresh (vision State:
   the validated win; Decisions: the sharing verdict; todo: the list below).
2. The breadth sweep's worst projects: the critical-path/scheduling fixes it names (this is where the human's
   remaining losses to bun live).
3. Memory: the recycle-budget default from the laptop sweep; then per-file release if the census says it's large.
4. Kernel round three from the laptop's `cycles:k` profile and the thread-placement experiment.
5. Correctness: kebo's mobx-state-tree cycle (lazy parameter types in signature resolution: design), mcp-linker's
   1.73x types, the recursion-identity repro (youtube-enhancer), hono's this-instantiation design, better-drizzle's
   two TS2578, a fifth refresh rule when the human reports new divergences.
6. Small projects: react is form.tsx-bound (tsc too); nest is start-up bound (front end 2/3 of 55 ms).

## Hygiene
- The merger agent is the only one that should push to main; the human's laptop pushes measurements directly
  (pull --rebase before every fast-forward). Scripts-only and docs-only commits skip the oracle gates. No vscode
  callgrind per batch (once per round). `scripts/fastbuild.sh` -> `fast/tttt` is the only documented build; the
  measuring scripts measure it.
- Disk: thirdparty/sources is 44 GB (316 repos); ~28 GB free; budget the next refresh.
- Worktrees at hand-off: wt-ktime2, wt-pb, wt-memc (workers' hand-offs committed on them), /tmp/wt-merge (the
  merger's). Remove each after landing.
