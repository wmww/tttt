# Code footprint and layout (the front-end lever)

tttt is frontend-bound on real hardware (measurements.md, "The 12-core box at 2989c0f0": 53% of slots, the I-cache
missing 23% of fetches). This note is what was built and measured against it on the VM (no perf counters there:
cachegrind's simulated I1 misses and instructions stand in), and what the laptop should confirm.

**Use `fast/tttt`** (`scripts/fastbuild.sh`): the laptop and every timing use it; the measuring scripts (bench.sh,
instrcount.sh, perfsweep.py, cgrank.py) take `scripts/fastbuild.sh --measured`, which is pgo/tttt until the laptop's
counters confirm BOLT, then its `MEASURED=pgo` becomes `fast`. When the pipeline changes, fastbuild.sh changes and
the path doesn't.

## The pipeline (2026-10-22)
1. `scripts/pgobuild.sh`: LTO + PGO (pgo/tttt), with `-fno-reorder-blocks-and-partition -fno-plt`, linked with
   `--emit-relocs -z now`.
2. `scripts/boltbuild.sh`: llvm-bolt (LLVM 21) in instrumentation mode on pgobuild's training set at K1 and at the
   defaults, then ext-tsp blocks, cdsort functions, hot/cold splitting, ICF (bolt/tttt). Hot text ~2.05 MB in its own
   2 MB-aligned segment.

## Measured (cachegrind, K1)
| | zod I1 misses | drizzle | tsc (untrained) | vscode (untrained) | instructions |
|---|---|---|---|---|---|
| pgo -> bolt | 81.1 -> 68.2 M (-16%) | 84.1 -> 70.4 M (-16%) | 60.7 -> 51.0 M (-16%) | 1438 -> 1207 M (-16%) | -0.13 to -0.28% |
| + -fno-plt -z now | -2.7% | -2.8% | -1.9% | | -0.17 to -0.29% |

- **GCC's hot/cold partitioning must be off for BOLT**: with its 1909 `.cold` fragments BOLT's profile was broken
  (CFG discontinuity 60% on the hottest functions) and the bolted binary had 5-10% more I1 misses and +1.2%
  instructions. The flag is neutral on pgo/tttt itself.
- Two PGO builds of the same flags differ by ~2% in I1 misses (the defaults' training runs are multi-threaded):
  smaller I1 effects than that are noise; instructions are stable to ~0.05%.
- `-fno-plt`: ~98 M PLT calls per training profile (libc) become GOT calls; BOLT's `-plt=hot` (with -z now) got the
  same within noise, -fno-plt is simpler and helps pgo/tttt too.
- `-z now`'s startup cost: the dynamic loader's whole startup is ~45k cycles for 251 relocations either way
  (`LD_DEBUG=statistics`; LD_BIND_NOW=1 on the lazy binary: 41.6k vs 46.7k cycles), i.e. < 5 us. -fno-plt resolves
  its GOT entries at load anyway.
- BOLT's `-hugify` does nothing on the VM: in THP madvise mode its runtime only madvises the file-backed text, which
  needs `CONFIG_READ_ONLY_THP_FOR_FS` (unset there).
- Output byte-identical (stdout, exit code) pgo vs bolt on every corpus at K1 and 4 threads; vscode peak RSS equal.

## Compiler flags measured and declined (deliverable 2)
Each row a full PGO + BOLT build with the flag added (PGO_CFLAGS/BOLT_EXTRA), cachegrind at K1 against two baseline
builds (which differ by <= 0.5% in I1 misses and 0.1% in instructions here). Instructions / I1 misses:

| flag | text | zod | drizzle | tsc | verdict |
|---|---|---|---|---|---|
| baseline (2 builds) | 4.20 MB, hot 2.06 MB | 3.689 G / 66.0 M | 4.544 G / 68.3 M | 3.715 G / 49.4 M | |
| `-O2` | 3.60, hot 1.64 | +9.1% / -3% | +9.8% / -9% | +8.1% / 0% | no |
| `-Os` | 2.73, hot 1.20 | +72% / -16% | +72% / -26% | +73% / -3% | no |
| `--param=max-inline-insns-auto=10` | 3.74, hot 1.71 | +8.4% / -3% | +8.3% / -7% | +8.3% / -3% | no |
| `--param=inline-unit-growth=20` | 3.98, hot 1.92 | +1.3% / +1% | +1.3% / -1% | +1.3% / +2% | no |
| `--param=inline-unit-growth=80` | = | = / +0.5% | = / = | = / +1% | the limit doesn't bind |
| `-falign-functions=16 -falign-jumps=1 -falign-loops=1 -falign-labels=1` | -0.7% | = / = | = / = | = / +2% | no |
| `-fno-tree-vectorize` | = | = / -0.5% | = / -0.6% | -0.2% / = | noise |
| `-fno-ipa-cp-clone` | = | = / +0.5% | -0.1% / = | = / +1.5% | no |
| `-fno-split-paths -fno-tracer` | 4.12, hot 2.02 | +0.8% / -2.6% | +0.9% / -2.9% | +0.6% / -2.3% | marginal |
| `-fno-unroll-loops` | = | = / = | = / +2% | = / = | no |
| `--param=max-unroll-times=2` | = | = / = | = / = | = / = | no |
| **`-fno-peel-loops`** | 4.03, hot 1.95 | **+2.0% / -9.2%** | **+2.1% / -10.3%** | **+1.1% / -5.4%** | laptop candidate |
| `--param=max-peeled-insns=40` | 4.12, hot 2.00 | +0.4% / -2.1% | +0.3% / -2.7% | +0.4% / -2.4% | marginal |
| `--param=max-peel-times=4` | 4.17 | +0.2% / -1.7% | +0.2% / -1.4% | +0.3% / -0.4% | no |
| `--param=max-completely-peel-times=4` | 4.18 | +0.6% / = | +0.5% / +1% | +1.6% / = | no |
| BOLT `-align-functions=16` / `=32` | = | = / +4.4% / +2.3% | = / +4.6% / +2.7% | = / +4.8% / +2.4% | no |

- PGO already compiles never-executed functions for size and BOLT moves them out of the hot text, so `-Os` on cold
  units (the brief's idea) can't shrink the hot footprint; `-Os` everywhere shows the ceiling: hot text halves, I1
  misses -16 to -26%, at +72% instructions.
- Loop peeling (GCC enables it with profile feedback) is the one lever in reach: `-fno-peel-loops` takes 110 KB of
  hot text and 5-10% of the I1 misses for +1-2% instructions. Over the 1% instruction budget, so not in pgobuild.sh;
  the laptop can A/B it in cycles with `PGO_DIR=pgo-np BOLT_DIR=bolt-np PGO_CFLAGS=-fno-peel-loops
  scripts/boltbuild.sh` against fast/tttt.
- ICF: BOLT's `-icf=all` folds only 4 functions (GCC's -fipa-icf, on at -O2, got the rest); `-fno-semantic-interposition`
  and `-fvisibility=hidden` change nothing in a PIE executable.

## clang against GCC
`CC=clang PGO_DIR=... BOLT_DIR=... scripts/boltbuild.sh` builds clang's LTO + PGO (+ BOLT) beside the default.
Cachegrind at K1, instructions / I1 misses / stall estimate (clang 21.1.8, GCC 15.2):

| | zod | drizzle | tsc |
|---|---|---|---|
| gcc -O3 | 4.405 G / 121.3 M / 2.13 G | 5.481 G / 113.3 M / 2.26 G | 4.328 G / 100.7 M / 1.78 G |
| clang -O3 | +3.3% / +4.7% / +7.5% | +3.1% / +3.1% / +6.6% | +3.7% / +3.8% / +4.5% |
| gcc PGO | 3.696 G / 79.3 M / 1.71 G | 4.554 G / 81.8 M / 1.95 G | 3.720 G / 60.0 M / 1.37 G |
| clang PGO | -3.2% / = / +6.4% | -1.6% / -0.5% / +5.6% | -4.7% / +1.2% / +3.6% |
| gcc PGO + BOLT | 3.690 G / 66.0 M / 1.57 G | 4.546 G / 68.1 M / 1.81 G | 3.716 G / 50.1 M / 1.27 G |
| clang PGO + BOLT | -3.2% / +9.6% / +11.5% | -1.6% / +7.8% / +9.4% | -4.7% / +11.8% / +7.1% |

Text: gcc -O3 5.23 MB, clang -O3 4.30 MB; gcc PGO 4.40 MB, clang PGO 7.57 MB (its profile-guided inlining). clang's
PGO build executes 2-5% fewer instructions but its hot text is 3.57 MB after BOLT against GCC's 2.06 MB, and
the I1 misses and the stall estimate are 7-12% worse: by the proxies GCC stays. The laptop could still A/B cycles
(fewer instructions against a bigger footprint), but nothing here says clang wins.

## Huge pages for the hot text (main.c's huge_text())
At startup, the first 2 MB of BOLT's hot segment (an executable PT_LOAD with p_align >= 2 MB, so pgo/ and release/
binaries are left alone) are copied into a 2 MB-aligned anonymous MADV_HUGEPAGE page, which mremap(MREMAP_FIXED)
moves over the text atomically. Gated by huge_pages_take()'s free-block budget (no synchronous compaction), skipped
under valgrind (LD_PRELOAD has vgpreload) and with `TTTT_HUGE_TEXT=0`. Verified in smaps: `AnonHugePages: 2048 kB` on
the hot range (BOLT's hot text is 2.05 MB: the first page covers 97% of it).
- Cost: ~0.35 ms of sys time per process on the loaded VM (`--version`, 3x200 runs, CPU time: 0.63 vs 0.27 ms sys):
  the kernel zeroing the huge page, the 2 MB copy, the buddyinfo read. Under the 1 ms budget for nest's 70 ms; the
  laptop should time nest with and without TTTT_HUGE_TEXT=0.
- RSS: +2 MB anonymous at most (the file pages it replaces were mostly resident anyway).
- perf attributes the hot code to an anonymous mapping (`//anon`): profile with `TTTT_HUGE_TEXT=0`.
- The VM can't measure the iTLB effect (cachegrind models no TLB): the laptop's `iTLB-load-misses` decides.

## For the laptop
`scripts/fastbuild.sh` then, on vscode (`-p thirdparty/corpora/vscode/src/tsconfig.corpus.json --noEmit`):
- `perf stat -M PipelineL1` and `-M PipelineL2` at `--threads 1 --checkers 1`, pgo/tttt against fast/tttt
- the same at the defaults
- `perf stat -e instructions:u,cycles:u,L1-icache-load-misses,iTLB-load-misses` for both, and fast/tttt with
  `TTTT_HUGE_TEXT=0` (huge text pages off) as the third column
- nest's wall (`hyperfine -N`) with and without `TTTT_HUGE_TEXT=0`: the remap must cost it nothing measurable
If BOLT wins on cycles, flip fastbuild.sh's `MEASURED` to `fast` (and re-save test/instrcount.txt with it).
