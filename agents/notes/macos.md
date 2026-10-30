# macOS (Apple Silicon)
tttt builds and runs on macOS 15 arm64 since 520d0e981 (2026-10-29). Output is byte-identical to Linux x86-64's: the
suite's `baseline_tests --dump` (all 56712 files, K1 and `--checkers 4 --threads 4`) and the corpora tsc, zod,
drizzle, excalidraw, webpack and effect (effect differs only where a long message's truncation counts the absolute
path's characters, which differ between the trees). checkerdiff (K4, K16), threadcheck, sgcheck (K4/8/16, both
deals) and realworld (0 spurious, 0 missing) pass there on those corpora. The platform code is `#ifdef __APPLE__` /
`__linux__` / `__GLIBC__` inside the function that owns each concern (520d0e981's message lists them).

## Setting up a Mac
- Command Line Tools (Apple clang 17), Homebrew: `brew install meson ninja bash` (bash: macOS's /bin/bash is 3.2,
  without `mapfile` and with `set -u` failing on empty arrays; the scripts' `#!/usr/bin/env bash` finds Homebrew's
  once /opt/homebrew/bin is on the PATH, which a non-interactive ssh shell doesn't put there), python3 and node from
  Homebrew. `meson setup release --buildtype release && ninja -C release`.
- The suite needs `thirdparty/typescript/tsc/testdata` (350 MB: copy it, the Go sources aren't needed) and, for the 28
  contentMapper tests, `thirdparty/typescript/built/tttt-test-mapper`. tsgo needs no Go on the Mac: cross-compile on
  Linux with ensure-typescript.sh's Go environment and `GOOS=darwin GOARCH=arm64 CGO_ENABLED=0 go build` for `./cmd/tsc`
  and, with `-overlay ../built/overlay.json`, `./cmd/tttt-oracle` and `./cmd/tttt-test-mapper`, then copy the
  binaries into `thirdparty/typescript/built/`.
- Corpora: copy (`rsync -aH`) only what fits; the PGO training set (drizzle, zod, effect, excalidraw, webpack) plus
  TypeScript6 is 3.2 GB. `TTTT_NO_ENSURE=1` for threadcheck/checkerdiff keeps them from fetching the rest.

## Compiler and fast build
- Apple clang 17 is the macOS compiler (meson's default there; pgobuild.sh defaults to clang on Darwin). Homebrew's
  GCC 16 also builds (no source changes) and is equally fast after LTO + PGO: K1 medians on an M-series VM,
  2026-10-29: tsc 0.246/0.246 s, zod 0.272/0.275, drizzle 0.346/0.353, effect 0.583/0.578, excalidraw 0.380/0.381
  (GCC/clang); plain -O3 GCC was ~2% ahead. Clang wins on everything else: the training build takes 1 min against
  GCC's 2, needs no extra toolchain, and avoids GCC's aarch64 extension-elimination miscompile (e77c2b3a9).
- LTO + PGO is worth 15-16% at K1 over release (e.g. effect 0.686 -> 0.578 s). `scripts/fastbuild.sh` stops at
  pgo/tttt on Darwin (BOLT rewrites ELF only); fast/tttt -> pgo/tttt. Both PGO builds were output-identical to
  release on six corpora at the defaults and K1.
- ld64 notes: undefined weak references need `-Wl,-U,_name` (the LD_PRELOAD tools' hooks); Apple clang still makes
  tentative definitions common symbols, which ld64 aligns by size (a 1 MB array asked 32 KB, past the 16 KB segment
  alignment, fatal under werror): `-fno-common`. Clang before 20 has no flag for C23's wide enumerators but
  -Wno-pedantic (meson tests for it).

## What differs at run time
- 16 KB pages: the runtime page size (9dc8effa3). No MADV_POPULATE_WRITE (pages fault in one at a time); discarded
  arena pages use MADV_FREE_REUSABLE (macOS's MADV_DONTNEED neither zeroes nor lowers the footprint); mapped source
  texts keep MADV_DONTNEED (file pages, read back later).
- process_resident_bytes() is the physical footprint (task_info), memory_available_bytes() free + inactive +
  speculative + purgeable pages (host_statistics64). physical_cores() is hw.physicalcpu (performance and efficiency
  cores alike: a real M-series Mac's E cores get checkers too; untested on hardware with them).
- No exit reaper (clone, pidfd): the address space is torn down in process. No glibc malloc tuning: macOS's libmalloc
  is used as is (not measured against anything else).
- Directory listings use readdir() instead of getdents64.

## Scripts
Work there: threadcheck.sh, checkerdiff.py, sgcheck.py (`-f '^(zod|drizzle)$'`: its default list is source sweep
projects), realworld.py, pgobuild.sh, fastbuild.sh, the suite (`./release/baseline_tests` directly: fasttest.sh runs
ensure-typescript.sh, which wants Go). tttt_run.py sets no RLIMIT_AS on macOS (it refuses one below the address space
already reserved, and enforces none). Linux-only: anything with valgrind (test.sh -d, instrcount.sh, cgrank.py,
filetest.sh without FAST=1), perf, bwrap sandboxes (clidiff, projdiff, dtsdiff, sourcesweep's tsgo side, reduce.py,
installsandbox.py), boltbuild.sh, bench.sh (GNU time, a Linux tsgo); /proc readers degrade silently. A benchmark on
the Mac is a few lines of Python around `os.wait4` (ru_maxrss is bytes there, KB on Linux: tttt_run.maxrss_kb()).

## Benchmark (2026-10-29, main at the port + embedded libs; VM: 4 vCPU, 8 GB, macOS 15.7)
fast/tttt (clang LTO + PGO) against tsgo 7.1.0-beta (darwin/arm64, cross-compiled) and `bun check` 1.4.3
(@oven/bun-darwin-aarch64), median of 5 interleaved runs after a warm-up; wall s / CPU s / peak MB (max). "1T":
tttt and tsgo `--singleThreaded`, bun `--threads=1`. tsc-compiler is bench.sh's TypeScript6/src/compiler with its
REAL_ARGS.

| corpus | tttt | tttt 1T | tsgo | tsgo 1T | bun | bun 1T |
|---|---|---|---|---|---|---|
| tsc-compiler | 0.111 / 0.35 / 258 | 0.245 / 0.24 / 229 | 0.374 / 1.07 / 437 | 0.628 / 0.78 / 364 | 0.170 / 0.59 / 252 | 0.381 / 0.38 / 231 |
| zod | 0.132 / 0.48 / 456 | 0.275 / 0.27 / 380 | 0.740 / 2.81 / 1097 | 1.268 / 1.85 / 760 | 0.253 / 0.92 / 349 | 0.585 / 0.58 / 412 |
| drizzle | 0.176 / 0.66 / 525 | 0.352 / 0.35 / 429 | 1.032 / 3.70 / 1298 | 1.243 / 1.82 / 730 | 0.372 / 1.34 / 394 | 0.702 / 0.70 / 406 |
| effect | 0.248 / 0.94 / 704 | 0.580 / 0.58 / 614 | 1.039 / 3.51 / 1294 | 1.875 / 2.58 / 991 | 0.491 / 1.88 / 370 | 1.378 / 1.38 / 463 |
| excalidraw | 0.157 / 0.58 / 443 | 0.384 / 0.38 / 383 | 1.018 / 3.99 / 1680 | 1.280 / 1.65 / 749 | 0.296 / 1.11 / 316 | 0.668 / 0.67 / 382 |
| webpack | 0.562 / 1.11 / 606 | 0.540 / 0.54 / 525 | 0.788 / 2.56 / 950 | 1.481 / 1.85 / 788 | 0.454 / 1.54 / 429 | 1.036 / 1.04 / 437 |

At the defaults tttt is 3.4-6.5x faster than tsgo and 1.5-2.1x faster than bun, except webpack (1.4x tsgo, bun 19%
ahead), where the one-checker rerun after a history-dependent diagnostic (one_checker_rerun.md) makes the defaults no
faster than one thread (same on Linux: 1.04 s at the defaults, 0.95 single-threaded). Peak memory is above bun's on every corpus but
tsc (effect 1.9x). vscode wasn't run (2.1 GB of disk and 8 GB of RAM on the VM).
