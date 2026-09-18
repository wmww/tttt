# Triage
Uninvestigated items. Triage sessions drain this.

- 726 tests are `undefined` in `test/baseline_results.ts`. Why? (no baseline? skipped? crash?)
- readme.md says development is halted and describes the project as "Tess"'s; refresh it.
- Catalogue the ~60 PERF/HACK/TODO/FIXME comments in `src/` into hacks.md (smaller-model sub-agent job).
- AGENTS.md is a byte-identical copy of CLAUDE.md; make one a symlink or note which is canonical so they don't drift.
- Parser: no tokenizer + backtracking with fail cache. Fine for speed in principle, but check parse-error fidelity
  against tsc (the errors baseline includes syntax errors) and worst-case behaviour once plan 00's bench exists.
- `engine.c` (JS interpreter, 634 lines): is it used by the checker (const enum / literal evaluation?) or dead
  weight? Remove if unused.
- Does `./test.sh` (valgrind) currently run clean on a sample? Unknown after the cleat port.
