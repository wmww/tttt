# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-18 (triage session, todo entry 9 of the nineteenth window). The five items: deferral restarts
(inbox-zero's apps/web and 17 more source sweep projects; none left) fixed along with a signature suggestion bug they
hid; the symlinked-directory double load is package ids (exports maps now give one; a HACK covers the ordering, plan 84
the real fix); tttt-oracle neither reads nor writes build info any more (851 stray `.tsbuildinfo` files deleted from
thirdparty/sources); TS5115/TS2589's checker dependence is tsgo's own (bugs/tsgo_divergences.md); the harness's leak
list is empty again (materialized bodies walked before their drop under the counting allocator). inbox-zero's remaining
gap to bun is per-checker re-derivation (bugs/perf_leads.md).

- (2026-10-18, todo entry 1) `--checker-memory 0` now really recycles after every file (it hadn't for small programs
  since the warm-up rule), and `scripts/oracles.py`'s recycle line went 1 -> 4 / 3566: inbox-zero/apps/web (known,
  the undefinedProperties order), sequelize and sequelize/packages/core (TS7022 on `models`,
  sequelize-typescript.ts:335: tsgo's own output differs by checker count there, CLAUDE.md), dokploy/apps/dokploy
  (didn't reproduce on a rerun: flaky). All the same with single-body re-materialization off. Worth a look whether
  dokploy's is a real order dependence (`scripts/kdiff.py -k 1 -m 0 -f dokploy`, a few runs).
- (2026-10-18, human's laptop) **Message text is a blind spot of the "saturated" oracles.** The corpora, both sweeps
  and flagflip compare (file, line, code) only; the text (elaboration chains, printed types) is checked byte for byte
  only by the suite's errors component and `scripts/clidiff.py`, which isn't in `scripts/oracles.py` and was last run
  as `--sample 4` (827 / 839, 2026-10-15). `clidiff --corpora` now: 11 / 14, webpack (TS2322 at
  NormalModuleFactory.js:658: tsc prints `SyncBailHook<ParserOptions | [EmptyParserOptions], ...>`, tttt drops the
  `ParserOptions |` member) and drizzle (tsc's "types returned by 'with(...).update(...).set(...)'", tttt's one call
  deeper, `.$dynamic()`) text, vue the known 0 / 2. The human was told correctness is saturated and was surprised by
  these: add a full `clidiff.py` line to oracles.py (or a text-level count to the sweep caches), report it alongside
  the others, and write the two up / fix them.
- (2026-10-18) tsc-rs 0.7.0 (pingdotgg/ts-rust, Rust port of tsgo at our very pin) is a new comparator: least CPU at
  4 checkers, byte-identical to tsgo on 13 / 14 corpora, but 3-5 GB on vscode/type-fest. `scripts/bench.sh --tscrs`.
  The next insights should decide whether report.md tracks it next to bun.
- From human: continue working on correctness where applicable and perf/memory. If there's no reasonable way to make any progress on perf look at the final tests/more real-world code bases and make sure there's no remaining fixes that make sense
- (2026-10-18, plan 83) One full `scripts/flagflip.py` run (under the oracles' load) had a third xstate/sloppy TS5115
  (test/order.test.ts:62, 'Values', 'StateId'); the cell alone and a second full run had the usual two. Timing-
  dependent output at one K (work stealing decides which checker sees which file first): TS5115's checker dependence
  is tsgo's own (bugs/tsgo_divergences.md), but tttt's K-fixed output shouldn't vary run to run. Worth a few
  `-c xstate -F sloppy` runs under load to see how often. Seen again 2026-10-18 (todo entry 6, agent D): flagflip 18 / 9 / 3, the extra spurious an xstate
  TS5115 whose count varied between runs on the same binary.
- From human: fan out to parallel sub-agents in worktrees where possible, makes things go faster
- types baseline: a reused node's `import("./routers")` printed `import("./routers.js")` in hover-style types output (`>mk : () => ... | ((s: string) => import("./routers").Action)`, tsgo keeps the node's specifier without .js for the reused return annotation of a function type in a union); found by agent C 2026-10-18 writing ttttAliasBodyMappedTypeIdentity
- (2026-10-18, todo entry 6) **dtsdiff's tail is now mostly tsgo's checker split.** After the fan-out (1206+ / 1233),
  the biggest bucket left is member order (11 projects: the per-checker undefinedProperties cache, incl. vscode's
  `readonly id?: undefined`, the readonly copied from whichever declaration made the symbol), and three agents
  independently found single files that match `tsgo --checkers 1`/`--singleThreaded` but not tsgo's default 4
  checkers (babel-parser's inferred predicate, sequelize's `models: any`, mcp-sdk's hono specifier). tsgo 7.1 assigns
  files to checkers by weighted FENNEL partitioning (compiler/checkerpool.go), so emulating it means porting that and
  matching per-checker check order: not worth it. Decide: have dtsdiff (and maybe the sweeps) also cache tsgo's
  `--singleThreaded` output and report "matches tsgo single-threaded" as its own verdict, so the real tail is visible.
- (2026-10-18, todo entry 7, refresh 2) **dtsdiff on the new checkJs projects, uninvestigated**: rocket 264 / 316
  files, eslint-plugin-jsdoc 319 / 338, markdown-magic 124 / 131 and its packages/core 64 / 80, block-transformer
  0 / 1 (JavaScript declaration emit, plan 64 stage 6 territory: `.d.ts.map` mappings missing at names and line ends,
  "type text", "comment", "member order", markdown-magic's 10 extra files where tsgo reports a declaration diagnostic).
  `scripts/dtsdiff.py -f '^(rocket|eslint-plugin-jsdoc|markdown-magic)'` then `--show`. Worth a fan-out bucket once
  the TypeScript tail is thin. Also: 3 refresh repos were dropped for their installs (repos.txt comments); the
  installer runs yarn 1 flags when a berry lockfile has no `packageManager` (algoliasearch), fixable in
  thirdparty/ensure-sources.sh's install_command() by sniffing the lockfile's `__metadata`.
