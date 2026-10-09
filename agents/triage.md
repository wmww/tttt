# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-08 (triage session, todo entry 10 of the twelfth window): scripts moved to `scripts/` (the
human's request); the human's TypeScript 7 request researched and written up as plan 53 (+ 53_findings.md); plans
36-42, 44, 45, 48, 49, 51 consolidated into bugs/, documentation.md and hacks.md and deleted; Opus sub-agents fixed the
vue order dependence (alias variances), discord.js's global-block merge order, unknown compiler options
(TS5023/5025/5093/6369/6114/6258), the root-file "in the program because" chain and TS6305 for `/// <reference
path>`, the lone-`\r` harness split, the mikro-orm spread constraint and several source-sweep keys, then second
tasks (a mutation seed, the kysely perf lead, svelte/react-navigation). The commit message has the numbers;
leftovers are in bugs/include_order.md, harness.md, sourcesweep_checker.md, missing_checks.md, perf_leads.md.

## From plan 53 stage 1 (CLI parity, 2026-10-08)
- `scripts/clidiff.py` (tttt's stdout against `tsc -p --noEmit --pretty false`, byte for byte) finds message text the
  head-message oracles never compared, all checker side, none CLI:
  - **Overload elaboration chains print untruncated types**: tsc's `Overload 2 of 3, '(...): Promise<...>', gave the
    following error.` truncates the signature's types (signatureToString without NoTruncation); tttt prints them whole
    (jose/test, nest/sample/02-gateways). Probably one flag in the overload chain's signature printing.
  - **TS6059/TS6307 lack the "The file is in the program because:" chain** under them: every inclusion reason of the
    file (`Imported via "../x" from file '/abs/a.ts'`, `with packageId '...'`), in tsc's order of discovery
    (socket.io/examples/basic-crud-application/server, nest/integration/file-upload). tttt only keeps
    `SourceFile.included_at`; the root-file chain (`program_explain_root()`) exists already.
- tsgo's **pretty** related information differs from tsc 6's: `a.ts:2:16 - message` on one line, then the code frame
  (tsc 6: location, frame, then the message). tttt prints 6's (the harness's pretty tests); the 7.x switch flips it.
- `--showConfig`, `--explainFiles`, `--watch`, `--init` are refused (exit 1). `--listFiles` prints tttt's own lib paths
  (thirdparty/lib/X.d.ts, not lib.X.d.ts) in tttt's file order: not compared against tsc.
- TS7016 at a package's import of its own name (`@vitest/browser/internal/vendor-types` from inside
  vitest/packages/browser, resolved through its own exports) lacks tsc's TS7058 chain line ("If the '@vitest/browser'
  package actually exposes this module, try adding..."): the self-name resolution leaves `ModuleRef.package_name`
  empty, so `program.c`'s package hint pass skips it (vitest/test/e2e/dts/browser-playwright).
- `function h(v: 0|1|2) { v++; v += 1; }`: tsc reports nothing, tttt TS2322 on `v += 1` (the compound assignment's
  result checked against the declared literal union; tsc assigns `number` to a narrowed-by-assignment reference?).
  Found by the plan 54 stage 1 crash fixes, reachable code too.
- failstats.py counts "pass: all" over its own notion of ran tests (12128 / 13835 on the native suite at plan 54 stage
  1) while the harness says 11860 / 13571: the errors-only tests without any baseline and skipped configurations are
  counted differently. Make failstats read the harness's saved results for its totals.

## From plan 54 stage 2 (oracle tools on tsgo, 2026-10-09)
- tsgo's CLI names a default lib file `bundled:///libs/lib.es5.d.ts` (not relative to the cwd) and sorts it after the
  project's files (`a.ts` before `bundled:...`); tttt prints `../home/.../thirdparty/lib/es5.d.ts` and sorts it first.
  A clidiff mismatch whenever a diagnostic lands in a lib (`type Partial<T> = T;` in a project). `sweep.lib_key`
  accepts both spellings.
- `scripts/tscinstcount.js` (tsc's per-node TS2589 instantiation counter, `VARIANCES=1` per variance measurement) was
  deleted with tsc 6 (plan 54 stage 3). If a TS2589 runaway needs tsc's budget again, port it as a `test/oracle`
  overlay: a patched copy of `internal/checker/checker.go` logging `c.instantiationCount` where it's reset (in a scratch
  branch, never committed; README's "instrument the oracle").
- tsgo 7.1's `-b` exit codes weren't compared (stage 2 moved the non-build CLI to 7.1's: 2 with diagnostics, 1 only
  when noEmitOnError skips the emit). `build()` in main.c still returns 1 for any project with errors.
