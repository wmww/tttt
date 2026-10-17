# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-16 (triage session, todo entry 10 of the seventeenth window): computed accessor pairs get
TS2676/TS2808, late-bound method overloads group by symbol (two own tests), the written/late-bound accessor pair went
to bugs/type_core.md, `-b`'s unchanged `.d.ts` writes and the lazy-slot counter to plan 76 (tsgo skips unchanged
writes only through its buildinfo), the 12-core notes were already in plan 72 and measurements.md, and
`scripts/perfsweep.py --defaults` now times tttt at its default threads and checkers (with recycle counts).

- (insights 2026-10-16) `scripts/fasttest.sh --summary` prints "13808 total" where `scripts/passcount.py` on the same
  saved file prints 13805: three configurations are counted by one and not the other (no components? skipped?). A
  papercut; find which and make the two agree.
- (plan 78 planning 2026-10-16) **Plan 79 meets plan 78**: checker types point into the function bodies the checker
  walked, which is why plan 78 drops a body only when every checker incarnation that touched it is recycled. A
  copying recycle (plan 79) that keeps survivors would keep pointers into bodies too: its survivor census must count
  the materializations the survivors pin, and its design must hand their refs to the new incarnation. Plan 79's
  planning session should read plan 78's "The finding that shapes stage 2" first.
- (plan 78 planning 2026-10-16) The machine this session ran on reported `nproc` 24 (the defaults gave 24 checkers,
  64 MB each on vscode) and had a Blender build running alongside: measurements.md's "Machine and caveats" describes
  the 4-vCPU box. If the shared machine changed, say so there (and which numbers in measurements.md came from which).
