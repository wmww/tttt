# Plan 89: the next coordinator session (written 2026-10-21 by the first coordinator)

You are the coordinator of tttt, a TypeScript typechecker in C that aims to beat typescript-go on wall time. This file
is your whole briefing: the workflow, what the project is in the middle of, what to do next and in what order, and what
the first coordinator learned about running it. Read it fully, then `CLAUDE.md`, then `agents/vision.md`; read other
files only when a task below points at them. Don't read `agents/plans/86_multiples.md` end to end (1000+ lines): its
section headings are a table of contents, read the sections named here.

## 1. The workflow (settled with the human on 2026-10-20/21)

**You coordinate; workers code.** Workers are Opus sub-agents (Sonnet for mechanical refactors and long iteration
loops with a clear oracle), each in its own git worktree (`scripts/worktree.sh NAME` makes `/tmp/wt-NAME` on branch
`wt-NAME` with the shared `thirdparty/` checkouts symlinked in). One **merger** agent (Opus) works in the main tree
`/home/ai/tttt` and nobody else does: it cherry-picks the workers' commits onto `ai-main` by hash, builds, runs the
check batch, measures instruction counts, pushes, and reverts or blocks what regresses. You never edit the repo while a
merger is active, and you keep your own context small: you read reports, decide, and write briefs.

**Stay idle.** The human wants to be able to talk to you at any time, so between reports you end your turn. Workers and
the merger run in the background and their completion notifications wake you; agents also message you directly
(`SendMessage`), and you relay between them (they can't reach each other). Never poll in your own context; never run
multi-minute commands yourself. Edit files yourself only when the content is something only you hold (this plan was one
such case); even `agents/vision.md`/`todo.md` refreshes go to a worker with a brief.

**Briefs.** Every worker gets `Read FILE and follow it`, with FILE a brief in your scratchpad: a common rules file plus a
per-task file. The common rules that mattered (put them all in; each one was learned the hard way):
- Work only in your worktree; never edit, build in or run scripts from `/home/ai/tttt`. The symlinked `thirdparty/`
  trees are shared: never run `thirdparty/ensure-*.sh`, `dtsdiff.py --tsc`, `flagflip.py --tsc` from a worktree, and
  no heavy oracles (`oracles.py`, `sweep.py`, `sourcesweep.py`, `perfsweep.py`): the coordinator runs those at merge.
- Never `git stash` (shared by all worktrees). Never `pkill -f PATTERN` with the pattern in the same command line
  (kills your own shell): `pgrep` first, kill by pid.
- **Never `rm` a path built from a shell variable** (`rm -f $S/*.x`): the harness prompts the human. Literal paths or
  `"${S:?}"/...`.
- Commit early, real messages, never amend or rebase an already-made commit (the merger picks by hash; a re-hash is a
  duplicate pick or a conflict), rebase onto `ai-main` before each new commit once other tracks touch the same files.
- Validate: suite (`scripts/fasttest.sh --summary`), the corpora `scripts/realworld.py --corpus X`, byte-identical
  output across checker and thread counts (`checkerdiff.py`, `threadcheck.sh`), instruction counts against the base
  commit built in the same worktree (`scripts/instrcount.sh --bin release/tttt corpus...`; worktree numbers aren't
  comparable with `test/instrcount.txt`).
- The final message is the report (the coordinator sees nothing else): commits with one line each, numbers, what's
  left with file/function names, risks; under ~60 lines. Interim reports at milestones for long tasks (you resume the
  agent with `SendMessage`; it keeps its context).
- The machine (16 cores, shared with another project's agents, load often 10-40) makes wall time noise: workers measure
  instructions (callgrind) and type counts; wall only as best-of-N with the load stated. Real wall numbers come from the
  human's 12-core laptop (`scripts/bench.sh`), when the human runs it.

**The merger's loop** (give it this verbatim; the first one ran 15 hours on it): per cycle, for each active branch
`pick.sh BRANCH` (cherry-pick unpicked commits, skipping hashes and patch-ids already picked, stopping at a conflict or a
"wip" commit); on a conflict, resolve trivially (notes files: keep both; `test/baseline_results.ts`: ours, regenerate)
or abort, block the hash and message the coordinator for the author to rebase; then a check batch in the background
(build, `fasttest.sh -s`, `resultdiff.py <session-start copy> --lost`, four corpora, threadcheck, checkerdiff,
scenarios; ~6 min), poll a DONE marker with bounded loops (never a foreground run over 10 minutes); `git pull --rebase
&& git push`; instruction counts (`scripts/instrcount.sh nest drizzle zod effect tsc`, background) every few cycles or
whenever checker code was picked, with plain release builds of commit vs parent to confirm a PGO delta over ~0.5%
before chasing it; a bounded wait for new commits between cycles; stop after 90 idle minutes. The scripts from the
first session are in the coordinator scratchpad only; have the merger rewrite them (10 minutes) rather than hunting.

**Oracles.** The full `scripts/oracles.py` (~20 min from a worktree at the `ai-main` head, so cycles continue) ran
three times in the first session and each time found or confirmed something the per-cycle checks can't (a memory cap
scaled for a smaller box, a K-dependence, a declaration-emit history dependence). Run it after every ~30 picks of
checker code, and run it twice (plain and `TTTT_SHARED_GRAPH=1`) for anything touching the shared graph; the two logs
side by side are the correctness list.

**Tokens.** A worker session costs 250-900k tokens; the first day used ~5M for ~90 commits. Resuming a finished worker
with a follow-up (its context intact) was consistently cheaper and better than a fresh one for adjacent work; past
~600k context, hand off to a fresh worker with a written resume paragraph in the plan (the 4d worker did this well).
Three to four concurrent workers plus the merger is the right load for this box; the human wants them all efficient,
and prefers fast results at the same total spend over slow ones.

## 2. State of the project (ai-main, 2026-10-21 evening)

Numbers: suite 13958 / 13992, every `-p` corpus 0 / 0 against tsc but vue 0 / 2 (known), DT sweep 8595 / 8595,
`$ExpectType` 68640 / 68640, source sweep 5235 / 5242, `-b` sweep 411 / 416, clidiff 3824 / 3831, dtsdiff 1327 / 1350,
kdiff 4, recycle 4 (`agents/measurements.md` "Oracle totals"). ~88 commits landed in the first coordinator day;
`git log 4eadfcc74..` is the record, every commit message says what and why.

**Plan 86 stage 4, the shared type graph, is built behind `--shared-graph` / `TTTT_SHARED_GRAPH=1`** (`src/ty/graph.c`,
`src/ty/shared_table.c`, `src/ty/atom_table.c`; plan 86 sections "Stage 4d" and "Stage 4e"). What it does: one type
graph (intern tables, node links, declaration memos, atoms, type ids, lib singletons, relation results) shared by all
checkers on real threads; identity kinds publish at creation, memo kinds when their value exists (first wins by CAS),
lazy fields on types are claimed by CAS and other workers wait for them; a graph-wide budget-triggered recycle at file
boundaries (`TTTT_GRAPH_BUDGET_MB`, default 800). Measured (measurements.md "Plan 86 gate G2"): K12 check work 1.10-1.13x
K1 (was 1.4-2.6x); output identical to K1 on every corpus and the whole oracle set (the two differences found were
fixed); CPU below today's defaults on 10/14 corpora; wall equal or better on 11/14; dokploy 4.4x faster on 8x less CPU,
monkeytype 3x; vscode 2.75 GB peak against 1.55 at today's defaults (the human accepts up to ~3x bun's memory for a
clear speed win). It is NOT the default, for the reason in section 3.

**Also done in the day**: plan 86 stages 4a-4c (K1 refactors, every oracle byte-identical: in-flight state in
per-checker records, plan 88 stage B, identity purity, global atoms and ids); stage 3's cheap parts (vscode's load +
name resolution 593 -> ~370 ms at 12 threads here: sharded resolver locks, loader off the stat/free paths, rank-ordered
load pool, parallel declare, word-at-a-time symbol hash; link stays serial by a written design); the IntersectionState
audit (relate.c; one divergence fixed with a test); the relation-cache sharing (4e item 2); the redo's gaps (4e item 4).

**Default path**: untouched in behaviour (byte-identical everywhere) and within budget on instructions (zod +2.5%
against `test/instrcount.txt`, of which +1.2% is a checker-recycle boundary moving after the IntersectionState audit;
the rest under 1.4%). The human cares about speed over memory.

## 3. The decision that shapes the next session: correctness by construction

The shared graph's residual nondeterminism (with its variance lock off, 2-3% of loaded drizzle K12 runs lose an
error; with the lock on, 1 in 43 stress runs on effect) was being chased statistically: run hundreds of times, diff,
hypothesize, rule out. Two sessions found five real bugs that way and stalled on the sixth. **The human's ruling: a
design whose correctness rests on not having seen it fail is not acceptable; once a design has had a couple of races it
will always have one more, and the next one shows up on other hardware (ARM's memory model), other OSes, other code.
Correctness has to come from the design; testing is a regression gate, not the method.**

The design already has a construction-level argument, half enforced. The rule (plan 86 "The design", "The publish
rule"): **a value is published only if its computation was clean** (read no in-flight state, popped no cycle, hit no
limit, saw no unreliable variance). Clean results are pure functions of other published clean results, so whoever
computes one first gets the same answer: the shared graph is confluent regardless of schedule. Tainted results stay
private to the worker that computed them, whose history is then exactly one of today's K private checkers', so the
output is as schedule-independent as today's K checkers (tsgo's own standard: its genuine-cycle results are order
dependent too, the kdiff four). Where it is broken: the rule is enforced for table entries (memos, with the taint
counter) but **not for lazy fields stored on shared types** (members, property types, signature resolution and return
types, constraints, branches...), because a tainted value there has no private home. Every ad-hoc mechanism since (the
graph-wide variance lock, the cycle-break redo of a file, the lib warm-up) patches that hole, and every unexplained
residue has the signature of a value leaking through it (a missing error: a type that came out too permissive).
**Late finding (the lock hunter's last report, 2026-10-21 evening; plan 86 4e item 1 has the details).** The lock's
residue decomposed into two bugs, neither about variance results: (a) the cycle-break redo cut a given-up file's errors
out of the checker's *sorted* error vector, which removed other files' errors (every TS2578 case): fixed on ai-main
(4bdbb37de; a given-up file reports into its own error context), 150/150 loaded drizzle runs with the lock off; (b)
`is_deeply_nested_as()` decides by increasing type id, and under the shared graph ids come out in scheduling order, so
one worker's relation cache got poisoned (`super-T -> sub-T = true` stored from a Maybe frame) and it measured every
variance as bivariant+unreliable from then on: with `TTTT_FIRST_TOUCH_STAMPS=1` (4c's per-worker stamps, +0.7-0.8% K1
instructions) 500/500 runs matched. So stage 4c's "id order is unobservable" was wrong under concurrency, and the
stamps must be on whenever the graph is shared. **Not yet done**: the lock actually removed with stamps on, drizzle and
effect K12 wall against today's defaults, the 20-run tables on the other corpora, vscode, checkerdiff/threadcheck. This
is the first concrete task of the next window (section 3's build-out can start with it), and it does not change the
ruling: the by-construction rule is still what makes the design trustworthy; these two were found by a recheck tool
(every file checked again by one fresh checker after the workers join, errors compared per file), which is a good
candidate for the standing gates in item 3.

Second rule: **zero data races**, hardware-independent: ThreadSanitizer clean on every corpus with the flag; the two
places that trade an acquire load for ~1% ("formally a race, x86 saves us": `ty_members()`/`prop_type()` fast paths,
`SymbolLinks.base_cycle`, hacks.md "shared graph") get fixed properly or eat the cost.

### The next window: plan 86 stage 4f, "by construction" (this is the work; the number of sessions is yours to find out)
1. **Design session (one Opus worker, a day of work at most, no code beyond measurement).** Cost two designs against each
   other on the real code and write the chosen one into plan 86 as "Stage 4f":
   - **A. The overlay**: keep today's claimed-and-waited fields on shared types, but a field computed under taint goes to
     a per-worker overlay (a side table keyed by type id and field, read on the slow path only) and the shared field
     goes back to not-started so the next worker computes it clean. The known cost: a signature's resolution fills a
     dozen fields in place and callers hold the `Signature*`; members hold `Prop`s that other structures point at.
     Price the cloning per kind (members, prop types, signatures, constraints, branches, mapped templates, deferred
     args, declared and return types: the 18 sites plan 86 "Stage 4d" step 3 lists).
   - **B. Immutable shared types, publish at quiescence**: no lazy field lives on a shared type; every lazy field lives
     in per-worker tables (the `LazySlots`/`links` machinery already does this for memo kinds), and a worker publishes
     its clean fields only at quiescent points (file boundaries, where nothing of its is in flight), first wins. No
     worker ever sees another's in-flight state: no waiting, no wait-for graph, no cycle break, no redo, no variance
     lock, no warm-up. Risks to price: lookup cost on the hot `state == DONE` paths (a table probe instead of a field
     read; measure on drizzle/zod at K1 with callgrind), member identity (callers hold `Prop`/`Signature` pointers:
     two workers' private member sets for one shared type have different identities until publication; find every
     identity comparison on members/props/signatures, `documentation.md` "member identity"), and how much sharing is
     lost by publishing per file instead of per computation (track S measured per-share publication at 1.08-1.10x K1
     work, so per-file should be close).
   - Either way the gate for the build-out is: **the variance lock, the redo and the lib warm-up deleted** (the lock
     likely goes first, with first-touch stamps on under the flag, per the late finding above), TSan clean
     on every `-p` corpus with the flag (`documentation.md` "Typecheck" has the TSan build line), the 20-run tables and
     300 loaded drizzle runs identical to K1 (these are now regression gates, not the proof), both full oracle runs
     identical, default path byte-identical and instruction-flat, K12 work still within 1.15x K1, drizzle and effect K12
     wall at or below today's defaults (the lock cost drizzle +54%).
   - The design worker should read: plan 86 "The design" (the publish rule), "Stage 4d" (what's built, step 3's 18
     sites, the resume paragraph at the top of "Stage 4e"), "Stage 4e" item 1 (the lock findings and the ruled-out
     list), `86_stage4_census.md` Part C (bun's "small commits" design is close to B), `src/ty/graph.c`,
     `shared_table.c`, `src/ty/taint.{h,c}` (what "clean" means in code), and hacks.md's "shared graph" entries.
2. **Build-out** (2-4 workers in parallel once the design names disjoint kinds; the merger as always). Each step a
   commit with the default path untouched. Delete the ad-hoc mechanisms as the rule makes them unnecessary; a mechanism
   that can't be deleted is a sign the rule isn't fully enforced yet.
3. **Tooling as gates, built by one worker alongside**: (a) a fingerprint-at-publish oracle in a debug build: record one
   K1 run's structural fingerprint per published key (`taint.c` has the fingerprinting), then abort with a stack trace
   at the first publication in a K12 run whose fingerprint differs; (b) a seeded deterministic scheduler knob (workers
   hand off at publication and wait points in a seeded order) so a failing schedule is a seed, run thousands of seeds on
   idle cores; (c) TSan on the corpora under the flag as a standing merger check. These find violations of the rule;
   they are not the correctness argument.
4. **Release criterion for default-on** (plan 86 4e item 5): the design's gate above, plus the full oracle set identical
   plain vs flag, plus a run-count bound (thousands of seeded runs across corpora and the source sweep sample, zero
   diffs), plus the human's laptop bench (`scripts/bench.sh --runs 3 --bun PATH --tscrs PATH`, with and without
   `TTTT_SHARED_GRAPH=1`; measurements.md says how to get bun canary and tsc-rs) showing the flag faster than bun on
   the corpora where bun led (vscode, drizzle, nest). Only then flip the default and rewrite the defaults' policy.
5. **Standing items, lower priority, fillers for free worker slots**: plan 58 (re-pin to TypeScript 7.1.0 stable when
   `npm view typescript dist-tags` shows it: do it first, ahead of everything, then plan 66 stage 5); the plain-path
   dtsdiff history dependence (vscode's `?: undefined` property order: done 2026-10-21, bugs/dts_emit.md "Member
   order"); `bugs/sourcesweep_checker.md`'s
   cheapest items (real-code correctness); the front end's wall checks on the laptop (`TTTT_LOAD_POOL_STATS=1`,
   `TTTT_RESOLVE_TIMES=1` on vscode) to decide the import pre-scan (plan 86 stage 3 says how); the effect TS2769 stress
   miss and any other residue, which the by-construction design should make moot (verify, don't chase).

## 4. What the first coordinator learned (read this twice)

- **Parallel tracks beat sequential sessions by a lot when the work is decomposable by files.** The ~25-session plan
  86 stage 4 estimate became one day because 4a-4c were ~50 mechanical sites in disjoint kinds, stage 3 lived in the
  loader and binder, and the spike (sequential sharing) could run next to the refactors. Split by *files and kinds*,
  never by diagnostic codes; tell each worker what the others own, and relay ownership changes immediately.
- **A spike that becomes the skeleton is worth more than a throwaway.** Track S (sequential `--shared-graph`) answered
  the plan's central uncertainty in a few hours and its code became 4d. Prefer spikes whose code is on the critical
  path.
- **Workers' judgment is good; their verification is the gap.** Every worker passed its own gates, and the merger's
  instruction counts, the full oracle runs and the cross-flag oracle diff still found things (a +1% always-on counter,
  a memory cap, two K-dependences, a dtsdiff history dependence). Keep the merger's measurements and the periodic full
  oracle runs non-negotiable; budget them.
- **Decide fast on design questions, and give reasons.** Workers asked for decisions five or six times (sharing order,
  wait vs compute-privately, overlay vs experiment, lock vs move on, recycle design). Each time a one-paragraph answer
  with the reasoning let them continue within minutes. Ask them to propose; you choose.
- **A worker that can't find a mechanism after one session won't find it in the second.** Stop and change the method
  (tooling, design) rather than funding hypothesis rounds. This is also the human's position (section 3).
- **"Fewer flakes" is not a fix.** Any change that makes a nondeterminism rarer without a named mechanism is noise;
  require the mechanism in the plan (the window rules in todo.md say so).
- **Protect the human's attention.** Shell hygiene in the brief (the `rm $VAR` prompt interrupted the human once), the
  idle coordinator model (the human wants to chat any time), and status on request in a few lines with the numbers.
- **Memory of the project is in `agents/`, not in agents.** Resume paragraphs in the plan at every milestone made two
  worker hand-offs seamless; a 900k-context worker is a liability without them. Keep `vision.md` and `todo.md` current
  via a docs worker at each milestone (brief with the facts; it will catch what you got wrong).
- **The human's priorities, stated**: speed over memory (1.5-3x bun's memory is fine for a clear speed win); tokens
  matter but fast results at the same spend are much better than slow ones; Opus minimum for judgment work, Sonnet for
  mechanical loops; don't write coordinator procedures into durable project files beyond CLAUDE.md's line (this plan is
  the exception, by request); correctness by construction over catching races.

## 5. First hour checklist for the next coordinator
1. `git -C /home/ai/tttt status` clean, `git worktree list` only the repo, `df -h /` (the box filled once; ~55 GB were
   free at hand-off; `~/osis-targets` grows fast and is not ours), `npm view typescript dist-tags` (plan 58?).
2. Spawn the merger with section 1's loop (it rewrites the pick/check scripts first), then the stage 4f design worker
   (section 3 item 1) and, in parallel, the tooling worker (item 3) and one filler (item 5). Four agents, no more.
3. Write a small memory note for yourself if your harness has one; the first coordinator's notes are in
   `~/.claude/projects/-home-ai-tttt/memory/` (coordinator-workflow-preferences.md, parallel-subagent-merging.md).
4. Then go idle and let the reports come to you.

## Outcome (2026-10-23)
- Stage 4f was designed, built and its deletions landed (~70 picks, `git log de94e1179..`): write-once shared words,
  private overlays, no waiting; plan 86 "Stage 4f: done" is the write-up, with the residues (C, V, W).
- The numbers are in measurements.md "Plan 86 stage 4f: the end state" and "Oracle totals (2026-10-23)"; vision.md
  "Course correction (2026-10-23)" has the decisions.
- The flag stays off by default: the release criterion of section 3 item 4 is not met (residues C/V/W, the human's
  laptop bench). Section 1's workflow and section 4's lessons held; the sections on stage 4f's design are history.
- **Superseded by `agents/plans/90_next_coordinator.md`**, the next coordinator's briefing.
