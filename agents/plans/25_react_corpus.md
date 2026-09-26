# Plan 25: a React corpus as the third oracle

Decided at the insights after plan 23 (2026-09-26). The suite's JSX tests are narrow (mostly hand-written `JSX`
namespaces, 313 of 457 pass); real React code runs through `@types/react`'s overloads, `LibraryManagedAttributes`,
`defaultProps`, huge intrinsic element tables, hooks' generic inference, `forwardRef`/`memo` HOCs and
`ComponentProps<typeof X>`-style type-level code. It's also where tsgo's performance is weakest, so it's the
benchmark most likely to show a real-world win or loss. Modeled on plan 17 (type-fest + valibot, one session).

## Steps
1. **Pick** a project. Requirements: real code that is clean under tsc 5.8 (`thirdparty/tsc`) with its own tsconfig,
   `strict`, mostly `.tsx`, roughly 100-600 files of its own, React 18 or 19 typings from the lockfile, no build step
   needed to typecheck (no generated `.d.ts` from codegen, no Next.js `next-env.d.ts` plugin magic that tsc can't see;
   workspace packages are fine if tsc resolves them through `paths`/`references`-free config). Prefer an
   *application* or a component library with its own components over a thin wrapper library. Candidates to evaluate
   (don't spend more than ~an hour picking): react-hook-form (library + `.tsx` tests), a medium app such as
   excalidraw's `packages/excalidraw` (big, maybe too many workspace deps), a Radix/shadcn-style component library, a
   TanStack package with `.tsx` tests. Verify with `node thirdparty/tsc/bin/tsc -p <dir> --noEmit`; if tsc isn't
   clean, try a different commit or a different project before patching anything.
2. **Pin** it in `thirdparty/ensure-corpora.sh` (commit + lockfile in `corpora-locks/`), add it to `realworld.py`'s
   corpus table and to `bench.sh`'s corpus loop (tsgo too; note any tsgo-only diagnostics like valibot's TS2554).
3. **Measure**: `./realworld.py --corpus NAME` counts by code and by file, `./bench.sh` against tsc/tsgo (wall, RSS,
   phase split). If tttt crashes or runs away, `--timeout`/`--mem-gb` and fix that first (that's in scope).
4. **Rank root causes**, not codes: sample the biggest groups, reduce each to a probe in `test/probes/plan25/`, and
   check each probe against tsc (`./tscbaseline.sh`). Write them into plan 19 (which becomes "all corpora to zero")
   as new R-items with size estimates, merged with the existing ones where they share a cause. Fixing a few cheap ones
   in the same session is fine, but the deliverable is the ranking.
5. `measurements.md`: a new entry with all corpora and the bench table. vision.md State: the new corpus.

## Validation
`./realworld.py --corpus NAME` runs to completion; tsc reports 0 on the pinned commit; plan 19 lists the new corpus's
root causes with probes; `./bench.sh` has a row for it.

## Risks
- A corpus with thousands of diagnostics is fine (valibot started at 4473, one augmentation bug was half of it); a
  corpus tttt can't finish is the first thing to fix.
- Don't let the pick grow into a monorepo with 50 workspace packages: the point is signal per diagnostic.
