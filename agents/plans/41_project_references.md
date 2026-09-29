# Plan 41: project references (planning stub)
Brief by the insights of 2026-09-29. **A stub for a planning session** (after plan 40's), which may also pin a corpus
that uses them.

## Why
Real monorepos type-check with `tsc -b` / `references` in tsconfig, and tttt ignores `references` (vision.md: "Not
built: project references"). Today such a project can only be checked by pointing tttt at each package with a
hand-written overlay tsconfig (what the zod and xstate corpora do). A drop-in checker must accept these projects.

## Direction (to be decided by the planning session)
tsc -b builds referenced projects first and the referencing project reads their `.d.ts` outputs; tttt has no emit.
The option that needs none is tsc's language-service behaviour (useSourceOfProjectReferenceRedirect, also what tsgo
does in its LSP): a referencing project resolves imports of a referenced project's outputs to that project's *source*
files, and each project is checked with its own compiler options. Questions:
- Module resolution: map output paths (`outDir`/`declarationDir`/`rootDir`) back to sources; `composite` rules
  (TS6306, TS6307...) and the suite tests that exercise them (`./failstats.py` codes TS6305-TS6310, `-f 'project.*'`).
- Several option sets in one program: `compiler_options()` is thread-local and per-program today; each file needs its
  project's options (strictness differs between projects). Is it one program per project sharing parsed files (the
  load pool's cache), or one program with per-file options?
- A corpus: a monorepo that is clean under `tsc -b` (candidates: vue core, a tRPC/tanstack monorepo, TypeScript's own
  `src/` via `src/tsconfig.json`, which references `compiler` and the rest).
- Validation: the suite's project-references tests, the corpus at tsc's count, `./threadcheck.sh` on it.
