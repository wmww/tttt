# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction (insights after plan 13)")
for why.

1. **Insights.** Decide plan 22's "After the plan" question (a React corpus as the next oracle) and delete the plan
   file; decide when JS/JSDoc starts; direct `./displaydiff.py`'s `other` bucket (mostly checker bugs, see triage.md
   "Display leftovers after plan 21"). Plan 19 (corpora to zero) is still open: type-fest 168, valibot 164.
   Plan 23 (symbol merging) is done (8139 -> 8202); its leftovers are parser gaps and missing grammar checks,
   listed in triage.md. Members-as-symbols question the plan raised: not needed for the diagnostics. Static
   members vs merged namespace exports and object literal members are check-time replays of tsc's binder tables
   (hacks.md, "Class and interface *members*"). Their one semantic gap: a namespace export that conflicts with a static
   member still ends up in `typeof C`. Revisit only if members need symbols for other reasons.
   Process notes from a conversation with the user (2026-09-26, not urgent, act on them as you see fit):
   - **Insights cadence drifted.** The last insights (b5a8d94) planned 10 sessions; ~15 ran. Plan 21's "first stages"
     ran to completion, plan 22's execution was added after "planning: JSX", and triage queued plans 24/23 ahead of
     insights. Each step was reasonable; nobody re-approved the total. Consider how todo.md should cap this.
   - **Sessions ending without committing** (4 so far: plan 17, plan 16 stage 3, plan 22 stage 3, triage after
     21/22; the next session committed each). The two with transcripts ended their final turn while waiting on a
     background job (`findcrash.py`, `bench.sh`), so they never woke up again. The candidate fix is a CLAUDE.md
     workflow rule: ending the turn ends the session, so run long jobs in the foreground with a timeout, and commit
     before starting anything long.
