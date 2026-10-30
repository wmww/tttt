# Releases: building and packaging (per platform, then npm)

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

## npm

Since v3 tttt is on npm in esbuild's layout (v2 was one Linux x86-64 package, `tttt-ts@2.0.0`, whose `bin` was the
ELF itself; `scripts/npmpack.sh TAG BINARY` still packs that shape).

**Packages.** `tttt-ts` (the wrapper users install) has `optionalDependencies` on `tttt-ts-linux-x64`,
`tttt-ts-linux-arm64`, `tttt-ts-darwin-arm64` and `tttt-ts-win32-x64`, all at exactly its version. Each platform
package has `os`/`cpu` (`libc: ["glibc"]` on Linux) so the package manager installs only the matching one, and holds
the binary (`bin/tttt`, `tttt.exe` on Windows), LICENSE, `licenses/` (TypeScript's Apache-2.0 for the compiled-in
libs, cleat's and STC's MIT) and a short README; no `bin` field (two packages linking `tttt` would clash) and
`preferUnplugged` (Yarn PnP extracts it). The wrapper holds `bin/tttt` (the launcher), `install.js`, LICENSE, README.

**Build.** `scripts/npmpack.sh v3 DIR [OUT]` with `DIR/{linux-x64/tttt,linux-arm64/tttt,darwin-arm64/tttt,
win32-x64/tttt.exe}` (dispatches to `scripts/npm/npmpack-platforms.sh`): checks each binary's architecture
(`file`), the newest glibc symbol version on Linux (<= 2.28, readelf) and the Mach-O minimum macOS (<= 13), writes the
five tarballs and `OUT/publish.sh`. `NPMPACK_FAKE=1` turns the checks into warnings (stand-in binaries).

**Launcher** (`scripts/npm/launcher.js`, packed as `bin/tttt`): `require.resolve`s the platform package's
`package.json`, refuses a version mismatch, spawns the binary with stdio inherited, passes the exit code on and dies of
the child's signal (so the shell sees 128+N); signals sent to the launcher alone are forwarded on Unix. Errors (exit
1): no package for the platform (lists the four, mentions musl and building from source), the platform package
missing (names `--omit=optional`/`--no-optional`), musl (checked with `process.report` only on those error paths:
it costs milliseconds), a version mismatch. `TTTT_BINARY=/path` runs another binary (a source build). Its cost is
node's start-up: `tttt --version` 0.7 ms native, ~25 ms through the launcher (`node -e 0` alone ~23 ms), on the
24-core box under load, 2026-10-29.

**postinstall** (`install.js`, esbuild's `maybeOptimizePackage`): hard-links (or copies) the platform binary to a
temporary name in the wrapper's `bin/` and renames it over `bin/tttt`; npm's `.bin/tttt` is a symlink to that path,
so it then runs the binary with no node. The rename replaces only the directory entry (a store-shared file is never
written). Skipped, leaving the launcher: Windows (npm's `.cmd` shim runs `node bin/tttt`), Yarn 2+ (runs bins
through node; PnP zips), pnpm (its `.bin` entries are shell shims that `exec node <target>`, decided by the shebang
when written: verified that a swapped-in ELF breaks them, so the user agent and a `.pnpm` path component both skip),
a missing or mismatched platform package, a binary that doesn't run here (`--version` fails: too old a glibc).
Never fails the install. esbuild's fallbacks (an `npm install` of the platform package from the postinstall, a direct
registry download) were left out: no network from an install script.

**Who gets the native binary in practice.** npm 12 blocks dependencies' install scripts by default (`allowScripts` in
package.json, `npm approve-scripts tttt-ts`, `--allow-scripts=tttt-ts` for `-g`), pnpm 10 too (and skips anyway),
Yarn 1 runs them (native), Yarn 4 runs them (skipped). So the launcher is the common path; the README says how to
allow the script. Yarn 4.18 also refuses versions younger than a day (`npmMinimalAgeGate` default `1d`): a release
can't be installed with recent Yarn on its first day.

**Tests.** `scripts/npm/npmtest.sh OUT` (67 checks, ~20 s, nothing published, HOME in the scratch dir so no real
cache is touched): a throwaway registry (`scripts/npm/registry.js`, packuments from the tarballs, publish
times in 2020 for Yarn's gate) serves OUT; npm (default, approved, `npm rebuild`, `--ignore-scripts`,
`--omit=optional`, `--no-optional`, `-g --allow-scripts`, `npx tttt`, `npx -y tttt-ts`), pnpm 10 (default,
`onlyBuiltDependencies`, `--ignore-scripts`, `--no-optional`), Yarn 1 (default, `--ignore-scripts`,
`--ignore-optional`), Yarn 4 (node-modules and PnP): which of launcher/native `bin/tttt` became, `--version`, exit 0
and 2, and for native no node `execve` (strace). Through the launcher: stdin/stdout, arguments verbatim, exit 7, a
SIGTERM and a SIGSEGV death (143, 139), a SIGTERM to the launcher reaching the child, unsupported platform (faked
`process.arch`), Intel macOS, musl (faked report), a version mismatch, and the three other platforms' packages (npm
`--os/--cpu` installs exactly that one; the launcher with a faked platform runs its stand-in). Needs a real Linux
x86-64 tttt in the linux-x64 slot. Not tested: bun (not on the box), real Windows/macOS/arm64 hosts.

**Publishing** (the human's; every package and every publish needs a 2FA code): `OUT/publish.sh` lists
`npm publish TGZ --access public` for the four platform packages, then the wrapper, last: a wrapper whose optional
dependencies 404 installs with no binary. The four platform names are new packages: publish them with plain
`npm publish --otp` rather than `npm stage publish` (staging on a new name left the `tttt-ts@0.0.0-stage`
placeholder version in v2, worth `npm deprecate`-ing). After publishing: `npm view tttt-ts optionalDependencies`
and an install in a scratch dir on each platform at hand.
