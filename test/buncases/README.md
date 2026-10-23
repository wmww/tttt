# bun's differential check cases
`differential-cases.json` is bun's `test/cli/check/differential-cases.json` (github.com/oven-sh/bun at 5c6d4e24c,
2026-10-23; the file last changed in b1d468617, 2026-10-22; bun's tests are MIT). 2418 small projects (files and a
tsconfig.json, each with a `sentinel` error so the output is never empty) on which `bun check`, bun's TypeScript 7
reimplementation, once disagreed with tsc; `build: true` cases run `tsc -b`. `scripts/buncases.py` checks them
against `tsc.jsonl.gz`: the pinned tsgo's output for each case (lines with the scratch root stripped, exit code),
keyed by case id with a hash of its content (`scripts/buncases.py --tsc` regenerates the stale ones). To refresh,
copy the file from a newer bun commit, update the commit here and in the script, and run `--tsc`.
