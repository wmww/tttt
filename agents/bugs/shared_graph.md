# --shared-graph (TTTT_SHARED_GRAPH=1): flagged output against plain one checker

Flag-only: the default path has none of these (tsgo's output and plain tttt's are checker-count independent), so the
open ones are tagged `[note]` and don't count in the headline; they block turning the flag on by default. The gate is
`scripts/sgcheck.py` (flagged runs at K 1/4/8/16, the scheduler's deal and a program-order round robin, against plain
K1 byte for byte). Census 2026-10-29 (K8, both deals, 3 runs each, the 43 hub projects and 15 corpora of
scripts/sharedgraph/sgcheck_projects.txt, load 8-15): main at ab39dce20 12 of 348 runs differ in 5 projects
(auth-api 3, folo renderer 2, better-drizzle 5, drizzle 1, effect 1); wt-sg after 30bee8a72 6 runs in 3 projects
(better-drizzle 4, drizzle 1, effect 1), after 756a0b2d4 better-drizzle 0/21.

- **openbot/apps/auth-api: private names raced** (hub_regime.md's 75 TS2345s): ty_private_name() returned its own
  `#x` atom when another checker's was published first; the class's members carried the winner's, so lookups missed
  them (TS18014, TS6133, `unknown`/`{}` through Effect chains). 10/10 runs before, 0/60 after. [fixed 30bee8a72]
- **No one-checker rerun under the flag**: a history-dependent TS2589 stayed whatever the schedule hit
  (better-drizzle src/shared/client/factory.ts(77,19)). The flag reruns now, in a fresh graph. [fixed 756a0b2d4]
- **TSan groups**: the could-contain-type-variables memo in xflags bits (fetch_or against plain reads, five groups)
  and KEY_PROP_WRITE_TYPE's null "none" written into another checker's published block (intern_set()), fixed by
  90735aabd; ty_reduced_slow()'s second KEY_REDUCED_UNION store into a possibly published block, fixed by 3be65fc1d.
  TSan clean on drizzle, effect, excalidraw, zod at T4/T8 after. [fixed 90735aabd]
- **drizzle residue C** (sharing_design.md §4): the Gel/MySql query builders' `execute` chains (TS2430/TS2416 at
  src/gel-core/query-builders/delete.ts(98,18), insert.ts(198,18), update.ts(280,18), mysql2/session.ts(281,17))
  missing or elaborated differently (`Promise<unknown>`). 5/10 K8 runs with the scheduler's deal, 0/30 with the round
  robin; 0/128 seeded schedules (scripts/sharedgraph/seeds.sh, K8, rates 16 and 2): it needs true concurrency. **0/10
  under `TTTT_NESTING_ORDER=content TTTT_FIRST_TOUCH_STAMPS=0`** (the reference K1 run with the same order): the
  mechanism is the first-touch stamps standing in for type ids in is_deeply_nested_as(). A worker stamps a type when
  it first touches it, but a published composite (members, an instantiation) brings many types at once, which plain
  creates in its own order at its own time: the "newer" test, and with it a deep-nesting cut, follows the schedule.
  The cut is a taint event, but what it decides (a relation verdict reported in this file's diagnostics) is the
  worker's own history, not plain K1's. The fingerprint oracle also shows a KEY_UNION_PAIR published from inside a
  variance measurement in expo-sqlite/query.ts (definitely_unrelated()'s permissive check -> compare_signatures ->
  infer_from_type_args -> get_variances) whose cloned `TResult1`'s mapper targets `any` where K1's targets `unknown`
  (9/12 runs, output unaffected there). Fix: the content order (sharing_design.md stage 1; its parity cost is listed
  there), a decision for the coordinator; stamps can't be made a function of the node. [note]
- **effect: Schema.ts's `X.pipe(lowercased(...))` classes** (the known Schema.tst.ts cascade, now (2892,39) TS2345 and
  80-110 lines): 2-3/10 K8 runs with either nesting order, not the never-intersection commits (9967c8e35 2/10,
  73f823ccc 3/10). `TTTT_GRAPH_RECHECK=1` (a fresh checker over the finished graph) reproduces it in 3 of 6 runs:
  Schema.ts(4543,33) TS2507 "Type 'never' is not a constructor function type" for `class Lowercased extends
  String$.pipe(lowercased(...))` and TS2345 `typeof String$` not assignable to `typeof String$ & Schema<A, string,
  never>` ("Types of property 'Type' are incompatible"): a wrong value is *published* (a fresh checker reads it), not
  only seen privately. The fingerprint oracle never reproduces it (0/40: its K1 record pass before the workers moves
  the timing or pre-fills something they race on). Repro: `scripts/sgcheck.py -f '^effect$' -k 8 --deals def -n 10`;
  next step a fingerprint build without the record pass (compare against a stored K1 dump). [note]
- **folo/apps/desktop/layer/renderer** (residue W): data-control.tsx(36,77) and CustomIntegrationSection.tsx(18,3)
  TS2345, `GeneralSettings[K]` with `K extends keyof ReturnType<typeof getSettingsRaw>` read as `never`, 1/6 K8 runs
  with the scheduler's deal (2/3 on main before 30bee8a72, so possibly partly the private-name race), 0/6 round robin.
  Repro: `scripts/sgcheck.py -f 'folo/apps/desktop/layer/renderer' -k 8 -n 6`. [note]
