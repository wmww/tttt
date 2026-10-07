# Plan 52: the source sweep (real TypeScript repositories at breadth)
Brief by the insights of 2026-10-07 (end of the eleventh window). A planning session (todo entry 2) prototypes it and
writes the real plan.

## Why
Every pinned corpus is at 0 and goes quiet once drained, yet each new one found tens to hundreds of bugs on its
first day (effect 6300, vscode 129, excalidraw/tanstack-router same-day drains). The DefinitelyTyped sweep (plan 44)
covers declarations and small test files, not application or library *source*. The source-code analogue of plan 44:
tttt on ~100-300 real TypeScript repositories, each with its own tsconfig(s) and installed dependencies, compared with
tsc 4d4f005c per project, ranked by root cause across projects. It also exercises tsconfig/CLI shapes (paths,
extends chains, moduleResolution bundler, composite/-b, allowJs mixes) the corpora don't.

## What the planning session decides (prototype ~20 repos first)
- Selection: e.g. the most-depended-on npm packages with TypeScript sources plus popular TS apps; mix of
  libraries/apps/monorepos, JSX, checkJs. Pinned commits in a manifest (like `thirdparty/ensure-corpora.sh`).
- Dependencies: how to install reproducibly and offline-after-first-fetch (a shared pnpm/npm cache), disk budget,
  repos that need build steps (generated code) -> drop them.
- Oracle: tsc's diagnostics cached per project (`test/sourcesweep/` like `test/sweep/`); projects needn't be clean
  under tsc (compare, like zod-root). Missing direction: one or two strict flag flips per project (plan 46's cells).
- Tooling: `./sourcesweep.py` modelled on `./sweep.py` (crashes first, `-k KEY`, `--last`), runtime budget (minutes).
- Then the plan's stages: build + first measurement (todo entry 5), drain fan-out (entry 8).

## Validation
The tool runs in minutes from cache, every selected project's tsc diagnostics cached, first numbers in
measurements.md, crashes/hangs zero.
