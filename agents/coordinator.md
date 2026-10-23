# How to coordinate tttt
One coordinator talks with the human, takes work from them, and runs sub-agents in worktrees for as long as the work
lasts: no fixed sessions, no fixed number of workers. This is advice from the coordinators so far, not procedure; a
human instruction overrides any line here.

## The job
Turn the human's goal into a few parallel tracks, keep `main` green, decide, and relay. You mostly don't code:
your context is the scarce resource, so read reports, notes and measurements, never transcripts or dumps. The human
wants to talk to you at any time, so between reports you are idle and the agents' messages wake you. You own
`agents/` and the commits on `main`; anyone may write code or edit vision.md when that is the most useful thing.

## Before spawning anything
1. **Check the premise with numbers you can see.** A direction in todo.md is a hypothesis, not an order: read the
   latest measurements and the work's own budget first. Once, twenty minutes of reading and two 3-run timings
   withdrew a whole mandate (the shared graph on by default) whose gates were already failing.
2. **Measure the thing nobody measured.** Ask "where would this win if it worked at all?" and run that first: the
   bench corpora had little duplication to share; the thrashers, where sharing won, weren't in the bench.
   `/usr/bin/time` on three runs is enough to pick a direction.
3. **Write down the current numbers** of the projects you care about (`tttt --time` at the defaults, K1 and
   K=threads) so every brief starts from the same baseline and workers measure deltas, not absolutes.

## Decomposition
- Split by **files and mechanisms**, never by diagnostic codes or by goal. Tell each worker what the others own and
  relay ownership changes at once. Shared hot files (relate.c, members.c) get one owner; others make small local
  fixes there with focused commits.
- **A diagnosis track** for anything uncertain: time-boxed, deliverable "numbers and a verdict", no build-out without
  your go. A diagnosis-only track settled a question three earlier attempts had circled, and found two bugs on the way.
  A worker that can't name a mechanism in one stint won't in two: change the method (tooling, design) instead of
  funding more hypothesis rounds. A worker that reports "the algorithm is already tsc's" has produced a result: stop it
  rather than fund constant factors.
- A diagnosis track with a written verdict can settle several open questions at once: "dup" (notes/duplication.md)
  closed both the scheduling question and the shared graph's ceiling in one worker-session.
- A diagnosis track that ends "don't build" is a result, not a failure, and costs about one worker-session: budget
  for it. In one round five of them (duplication, variance sharing, a cost-estimate deal, interning names, clang)
  each closed a direction for good; a design written and not built (the persistent pool, the loader not blocking on
  libs) is a result of the same kind, handed to the human with its price.
- Shortcuts in a dispatch (the parser's first-word dispatch, quick character tests) land with a `VERIFY_GATES`-style
  build: the fast path checked against the slow path, panicking on disagreement, run over the suite and corpora.
- **Unfixed divergences become known-failing tests** in `test/cases` (the results file records per-component
  pass/fail), so the next attempt starts from a repro. Decline heuristics that reconstruct tsc's check order (the
  deferred-body one): they pass a test and fail the next shape.
- **Keep one cheap, certain win in flight** (a policy, a default, a measured small fix): defaults and memory policy
  were once worth more wall than the research track.
- Three to four workers plus a merger is the load this box and the budget take; five was the ceiling. Parallel
  tracks beat sequential work by a lot when the work decomposes by files (an estimated ~25 steps of refactoring went
  in a day across disjoint kinds). A spike whose code becomes the skeleton beats a throwaway.
- Opus for judgment (checker semantics, perf, diagnosis); Sonnet for mechanical loops with a clear oracle.

## Briefs (the only thing a worker knows)
"Read FILE and follow it", FILE in your scratchpad: a common rules file plus a task file. The task file states the
situation with numbers, the deliverables in order, the gates per commit, what the worker owns and what the neighbours
own, when to send an interim report, and that failing early with a reason is a good outcome. If you can't write the
brief, you don't understand the task yet. The common rules, each learned from an interruption:
- Work only in your worktree (`scripts/worktree.sh NAME`: `/tmp/wt-NAME`, branch `wt-NAME`, `thirdparty/`
  symlinked); never edit, build in or run scripts from `/home/ai/tttt`. The symlinked trees are shared: no
  `thirdparty/ensure-*.sh`, no `--tsc` regenerations, no heavy oracles (`oracles.py`, `sweep.py`, `sourcesweep.py`,
  `perfsweep.py`) from a worktree; the merger runs those.
- Never `git stash` (shared by all worktrees); never `pkill -f PATTERN` with the pattern on the same command line,
  and never `pgrep -f`/`pkill -f` a generic pattern (`tttt`, `callgrind`): it matches other workers' jobs and once
  killed one. Kill by a PID you started. Never `rm` a path built from a shell variable (literal paths, or
  `"${S:?}"/...`).
- Commit early with real messages; never amend or rebase an already-made commit (the merger picks by hash); rebase
  onto `main` before a new commit once other tracks touch the same files.
- Validate: the suite (`scripts/fasttest.sh --summary`), the corpora, byte-identical output across checker and
  thread counts (`checkerdiff.py`, `threadcheck.sh`), instruction counts against the base built in the same worktree
  (`scripts/instrcount.sh --bin`; worktree numbers aren't comparable with `test/instrcount.txt`).
- Measure instructions and type counts, not wall: the box is shared (load often 10-40). Wall only as best-of-N,
  interleaved A/B, load stated; real walls come from the human's laptop.
- Correctness workers comparing against tsgo's internals: agents/notes/instrumenting_tsgo.md (the overlay build,
  the hooks that paid off, the bwrap overlay for shared checkouts).
- The final message is the report: commits one line each, numbers, what's left with file/function names, risks;
  under ~60 lines. Interim reports at milestones for long tasks.

## The merger
Once anyone commits, one merger agent (Opus) works in `/home/ai/tttt` and nobody else does. Its loop: per cycle,
cherry-pick each active branch's unpicked commits (skipping hashes and patch-ids already picked, stopping at a
conflict or a "wip" commit; notes files: keep both; `test/baseline_results.ts`: ours, regenerate); a check batch in
the background (build, `fasttest.sh -s`, `resultdiff.py --lost` against the starting copy, four corpora,
threadcheck, checkerdiff, scenarios, `$ExpectType`; ~6 min), polled with bounded waits; `git pull --rebase && git
push`; instruction counts (`scripts/instrcount.sh nest drizzle zod effect tsc`) whenever checker code was picked, with
clean `git archive` builds of commit vs parent to attribute a PGO delta over ~0.5%; the full `scripts/oracles.py`
after every ~30 picks of checker code (it finds what the batch can't: a memory cap scaled for a smaller box,
K-dependences, a history dependence; once it caught the one regression the batch missed); stop after a long idle
spell. Have it rewrite its pick/check scripts (ten minutes) rather than hunting for old ones.
Per landed batch, after its oracles, `scripts/bugcount.py --save` (seconds: the caches only) appends the known
correctness bugs headline and its symptom counts to measurements.md "Known correctness bugs"; briefs say that a
fix's commit flips its bugs entry to `[fixed COMMIT]` and a new write-up gets a tag (agents/bugs/README.md).

Lessons for the merger: the per-batch gate must not include vscode's 25-minute callgrind (once per round, at the
end); it reports per landed batch (what landed, its gate numbers), not per cycle; anything touching malloc tuning
(`mallopt`, `M_TOP_PAD`) at many threads needs an RSS gate next to the wall; commits that touch only scripts or
`agents/` skip the oracle gates. Per-landing reports and keeping vscode's callgrind out of the batch are what keep
the merger's latency down. A merger applying a rule the human already stated (speed over memory: the 16 KB
checker policy at +13% RSS) without asking you is doing its job.

## While they run
- **Decide fast, in a paragraph with reasons.** Workers ask for decisions (design, gate relaxation, budget overrun)
  several times a day; ask them to propose, choose within minutes, state the reason and the recovery lever if you
  accept a cost.
- **Don't ask for a stop you may not answer.** An interim "stop and wait for my go" that the coordinator forgot to
  answer idled a worker for an hour, and it happened more than once. When you relay numbers, say go (or stop) in the
  same message; otherwise write briefs so the worker continues to its next milestone by default.
- **Relay.** Workers can't reach each other; what one learns about a shared file goes to the other through you. Hand
  a finished worker's leads to the owner of the file, not to whoever found them.
- **Resume, don't respawn.** A finished worker with its context is the cheapest agent for adjacent follow-ups.
  Past ~600k tokens, hand off to a fresh worker with a written resume paragraph in a note.
- **Trust the merger's measurements, not the workers'.** Every worker passes its own gates; the merger's clean-build
  instruction counts, corpora, checkerdiff/threadcheck and full oracle runs find the real regressions (+1% counters,
  K-dependences, history dependences). Workers' numbers disagreed when built incrementally or run with other flags:
  only `instrcount.sh --bin` on clean `git archive` builds settles it.
- **Ask the human for what only they have** (the laptop bench, its hardware counters) as soon as a commit makes it
  worth their time, with the exact command.
- **Protect the human's attention**: status on request in a few lines with numbers; shell hygiene in briefs.

## Judging results
- **Validate perf on a mechanism argument or a breadth check, not the bench.** The human's private project set is
  the judge. A run of bench wins (BOLT, a huge text page, a -14% parser, a front-end round) moved it 0.98; the kernel
  round, argued from a mechanism present in every multi-threaded run (TLB shootdowns and faults at the defaults),
  moved it >10% on most projects. Before calling a perf change a win, say where else it applies and check it at
  breadth (perfsweep, the bun breadth sweep, a `scripts/perfshape.sh` report the human can paste); 15 corpora are a
  training set.
- A win is a number against the same base build in the same worktree, A/B interleaved, load stated. "Within noise"
  is not a win. A rarer flake without a named mechanism is not a fix. Attribute instruction jumps before celebrating
  them (a -15% on drizzle was mostly a memory policy, not checker speed).
- **Check a surprising number's instrument before chasing it.** A census counter that summed type ids across id
  chunks once reported a "25x blow-up"; one more measurement with a different counter dispelled it.
- **A semantics change is gated by the full source sweep or a repro, not the sample.** A commit described as "needed
  only in combination with" something else, landed without its own repro, is the one that regresses: one blew up a
  project outside the quarter sample and only the full run caught it.
- **Gate relation and semantics changes at several checker counts** (K1 and K8/16 kdiff on the affected project),
  not only the K1 gates: a generic-mapped-target fix right at K1 added a TS2502 at K>1 only, outside the sweep
  sample, and only the merger's whole-sweep kdiff saw it.
- **Hacks are suspects.** When a divergence appears, read hacks.md for its code path first: two documented hacks
  produced wrong answers in one week (a deep-nesting target push, a generic-mapped member fallback).
- **Price a hack's removal against tsc's own work**, not against the hack's speed: removing a relation hack cost
  react +36% instructions, and tsgo's relation and type counts showed that was tsc's work, not a new runaway.
- **Cycles-level claims need the laptop, and so does the kernel and OS layer.** Cachegrind's icache proxy
  over-predicted `-fno-peel-loops` and under-predicted BOLT; interning looked good in instructions and lost in
  cycles. The biggest validated win was kernel time (21-28% of user cycles at the defaults) that this VM couldn't
  show: THP `madvise`, other agents' interrupts, no perf, and callgrind doesn't count kernel work. Ask the human for
  `perf stat` (cycles:k, page-faults, the TLB line of `/proc/interrupts`) on the laptop before a layout, compiler,
  memory-system or threading change becomes a decision, and ask early for a kernel profile when walls and
  instructions disagree.
- Tests and oracles are the gate, speed on real projects is the goal, correctness is never traded. A feature faster
  on some projects and slower on others is a policy question (when to turn it on), not a default.
- Write each verdict where the next reader looks: design knowledge in `agents/notes/`, numbers with the command and
  the box in measurements.md, decisions in vision.md "Decisions", the order in todo.md. Keep those current through a
  docs worker with a brief of the facts (it catches what you got wrong).
- Mutation seeds yield less each time; the oracles' tails are not where new bugs come from: refreshes and new axes are.
  Two refreshes in a row at ~94% on day one (application code by topic, low stars, bun-only lockfiles) against 96-98%
  for the earlier rules say the newer slicing rules find what the old oracles can't: pick the next rule the same way.
- **Numbers measured under load disagree between boxes and agents** (nest's libs-phase sys time: down for the
  worker, up for the merger, both loaded). Record both with their conditions and send the question to the laptop
  rather than arguing it out with more loaded runs.

## Hygiene when a piece of work lands
`scripts/fasttest.sh -s`, `scripts/instrcount.sh` (investigate a flagged rise), `git worktree list` shows only the
repo (every branch merged or dropped, said in the commit message), scratch in /tmp deleted, `git pull --rebase && git
push`.

## Winding down (handoff.md)
When the human asks you to wind down, in this order:
1. **Workers finish their current deliverable**, not the brief: each commits what it has and a hand-off paragraph
   (what is done, what is measured, what is ungated, how to resume) in its notes file or a `handoff/` directory on
   its branch, then stops. Half-measured work is committed as a patch file with its numbers, not landed.
2. **Write `agents/handoff.md`** yourself: the state with numbers, each in-flight track (branch, last commit, what it
   was doing, whether the merger will land it), what was decided and why, the human's pending asks (exact commands),
   what the next coordinator should re-check before believing it, and which worktrees and branches exist. Commit it.
3. **The merger lands** the branches (gated as usual; scripts-only and docs-only commits skip the oracles), runs the
   final oracles and records them, removes the landed worktrees.
4. **A docs worker refreshes vision.md, todo.md, measurements.md and this file last**, from handoff.md and the
   merger's results, so the next coordinator finds the order in todo.md and the verdicts in vision.md.
The next coordinator reads handoff.md first, acts on it, folds anything durable into vision.md, todo.md or notes/, and
deletes it in its first commit. There is no handoff.md between coordinators otherwise.
