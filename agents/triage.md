# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-12 (triage session, todo entry 10 of the fourteenth window): own regression tests wired in
(test/cases), lib files checked like tsgo, the knex variance blow-up fixed, the vscode excludes dropped, GCC 16 /
clang 22 builds clean, the small items filed into bugs/display.md and harness.md, the human's emit answer in vision.md.
The commit messages have the outcome.

From human:
- please merge CLAUDE.md and AGENTS.md, and have AGENTS.md be a symlink to CLAUDE.md
- also please make the final tests passing if not done already, so we're 100% green instead of 99.X%

From plan 61 stage 3 (2026-10-13):
- Order dependence around conditional branch instantiation: tsc simplifies every type before computing its base
  constraint (getResolvedBaseConstraint); tttt does it for indexed accesses only (hacks.md). Doing it for conditional
  types too, with a prefilter in `compute_simplified_conditional()` that skips instantiating branches when the root's
  branches can't be never, loses distributiveConditionalTypeConstraints (`f10`: `ZeroOf<T>` with `T extends {}` assigned
  to `"" | 0 | false` errors), although the prefilter returns the same answers: the earlier branch instantiation was
  a side effect something downstream depends on. Not in base/distributive constraints nor narrowable_type (checked);
  look at the relation of `ZeroOf<T>` to the union. Probe: the test's lines 27-37 under `--strict`.
