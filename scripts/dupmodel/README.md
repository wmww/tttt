# dupmodel: cross-checker duplication, offline

What K checkers make twice, and whether a different file-to-checker assignment would avoid it
(agents/notes/duplication.md has the results). Everything is behind env vars of the ordinary build (ty/dupcensus.c).

## The census (no scripts needed)
    TTTT_DUP_CENSUS=1 release/tttt --threads 1 --checkers 12 -q --keep-going --noEmit -p P 2> census.txt
After checking, every checker's types and relation cache entries by structural fingerprint (fingerprint.inc): per
checker its types, arena and how much of it another checker / every checker made too; per domain copies against
distinct ones, how many checkers hold each, and the classes (type kind / home: lib, dts, ts, - for none) by extra copies.
`--threads 1` is the pure deal (no stealing happens on one thread); with threads it is the default schedule.
The `types` and `relation cache` domains are history-stable (K12 distinct within 0.1% of K1); the intern-key and
node-link domains hash ids and arena addresses, so their duplication is a lower bound.
Per-checker instructions: callgrind with `--separate-threads=yes --collect-atstart=no --toggle-collect=check_share`
at `--threads K --checkers K`, one `totals:` per thread file.

## Inputs for the model
    TTTT_DUP_CENSUS=1 TTTT_DUP_DUMP=P.dump release/tttt --checkers 1 --threads 1 --checker-memory 0 -q --keep-going --noEmit -p P
    TTTT_DUP_GRAPH=P.graph release/tttt -q --keep-going --noEmit -p P
The dump: a fresh checker per file, each file's sampled (1/4) fingerprints. Fingerprints use addresses, so a dump is
only comparable with itself (each run samples a slightly different subset: ~0.5% noise between runs). The graph:
per checked file its index, source bytes, default-library flag, path and the checked files it imports.

## Scripts (numpy; run with `python3 -I`)
- `DOMAINS=1 model.py P.dump K...`: the current deal's predicted total types (x distinct) for each K, and a naive
  biggest-first greedy (worse than the deal). Reproduces the census's `types` line within 0.2-3%.
- `graph_ls.py P.dump P.graph K...`: local search on import closures (all, depth 2, depth 1) from the deal.
- `write_deals.py P.dump P.graph K PREFIX`: writes PREFIX.deal / .oracle (local search on the true type sets: the
  bound on what assignment can save) / .graph1 (direct imports), one checker per line.
- Measure one for real: `TTTT_DUP_DEAL=PREFIX.oracle` with `--checkers K` (the census, or callgrind for instructions).
