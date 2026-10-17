# Todo
Written by the insights of 2026-10-16 (after the eighteenth window); `vision.md` "Course correction" and "Memory
target" say why. **Ten sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with
the numbers). A plan that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run
(count the commits since this file was written), the next session is insights, whatever is left here. New work goes
into plans or triage.md, not ahead of the insights entry. Every entry ends with `scripts/fasttest.sh -s`,
`scripts/oracles.py` (every oracle; it exceeds the 10-minute foreground cap: run it in the background with a bounded
wait) after checker changes, `scripts/instrcount.sh`, the memory numbers of the entry in measurements.md (vscode,
effect, webpack, drizzle, type-fest: RSS after parse / after names / peak at K1 and at the defaults, `--time` and
`/usr/bin/time`), an empty `git worktree list`, /tmp scratch deleted (CLAUDE.md), and a commit. **If TypeScript
7.1.0 stable is on npm when an entry starts (`npm view typescript dist-tags`: on 2026-10-16 `latest` was 7.0.2 and
`next` 7.1.0-dev.20261016.1), do plan 58 (the re-pin) first, then plan 66 stage 5**, and push the rest down.
`claude-stop` is gitignored: never commit it.

Starting point (2103f8b7 + this commit): suite 13766 / 13805 (dts 1834 / 1859), corpora 0 / 0 but vue 0 / 2, DT
8595 / 8595, `$ExpectType` 68639 / 68640, flagflip 18 / 6 / 3, scenarios 210 / 0, source sweep 3356 / 3363, kdiff
1 / 3363, recycle 4 / 3363, dtsdiff 1193 / 1233, clidiff `--emit` 369 / 396. Memory (vscode): K1 ~2.55 GB peak
(program ~2.2 after names), 4-core defaults ~2.8 GB, 12-core K24 ~4.7 GB under the cap; bun 1.4 / 1.6 / 2.1.
Instructions: measurements.md "Perf + real code".

**This window: memory, bought with speed where it costs any.** The human's target (2026-10-16): less memory than
every competitor (bun, tsgo) in most cases, as fast as possible, correctness unchanged; features (watch, incremental,
the cheap CLI modes: plan 76) parked. The rule: a memory change may cost at most ~3% wall at the defaults and at K1,
and the window's perf session pays it back; never trade at a worse rate than another item would give. Output stays
byte-identical at every checker count and budget (kdiff, checkerdiff, recycle oracles).

1. **Plan 78 planning + prototype gate** (deferred function bodies): the upper bound (every body skipped at parse:
   RSS after parse/names on vscode, effect, webpack, drizzle), the skipper's give-up rate, cross-file body demand
   counts, the per-body bind/resolve design, the stages. Gate: >= 600 MB off vscode's program. Rewrite plan 78 as
   stages with validation, or close it with the numbers if the gate fails (then entries 2-4 go to plan 80's
   leftovers and plan 79).
2. **Plan 78 stage 1**: skip + materialize at check, whole-file semantics; byte-identical everywhere, instrcount.
3. **Plan 78 stage 2**: drop after check, per-body LazyType slots and memo pages; the memory table.
4. **Plan 78 stage 3**: cross-file demand policy (keep vs re-materialize, measured), JavaScript, `-b`, the harness,
   kdiff/recycle, perfsweep; plan 78 closed into documentation.md ("Deferred bodies") and hacks.md.
5. **Plan 80** (AST boxing, spans, mmapped text; main + Opus agents in worktrees for the mechanical parts): Expr
   72 -> 40, the other fat unions, Span 16 -> 8 for AST nodes if the saving is still >= 50 MB after boxing, source
   text mmapped. Gate per item in the plan. instrcount flat.
6. **Plan 79 planning + prototype** (a copying checker recycle): survivors from declaration-level roots at a recycle
   on vscode, dokploy, drizzle, type-fest (gate: < ~25% of the arena); a prototype copy's wall at B=64/32 (gate: < 3%
   at a budget halving the checkers' share on vscode K24); else the generational-arena alternative. Rewrite plan 79
   as stages or close it with the numbers.
7. **Plan 79 execution** (one or two sessions): the copying recycle, the budget policy and the CLI's cap redone
   (plan 75's `program_set_checker_memory_cap()`), plan 67 stage 1's K1/T4 table redone, `checkerdiff -k 1 -m 0`.
8. (Plan 79's second session if needed; else **Expr/Span leftovers of plan 80** or the LazyType slots fallback.)
9. **Perf session, paying for the window**: `scripts/instrcount.sh` against the starting point on every corpus
   (find what entries 2-7 cost: bisect with `--bin`), then the single-thread leads of bugs/perf_leads.md (type-fest's
   tuple members merge, lazy parameter types with a cheaper mapper composition, the Source-keyed Maybe cache then
   hacks.md "Relations"' intersection_source rule, the parser's per-attempt copies), perfsweep (types ratio),
   scaleprobe. Then **the memory bench**: `scripts/bench.sh --runs 3 --bun PATH` (measurements.md says how to get
   bun) at the defaults and single-threaded, every corpus, against the window's start, into measurements.md and plan
   67's "Where it stands"; report.md's question to the human is the 12-core rerun.
10. **Triage.**
11. **Insights.** report.md: the memory table against bun and tsgo, what the window cost in speed, whether the
   target ("less than every competitor in most cases") is met at K1 and at the defaults.

After the insights entry (for the next window): plan 77 (fresh repositories: does the sweep's match rate
generalize?), checker balance (plan 72 "Lead"), plan 58 + plan 66 stage 5 if 7.1.0 is out, plan 76 only if the
human reopens it, webpack's serial declare.
