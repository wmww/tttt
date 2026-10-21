# Plan 90: the third coordinator session (written 2026-10-22/23 by the second coordinator)

You are the coordinator of tttt. **Your mandate, from the human (2026-10-22): get the shared graph (`--shared-graph` /
`TTTT_SHARED_GRAPH=1`, plan 86 stage 4f) fully working and turned on by default.** Be economical: the budget is thin;
correctness can't be sacrificed, but shared-graph features with diminishing perf rewards go to later windows. Read
this file, then CLAUDE.md, then plan 89 §1 (the workflow: it still holds, verbatim) and §4 (lessons), then plan 86's
"Stage 4f" section with its progress notes and the "Stage 4f: done" write-up, then `agents/vision.md`.

## 1. What the second session did (2026-10-21/22, ~70 picks; `git log de94e1179..`)
- **Design (plan 86 "Stage 4f")**: the publish-when-clean rule enforced for lazy fields on shared types by a hybrid of
  the two designs plan 89 proposed: every lazily computed value on a shared object is one write-once `SharedWord`
  (null -> clean value | NONE | POISON, CAS release / acquire loads, typed accessors), in-flight state lives only in
  the computing worker (a per-worker claim stack and an Overlay for private values), no worker ever waits for or sees
  another's in-flight state, a race loser adopts a clean winner, a tainted loser is "shadowed" until its file ends.
  Costed on the real code (B's per-worker tables 4-6x the hot-path cost, loser identity leaks); identities publish at
  creation whatever their taint; "clean" defined precisely (plan 86 "Clean, precisely", rule 5 tightened: a peek that
  selects a result is a read of in-flight state).
- **Build-out**: S0 primitives (`src/ty/overlay.{h,c}`, taint always on with the flag, deep-nesting cuts taint); S1
  Signature's resolved state behind one pointer (`SigResolved`); P1 members.c (prop types, signature resolution/return/
  predicate, constraints, members, mapped templates...), P2 checker/lower/function (function returns, declared types,
  reduced, deferred args, branches, implied constraints, type directory, TSan C), P3 relate/graph/memos (memo kinds
  publish only clean entries, variance groups without waiting, declaration memos as words, relation cache rule);
  CTX contextual signatures settled from the node on demand (tsc's way; also 5 default-path TS7006/TS7031 fixes);
  CONDP one evaluation path for conditional instances + intersections keyed tsgo's way (type-fest -8%, drizzle -3%
  instructions) + the variance-barrier fix for base constraints; IDENT function-expression instances keyed by syntactic
  outer type parameters; PERF the flag's K1 overhead +14.6% -> +4% (claims as a stack, no publication while one
  checker is alone); D the deletions (wait protocol, wait-for graph, redo, LAZY_* states, lib warm-up, dead knobs),
  three rule holes fixed (tainted private memos, in-flight memo placeholders, late module-clone bit), the end-state gates.
- **Gate tooling** (scripts/sharedgraph/README.md): `tsan.sh` (every corpus, groups by function pair), the recheck
  gate (`TTTT_GRAPH_RECHECK=1|private`), the fingerprint-at-publish oracle (`fingerprint.sh`, every published kind,
  clean results only), the seeded deterministic scheduler (`TTTT_GRAPH_SEED`, `seeds.sh`): a failing schedule is a
  seed, named within an hour. Also: dtsdiff's history dependence fixed (vscode 9929/9929), the debug build repaired,
  checkerdiff/threadcheck usable from worktrees.
## 2. Where it stands (ai-main after D's picks, 2026-10-23; measurements.md "Plan 86 stage 4f")
- Default path: suite 13962 / 13996 (0 lost, +4 tests), corpora 0/0 (vue 0/2), DT 8595/8595, source sweep sample
  1272/1274 (two old leftovers), dtsdiff 1327/1350; instructions vs the session start: drizzle lower, type-fest -6%,
  the rest within ~1% PGO (nest +6.8% PGO with a flat plain build: a profile/layout effect, see the merger's note).
- With the flag at K12: 20/20 tables on drizzle/zod/effect/excalidraw/nest; 300 loaded drizzle runs 0 differ; effect
  1/100 (the Schema.tst.ts cascade); vscode 5/5; seeds drizzle 191/200 (query.ts(139) TS2322, residue C), effect
  300/300, others 100/100; fingerprint 0 on zod/effect/nest, drizzle 1 (union-order tie-break); TSan 14 corpora T4/T12
  clean; recheck drizzle 3/519 (warm shared values with cold variances: not the plain recycle class); kdiff flag 8/3814
  vs plain 3. Work K12/K1 drizzle 1.305, zod 1.285, effect 1.134 (vs plain K1 ~1.35; today's defaults 2.3-4.4x). Wall
  K12 best-of-7 flag vs defaults: drizzle 0.239/0.309 s, zod 0.211/0.241, effect 0.355/0.336 (Schema.ts 167 vs 84 ms:
  per-instruction cost on the critical path). Flag K1 cost: drizzle +4.1%, zod +4.3%, nest +0.7% instructions.
  Memory: not re-measured this session (G2: vscode ~2.75 GB at K12 vs 1.55; the graph budget recycle exists).
## 3. The work of your window, in order
1. **The three residue mechanisms** (plan 86 "Stage 4f: done", residues; each about a session; one Opus worker each,
   in parallel, disjoint files):
   - **V, variance groups' side state** (relate.c/graph.c): an adopter of a published variance group lacks the side
     state the measurement left in the measuring worker (results computed at variance_depth > 0, marker instances);
     trpc x3, dokploy. Proposed: the side state becomes part of what the group publishes, or an adopter re-measures
     when the group left side state ("self-produced side data" rule).
   - **C, conditional/alias instance identity vs resolution** (checker.c/conditional.c/instantiate.c): instances
     publish as identities whatever their taint but carry resolved state; mikro-orm x3. Proposed: the instance
     object is an identity published at creation, its resolution a word published only when clean.
   - **W, type-resident leftovers**: trpc/examples/nuxt and folo's renderer (mechanism open); react-navigation (the
     object-literal half of divergence (2): object_literal_type_mentions()'s peek needs a literal scope, hacks.md);
     fingerprint at K2 T1 names the first leaking publication. Plus the effect Schema.tst.ts(2936,39) cascade (~1% of
     loaded runs; the merger's bimodal effect fingerprint log) and the drizzle seeds' TS2322 (both the C+V family).
   The gate for each: the flag's kdiff set on the source sweep equal to plain's (rxjs, sequelize x2), drizzle seeds
   200/200, effect loaded 100/100, fingerprint silent. V shows in no project today (trpc/dokploy went with the
   signature-return words) but the rule is recorded: fix it when C's work touches the same code or a project shows it.
2. **The release criterion** (plan 89 §3 item 4): the full `scripts/oracles.py` plain and with the flag identical
   (done once at the end state, 8d693aeb5: identical but effect's 21 / 0 cascade once and kdiff 8 vs 3; measurements.md "Oracle totals"; rerun it after the residues), a seeded-run bound (`scripts/sharedgraph/seeds.sh`: thousands
   across drizzle/zod/effect/excalidraw/nest and a source-sweep sample), the 300 loaded drizzle + 100 effect runs,
   TSan zero groups on every corpus at T4/T12, then the human's laptop bench (`scripts/bench.sh --runs 3 --bun PATH
   --tscrs PATH`, with and without `TTTT_SHARED_GRAPH=1`).
3. **Default on**: flip the default, rewrite the defaults' policy (checkers per core, the graph budget
   `TTTT_GRAPH_BUDGET_MB`, memory: vscode ~2.75 GB at K12 vs 1.55 today; the human accepts up to ~3x bun for a clear
   speed win), delete the plain path's state bytes (plan 86 D note: <0.1%) and the plain-only code paths once the
   flag is the only path, re-measure `test/instrcount.txt`, update vision.md's perf bullet with the laptop numbers.
4. **Only if cheap and after 1-3**: PERF's remaining 2% (a single look-and-claim call at the members.c sites), the
   narrower outer-inference event, object literals' and JS function expressions' identity keyed syntactically.
5. Plan 58 first if `npm view typescript dist-tags` shows 7.1.0 stable.

## 4. What the second coordinator learned
- Every converted site exposed a class of per-worker state the taint counter couldn't see (contextual links,
  identity keying by computed sets, memo placeholders, module-clone flags written late). The fix each time was tsc's
  shape: derive it from the node/declaration, never from history. Expect the residues to be the same.
- Workers' measurements of each other's commits disagreed twice: the merger's stale incremental builds, and
  `--keep-going` vs the standard flags. Only `scripts/instrcount.sh --bin` on clean `git archive` builds counts.
- The fingerprint oracle plus the seeded scheduler turned every "flake" into a named key within an hour; TSan's pair
  union over 3 runs is the right standing gate. Keep the merger running all three after every shared-graph pick.
- Five workers plus the merger was the ceiling; the human's budget note came at ~70 picks. Prefer resuming a
  finished worker (context intact) over a fresh one; hand off past ~600k tokens with a plan note.
- The briefs that worked are in the previous coordinator's scratchpad only; the rules that mattered are plan 89 §1's.

## 5. First hour
1. Plan 89 §5's checks (tree clean, worktrees, disk, `npm view typescript dist-tags`).
2. Spawn the merger with plan 89 §1's loop plus this session's cadence: after every shared-graph pick, `tsan.sh
   drizzle effect` (THREADS=12, pair union over 3 runs), `fingerprint.sh 5 zod drizzle effect`, and the double
   `oracles.py` (plain and flag) after each batch; per-pick instruction attribution with clean `git archive` builds only.
3. Spawn one Opus worker for C (checker.c/conditional.c/instantiate.c: the instance object vs its resolution) and one
   for W (trpc nuxt, folo renderer: fingerprint at K2 T1 first). Then the release criterion's runs, then the human's
   laptop bench, then the default flip. Three workers plus the merger at most; the budget is thin.
4. Perf items only after the flip, and only the ones with a measured payoff: effect's per-instruction critical path
   (Schema.ts 2x slower with the flag), the remaining 2% of the flag's K1 cost, memory at the defaults.


## Outcome (2026-10-22)
Superseded by plan 91. The human withdrew the mandate (the shared graph on by default) on 2026-10-21 after the laptop
bench (measurements.md "at 5f2cd699") showed no wall win on the corpora. Of its items: the residues (C, V, W) were not
worked; the release criterion was not met; the flip was not done (the graph stays parked behind `TTTT_SHARED_GRAPH=1`,
plan 86 "Stage 4f: the perf accounting"); the bench was done. The workflow lessons in section 4 still hold.
