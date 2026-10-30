# Release binaries

## macOS arm64
`scripts/release/build-macos.sh OUTDIR [COMMIT]` on an Apple Silicon Mac (macos.md's setup: Command Line Tools,
Homebrew meson/ninja/bash/python3, the PGO training corpora drizzle, zod, effect, excalidraw, webpack, plus TypeScript6
for the untrained comparison). COMMIT is the release commit: HEAD may carry later script commits, but src, thirdparty
and meson.build must equal COMMIT's and be unmodified. ~80 s on a 4 vCPU VM.
- What it builds: scripts/pgobuild.sh with `PGO_DIR=dist-pgo` and `--force` (Apple clang LTO + PGO, the same training
  as the fast build) and `dist-release/` (plain -O3, also baseline_tests), both with `-mmacosx-version-min=$MINOS`
  (default 13.0, also exported as MACOSX_DEPLOYMENT_TARGET) and `-Werror=unguarded-availability` (an API newer than the
  minimum used without an availability check fails the build).
- What it checks: LC_BUILD_VERSION minos is the minimum (`vtool -show-build`), the only library is /usr/lib's
  libSystem (`otool -L`), no weak imports (where a symbol newer than the minimum would show), the linker's ad-hoc
  signature verifies, and the PGO binary's whole output and exit code equal dist-release's on the six corpora at the
  defaults and `--checkers 1`. Writes OUTDIR/tttt and build-info.txt (commit, compiler, SDK, minos, flags, sha256).
- The binary isn't reproducible bit for bit: the training runs at the defaults are multi-threaded, so each build's
  profile (and sha256) differs; its output doesn't.
- Verification done by hand for v3 (b95d0f2f6, 2026-10-29): `dist-release/baseline_tests --dump` (all 56712 files)
  byte-identical to Linux x86-64's; the six corpora at the defaults and K1 byte-identical to Linux's after replacing
  the path up to `/thirdparty/corpora/` (effect: 2 lines, the known truncation that counts the absolute path's
  characters); threadcheck and checkerdiff -k 4 / -k 16 on the six: identical. Run the Linux side from a real checkout
  (cwd the main tree): a worktree whose `thirdparty/corpora` is a symlink gives webpack 3115 lines instead of 789
  (its `node_modules/webpack -> ..` then reaches files under two paths).
- Gatekeeper: the binary is ad-hoc signed, not notarized (the human's decision). `spctl -a -t exec` says "rejected".
  With `com.apple.quarantine` set (as a browser download has), it still ran from an ssh shell; a GUI Terminal launch
  couldn't be tested over ssh (osascript waits on an automation prompt). Expect a browser-downloaded binary to be
  blocked when launched from Terminal ("cannot be opened because the developer cannot be verified") until
  `xattr -d com.apple.quarantine tttt`; npm installs don't set the attribute.

## Windows x86-64
`scripts/release/build-windows.sh OUTDIR [COMMIT]` in an MSYS2 CLANG64 shell (windows.md's setup), same COMMIT rule.
~4 min on a 16 vCPU VM (two serial full-LTO links).
- What it builds: scripts/pgobuild.sh with `PGO_DIR=dist-pgo --force` (clang LTO + PGO, static: meson.build's flags)
  and `dist-release/` (plain -O3, also baseline_tests.exe).
- What it checks: imported DLLs are system ones only (today KERNEL32 and the UCRT's api-ms-win-crt-*), the manifest
  resource has `activeCodePage` UTF-8 and `longPathAware`, and PGO = -O3 output on the six corpora at the defaults
  and K1. Writes OUTDIR/tttt.exe, build-info.txt and imports.txt (every imported function by DLL).
- Minimum Windows: Windows 10 (any build) runs it; the UTF-8 process code page needs 1903. The 88 KERNEL32 imports
  are all Windows 7 or older (GetLogicalProcessorInformationEx, K32GetProcessMemoryInfo, GetFinalPathNameByHandleW,
  InitOnceExecuteOnce...); the UCRT is part of Windows 10; winpthreads looks up GetSystemTimePreciseAsFileTime,
  GetTickCount64 and SetThreadDescription at run time with fallbacks. Before 1903 (Windows 10 1809, Server 2019) the
  manifest's activeCodePage is ignored: the process runs in the ANSI code page, so non-ASCII paths and arguments
  would break (ASCII ones work); not tested on such a system. longPathAware also needs the LongPathsEnabled policy.
  PE header OS/subsystem version 6.0 (lld's default).
- Verification done by hand for v3: `dist-release/baseline_tests.exe --dump` byte-identical to Linux's (56712 files);
  `scripts/wincompare.py --tttt OUTDIR/tttt.exe` (and `--checkers 1`): 14 of 15 corpora identical to tsgo's Windows
  build, vue differs as on Linux; the binary copied to a fresh directory ran from cmd.exe and PowerShell with
  PATH=C:\Windows\system32 only, from unrelated directories, on a project under a non-ASCII directory name (printed
  in UTF-8).
- Run long things over ssh in the foreground of one connection (OpenSSH kills the session's job at disconnect).
