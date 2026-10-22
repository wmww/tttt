# tttt

A [TypeScript](https://www.typescriptlang.org/) typechecker written in C. Work in progress: the goal is full
support for the language, then beating typescript-go on speed. It started as
[Tess](https://loginasroot.net/i_tried_to_write_a_typescript_type_checker), a solo project halted in 2025, and is
now developed autonomously by AI agent sessions (see `CLAUDE.md` and `agents/`).

State (targeting TypeScript 7.1): 13440 of 13568 tests of TypeScript 7.1's native test suite pass in full (errors,
symbols and types output identical to tsgo). TypeScript 6's own `src/compiler` (193k lines) and a dozen pinned
real-world projects (vscode, react, zod, effect, vue...) typecheck with the same diagnostics as tsgo (but a handful
missing on webpack, vue and effect), as do 8593 of 8595 DefinitelyTyped packages and 3304 of 3363 projects from 174
open-source repositories. Single-threaded, tttt's check phase is ~5x faster than tsgo's on the median project.

Internal documentation: [documentation.md](documentation.md). Standard library: [cleat](https://codeberg.org/sphi/cleat).

Dependencies: a C23 compiler (GCC 13+ or clang; tested up to GCC 16 and clang 22), meson, ninja, valgrind (debug
scripts), python3, node 22 + npm (corpora). The test scripts fetch the oracle, TypeScript 7.1 (tsgo) at a pinned
nightly commit, into `thirdparty/typescript` along with the Go toolchain it needs (`thirdparty/ensure-typescript.sh`,
which also builds its CLI); `thirdparty/ensure-corpora.sh` and `thirdparty/ensure-sources.sh` fetch the real-world
corpora and the source sweep's repositories.

To build the fastest binary (what the benchmarks measure):
```bash
scripts/fastbuild.sh                  # prints fast/tttt: LTO + GCC PGO, then llvm-bolt layout (~3 min on 4 cores)
fast/tttt path/to/source.ts           # or: fast/tttt -p path/to/project (tsconfig.json), or -b like tsc -b
```
`fast/tttt` is always the current fastest build; `scripts/fastbuild.sh` is the one place that defines what that is
(today `scripts/pgobuild.sh`, trained on a few of the pinned corpora, then `scripts/boltbuild.sh`, which needs
`llvm-bolt`; without it, use `pgo/tttt`). For development, a plain optimized build without the training step:
```bash
meson setup release --buildtype release
ninja -C release
release/tttt path/to/source.ts
```

tttt is meant as a drop-in `tsc` for checking: the same command lines and tsconfigs, the same diagnostics in the same
formats (`--pretty false` byte for byte, the pretty one when the output is a terminal) and exit codes (tsc 7's: 1 with
any error). It checks, and emits only declarations: every run behaves like `tsc --noEmit`, except that a project whose
config has `emitDeclarationOnly` gets its `.d.ts` and `.d.ts.map` files written like `tsc` does (no `.js`, no
`.tsbuildinfo`; `--watch` isn't supported). `--checkers`, `--builders`, `--singleThreaded` and `-q` are
tsgo's; `release/tttt --help` lists the rest.

Scripts useful for development (all rebuild first; `CLAUDE.md` has the full list):
```bash
scripts/run.sh path/to/source.ts            # Build and run in Valgrind
scripts/fasttest.sh --summary               # Release build, whole tsc test suite, what changed vs test/baseline_results.ts
scripts/fasttest.sh -s                      # Save the results as the new baseline
scripts/test.sh -f castingTuple -d          # One test under Valgrind, with the diff against tsc's baselines
scripts/test.sh -f castingTuple --sym --typ # Only the symbols and types diffs; -f takes a whole-name regex
scripts/filetest.sh foo/bar.ts -d           # Compare tttt and tsc on any file (FAST=1 for the release build)
scripts/failstats.py                        # Why tests fail, in aggregate
scripts/realworld.py                        # tttt on tsc's own compiler sources, diagnostics grouped by code
scripts/bench.sh                            # Wall time and memory against tsgo
scripts/diffapp.sh [testName]               # Web app to compare results with tsc
```
