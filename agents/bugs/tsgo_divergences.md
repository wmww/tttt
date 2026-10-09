# Leftovers of the tsc 6 era in the corpora
tsgo 7.1 is the oracle since plan 54 (the corpora compare against its own cached diagnostics, `test/corpora.tsc.jsonl`).
What the old tsc 6 oracle forced on the corpora and could now be undone:

- **vscode's corpus config excludes 3 files** (`thirdparty/corpora/vscode/src/tsconfig.corpus.json`:
  agentFinderRestProvider.test.ts, codexProviderConfiguration.test.ts, extHostNotebookStatusCommands.test.ts) because
  tsc 6 reported circularity (`class C { count = 0; constructor(r: () => number) {} } const c = new C(() =>
  id(c.count));`, TS7022+TS7024) that tsgo doesn't. Dropping the excludes (and `realworld.py --corpus vscode --tsc`)
  checks them against tsgo; tttt reported 3 of tsc 6's 5 pairs there, its circularity detection neither oracle's.
- **An Effect 4 corpus** (effect `main`): tsgo-clean where tsc 6 wasn't, heavy (tsgo 8.6 s on its src): a perf corpus
  candidate next to vscode.
- **zod-root**: tsgo 7.0.2 ran away on it (18 GB); the 7.1 cache exists, measurements.md has tttt's count. Which
  package blew 7.0.2 up was never bisected: a perf hazard worth a look if tttt slows down there.
