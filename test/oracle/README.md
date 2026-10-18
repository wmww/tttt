# test/oracle: tttt's Go code inside tsgo (plan 54)
The oracle is tsgo (TypeScript 7.1, `thirdparty/typescript`, pinned by `thirdparty/ensure-typescript.sh`). Where a
script needs more than tsgo's CLI, it runs Go code of ours compiled *inside* tsgo's module, so it can use
`internal/...` packages: `go build -overlay` maps these files into the checkout at build time (the checkout itself is
never modified). `ensure-typescript.sh` writes the overlay map (`typescript/built/overlay.json`, its `OVERLAY` list
says where each file goes) and rebuilds both tools whenever a file here is newer than them.

| file | goes to | builds |
|---|---|---|
| `testdata.go` | `internal/repo/tttt_testdata.go` | (both) `TTTT_TESTDATA=DIR` moves the harness's test data root to DIR |
| `probe_test.go` | `internal/testrunner/tttt_probe_test.go` | `typescript/built/tttt-probe.test` |
| `tttt-oracle/main.go` | `cmd/tttt-oracle/main.go` | `typescript/built/tttt-oracle` |
| `tttt-test-mapper/main.go`, `stdio.go` | `cmd/tttt-test-mapper/`, `internal/testutil/contentmappertest/tttt_stdio.go` | `typescript/built/tttt-test-mapper` |

- **The probe** (`scripts/tsgoprobe.sh DIR`): tsgo's own compiler test runner on every `DIR/tests/cases/
  {compiler,conformance}/**.ts(x)`, the baselines written to `DIR/baselines/local/SUITE`, byte for byte what the
  native suite's reference baselines are (headers say `tests/cases/SUITE/...` relative to DIR). ~40 ms a file, many
  files per process. Used by `scripts/tscbaseline.sh`, `scripts/filetest.sh`, `scripts/mutate.py` and diffapp
  (`--rerun`: again after each stdin line). Validated: 184/184 baselines of 50 random suite tests identical.
- **tttt-oracle** (`scripts/sweep.py`'s `run_tsc`, used by sweep, sourcesweep, expecttypes, expectprobe): project
  directories on stdin, a JSON line each. Diagnostics are tsgo's command line run in-process (`-p DIR --noEmit
  --pretty false --singleThreaded`, extra arguments passed on) and parsed back: what `tsc -p DIR --noEmit` reports,
  the drop-in definition (option errors stop it; emit-only option errors never appear). `--text` adds the whole output (`text`: scripts/clidiff.py's expectation, kept in the source sweep's cache). Build info is hidden and
  never written (`withoutBuildInfo()`): every project is checked cold, and nothing lands in the shared checkouts (a
  `.tsbuildinfo` there makes a later `tsc -p --noEmit` skip checking: 851 had piled up in thirdparty/sources by
  2026-10-18). `--expect`: DefinitelyTyped's
  `$ExpectType` assertions, the type printed by one checker after checking every non-lib file. Validated: identical
  to `built/tsc` on 30 DT packages and 30 source sweep projects (diagnostics, order, exit codes).

- **tttt-test-mapper** (plan 56): tsgo's suite serves its content mappers in process (contentmappertest's registry,
  by a manifest's `exec[0]`); `tttt-test-mapper NAME` serves the same handler on stdin/stdout, so baseline_tests runs
  a test's mapper as `[tttt-test-mapper, exec...]` against tttt's real host. Also usable as a real mapper by tsgo's
  CLI (`exec: ["/abs/tttt-test-mapper", "compiler-test-mapper"]`), which is how CLI probes compare the two.

To instrument the oracle (what `_tsc.js` patching was for with tsc 6): add a patched copy of a tsgo file to the
`OVERLAY` list (replacing the original) in a scratch branch; never commit an instrumented copy.
