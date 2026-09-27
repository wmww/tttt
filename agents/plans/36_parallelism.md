# Plan 36: parallelism readiness (planning session wanted)
Written by the insights of 2026-09-27 (third of the day) as a brief for a **planning** session, which replaces this
file with the real plan.

## Why now
The goal is beating tsgo, and tsgo runs on all cores. Single-threaded tttt still wins on six of seven corpora, but
the margins shrink as checks land (+7% instructions over two days) and two corpora are close or lost: webpack (0.69
s vs tsgo 0.61 s multi-threaded, 0.60 s single) and nest (0.26 vs 0.40 s, parsing is half of tttt's time). Vision
has said "not precluded, not built" for a long time; meanwhile the binder has grown more shared state (JS expandos,
`declare global`, `Symbol.referenced`). The longer it waits, the more there is to unpick. Parsing is embarrassingly
parallel in principle and is 13-43% of the time on every corpus.

## What the planning session should find out
1. **Parallel parse, measured**: what stops `parse_file` from running on N threads today (the string interner, the
   allocator, `DEBUG_ALLOCATOR` being on in release (`src/prelude.h`), global tables, error lists)? Prototype it on a
   scratch branch or behind a flag, even with a crude lock around the interner, and measure wall time on nest,
   webpack and the tsc corpus. This is the number that decides how much the rest is worth.
2. **Bind/resolve**: `hacks.md` "Parallelism contract" lists what the declare and resolve phases write to shared
   state. Estimate the cost of per-file arenas and tables merged at link time, like tsc's per-file binding.
3. **Check**: "N `Checker`s over one AST, files partitioned" (vision). What's per-checker today and what isn't (the
   thread-local `current_checker`, lazily cached per-node types, shared symbol slots)? Type ids are per checker and
   printed union order depends on them: how does output stay deterministic? (tsgo's answer: checkers own files,
   diagnostics are sorted.) Memory: N checkers duplicate the global types; measure one checker's footprint.
4. Decide stages (parallel parse first is likely), each with a validation (identical output under any thread count,
   `./fasttest.sh` unchanged, bench numbers), and rewrite this file.

## Constraints
Output must be byte-identical for any thread count. No stage may leave the suite unrunnable. Don't start a
rewrite of the parser or binder under this plan: if the prototype says a component can't be made parallel without
one, write that up for the next insights.
