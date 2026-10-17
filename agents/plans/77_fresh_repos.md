# Plan 77: fresh repositories (does the sweep's match rate generalize?)
Written by the insights of 2026-10-16 (after the eighteenth window); **not in the memory window, the first
correctness session after it**. One session: main plus up to two Opus agents in
worktrees for the fixes, if there are more than a session's worth.

## Why
Every breadth oracle is saturated (vision.md "Course correction"): source sweep 3356 / 3363 with the 7 left argued,
DT 8595 / 8595, corpora 0, mutation 7 / 2909 per seed. The 174 repositories of `test/sourcesweep/repos.txt` were
chosen once (one GitHub query: stars > 3000, pushed after 2025-10-01, size < 300 MB, a lockfile) and drained over
eight sessions. 3356 / 3363 says nothing about code tttt has never seen. The number this plan produces: **the
first-day match rate on ~30 repositories chosen by a different rule**, before anything is fixed.

## Steps
1. Pick ~30 repositories not in the manifest, deliberately unlike the original query: stars 300-3000 (messier code,
   older configs), domains the sweep lacks (games and graphics, Electron apps, bots and CLIs, Angular and NestJS
   applications, data tooling, monorepos with exotic `paths` / `references`), a few with `checkJs` or JS-heavy
   sources, a few on `moduleResolution: node16` / `bundler` with `exports` maps, one or two with `extends` arrays or
   deep config chains. Each needs a lockfile and a frozen install under its own package manager
   (`thirdparty/ensure-sources.sh`'s rules; drop what won't install and say so in a manifest comment). `gh search
   repos` or the API; record the selection rule in the manifest's comments.
2. Append them (NAME URL COMMIT), run `thirdparty/ensure-sources.sh` (idempotent; 30 s to 20 min a repo, ~230 MB
   each; 294 GB were free on 2026-10-16), then `scripts/sourcesweep.py --tsc-new` (a few seconds a project), then
   `scripts/sourcesweep.py -f '^(name1|name2|...)/'` on the new ones only.
3. **Record before fixing**: projects matched / compared, crashes, timeouts, mismatches ranked by root-cause key, in
   measurements.md "Plan 77" and in the commit message. This number is the deliverable, whatever it is.
4. Fix by root cause (the sweep ranks them), crashes first; a cut-down repro in `test/cases` for each mechanism;
   leftovers to bugs/sourcesweep_checker.md with the repo and the reduction.
5. End: the full sweep (old and new) with 0 lost on the old 3363 (vision.md "Fan-out merge discipline"), every
   oracle (`scripts/oracles.py`), `scripts/instrcount.sh`, the after-fix rate next to the day-one rate.

## What the number means
- **>= 90%** of the new projects match on day one: the checker generalizes and the tail is per-repository. Refresh
  the sweep this way every few windows; it is cheap.
- **70-90%**: a fan-out session on them (todo entry 2), then the rate again on the next batch.
- **< 70%**: the long tail is bigger than the oracles show; insights rebalances the window toward breadth.
Config-level mismatches (TS5xxx/TS6xxx, module resolution) count separately from checker ones: say which kind
dominates, as plan 52 stage 1 did.

## Validation
measurements.md has the day-one and after-fix rates with the mismatch kinds; repos.txt has the rows and the rule;
the old 3363's match set is unchanged or better; `scripts/oracles.py` unchanged or better.
