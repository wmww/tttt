# Leftovers of the tsc 6 era in the corpora
tsgo 7.1 is the oracle since plan 54 (the corpora compare against its own cached diagnostics, `test/corpora.tsc.jsonl`).
What the old tsc 6 oracle forced on the corpora and could now be undone:

- **An Effect 4 corpus** (effect `main`): tsgo-clean where tsc 6 wasn't, heavy (tsgo 8.6 s on its src): a perf corpus
  candidate next to vscode.
- **zod-root**: tsgo 7.0.2 ran away on it (18 GB); the 7.1 cache exists, measurements.md has tttt's count. Which
  package blew 7.0.2 up was never bisected: a perf hazard worth a look if tttt slows down there.
