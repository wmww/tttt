# Plan 58: re-pin the oracle to TypeScript 7.1.0 stable (one session, after 2026-11-24)
The oracle is `main` at fed0bf24 = `typescript@7.1.0-dev.20261008.1` (plans 53 and 54, done and deleted: git has them). 7.1.0
stable is expected after 2026-11-24; the dev-to-stable drift was estimated at ~2% of the suite. Don't do it before
the release exists on npm (`npm view typescript@7.1.0 gitHead`, or `@latest` once 7.1 is it).

## Steps
1. `thirdparty/ensure-typescript.sh`: `TS_COMMIT` = the release's `gitHead`, `TS_VERSION` its version; Go version
   from the new `tsc/go.mod` (`GO_VERSION` + its sha256). `rm -rf thirdparty/typescript` and rerun (~90 s cold, plus
   the overlay tools: test/oracle may need small API fixes if tsgo's internals moved).
2. `thirdparty/refresh.sh`: libs from the new tree; regenerate the C tables (`src/lib_features.py`,
   `src/lib_names.py`, `src/unicode_identifiers.py`, `src/regex_tables.py`) and diff them.
3. The suite: `scripts/fasttest.sh --summary`. Lost tests are 7.1 changes landing after the dev pin: bucket them
   (failstats), fix the cheap ones, list the rest in the commit message / triage.md. New tests in the tree count too.
4. Every cache, regenerated with tsgo (each script's `--tsc`): `scripts/realworld.py --tsc --corpus X` for every
   corpus, `scripts/sweep.py --tsc` (and `--tsc --flags nuia`, `--flags eopt`), `scripts/expecttypes.py --tsc`,
   `scripts/sourcesweep.py --tsc`, `scripts/flagflip.py --tsc --force`, mutate's /tmp cache (just delete it). Commit
   between them: they take minutes each (sweep ~10, source sweep ~12).
5. `scripts/oracles.py`, `scripts/scenarios.py`, `scripts/clidiff.py --sample 4`: record the before/after numbers in
   measurements.md; update CLAUDE.md's and documentation.md's oracle sentence (the version), vision.md's state.

## Validation
`grep -rn fed0bf24 scripts src test thirdparty/*.sh` empty; every oracle line in `scripts/oracles.py` ran against the
new caches (none STALE); the suite's lost list is either fixed or written up.
