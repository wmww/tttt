# How to be a good coordinator of tttt

Written 2026-10-21 by the third coordinator at the human's request, from plans 89/90's lessons, the memory notes of
the first two coordinator sessions, and this one. CLAUDE.md "Workflow" states the model in one line; this is the
practice. It is advice, not procedure: a later human instruction overrides any line here.

## The job
You turn the human's goal into a few parallel tracks, keep the tree green, and decide. You do not code. The human
wants to talk to you at any time, so between reports you are idle: workers and the merger run in the background and
their messages wake you. Your context is the scarce resource: read reports and plans, never transcripts or dumps.

## Before spawning anything
1. **Check the premise with numbers you can see.** Read the latest measurements and the plan's own budget before
   adopting its mandate. Plan 90 asked for the shared graph on by default; the end-state gates it cited were already
   failing on effect and the laptop bench then showed no win. Twenty minutes of reading and two short runs changed the
   window. A plan written by the previous coordinator is a hypothesis, not an order.
2. **Measure the thing the plan didn't.** The bench corpora never had much duplication to share; the projects where
   the feature wins (the thrashers) were not in the bench. Ask "where would this win if it worked at all?" and run
   that first (`/usr/bin/time` on three runs is enough to decide a direction).
3. **Write down the current numbers** of the project you care about on this box (`tttt --time` at the defaults, K1
   and K=threads) so every brief starts from the same baseline and workers measure deltas, not absolutes.

## Decomposition
- Split by **files and mechanisms**, never by diagnostic codes or by goal. Tell each worker what the others own and
  relay ownership changes the same hour. Shared hot files (relate.c, members.c) go to one owner; others may make
  small local fixes there and are told to keep commits focused.
- One **diagnosis track** for anything uncertain, time-boxed to a session, with the deliverable "numbers and a
  verdict", and no build-out without your go. A worker that can't name a mechanism in one session won't in two.
- One **cheap, certain win** per window (a policy, a default, a measured small fix) so the window shows a result even
  if the big bets fail.
- Three to four workers plus a merger. Five was the ceiling; the merger is non-negotiable once anyone commits.
- Opus for judgment (checker semantics, perf, diagnosis); Sonnet for mechanical loops with a clear oracle.

## Briefs (the only thing a worker knows)
A common rules file plus a task file, "Read FILE and follow it". The task file states: the situation with numbers,
the deliverables in order, the gates per commit, what the worker owns and what the neighbours own, when to send an
interim report, and that failing early with a reason is a good outcome. The common file carries the hygiene rules
that each cost an interruption once: no `git stash`, no `pkill -f` with the pattern inline, no `rm` on a
variable-built path, no heavy oracles or `--tsc` regenerations from worktrees, measure instructions not wall on the
shared box, never amend a commit. If you can't write the brief, you don't understand the task yet.

## While they run
- **Decide fast, in a paragraph with reasons.** Workers ask for decisions (design choice, gate relaxation, budget
  overrun) several times a window; ask them to propose, choose within minutes, state the reason and the recovery
  lever if you accept a cost.
- **Relay.** Workers can't reach each other; what one learns about a shared file goes to the other through you.
- **Resume, don't respawn.** A finished worker with its context is the cheapest agent for adjacent follow-ups
  (a regression it caused, the next item on its list). Hand off past ~600k tokens with a resume paragraph in the plan.
- **Trust the merger's measurements, not the workers'.** Every worker passes its own gates; the merger's instruction
  counts on clean builds, the corpora, checkerdiff/threadcheck and the periodic full `oracles.py` find the real
  regressions (+1% counters, K-dependences, history dependences). Budget them; never skip them to go faster.
- **Ask the human for what only they have** (the laptop bench) as soon as a commit makes it worth their time, with
  the exact command, not at the end of the window.

## Judging results
- A win is a number against the same base build in the same worktree, A/B interleaved, load stated. "Within noise"
  is not a win. A rarer flake without a named mechanism is not a fix.
- Keep the big picture: tests and oracles are the gate, speed on real projects is the goal, and correctness is never
  traded. When a feature is faster on some projects and slower on others, it is a policy question (when to turn it
  on), not a default.
- Write the verdict where the next reader looks: the plan file for design conclusions, measurements.md for numbers
  with the command and the box, vision.md's course-correction paragraph for decisions, todo.md for the order.

## Ending the window
A docs worker refreshes vision.md, todo.md, measurements.md and the handoff plan from your facts (it catches what you
got wrong). Then: `git worktree list` shows only the repo, the branches are merged or dropped with a line saying
which, scratch deleted, instruction counts saved, pushed. The handoff plan says what was decided and why, what the
numbers are, and what the next coordinator should check before believing it.

## Lessons of the third session (2026-10-22)
1. Check the handoff's premise with numbers before adopting its mandate: a 20-minute read and two 3-run timings
   withdrew plan 90's mandate (the shared graph on by default) and changed the whole window.
2. A diagnosis-only track with a written verdict (sg) settled a question three sessions had circled, in one session,
   and found two real bugs on the way.
3. The cheap certain wins (defaults policy, memory default) were worth more wall than the research track.
4. A worker that reports "the algorithm is already tsc's" (k2) has produced a result: stop it rather than fund
   constant factors.
5. The full oracle run caught the one regression the per-cycle batch missed (`$ExpectType`), so `$ExpectType` joined
   the batch.
6. Attribute instruction jumps before celebrating them: -15% on drizzle was mostly a memory policy, not checker speed.
7. Hand finished workers' leads to the owner of the file, not to whoever found them (k1 -> fe path lead, k1 -> k2 flow
   leads, k2 -> k1 when k2 finished).
8. Mutation seeds yield less each time (5, then 2 fixes per 8000 mutants).
