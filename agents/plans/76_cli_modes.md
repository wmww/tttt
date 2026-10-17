# Plan 76: the CLI modes a drop-in tsc still refuses (planned 2026-10-16; **parked by the human 2026-10-16**)
**Parked**: the human answered on 2026-10-16 (report.md): memory first, features (watch, incremental, the cheap modes)
later. Nothing here is scheduled; the measurements and the recommendation stand for when it is reopened.
Written by the insights of 2026-10-16, answered by todo entry 8 (main + two research agents). The human decides scope
(they ruled out JS emit on 2026-10-12; watch and incremental were never asked). **The question for the human is at the
end.** Nothing here is scheduled until they answer, except the "build" rows of the last table.

## State
tttt refuses `--watch`, `--showConfig`, `--explainFiles`, `--init` (exit 1, bugs/harness.md) and neither reads nor
writes `.tsbuildinfo` (plan 64 decision 5), so `--incremental` / `composite` projects are rechecked in full and a
later `tsc -b` rebuilds everything tttt "built". `--diagnostics`, `--extendedDiagnostics`, `--traceResolution`,
`--generateTrace` are accepted and ignored. `--listFiles` and `--listFilesOnly` match tsgo (the latter's library order
fixed this session).

## 1. tttt's full check against tsgo's incremental rebuild (measured)
Method (`/tmp/p76` scripts, gone; easy to redo): each project in a bwrap overlay of its checkout. tsgo
`--incremental --noEmit --tsBuildInfoFile X` cold, then a no-op rerun, then after appending a statement to a file no
other file imports ("body": the file's signature is unchanged, so tsgo rechecks only it), then after appending an
`export const` to the most imported file ("hub": its importers are rechecked). tttt `-p --noEmit` at the defaults
(full check, edits in place). 4-vCPU box, both sides multi-threaded, warm page cache. Seconds:

| project | tsgo full | tsgo incr no-op | tsgo incr body | tsgo incr hub | **tttt full** |
|---|---|---|---|---|---|
| vscode (9945 files) | 16.2 | 5.26 | 5.11 | 22.4 | **6.2** |
| TypeScript6 compiler (body: checker.ts) | 1.21 | 0.16 | 1.22 | 1.03 | **0.29** |
| webpack | 2.25 (cold) | 0.56 | **runaway**: 28 GB, killed at 5 min, twice | - | **0.65** |
| mikro-orm | 6.80 | 0.72 | 0.85 | 7.02 | **3.7** |
| dokploy/apps/dokploy | 7.86 | 1.28 | 3.63 | 7.65 | **3.05** (55.6 before this session's recycling fix) |
| renovate | 5.16 | 1.15 | 1.38 | 5.32 | **1.27** |
| lexical | 3.31 | 0.62 | 0.70 | 5.90 | **0.91** |
| trpc | 2.22 | 0.84 | 1.05 | 4.24 | **0.75** |
| tiptap/demos | 2.35 | 0.41 | 0.50 | 2.78 | **1.32** |
| inbox-zero/apps/web | 2.14 | 0.73 | 0.83 | 2.08 | **1.26** |
| arktype | 2.26 | 0.18 | 0.24 | 0.26 | **1.73** |
| react-navigation | 2.91 | 0.40 | 0.41 | 3.50 | **0.78** |
| monkeytype/frontend | 5.91 | 0.32 | 0.41 | 5.91 | **0.68** |
| vencord | 2.94 | 0.25 | 0.28 | 3.48 | **0.38** |
| apollo-client | 2.15 | 0.39 | 0.45 | 0.50 | **0.55** |
| openai-node | 2.72 | 0.41 | 0.49 | 2.75 | **0.50** |
| gemini-cli | 1.05 | 0.93 | 0.98 | 0.98 | **1.13** |
| soybean-admin | 2.65 | 0.40 | 0.46 | 2.68 | **0.42** |
| openapi-typescript | 2.14 | 0.62 | 0.64 | 0.64 | **0.45** |

- **Hub edits (an export changes): tttt's full check wins everywhere but arktype, apollo and gemini-cli**, mostly by
  3-9x: tsgo's incremental rebuild then costs as much as its full check, or more (signature computation).
- **Body edits (the common edit while typing): tsgo's incremental wins on 12 of 19**, mostly by 1.2-2x (vscode 5.1 vs
  6.2), by 2.6-7x where the program's check time is in a few hot files (tiptap, mikro-orm, arktype). tttt wins on
  TypeScript6 (4x), trpc, openapi, renovate, dokploy, and webpack, where tsgo's rebuild runs away.
- tsgo's no-op floor (load, parse, read the buildinfo, nothing checked) is 0.2-1.3 s, 5.3 s on vscode. tttt's whole
  check is within 1-2x of that floor on most projects.
- A cold `--incremental` build costs tsgo 1.0-2x its plain full check (vscode 28 s vs 16, mikro-orm 13.7 vs 6.8).
- tsgo's `-w` keeps parsed files in memory, so its watch rebuilds are somewhat faster than the "body" column (which
  reparses everything). Not measured.

**Answer: incremental is not a perf feature for tttt today, except on body edits in big or check-concentrated
programs, where a tttt watch with affected-file rechecking (below) would close the gap without a file format.**

## 2. Watch: what it costs in tttt (agent read of src/, `--time` splits)
- vscode at the defaults: parse 11-13%, declare + link + resolve 9-10%, check 75-80%. Libraries' parse + resolve are
  ~50 ms.
- Resolve writes into the AST: each identifier's `symbol`/`target`/`flow`, `Block.scope`, `FunctionImpl` fields,
  LazyType slots, `*_rare_mut()` side structs. One merged binder means a user's `declare global` mutates the
  libraries' symbols. There is no un-resolve pass, and several writes are first-write-only.
- Checkers can't survive a rebuild (types point into the AST). But nothing a file reports depends on its checker's
  history (plans 62 and 67, kdiff `-m 0`), and `TTTT_CHECK_ONLY` already checks a subset of files.
- Teardown exists and is used (`program_destroy()`: `-b` between projects, `baseline_tests`), ~0.7-1.9 s on vscode
  (`TTTT_TEARDOWN=1`). The `lazy_slot_new()` counter (2^30) is never reset: unmeasured how many vscode rebuilds it
  allows.
- tsgo's watch (`execute/watcher.go` 723 lines, `watchmanager/` 514, on its own `fswatch`: fanotify/inotify) keeps
  parsed files by path and mtime, swaps one changed file into the old program, and rechecks only the affected files
  through `incremental.Program`'s signatures.

Stages, if wanted:
- **W1, naive** (inotify, full in-process rerun, the harness's lib-program overlay `program_copy_globals_from()` for
  the libraries): ~500 lines. It saves <1% against a fresh process (teardown costs more than the libraries save). Its
  value is only that `tttt -w` exists. Latency = the full check column.
- **W2, affected files**: keep each file's diagnostics, and per file a signature (the hash of its declaration
  emit, which tttt has: plan 64). Recheck only the changed files and the importers of files whose signature changed,
  in fresh checkers (output-identical by plans 62/67). Load, parse and resolve stay whole-program: vscode ~1.5-2.5 s
  per body edit (an estimate) vs 6.2 today and tsgo's ~5. A few days on top of W1.
- **W3, sub-second on vscode**: keep unchanged files' ASTs and redo resolve only where needed. That needs resolve's
  outputs out of the AST into per-program side tables, and per-file binding. A large refactor (weeks) touching the
  core of name resolution.

## 3. The cheap modes (agent read of tsgo's `execute/` and tttt's main.c)
| mode | tsgo | tttt has | cost | oracle |
|---|---|---|---|---|
| `--init` | `execute/tsc/init.go` 214 lines, a static commented template + the command line's options | option tables | ~250 lines, hours | tsgo's 17 `tsc/*` baselines, byte compare |
| `--showConfig` | `tsoptions/showconfig.go` 263 lines: 4-space JSON, options in declaration order, implied options appended | `is_set[]`, as-written strings; but emit-only options are dropped and `extends` chains aren't kept as JSON | ~600 lines, a day | 21 `tsc/showConfig` baselines + clidiff |
| `--explainFiles` | `Program.ExplainFiles` + `fileInclude.go` 320 + `includeprocessor.go` 186 | `include_reasons`, `include_reason_text()` (TS6xxx chains) | ~400 lines, a day (lib reasons, type reference directives, "is ECMAScript module because") | clidiff on the sweeps, 62 tsbuild baselines |
| `--extendedDiagnostics` format | `statistics.go` 183 | `--time` | ~100 lines | none (timings) |
| `--traceResolution` | ~139 trace sites in `module/resolver.go` | `module_resolution.c` 1907 lines, another step order | days | deterministic, byte-comparable |

## 4. `.tsbuildinfo`
- tsgo's format is one-line JSON (`execute/incremental/buildInfo.go`). File versions are the xxh3-128 hex of the
  text. A signature is the xxh3 of the emitted `.d.ts` plus its declaration diagnostics. **`version` must equal
  `core.Version()` exactly**, else everything is rebuilt.
- **Mixed use is safe today** (tested): tsgo `-b` after tttt `-b` finds no buildinfo and rebuilds everything. One
  wrinkle: tttt rewrites every `.d.ts` even when unchanged (new mtimes), and tsgo then "updates output timestamps".
  If tttt's `.d.ts` differs from tsgo's (dtsdiff 1193 / 1233 projects identical), a later tsgo `-b` on an incremental
  project keeps tttt's file. tsgo itself skips an unchanged `.d.ts` only through its buildinfo (emitfileshandler.go's
  skipDtsOutputOfComposite(): composite projects whose emit signature is the stored one), never by comparing bytes:
  without B1 tttt's always-write is tsgo's behaviour; with B1, skip it the same way (triage 2026-10-16).
- **B1, a buildinfo for `-b` up-to-date checks**: `version`, `root`, `fileNames`, `fileInfos` (xxh3 of non-lib files),
  `options`, `latestChangedDtsFile`, and erroring files as pending. 400-600 lines + a vendored xxhash, 1-2 days. It
  would let `tttt -b` skip up-to-date projects (today it rechecks every project every time; tsgo `-b` on an
  up-to-date solution is near-instant) and let tsgo trust tttt's builds. It is tied to tsgo's exact version string:
  re-pins (plan 58) must update it.
- **B2, full `--incremental` resumption**: diagnostics serialized by tsgo's message keys and arguments (tttt only has
  texts: a structural gap), `referencedMap`, real signatures, `emitSignatures`, the affected-files logic. 3-6k lines,
  weeks. Not worth it given section 1.

## Recommendation
| item | recommendation |
|---|---|
| `--init`, `--showConfig`, `--explainFiles` | **build** (one or two execution sessions; all oracle-able byte for byte) |
| skip writing unchanged `.d.ts` | **build** (triage-sized) |
| `--extendedDiagnostics` / `--diagnostics` format | build with the above if cheap; not oracle-able |
| `--traceResolution`, `--generateTrace` | don't (debugging aids; `--traceResolution` is a port of tsgo's step order) |
| B1 buildinfo for `-b` | **ask the human** (a real `-b` speedup and tsgo interop, but tied to tsgo's version string) |
| B2 full incremental | **don't** (weeks; section 1 shows the full check usually suffices) |
| W1 + W2 watch | **ask the human** (W1 alone is UX parity; W2 is the part that makes tttt's watch faster than tsgo's) |
| W3 | don't now: it reopens name resolution's core for vscode-scale watch latency |

## The question for the human
tttt is a drop-in `tsc` for CI (full check, `-b`, declaration emit) and is faster there. The local loop is different:
people run `tsc -w`, or more often rely on their editor's language service, which tttt doesn't have. Should tttt cover
the local loop? Options: (a) no, tttt is the CI/batch checker: build only the cheap modes; (b) watch W1+W2 and the `-b`
buildinfo B1 (~2 weeks of sessions), which makes tttt's `-w` and `-b` competitive with tsgo's; (c) (b) plus a language
server, a separate, much larger product question.

## Insights recommendation (2026-10-16, after the eighteenth window; asked in report.md)
**(b), staged**: W1 first (a day: `tttt -w` exists, latency = the full check), W2 only if W1 loses to `tsgo -w` on
the body-edit projects of section 1 (measure both before building W2). **B1 as tttt's own file** (`.tttt-buildinfo`
next to where tsgo's would go, or tsgo's path only with a format tsgo rejects): the up-to-date skip for `-b` without
claiming tsgo's version string, so tsgo never trusts a `.d.ts` tttt wrote. B2 and an LSP: no. Nothing of (b) is
scheduled until the human answers (triage.md); the cheap modes of section 3 are (todo entry 3).
