# Plan 46: oracles for missing diagnostics (stub by the insights of 2026-10-05, to be planned in todo entry 1)

## Why
Every oracle we run at scale is clean under tsc: the corpora and the sweep can only show *spurious* diagnostics.
Missing checks are found by the suite (thin now: ~150 failing errors components over ~190 codes) and by
DefinitelyTyped's `@ts-expect-error`s. A real user replacing tsc with tttt needs both directions to hold.

## Prototype (the insights session, 2026-10-05, tsc `src/compiler`, (file, line, code) sets)
Rerunning a clean corpus with a stricter flag turns it into a large set of *real* tsc diagnostics on real code:
| flag | tsc | tttt missing | tttt spurious |
|---|---|---|---|
| `--noUncheckedIndexedAccess` | 1233 | 25 (16 TS2339, 8 TS2345: some under another code) | 5 |
| `--exactOptionalPropertyTypes` | 747 | 10 (every TS2412/TS2375 is TS2322 or absent in tttt) | 4 |
| `--noPropertyAccessFromIndexSignature` | 11 | 0 | 0 |
| `--noImplicitOverride` | 0 | 0 | 0 |
Commands: `node thirdparty/tsc/lib/tsc.js -p DIR --noEmit --pretty false --FLAG` vs `release/tttt -q --all-errors -p
DIR --FLAG` (strip ANSI, `file:line [TSxxxx]`), compare with `LC_ALL=C sort -u`/`comm`.

## Stage 1: flag flips (the planning session builds the tool; execution drains it)
- A tool (`./flagflip.py`, or a `--flags` mode of `realworld.py`/`sweep.py`): every pinned corpus x a flag matrix,
  tsc's output **cached** under `test/` keyed by corpus pin + flags (tsc takes minutes on the big ones; vscode with
  `noUncheckedIndexedAccess` will have thousands), ranked like the sweep (root cause across corpora, `-k`, `--names`).
- The matrix to decide (measure what each produces): `noUncheckedIndexedAccess`, `exactOptionalPropertyTypes`,
  `noPropertyAccessFromIndexSignature`, `noImplicitOverride`, `noImplicitReturns`, `noFallthroughCasesInSwitch`,
  `useUnknownInCatchVariables`, `strictNullChecks`/`noImplicitAny`/`strict` **off** (many real projects aren't strict,
  and code paths for non-strict are barely corpus-tested), `noUnusedLocals`/`noUnusedParameters`, older `target`s, and
  the sweep with the same flips if cheap enough (DT tsconfigs set their own flags; tsc 5.8 over 8.6k packages is ~1.5 h
  per flag, so pick one or two).
- Validation: the tool in CLAUDE.md's script list, numbers in measurements.md, top causes with repros here.

## Stage 2 (decide in the planning session): mutation testing
Apply small semantic mutations to clean code (drop an `await`, swap two arguments, delete an object literal property,
widen/narrow an annotation, remove a `return`, rename a member access) and compare tttt with tsc on the mutated
program. Finds missing checks and wrong elaboration at breadth, and crashes on error paths no clean code reaches.
Needs a warm tsc (a node script reusing `oldProgram` per mutation, like `test/sweep_tsc.js`) and small programs (DT
packages, suite tests) to be cheap. The planning session prototypes ~100 mutations and says whether the yield is
worth a tool.
