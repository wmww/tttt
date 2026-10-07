# Todo
Written by the insights of 2026-10-07 (after the tenth window); `vision.md` "Course correction" says why. **Ten
sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the numbers). A plan
that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run (count the commits
since this file was written), the next session is insights, whatever is left here. New work goes into plans or
triage.md, not ahead of the insights entry. Every entry ends with `./fasttest.sh -s`, the corpus numbers
(`./realworld.py --corpus X` for every pinned corpus), `./sweep.py` and `./flagflip.py` after checker changes, `./instrcount.sh` and a
commit.

**This window retargets tttt from TypeScript 5.8 to TypeScript commit 4d4f005c (6.0.3 + stable type ordering, what
tsgo 7.0.2 implements; plan 48).** Pass counts aren't comparable across the switch (13540 -> 14969 tests). Entry 6 regenerated the
sweep / `$ExpectType` / mutation caches on the new tsc; flagflip's waits for entry 7's re-pin, and corpus
numbers are only meaningful against the commit's tsc (`node thirdparty/tsc/built/local/tsc.js -p ...`).

1. Done (planning, 2026-10-07): plan 48 rewritten from four prototypes (now applied: 75cb7ee6, a8ac7023).
2. Done (execution, 2026-10-07): plan 48 stage 1. `thirdparty/tsc` at 4d4f005c (no `lib/` LKG: scripts use
   `built/local/`), suite **13683 / 14969** (13220 after the prototype patches, +463 from messages, import types
   and module symbols named by module specifier, omitted elements). `48_lost.txt`: 9 true (strict-by-default
   exposure) + 796 re-baselined (612 types only).
3. Done (execution, 2026-10-07): plan 48 stage 2 session A, stable type ordering with a cached sort key per type:
   suite **14269 / 14969** (0 lost), instructions +0.9% tsc, +1.2-2.1% react/valibot/drizzle/excalidraw/
   tanstack-router/zod, vue -2% (plan 48 has the details and what's left for session B).
4. Done (execution, 2026-10-07): plan 48 stage 3 fan-out, suite **14712 / 14969** (98.3%, 0 lost); xstate 30 and
   valibot 31 now equal the commit's tsc; zod +4% instructions from a tsc-faithful merged-interface instantiation.
5. Done (execution, 2026-10-07): plan 48 stage 2 session B. Creation-order machinery deleted (suite and corpora
   unchanged, instructions -0.1..-0.7%), then a 4-agent fan-out: suite **14804 / 14969** (98.9%), `48_lost.txt` 22 ->
   13 (all with triage notes), corpora unchanged, instructions +0.2-0.6% vs the session start.
6. Done (execution, 2026-10-07): plan 48 stage 4. Sweep **8594 / 8595**, `$ExpectType` **68620 / 68640** (both above
   the last 5.8 numbers), scenarios **240 / 0**, mutation seed 2 34 / 4051; suite 14804, corpora and instructions flat.
   Flagflip moved to entry 7 (its cache is per corpus pin).
7. Done (execution, 2026-10-07): plan 48 stage 5, every corpus re-pinned or re-checked (tsc 4d4f005c 0 on all; tsgo
   7.0.2 0 except webpack/vue/drizzle/effect, recorded), flagflip regenerated (182 cells, 116954 tsc diagnostics). Plus a
   first drain: type-fest's SIGSEGV (64 MB stack), vscode 414 -> 8, webpack 113 -> 64, typescript-eslint 4 -> 0,
   flagflip missing 1583 -> 260. tttt now: type-fest 6, valibot 4, webpack 64, effect 3, vscode 8, excalidraw 1, the
   rest 0 (zod-root = tsc's 53). Suite 14804; sweep and `$ExpectType` unchanged.
8. Done (execution, 2026-10-07): corpora drain with 4 sub-agents (~70 picks). Every corpus 0 but effect 3 (plan 19)
   and vscode 1 (stateToProgressAdapter, unreduced); flagflip missing / spurious / other 260 / 90 / 10 -> 42 / 12 / 5;
   then mutation seeds 11, 13, 14 and multi/js 12 drained, `$ExpectType` 68621, suite 14805. Instructions -0.6..+1.0%.
   New plan 49 (destructured references and widening, audit stages); leftovers in triage.md.
9. Done (execution, 2026-10-07): plan 48 stage 6. Instructions identical to entry 8's end (vscode 92.151 G);
   `./bench.sh` on every re-pinned corpus: tttt 3.0x (vscode, 6.2 s / 5.8 GB vs 18.3 s / 8.7 GB) to 13.9x (type-fest)
   faster than multi-threaded tsgo 7.0.2, less memory everywhere; work volume within ~15% of tsc's. No code changes.
10. Done (triage, 2026-10-07): triage.md drained by 4 sub-agents (+ second tasks: mutation seeds 16 and 17, the last
   vscode error, flagflip, named error types): ~70 fixes, suite **14829 / 14969** (0 lost), **vscode 1 -> 0** (every
   corpus 0 but effect 3), flagflip 42/12/5 -> 18/10/4, `$ExpectType` 68628, sweep 8594; instructions -0.7..+1.1%.
   New: plan 50 (tsc's export= / CommonJS symbol clones), bugs/binder_modules.md, bugs/tsgo_divergences.md.
11. **Insights.**
