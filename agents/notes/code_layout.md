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
- BOLT's `-hugify` does nothing on the VM: in THP madvise mode its runtime only madvises the file-backed text, which
  needs `CONFIG_READ_ONLY_THP_FOR_FS` (unset there).
- Output byte-identical (stdout, exit code) pgo vs bolt on every corpus at K1 and 4 threads; vscode peak RSS equal.

## For the laptop
`scripts/fastbuild.sh` then, on vscode (`-p thirdparty/corpora/vscode/src/tsconfig.corpus.json --noEmit`):
- `perf stat -M PipelineL1` and `-M PipelineL2` at `--threads 1 --checkers 1`, pgo/tttt against fast/tttt
- the same at the defaults
- `perf stat -e instructions:u,cycles:u,L1-icache-load-misses,iTLB-load-misses` for both
If BOLT wins on cycles, flip fastbuild.sh's `MEASURED` to `fast` (and re-save test/instrcount.txt with it).
