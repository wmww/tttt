# Plan 76: the CLI modes a drop-in tsc still refuses (planning)
Written by the insights of 2026-10-16. A planning session: measure and recommend; the report tells the human, who
decides scope (they ruled out JS emit on 2026-10-12; watch and incremental were never asked).

## State
tttt refuses `--watch`, `--showConfig`, `--explainFiles`, `--init` (exit 1, bugs/harness.md) and neither reads nor
writes `.tsbuildinfo` (plan 64 decision 5), so `--incremental` / `composite` projects are rechecked in full and a
later `tsc -b` rebuilds everything tttt "built".

## Questions
1. **Is a full tttt check faster than tsgo's incremental rebuild?** On the corpora and a sample of source sweep
   projects: tsgo `--incremental` after a one-line edit in a leaf file and in a widely imported file, against tttt's
   full check at the defaults (both measured warm, in a copy, never the shared checkout). If tttt's full check wins
   on most projects, incremental is a compatibility question (don't break `tsc -b`'s view), not a perf one.
2. **Watch**: what would a watch mode cost architecturally? The program is rebuilt from scratch today; what can be
   kept across rebuilds (parsed libs, unchanged files' ASTs, the resolver caches), given that resolve writes into the
   AST and checkers are recyclable? Estimate a naive watch (file watching + full re-run in-process, libs kept) and its
   latency on vscode; that may be enough given question 1.
3. **The cheap ones**: `--showConfig`, `--explainFiles`, `--init`, `--listFilesOnly` parity: sizes against tsgo's
   sources (`thirdparty/typescript/.../execute`), whether clidiff can oracle them.
4. **`.tsbuildinfo` compatibility**: is writing a tsbuildinfo that tsgo accepts feasible (format stability across
   7.x), or should tttt at least not leave a state that makes a later `tsc -b` wrong?

## Output
A recommendation per mode (build / don't / ask the human) with sizes, in this file; the human-facing question goes
into the next insights' report.md entry via todo.md. Nothing built in this session unless it is one of the cheap
ones and under an hour.
