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

From plan 63 stage 1 (2026-10-14):
- `is_bindable_static_name()` (ty/expr.c, tsc's isBindableStaticNameExpression) returns false for every dot access:
  its `a.#x` test compared a dot name's Expr tag to EXPR_IDENTIFIER, and a dot name was always an EXPR_LITERAL (now a
  Span; the behaviour was kept, with a TODO). So `annotated_property_context()` treats `a.b = v` like `this.b = v`.
  Find a test where tsc's AssignmentDeclarationKind.Property contextual type differs, fix (`property_name()` starting
  with `#`), and see what moves.
- scenarios.py is 207 / 213 at f67e4fa7 already (todo.md's starting point says 209): the 6 `-b` failures
  (contentMapperOptionDiagnostics, dependencyUpdate, inferredTypeFromMonorepoReference, libraryResolution x2,
  projectReferenceRedirect) predate plan 63. Find when they started failing (or whether 209 was a different checkout).
