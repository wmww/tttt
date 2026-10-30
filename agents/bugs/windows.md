# Windows (branch `win`, agents/notes/windows.md)

- **One file referenced in two cases is loaded twice; TS1149/TS1261 missing.** tsgo on Windows keys files by their
  lower-cased path (case-insensitive host), so `files: ["a.ts", "b.ts"]` with `import "./B"` in a.ts is one file,
  named as first found (`C:/p2/B.ts`), with `a.ts(1,17): error TS1261: Already included file name 'C:/p2/B.ts'
  differs from file name 'C:/p2/b.ts' only in casing.` and its include-reason chain (forceConsistentCasingInFileNames
  is on by default). tttt keys files by exact path (program_key_view()) and loads B.ts and b.ts both (no duplicate
  errors in that probe only because the declarations are module-scoped). The fix: fold program_key_view()'s key the
  way tsgo's ToFileNameLowerCase does when the host is case-insensitive (Windows; the harness's
  `@useCaseSensitiveFileNames: false` too), keep the first-found name for printing, and report tsgo's
  addProcessingDiagnosticsForFileCasing (compiler/filesparser.go, includeprocessor.go). tttt reports neither code on
  any platform: tsgo's case-sensitive hosts report them too, for two loaded files that differ only in case
  (tasksSeenByNameIgnoreCase). Probe: the p2 directory of the Windows probes (two files and an import, above) [open]
- **A tsconfig `include` that lists a file before a directory glob lists it first; tsgo lists the glob's match
  first** (`"include": ["main.ts", "lib/*.ts"]`: tsgo's --listFiles has lib/util.ts before main.ts, tttt main.ts
  first). Found by the Windows probes, Linux too; not investigated [open]
- **The suite runs ~12x slower on Windows than on Linux** (`baseline_tests -f '^a.*'`: 8.9 s against 0.73 s), not
  CPU-bound in the harness's accounting; demand-zero page faults (1.2 us per 4 KB page, no THP) are the suspect for
  the CLI's share. Not profiled (no perf; WPR/xperf would be the tool) [note]
