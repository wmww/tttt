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
- From human: please claude-stop after triage, before next insights
- From human: continue working on correctness where applicable and perf/memory. If there's no reasonable way to make any progress on perf look at the final tests/more real-world code bases and make sure there's no remaining fixes that make sense
- (2026-10-18, plan 83) One full `scripts/flagflip.py` run (under the oracles' load) had a third xstate/sloppy TS5115
  (test/order.test.ts:62, 'Values', 'StateId'); the cell alone and a second full run had the usual two. Timing-
  dependent output at one K (work stealing decides which checker sees which file first): TS5115's checker dependence
  is tsgo's own (bugs/tsgo_divergences.md), but tttt's K-fixed output shouldn't vary run to run. Worth a few
  `-c xstate -F sloppy` runs under load to see how often.
