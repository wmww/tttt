# arm64 (aarch64) Linux

tttt builds, passes and runs byte-identical to x86-64 on aarch64 Linux since 2026-10-29 (first port: an arm64 VM, 32
vCPU Neoverse V2, 128 GB, Ubuntu 26.04, GCC 15.2, 4 KB pages). This note is the setup and what the port found.

## Setting up a fresh box (any Linux, arm64 or x86-64)
- Packages (Ubuntu/Debian names): `build-essential meson ninja-build valgrind clang llvm llvm-bolt bubblewrap python3
  rsync curl git time bc nodejs npm gdb`. The system node/npm (22 is fine) is what `ensure-corpora.sh` installs with;
  `ensure-sources.sh` fetches its own pinned node 24 and bun for the source repos.
- `thirdparty/ensure-typescript.sh`: clones the oracle and builds tsgo and the oracle tools with a pinned Go it
  downloads for the machine (amd64 or arm64, sha256-pinned: add a checksum for another architecture). ~3 min.
- `thirdparty/ensure-corpora.sh`: ~10 min (DefinitelyTyped's install is most of it). `thirdparty/ensure-sources.sh`:
  the ~150 source repos, 4 installs at a time, about an hour; `ensure-sources.sh NAME...` installs only some
  (sgcheck's hub projects are 21 repos).
- `scripts/fastbuild.sh`: PGO + BOLT work on aarch64 with Ubuntu's llvm-bolt 21 (its instrumentation runtime needs
  no rebuild there; boltbuild.sh's rebuild path picks `sys_aarch64.h` if it ever does).
- Always check the fast build against release (both at the defaults and `--checkers 1 --threads 1` on the -p corpora,
  byte for byte) on a new machine or compiler: that is how the GCC bug below was found.

## What the port found (fixed: 9dc8effa3, e77c2b3a9, ad08dbadd and the tsan.sh change)
- **48-bit user addresses**: aarch64's mmap returns addresses up to 2^48, x86-64's up to 2^47. `ast_segment_map` was
  sized for 47 bits (the first parse hit its check()); `USER_ADDRESS_BITS` (ast_alloc.h) is 48 now. Every pointer
  packing elsewhere (kind << 56 link keys, relation and intern keys) needs < 2^56 and is fine. Past 48 bits Linux goes
  only for an mmap hint that asks (both arches).
- **Page size**: `page_size()` (prelude.c) is the kernel's; `AST_PAGE`, the arena/zero-pages populate ranges and
  `resident_mb()` use it. The dangerous one was `ast_arena_drop()`: a range rounded to 4 KB and given to
  madvise(MADV_DONTNEED) on a 16/64 KB kernel would have its length rounded up by the kernel, zeroing a live
  neighbour. Logical "pages" (LAZY_PAGE_BITS, memo pages) are not the kernel's and stay. **Tested on a 64 KB
  kernel** (Ubuntu's linux-image-*-64k, booted once with grub-reboot): suite identical (K1, and K32 / 32 threads
  against K1: 0 of 56712 dump files differ), all 14 corpora 0 / 0 (vue's known 0 / 2), fast/tttt = release 28 / 28,
  threadcheck at 32 threads and checkerdiff at K8 and K32 identical. 16 KB Linux kernels untested.
- `__rdtsc` (overlay.c TTTT_GRAPH_WORK, taint.c) -> `cycle_counter()` (prelude.h: CNTVCT_EL0 on aarch64, a fixed-rate
  counter: ticks, not cycles); `SYS_poll` (the exit reaper) -> `SYS_ppoll`, aarch64 has no poll syscall.
- **`char` is unsigned on aarch64**: no test changes. Checked on x86 with `-Dc_args=-funsigned-char` (suite results
  byte-identical) and by the arm64 suite itself.
- **GCC 15's `ext-dce` miscompiles the LTO + PGO build on aarch64** (pgobuild.sh now passes `-fno-ext-dce` on every
  arch, instruction-neutral on x86): in class_decl_find_private_name() the Span returned by class_item_private_name()
  went on as a csview without sign-extending its i32 size, so the 64-bit size held Span.file in its upper half; every
  `this.#x` reported TS18013 and webpack segfaulted. Plain -O3, LTO alone, PGO without LTO and PGO's implied flags
  without a profile were all correct, so only the shipped build showed it. Not reduced to a GCC report (it needs the
  profile). Bisection recipe: a PGO build reusing pgo/'s `.gcda` must sit at the same relative path as pgo/ (a
  directory directly in the repo: GCC's profile checksum includes the source path), and `-g` doesn't change codegen,
  so `meson setup pgg -Db_lto=true -Db_pgo=use -Dc_args=[...,-g]` plus `cp pgo/tttt.p/*.gcda pgg/tttt.p/` gives a
  debuggable copy of the shipped binary.
- **GCC's libtsan on aarch64 dies of SIGILL** at the first pthread_cond_wait (its call_pthread_cancel_with_cleanup
  branches into glibc's BTI-guarded sigsetjmp with a jump BTI rejects; GCC 15.2, glibc 2.43; the glibc BTI tunable
  doesn't help). scripts/sharedgraph/tsan.sh builds with clang there.
- Memory ordering: see "Verification" below; nothing x86 hid showed up.
- Found along the way, x86-only: the suite harness at `--threads > 1` segfaulted on x86 (4-5 runs in 6) in
  load_pool_run(): debug_allocator.h's 8-byte offset made every block of the harness's (DEBUG_ALLOCATOR) build only
  8-aligned, and GCC vectorised LoadPool's two adjacent double counters into a `movapd`. The offset is
  alignof(max_align_t) now. aarch64's vector loads take any alignment, so arm never crashed.

## Verification at the port (2026-10-29)
On the arm64 VM, release builds unless said otherwise; every comparison byte for byte:
- **Suite**: results identical to x86's saved test/baseline_results.ts (at each main the port went through; later
  only bun43-C15c's gain from d2ae85f9d, which x86 shows too).
- **Corpora** (realworld.py, all 14 -p corpora): 0 spurious / 0 missing everywhere but vue's known 0 / 2.
- **Fast build**: fast/tttt (PGO + BOLT) against release on the 14 corpora at the defaults and K1: 28 / 28 identical
  (after `-fno-ext-dce`; 22 / 28 before).
- **Concurrency** (the reason for real hardware; x86's TSO hides orderings too weak for arm64): threadcheck.sh at
  THREADS 4, 8, 32 (K8, three runs each) and checkerdiff.py at K 2, 4, 8, 16, 32 on every corpus: all identical.
  sgcheck.py at K 4, 8, 16, 32 (both deals, three runs each): 61 projects, 1464 shared-graph runs, 0 differ from
  plain K1. kdiff.py: DefinitelyTyped K16 0 / 8595, K32 1 / 8595 (nodemailer/v6, the same bytes on x86: triage.md),
  the installed source sweep K16 1 / 3554 (rxjs, the known x86 elaboration). TSan (clang, tsan.sh) on every corpus at
  4, 16 and 32 threads: three race groups, none arm-specific, fixed (c640edf82, 4cfa18295); clean after.
  Nothing found that x86 doesn't show: no ordering bug surfaced.
- **Source sweep** (216 of 378 repos installed): 5011 / 5065 match the cached tsc verdicts; the rest are known
  entries, install drift (openclaw's generated kysely types) and one new spurious TS7022 that x86 shows too
  (bugs/sourcesweep_checker.md "Found on the arm64 box").

## Untested
- 16 KB page kernels on Linux (macOS's 16 KB pages are the mac port's: notes/macos.md).
- Old glibc / distributable builds (out of scope).
