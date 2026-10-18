# Todo
Written by the insights of 2026-10-18 (after the twentieth window); `vision.md` "Course correction" says why. **Ten
sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the numbers). A plan
that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run (count the commits
since this file was written), the next session is insights, whatever is left here. New work goes into plans or
triage.md, not ahead of the insights entry. Every entry ends with `scripts/fasttest.sh -s`, `scripts/oracles.py`
(every oracle; it exceeds the 10-minute foreground cap: run it in the background with a bounded wait) after checker
changes, `scripts/instrcount.sh`, the memory numbers in measurements.md after memory-relevant changes (vscode,
effect, webpack, drizzle, type-fest: RSS after parse / after names / peak at K1 and at the defaults), an empty `git
worktree list`, /tmp scratch deleted (CLAUDE.md), and a commit. **If TypeScript 7.1.0 stable is on npm when an entry
starts (`npm view typescript dist-tags`: on 2026-10-18 `latest` was 7.0.2 and `next` 7.1.0-dev.20261017.1), do plan
58 (the re-pin) first, then plan 66 stage 5**, and push the rest down. `claude-stop` is gitignored: never commit it.

Starting point (40afe12a + this commit): suite 13792 / 13829 (dts 1837 / 1860), corpora 0 / 0 but vue 0 / 2, DT
8595 / 8595, `$ExpectType` 68639 / 68640, flagflip 18 / 8 / 3, scenarios 210 / 0, source sweep 3559 / 3566, dtsdiff
1193 / 1233, clidiff `--emit` 369 / 396. Memory and wall against bun: plan 67 "Where it stands" (2026-10-18);
instructions: `test/instrcount.txt`.

Rules of the window (vision.md "Memory target"): a memory change costs at most ~3% wall at K1; at the defaults it must
stay faster than bun on every corpus and the sweep's tail. Output byte-identical at every K and budget.

1. Done (2026-10-18): deferred bodies leak nothing at a drop (strings sealed into the arena, the walk's lists the
   AST's; vscode K1 drop leak 12.6 MB -> ~60 KB) and a drop's re-materialization makes the demanded body alone
   (vscode defaults 0.80 -> 0.16 s CPU re-materializing, 6.17 -> 6.10 s, peak 1253 -> 1221 MB). `--checker-memory 0`
   recycles after every file again (the -m 0 oracles weren't recycling small programs): recycle oracle 4 / 3566,
   pre-existing (measurements.md).
2. Done (2026-10-18): plan 82 stage 1. ~140 sites inventoried; tttt already mixes both representations, safe only
   because the adds are idempotent. A one-line flip loses 36 tests and adds 20 corpus diagnostics, all in predicted
   classes. Cost: +0.1% types, effect +4% / ~+1% wall. Stage 2 sized at one session, stage 3 (tuples, parameters) at
   one more.
3. Done (2026-10-18): plan 82 stage 2. Properties store their undefined (missing under exact), every reader add and
   the intersection hacks deleted, `TTTT_CHECK_OPTIONALITY` silent before deletion. Suite 13795 / 13832 (+3 own
   repro tests, +1 tsc test), every oracle at the starting point except the source sweep 3558 / 3566: tiptap/demos'
   predicted TS2590, a pre-existing JSX tag type bug that matched tsc by luck before (bugs/sourcesweep_checker.md).
4. Done (2026-10-18): plan 83. The re-entry came from asking a mapped type's genericity, keys and MappedInfo of its
   expansion (which relates the `as` clause): those now come from the declaration (`ty_generic_mapped()`, an
   instantiation expands from its declaration's info instantiated, not the generic target's expansion). The `as`
   clause kind is tsc's relation alone (shape reading and the `keyof` text test deleted, hacks.md entry gone);
   `never_index_read` narrowed. Suite 13797 / 13834 (+1 tsc test, +2 own), every oracle at the starting point,
   instructions flat (documentation.md "Members asked for while they are resolved").
5. Done (2026-10-18): plan 84. The loader records each file's first package id and what it reached in order;
   `program_collect_files()` (tsgo's collectFiles) picks representatives depth-first, a redirect and what only it
   reaches are excluded (not bound, not reported). HACK `sources_by_package_id` and `program_share_redirect_ref()`
   gone; type references and JS/JSON resolutions give ids too. effect through a symlinked checkout byte-identical
   to tsc (was ~25 diagnostics); base-ui's 10 extra files gone. Suite 13798 / 13835 (+1 own), every oracle at the
   starting point (recycle 3 / 3566, was 4), instructions flat. Worktrees with symlinked corpora now match tsc.
6. Done (2026-10-18): declaration emit fan-out, four agents. dtsdiff 1193 -> 1211 / 1233 (goal 1210), clidiff
   `--emit` 403 / 410; suite 13823 / 13859 (dts 1864 / 1886; +24 own tests, +1 tsc test), every oracle at the starting point
   (sourcesweep 3559 / 3566, redux-toolkit's text now tsc's). Tail is mostly tsgo's checker split (triage.md);
   new bug: bugs/expando_instantiation.md (an expando function in a generic one is never instantiated).
7. Done (2026-10-18): second refresh, 22 repositories (16 declaration-emitting libraries, 9 checkJs projects, 3
   dropped for their installs; repos.txt "Refresh 2"). **Day one: source sweep 66 / 68 projects (97.1%), dtsdiff
   34 / 41 projects, 4007 / 4100 files**. Fixed: `export default async x =>` (parser), `this.f = this.f.bind(this)`
   (binder), elaborated JSDoc `export default` (checker); left rocket's order-dependent TS7031
   (bugs/context_on_demand.md, architectural) and vest's dts items (bugs/dts_emit.md). Source sweep 3626 / 3634,
   dtsdiff 1245 / 1274, every other oracle at the starting point; suite 13826 / 13862.
8. Done (2026-10-18): perf session. webpack's serial declare 40 -> 18 ms (the CommonJS export walk moved to parse
   time, `source_collect_commonjs_candidates()`); error merges in one pass (inbox-zero 1.29 -> 0.85 s, now level with
   bun; apps/web 0.86 against bun's 0.97: ~22k errors merged quadratically, not warm state). The shared-warm-state
   checker count: measured, five policies prototyped and reverted, none off plan 81's declined frontier
   (perf_leads.md "Shared warm state at the defaults"). Every oracle at the starting point, instructions flat; bench
   in plan 67 "Where it stands".
9. **Triage.**
10. **Insights.** report.md: did the debt plans delete their hacks, the refresh's two day-one numbers, whether the
    human's 12-core numbers arrived (vision.md item 22 waits on them).

After the insights entry (for the next window): plan 58 + plan 66 stage 5 when 7.1.0 is out (expected after
2026-11-24); plan 82 stage 3 (tuples and parameters, one session), plus stage 2's remainder if any; plan 76 only if the human reopens it.
