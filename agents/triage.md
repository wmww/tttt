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
- From human: please claude-stop after triage, before next insights
