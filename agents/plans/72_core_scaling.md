# Plan 72: core scaling (planning session)
Written by the insights of 2026-10-15. With plan 62 the checker count follows the cores and with plan 67 the
checkers' memory is bounded; what's left between tttt and "every core busy" is the serial part of the pipeline.

## The planning session's questions
1. **Where the serial time is**, at vscode scale and on a mid-size corpus (effect or webpack), after plan 67 stage 1:
   wall per phase at `--threads 1/2/4` (`--time`), and the serial remainder (declare, link,
   `binder_mark_serial_resolves()`, program setup, error sorting/printing, the `.d.ts` write if plan 64 stage 4 has
   landed). Extrapolate to 12 and 32 cores (Amdahl). bugs/perf_leads.md "Parallelism" has the earlier numbers and the
   "below ~10% isn't worth it" bar.
2. **Parallel declare/link**: per-file tables merged in program order (tsc's mergeSymbolTable order), merged type
   parameters in declare. Prototype the cheapest version if the serial share justifies it; say no otherwise.
3. **Load discovery** (the lexical import pre-scan, perf_leads.md): re-measure on a quiet moment.
4. The human's 12-core bench (triage.md, still open): if it's in measurements.md by then, use it; if not, say what
   the plan would do differently depending on its result.

## Deliverable
Either an execution plan (stages, expected wall gains at 12 cores, validation: byte-identical output at any thread
count, `scripts/threadcheck.sh`, kdiff) or a written "no" with the numbers, in this file.
