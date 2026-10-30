# Windows x86-64
tttt builds and runs on Windows (Server 2025, x86-64) on branch `win` (2026-10-29/30; not on main: the human's
decision, it may not land in v3). Its output there is byte-identical to tsgo 7.1's Windows build on the corpora and
its suite dump to Linux's (numbers below). The platform code is `#ifdef _WIN32` in the function that owns each
concern, like the macOS port, plus `src/win32/`: an include directory used only on Windows whose headers stand in for
the POSIX ones Windows lacks and whose win32.c implements them.

## Toolchain: MSYS2 CLANG64 (clang 23, lld, UCRT, compiler-rt), static
- The code is C23 with GNU extensions (statement expressions, `__auto_type`, `[[gnu::...]]`): MSVC is out. clang
  targeting MinGW-w64 (UCRT) builds it with no source changes beyond the platform code. Both MSYS2 compilers use
  native TLS for `thread_local` (`_tls_index` and `%gs:88`; older MinGW GCCs used emulated TLS, a call per access).
- `-mno-ms-bitfields` (meson.build): MinGW targets default to MSVC's bitfield layout, which grew `Expr` past its
  static_assert (72 bytes); GCC's layout keeps every struct its Linux size.
- `-static`: the binary needs only system DLLs (`ldd release/tttt.exe`: ntdll, kernel32, kernelbase, ucrtbase), no
  MinGW runtime (libc++/compiler-rt and winpthreads are linked in).
- The manifest (src/win32/tttt.manifest, linked as a resource) sets the process code page to UTF-8 (`activeCodePage`,
  Windows 10 1903+): argv, getenv() and the C runtime's file functions take UTF-8, so the code keeps `char*` paths.
  `longPathAware` lifts MAX_PATH; src/win32/limits.h makes PATH_MAX 4096 (MinGW's is 260).
- `-Wl,--stack,268435456`: each thread reserves 256 MB of stack (what worker_stack_size() caps at on Linux), committed
  as it grows. load_pool.c's own stack pool is Linux/macOS only: winpthreads can't run a thread on a given stack (it
  would commit the size).
- Every file and the standard streams are binary (win32.c's constructor: `_set_fmode(_O_BINARY)`, `_setmode`): tsgo
  writes LF on Windows too, and a text-mode read would turn CRLF sources into LF (positions) and stop at a Ctrl-Z.
- Cross-compiling: `zig cc -target x86_64-windows-gnu` (zig 0.17) with a meson cross file builds it on Linux in a
  minute (llvm-windres for the manifest) and wine runs it, enough for compile errors; the real verification is on
  Windows. The cross file: `[binaries] c = zig-cc wrapper, ar, ranlib, windres = llvm-windres --target=x86_64-w64-mingw32
  -I<zig's any-windows-any include dir>`, `[host_machine] system = 'windows'`.

## Memory: VirtualAlloc with lazy commit (src/win32/win32.c)
Linux's anonymous memory is reserved address space whose pages appear zeroed when first touched; Windows' faults until
committed, and commit counts against RAM + page file (no overcommit). src/win32's `mmap()` reserves, registers the
range, and a vectored exception handler commits the part of a registered range an access faults on (1 MB at a time).
So MAP_NORESERVE ranges cost commit only as used, `MADV_DONTNEED` decommits (the next touch reads zeroes, exactly
Linux's semantics), `MADV_POPULATE_WRITE` (pages_populate()) commits up front, so the code that populates ahead (AST
slabs, checker arenas, read chunks, big tables) takes no exception. A mapping without MAP_NORESERVE is committed whole.
munmap() of part of a reservation (ast_alloc.c's alignment trim) decommits it and leaves it reserved (Windows can't
release part of one). File mappings aren't supported: source texts are always read (source_file.c), which is Linux's
default too (TTTT_TEXT_READ_BELOW); no SIGBUS handler. aligned_alloc() doesn't exist in UCRT: cache_aligned_alloc()
(prelude.h).
- Page faults cost more: a demand-zero 4 KB fault is ~1.2 us (measured: 256 MB touched in 79 ms), with no cheap
  prefault (VirtualLock + VirtualUnlock: 181 ms for 256 MB; PrefetchVirtualMemory: nothing for fresh pages); Linux
  faults 2 MB THP pages and MADV_POPULATE_WRITEs. Large pages need SeLockMemoryPrivilege, which users don't have.
- `long` is 32 bits (LLP64): ~20 uses in src/, none holding a size, pointer or offset but one debug print (fixed); off_t
  is 64-bit (`-D_FILE_OFFSET_BITS=64`).

## Paths
tsgo on Windows: forward slashes everywhere (`C:/dir/file.ts`), case-insensitive file system
(`useCaseSensitiveFileNames` false), stdout LF. tttt keeps paths UTF-8 with forward slashes: getcwd()'s backslashes
become slashes (cleat's real_fs_get_cwd, cleat-windows-patch.txt), realpath() is GetFinalPathNameByHandleW like tsgo's
nativepath.Realpath (the case as on disk, `C:` upper case).
- **Roots**: `path_root_size()` (string_helpers.h) is the one rule: `/` everywhere, and with TTTT_WINDOWS_PATHS (on
  `_WIN32`) `C:/` and `C:`. Every `buf[0] == '/'` root test and `size == 1` root test became path_is_rooted() /
  path_ends_with_slash() / path_is_root(); cleat's absolute_path_of() and relative_path_of() know drive roots (another
  drive: no relative path, the absolute one; segments compare without case on Windows). A UNC path `//server/share`
  keeps the root `/` (tsgo reads nothing on a network path, and joins like `"/" + "/package.json"` must keep
  meaning what they mean on Linux).
- **Testing the path rules on Linux**: `meson setup B -Dc_args=-DTTTT_WINDOWS_PATHS=1` builds Linux tttt with
  Windows' root rules; its suite dump is byte-identical to the plain build's, and reproduced the Windows suite's path
  divergences exactly while they existed.
- **Backslashes**: tsgo normalizes `\` to `/` in every path from the command line or a config, on every platform:
  `-p` and file arguments (main.c) and tsconfig.c's path_join() (include/exclude/files, paths patterns, outDir...)
  do now. This was a Linux bug too (`-p 'dir\tsconfig.json'`, `"include": ["lib\\*.ts"]`).
- **Directory listings**: cached_fs.c lists with FindFirstFileExW and looks names up case-insensitively (ASCII folded;
  a miss of a non-ASCII name stats); src/win32/dirent.h gives readdir() a d_type (tsconfig.c's walk, cleat).
- **Not done: case-insensitive file identity.** Two references to one file in different cases (`./B` and `b.ts`) are
  one file to tsgo (its Path key is lower case) with TS1261/TS1149 under forceConsistentCasingInFileNames; tttt keys
  files by exact path and loads it twice. tttt implements neither TS1149 nor TS1261 on any platform (tsgo reports them
  on Linux too, for two files that differ only in case). agents/bugs/windows.md.

## The process
- Threads: winpthreads (static). Not measured against native threads; the checkers' locks are few.
- Subprocesses: content mappers spawn with CreateProcessW over two pipes (content_mapper.c's mapper_spawn(); Go's
  `%PATH%` message when the program isn't found); the 5 s initialize deadline polls the pipe (PeekNamedPipe).
  cleat's run_subproc() is unsupported (cleat-windows-patch.txt): the suite runner's `git rev-parse` and `-d`'s diff
  are skipped.
- physical_cores(): the cores in the affinity mask (GetLogicalProcessorInformationEx); memory_available_bytes():
  GlobalMemoryStatusEx; process_resident_bytes(): the working set; getrusage(): GetProcessTimes (src/win32).
- No exit reaper (Linux only). backtrace() is CaptureStackBackTrace, addresses only (src/win32/execinfo.h).

## The VM and its setup
- MSYS2 (`msys2-base-x86_64-latest.sfx.exe -y -oC:\`), then `pacman -S git make rsync zstd patch diffutils
  mingw-w64-clang-x86_64-{clang,lld,llvm,compiler-rt,meson,ninja,python,nodejs,lldb}`. Build in a CLANG64 shell:
  `meson setup release --buildtype release && ninja -C release`.
- ssh lands in cmd.exe: `ssh HOST 'C:\msys64\usr\bin\env.exe MSYSTEM=CLANG64 CHERE_INVOKING=1 /usr/bin/bash -l -s'`
  with the script on stdin. Pushing: a repo on the VM with `receive.denyCurrentBranch updateInstead`, and
  `GIT_SSH_COMMAND=ssh git push HOST:C:/tttt --receive-pack='C:/msys64/usr/bin/env.exe PATH=/usr/bin git-receive-pack' win:main`.
- OpenSSH puts a session's processes in a job object it kills at disconnect, `nohup`/`setsid` included: start long
  runs with `Invoke-CimMethod Win32_Process Create` (outside the job). Attaching lldb in `--batch` kills the process
  at detach.
- WER: a crash waits on an invisible dialog; `reg add "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting" /v
  DontShowUI /t REG_DWORD /d 1` (and `Disabled`).
- tsgo for Windows cross-compiles on Linux like the macOS note says, `GOOS=windows GOARCH=amd64`: `tsc.exe`,
  `tttt-oracle.exe`, `tttt-test-mapper.exe`, `tttt-probe.test.exe` into thirdparty/typescript/built.
- Untracked trees: testdata (357 MB) and the corpora shipped as tar | zstd over ssh, extracted with
  `MSYS=winsymlinks:nativestrict` (native symlinks: the ssh session is elevated); `zod/node_modules/zod -> ../packages/zod`
  and `webpack/node_modules/webpack -> ..` become `<SYMLINKD>`, which realpath follows like tsgo's.
- Windows Defender's real-time scan dominates file-heavy work: during the corpora extraction and a suite `--dump` run
  MsMpEng used 2500 s of CPU against the suite's 100. The dev directories are excluded (`Add-MpPreference
  -ExclusionPath C:\tttt,C:\msys64,...`); benchmarks measure both ways.

## Scripts
Work on Windows (MSYS2 CLANG64 bash): pgobuild.sh and fastbuild.sh (fast/tttt.exe is a copy of pgo/tttt.exe),
`release/baseline_tests.exe` directly, scripts/wincompare.py (tsgo against tttt on the corpora, and tsgo against its
Linux output cached in test/corpora.tsc.jsonl). Linux-only: everything using valgrind, perf, bwrap, `resource`
(tttt_run.py's RLIMIT_AS, so realworld/clidiff/sweeps), BOLT, fasttest.sh (ensure-typescript.sh wants Go).

## Benchmark (2026-10-30, branch win at 725d600c7; a 16 vCPU / 64 GB x86-64 cloud VM, Windows Server 2025)
fast/tttt.exe (clang LTO + PGO) against tsgo 7.1.0-beta (windows/amd64, cross-compiled) and `bun check` 1.4.3
(@oven/bun-windows-x64), `scripts/winbench.py -n 5 --single`: median of 5 interleaved runs after a warm-up; wall s /
CPU s / peak working set MB (max). "1T": tttt and tsgo `--singleThreaded`, bun `--threads=1`. Corpora copied to a
directory of their own (C:\bench), binaries there too. Defender's real-time protection on (the default):

| corpus | tttt | tsgo | bun | tttt 1T | tsgo 1T | bun 1T |
|---|---|---|---|---|---|---|
| tsc-compiler | 0.469 / 1.47 / 261 | 1.426 / 5.12 / 417 | 0.901 / 4.02 / 309 | 1.180 / 1.17 / 229 | 3.107 / 3.62 / 368 | 2.123 / 2.11 / 211 |
| zod | 0.430 / 2.66 / 461 | 2.357 / 11.14 / 1047 | 0.802 / 6.34 / 448 | 1.466 / 1.45 / 377 | 5.953 / 8.34 / 776 | 3.446 / 3.42 / 385 |
| drizzle | 0.496 / 3.78 / 535 | 3.360 / 16.78 / 1248 | 0.989 / 9.17 / 533 | 1.925 / 1.91 / 425 | 6.276 / 8.72 / 725 | 4.059 / 4.05 / 369 |
| effect | 0.627 / 4.73 / 696 | 3.874 / 16.94 / 1175 | 1.261 / 14.55 / 444 | 3.010 / 3.02 / 609 | 9.783 / 12.31 / 1017 | 8.210 / 8.22 / 378 |
| excalidraw | 0.417 / 3.17 / 448 | 3.183 / 17.19 / 1446 | 0.749 / 7.72 / 383 | 2.019 / 2.02 / 381 | 6.236 / 7.80 / 734 | 3.731 / 3.72 / 362 |
| webpack | 2.293 / 5.78 / 616 | 2.726 / 12.44 / 935 | 1.297 / 9.83 / 476 | 2.667 / 2.67 / 527 | 7.113 / 8.59 / 790 | 5.304 / 5.30 / 381 |
| vscode | 3.988 / 49.16 / 4577 | 17.014 / 90.28 / 6046 | 6.343 / 79.59 / 2029 | 27.371 / 27.25 / 4141 | 59.440 / 73.17 / 5405 | 50.624 / 50.69 / 1530 |

With C:\bench excluded from Defender, the same runs: tsc-compiler 0.458 (tsgo 1.409, bun 0.889), zod 0.414 (2.280,
0.763), drizzle 0.462 (3.257, 0.944), effect 0.595 (3.795, 1.200), excalidraw 0.397 (3.155, 0.738), webpack 2.251
(2.657, 1.283), vscode 3.847 (17.195, 6.172) at the defaults; 1T within 1-3% of the table. So on warm runs the scan
costs tttt 2-7% and every tool about the same: Defender caches a file's verdict, and what it costs is on files written
or first read (the corpora's extraction: 2500 s of MsMpEng CPU), which a benchmark of repeated runs doesn't see.

At the defaults tttt is 3.0-7.6x faster than tsgo and 1.5-2.0x faster than bun, except webpack (1.2x tsgo, bun 1.8x
ahead: the one-checker rerun, as on Linux and macOS). Single-threaded: 2.2-4.1x tsgo, 1.8-2.7x bun.
Peak working set is above bun's on effect, excalidraw, webpack and vscode (vscode 4.6 GB against 2.0). Absolute times
are ~5x the macOS VM's for every tool alike (bun 1T zod 3.45 s here, 0.585 there): the box, not the platform.

PGO: pgo/tttt.exe against release/tttt.exe (plain -O3), median of 5: 1T zod 1.448 / 1.651 s, drizzle 1.888 / 2.102,
effect 3.006 / 3.441, tsc 1.179 / 1.357 (-10 to -13%); defaults zod 0.411 / 0.453, drizzle 0.474 / 0.510, effect
0.602 / 0.675, tsc 0.461 / 0.530 (-7 to -13%). The PGO build takes 8 min on the VM (two full-LTO links of ~2 min each,
lld LTO being serial). pgo/tttt.exe's output matched tsgo on the same 14 corpora.
