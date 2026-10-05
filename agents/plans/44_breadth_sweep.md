# Plan 44: a breadth sweep (stub, insights 2026-10-05)
Every pinned corpus is at or near 0 and they've stopped finding bugs. New corpora found tens to hundreds of false errors
each time they were pinned, but each is one project and costs a session to pin. A breadth sweep runs tttt on
**hundreds of small typed projects** that tsc 5.8 accepts. It's the cheap way to find crashes, hangs and false errors
that come up across many projects.

## For the planning session to decide (prototype first, then write the real plan here)
- **Source.** DefinitelyTyped is the obvious one: thousands of `types/*` packages, each with a tsconfig and `*-tests.ts`,
  and its CI keeps them clean (it uses the latest TS, so verify each against tsc 5.8 and keep only the ones that are
  clean there). A sparse checkout of a pinned commit, sampled (e.g. the 300 most depended-on plus a random 300). An
  alternative or complement is the `.d.ts` files of the top-N npm packages with a generated test importing each. Pick
  whatever gives diverse code cheaply and reproducibly. Dependencies between DT packages resolve through `types/`
  paths. Check how DT's tsconfigs do that.
- **Tool.** `./sweep.py` (or a `realworld.py --sweep` mode): for each package, run tttt (timeout + `ulimit -v`, like
  findcrash.py), and report crashes and hangs first. Then rank tttt's diagnostics by **root cause across packages**
  (code + message shape + offending construct, the way realworld.py groups by token), not per package. Cache tsc's
  verdict per package (tsc is slow; run it once, and record which packages tsc 5.8 itself rejects).
- **Oracle hygiene.** A package counts only if tsc 5.8 reports 0 for it under the same options. Pin the commit and
  the sample in the repo (a list file) so numbers compare across sessions.
- **Perf side-product.** Sum tttt and tsc wall over the sample: many small programs stress startup and lib loading,
  which the big corpora don't.

## Validation
The tool runs in under ~5 minutes on the sample and is documented in CLAUDE.md's script list. `measurements.md`
gets the first numbers (packages, crashes, hangs, packages with false errors, top root causes). The top root causes
go to triage.md or bugs/ with repros.
