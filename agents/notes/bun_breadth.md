# tttt vs bun at breadth (PARTIAL: not started)

Status 2026-10-23: only the tool exists. The breadth sweep (tttt at its defaults vs bun canary 1.4.4-canary.20261022.1
on the source sweep's heaviest projects) was stopped before its first run, when the coordinator wound down.
Nothing below is measured at breadth yet.

## Tool
`scripts/perfshape.sh -p P --bun PATH` (f903d2e0e) prints one project's redacted shape: per-phase wall, CPU and
cores busy, the checkers' span against per-file check sums (wall and thread CPU), idle tails, the costliest file's
share of the span (the critical path floor), the K1 run, bun and tsgo. It is also the per-project shape report the
human pastes from private code.

## One sample (loaded box, load ~38 on 16 vCPUs)
trpc/packages/tests at the defaults: tttt 0.32 s wall vs bun 0.43 s; tttt CPU 0.71x bun's. Typecheck: 8 checkers
(16 threads) kept 4.0 cores busy, and the costliest file's wall is 41% of the checker span. bun's first run of a
project was often seconds slower than its next runs (4.0 s and 15.6 s vs ~0.3-0.7 s): warm bun once per project or
take a median of 3.

## How to continue
1. Get bun: `npm pack @oven/bun-linux-x64@1.4.4-canary.20261022.1` into scratch and untar it; the binary is
   `package/bin/bun`.
2. Select the sample: the perfsweep cache `test/perfsweep/tsgo.jsonl.gz` has 767 projects with tsgo
   `check_s >= 0.3`. Take the 500 largest.
3. Measure: `scripts/perfsweep.py --defaults-only --bun PATH -f REGEX` runs tttt at its defaults and bun once each.
   It needs a prior K1 run for `--last`; otherwise run without `--defaults-only` with `--defaults`. Alternatively,
   a small driver can interleave 3 runs of each and take the median.
4. Shape the losers: run `scripts/perfshape.sh` on the worst ~15.
5. Classify each loss by the brief's mechanisms:
   - (a) one-file critical path
   - (b) front-end share >= 40% of wall
   - (c) duplication
   - (d) sys share high
   - (e) types ratio vs tsgo > 1.3
   - (f) single-thread speed
   - (g) other

   The human says tttt uses less total CPU than bun on every project where it loses. So expect (a), cores left
   idle, and checker-count policy, not (c).
