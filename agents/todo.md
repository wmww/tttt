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

1. Done 2026-10-16: plan 78 planned, **go** (every body skipped: vscode after names 2161 -> 898 MB at T4, effect 266
   -> 194, drizzle 143 -> 128; skipper 0.4% give-ups, 0 mismatches on real code; cross-file demand ~2k of 99k vscode
   bodies, re-materialize; bodies drop only at checker recycle, since types point into them).
2. Done 2026-10-17: plan 78 stage 1 (deferred bodies materialized at check, CLI default; vscode after names 2161 ->
   1025 MB, peaks unchanged until stage 2 drops them; K1 T4 wall 11.6 -> 11.3 s with materialize-ahead helpers, the
   defaults 5.6 -> 5.5; instructions +0..2% (tsc +2.0, vscode +1.7); every oracle unchanged, kdiff/recycle 1 / 3363;
   0 restarts on the corpora; suite with `--defer-bodies` 0 lost). Stage 2 must bound the helpers without running them
   next to the checker (a 32-file window cost +25%: plan 78 "Stage 1: done").
3. Done 2026-10-17: plan 78 stage 2 (materializations dropped with the last checker incarnation that read them, no
   address reuse so no pool or audit; vscode peak K1 2490 -> **1574 MB**, defaults 2643 -> **1829**, others equal or
   lower; output identical, every oracle unchanged; wall **+4%** on vscode at K1 and the defaults, instructions +2.2%:
   re-materialization at file granularity, 737 files on vscode K1). Over the 3% budget: entry 4 starts by winning it
   back (plan 78 stage 3 "Cross demand, first").
4. Done 2026-10-17: plan 78 stage 3, plan closed (documentation.md "Deferred bodies", hacks.md, perf_leads.md
   "Deferred bodies"). Materialization chunks as huge pages given back whole, only cross-made materializations carried
   (small pages): vscode K1 T4 11.83 -> 11.63 s, defaults 5.73 -> 5.66, T1 13.72 -> 13.47 (against eager +1% / +2% /
   +3%), peaks unchanged; table-driven skipper (-33% of its instructions); .tsx bodies deferred (excalidraw after names
   181 -> 156 MB, tanstack-router 108 -> 88); the harness defers by default. Not done, with reasons: single-body
   cross demand and gated runs' second parse (perf_leads.md), `import()` and `-b` bodies (hacks.md). Every oracle
   unchanged.
5. Done 2026-10-17: plan 80, closed. With bodies deferred only the source text passed the gate: files mmapped into
   reserved segments, their pages dropped after names and at every checker recycle, and the ResolvedModule cache
   dropped after names: vscode peak K1 1584 -> **1447 MB**, defaults 1852 -> **1706**, after names 1029 -> 843; wall
   +1.3% / +1.9%; other corpora's peaks flat. Expr boxing (~-15..20 MB now), the fat unions (ClassItem 448 B etc.,
   ~20 each) and 8-byte spans (~-35) declined, leads in perf_leads.md "Memory". Every oracle unchanged.
6. Done 2026-10-17: plan 79 planned and **declined** (measurements.md "Plan 79", perf_leads.md "Memory"): survivors
   from declaration-level roots are 35% of vscode's arena at 64 MB and 37-57% of dokploy's (gate 25%); a vscode
   recycle re-derives in ~15-35 ms, what a copy would cost; K1's peak is the program's already (1062 MB at 64 MB and
   below). The defaults' lever is the per-checker budget: vscode at 4 checkers 5.80 s / 1747 MB at 128 MB each, 5.87
   / **1537** at 96, 6.04 / 1362 at 64; drizzle and zod pay (+10-18% at 64), effect, type-fest, webpack don't.
   `scripts/survivors.sh` is the tool.
7. Done 2026-10-17: the checkers' budget floor 128 -> **96 MB** (measurements.md "The checkers' budget"): at 4
   checkers the 15 corpora +1.4% wall / -6.5% peak (vscode 1690 -> **1492 MB**, effect 705 -> 613, zod-root 609 ->
   506), the sweep's 169 biggest +3% / -3%; 80 and 64 cost the sweep +5% / +9%. drizzle and dokploy pay because a
   fresh checker rebuilds their schema graph (dokploy ~70 MB in one file, drizzle +20-27 MB in each of several); a
   largest-growth warm-up guard only moved along the same frontier (and let type-fest grow to 767 MB): dropped. The
   12-core cap left as is (binds past 16 checkers), unmeasured. Every oracle unchanged; K1 untouched.
8. Done 2026-10-17: none of the entry's leads was worth a session (measurements.md "The K1 budget, LazyType slot
   blocks"). ClassItem boxing would *grow* vscode (+9 MB: methods are the biggest variant; exact sizes ~-7); the
   LazyType slots now come in per-thread page blocks, a fresh one per file (pages touched -24% at 4 checkers, peaks
   within noise); drizzle's re-derivation isn't one memo kind (entry 7). The real K1 lever is the budget (vscode's
   peak is the checker's sawtooth: 128 MB is -15% peak / +2% wall on the corpora) but the sweep pays +6% (folo
   renderer +62%): K1 stays at 256 until something can tell a cheap recycle from an expensive one (perf_leads.md).
9. Done 2026-10-17: perf session (measurements.md "Perf session paying for the window", plan 67 "Where it stands").
   Instructions every corpus below entry 8 (type-fest 14.09 -> 10.94 G: tuple members; vscode 95.52 -> 94.34, still
   +2.1% over the window's start: re-materialization, perf_leads.md "Deferred bodies" has the lever). The bench: vscode
   1560 MB at the defaults (bun 1552, tsgo 7975), 1388 single (bun 1414); less than tsgo everywhere, than bun on 7 of 14
   single-threaded and 3 of 14 at the defaults (mid-size corpora 1.5-2x bun there: per-checker state). Faster than bun
   everywhere. Not done: lazy parameter types, the Source-keyed Maybe cache, the parser's per-attempt copies (no hot
   spot in a flat vscode profile; the parser is ~15% of it with bodies).
10. **Triage.**
11. **Insights.** report.md: the memory table against bun and tsgo, what the window cost in speed, whether the
   target ("less than every competitor in most cases") is met at K1 and at the defaults.

After the insights entry (for the next window): plan 77 (fresh repositories: does the sweep's match rate
generalize?), checker balance (plan 72 "Lead"), plan 58 + plan 66 stage 5 if 7.1.0 is out, plan 76 only if the
human reopens it, webpack's serial declare.
