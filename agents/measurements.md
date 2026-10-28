# Measurements
The latest of each kind of measurement. **Replace a section when re-measuring** (keep its date and commit); don't
append history, git has it. Older sections (oracle tallies, stage logs, superseded perf tables) were
condensed on 2026-10-20 into "Past conclusions" below; the full log is `git show a9c5e07da:agents/measurements.md`.

## Machine and caveats
AMD EPYC-Turin, 16 vCPUs = 8 cores x 2 SMT threads (KVM, shared with other agents: load often 10-40), 60 GB
(4 vCPUs and 31 GB until about 2026-10-20: older sections say which); Ubuntu 26.04, GCC 15.2, node 22.22. The oracle (tsc)
is TypeScript 7.1.0-beta (`v7.1-beta` = 3e923636, `thirdparty/typescript`; every cache since 2026-10-23; the
2026-10-08 nightly fed0bf24 before). `scripts/bench.sh` compares against the pinned tsgo only (since 2026-10-12). Wall times drift by ~15% between days (a shared machine) and anything running alongside skews
them: compare walls only within one sitting. Callgrind instruction counts (`valgrind --tool=callgrind ./release/tttt
-q --keep-going -p DIR`) are the stable measure across days. Neither side is clean on webpack, drizzle, vue, effect and
vscode (exit 2): the same (file, line, code) on both (realworld.py), so equal work (and the same
text since 2026-10-18 but webpack's TS5115 name order, `scripts/clidiff.py --corpora`). **The 12-core machine** (the human's
Framework laptop, Ryzen AI 9 HX 370, 12 cores / 24 threads) is measured only when the human runs something
there (`nproc` 24): its section below; it is the one to read for core scaling.

## How to measure (notes that still hold)
- **Instructions**: `scripts/instrcount.sh` (callgrind, K1 T1, fast/tttt since 92828ad6e (pgo/tttt 2026-10-19 ..
  10-22): `fastbuild.sh --measured`; saved in `test/instrcount.txt`, vscode 66.37 G there at d7bc837b0). Callgrind counts run ~3% above `perf stat -e instructions:u`: don't compare across tools. At
  K1 the counts include recycling's warm-ups (type-fest, drizzle, vscode recycle at 256 MB): compare with
  `--checker-memory off` for the checker's own work. instrcount gives valgrind a 64 MB main stack (type-fest's
  IntRange needs ~16 MB). A worktree with symlinked corpora can measure some corpora differently (nest +8% once):
  measure in the main tree.
- **bun**: `npm pack @oven/bun-linux-x64@1.4.3` (canary: `@oven/bun-linux-x64@1.4.3-canary.20261019.1`, see below),
  `scripts/bench.sh --bun PATH`. Run `bun check` from an empty directory (`perfsweep.py --bun` does): in a corpus whose
  package.json has a `check` script, `bun check` runs the script. Bun's output is its own format and not tsc's.
- **tsc-rs** (pingdotgg/ts-rust, a Rust port of tsgo at our pin fed0bf24): `scripts/bench.sh --tscrs PATH`.
- **Peaks**: `/usr/bin/time` (no Python parent). `--time` prints RSS after parse / after names, arena and types.
  Recycling's peaks wobble +-10 MB with the sawtooth's phase.
- **sys and CPU accounting: `TTTT_SYNC_EXIT=1`.** Since the exit reaper (main.c exit_detached(), wt-ktime2's
  35130c775) the kernel's address-space teardown (7-66 ms, nest to vscode) runs in a detached CLONE_VM child after
  the caller has the exit status: `/usr/bin/time` and wait4's rusage then undercount sys (and CPU) by it, and wall to
  exit is 3-6% shorter. Set `TTTT_SYNC_EXIT=1` when comparing sys, kernel cycles or CPU across builds (bench.sh takes
  a second, synced run per sample for CPU; perfshape.sh and perfsweep.py run synced and take wall to the last
  output byte of `--time`); leave it unset for the user-visible wall. Valgrind, sanitizers, debug builds and
  `TTTT_TEARDOWN` exit in the process anyway. MADV_POPULATE_WRITE counts as minor faults on this box's kernel and not
  in the laptop's `perf stat page-faults`: compare faults within one box.
- **perfsweep**: tsgo's side is cached cold (`test/perfsweep/tsgo.jsonl.gz`, incremental projects with their own
  `--tsBuildInfoFile`); its walls right after `oracles.py`'s other oracles are inflated: rerun alone. A project whose
  `.tsbuildinfo` is committed makes tsc skip checking (webcmd).

## Checker footprint at K1 (2026-10-28, worker foot, 7713f7dfa -> 796d25fb7; this box, release and fast builds)
Census and method: agents/notes/footprint.md. Arena K1 (`--time`) imm(utable.d.ts one-file) 227 -> 217 MB, zod 284 ->
265, drizzle 299 -> 271, effect 298 -> 287, store.d.ts 144 -> 143, type-fest 475 -> ~451. Peak RSS K1 (fast builds,
/usr/bin/time) imm 277 -> 267, zod 458 -> 439, drizzle 506 -> 476, effect 740 -> 729 MB (bun --threads=1: 70, 211,
266, 336). cachegrind D1 read misses imm -2.4%, zod -1.3%, drizzle -0.5%; instructions (callgrind) drizzle -0.30%,
zod -0.36%. tsgo `--singleThreaded --extendedDiagnostics` Memory used: imm 346 MB, store 218, zod 616, drizzle 589,
effect 834.
On the human's laptop (2026-10-27, dc8d517a0, Zen 5 big core pinned, K1 T1, medians of 5; footprint.md has the
table): cycles 7713f7dfa -> dc8d517a0 imm -3.0%, store -0.4%, zod -1.1%, drizzle -1.7%, effect -1.1%; against bun
`--threads=1` tttt takes 1.42x bun's cycles on imm, 1.21x on store, 0.54x zod, 0.55x drizzle, 0.50x effect.

## Oracle totals (2026-10-23 evening)
The merger's full `scripts/oracles.py` in wt-gate at **37d832827** (main after the day's first correctness batch,
11:39; before the `bun*` suite tests and the afternoon's fixes), finished 13:07 on the loaded box (clidiff alone 997 s);
the next batch's run (at 03bf32424) was not complete when this was written. Then `python3 scripts/bugcount.py` and
`scripts/passcount.py` at 81bc6b5d4 (main, 14:16), which read the newest caches (wt-gate's, ~1 h old).
- Suite 14198 passing (tsc and tttt's own tests), 0 lost, 0 gained against HEAD; `--no-defer-bodies` 0 newly
  failing; `--checker-memory 0` 0 of 17921 outputs differ. At 81bc6b5d4 with the bun tests (`passcount.py`): **15177 /
  16590** (errors 15266, symbols 16312, types 15891; dts 1908 / 1937); failing by origin (bugcount): tsc 53 / 13706,
  own 8 / 552 (each a known-failing repro), bun 1352 / 2332.
- Corpora: every one 0/0 but vue 0/2 (tsgo's TS5115 runaway artifact). DT sweep 8595 / 8595. `$ExpectType` 68640 /
  68640. flagflip missing 16 / spurious 2 / other code 2 (all vue pluginScoped.ts).
- bun cases (`scripts/buncases.py`): match 1278 / 2411 (53.0%), byte-identical 1072; spurious 312, missing 489, both
  325, crash 7, K-dependent 1.
- Source sweep `--sample 4`: **1458 / 1464** projects (345 clean under tsc), 218 / 222 repos; spurious 1, missing 2,
  both 2, timeout 1. (bugcount's later quarter-sample cache, wt-gate-sample4's: 16 / 1464 mismatching, 4 keys, all
  written up; openclaw 36 / 49 configs match with 13 timed out.) The full sweep's last run (main + wt-cor8, midday):
  6053 / 6085 projects, 334 / 346 repos ("Fresh repositories, fifth rule").
- `-b` sweep 418 / 426 projects, 77 / 84 repos (e342649f8 since: 419 + 2 known, atproto and b4run, tsgo's build cache
  bug: bugs/sourcesweep_build.md).
- dtsdiff **1497 / 1503** projects, 129347 / 129362 files (+1 project, 6 files as tsgo `--singleThreaded`).
- clidiff 4450 projects: **match 4417**, text 14, order 0, exit code 0, diagnostics 19, crash 0; corpora 16 / 17 (vue).
- kdiff (K1 against K16): suite 0 files; sources **7 / 4436** (openclaw/packages, openclaw/packages/plugin-sdk,
  openclaw/src/agents/sessions, openclaw/src/config: new with openclaw; rxjs/packages/rxjs, sequelize,
  sequelize/packages/core: tsgo-shared). Recycle 3 / 4436 (rxjs, sequelize x2), 21 timed out.
- perfsweep 2215 projects: median check ratio 0.24, worst types ratio 1.73 (mcp-linker).
- **Known correctness bugs** (bugcount at 81bc6b5d4): 162 root causes open (11 designs), 72 text-only, 36 argued tsgo
  artifacts excluded; 429 tagged entries (62 fixed, 32 dup, 40 notes, 25 harness; 11 of the open in triage.md); 530 of
  bun's 532 keys unexplained (largest: -TS2322 252, -TS2509 184, -TS2339 177, +TS5114 146, +TS2322 127, +TS2339 127,
  +TS2842 126, +TS7022 112). The day: 156 at 2f0cc14a3 (13:20, the first headline) -> 162 (new write-ups outpacing
  the flips).
- Instructions: not re-saved today; `test/instrcount.txt` is a6af311ed's (9894ae8e5). The per-fix quick gates
  measured drizzle +0.42..+0.58%, zod +0.18..+0.23% against it during the day (~+0.8% on drizzle by the end, pending
  attribution); the escaped-keyword commit's +1.05% on nest was taken back by 9a5647e3b (+0.08%).

## Large projects at the defaults (2026-10-23)
The question: do users (tttt's defaults: threads = cores, a checker per physical core, the memory pool) see
divergences the sweep (`--threads 1`, one checker) can't? Measured at **da9e17675** (release build) on this box: 16
vCPUs, load 31-44 from other agents, ~27 GB available (so the default pool never recycled). The projects: the 255
installed source-sweep projects with a cached tsgo time over 5 s (`test/sourcesweep/tsc.jsonl.gz` `ms`), openclaw and
opencode aside (owned elsewhere). Each run `tttt --pretty false --noEmit -p P` from thirdparty/sources through
tttt_run (mem_gb 16, nice 10, one project at a time), compared with tsgo's cache by (file, line, code) and with each
other line by line. The script and raw data: the worker's scratchpad `big/measure.py`, `m1.jsonl` (K1 + 3 defaults),
`m2.jsonl` (2 runs at the defaults with `--checker-memory 1024`), `cwd.py` (run from the config's directory).

| class | projects |
|---|---|
| (a) match tsgo in all 4 runs (K1 + 3 defaults) | **248** (92 of them tsgo-clean: all 92 clean in tttt) |
| (b) K1 matches tsgo, the defaults don't | **0** |
| (c) the defaults vary run to run | **1**: sequelize (below) |
| (d) diverge alike at K1 and the defaults (all already in the sweep's list) | **6** |

| project | tsgo | K1 | defaults | class | (file, line, code) differing |
|---|---|---|---|---|---|
| sequelize | 3390 | 3390 | 3389-3390 | c | 1 run in 3: +core/src/sequelize-typescript.ts:335 TS7022 ('models'), -:1053 TS2322, -:1080 TS2488 |
| sunmao-ui | 3067 | 3064 | 3064 | d | -TS2590 chakra-ui-lib Button.tsx:85, Dialog.tsx:160, editor AddTraitButton.tsx:59 |
| portabase | 52 | 51 | 51 | d | -TS2322 src/components/ui/form.tsx:227 |
| monkeytype/frontend | 2 | 4 | 4 | d | +TS2578 SearchableAutoSetting.tsx:56, +TS2322 :59 |
| better-auth/packages/better-auth | 4418 | 4416 | 4416 | d | -TS2345 lynx-store.ts:65, react-store.ts:67 (notes/reentrancy.md C/D) |
| pothos/tsconfig.eslint.json | 7271 | 7270 | 7270 | d | -TS7023 plugin-smart-subscriptions/src/cache-node.ts:41 |
| hono/tsconfig.spec.json | 6 | 7 | 7 | d | +TS2769 src/request.ts:214 |

Also: the root `openclaw` project hits the 16 GB address-space limit at K1 and at the defaults (status memory).
Recycling forced (`--checker-memory 1024`, 2 runs each; dokploy/apps/dokploy recycles each checker twice): **0 / 255**
differ from K1. Run from the config's own directory (`cd DIR && tttt -p .`): 0 / 255 differ (path spelling aside).
Conclusion: on the sweep's projects the defaults, recycling and the invocation add nothing the K1 sweep doesn't see;
the human's "over half of large tsgo-clean projects" (private projects, single-threaded too, per the coordinator) is
a training-set gap, not a checker-count one: the measure is fresh large repositories (vision.md).

## The one-checker rerun on history-dependent diagnostics (2026-10-23, det3, release builds, this box, load 7-40)
Trigger: a limit (TS2589/5114/5115/2590/2321/2859) or a circularity (TS2615/7022/7023/7024/2456/2502) reported by a
run with several checkers or a recycled one; program_typecheck() checks again in one checker (documentation.md).
- How often: tsgo's caches report a limit in 17 of the source sweep's 6852 configs, a circularity in 111 (77 nest
  samples, mostly TS7023), 0 of DT's 8595, 138 of bun's 2411 cases; tttt reruns 15 + 108 of those sweep configs.
- Cost at the defaults, best of 5 (load ~7 for the first two, ~37 for the rest), before -> with the rerun:
  vue corpus 0.13 -> 0.67 s (peak 424 -> 436 MB), better-auth/packages/better-auth 0.14 -> 0.48 s (528 -> 529 MB),
  inbox-zero 0.99 -> 2.03 s (756 -> 781 MB), hop-protocol 0.81 -> 2.30 s (1192 -> 1207 MB), soybean-admin 0.59 ->
  1.60 s. The 111 circularity configs one run each: 50.5 -> 60.6 s in total.
- Reusing the parsed program (0724c2984) against the CLI restart (7ca8d73ea): vue 0.73 -> 0.67 s, better-auth 0.56 ->
  0.48 s: the one-checker check is most of the cost.
- kdiff -k 16 after it (det3, 1ee362e69 + docs): sequelize, rxjs and openclaw's configs, 190: only rxjs/packages/rxjs
  differs (elaboration text, decided to stay). openclaw at -j 1, 1200 s, 16 GB: 0 / 160 differ; unmeasurable at that
  budget: `openclaw` (memory at both K) and openclaw/src/infra (K16 killed). vision.md's "kdiff 7 / 4436 (openclaw x4,
  rxjs, sequelize x2)" is now rxjs alone plus those two.

## Random npm projects (scripts/npmsample.py; agents/notes/npm_sampling.md)
`scripts/npmsample.py --seed S` (50 projects per seed, the first accepted in sample order; tsgo 7.1 via tttt-oracle,
tttt at its defaults, both sandboxed; records in test/npmsample/S.*; divergences in agents/bugs/npmsample.md).

| seed | date | tttt | byte-identical (mod lib paths) | same (file, line, code) | agree on clean |
|---|---|---|---|---|---|
| 20261023 | 2026-10-23 | 4dcca0d89 (main) | 44/50 | 46/50 | 49/50 |
| 20261023 | 2026-10-23 | 8ec4e47a4 (+2 fixes) | 46/50 | 48/50 | 49/50 |
| 20261029 | 2026-10-23 | 71ec34371 (vobs: +daf488efd, ef9b44945) | 55/57 | 55/57 | 56/57 (stencil's TS2320 flip) |

Seed 20261023 differences at 8ec4e47a4 (all persist at --threads 1):

| project | tsgo | tttt | missing | extra | codes |
|---|---|---|---|---|---|
| AkilSaji/firelab | 0 | 1 | 0 | 1 | +TS2538 (the clean disagreement) |
| overture-stack/arranger modules/sqon | 40 | 41 | 2 | 3 | -TS2345 +TS2322 |
| axiona/uri | 52 | 52 | 0 | 0 | text: TS2416 base type alias vs expanded |
| alex-korneyko/ngx-summernote-mod | 46 | 46 | 0 | 0 | text: TS2769 elaboration unknown[] vs (string \| false)[] |
| (fixed) JohnDeved/zooqle-api-ts | 22 | 19 | 3 | 0 | -TS2345 -TS2339 -TS2322 (68bd2051d) |
| (fixed) vijay8856/react-native-find-background-location-vd | 2 | 4 | 0 | 2 | +TS5023 (8ec4e47a4) |

## Known correctness bugs
`scripts/bugcount.py --save`, one line per run (per batch): the open root causes from the tags of agents/bugs/*.md and
triage.md (agents/bugs/README.md), then the symptom counts it read (suite_*: failing tests by origin; bun: bun cases
mismatching; dt, source: projects mismatching; expect: $ExpectType assertions; dts: dtsdiff projects; flagflip:
diagnostics; clidiff_text: projects; unexplained_*: the root-cause keys of each oracle no entry mentions). The oracle
numbers are those of whichever tree last ran each oracle (bugcount.py prints which).
- 2026-10-23 00ba060b2: 156 open (11 designs), 72 text-only, 36 tsgo excluded; symptoms: suite_tsc 53, suite_own 8, suite_bun 1352, bun 1091, dt 0, source 6, expect 0, dts 6, flagflip 20, clidiff_text 14, unexplained_sweep 0, unexplained_sourcesweep 8, unexplained_buncases 533
- 2026-10-23 81bc6b5d4: 162 open (11 designs), 72 text-only, 36 tsgo excluded; symptoms: suite_tsc 53, suite_own 8, suite_bun 1352, bun 1072, dt 0, source 16, expect 0, dts 6, flagflip 20, clidiff_text 14, unexplained_sweep 0, unexplained_sourcesweep 0, unexplained_buncases 530
- 2026-10-23 17c1719b2: 162 open (11 designs), 72 text-only, 36 tsgo excluded; symptoms: suite_tsc 53, suite_own 8, suite_bun 1352, bun 1072, dt 0, source 16, expect 0, dts 6, flagflip 20, clidiff_text 14, unexplained_sweep 0, unexplained_sourcesweep 0, unexplained_buncases 530
- 2026-10-24 8745dfb87: 294 open (23 designs), 79 text-only, 38 tsgo excluded; symptoms: suite_tsc 62, suite_own 13, suite_bun 1135, bun 886, dt 0, source 7, expect 0, dts 6, flagflip 20, clidiff_text 22, unexplained_sweep 0, unexplained_sourcesweep 1, unexplained_buncases 129
- 2026-10-24 f8d57e9b9: 294 open (23 designs), 81 text-only, 38 tsgo excluded; symptoms: suite_tsc 63, suite_own 13, suite_bun 1135, bun 886, dt 0, source 7, expect 0, dts 6, flagflip 20, clidiff_text 22, unexplained_sweep 0, unexplained_sourcesweep 1, unexplained_buncases 129
- 2026-10-24 45397f75f: 309 open (27 designs), 82 text-only, 38 tsgo excluded; symptoms: suite_tsc 67, suite_own 15, suite_bun 1133, bun 879, dt 0, source 3, expect 0, dts 6, flagflip 23, clidiff_text 34, unexplained_sweep 0, unexplained_sourcesweep 0, unexplained_buncases 129
- 2026-10-28 2625572b5: 298 open (27 designs), 84 text-only, 38 tsgo excluded; symptoms: suite_tsc 63, suite_own 16, suite_bun 1131, bun 878, dt 0, source 5, expect 0, dts 6, flagflip 23, clidiff_text 32, unexplained_sweep 0, unexplained_sourcesweep 1, unexplained_buncases 128
- 2026-10-28 ed02bc573: 303 open (28 designs), 84 text-only, 38 tsgo excluded; symptoms: suite_tsc 62, suite_own 17, suite_bun 1114, bun 861, dt 0, source 3, expect 0, dts 6, flagflip 23, clidiff_text 32, unexplained_sweep 0, unexplained_sourcesweep 0, unexplained_buncases 128

## vscode since v1: the regression attributed (2026-10-29, worker regress; v1 438727981 against main 02ebfb96a)
The human's laptop (24 threads, 3 runs each after a warm-up): v1 against main at ~8c9afd512.

| | v1 | main | bun |
|---|---|---|---|
| 1 thread wall | 12.19 s | 13.32 s (+9.3%) | 16.21 |
| 1 thread peak RSS | 5.0 GiB | 4.7 GiB | 1.8 |
| all cores wall | 2.02 s | 2.24 s (+11%) | 2.33 |
| all cores CPU | 38.6 s | 38.9 s | 48.6 |
| busy cores | 19.1 | 17.4 | 20.9 |

**This box** (THP `madvise`, load 5-40 from other workers): `--fast` builds out of tree (`scripts/abbuild.sh REV --fast`),
`TTTT_SYNC_EXIT=1`, vscode `src/tsconfig.corpus.json --noEmit`. Builds: `main+huge` = main with 4f3fa414e and 24820df46
reverted (huge text page on, arena/AST huge pages behind `TTTT_HUGEPAGES=budget|1` as after 45397f75f), `main-a5a0` =
main with a5a0b7100's src reverted, `both` = the two together. **K1 under load: sequential runs on one core were
useless (+-20%, other workers on the SMT sibling); concurrent pairs on the two SMT siblings (CPUs 6 and 7, swapped
each round, so each run's neighbour is the other side) were steady to +-1%** (per-round ratios below; 4-6 rounds each).

| K1 pair (B against A) | wall A / B s | user A / B s | sys A / B s | B/A wall per round |
|---|---|---|---|---|
| main against v1 | 16.05 / 17.56 | 15.60 / 16.36 | 0.40 / 1.12 | **+8.8%** (+8.2..+9.9; +3.6% in a run at load 30) |
| main+huge (`=1`) against main | 15.34 / 14.50 | 14.40 / 14.13 | 0.93 / 0.39 | **-5.4%** (twice: -5.4, -5.7) |
| v1 against main+huge (`=1`) | 15.19 / 15.21 | 14.79 / 14.82 | 0.37 / 0.39 | **0.0%** (twice: 0.0, +0.1) |
| main+huge text page only against main | 17.66 / 17.60 | 16.46 / 16.43 | 1.11 / 1.12 | -0.2% (the text page: nothing here) |
| main-a5a0 against main | 16.81 / 16.69 | 15.84 / 15.67 | | -0.8% |
| v1 against both (`budget`) | 15.41 / 15.52 | 15.00 / 15.12 | 0.40 / 0.39 | +0.5% |

Defaults (16 threads, 6 interleaved rounds, load 13-20; medians; `--time` phases):

| | wall s (best) | user s | sys s | RSS GB | load+parse files ms | typecheck ms |
|---|---|---|---|---|---|---|
| v1 | 1.59 (1.48) | 17.02 | 0.60 | 5.49 | 179 | 1174 |
| main | 1.85 (1.75) | 17.56 | 1.21 | 5.05 | 195 | 1281 |
| main-a5a0 | 1.85 (1.67) | 17.46 | 1.23 | 5.04 | 182 | 1236 |
| main+huge (`=1`) | 1.56 (1.50) | 16.88 | 0.60 | 5.12 | 152 | 1203 |
| both (`=1`) | 1.50 (1.44) | 16.71 | 0.60 | 5.11 | 164 | 1145 |
| both (`budget`) | 1.52 (1.41) | 16.72 | 0.59 | 5.13 | 149 | 1155 |

(An earlier set at load 30-40: load+parse files v1 ~360 ms, v1 with `TTTT_HUGEPAGES=0` ~500, main ~480: the front
end's loss is the AST slabs' 4 KB faults, 59k -> 166k in that phase.)

Instructions (callgrind, K1, `--fast`): **v1 66.665 G, main 68.369 G (+2.56%), main-a5a0 66.171 G**: a5a0b7100 is
+2.198 G (+3.3%), the rest of the 171 commits -0.74% together (front end and loader cheaper; the intern tables and
lazy props move instructions both ways). Check-only node links +169k (4.5%) are ffba37893's per-function-expression
`KEY_SIGNATURE_DECL_CHECKED` link (cheap). RSS fell 4.48 -> 4.23 GB at K1 independently of huge pages (v1 with them off
is 4.47).

**Attribution.**
1. **Huge pages off (45397f75f, 2026-10-24) and removed (4f3fa414e)**: the whole wall loss on this box. With
   THP `madvise` nothing gets a huge page any more: typecheck faults at K1 25k -> 800k, sys 0.38 -> 0.93 s, plus the
   TLB misses (user +2%); at the defaults the AST's faults make the parallel parse slower and the checkers' faults
   cost sys 0.6 -> 1.2 s. Restoring them (`TTTT_HUGEPAGES=1` or `budget` on the reverted build) gives v1's wall back
   at K1 and at the defaults. The laptop runs THP `always` (its K1 runs fault only 3-5k times on zod/drizzle at
   2c389b213, so its arenas still get huge pages): there this part is probably small, and df95ec2f's laptop A/B
   (huge pages within noise) agrees.
2. **The huge text page's removal (24820df46)**: no effect on this box (EPYC Turin VM), but the laptop measured it at
   -4.6% wall / -3.6% cycles at K1 and -1.2% at the defaults (section "The 12-core box at 277be4bc"). The removal
   cited df95ec2f's A/B, which compared the arena pages only (`--huge-pages`) with the text page on in both arms: the
   text page's removal itself was never measured. Probably the laptop's largest share; unconfirmed (no laptop here).
3. **a5a0b7100 (the return mapper's own inference when candidates would combine)**: +3.3% instructions, all in one
   file: vs/platform/agentHost/test/node/mapSessionEvents.test.ts, 146 `fusionTestEvent<K extends SessionEventType>(...)`
   calls whose return type `SessionEventPayload<K>` is inferred against the big SessionEvent union twice (K gets many
   candidates, so the shortcut doesn't apply): that file 188 -> 357 ms at K1 (infer_to_union's infer_from calls
   2.57M -> 4.77M). Only -0.8% wall at K1, but at the defaults the file is the check phase's last to finish (critical
   path: check ends 1240 ms against ~1150 without it), which fits the laptop's busy cores 19.1 -> 17.4 at the same CPU.
   tsc does the same second inference (tsgo's inferTypeArguments always makes the return mapper's context).
So the laptop's +9.3% at K1 is most likely the text page (~4.6%) + a5a0b7100 (~1%: its share of instructions there
may cost more cycles) + something small; at the defaults a5a0b7100's critical path (~0.1 s) + the text page (~1.2%).
Leads: bugs/perf_leads.md "vscode since v1".

**Recovered (2026-10-29, worker rehuge, this box, `--fast`, load 3-10)**: huge pages back by the THP settings
(b5c829162) and the return mapper without a second walk (4a8829f72). vscode main e73c517ff against the branch: K1
(sibling pairs, 4 rounds) 15.36 -> 14.41 s (-6.2%), sys 0.98 -> 0.38, faults 1035k -> 79k; defaults (8 interleaved)
1.58 -> 1.27 s (-19%), user 17.20 -> 16.61, sys 1.22 -> 0.60, RSS 5.05 -> 5.12 GB. The return mapper alone (both at
TTTT_HUGEPAGES=0): defaults 1.57 -> 1.49 s (-4.8%); huge pages alone: kernel_time.md's table. Instructions (callgrind
K1): a tsconfig over mapSessionEvents.test.ts 7.004 -> 4.697 G, zod -0.21%, drizzle -0.55%, effect -0.20%, react -0.18%.

## The 12-core box at 2c389b213: type graph step 1 and the intern tables (2026-10-29)
The human's laptop agent (Ryzen AI 9 HX 370; the box not idle: Firefox and other sessions, load 1-5 at K1, up to 13 at
the defaults; a few outlier runs up to +33% on zod, medians steady to a few %). BASE 6d5e31b4c, MID c28559e09 (type
graph step 1: lazily made instance props, notes/type_graph_design.md), NEW 2c389b213 (MID + 8-byte intern slots,
notes/intern_tables.md), all `--fast` (PGO + BOLT); output byte-identical across the three on every project. imm and
store as in notes/footprint.md. K1: `taskset -c 2`, `--checkers 1 --threads 1`, medians of 5 interleaved runs, % against
BASE; DRAM fill = `ls_dmnd_fills_from_sys.dram_io_all`, L3 fill = `.local_ccx`.

| | cycles M | instr M | L1D miss M | L3 fill M | DRAM fill M | faults | RSS MB |
|---|---|---|---|---|---|---|---|
| imm base | 1147 | 2376 | 49.3 | 4.9 | 2.0 | 2995 | 278 |
| imm mid | 1144 (-0.3) | 2356 (-0.8) | 49.7 (+0.8) | 4.7 (-4.2) | 1.9 (-5.3) | 3012 | 272 |
| imm new | 1107 (-3.5) | 2373 (-0.1) | 48.5 (-1.6) | 4.6 (-6.8) | 1.6 (-19.5) | 3303 | 256 |
| store base | 2035 | 3050 | 45.3 | 4.1 | 4.1 | 3393 | 278 |
| store mid | 2049 (+0.7) | 3040 (-0.3) | 45.1 (-0.4) | 4.1 (-1.2) | 4.1 (+1.1) | 3214 | 278 |
| store new | 1934 (-4.9) | 3115 (+2.1) | 40.7 (-10.2) | 4.0 (-3.2) | 3.3 (-18.1) | 3241 | 242 |
| zod base | 2306 | 3435 | 67.5 | 6.7 | 4.1 | 3971 | 446 |
| zod mid | 2270 (-1.6) | 3391 (-1.3) | 63.1 (-6.4) | 6.5 (-2.8) | 3.8 (-7.6) | 4035 (+1.6) | 392 (-12.2) |
| zod new | 2237 (-3.0) | 3426 (-0.2) | 61.6 (-8.7) | 6.4 (-4.9) | 3.5 (-15.7) | 4846 | 377 |
| drizzle base | 2897 | 4261 | 77.9 | 7.8 | 5.0 | 3827 | 484 |
| drizzle mid | 2901 (+0.1) | 4255 (-0.2) | 76.4 (-2.0) | 7.7 (-0.7) | 4.8 (-4.8) | 3856 | 459 |
| drizzle new | 2846 (-1.8) | 4307 (+1.1) | 73.8 (-5.3) | 7.8 (+0.3) | 4.5 (-10.7) | 4862 | 425 |
| effect base | 4816 | 6923 | 138.3 | 13.1 | 7.9 | 4894 | 675 |
| effect mid | 4845 (+0.6) | 6902 (-0.3) | 137.3 (-0.7) | 13.2 (+0.8) | 8.0 (+1.2) | 4908 | 661 |
| effect new | 4789 (-0.6) | 6962 (+0.6) | 135.1 (-2.3) | 13.8 (+5.1) | 7.7 (-2.9) | 4867 | 612 |

MID -> NEW (cycles / instructions / L3 fills / DRAM fills / RSS): imm -3.2 / +0.7 / -2.6 / -15.0 / -5.8%; store -5.6 /
+2.5 / -2.0 / -19.0 / -13.2%; zod -1.5 / +1.1 / -2.1 / -8.7 / -3.7%; drizzle -1.9 / +1.2 / +1.0 / -6.2 / -7.4%; effect
-1.1 / +0.9 / +4.3 / -4.0 / -7.5%.

Defaults (no taskset, 10 interleaved runs, BASE -> NEW): drizzle cycles 6224 -> 5885 M (-5.5%), instructions 8103 ->
8081 M, RSS 1121 -> 978 MB (-12.7%), wall about -5% (timed without perf stat: base 0.22-0.25 s, mid 0.22-0.23, new
0.21-0.23); vscode cycles 82676 -> 80531 M (-2.6%), instructions 71577 -> 71727 M, RSS 5502 -> 5220 MB (-5.1%), wall
1.92 -> 1.95 s (noise). Attribution on store (`perf record -e ls_dmnd_fills_from_sys.local_ccx`, by symbol): the
intern share of L3 fills 10.4% on MID (intern_composite_ex 5.1, intern_slot 3.3, intern_put 1.6, intern_grow 0.5),
7.6% on NEW (intern_slot 5.7, intern_composite_ex 1.0, intern_put 0.9); elsewhere on NEW ty_alloc 5.0%, ty_realias
3.6%.

Verdicts by the stop rules:
- **Type graph step 1 (BASE -> MID) misses its bars; step 2 is not started.** zod: cycles -1.6% (bar -1%: passes), DRAM
  fills -7.6% and faults +1.6% (bar -10.2% each: miss); drizzle cycles +0.1% (bar -4.6%); effect's instruction cost
  doesn't show (instructions -0.3%, cycles +0.6%). Faults are only ~3-5k a run on this box and don't track memory;
  RSS does (zod -12.2%, drizzle -5.2%). Step 1 stays landed as a memory win; why the fills fell less than the arena:
  notes/type_graph_design.md §4.
- **Intern tables (MID -> NEW) kept.** The first clause misses (store's L3 fills -2.0%, DRAM fills -19%, not halved),
  the second holds: cycles fall on all five (-1.1..-5.6%) against instructions +0.7..+2.5%. Open: NEW has +20-26% page
  faults on zod and drizzle (~4.0k -> 4.8k), small in absolute terms, RSS still falls (notes/intern_tables.md).

## The 12-core box at df95ec2f: `--huge-pages` off (default) vs on, kernel profile (2026-10-27)
Run by an agent on the human's laptop (Ryzen AI 9 HX 370, 24 threads, kernel 7.2.11-zen1, THP `always`, defrag
`defer+madvise`, desktop load 1.3-2). fast/tttt (copied out), `TTTT_SYNC_EXIT=1`, one warm-up each, then 5
alternating off/on pairs, medians; `perf stat -e cycles:u,cycles:k,instructions:u,page-faults,dTLB-load-misses`
under `/usr/bin/time -v` (minor faults, peak RSS). `perf_event_paranoid` lowered to 1 for cycles:k.

| corpus | wall s | cycles:u G | cycles:k M | k/u | minor faults | dTLB-load-misses | peak RSS MB |
|---|---|---|---|---|---|---|---|
| drizzle off | 0.25 | 6.09 | 499 | 8.0% | 45.7k | 482k | 1180 |
| drizzle on | 0.23 | 6.05 | 424 | 7.0% | 13.0k | 376k | 1202 |
| vscode off | 1.57 | 78.0 | 3096 | 4.0% | 166k | 5.55M | 5629 |
| vscode on | 1.56 | 78.4 | 3148 | 4.0% | 44k | 5.03M | 5651 |

- The flag cuts minor faults 3.6x and dTLB misses 10-22%, but wall and cycles:u are within noise on both (ranges
  overlap); kernel cycles drop ~75 M on drizzle (~1% of user), none on vscode. **The opt-in huge-pages path has
  nothing left to buy here.** Kernel/user at the defaults (4% vscode, 8% drizzle) is at or below 3a77c827's 6-9%
  measured with huge pages still on by default.
- perf's `page-faults` (6.8k drizzle, 21k vscode) is 6-8x below rusage's minor faults and doesn't move with the
  flag; rusage is the number that responds.
- Kernel profile, drizzle, defaults (`perf record -e cycles:k -g`, `--no-children --sort symbol`): `clear_pages`
  29.8%, `get_page_from_freelist` 5.4%, the rest of faulting/allocation (anon pte, memcg charge, LRU, prep_new_page)
  ~10%; spinlocks ~4.7% (`rwsem_down_write_slowpath` 0.5%: mmap_lock); TLB shootdowns ~3.7%
  (`smp_call_function_many_cond`, `flush_tlb_func`); futex and mmap/munmap not in the top 40. So zeroing fresh pages
  is ~2.4% of user cycles; huge pages don't avoid it (a 2 MB page is zeroed too); touching less fresh memory
  (reusing arena memory) is the remaining lever. `strace -c -f` on the same run: 2617 madvise calls (24% of
  syscall time), 2146 futex, 382 mmap, 69 munmap; where the madvise calls come from wasn't looked at.

## 2 MB-aligned huge arena chunks: A/B on this box (2026-10-23, on 9894ae8e5)
arena_alloc_chunk() (src/ty/checker.c) mapping a huge-page chunk as whole, aligned 2 MB pages (agents/notes/
kernel_time.md item 5). fast/tttt before and after, `scripts/abbench.sh` (interleaved, `TTTT_SYNC_EXIT=1`, medians,
the defaults; drizzle/zod/nest 15 runs, vscode 7) on a box loaded 24-37 by another project's builds: walls are noise
(none regressed; vscode's -32% is load), faults and sys are the signal. Instructions flat (callgrind K1 drizzle
-0.02%, zod +0.07%, nest +0.03%).

| corpus | wall s | sys s | minor faults | peak RSS MB |
|---|---|---|---|---|
| drizzle | 0.871 -> 0.852 | 0.132 -> 0.106 (-20%) | 72.5k -> 44.0k (-39%) | 884 -> 890 (+0.7%) |
| zod | 0.610 -> 0.591 | 0.103 -> 0.083 (-20%) | 62.5k -> 39.8k (-36%) | 657 -> 656 |
| nest | 0.233 -> 0.210 | 0.065 -> 0.062 | 31.9k -> 31.5k | 172 -> 175 (+1.7%) |
| vscode | 4.12 -> 2.80 | 0.713 -> 0.628 (-12%) | 301k -> 185k (-39%) | 5185 -> 5185 |

## The 12-core box at 3a77c827: kernel time (before f903d2e0e vs after) (2026-10-22)
Run by an agent on the human's laptop (Ryzen AI 9 HX 370, 24 threads; kernel 7.2.11-zen1, perf 7.2.11, THP `always`):
AC, `balanced`, 98% idle before, but Firefox, a qemu VM, a Java process and `odm` open (load ~1 settled; ~9 right
after the builds, so part 1 started on a load still decaying). No code change. Two builds by `scripts/fastbuild.sh`
(PGO + BOLT): **before** = f903d2e0e (a worktree in /tmp), **after** = 3a77c827 (the repo): the kernel-time batch
(stack pool, glibc M_MMAP_THRESHOLD 32 MB / M_TRIM_THRESHOLD 1 GB, source texts kept mapped, huge-page big tables;
notes/kernel_time.md). Idle background: TLB shootdown interrupts ~55 per 10 s, function-call interrupts ~2.2k per 10 s.

**1. perf stat at the defaults** (`--noEmit --pretty false -p P`, 24 threads; `perf stat -e
cycles:u,cycles:k,instructions:u,page-faults`, one run per call, 5 per binary, the binaries alternating (ABBA...) after
one warm-up each; medians but the wall's best / median; peak RSS from wait4; TLB and CAL = the `/proc/interrupts`
lines summed over CPUs, delta across each run, median (min-max)):

| corpus | | kernel / user cycles | cycles:u G | cycles:k G | instr:u G | faults | wall s best / median | peak MB | TLB per run | CAL per run |
|---|---|---|---|---|---|---|---|---|---|---|
| drizzle | before | 27.1% | 6.35 | 1.71 | 7.94 | 88.7k | 0.276 / 0.288 | 1062 | 427,650 (423k-442k) | 330,649 |
| | after | **8.5%** | 5.94 | 0.50 | 8.13 | **6.7k** | **0.227 / 0.231** | 1178 (+11%) | **8,816** (8.7k-9.0k) | 11,187 |
| zod | before | 26.7% | 4.11 | 1.10 | 4.95 | 57.9k | 0.186 / 0.190 | 829 | 264,048 (249k-279k) | 207,498 |
| | after | **9.1%** | 3.86 | 0.36 | 4.91 | **9.6k** | **0.158 / 0.161** | 897 (+8%) | **9,498** (7.0k-9.8k) | 10,917 |
| effect | before | 21.6% | 6.79 | 1.47 | 7.56 | 76.1k | 0.276 / 0.278 | 913 | 378,956 (357k-395k) | 312,938 |
| | after | **6.8%** | 6.50 | 0.44 | 7.57 | **6.7k** | **0.225 / 0.228** | 978 (+7%) | **11,279** (10.3k-12.4k) | 13,646 |
| vue | before | 22.4% | 4.01 | 0.90 | 5.56 | 43.4k | 0.143 / 0.149 | 536 | 199,514 (198k-204k) | 165,112 |
| | after | **7.8%** | 3.79 | 0.30 | 5.55 | **5.6k** | **0.123 / 0.129** | 579 (+8%) | **10,345** (5.7k-13.4k) | 11,775 |
| react | before | 28.3% | 1.71 | 0.47 | 2.04 | 20.6k | 0.114 / 0.122 | 424 | 52,786 (46k-55k) | 45,652 |
| | after | **17.4%** | 1.77 | 0.31 | 2.04 | **4.4k** | **0.098 / 0.102** | 475 (+12%) | **2,647** (2.2k-3.3k) | 4,686 |
| nest | before | 30.0% | 1.06 | 0.32 | 0.99 | 5.5k | 0.055 / 0.055 | 295 | 6,801 (6.5k-7.7k) | 8,360 |
| | after | 27.5% | 1.09 | 0.30 | 0.99 | 2.6k | 0.052 / 0.055 | 339 (+15%) | 758 (655-1232) | 3,263 |
| vscode | before | 20.5% | 82.81 | 17.01 | 69.78 | 520.4k | 1.981 / 2.203 | 5325 | 4,433,402 (4.12M-4.86M) | 3,140,450 |
| | after | **6.4%** | 80.58 | 5.16 | 69.80 | **20.7k** | **1.666 / 1.704** | 5515 (+4%) | **53,484** (34k-66k) | 65,507 |

- **Confirmed: kernel cycles 21-28% of user -> 6.4-9.1%** on drizzle, zod, effect, vue and vscode (cycles:k -67 to -71%),
  TLB shootdowns per run **-95 to -99%** (nest -89%; drizzle 428k -> 8.8k, vscode 4.4M -> 53k), function-call IPIs likewise.
  User cycles also drop 3-7% on those five (fewer flushed TLBs to refill). **Walls -13 to -23%** (drizzle 0.288 -> 0.231 median,
  zod 0.190 -> 0.161, effect 0.278 -> 0.228, vue 0.149 -> 0.129, vscode 2.20 -> 1.70), react -16%, nest level (it is
  start-up bound: its kernel share stays 27.5%, faults 5.5k -> 2.6k).
- **Faults -79 to -96%** (nest -53%), far more than the worker's box measured (-8 to -27%): with THP `always` and an idle laptop the
  huge-page tables and the retained glibc heaps fault once per 2 MB.
- **Peak RSS +4 to +15%, above the expected +2-5%**: drizzle +116 MB, zod +68, effect +65, vue +43, react +51, nest +44,
  vscode +190. The small projects pay most in relative terms (the big tables' 2 MB pages kept per class, heaps no
  longer trimmed). Not investigated: the four commits have no switches, so splitting the rise needs a build per commit.
- drizzle's instructions:u +2.4% (7.94 -> 8.13 G) at the defaults, the others level (+-0.3%); the worker's callgrind K1
  was flat, so likely the defaults' scheduling (who recycles, what is re-derived), not checked.

**2. perf c2c on drizzle at the defaults** (`perf c2c record -- sh -c '10 runs'`, before then after; HITMs attributed
by the sub-rows' code addresses in `perf c2c report --stdio -N`'s Pareto table; parser in the run's scratchpad, not kept):

| | records | HITMs (local + remote) | kernel code | TLB-shootdown code (llist_reverse_order, should_flush_tlb, smp_call_function_many_cond, flush_tlb_func) | perf's own (merge_sched_in, event_sched_in, perf_event_update_userpage) | user code |
|---|---|---|---|---|---|---|
| before | 121.5k | 556 (374 + 182) | 65% | **217 (39%)**: llist_reverse_order+19 105, should_flush_tlb+27 73, smp_call_function_many_cond 30 | 34 | 193 (35%) |
| after | 100.4k | 330 (272 + 58) | 48% | **2 (0.6%)** | 33 | 170 (52%) |

- **The shootdown lines are gone.** The before kernel share here is 65%, not the 77-90% of the 48cd9835 section: that
  was `--threads 12 --checkers 12`, this the defaults (24 threads), and HITMs are few (~55 per run), so read the
  shares loosely. After, the kernel HITMs left are scheduling, futex (futex_wait, futex_unqueue, __x64_sys_futex:
  ~19), FPU restore and perf's own.
- **No hot user line**: in 10 runs no user cache line has more than one HITM, before or after. By function (after):
  instantiate_structured 8, ty_instantiate_mapped 7, ty_prop_key_literal 6, distribute_conditional 4,
  instantiate_anonymous 4, instantiate_conditional 4, heap_pop 4, ty_mapper_new 4 (before: instantiate_anonymous 15,
  ty_instantiate_mapped 8, instantiate_conditional 8, instantiate_members 8, ty_realias 5): checker type creation on
  freshly reused memory, spread over hundreds of lines, so no false sharing to fix.

**3. Bench** (`scripts/bench.sh --runs 3 --checkers 12 --bun ...` on main, after part 2; fast/tttt). bun: the newest
canary on the box (`--revision` 1.4.4-canary.1+fec4a8850, GitHub canary build of 2026-10-22; bbe37c5f's was
+6bb5dc195). Best wall s / CPU s (median run) / peak MB; bbe37c5f's in the column after each, bun's bbe37c5f best wall in
parentheses:

| corpus | default | bbe37c5f | K12 | bbe37c5f | single | bbe37c5f | bun | bun single | tsgo | tsgo single |
|---|---|---|---|---|---|---|---|---|---|---|
| TypeScript6 | 0.22 / 0.83 / 459 (K4) | 0.24 / 1.21 / 494 | 0.19 / 1.07 / 518 | 0.23 / 1.27 / 489 | 0.43 / 0.43 / 244 | 0.45 / 0.47 / 233 | 0.26 / 1.60 / 283 (0.33) | 0.95 / 0.95 / 153 (0.85) | 0.60 / 3.11 / 497 | 1.09 / 1.94 / 418 |
| type-fest | 0.28 / 0.99 / 906 (K4) | 0.25 / 1.56 / 940 | 0.18 / 1.35 / 1052 | 0.20 / 1.42 / 948 | 0.65 / 0.65 / 604 | 0.68 / 0.68 / 586 | 0.94 / 6.14 / 969 (1.05) | 2.97 / 3.00 / 798 (3.30) | 3.98 / 18.1 / 5667 | 5.90 / 13.0 / 4313 |
| valibot | 0.10 / 1.03 / 568 | 0.12 / 1.17 / 518 | 0.09 / 1.00 / 561 | 0.14 / 1.33 / 511 | 0.33 / 0.33 / 272 | 0.36 / 0.37 / 261 | 0.16 / 2.66 / 264 (0.18) | 0.77 / 0.95 / 148 (1.22) | 0.73 / 5.21 / 836 | 1.53 / 2.98 / 623 |
| bulletproof-react | 0.10 / 0.63 / 470 (K11) | 0.11 / 0.66 / 438 | 0.09 / 0.65 / 492 | 0.11 / 0.72 / 443 | 0.18 / 0.17 / 176 | 0.17 / 0.17 / 174 | 0.18 / 1.07 / 238 (0.17) | 0.43 / 0.51 / 150 (0.43) | 0.43 / 2.59 / 449 | 0.57 / 1.06 / 289 |
| nestjs-boilerplate | 0.05 / 0.41 / 340 | 0.05 / 0.39 / 321 | 0.05 / 0.43 / 336 | 0.05 / 0.42 / 312 | 0.12 / 0.12 / 141 | 0.12 / 0.13 / 146 | 0.06 / 0.58 / 198 (0.07) | 0.23 / 0.25 / 120 (0.26) | 0.15 / 1.46 / 253 | 0.29 / 0.56 / 207 |
| webpack | 0.24 / 2.39 / 899 | 0.26 / 2.76 / 869 | 0.25 / 2.48 / 896 | 0.27 / 2.71 / 877 | 0.97 / 0.96 / 549 | 0.96 / 0.97 / 539 | 0.48 / 5.45 / 508 (0.47) | 1.52 / 1.52 / 331 (1.57) | 1.31 / 8.07 / 1159 | 2.66 / 4.53 / 965 |
| zod | 0.16 / 1.65 / 906 | 0.17 / 1.60 / 848 | 0.16 / 1.66 / 899 | 0.19 / 1.85 / 852 | 0.52 / 0.53 / 456 | 0.50 / 0.50 / 446 | 0.23 / 3.56 / 373 (0.24) | 1.39 / 1.40 / 205 (1.25) | 1.12 / 8.57 / 1303 | 2.12 / 4.28 / 947 |
| xstate | 0.09 / 0.79 / 532 | 0.12 / 1.08 / 491 | 0.11 / 0.89 / 525 | 0.12 / 1.06 / 482 | 0.27 / 0.32 / 205 | 0.29 / 0.29 / 200 | 0.16 / 1.63 / 234 (0.13) | 0.51 / 0.80 / 107 (0.49) | 0.43 / 2.90 / 469 | 0.84 / 1.52 / 363 |
| drizzle | **0.24** / 2.45 / 1146 | 0.31 / 3.02 / 1121 | 0.25 / 2.52 / 1140 | 0.28 / 2.82 / 1098 | 0.63 / 0.63 / 485 | 0.63 / 0.65 / 471 | 0.31 / 5.05 / 428 (0.33) | 1.28 / 1.36 / 261 (1.44) | 1.53 / 10.9 / 1603 | 2.22 / 4.21 / 933 |
| vue | 0.15 / 1.54 / 593 | 0.14 / 1.49 / 557 | 0.14 / 1.58 / 588 | 0.16 / 1.59 / 562 | 0.56 / 0.56 / 319 | 0.56 / 0.55 / 313 | 0.27 / 3.04 / 321 (0.26) | 1.06 / 1.52 / 181 (0.95) | 0.94 / 5.57 / 743 | 1.89 / 3.30 / 667 |
| effect | 0.24 / 2.65 / 985 | 0.28 / 2.93 / 918 | 0.24 / 2.76 / 968 | 0.26 / 2.90 / 924 | 1.02 / 1.03 / 669 | 1.03 / 1.02 / 621 | 0.58 / 7.51 / 509 (0.62) | 2.52 / 2.70 / 326 (2.41) | 1.49 / 11.7 / 1509 | 3.44 / 5.39 / 1305 |
| vscode | **1.75** / 33.1 / 5525 | 1.97 / 35.7 / 5354 | 1.77 / 21.0 / 5178 | 2.13 / 23.1 / 4992 | 9.62 / 9.82 / 4293 | 9.15 / 9.35 / 4090 | 1.93 / 41.1 / 2231 (1.98) | 12.85 / 12.9 / 1668 (14.83) | 8.64 / 56.5 / 7925 | 22.28 / 32.5 / 6612 |
| excalidraw | 0.18 / 1.83 / 762 | 0.20 / 1.91 / 720 | 0.19 / 1.91 / 769 | 0.21 / 2.14 / 740 | 0.66 / 0.66 / 391 | 0.63 / 0.63 / 378 | 0.30 / 3.47 / 383 (0.24) | 1.29 / 1.54 / 253 (1.25) | 1.44 / 10.6 / 1781 | 2.19 / 3.56 / 911 |
| tanstack-router | 0.14 / 1.00 / 606 (K8) | 0.18 / 1.59 / 593 | 0.16 / 1.32 / 631 | 0.16 / 1.47 / 588 | 0.49 / 0.48 / 323 | 0.48 / 0.47 / 320 | 0.23 / 2.86 / 375 (0.22) | 1.32 / 1.33 / 184 (0.96) | 1.21 / 6.53 / 888 | 2.09 / 3.86 / 691 |
| TS6 -b | 0.49 / 2.62 / 560 | 0.48 / 2.72 / 518 | - | - | - | - | - | - | 1.31 / 6.96 / 548 | 2.52 / 4.02 / 526 |

- **tttt beats the newest bun canary's wall at the defaults on every corpus**, vscode included (1.75 against 1.93;
  bbe37c5f's bench tied it at 1.97 / 1.98), at 81% of bun's CPU there but 2.5x its peak (5525 against 2231 MB).
  Single-threaded bun still leads on no corpus (vscode 12.85 against tttt's 9.62).
- Multi-threaded walls -10 to -25% where the kernel share was (drizzle 0.31 -> 0.24, vscode 1.97 -> 1.75, xstate 0.12 ->
  0.09, tanstack 0.18 -> 0.14); peaks +2-10% (part 1's RSS note). Single-threaded unchanged within this sitting's drift
  (vscode 9.15 -> 9.62, but tsgo single moved 19.82 -> 22.28 in the same run: the box, not tttt).
- The default checker policy now picks fewer checkers on some corpora than at bbe37c5f (in parentheses after the
  default: TypeScript6 4, type-fest 4, tanstack 8, react 11): CPU down 0.4-0.6 s there; type-fest's default 0.28 is
  now behind its K12's 0.18.

## The 12-core box at 48cd9835: shared graph A/B, nest libs sys (2026-10-22)
Run by an agent on the human's laptop (Ryzen AI 9 HX 370, the hybrid of the 2989c0f0 section): AC, `balanced`, but
**Firefox open and a qemu VM idling** (load ~1.7 before, 96% idle). No code change. perf 7.2.11, paranoid -1.

**A. Shared graph A/B.** Two plain release builds (`-O3`, not PGO): `rel/` and `relpacked/` (`-DTTTT_SHARD_PACKED`,
today's shard layout: the slots pointer beside the lock). Every run `--threads 12 --checkers 12 --checker-memory off
--noEmit -p P` (paths.sh's). Configurations: **1** plain `rel`; **2** `TTTT_SHARED_GRAPH=1` `relpacked` (today's
flag); **3** 2 + `TTTT_FIRST_TOUCH_STAMPS=0 TTTT_NESTING_ORDER=content`; **4** 3's env on `rel` (the layout fix); **5**
4 + `TTTT_GRAPH_BATCH=1`. `perf stat -r 7` (8 events multiplexed at 71%; run-to-run +-0.2-1.7%). `LLC-load-misses` is
`<not supported>` on this CPU: the fills are demand data fills by source, `ls_dmnd_fills_from_sys.local_ccx` (L3 or
another L2 in the same CCX), `.near_cache` (the other CCX's cache) and `.dram_io_all`. Wall: best / median of 7,
python `perf_counter`, the five configurations interleaved round-robin (the perf stat walls agree at 10 ms resolution).
Changes against 1:

| corpus | cfg | user instr | user cycles | IPC | kernel cycles | L1d misses | L3/L2 same CCX | other CCX | DRAM | wall best / median ms |
|---|---|---|---|---|---|---|---|---|---|---|
| drizzle | 1 | 10.08 G | 7.20 G | **1.40** | 1.71 G | 183 M | 15.1 M | 1.79 M | 17.6 M | 286 / 299 |
| | 2 | 6.96 G -31% | 6.18 G -14% | 1.13 | 1.29 G | 149 M -19% | 16.9 M +11% | 2.98 M +67% | 15.4 M | 252 / 264 (-12%) |
| | 3 | 7.33 G -27% | 6.33 G -12% | 1.16 | 1.25 G | 154 M -16% | 17.6 M +16% | 3.23 M +81% | 15.6 M | 256 / 268 (-10%) |
| | 4 | 7.30 G -28% | 5.91 G -18% | **1.24** | 1.23 G | 152 M -17% | 16.6 M +10% | 2.43 M +36% | 15.3 M | **246** / 260 (-14%) |
| | 5 | 7.86 G -22% | 6.54 G -9% | 1.20 | 1.30 G | 156 M -15% | 16.5 M +9% | 2.35 M +32% | 16.9 M | 262 / 269 (-8%) |
| zod | 1 | 6.08 G | 4.62 G | **1.32** | 1.11 G | 118 M | 11.1 M | 1.18 M | 12.1 M | 183 / 188 |
| | 2 | 5.58 G -8% | 4.76 G +3% | 1.17 | 0.96 G | 122 M +4% | 14.5 M +31% | 2.21 M +87% | 12.8 M | 195 / 199 (+7%) |
| | 3 | 5.63 G -7% | 4.71 G +2% | 1.20 | 0.92 G | 121 M +2% | 14.1 M +28% | 2.18 M +84% | 12.5 M | 194 / 199 (+6%) |
| | 4 | 5.58 G -8% | 4.53 G -2% | **1.23** | 0.93 G | 120 M +2% | 13.7 M +24% | 1.65 M +39% | 12.0 M | 187 / 189 (+2%) |
| | 5 | 5.93 G -2% | 4.82 G +4% | 1.23 | 0.93 G | 122 M +3% | 13.9 M +26% | 1.63 M +38% | 12.7 M | 194 / 196 (+6%) |
| effect | 1 | 9.23 G | 7.45 G | **1.24** | 1.38 G | 181 M | 17.2 M | 1.56 M | 17.6 M | 273 / 295 |
| | 2 | 9.54 G +3% | 8.30 G +11% | 1.15 | 1.47 G | 207 M +14% | 23.6 M +37% | 3.15 M +102% | 20.4 M | 312 / 315 (+14%) |
| | 3 | 9.54 G +3% | 8.25 G +11% | 1.16 | 1.38 G | 203 M +12% | 23.1 M +35% | 3.04 M +95% | 20.2 M | 309 / 311 (+13%) |
| | 4 | 9.54 G +3% | 8.04 G +8% | **1.19** | 1.36 G | 204 M +13% | 22.7 M +32% | 2.20 M +41% | 20.0 M | 302 / 311 (+11%) |
| | 5 | 9.82 G +6% | 8.35 G +12% | 1.18 | 1.39 G | 207 M +14% | 23.1 M +34% | 2.29 M +47% | 20.4 M | 301 / 310 (+10%) |
| vue | 1 | 6.60 G | 4.32 G | **1.53** | 0.83 G | 116 M | 8.9 M | 1.04 M | 8.3 M | 156 / 157 |
| | 2 | 6.65 G +1% | 4.91 G +13% | 1.36 | 0.79 G | 129 M +11% | 11.9 M +34% | 1.85 M +78% | 10.0 M | **198** / 203 (+27%) |
| | 3 | 6.67 G +1% | 4.87 G +13% | 1.37 | 0.76 G | 127 M +10% | 11.7 M +32% | 1.76 M +69% | 9.8 M | 190 / 198 (+22%) |
| | 4 | 6.63 G +0% | 4.77 G +10% | **1.39** | 0.76 G | 127 M +9% | 11.6 M +31% | 1.34 M +29% | 9.8 M | **195** / 199 (+25%) |
| | 5 | 7.29 G +11% | 5.13 G +19% | 1.42 | 0.79 G | 135 M +17% | 11.6 M +31% | 1.33 M +28% | 10.1 M | 200 / 215 (+28%) |

- **Decision numbers. IPC gap 1 -> 4: drizzle 1.40 -> 1.24 (-11%), zod 1.32 -> 1.23 (-7%), effect 1.24 -> 1.19 (-4%),
  vue 1.53 -> 1.39 (-9%).** Today's flag (2) is 1.13 / 1.17 / 1.15 / 1.36: the layout fix (3 -> 4) closes about a
  third of the gap on drizzle and zod, less on vue and effect. **vue's wall: 2 = 198 / 203 ms, 4 = 195 / 199 ms**,
  against plain's 156 / 157: under the flag vue is +25%, not 2x (the 2x isn't reproduced here), and the layout fix
  doesn't change it (-2%, noise). vue's flag runs do the same instructions as plain (+0-1%): the graph saves vue
  nothing and its sharing costs 10-13% cycles.
- The layout fix's mechanism is visible: fills from **the other CCX's cache** fall from +67-102% over plain (2, 3) to
  +29-41% (4) on every corpus, user cycles -2 to -7% at equal instructions (3 -> 4). Fills within the CCX stay
  +10-32% (true sharing of published data, not the shard header).
- Only drizzle wins on wall with the flag (-14% at 4); zod is level (+2% at 4), effect +11%, vue +25%. drizzle's
  win is instructions (-28%); every other corpus saves <= 8% instructions and loses more in IPC.
- Content nesting order (2 -> 3) costs +5% instructions on drizzle, nothing elsewhere. Batched publication (5) is
  worse everywhere: +6-10% instructions over 4 (vue +10%: more re-derivation while results are held to a file's end),
  wall +1 to +8%.
- **perf c2c on drizzle** (`perf c2c record -c 2000`, AMD IBS, one run each, ~330k samples): HITM loads 473 (2) / 506
  (4). **Kernel lines dominate both: 90% (2) and 77% (4) of HITMs** are the TLB-shootdown IPI path
  (`should_flush_tlb` 35% / 32%, `llist_reverse_order` 34% / 29%, `smp_call_function_many_cond`, `flush_tlb_func`:
  per-CPU lines written by remote CPUs during munmap/madvise shootdowns; flush_tlb_func + native_flush_tlb_* +
  should_flush_tlb are ~10% of all IBS op samples in both). User-space HITMs are 47 (2) and 114 (4):
  `pthread_mutex_lock` 14 -> 44 (in 4 spread over lines 0x400-0x1000 apart, the separated shard locks most likely, plus the load pool's
  mutex beside `load_pool_worker` 7 and libc's unresolved 0x94e81 15), `shared_table_insert` 11 / 10,
  `structured_related_members` 3 / -, `scope_new` - / 4. No single user line above 1% of HITMs in either. Who
  issues the shootdowns (arena madvise/munmap at the end of phases, AST drops) wasn't checked; with HITM samples this
  small the user-space split is indicative only.
- **Determinism**: configuration 4 on drizzle, 5 runs: byte-identical to each other, **but not to configuration 1**
  (which equals K1 plain and, paths aside, tsc's cached text). `TTTT_NESTING_ORDER=content` alone at K1 (no graph)
  gives configuration 4's output: 3 elaboration chains (mysql2, planetscale, singlestore `with(...).update(...)`)
  print `'with(...).update(...).set(...).where(...).$dynamic()'` / `MySqlUpdateDynamic<...>` where tsc and plain
  print `'with(...).update(...).set(...)'` / `MySqlUpdateBase<...>`. So content order is deterministic but moves
  drizzle's text away from tsc's. Separately, today's flag on `rel` (stamps on) differed from K1 in 2 of 6 drizzle runs
  (the `mysql-core` `execute` chain, a `Two different types with this name exist` elaboration); `relpacked` 1 of 1 the same.

**B. nest libs-phase system time** (fast/tttt rebuilt at 48cd9835, 10 runs each, interleaved; median [min-max]).
**`TTTT_AST_SMALL_KB=0` is the default, not off** (ast_alloc.c `populate()`: 0 means AST_SMALL_PAGES), so the
requested pair is an A/A; `=1` (huge pages from the second 2 MB step, the nearest to off) added:

| | default | SMALL_KB=0 | SMALL_KB=1 |
|---|---|---|---|
| libs wall / sys ms | 0.2 / 1.2 [0-4.1] | 0.2 / 1.4 [0-2.0] | 0.2 / 0.7 [0.2-1.7] |
| load+parse files wall / sys ms | 22.5 / 63.7 [33-76] | 22.5 / 58.8 [42-68] | 21.8 / 58.9 [42-75] |
| config + libs + files sys ms | 68.7 | 64.1 | 64.0 |
| total wall ms | 62.2 [59.8-65.8] | 63.0 [59.3-67.8] | 63.1 [58.9-66.8] |
| peak RSS MB | 303 [293-314] | 300 [286-309] | 304 [295-312] |

- **The libs phase is gone**: 0.2 ms wall since b6f8a465 (the loader no longer waits for a library a worker is
  parsing), its work now inside load+parse files. Compare the front end's sys as a whole.
- Default vs `=1`: sys +4.7 ms median in load+parse files, but the A/A pair (default vs `=0`, the same code) differs by
  4.9 ms and the spread is 35 ms: **no difference measurable** at 10 runs. Peak RSS isn't lower by default either
  (303 vs 304): nest's peak is the typecheck's (~295 MB resident there against ~150 after parse), so the AST's last
  huge pages don't reach it on this box. Total wall equal.

**C. react's form.tsx** (single-threaded, 9 runs, median [min-max]). tttt: `TTTT_FILE_TIMES=1 fast/tttt --checkers 1
--threads 1`: **`src/components/ui/form/form.tsx` 36.6 ms [36.1-40.3] of a 93.8 ms typecheck** (39%; the next file is
dropdown.tsx at 5.6). tsgo prints no per-file time, so a differential on both sides: a scratch tsconfig extending
`tsconfig.corpus.json` with only form.tsx in `files`, minus one with only `form/label.tsx` (which form.tsx imports;
`typeRoots` set absolute), tsgo `--singleThreaded --extendedDiagnostics` check time:

| | form.tsx alone | label.tsx alone | difference | whole project |
|---|---|---|---|---|
| tsgo check ms | 305 [201-413] | 109 [69-125] | **~196** | 363 [351-414] |
| tsgo types | 105334 | 7052 | 98282 (74% of the project's) | 133385 |
| tttt typecheck ms | 42.1 [40.9-54.8] | 3.1 [2.8-5.0] | **~39** | 93.8 [90.4-96.1] |
| tttt types | 66951 | 2298 | 64653 (72%) | 89481 |

- **tsc pays the same**: form.tsx (react-hook-form's generics with zodResolver) is about half of tsgo's check time
  and three quarters of its types, as it is ~40% of tttt's time and 72% of its types. tttt does it 5x faster. tsgo's
  form-alone times are noisy (GC); the type counts are exact.

## bun's differential cases, day one (2026-10-23, ab01759ea + the global alias arity fix; `scripts/buncases.py`)
bun's 2418 differential check cases (test/buncases/, bun 5c6d4e24c), 7 skipped (`types` the case doesn't ship), tsgo
cached, tttt at its defaults (one checker on projects this small) and at `--checkers 1` and `4`; this box, loaded, 32 s
wall at -j 12. The cases are shapes on which another tsgo reimplementation once diverged, so they are hard by
selection: about half match.
- 2411 cases: (file, line, code) match 1279 (53.0%), of them byte-identical 1073; spurious 312, missing 489, both 325,
  crash 6 (4 SIGSEGV: 13-C11 41-C8b 88-R05b 99-V4; 36-C8 abort; 41-C12 exit 4), timeout 0; K-dependent 1 (17-C1d);
  one log_error (54-L02a: scope.c's "Alias resolved while typechecking"). Before the arity fix 01-C12b crashed too.
- tsgo agrees with bun's recorded 7.1 output (`theirs`) on 21 of the 22 cases carrying one.
- Root causes by cases (cases / only-mismatch): -TS2322 122/54, +TS2322 92/40, +TS2339 59/32, -TS2339 48/27,
  -TS7022 46/17, -TS1005 45/4, +TS1003 33, -TS1003 32, +TS1005 31, +TS2304 26, -TS2304 25, -TS2303 25/19 (circular
  import alias), -TS7023 23/11, +TS7006 19; parser recovery (TS1005/1003/1128/1109/1434/1146) is a sizeable share.
- Text-only (same (file, line, code), 206 cases): TS2322 header 98 (type printing), TS2322 chains ~20, columns ~30,
  line order 10.

## Fresh repositories, seventh rule: random large repositories (2026-10-23)
Refresh 8 (cor10, repos.txt "Refresh 8"): the human's claim, "over half of the random reasonably large projects I
test, clean under tsgo 7.1, have incorrect errors under tttt, even single-threaded", against a test set chosen by size
and freshness only: `scripts/reposearch.py large 20261023 28` (20 search pages: size buckets x sort by updated / by
stars x two random offsets, a 1669-repo pool, shuffled; TypeScript the main language with >= 5 MB of it, stars
100..20000, pushed since 2026-07-01, a root package.json and lockfile). 28 picked, 3 dropped at install (ordewell, realm,
vercel-sdk), 5 pending (agentic-qe, emperor-agent, foldkit, coop, harness-mcp: installs waiting), **20 repos of 153k to
1.57M lines of .ts/.tsx (outside node_modules), 617 configs**, every one `# perf: no`. tsgo's verdicts: 160 s at `-j 3` (the 11 repo-root configs, first missed by a `-f '^(...)/'` filter, ~200 s more; their
day-one verdicts from 57c517455's build: all match at K1, appflare's root not at the defaults).
**Day one (57c517455, before any fix), (file, line, code): 605 / 617 configs (98.1%) at `--threads 1`, 594 / 617
(96.3%) at the defaults; per repo 12 / 20 fully match at one thread, 11 / 20 at the defaults** (`scripts/sourcesweep.py
-f ... [--defaults -j 2]`). By repo, the human's "over half" holds at the defaults (9 / 20 repos with a wrong config)
though most configs match: the wrong ones are few per repo and a single parser bug (a raw NUL byte in a regex literal:
TS1127 and every semantic diagnostic of the program suppressed) took out 3 configs of 2 repos with ~4000 missing
diagnostics. Only 90 configs (1 whole repo, onco) are clean under tsgo: tests without their runner's types, unbuilt references.

| repo | TS lines | configs | tsgo-clean | match K1 | match defaults | repo matches K1 / defaults | tsgo-clean repo |
|---|---|---|---|---|---|---|---|
| 0sec | 619,326 | 12 | 1 | 10 | 10 | no / no | no |
| ark | 183,767 | 14 | 3 | 12 | 12 | no / no | no |
| semiotic | 596,888 | 14 | 9 | 14 | 14 | yes / yes | no |
| papra | 152,799 | 11 | 3 | 10 | 10 | no / no | no |
| deepmonkey-studio | 769,812 | 47 | 19 | 46 | 46 | no / no | no |
| open-connector | 1,569,814 | 6 | 2 | 6 | 6 | yes / yes | no |
| mulmoclaude | 350,853 | 84 | 15 | 82 | 82 | no / no | no |
| genkit | 245,916 | 97 | 1 | 96 | 96 | no / no | no |
| abacusai-bot | 503,470 | 15 | 9 | 13 | 13 | no / no | no |
| smartperfetto | 555,810 | 2 | 0 | 2 | 2 | yes / yes | no |
| panelui | 216,646 | 9 | 2 | 9 | 9 | yes / yes | no |
| onco | 638,136 | 2 | 2 | 2 | 2 | yes / yes | yes |
| amplify-ui | 217,737 | 51 | 3 | 51 | 51 | yes / yes | no |
| appflare | 391,635 | 16 | 3 | 16 | 5 | yes / no | no |
| javdbviewed | 354,286 | 4 | 3 | 4 | 4 | yes / yes | no |
| molio | 158,558 | 8 | 2 | 8 | 8 | yes / yes | no |
| vuetify0 | 233,550 | 20 | 5 | 20 | 20 | yes / yes | no |
| ps5upload | 321,699 | 1 | 0 | 1 | 1 | yes / yes | no |
| crowd-dev | 331,853 | 61 | 1 | 60 | 60 | no / no | no |
| opentabs | 339,646 | 143 | 7 | 143 | 143 | yes / yes | no |
| **total** | | 617 | 90 | 605 (98.1%) | 594 (96.3%) | 12 / 11 of 20 | 1 |

Causes (agents/bugs/sourcesweep_checker.md "Refresh 8"): 0sec and abacusai-bot/apps/host (the NUL byte), appflare
(+TS5115 on zod 4 types, checker-count dependent: 15 -> 5 at the defaults), abacusai-bot/apps/web (+TS2322 on motion's
props), crowd-dev (TS7022 on other `nodes` loops), mulmoclaude (+TS2345 unknown), deepmonkey-studio (+TS2339 on
never), ark (solid factory +TS2322), papra (+TS7006), genkit (-TS7022/-TS7024).

## Fresh repositories, fifth rule: TypeScript 7 adopters (2026-10-23)
Refresh 6 (cor8, repos.txt "Refresh 6"): repositories whose root package.json depends on `@typescript/native-preview`
or `typescript` 7 (the people who'd use tttt), `scripts/reposearch.py ts7` over 30 topics (stars 20..5000, pushed
since 2026-07-01, any root lockfile, bun's included): 51 hits, the 32 with the most stars under 100 MB, 3 dropped
(stale frozen lockfiles), 29 repos, 429 projects with a verdict. tsgo 7.1.0-beta's cache took ~1 h at `-j 2` (its
largest runs 11 GB). **Day one (693d1440c plus the day's openclaw fixes): 409 / 429 projects (95.3%), 24 / 29 repos**
on (file, line, code), 0 crashes; **byte-identical text (clidiff): 291 / 307** (14 of the misses are diagnostics
verdicts, 2 text-only: tanstack-ai/packages/ai-react's header wording, pothos/packages/plugin-dataloader's header).
Root causes by repo: openscience (Solid and React JSX in one tree; cor8: a quick type for `f?.(x)`, union mapped
modifiers through index signatures, tsc's JSX children indexing past empty expressions: 14 / 14 after), pothos (corF),
tanstack-ai, node-pg-migrate and portabase (corG). **After (2026-10-23 evening, main + wt-cor8): 423 / 429 projects
(98.6%), 25 / 29 repos; text 300 / 307** (openscience's 215 header lines now visible as text-only, bugs/display.md;
tanstack-ai/packages/ai-react's 2). Left: pothos (plugin-with-input TS7031, plugin-add-graphql TS2345,
plugin-smart-subscriptions -TS7023), node-pg-migrate/docs (TS7's own `typeof ts` typings, +TS2322/-TS2741),
portabase (auth.ts:690 `never`, form.tsx:227 -TS2322), tanstack-ai/packages/ai-harness (-TS2615), with corF and corG.
The full source sweep then (main + wt-cor8, the shared store repaired after corF's 10:51-10:57 corruption):
**6053 / 6085 projects, 334 / 346 repos**, 1 timeout (openclaw's root under load); the rest are openclaw's det
items, kebo's MST cycle, monkeytype, hono, discordx, sunmao, better-auth, es-toolkit (all known) and refresh 6's.

## Fresh repositories, fourth rule: bun lockfiles (2026-10-22)
Refresh 5 (cor6, repos.txt "Refresh 5"): the application-code rule of refresh 4 for repos whose only lockfile is bun's,
installed with a pinned bun 1.4.3 (ensure-sources.sh): refresh 4's 16 skipped hits (12 pass its tsconfig criteria) plus
`scripts/reposearch.py appbun` over bun-centric and application topics; 32 picked, 4 dropped (installs), 28 repos, 100
projects. **Day one (4f5f43116, before any fix): 94 / 100 projects (94.0%), 24 / 28 repos**, 0 crashes, 0 timeouts.
Root causes: kebo (TS2882 `moment/locale/es`: a typesVersions pattern without a file didn't fall back; an MST model
cycle), better-drizzle (a 4.23x types runaway behind tsc's TS2502: a generic mapped target passed an intersection
source by the members fallback HACK, which made variances independent; then two TS2344 from a reporting-pass quirk of
alias variances on conditional types), dev-3.0 (`Parameters<typeof f>` re-entering a half-resolved signature through
a type-predicate lookup tsc doesn't make), emailthing (typesVersions tested against 6.0 instead of tsgo's 7.1.0-dev:
bun-types' `>=7.1` import-attribute modules missed). **After (769d64973): 97 / 100 (97.0%), 26 / 28 repos**; left:
kebo's MST cycle (eager signature parameters, bugs/sourcesweep_checker.md) and better-drizzle's K-dependent second
TS2502 (fixed since by acb8dfe7f) plus two TS2578. better-drizzle at K1: 2.08M -> 364k types (tsgo 492k), types ratio 4.23 -> 0.74. Instructions
(callgrind, release builds) flat within 0.4% across the corpora for the relation fixes.

## Fresh repositories, third rule (2026-10-23)
Refresh 4 (cor3, repos.txt "Refresh 4"): application code by GitHub topic (nextjs, remix, sveltekit, nuxt, express,
fastify, hono, trpc, react-native, expo, tauri, electron-app, chrome-extension, obsidian-plugin, homebridge-plugin,
telegram-bot, discord-bot, threejs, p5js, phaser, bun), 40..400 stars, the first 2 per topic with a root lockfile and a
less covered tsconfig setting (`scripts/reposearch.py app`): 40 repos, 122 projects (nuxt has none: both candidates
and the third extend a generated `.nuxt` config; 16 bun-only hits skipped, triage.md). **Day one (48c7ffdf0, before
any fix): 115 / 122 projects match (94.3%), 36 / 40 repos**, 0 crashes, 0 timeouts, 7 spurious, 0 missing. Root
causes, all checker, none config-level: pi-bolt's TypeBox Record (4 projects, ~120 diagnostics: a target-pushing
deep-nesting HACK makes an eager conditional "definitely true"), bekuto3d (a `declare global { export type { X } from
"vue" }` re-export, binder), hermes-ide (TS7022: for-of variable isolated from the loops in progress), youtube-enhancer
(TS2322: a generic type literal's literal conflict left unreduced). **After (aba95017e): 117 / 122 (95.9%), 38 / 40
repos**; pi-bolt and bekuto3d left (bugs/sourcesweep_checker.md, known-failing repros), plus three tuple fixes found on
the way. By plan 77's scale (>= 90% day one) the checker still generalizes, a point below the two earlier refreshes
(96.1%, 97.1%). Perf on the three biggest (perfsweep, K1 vs tsgo single-threaded): types ratios 0.61-0.72, check
0.19-0.46. Instructions (callgrind, fast/tttt) against the branch point: within 0.2% everywhere (drizzle +0.19%,
type-fest +0.11%). **Round three** fixed pi-bolt (f38a8b493, the relation HACK's removal: 13 / 13) and bekuto3d
(4a482aba3 and 0a400eb6f, global re-exports and tsc's augmentation-reference model): all 122 expected, unconfirmed
until the next full sweep.

## Memory budget sweep on vscode (2026-10-22, 16 vCPUs, loaded)
`--checker-memory` at the defaults otherwise (16 threads), typecheck phase wall / peak RSS:

| budget | typecheck | peak |
|---|---|---|
| default (a quarter of RAM) | 1.20 s | 5.15 GB |
| 3 GB | 1.43 s | 4.2 GB |
| 2 GB | 1.46 s | 4.1 GB |
| 1 GB | 1.39 s | 3.1 GB |

Recycling costs 15-20% of check time, and ~2 GB of the peak (the program plus per-thread state) is there whatever the
budget: no knob, the fix is structural and deferred while speed comes first (vision.md "Decisions"). The cross-checker
duplication census (types and instructions per K, avoidable vs inherent) is in notes/duplication.md, not here.

## The thrashers with the flag (2026-10-21, this box)
pgo/tttt at ee9501eb8, quiet box, best of 3, wall s / CPU s / peak MB, plain -> `TTTT_SHARED_GRAPH=1`:

| project | defaults | K12 T12 |
|---|---|---|
| dokploy/apps/dokploy | 1.87 / 13.1 / 1900 -> 0.86 / 6.3 / 1700 | 1.85 / 18.7 / 2700 -> 0.84 / 8.1 / 1800 |
| monkeytype/frontend | 0.72 / 5.2 / 800 -> 0.31 / 2.1 / 600 | 0.71 / 7.0 / 1000 -> 0.29 / 2.8 / 700 |

2 of 9 dokploy flag K12 runs took 2.42 s / 23 CPU s: the lockstep recycle (notes/shared_graph.md "The perf
accounting"). These are the only projects measured where the flag wins big (recycling thrash in plain).

## Single-threaded checker speed (2026-10-22, ee9501eb8 -> 859a7a6a2, then 08a80d884 -> 2989c0f05; this box)
**The round of 08a80d884 -> 2989c0f05** (callgrind K1, PGO, against 08a80d884 at the round's start; merger's clean
builds): drizzle -1.8%, type-fest -1.5%, zod -1.3%, xstate ~-2.4% (-1.0 from the typeof-local fix, -1.44 from the ck
batch), vscode ~-0.6% net (ck -0.7%; the last correctness batch +0.21%: nested recomputation below call barriers and
non-isolated argument return inference do slightly more work, by tsc's rules). The ck batch: stack buffers for mapper
helpers, instantiate_members building in the arena, property arrays sized up front, the tuple-reference lookup before
renormalizing, MemberRefs not zeroed per access. Sites now give 0.1-0.5% each: mining stopped (notes/checker_speed.md).
`test/instrcount.txt` saved at 7f3cd4fbb.
**The round before** (ee9501eb8 -> 859a7a6a2; 4 vCPUs shared with other agents):
Base ee9501eb8 -> 859a7a6a2. **Callgrind K1 instructions, PGO + LTO
builds of both** (`pgobuild.sh` without ensure-corpora): vscode 77.79 -> 73.69 G (-5.3%), excalidraw 6.51 -> 5.80
(-10.9%), drizzle 5.60 -> 5.28 (-5.7%), effect 8.14 -> 7.68 (-5.6%), zod 4.22 -> 3.99 (-5.4%), tsc 3.87 -> 3.74
(-3.3%), nest 1.007 -> 0.993 (-1.4%). Release (-O3) vscode at 934908c76: 91.56 -> 88.62 G.
**Wall at K1**, best of N interleaved base/new, s: quiet (load 5-6): tsc 0.50 / 0.50 (7 runs), drizzle 0.86 / 0.82
(7); at abf151c0d (four commits earlier) vscode 12.87 / 12.00 (3, load ~5). Under load 19-21 (relative only): zod
0.92 / 1.01, effect 2.36 / 2.25, excalidraw 1.48 / 1.39, vscode 20.59 / 17.98. Needs the laptop's numbers.
What gave what (release, callgrind, against the commit before): a type parameter's instantiation out of
instantiate_structured()'s frame (drizzle -2.5%); allocation-free temporaries in call resolution, relations,
inference, members (zod -2.0%; the malloc family was 5.6% of vscode's instructions, ~3% after, most of the rest the
loader's paths: push_path_to_absolute, relative_path_of, module_resolver_resolve_shared); a name filter for member
tables of at most 8 props (excalidraw -2.5%); subtype reduction's discriminating key remembered per type
(excalidraw -5.8%); node links, intern tables and relation caches at most 5/8 full (-0.3 to -0.6%); template text
appended in runs (excalidraw -1.3%); hot functions' rare cases (with a local buffer: a frame and a stack protector
check for every call) out of line (apparent_type_inner, find_prop_in, apparent_type_of_mapped_at,
resolved_base_constraint: -0.2 to -0.9%). **Declined**: the distribution's hardening flags (-fno-stack-protector etc.:
-4 to -5.5% instructions, 0-3% wall, a security-posture change: kept); LazySlots as two levels (-0.1% instructions,
D1 read misses up 2.07 -> 2.13% on tsc); splitting is_related_to(), recursive_related() or check_related() at their
caches (instructions up or flat). The parser is ~12% of vscode's instructions with no hot site (74M child Parser
creations for 138 MB of source, the top site 10%): structural.

## The 12-core box at bbe37c5f: round three (chunked parse, 16 KB checkers, listings, huge text, react) (2026-10-22)
Run by an agent on the human's laptop (the box of the sections below): AC, `balanced`, Firefox open (idle), load ~2
before. fast/tttt from `scripts/fastbuild.sh` at bbe37c5f in the repo (ext4), hot window 2.34 MB (the first 2 MB on the
huge page). bun: the same canary package (`@oven/bun-linux-x64@1.4.3-canary.20261019.1`, refetched into the scratchpad;
`--revision` 1.4.4-canary.1+6bb5dc195). Walls of parts 2-4 by perf_counter around each run, CPU and RSS from wait4.

**1. Bench** (`scripts/bench.sh --runs 3 --checkers 12 --bun ...`, run first): best wall s / CPU s (median run) / peak
MB, against the 277be4bc section's table below:

| corpus | default | 277be4bc | K12 | 277be4bc | single | 277be4bc | bun | bun single | tsgo | tsgo single |
|---|---|---|---|---|---|---|---|---|---|---|
| TypeScript6 | 0.24 / 1.21 / 494 | 0.25 / 1.20 / 485 | 0.23 / 1.27 / 489 | 0.23 / 1.21 / 486 | 0.45 / 0.47 / 233 | 0.45 / 0.45 / 231 | 0.33 / 2.03 / 279 | 0.85 / 1.02 / 163 | 0.64 / 3.30 / 506 | 1.19 / 2.40 / 424 |
| type-fest | 0.25 / 1.56 / 940 | 0.21 / 1.42 / 921 | 0.20 / 1.42 / 948 | 0.25 / 1.45 / 905 | **0.68** / 0.68 / 586 | 0.61 / 0.61 / 565 | 1.05 / 6.89 / 1044 | 3.30 / 3.38 / 785 | 4.12 / 19.0 / 5639 | 6.14 / 14.8 / 4413 |
| valibot | 0.12 / 1.17 / 518 | 0.14 / 1.26 / 513 | 0.14 / 1.33 / 511 | 0.14 / 1.24 / 514 | 0.36 / 0.37 / 261 | 0.36 / 0.35 / 258 | 0.18 / 3.02 / 273 | 1.22 / 1.26 / 157 | 0.76 / 4.85 / 826 | 1.53 / 3.39 / 628 |
| bulletproof-react | **0.11** / 0.66 / 438 | 0.09 / 0.38 / 327 | 0.11 / 0.72 / 443 | 0.09 / 0.71 / 422 | **0.17** / 0.17 / 174 | 0.14 / 0.14 / 150 | 0.17 / 0.94 / 238 | 0.43 / 0.45 / 154 | 0.42 / 2.57 / 456 | 0.53 / 1.19 / 297 |
| nestjs-boilerplate | **0.05** / 0.39 / 321 | 0.06 / 0.43 / 291 | 0.05 / 0.42 / 312 | 0.06 / 0.51 / 300 | 0.12 / 0.13 / 146 | 0.13 / 0.14 / 135 | 0.07 / 0.67 / 197 | 0.26 / 0.26 / 117 | 0.15 / 1.38 / 266 | 0.27 / 0.70 / 212 |
| webpack | 0.26 / 2.76 / 869 | 0.28 / 2.78 / 872 | 0.27 / 2.71 / 877 | 0.28 / 2.85 / 886 | 0.96 / 0.97 / 539 | 1.00 / 1.00 / 557 | 0.47 / 5.67 / 509 | 1.57 / 1.68 / 323 | 1.21 / 8.18 / 1177 | 2.56 / 4.73 / 964 |
| zod | 0.17 / 1.60 / 848 | 0.21 / 1.91 / 849 | 0.19 / 1.85 / 852 | 0.20 / 1.91 / 827 | 0.50 / 0.50 / 446 | 0.54 / 0.54 / 440 | 0.24 / 3.63 / 385 | 1.25 / 1.46 / 214 | 1.08 / 8.10 / 1338 | 2.12 / 4.82 / 967 |
| xstate | 0.12 / 1.08 / 491 | 0.13 / 1.12 / 470 | 0.12 / 1.06 / 482 | 0.13 / 1.11 / 470 | 0.29 / 0.29 / 200 | 0.28 / 0.28 / 196 | 0.13 / 1.64 / 239 | 0.49 / 0.71 / 111 | 0.44 / 3.00 / 473 | 0.80 / 1.81 / 365 |
| drizzle | 0.31 / 3.02 / 1121 | 0.33 / 3.19 / 1103 | 0.28 / 2.82 / 1098 | 0.29 / 2.93 / 1103 | 0.63 / 0.65 / 471 | 0.63 / 0.65 / 467 | 0.33 / 5.27 / 446 | 1.44 / 2.04 / 269 | 1.44 / 10.1 / 1612 | 2.09 / 3.87 / 930 |
| vue | 0.14 / 1.49 / 557 | 0.17 / 1.71 / 550 | 0.16 / 1.59 / 562 | 0.17 / 1.66 / 552 | 0.56 / 0.55 / 313 | 0.55 / 0.55 / 307 | 0.26 / 3.27 / 314 | 0.95 / 0.96 / 179 | 0.88 / 5.12 / 761 | 1.79 / 3.28 / 702 |
| effect | 0.28 / 2.93 / 918 | 0.29 / 3.07 / 987 | 0.26 / 2.90 / 924 | 0.29 / 3.09 / 993 | 1.03 / 1.02 / 621 | 1.07 / 1.07 / 684 | 0.62 / 8.54 / 520 | 2.41 / 2.59 / 339 | 1.43 / 10.4 / 1510 | 3.36 / 5.55 / 1305 |
| vscode | 1.97 / 35.7 / 5354 | 1.81 / 33.1 / 5348 | 2.13 / 23.1 / 4992 | 2.01 / 22.3 / 4989 | 9.15 / 9.35 / 4090 | 9.23 / 9.28 / 4088 | 1.98 / 41.9 / 2234 | 14.83 / 15.2 / 1656 | 8.07 / 59.0 / 7610 | 19.82 / 33.5 / 6688 |
| excalidraw | 0.20 / 1.91 / 720 | 0.21 / 2.17 / 738 | 0.21 / 2.14 / 740 | 0.22 / 2.28 / 733 | 0.63 / 0.63 / 378 | 0.67 / 0.68 / 372 | 0.24 / 3.46 / 383 | 1.25 / 1.69 / 256 | 1.40 / 10.2 / 1765 | 2.17 / 4.40 / 891 |
| tanstack-router | 0.18 / 1.59 / 593 | 0.19 / 1.54 / 575 | 0.16 / 1.47 / 588 | 0.19 / 1.57 / 576 | 0.48 / 0.47 / 320 | 0.50 / 0.49 / 313 | 0.22 / 2.94 / 367 | 0.96 / 1.56 / 190 | 1.14 / 6.30 / 986 | 2.00 / 3.78 / 670 |
| TS6 -b | 0.48 / 2.72 / 518 | 0.53 / 2.92 / 520 | - | - | - | - | - | - | 1.22 / 6.76 / 710 | 2.37 / 4.02 / 527 |

- **nest faster** (0.06 -> 0.05; alone below: median 84 ms at 2989c0f0 -> 58 ms). **react slower, not faster: 0.09 ->
  0.11 at the defaults and K12, single 0.14 -> 0.17** (still 3x under tsgo single's 0.53; the default's 0.11 beats
  bun's 0.17). Cause: `src/components/ui/form/form.tsx` (react-hook-form) alone is react's critical path at every
  checker count: 48 ms checked at K1, 55-62 ms at the defaults (`TTTT_FILE_TIMES=1`); the next file is 9-29 ms. Release
  builds of 277be4bc and bbe37c5f, K1: form.tsx 8.3 -> 44.5 ms. That is f38a8b49 (the Path/PathImpl HACK removed, 247d87ac's
  +35.8% instructions): tsc's own work there, so the 16 KB policy (11 checkers on react now) only adds CPU (0.38 ->
  0.66 s) and RSS (327 -> 438 MB) without wall to gain: one file of ~50 ms plus a ~35 ms front end is the floor.
- **type-fest single 0.61 -> 0.68 (+11%)**, its default 0.21 -> 0.25 but K12 0.25 -> 0.20 (the parallel ones are noise
  either way). 247d87ac counted +6.9% instructions there from the same commit; not looked into further.
- **vscode's 1.97 at the defaults is the session's drift**, not a regression: run alone afterwards (6 runs) it reads best
  1.70 / median 1.73 s, CPU 31.4 s, 5350 MB, 24 checkers; K12 alone (4 runs) best 1.81 / median 1.87. Single 9.15 against
  9.23: level. Everything else level or faster (zod 0.21 -> 0.17, vue 0.17 -> 0.14, TS6 -b 0.53 -> 0.48). Effect's
  single-threaded peak is back to 621 MB (684 at 277be4bc).
- Against bun: tttt leads on every corpus at the defaults; vscode ties in the bench (1.97 against bun's 1.98; 1.70
  alone, bun not rerun alone).

**2. nest and react alone** (10 runs each, `--time`; per phase the median wall ms (min-max), the main thread's CPU ms,
faults and sys ms of the whole process during the phase):

| phase | nest default (K12) | nest T4 (K4) | react default (K11) | react T4 (K4) |
|---|---|---|---|---|
| config | 1.7 (1.4-1.7), 2.4, 668, 4.3 | 1.1, 2.0, 422, 2.1 | 1.6, 2.5, 728, 2.8 | 1.0, 2.0, 454, 1.8 |
| **load+parse libs** | **7.1 (4.6-10.3)**, 3.3, 2053, 26.8 | 3.7 (2.9-4.4), 3.4, 351, 4.5 | 8.2 (6.6-11.2), 4.8, 2029, 25.1 | 4.4, 4.2, 359, 3.4 |
| load+parse files | 12.1 (7.8-14.4), 8.6, 372, 29.5 | 23.2, 21.2, 1218, 22.5 | 7.8, 5.9, 296, 22.2 | 14.7, 13.7, 623, 11.3 |
| resolve names | 19.3 (17.9-21.3), 16.1, 2015, 16.0 | 22.2, 21.8, 511, 3.0 | 17.9, 13.8, 2363, 17.0 | 19.7, 19.3, 840, 3.2 |
| typecheck | 14.3 (13.7-15.4), 10.3, 2901, 12.0 | 21.9, 19.9, 2719, 7.9 | 76.2 (67.6-106), 34.0, 24595, 61.4 | 66.1 (57.5-98), 63.1, 15669, 28.4 |
| **wall ms, best / median** | **55.6 / 58.6** | 69.7 / 75.6 | 107.2 / 115.2 | 101.1 / 109.3 |
| per run | 61 56 58 63 59 58 59 59 56 60 | 80 72 70 83 71 79 77 71 74 81 | 122 127 109 149 115 114 107 128 112 116 | 135 108 104 112 148 104 101 108 119 110 |
| CPU s (user + sys) / RSS MB | 0.445 (sys 0.093) / 315 | 0.265 (0.040) / 187 | 0.677 (0.137) / 436 | 0.373 (0.051) / 251 |

- **libs halved as expected: nest 7.1 ms median at the defaults** (2989c0f0: 12-19; timeline 16), 3.7 at T4 (single
  thread parses it in chunks as well). `TTTT_PARSE_CHUNK_STATS=1` on nest: `1 files in 34 chunks (29 parsed by
  helpers), 0 fallbacks`. The defaults' libs phase still costs ~2000 faults and 27 ms of sys across the pool (T4: 351, 4.5).
- **nest 84 ms (2989c0f0, median) -> 58.6 at the defaults, 83 -> 75.6 at T4**; the defaults now beat T4 by 17 ms.
- react: T4 is as fast as the defaults (109 vs 115 median, at 55% of the CPU and 58% of the RSS): typecheck is
  form.tsx-bound (above). The defaults' typecheck takes 24.6 k faults and 61 ms of sys (11 checkers' arenas).
- **`TTTT_HUGE_TEXT=2`, 20 nest runs: 20 taken at the fault**, none by collapse, none missed (best 53.1 / median 56.1 ms).
- **Timeline, nest at the defaults** (`bench/timeline/`, 3 runs, the middle one; 60.4 ms to the end of typecheck): config
  1.7, libs 6.8 (main blocked 3.3, the pool at 11.7x), load+parse files 13.0 (10.7x; others' samples 17%
  `__lll_lock_wait_private`), file scopes + pool end 2.2, declare in isolation 3.5, declare shard merge 3.8, link 3.0
  (+ module refs 0.3, export star conflicts 1.1), parallel walk 6.4 (12.5x), typecheck 15.7 (13 threads busy, 7.2x).
  Before typecheck: 44.7 ms (2989c0f0: 52). The BOLT binary's frames mostly symbolize as `?` here.

**3. Listings A/B** (`TTTT_NO_LISTINGS=1`, 10 runs per block, ABBA: default, off, off, default; best / median ms,
median sys s):

| | default a | off a | off b | default b |
|---|---|---|---|---|
| nest | 54.3 / 58.3, 0.090 | 53.0 / 58.0, 0.092 | 54.3 / 59.0, 0.093 | 54.8 / 57.7, 0.090 |
| react | 110.8 / 130.1, 0.135 | 109.6 / 114.3, 0.128 | 111.4 / 120.2, 0.136 | 106.5 / 117.6, 0.131 |

- **Nil, as expected**: nest within 1 ms either way, sys level; react's spread (106-149 ms per run) is larger than any
  difference (default a's 130 median is that block's noise: default b 117.6).

**4. Checker policy A/B** (5 runs each, best / median wall ms, median CPU s, median RSS MB):

| corpus | default (checkers) | --checkers 4 | --checkers 12 |
|---|---|---|---|
| nest | **53.2 / 54.7**, 0.43, 310 (12) | 59.4 / 60.0, 0.40, 296 | 54.4 / 57.8, 0.44, 317 |
| react | 111.5 / 117.0, 0.68, 435 (11) | **102.2** / 116.6, **0.48**, 368 | 110.7 / 115.0, 0.72, 441 |
| xstate | **115.5** / 123.3, 1.04, 476 | 127.8 / 130.7, 0.55, 394 | 115.2 / 122.1, 1.01, 481 |
| valibot | 129.7 / 132.3, 1.20, 515 | 147.6 / 165.9, 0.73, 451 | **122.6** / 130.7, 1.19, 523 |
| TypeScript6 | 208.9 / 225.1, 1.13, 487 | **199.7 / 204.3**, **0.77**, 413 | 211.9 / 216.0, 1.14, 479 |

- The default is level with K12 everywhere (it is K12 or K11 on all five) and beats K4 on nest (-5 ms), xstate (-7) and
  valibot (-18 to -33). **K4 wins on TypeScript6 (-21 ms median, -32% CPU)** and ties react at 70% of the CPU: both have
  one file or a few that bound typecheck (TypeScript6's checker.ts; react's form.tsx), so checkers beyond 4 only add
  start-up and faults. A cap from the largest file's share of the source would catch both; not tried.

**5. perf stat** (`-e cycles,instructions,L1-icache-load-misses`, fast/tttt, `--noEmit --pretty false`):

| | bbe37c5f | 277be4bc |
|---|---|---|
| vscode K1 pinned (taskset -c 2), -r 3: wall s | 9.067 (+-0.4%) | 9.158 (-r 10) |
| cycles G | **44.24** (-1.7%) | 44.99 |
| instructions G | **63.05** (-2.9%) | 64.93 |
| L1-icache-load-misses | 496 M (-1.7%), 7.87 per 1000 | 505 M, 7.76 per 1000 |
| drizzle defaults, -r 10: wall s / cycles G / instructions G / icache | 0.284 / 8.12 (-2.0%) / 8.85 (0.0%) / 63.2 M (0.0%) | 0.287 / 8.29 / 8.84 / 63 M |
| drizzle K1 pinned, -r 10: wall s / cycles G / instructions G / icache | 0.622 / 3.04 / 4.33 / 30.0 M (7.0 per 1000) | (not measured) |

- vscode K1: cycles -1.7%, instructions -2.9%, icache misses -1.7% absolute but +1.4% per instruction: **the noinline
  splits' simulated -5% doesn't show on hardware**; the gain is fewer instructions. drizzle at the defaults: cycles -2%,
  icache level. (277be4bc's drizzle row was at the defaults; K1 has no earlier number.)

## The 12-core box at 277be4bc: huge text, -fno-peel-loops, clang (2026-10-22)
Run by an agent on the human's laptop (the box of the sections below): AC, `balanced`, Firefox open (~1.5% of the box
in `top`), nothing else running. Built in a detached worktree of origin/wt-layout (277be4bc) **in /tmp (tmpfs)**, with
one local, uncommitted change: main's `scripts/boltbuild.sh` (e790f109, the GOT-free instrumentation runtime; wt-layout
predates it, and Arch's llvm-bolt runtime fails without it). fast/tttt = bolt/tttt (GCC PGO + BOLT), hot window
`__hot_start..__hot_end` 2.47 MB, of which the first 2 MB go on the huge page. `perf stat -e
cycles,instructions,iTLB-load-misses,bp_l1_tlb_miss_l2_tlb_hit,L1-icache-load-misses` on vscode
(`src/tsconfig.corpus.json --noEmit --pretty false`). K1 is `--checkers 1 --threads 1` pinned to CPU 2 (`taskset`, a Zen
5 core); the defaults unpinned. The clock varies between runs under `balanced` (4.75-4.85 GHz at K1), so **pinned
cycles** are the number to read.

**1. Hot text on a 2 MB huge page (on against `TTTT_HUGE_TEXT=0`), fast/tttt.** The page is in place during a run: an
anonymous `r-xp` 2 MB mapping at `__hot_start` with `AnonHugePages: 2048 kB` in smaps, the rest of the hot window
file-backed after it.

| | vscode K1 on | K1 off | vscode defaults on | defaults off | nest on | nest off |
|---|---|---|---|---|---|---|
| runs | -r 10 | -r 10 | 5 alternating rounds of -r 4 | | 5 alternating rounds of -r 10 (2 earlier non-alternating -r 10 pairs agree: a tie) | |
| wall s | **9.158** (+-0.4%) | 9.599 | **2.035** (every round) | 2.059 | 0.0789 | 0.0783 |
| cycles G | **44.99** (-3.6%) | 46.67 | **95.96** (-1.2%) | 97.09 | 1.270 | 1.272 |
| instructions G | 64.93 | 64.93 | 76.24 | 76.21 | 1.20 | 1.20 |
| IPC | 1.44 | 1.39 | 0.79 | 0.79 | 0.95 | 0.95 |
| L1 iTLB misses (L2 hits) per 1000 instructions | **0.22** | 6.59 | **0.20** | 5.36 | 0.6 | 2.8 |
| iTLB-load-misses (L2 misses) | 0.03 M | 0.15 M | **11.4 M** | 68.6 M | 0.03 M | 0.06 M |
| L1-icache-load-misses per 1000 instructions | **7.76** (-13%) | 8.88 | **7.05** (-18%) | 8.59 | 6.07 | 6.22 |

| `-M PipelineL1 -r 3` | K1 on | K1 off | defaults on | defaults off |
|---|---|---|---|---|
| wall s | 9.23 | 9.60 | 1.92 | 2.05 |
| frontend / backend / bad spec / retiring % | 51.1 / 24.8 / 7.9 / 16.0 | 50.7 / 25.7 / 7.6 / 15.5 | 47.7 / 25.8 / 3.2 / 9.2 (SMT 11.7) | 48.9 / 24.9 / 3.2 / 9.0 (SMT 11.7) |

- **Huge text is a win: -4.6% wall and -3.6% cycles at K1, -1.2% at the defaults (all 5 alternating rounds), nest a
  tie** (its 2 MB copy and fault don't show at 78 ms). The L1 iTLB nearly stops missing (30x fewer), the defaults'
  iTLB L2 misses drop 6x, and the icache misses fall too (13-18%: fewer page walks competing with fetch, presumably).
  Retiring +0.5 point at K1; the topdown split barely moves otherwise.
- The first defaults A/B (-r 10 each, not alternating) read on 1.884 / off 1.787 s against the cycles' -2%, and the
  PipelineL1 runs 1.92 / 2.05: wall at the defaults drifts more than the effect under `balanced`; the alternating
  rounds are the result.
- **"Off" isn't the 8e3f4fb0 section's BOLT number**: there, BOLT alone had 0.13 L1 iTLB misses per 1000 at K1; today
  main's fast/tttt (92828ad6, from the ext4 checkout) reads 6.6 like this build's off from tmpfs (one -r 3 of this
  build copied to ext4: 1.6, no FilePmdMapped in smaps either). Something gave 8e3f4fb0's runs large or huge text
  mappings (file THP or large folios in the page cache?) that today's runs don't get; unexplained, but huge_text()
  makes it deterministic.
- Some batches with the page on show 4 KB-level iTLB rates (clang K1 round 3: 3.5 per 1000; nest's first on round 2.4;
  no-peel zod 0.8-1.6): the page wasn't taken in some runs (`huge_pages_take()` or the THP allocation failing right
  after a previous run freed its memory?); not investigated.

**2 and 3. -fno-peel-loops and clang, against fast/tttt** (`PGO_DIR=pgo-peel PGO_CFLAGS=-fno-peel-loops
BOLT_DIR=bolt-peel` and `CC=clang PGO_DIR=pgo-clang BOLT_DIR=bolt-clang scripts/boltbuild.sh`, each its own PGO
training and BOLT layout; huge text on for all three). Hot windows: GCC 2.47 MB, no-peel 2.34 MB, **clang 3.67 MB**
(the huge page covers 2 MB of it). vscode output byte-identical for all three. 4 rounds, the order reversed after 2;
vscode -r 5 per round, drizzle and zod -r 10; medians of the 4 rounds (cycles per round in brackets):

| | GCC (fast/tttt) | -fno-peel-loops | clang |
|---|---|---|---|
| vscode K1 wall s | 9.41 | 9.32 (-0.9%) | 9.55 (+1.5%) |
| vscode K1 cycles G | 45.01 [46.90 44.56 44.38 45.47] | **44.58 (-1.0%)** [45.33 44.46 44.41 44.70] | 45.63 (+1.4%) [45.68 45.39 45.59 46.24] |
| vscode K1 instructions G / icache misses | 64.93 / 505 M | 65.73 (+1.2%) / 484 M (-4.2%) | 66.23 (+2.0%) / 509 M (+0.7%) |
| vscode K1 L1 iTLB misses | 14.5 M | 13.4 M | 37.0 M |
| vscode defaults wall s / cycles G | 1.958 / 97.05 | 1.892 (-3.4%) / 98.50 (+1.5%) | 1.966 (+0.4%) / 98.85 (+1.9%) |
| vscode defaults instructions G / icache misses | 76.36 / 535 M | 77.39 (+1.3%) / 514 M (-3.9%) | 78.13 (+2.3%) / 537 M |
| drizzle wall s / cycles G / instructions G / icache | 0.287 / 8.29 / 8.84 / 63 M | 0.284 (-1.0%) / 8.26 (-0.3%) / 9.17 (+3.7%) / 61 M (-3.8%) | 0.290 (+0.9%) / 8.42 (+1.6%) / 9.45 (+6.9%) / 62 M (-2.1%) |
| zod wall s / cycles G / instructions G / icache | 0.199 / 5.50 / 5.53 / 43 M | 0.193 (-3.3%) / 5.45 (-1.1%) / 5.65 (+2.1%) / 42 M (-3.1%) | 0.197 (-1.0%) / 5.51 (0.0%) / 5.80 (+4.9%) / 42 M (-1.7%) |

- **-fno-peel-loops: a small win, at the edge of the noise.** K1 cycles -1.0% (lower in 3 of 4 rounds, a tie in the
  4th; round 1's GCC run was disturbed: 5x its usual iTLB L2 misses), zod -1.1%, drizzle -0.3%; the defaults' wall
  -3.4% but their cycles +1.5% (the defaults' wall is the noisiest number here). Instructions +1.2-3.7% as predicted;
  icache misses -3 to -4% on hardware, less than cachegrind's simulated 5-10%. Worth taking only with another round
  (or `instrcount.sh`-style cycles on the corpora) to confirm.
- **clang: no, decided.** Worse at K1 in every round (+1.4% cycles), +1.9% cycles at the defaults, +1.6% drizzle, a
  tie on zod. **On hardware clang executes more instructions, not fewer**: +2.0% on vscode K1, +2.3% at the defaults,
  +6.9% drizzle, +4.9% zod (cachegrind's proxy said -6% on vscode; that run's builds or counting differ from these
  BOLTed binaries': unexplained). Its icache misses are level with GCC's (+0.7% K1, -2% on the small corpora), and its
  1.2 MB larger hot window spills past the huge page: 2.5x the L1 iTLB misses at K1, 6x at the defaults.

**4. Bench** (`scripts/bench.sh --runs 3 --checkers 12`, fast/tttt as built, huge text on), best wall s / CPU s
(median run) / peak MB, against the 8e3f4fb0 section's fast columns (BOLT, no huge text):

| corpus | default | 8e3f4fb0 | K12 | 8e3f4fb0 | single | 8e3f4fb0 | tsgo | tsgo single |
|---|---|---|---|---|---|---|---|---|
| TypeScript6 | 0.25 / 1.20 / 485 | 0.22 / 1.17 / 501 | 0.23 / 1.21 / 486 | 0.21 / 1.19 / 503 | 0.45 / 0.45 / 231 | 0.44 / 0.44 / 231 | 0.58 / 2.86 / 508 | 1.09 / 2.04 / 426 |
| type-fest | 0.21 / 1.42 / 921 | | 0.25 / 1.45 / 905 | | 0.61 / 0.61 / 565 | | 3.94 / 19.2 / 5567 | 5.90 / 15.1 / 4428 |
| valibot | 0.14 / 1.26 / 513 | | 0.14 / 1.24 / 514 | | 0.36 / 0.35 / 258 | | 0.69 / 4.59 / 844 | 1.47 / 3.03 / 634 |
| bulletproof-react | 0.09 / 0.38 / 327 | | 0.09 / 0.71 / 422 | | 0.14 / 0.14 / 150 | | 0.43 / 2.58 / 452 | 0.58 / 1.42 / 324 |
| nestjs-boilerplate | 0.06 / 0.43 / 291 | 0.07 / 0.42 / 300 | 0.06 / 0.51 / 300 | 0.06 / 0.48 / 319 | 0.13 / 0.14 / 135 | 0.13 / 0.13 / 134 | 0.14 / 1.38 / 269 | 0.29 / 0.62 / 216 |
| webpack | 0.28 / 2.78 / 872 | | 0.28 / 2.85 / 886 | | 1.00 / 1.00 / 557 | | 1.84 / 10.1 / 1429 | 3.28 / 5.97 / 1267 |
| zod | 0.21 / 1.91 / 849 | 0.20 / 1.86 / 860 | 0.20 / 1.91 / 827 | 0.19 / 1.82 / 848 | 0.54 / 0.54 / 440 | 0.50 / 0.50 / 442 | 1.19 / 8.61 / 1326 | 2.20 / 4.42 / 975 |
| xstate | 0.13 / 1.12 / 470 | | 0.13 / 1.11 / 470 | | 0.28 / 0.28 / 196 | | 0.44 / 2.90 / 476 | 0.81 / 1.79 / 373 |
| drizzle | 0.33 / 3.19 / 1103 | 0.29 / 2.75 / 1106 | 0.29 / 2.93 / 1103 | 0.28 / 2.72 / 1106 | 0.63 / 0.65 / 467 | 0.63 / 0.63 / 466 | 1.48 / 10.6 / 1644 | 2.00 / 4.02 / 923 |
| vue | 0.17 / 1.71 / 550 | | 0.17 / 1.66 / 552 | | 0.55 / 0.55 / 307 | | 0.86 / 5.25 / 787 | 1.78 / 3.69 / 686 |
| effect | 0.29 / 3.07 / 987 | 0.28 / 2.95 / 933 | 0.29 / 3.09 / 993 | 0.27 / 2.91 / 942 | 1.07 / 1.07 / 684 | 1.03 / 1.03 / 621 | 1.59 / 9.47 / 1562 | 3.26 / 6.97 / 1319 |
| vscode | **1.81** / 33.1 / 5348 | 1.81 / 32.9 / 5362 | 2.01 / 22.3 / 4989 | 1.97 / 22.7 / 5004 | **9.23** / 9.28 / 4088 | 9.13 / 9.18 / 4091 | 7.77 / 55.4 / 7639 | 19.47 / 34.5 / 6955 |
| excalidraw | 0.21 / 2.17 / 738 | | 0.22 / 2.28 / 733 | | 0.67 / 0.68 / 372 | | 1.46 / 11.5 / 1839 | 2.12 / 4.02 / 925 |
| tanstack-router | 0.19 / 1.54 / 575 | | 0.19 / 1.57 / 576 | | 0.50 / 0.49 / 313 | | 1.20 / 6.51 / 913 | 2.00 / 4.27 / 669 |
| TS6 -b | 0.53 / 2.92 / 520 | | - | | - | | 1.28 / 7.23 / 706 | 2.42 / 4.52 / 522 |

- Level with 8e3f4fb0 within the 10 ms granularity and the session's drift (vscode 1.81 default, single 9.23 against
  9.13; drizzle's default 0.33 against its K12 0.29 is one slow set). The bench doesn't show huge text's 4.6%: it ran
  last, after ~30 minutes of back-to-back runs, while part 1's A/B ran alternating within minutes; read the A/B, not
  this table, for the effect. Effect's single-threaded peak 684 MB against 621-626 in the earlier sections is new and
  not looked into.

## The 12-core box at 8e3f4fb0: BOLT and interning A/B (2026-10-22)
Run by an agent on the human's laptop (the box of the 2989c0f0 section below): AC, `balanced`, but **Firefox open**
(~25% of a core) and load 3-5 before each step (10 during B's runs, our own). llvm-bolt wasn't installed (Arch
packages it, `pacman -S llvm-bolt`: this agent's `pacman -F` missed it on a 2023 files database): `BOLT=` pointed at
llvm-bolt from LLVM's own `LLVM-23.1.3-Linux-X64` release tarball (the box's clang is 23.1.3), extracted into the
session scratchpad and gone with it. `scripts/fastbuild.sh`
at 8e3f4fb0 then built pgo/tttt (the baseline) and bolt/tttt (fast/tttt); byte-identical output on vscode.

**A. BOLT (pgo/tttt -> fast/tttt), vscode.** `perf stat -M PipelineL1 -r 3` and `perf stat -e
cycles,instructions,L1-icache-load-misses,iTLB-load-misses,bp_l1_tlb_miss_l2_tlb_hit,ic_tag_hit_miss.*,op_cache_hit_miss.*,de_src_op_disp.* -r 3`
(the event set of the 2989c0f0 section). K1 is `--checkers 1 --threads 1` **pinned to CPU 2** (a Zen 5 core, `taskset`:
the hybrid's core types would swamp a 3% A/B); the defaults unpinned:

| vscode | K1 pgo | K1 fast | defaults pgo | defaults fast |
|---|---|---|---|---|
| wall s (PipelineL1 run / events run) | 9.55 / 9.58 | **9.25 / 9.28** (-3.2%) | 1.850 / 1.919 | **1.812 / 1.844** (-2 to -4%) |
| cycles G / instructions G | 46.66 / 64.92 | 45.24 / 64.49 | 100.28 / 76.48 | 95.49 / 75.92 |
| IPC | 1.39 | 1.43 | 0.76 | 0.80 |
| frontend / backend / bad spec / retiring % | 53.0 / 23.4 / 7.5 / 15.5 | **51.1** / 24.8 / 7.9 / 16.0 | 51.5 / 22.4 / 2.9 / 8.8 (SMT 12.1) | **48.7** / 24.0 / 3.2 / 9.3 (SMT 12.4) |
| L1-icache-load-misses per 1000 instructions | 11.2 | **7.9** (-30%) | 10.5 | **7.1** (-33%) |
| ic_tag miss rate (of fetch lookups) | 22.1% | 18.3% | 31.8% | 26.8% |
| L1 iTLB misses (L2 hits) per 1000 instructions | 12.0 | **0.13** | 9.7 | **0.14** |
| iTLB-load-misses (L2 misses) | 0.2 M | 0.03 M | 89 M | **10 M** |
| op cache hit rate / ops from the x86 decoder | 62.8% / 29.4% | 63.6% / 29.3% | 56.0% / 37.5% | 56.9% / 37.4% |

- **BOLT is a 3% win on real hardware at K1 and 2-4% at the defaults**, all of it front end: 30% fewer icache misses
  (more than cachegrind's simulated 16% fewer I1 misses, c098d0c1), and the L1 iTLB nearly never misses (its hot text is
  packed into a ~2.5 MB window, `__hot_start..__hot_end`): the 300x iTLB-L2-miss excess of the defaults over K1 (the
  2989c0f0 section's unexplained number) drops 9x. Instructions -0.7%. The op cache doesn't move: it misses on the
  same working set, BOLT only changes where the lines come from.
- The frontend-bound share is still ~49-51%: layout took 2-3 points off, not the stall. Retiring rises 0.5 points.

**Bench** (`scripts/bench.sh --runs 3 --checkers 12`; bench.sh has no `--bin`, so the second run had bolt/tttt **copied
over pgo/tttt** and pgo/tttt restored afterwards, sha256 checked): best wall s / CPU s (median run) / peak MB:

| corpus | default pgo | default fast | K12 pgo | K12 fast | single pgo | single fast |
|---|---|---|---|---|---|---|
| TypeScript6 | 0.26 / 1.19 / 499 | 0.22 / 1.17 / 501 | 0.20 / 1.18 / 494 | 0.21 / 1.19 / 503 | 0.44 / 0.44 / 231 | 0.44 / 0.44 / 231 |
| nestjs-boilerplate | 0.07 / 0.41 / 299 | 0.07 / 0.42 / 300 | 0.07 / 0.47 / 327 | 0.06 / 0.48 / 319 | 0.15 / 0.15 / 136 | 0.13 / 0.13 / 134 |
| zod | 0.20 / 1.89 / 861 | 0.20 / 1.86 / 860 | 0.20 / 1.78 / 860 | 0.19 / 1.82 / 848 | 0.52 / 0.52 / 445 | 0.50 / 0.50 / 442 |
| drizzle | 0.31 / 2.85 / 1120 | 0.29 / 2.75 / 1106 | 0.28 / 2.65 / 1098 | 0.28 / 2.72 / 1106 | 0.65 / 0.65 / 467 | 0.63 / 0.63 / 466 |
| effect | 0.29 / 3.08 / 940 | 0.28 / 2.95 / 933 | 0.27 / 3.08 / 942 | 0.27 / 2.91 / 942 | 1.07 / 1.07 / 624 | 1.03 / 1.03 / 621 |
| vscode | 1.84 / 33.7 / 5353 | **1.81** / 32.9 / 5362 | 2.02 / 22.6 / 4992 | 1.97 / 22.7 / 5004 | 9.44 / 9.45 / 4094 | **9.13** / 9.18 / 4091 |

- Single-threaded 0-13% faster (TypeScript6 level; vscode -3.3%, effect -3.7%, nest -13% at 10 ms granularity); the defaults
  and K12 0-5% (CPU -1 to -4%), never slower beyond the 10 ms granularity. Peak RSS unchanged (bolt/tttt is 11.7 MB
  on disk against 7.7 MB: BOLT keeps the original text). TypeScript6's default 0.26 is again the bench's very first
  measurement (warm-up). vscode at the defaults 1.81-1.84 against the 2989c0f0 bench's 1.70: Firefox and the load.
- **Verdict: the counters confirm BOLT.** Flipping `MEASURED=fast` in `scripts/fastbuild.sh` is now justified (not
  done here: this round didn't change scripts); note llvm-bolt has to be installed off-repo on Arch.

**B. Interning names (wt-fe2: df1ef385, e713d565, 894d2292) against main.** wt-fe2 sits on 4a67416e, not on main
(no fastbuild.sh, no -fno-plt, no c9477eb6), so the three commits were **cherry-picked onto 8e3f4fb0** in a detached
`/tmp/fe2` (b3383033, 1508d86a, d18b702d; clean, never pushed, worktree removed) and built there with its own
`scripts/fastbuild.sh` (own PGO profile and BOLT). Byte-identical output on vscode, nest, TypeScript6. Both fast/tttt:

| vscode, defaults, 5 runs alternating (`--time`, `TTTT_RESOLVE_TIMES=1`), median ms | main | fe2 |
|---|---|---|
| resolve names (wall / main thread CPU) | 124.3 / 115.1 | 122.0 / 114.3 |
| of it: declare / link (of which aliases) / walk | 25 / **41.0 (34.4)** / 41.6 | 29 / **32.0 (25.0)** / 45.3 |
| load+parse files | 114.2 | 114.1 |
| typecheck | 1450 | 1479 |
| total wall / CPU | **1772 / 31.98 s** | **1804 / 32.96 s** |

| `perf stat` | main | fe2 |
|---|---|---|
| nest -r 10, two rounds: wall ms | 77.1, 79.8 | 81.1, 81.3 |
| nest: cycles G / instructions G | 1.249, 1.233 / 1.176, 1.165 | 1.345, 1.280 / 1.180, 1.172 |
| TypeScript6 (src/compiler) -r 5: wall ms / cycles G / instructions G | 231 / 3.71 / 4.33 | 230 / 3.75 / 4.36 |
| vscode defaults -r 3: wall s | 1.852 | 1.872 |
| cycles G / instructions G | 95.38 / 76.34 | 98.02 (+2.8%) / 76.72 (+0.5%) |
| dTLB-load-misses M / L1-dcache-load-misses G | 81.2 / 1.942 | 83.9 (+3.3%) / 1.977 (+1.8%) |

- **No: on this box the interning branch loses.** Link is the predicted -22% (41 -> 32 ms, all in aliases), but declare
  (+4 ms) and the walk (+4 ms) give most of it back, so resolve names wins 2 ms, and the checkers lose ~30 ms of wall
  and ~1 s of CPU (typecheck +2%). End to end vscode +1.8% wall, +3% CPU, +2.8% cycles; nest +2-5% wall, +4-8%
  cycles; TypeScript6 a tie on wall, +1% cycles. Instructions +0.3-0.6% as cachegrind said, but **the cycles rise 5x
  more than the instructions**, and the data side gets worse, not better: dTLB +3.3%, L1d misses +1.8%. The compact
  atom storage doesn't show up as a data-cache win at the defaults; where the checkers' extra cycles go (the shared
  process-wide atom table's lines under 24 threads? the {atom, symbol} entries' extra indirection?) wasn't measured.
- Caveat: each side has its own PGO training and BOLT layout, so ~1% of either way is layout noise; nest's two rounds
  agree on the sign; vscode's 5 alternating runs don't overlap (main 1756-1780, fe2 1787-1831).

## The 12-core box at 2989c0f0: front end cuts and perf counters (2026-10-22)
Run by an agent on the human's laptop: Ryzen AI 9 HX 370, **hybrid**: 4 Zen 5 cores (8 threads, 5.16 GHz, 16 MB L3,
CPUs 0-3,12-15) + 8 Zen 5c (16 threads, 3.29 GHz, 8 MB L3); Linux 7.2.11-zen1, perf 7.2.11, AC, `balanced`, Firefox
closed, load ~1 before. pgo/tttt retrained at 2989c0f0. bun: the same canary package as before
(`@oven/bun-linux-x64@1.4.3-canary.20261019.1`, refetched: the old binary was gone).

**Bench** (`scripts/bench.sh --runs 3 --checkers 12 --bun ...`): best wall s / CPU s (median run) / peak MB, the
f2ad73c9 columns from the section below:

| corpus | tttt default | f2ad73c9 default | tttt K12 | f2ad73c9 K12 | tttt single | f2ad73c9 single | bun | bun single | tsgo |
|---|---|---|---|---|---|---|---|---|---|
| TypeScript6 | 0.26 / 1.2 / 493 | 0.21 / 1.1 / 489 | 0.22 / 1.2 / 500 | 0.23 / 1.2 / 483 | 0.45 / 0.5 / 231 | 0.47 / 0.5 / 229 | 0.30 / 1.8 / 280 | 0.69 / 0.8 / 168 | 0.58 / 3.2 / 499 |
| type-fest | 0.22 / 1.3 / 923 | 0.31 / 1.6 / 891 | 0.22 / 1.4 / 934 | 0.32 / 1.7 / 905 | 0.61 / 0.6 / 567 | 0.88 / 0.9 / 563 | 0.97 / 6.0 / 1022 | 3.76 / 3.9 / 786 | 3.95 / 19.6 / 5238 |
| valibot | 0.14 / 1.2 / 526 | 0.14 / 1.2 / 517 | 0.14 / 1.3 / 533 | 0.13 / 1.2 / 524 | 0.36 / 0.4 / 258 | 0.38 / 0.4 / 257 | 0.14 / 2.4 / 264 | 1.16 / 1.2 / 158 | 0.67 / 4.6 / 833 |
| bulletproof-react | 0.10 / 0.4 / 332 | 0.10 / 0.5 / 304 | 0.08 / 0.7 / 434 | 0.09 / 0.7 / 408 | 0.18 / 0.2 / 149 | 0.15 / 0.2 / 147 | 0.16 / 0.9 / 242 | 0.40 / 0.4 / 157 | 0.42 / 2.5 / 446 |
| nestjs-boilerplate | 0.07 / 0.4 / 301 | 0.08 / 0.5 / 264 | 0.06 / 0.5 / 308 | 0.08 / 0.6 / 304 | 0.14 / 0.2 / 137 | 0.14 / 0.2 / 133 | 0.06 / 0.6 / 197 | 0.19 / 0.2 / 120 | 0.14 / 1.3 / 256 |
| webpack | 0.26 / 2.6 / 871 | 0.27 / 2.6 / 861 | 0.27 / 2.6 / 876 | 0.28 / 2.7 / 860 | 0.97 / 1.0 / 543 | 1.03 / 1.0 / 540 | 0.40 / 4.5 / 513 | 1.61 / 2.4 / 328 | 1.12 / 6.6 / 1163 |
| zod | 0.18 / 1.6 / 866 | 0.18 / 1.8 / 866 | 0.19 / 1.8 / 852 | 0.19 / 1.8 / 856 | 0.52 / 0.5 / 443 | 0.54 / 0.5 / 446 | 0.21 / 3.1 / 380 | 1.02 / 1.5 / 212 | 0.98 / 6.7 / 1340 |
| xstate | 0.11 / 0.9 / 477 | 0.14 / 1.2 / 458 | 0.13 / 1.0 / 504 | 0.13 / 1.1 / 485 | 0.27 / 0.3 / 195 | 0.29 / 0.3 / 197 | 0.14 / 1.4 / 237 | 0.46 / 0.6 / 111 | 0.38 / 2.6 / 480 |
| drizzle | 0.27 / 2.6 / 1087 | 0.30 / 2.8 / 1093 | 0.28 / 2.7 / 1190 | 0.29 / 3.0 / 1123 | 0.64 / 0.6 / 470 | 0.68 / 0.7 / 467 | 0.27 / 4.5 / 433 | 1.46 / 2.3 / 273 | 1.42 / 9.8 / 1619 |
| vue | 0.15 / 1.5 / 566 | 0.17 / 1.7 / 569 | 0.16 / 1.6 / 569 | 0.17 / 1.8 / 556 | 0.56 / 0.6 / 309 | 0.58 / 0.6 / 308 | 0.22 / 2.7 / 312 | 0.93 / 1.5 / 180 | 0.83 / 4.8 / 737 |
| effect | 0.24 / 2.6 / 946 | 0.30 / 3.2 / 909 | 0.26 / 2.8 / 933 | 0.29 / 3.1 / 916 | 1.04 / 1.1 / 626 | 1.11 / 1.1 / 625 | 0.50 / 6.3 / 516 | 2.23 / 2.3 / 333 | 1.42 / 10.3 / 1546 |
| vscode | **1.70** / 31.7 / 5365 | 1.93 / 35.4 / 5375 | 1.90 / 21.5 / 4993 | 2.02 / 22.9 / 5014 | 9.29 / 9.3 / 4093 | 9.90 / 10.0 / 4087 | 1.74 / 36.9 / 2234 | 12.93 / 13.5 / 1642 | 7.74 / 55.2 / 7861 |
| excalidraw | 0.21 / 2.1 / 743 | 0.21 / 2.1 / 730 | 0.21 / 2.2 / 740 | 0.24 / 2.3 / 727 | 0.67 / 0.7 / 374 | 0.71 / 0.7 / 375 | 0.21 / 2.9 / 386 | 1.24 / 1.7 / 255 | 1.45 / 11.7 / 1815 |
| tanstack-router | 0.18 / 1.5 / 586 | 0.20 / 1.6 / 579 | 0.17 / 1.5 / 609 | 0.19 / 1.7 / 589 | 0.50 / 0.5 / 315 | 0.53 / 0.5 / 316 | 0.22 / 2.9 / 376 | 1.05 / 1.1 / 186 | 1.10 / 6.3 / 958 |
| TS6 -b | 0.56 / 2.9 / 539 | 0.57 / 3.1 / 532 | - | - | - | - | - | - | 1.29 / 7.1 / 554 |

- **vscode 1.93 -> 1.70 s at the defaults, now level with bun (1.74).** More than the front end's expected 40-70 ms:
  single-threaded is also 6% faster (9.90 -> 9.29, the checker work since f2ad73c9), worth ~90 ms of the 1.5 s
  typecheck at the defaults. The front end (`--time`: config + libs + load+parse + resolve, 5 runs) is **251-261 ms**
  (efb91998: ~500): config 2, libs 11-20, load+parse 107-117, resolve 122-127. Run alone after the bench (5 runs,
  `/usr/bin/time`) vscode reads best 1.77 / median 1.80 s, CPU 32.7 s, 5343 MB: the 0.07-0.1 s gap to the bench's 1.70 is
  unexplained (thermals after 20 minutes of bench, probably). Either way below f2ad73c9's 1.93.
- vscode `--threads 4` (5 runs alone): best 3.52 / median 3.67 s, CPU 14.3 s, 4452 MB; front end 440-453 ms
  (load+parse 228-235, resolve 194-212).
- **nest**: 0.07 default, 0.06 K12, ties bun's 0.06 at K12. Alone (5 runs, perf_counter): defaults 78-87 ms
  (median 84), CPU 0.43 s, 297 MB, front end 51-58 ms (libs 12-19, load+parse 6-9, resolve 26-33); `--threads 4`
  69-88 ms (median 83), CPU 0.27 s, 169 MB, front end 47-60. The front end is ~2/3 of nest's wall at every T.
- Everything else within noise of f2ad73c9 or better (type-fest 0.31 -> 0.22, effect 0.30 -> 0.24, xstate 0.14 ->
  0.11: f2ad73c9's type-fest was the suspected noise). TypeScript6's default 0.26 against K12 0.22 is the bench's very
  first measurement (unconfirmed warm-up; f2ad73c9 0.21). tttt now leads or ties bun's wall on every corpus.

**Timeline** (`bench/timeline/`, 3 runs each, the middle one shown; the preload inflates walls ~5%):

| vscode | T24 (defaults) | | | T4 | | |
|---|---|---|---|---|---|---|
| phase | wall ms | main run / runq / blocked | pool CPU, parallelism | wall ms | main run / runq / blocked | pool CPU, parallelism |
| config | 2 | 3 / 0 / 0 | | 1 | 2 / 0 / 0 | |
| libs | 17 | 4 / 1 / 12 | 214, 12.9 | 13 | 12 / 0 / 1 | 61, 5.7 |
| load+parse files | 124 | 82 / 15 / 27 | 1664, 14.1 | 223 | 215 / 1 / 8 | 885, 4.9 |
| collect files, pool end, file scopes | 9 | 10 / 0 / 0 | idle | 10 | 10 / 0 / 0 | idle |
| declare in isolation | 21 | 13 / 0 / 7 | 227, 11.8 | 28 | 28 / 0 / 0 | 62, 3.2 |
| declare merge + between | 8 | 7 / 0 / 1 | idle | 8 | 7 / 0 / 1 | idle |
| link: module refs | 1 | | | 2 | | 3.5, 2.6 |
| **link** | **39** | 38 / 0 / 1 | idle | **40** | 41 / 0 / 0 | idle |
| parallel walk | 38 | 38 / 0 / 0 | 774, 21.6 | 104 | 103 / 0 / 1 | 294, 3.8 |
| shard merge, remaining aliases | 7 | 7 / 0 / 1 | idle | 7 | 7 / 0 / 0 | idle |
| typecheck | 1539 | 1288 / 29 / 222 | 29385, 19.9 | 3201 | 3146 / 4 / 51 | 9430, 3.9 |

- **Link at T4 reads ~40-51 ms, not 80-96** (T4 runs: 51, 40, 40; T24: 43, 39, 40; nest 10-15). The 82-96 ms on the
  16-vCPU box is that box's (SMT/VM), not tttt's. Link's samples: binder_link 41-46%, resolve_alias_target_in ~35%.
- T24 load+parse: the main thread is the critical path again but now waits too: 27 ms blocked, 15 ms on the run
  queue (24 busy threads on 24 hardware threads), its top sample `__lll_lock_wait_private<program_add_source_idempotent`
  14% (a glibc-internal lock under it, malloc's arena lock most likely; not checked), then program_add_made_source 13%. The pool's top leaf is
  skip_balanced_braces_at 16-20% (the deferred bodies' skipper) and mmap of each file 8%.
- At T4 the front end is serial-ish by construction: the walk 104 ms at 3.8x, load+parse 223 ms at 4.9x.
- **nest at the defaults** (run 2, 79 ms to the end of typecheck): config 2, libs 16 (main blocked 8; the pool at 14x,
  mmap 22% of its samples), load+parse files 6, declare in isolation 4, declare merge 3, **link 10** (madvise of
  arena_populate 55% and munmap of arena_drop 18% of its samples: the arena's page faulting, not linking), parallel
  walk 7 (thread start-up in the main thread's samples), typecheck 27 (2.8x on 3 checkers). Before typecheck: 52 ms.
- The timeline's `_dl_mcount_wrapper<X` frames are probably a symbolization artefact of the LTO binary (read as X).

**Hardware counters, vscode** (`thirdparty/corpora/vscode/src/tsconfig.corpus.json --noEmit --pretty false`;
`perf_event_paranoid` set to -1 by the human; 10 events multiplexed at 50%):

| per 1000 instructions | K1 T1 (a Zen 5 core) | defaults (T24, K24) |
|---|---|---|
| instructions / cycles | 65.2 G / 46.1 G, **IPC 1.42** | 76.6 G / 101.0 G, **IPC 0.76** |
| branch-misses | 6.05 (2.7% of branches) | 7.20 (3.2%) |
| cache-references / cache-misses | 85.9 / 8.64 | 105.8 / 12.05 |
| L1-dcache-load-misses | 16.6 | 25.7 |
| L1-icache-load-misses | 10.3 | 9.6 |
| dTLB-load-misses | 0.11 | 1.01 |
| iTLB-load-misses | 0.004 | 1.08 |
| wall / user / sys | 9.59 / 9.06 / 0.46 s | 1.86 / 31.0 / 2.77 s |

- The defaults do 11.3 G more instructions (+17%: cross-checker re-derivation) at half the IPC (SMT, Zen 5c's 3.3
  GHz cores and 8 MB L3; kernel 13.4 G cycles / 5.6 G instructions of it). The defaults' TLB misses are user space
  (`:u` 74.3 M dTLB, 74.6 M iTLB; `:k` 1.6 M / 0.08 M): **iTLB L2 misses ~300x K1's** per instruction, unexplained
  (SMT siblings sharing the iTLB? the c-cores' TLBs? worth a `taskset` pair at T8 on the Zen 5 threads only).
- **Topdown at K1 (`-M PipelineL1`/`PipelineL2`, AMD's metric groups; `--topdown` isn't supported): frontend-bound
  53.2%, backend-bound 23.3%, bad speculation 7.7%, retiring 15.7%** (8 slots/cycle). Level 2: frontend latency 33.4%
  (cycles with no op at all from the front end) + bandwidth 19.8%; backend 22.3% memory, 1.0% CPU; bad speculation all
  mispredicts (7.5%); retiring 15.3% fastpath, 0.4% microcode. Pinned: Zen 5 (CPU 2) 9.50 s, 53.3 / 23.5 / 7.7 / 15.8;
  Zen 5c (CPU 6) 13.92 s, 55.3 / 19.5 / 8.3 / 16.7, the same instructions (IPC 1.52 at the lower clock). The defaults:
  frontend 51.2, backend 22.8, bad speculation 2.9, retiring 8.9, **SMT contention 11.8%**.
- **The front end is the stall, and it's the code footprint**: 5.4 MB of text; the instruction cache misses 23% of
  its fetch lookups (8.1-8.6 G of 35.8 G); the op cache hits 63% (14.8 G hits / 8.9 G misses) so 29% of ops (25.1 of
  86.4 G) come from the legacy x86 decoder; an L1 iTLB miss every 86 instructions (757 M, all L2 iTLB hits);
  instruction fills from beyond L2 312 M against 3.62 G from L2. On the data side demand fills are 571 M from L2,
  111 M from L3 (local CCX), 70 M from DRAM: **more instruction lines than data lines come from beyond L2**.
- Profiles (K1 T1; `perf record -g --call-graph dwarf -F 2000` with `perf report --no-children --sort symbol`, and
  `perf record -e EVENT -F 2000` with `--sort dso,symbol`; `LLC-load-misses` doesn't exist on this CPU:
  `ls_dmnd_fills_from_sys.dram_io_all`, demand data fills from DRAM, stands in; `L1-icache-load-misses` added for
  the frontend result; `k:` kernel; suffixes trimmed). Flat: no symbol over 3% of cycles, and the icache-miss profile
  is the cycle profile (misses spread over the whole hot path, not one function's):

| # | dwarf -g, --no-children (cycles) | cycles | L1-dcache-load-misses | L1-icache-load-misses | DRAM demand fills |
|---|---|---|---|---|---|
| 1 | 3.03 type_at_flow_node | 2.68 type_at_flow_node | 5.88 k:clear_pages | 2.73 type_at_flow_node | 4.28 k:clear_pages |
| 2 | 1.90 find_prop_in | 2.45 atom_table_get | 3.95 type_at_flow_node | 2.55 atom_table_get | 3.21 unused_report |
| 3 | 1.73 atom_table_get | 2.33 find_prop_in | 3.16 find_prop_in | 2.53 find_prop_in | 3.00 k:_raw_spin_lock |
| 4 | 1.61 call_return_type | 1.84 recursive_related | 2.66 compute_inner | 2.18 compute_inner | 2.14 recursive_related |
| 5 | 1.56 k:clear_pages | 1.73 call_return_type | 2.31 call_effects | 2.15 expr_typecheck | 2.11 program_resolve_names |
| 6 | 1.53 compute_inner | 1.73 compute_inner | 2.21 atom_table_get | 1.86 recursive_related | 2.09 find_prop_in |
| 7 | 1.53 recursive_related | 1.47 skip_balanced_braces_at | 1.61 call_return_type | 1.55 parse_expr | 2.05 report_variables |
| 8 | 1.47 expr_lookup_names | 1.40 parse_expr | 1.58 expr_typecheck | 1.49 call_return_type | 1.98 atom_table_get |
| 9 | 1.32 check_object | 1.37 expr_lookup_names | 1.34 reorder_candidates | 1.23 is_related_to | 1.92 call_return_type |
| 10 | 1.27 skip_balanced_braces_at | 1.36 expr_typecheck | 1.09 check_object | 1.21 check_with | 1.83 k:map_anon_folio_pte_nopf |
| 11 | 1.23 parse_expr_right_side | 1.22 check_object | 1.07 parse_expr | 1.04 check_object | 1.77 compute_inner |
| 12 | 1.21 expr_typecheck | 1.10 k:clear_pages | 1.03 recursive_related | 1.03 resolve_with_signatures_inner | 1.39 reorder_candidates |
| 13 | 1.16 parse_expr | 1.10 scope_resolve | 0.97 ty_access_key_type | 1.02 ty_access_key_type | 1.26 ty_fresh_literal |
| 14 | 1.03 scope_resolve | 1.01 is_related_to | 0.95 ty_type_of_symbol | 0.99 lazy_type_evaluate_inner | 1.25 check_object |
| 15 | 0.97 props_related_impl | 0.88 reachability_walk | 0.86 expr_lookup_names | 0.97 ty_expr_type | 1.03 is_related_to |
| 16 | 0.94 identifier_flow_type | 0.87 props_related_impl | 0.86 class_instance_member | 0.93 reachability_walk | 1.01 instantiate_anonymous |
| 17 | 0.84 check_with | 0.87 parse_expr_right_side | 0.86 reachability_walk | 0.91 expr_lookup_names | 0.99 program_materialize_locked |
| 18 | 0.83 infer_from | 0.86 lazy_type_evaluate_inner | 0.84 lazy_type_evaluate_inner | 0.90 reorder_candidates | 0.89 reachability_walk |
| 19 | 0.82 lazy_type_evaluate_inner | 0.84 ty_access_key_type | 0.84 ty_string_literal | 0.86 identifier_flow_type | 0.83 k:__mem_cgroup_charge |
| 20 | 0.82 resolve_with_signatures_inner | 0.79 check_with | 0.82 ty_link_get | 0.79 ty_string_literal | 0.79 binder_link |
- By category (all symbols, cycles / L1d misses / L1i misses / DRAM fills, %): parser+lexer 9.8 / 4.4 / 9.4 / 0.3
  (skip_balanced_braces_at 1.5, parse_expr 1.4); name resolution 7.4 / 5.1 / 4.7 / 9.3; hash and intern tables
  (atom_table_get, intern_*, ty_link_get, symbol tables) 4.5 / 4.4 / 3.8 / 4.3, atom_table_get alone 2.5 / 2.2 / 2.5
  / 2.0, ty_link_get 0.4 / 0.8; malloc/free/arena 2.0 / 1.6 / 2.5 / 1.7; kernel 5.7 / 9.5 / 2.8 / 16.4 (clear_pages:
  zeroing fresh pages, 1.1% of cycles but 5.9% of L1d misses and 4.3% of DRAM fills); the checker's own code the
  remaining ~73%. The DRAM-fill top has unused_report 3.2 and report_variables 2.0 (the unused pass, walking cold
  memory), program_resolve_names 2.1. Stalls don't land in the hash tables, the Ty header reads (no single reader) or
  allocation: they land on fetching the checker's code.
- Direction this suggests (not tried): code layout and size (hot/cold splitting beyond PGO's, `-fno-plt`-style and
  `--icf`, BOLT on pgo/tttt, fewer near-duplicate inlined copies, `-Os` on cold units), and huge pages for text
  (`CONFIG_READ_ONLY_THP_FOR_FS` / remapping .text to a 2 MB page) for the iTLB.

**drizzle K12, plain vs `TTTT_SHARED_GRAPH=1`** (`perf stat -r 5`): user instructions 8.11 G -> 5.98 G (**-26%**),
user cycles 6.55 G -> 5.94 G (-9%: IPC 1.24 -> 1.01), kernel cycles 1.80 -> 1.49 G, L1d misses 183 -> 155 M, DRAM
demand fills 17.8 -> 15.9 M, L3/other-L2 fills 15.7 -> 18.9 M (+20%: sharing moves lines between cores); wall 0.300
-> 0.273 s (+-0.7%). The graph removes a quarter of the work but the remaining work runs a fifth slower.

## The 12-core box at f2ad73c9: the new defaults' laptop bench (2026-10-21, run by an agent on the human's laptop)
The new defaults (checkers past the cores for big programs, `--checker-memory` a quarter of RAM). Same machine
as below (on AC, `balanced`, load ~1), pgo/tttt retrained at f2ad73c9, `scripts/bench.sh --runs 3 --checkers 12` (no
bun/tsc-rs: their binaries were scratch). Best wall s / CPU s (median run) / peak MB; "5f2cd699 K12/K1" is the table
below's plain columns:

| corpus | tttt default | tttt K12 | 5f2cd699 K12 | tttt single | 5f2cd699 K1 | tsgo |
|---|---|---|---|---|---|---|
| TypeScript6 | 0.21 / 1.1 / 489 | 0.23 / 1.2 / 483 | 0.22 / 1.1 / 438 | 0.47 / 0.5 / 229 | 0.53 / 0.5 / 228 | 0.60 / 3.4 / 500 |
| type-fest | 0.31 / 1.6 / 891 | 0.32 / 1.7 / 905 | 0.23 / 1.3 / 680 | 0.88 / 0.9 / 563 | 0.72 / 0.7 / 432 | 5.60 / 24.3 / 5411 |
| valibot | 0.14 / 1.2 / 517 | 0.13 / 1.2 / 524 | 0.14 / 1.3 / 387 | 0.38 / 0.4 / 257 | 0.44 / 0.4 / 255 | 0.71 / 4.7 / 803 |
| bulletproof-react | 0.10 / 0.5 / 304 | 0.09 / 0.7 / 408 | 0.13 / 0.4 / 277 | 0.15 / 0.2 / 147 | 0.18 / 0.2 / 145 | 0.42 / 2.3 / 446 |
| nestjs-boilerplate | 0.08 / 0.5 / 264 | 0.08 / 0.6 / 304 | 0.08 / 0.4 / 265 | 0.14 / 0.2 / 133 | 0.18 / 0.2 / 131 | 0.14 / 1.4 / 257 |
| webpack | 0.27 / 2.6 / 861 | 0.28 / 2.7 / 860 | 0.29 / 2.9 / 631 | 1.03 / 1.0 / 540 | 1.16 / 1.2 / 533 | 1.21 / 7.8 / 1173 |
| zod | 0.18 / 1.8 / 866 | 0.19 / 1.8 / 856 | 0.23 / 2.2 / 556 | 0.54 / 0.5 / 446 | 0.68 / 0.7 / 388 | 1.09 / 8.2 / 1361 |
| xstate | 0.14 / 1.2 / 458 | 0.13 / 1.1 / 485 | 0.13 / 1.2 / 420 | 0.29 / 0.3 / 197 | 0.33 / 0.3 / 194 | 0.41 / 3.0 / 492 |
| drizzle | 0.30 / 2.8 / 1093 | 0.29 / 3.0 / 1123 | 0.32 / 3.1 / 670 | 0.68 / 0.7 / 467 | 0.84 / 0.8 / 393 | 1.46 / 9.0 / 1616 |
| vue | 0.17 / 1.7 / 569 | 0.17 / 1.8 / 556 | 0.17 / 1.6 / 471 | 0.58 / 0.6 / 308 | 0.68 / 0.7 / 305 | 0.90 / 5.5 / 737 |
| effect | 0.30 / 3.2 / 909 | 0.29 / 3.1 / 916 | 0.26 / 2.8 / 634 | 1.11 / 1.1 / 625 | 1.27 / 1.3 / 509 | 1.50 / 11.0 / 1549 |
| vscode | **1.93** / 35.4 / 5375 | 2.02 / 22.9 / 5014 | 2.45 / 27.4 / 1581 | 9.90 / 10.0 / 4087 | 12.44 / 12.5 / 1400 | 8.26 / 55.1 / 7879 |
| excalidraw | 0.21 / 2.1 / 730 | 0.24 / 2.3 / 727 | 0.22 / 2.4 / 544 | 0.71 / 0.7 / 375 | 0.84 / 0.8 / 370 | 1.53 / 11.0 / 1796 |
| tanstack-router | 0.20 / 1.6 / 579 | 0.19 / 1.7 / 589 | 0.20 / 1.6 / 455 | 0.53 / 0.5 / 316 | 0.61 / 0.6 / 309 | 1.26 / 7.2 / 901 |
| TS6 -b | 0.57 / 3.1 / 532 | - | 0.58 / 2.9 / 539 | - | - | 1.38 / 8.1 / 668 |

- **The defaults stay.** The default picks 24 checkers only for vscode (12 elsewhere, 2-3 on react/nest): vscode
  2.45 -> 1.93 s (K12 now 2.02: the checker-speed work alone is most of the gain; K24 adds ~5%, at 35 s CPU against
  23), as expected. Every other corpus is within noise of its K12 run and of 5f2cd699; nothing lost.
- Single-threaded improved 7-20% everywhere since 5f2cd699 (vscode 12.44 -> 9.90) but type-fest (0.72 -> 0.88, and
  0.23 -> 0.31 at K12). Spot checks right after the bench gave 0.62-0.75 s single-threaded with either memory default:
  probably noise/thermals in the long run, unconfirmed; rerun type-fest alone if it matters.
- **Memory** is the price of the memory default: vscode 5.4 GB at the default (2.4x bun's 2241 at 5f2cd699, inside
  the ~3x budget), 4.1 GB single-threaded (1.4 GB before); smaller corpora +20-60% (drizzle 670 -> 1093 MB). type-fest
  K1 with `--checker-memory 256`: 450-570 MB against 575-690, same time.

## The 12-core box at 5f2cd699: the flag's laptop bench (2026-10-21, run by an agent on the human's laptop)
The shared graph's release bench. Same machine as below (on AC, `balanced`), pgo/tttt at 5f2cd699 (the shared graph's
end state). `scripts/bench.sh --runs 3 --checkers 24 --bun bun-canary --tscrs tsc-rs-0.7.0`, run twice back to back: plain,
then with `TTTT_SHARED_GRAPH=1` ("flag"; it also reran bun/tsc-rs/tsgo, which agree with the first pass within
noise). bun canary `1.4.3-canary.20261019.1` (the same as below). Best wall s / CPU s (median run) / peak MB:

| corpus | tttt K12 | tttt K12 flag | K24 | K24 flag | K1 | K1 flag | bun | bun single | tsc-rs --fast | tsgo |
|---|---|---|---|---|---|---|---|---|---|---|
| TypeScript6 | 0.22 / 1.1 / 438 | 0.23 / 1.2 / 403 | 0.24 / 1.5 / 500 | 0.25 / 1.5 / 431 | 0.53 / 0.5 / 228 | 0.54 / 0.5 / 230 | 0.29 / 1.6 / 282 | 1.01 / 1.2 / 163 | 0.51 / 1.4 / 328 | 0.61 / 3.1 / 498 |
| type-fest | 0.23 / 1.3 / 680 | 0.31 / 1.7 / 761 | 0.22 / 2.0 / 863 | 0.31 / 2.4 / 791 | 0.72 / 0.7 / 432 | 0.77 / 0.8 / 567 | 0.98 / 6.0 / 980 | 3.62 / 4.1 / 783 | 1.78 / 5.1 / 3287 | 4.46 / 19.6 / 5473 |
| valibot | 0.14 / 1.3 / 387 | 0.17 / 1.4 / 444 | 0.14 / 1.9 / 447 | 0.16 / 2.0 / 472 | 0.44 / 0.4 / 255 | 0.47 / 0.5 / 264 | 0.14 / 2.4 / 263 | 1.19 / 1.2 / 158 | 0.23 / 2.2 / 644 | 0.71 / 4.7 / 810 |
| bulletproof-react | 0.13 / 0.4 / 277 | 0.11 / 0.4 / 282 | 0.10 / 1.1 / 493 | 0.09 / 0.8 / 396 | 0.18 / 0.2 / 145 | 0.18 / 0.2 / 149 | 0.18 / 0.9 / 241 | 0.35 / 0.3 / 153 | 0.27 / 0.7 / 306 | 0.43 / 2.5 / 448 |
| nestjs-boilerplate | 0.08 / 0.4 / 265 | 0.08 / 0.5 / 274 | 0.09 / 0.6 / 349 | 0.08 / 0.6 / 355 | 0.18 / 0.2 / 131 | 0.17 / 0.2 / 133 | 0.06 / 0.6 / 195 | 0.27 / 0.3 / 117 | 0.12 / 0.6 / 240 | 0.12 / 1.2 / 258 |
| webpack | 0.29 / 2.9 / 631 | 0.31 / 2.9 / 739 | 0.28 / 4.1 / 741 | 0.31 / 4.0 / 767 | 1.16 / 1.2 / 533 | 1.22 / 1.2 / 541 | 0.48 / 4.8 / 502 | 1.82 / 1.8 / 336 | 0.50 / 4.9 / 1209 | 1.18 / 7.6 / 1160 |
| zod | 0.23 / 2.2 / 556 | 0.22 / 2.0 / 662 | 0.23 / 3.4 / 755 | 0.24 / 3.0 / 697 | 0.68 / 0.7 / 388 | 0.67 / 0.7 / 442 | 0.22 / 3.2 / 386 | 1.83 / 1.8 / 210 | 0.30 / 2.5 / 864 | 1.08 / 7.4 / 1340 |
| xstate | 0.13 / 1.2 / 420 | 0.14 / 1.1 / 382 | 0.16 / 2.2 / 574 | 0.15 / 1.6 / 458 | 0.33 / 0.3 / 194 | 0.34 / 0.4 / 199 | 0.13 / 1.4 / 237 | 0.73 / 1.0 / 110 | 0.22 / 1.0 / 322 | 0.41 / 3.1 / 465 |
| drizzle | 0.32 / 3.1 / 670 | 0.28 / 2.6 / 715 | 0.35 / 5.8 / 941 | 0.24 / 3.6 / 802 | 0.84 / 0.8 / 393 | 0.83 / 0.8 / 479 | 0.27 / 4.5 / 440 | 1.74 / 2.4 / 270 | 0.55 / 5.7 / 1684 | 1.42 / 8.7 / 1633 |
| vue | 0.17 / 1.6 / 471 | 0.35 / 2.1 / 526 | 0.18 / 2.5 / 515 | 0.44 / 3.0 / 560 | 0.68 / 0.7 / 305 | 0.71 / 0.7 / 307 | 0.22 / 2.7 / 315 | 1.32 / 1.4 / 180 | 0.31 / 2.7 / 679 | 0.89 / 5.2 / 745 |
| effect | 0.26 / 2.8 / 634 | 0.34 / 3.4 / 856 | 0.25 / 4.1 / 751 | 0.33 / 4.8 / 901 | 1.27 / 1.3 / 509 | 1.35 / 1.3 / 622 | 0.55 / 6.2 / 513 | 2.69 / 2.8 / 336 | 0.88 / 9.1 / 1580 | 1.41 / 11.1 / 1537 |
| vscode | 2.45 / 27.4 / 1581 | 2.45 / 25.8 / 2924 | 2.03 / 36.7 / 1990 | 2.15 / 36.1 / 3012 | 12.44 / 12.5 / 1400 | 12.41 / 12.3 / 2400 | 1.74 / 36.4 / 2241 | 15.62 / 16.6 / 1664 | 3.20 / 33.5 / 6376 | 8.24 / 57.1 / 7889 |
| excalidraw | 0.22 / 2.4 / 544 | 0.20 / 1.9 / 575 | 0.24 / 3.5 / 712 | 0.21 / 2.8 / 646 | 0.84 / 0.8 / 370 | 0.87 / 0.9 / 374 | 0.26 / 3.2 / 379 | 1.36 / 1.5 / 256 | 0.76 / 9.4 / 948 | 1.47 / 11.9 / 1830 |
| tanstack-router | 0.20 / 1.6 / 455 | 0.22 / 1.8 / 512 | 0.22 / 2.7 / 618 | 0.22 / 2.3 / 576 | 0.61 / 0.6 / 309 | 0.65 / 0.6 / 317 | 0.24 / 3.3 / 367 | 1.14 / 1.2 / 187 | 0.48 / 2.4 / 525 | 1.21 / 6.9 / 892 |
| TS6 -b | 0.58 / 2.9 / 539 | 0.59 / 3.0 / 548 | - | - | - | - | - | - | - | 1.38 / 7.5 / 562 |

- **The flag does not win wall at the defaults.** K12: drizzle 0.32 -> 0.28 and excalidraw 0.22 -> 0.20 gain, vscode
  ties (2.45), zod/xstate/tanstack/webpack/TypeScript6 tie within 0.02; it loses on **vue 0.17 -> 0.35 (2x, medians
  0.17 / 0.37; K24 0.18 -> 0.44)**, type-fest 0.23 -> 0.31, effect 0.26 -> 0.34, valibot 0.14 -> 0.17. K1 is unchanged
  on vue (0.68 / 0.71), so vue's loss is contention or waiting between workers, not extra work: uninvestigated.
- At K24 the flag helps where K24 used to hurt: drizzle 0.35 -> 0.24 (CPU 5.8 -> 3.6), xstate CPU 2.2 -> 1.6,
  excalidraw 3.5 -> 2.8 s CPU; vscode K24 2.03 plain / 2.15 flag (K24 now beats K12 on vscode in both).
- **Memory**: vscode 1581 -> 2924 MB at K12 with the flag (1.3x bun's 2241, inside the ~3x budget), K1 1400 -> 2400;
  effect 634 -> 856, type-fest 432 -> 567 at K1. Smaller corpora +5-20%.
- K1 cost of the flag: +3-7% on type-fest, effect, tanstack, webpack; flat elsewhere (vscode 12.44 / 12.41).
- **bun canary still has the best vscode wall** (1.74 against tttt's 2.45 K12 / 2.03 K24); at K12 plain tttt leads bun on 8 of
  14, ties valibot/zod/xstate, loses nest (0.08 / 0.06), drizzle (0.32 / 0.27; the flag's K24 0.24 wins) and vscode. Single-threaded tttt is fastest everywhere (vscode 12.4 against bun 15.6 and tsc-rs 16.1).
- Raw outputs were scratch (not kept); rerun the command above to reproduce.

## The config glob next to the load pool (2026-10-21, this box: 16 vCPUs / 8 cores, load 10-25)
Since 0eca94ef9 the pool parses while the include glob walks; the glob (vscode, T16) went from 28-30 ms to 68-81 ms.
`--time` now prints the main thread's CPU time per phase: the glob's is 30-32 ms alone and 55-59 ms next to the
pool, so the extra wall is on-CPU, not waiting. The sampler (CPU mode against wall mode, 10-12 runs each) shows no
lock: the pool's and the resolver's locks are ~3% of its wall, and every function of the glob slows about
uniformly (glob_matches 1.7x, getdents 1.3x, hash lookups 1.3x, brk/morecore 2.8x): the SMT sibling and all-core
clocks. Giving the main thread a core (only 4 or 7 of the 16 workers started until the glob ends) brought the glob
back to 37-39 ms, but config + libs + load+parse stayed at 218-226 ms best against 202-207 with every worker:
what the glob gains, the pool's parse loses. A 64 MB M_TOP_PAD for the main arena gave 61-62 against 67-70 ms
(noise-sized). Nothing changed; the glob's own 250 M instructions (glob_matches(), path strings) are what is left.

## The 12-core box at efb91998 (2026-10-20, run by an agent on the human's laptop)
AMD Ryzen AI 9 HX 370 (12 cores / 24 threads), on AC, `balanced` profile, GCC 16.2. Answers 2026-10-20's ask.
**Costliest file first, A/B of the same pgo/tttt** (`TTTT_NO_COSTLIEST_FIRST=1`, 3 runs each at the default K12,
wall s / CPU s): TypeScript6 0.24-0.27 / 1.1 on, 0.21-0.26 / 1.1 off; type-fest 0.21-0.25 on and off; drizzle 0.33-0.34 /
3.2 on, 0.32-0.33 / 3.1 off; vscode 2.41-2.52 / 25.9 on, 2.46-2.49 / 25.9 off. **A wash on 12 cores**: the 4-vCPU
box's -6% on TypeScript6 doesn't show (its wall is 0.24 s here, the front end is most of it). vscode's front end at
K12: libs 40 ms, load+parse files 259 ms (loader waited 14 ms; it parsed 83 ms itself, 744 of 11035 files; workers
busy 5.6 of 7.2 s), resolve names 203 ms (declare 71, link 51, walk 44), typecheck 1852 ms: **~0.5 s of vscode's
2.5 s is the front end**, as the front-end estimate assumed.

**Full bench**: `scripts/bench.sh --runs 3 --checkers 24 --bun bun-canary --tscrs tsc-rs-0.7.0`, bun canary
`1.4.3-canary.20261019.1` (1.4.4-canary.1+6bb5dc195); best wall s / CPU s (median run) / peak MB:
| corpus | tttt K12 | tttt K24 | tttt K1 | bun canary | bun canary single | tsc-rs | tsc-rs --fast | tsgo |
|---|---|---|---|---|---|---|---|---|
| TypeScript6 | 0.24 / 1.13 / 426 | 0.21 / 1.48 / 518 | 0.44 / 0.44 / 225 | 0.26 / 1.76 / 284 | 0.69 / 1.07 / 168 | 0.42 / 1.22 / 335 | 0.41 / 1.24 / 329 | 0.57 / 2.97 / 498 |
| type-fest | 0.23 / 1.32 / 625 | 0.23 / 1.93 / 859 | 0.66 / 0.66 / 430 | 0.94 / 6.34 / 1011 | 4.56 / 4.57 / 788 | 2.45 / 5.49 / 3134 | 1.48 / 4.63 / 3287 | 4.01 / 17.78 / 5452 |
| valibot | 0.14 / 1.15 / 403 | 0.15 / 2.00 / 466 | 0.38 / 0.38 / 253 | 0.15 / 2.41 / 267 | 1.19 / 1.29 / 157 | 0.35 / 1.49 / 486 | 0.21 / 2.20 / 646 | 0.69 / 4.54 / 809 |
| bulletproof-react | 0.13 / 0.43 / 281 | 0.11 / 1.13 / 487 | 0.19 / 0.18 / 142 | 0.17 / 0.93 / 241 | 0.51 / 0.55 / 151 | 0.22 / 0.67 / 311 | 0.24 / 0.67 / 306 | 0.39 / 2.55 / 450 |
| nestjs-boilerplate | 0.09 / 0.54 / 279 | 0.10 / 0.78 / 359 | 0.16 / 0.17 / 135 | 0.06 / 0.63 / 196 | 0.17 / 0.29 / 120 | 0.12 / 0.54 / 220 | 0.11 / 0.56 / 237 | 0.13 / 1.31 / 257 |
| webpack | 0.30 / 2.90 / 625 | 0.31 / 4.60 / 738 | 1.00 / 0.99 / 528 | 0.45 / 5.90 / 505 | 2.19 / 2.25 / 333 | 0.64 / 2.90 / 841 | 0.45 / 5.13 / 1209 | 1.19 / 7.76 / 1166 |
| zod | 0.20 / 1.89 / 536 | 0.22 / 3.49 / 754 | 0.55 / 0.55 / 393 | 0.25 / 3.66 / 394 | 1.03 / 1.03 / 217 | 0.53 / 2.03 / 792 | 0.34 / 2.54 / 866 | 1.06 / 7.31 / 1346 |
| xstate | 0.14 / 1.22 / 408 | 0.17 / 2.37 / 569 | 0.29 / 0.28 / 192 | 0.14 / 1.49 / 239 | 0.52 / 0.60 / 111 | 0.22 / 1.17 / 324 | 0.26 / 1.14 / 323 | 0.41 / 2.74 / 473 |
| drizzle | 0.35 / 3.49 / 691 | 0.37 / 6.74 / 962 | 0.74 / 0.74 / 398 | 0.30 / 5.23 / 439 | 1.98 / 2.02 / 273 | 0.70 / 2.90 / 999 | 0.53 / 6.89 / 1684 | 1.51 / 9.96 / 1594 |
| vue | 0.18 / 1.68 / 475 | 0.19 / 2.69 / 567 | 0.58 / 0.58 / 308 | 0.23 / 3.13 / 313 | 0.95 / 1.34 / 179 | 0.51 / 2.03 / 496 | 0.28 / 2.98 / 675 | 0.85 / 4.95 / 736 |
| effect | 0.26 / 2.73 / 611 | 0.29 / 4.68 / 773 | 1.10 / 1.10 / 516 | 0.58 / 8.10 / 515 | 2.70 / 3.30 / 329 | 1.30 / 5.93 / 1261 | 0.81 / 9.54 / 1578 | 1.43 / 10.43 / 1555 |
| vscode | 2.67 / 28.55 / 1595 | 2.47 / 41.40 / 1893 | 10.51 / 10.54 / 1371 | 2.05 / 44.21 / 2250 | 17.09 / 17.22 / 1651 | 5.02 / 21.61 / 5498 | 3.02 / 35.64 / 6372 | 7.81 / 56.75 / 7839 |
| excalidraw | 0.25 / 2.47 / 556 | 0.25 / 4.12 / 726 | 0.73 / 0.73 / 367 | 0.24 / 3.52 / 382 | 1.65 / 1.75 / 255 | 0.75 / 2.96 / 614 | 0.88 / 11.05 / 948 | 1.47 / 10.34 / 1795 |
| tanstack-router | 0.19 / 1.64 / 447 | 0.19 / 2.31 / 580 | 0.53 / 0.53 / 307 | 0.22 / 2.78 / 375 | 1.03 / 1.65 / 186 | 0.51 / 1.79 / 536 | 0.42 / 1.91 / 528 | 1.21 / 6.60 / 984 |

- Against 2026-10-19's canary table: tttt K1 is unchanged (vscode 10.51 against 10.61), the canary's numbers are the
  same within noise. tttt K12's CPU reads 5-20% higher on some corpora (vscode 28.6 against 25.1 s, drizzle 3.5
  against 2.9) but the A/B above, minutes earlier, measured vscode at 25.4-26.6 s and drizzle 3.1 s: run-to-run frequency
  noise on a laptop, not a code change (K1 flat). Walls equal within 0.05 s except vscode (2.67 here, 2.41-2.52 in
  the A/B).
- **bun canary still beats tttt's default wall on vscode (2.05 against 2.47-2.67), drizzle (0.30 / 0.35), nest (0.06
  / 0.09) and ties excalidraw, xstate, valibot**; tttt wins the other 8 and every K1 (1.5-7x), and uses less CPU
  everywhere at the defaults (vscode 28.6 against 44.2 s). Nothing landed since 2026-10-19 that should change this:
  the shared graph was the lever then (since parked: notes/shared_graph.md).
- bun's "single-threaded" CPU exceeds its wall on several corpora (TypeScript6 0.69 s wall / 1.07 s CPU, vue,
  tanstack) and its best and median differ widely: it isn't strictly one thread. Compare medians for bun single.
- tsc-rs and tsgo: unchanged picture (tsc-rs --fast is the closest tsgo-exact competitor, vscode 3.02 s at 35.6 s CPU
  and 6.4 GB).

## Costliest file first and the front end's split (2026-10-20; this box, 4 vCPUs, release build)
Each checker now starts with its share's biggest file (`deal_files()`'s `share_first`; `TTTT_NO_COSTLIEST_FIRST=1`
turns it off). Best of 5 at the defaults (K4 T4), wall / CPU s / peak MB, before -> after: TypeScript6/src/compiler
0.31 / 0.92 / 235 -> 0.29 / 0.90 / 262, type-fest 0.49 / 1.42 / 413 -> 0.48 / 1.40 / 413; webpack, zod, xstate,
drizzle, vue, effect, excalidraw, tanstack-router, valibot, nest and vscode (6.13 -> 6.07) are within noise.
Output byte-identical (checkerdiff K4, threadcheck); every oracle unchanged but kdiff 2 -> 4: sequelize's TS7022,
which depends on the checker count in tsgo too (tsgo_divergences.md), now shows at K16. What the timeline (`TTTT_FILE_TIMES=1`, now with each file's
end time per checker) shows:
- TypeScript6: checker.ts alone is the critical path, ~65 ms materializing its deferred bodies (serial, the file's
  lock) and 195 ms checking it cold, while the other three finish at ~190 ms. `--no-defer-bodies` is 0.27 s: the
  biggest file's body parse sits on the critical path when it is checked first. Splitting one file's materialization,
  or parsing the costliest file's bodies up front in the loader, are the levers left; both are front-end work
  (notes/front_end.md).
- type-fest: size predicts nothing. union-to-tuple.ts is 1.4 KB and 180 ms at K1, the next are int-range.ts (2.3 KB,
  68 ms) and array-reverse.ts. bun's cost model (identifiers in expressions + type nodes) wouldn't see them either:
  their cost is type-level evaluation. So no second cost model was tried.
- A first version let the costliest file set its segment's recycle warm-up base (`segment_base`): excalidraw and
  xstate got 5-10% faster but excalidraw's peak rose 34 MB, because checker 0 held 2x its first file's 23 MB before
  recycling. That gain was fewer recycles, not ordering. The costliest file no longer sets the base.

The front end (`--time`, `TTTT_LOAD_POOL_STATS=1`, `TTTT_RESOLVE_TIMES=1`), ms:
| corpus, threads | libs | load+parse | resolve | of which declare + link (serial) / walk | check |
|---|---|---|---|---|---|
| vscode T4 | 34 | 454 | 318 | 138 / 130 | 5226 |
| vscode T1 | 10 | 866 | 551 | 135 / 365 | 11873 |
| webpack T4 | 15 | 85 | 85 | | 433 |
| webpack T1 | 9 | 231 | 165 | | 857 |
| effect T4 | 13 | 35 | 48 | | 512 |
| effect T1 | 10 | 100 | 89 | | 1187 |
On vscode at T4 the load pool's workers are busy 1779 of 1953 ms, so load is CPU-bound here (1953 / 4 = 488 ms).
The loader's own work is 279 ms, 201 ms of it parsing 3075 of 11035 files itself, because they weren't preparsed
yet when it needed their imports. On 12 cores the parse CPU is ~163 ms, so **the loader's serial 279 ms is the
front end's floor, then declare + link's 138 ms**. The front end's order then: (1) the import pre-scan, so that the loader
never parses; (2) parallel declare and link; (3) the parser itself, which is CPU and so helps 4-core boxes most.
webpack and effect show the same shape at a smaller scale (their pools are saturated at T4).

## PGO + LTO shipped (2026-10-19)
`scripts/pgobuild.sh` (pgo/tttt) is what bench.sh, instrcount.sh and perfsweep.py measure from now on. Callgrind
(`scripts/instrcount.sh`, re-saved: compare against it, not older release numbers) against release -O3:
tsc 4.530 -> 3.893 G (-14.1%, untrained), type-fest -12.9%, valibot -15.1%, react -12.7%, nest -11.5%, webpack
-14.6%, zod -16.2%, xstate -15.2%, drizzle -16.5%, vue -15.5%, effect -16.4%, vscode 89.06 -> 75.52 G (-15.2%,
untrained), excalidraw -16.9%, tanstack-router -15.9%. Wall K1-ish, one run, release -> pgo: vscode 12.37 -> 10.93 s,
tsc 0.67 -> 0.54, vue 0.79 -> 0.62, zod 0.78 -> 0.74. `scripts/bench.sh --runs 3` on the 4-vCPU box (pgo/tttt;
best wall s / CPU s / peak MB; drizzle's defaults above its K1 is the 4-core box's known checker split):
| corpus | tttt defaults | tttt K1 | tsgo defaults | tsgo single |
|---|---|---|---|---|
| TypeScript6 | 0.480 / 0.94 / 232 | 0.670 / 0.78 / 221 | 1.780 / 3.33 / 510 | 2.300 / 2.71 / 417 |
| type-fest | 0.860 / 1.51 / 425 | 0.890 / 0.90 / 425 | 12.130 / 22.06 / 5718 | 11.700 / 15.38 / 4334 |
| valibot | 0.580 / 0.92 / 169 | 0.680 / 0.69 / 251 | 2.540 / 5.14 / 832 | 2.860 / 3.91 / 652 |
| bulletproof-react | 0.210 / 0.41 / 160 | 0.250 / 0.25 / 139 | 1.420 / 2.52 / 501 | 1.140 / 1.35 / 290 |
| nestjs-boilerplate | 0.190 / 0.28 / 147 | 0.230 / 0.25 / 130 | 0.590 / 0.91 / 232 | 0.600 / 0.72 / 192 |
| webpack | 1.220 / 2.27 / 400 | 1.780 / 1.77 / 522 | 4.090 / 7.79 / 1167 | 5.420 / 6.03 / 955 |
| zod | 0.650 / 1.50 / 275 | 0.690 / 0.70 / 388 | 2.800 / 7.69 / 1419 | 3.140 / 4.59 / 962 |
| xstate | 0.280 / 0.75 / 185 | 0.340 / 0.36 / 190 | 1.010 / 2.76 / 460 | 1.130 / 1.54 / 338 |
| drizzle | 1.280 / 2.24 / 361 | 0.930 / 0.94 / 393 | 5.010 / 9.73 / 1683 | 3.390 / 4.42 / 880 |
| vue | 0.810 / 1.27 / 221 | 1.060 / 1.05 / 300 | 2.450 / 5.73 / 850 | 2.310 / 3.40 / 675 |
| effect | 0.810 / 2.25 / 331 | 1.610 / 1.81 / 511 | 5.760 / 11.13 / 1592 | 4.300 / 6.49 / 1241 |
| vscode | 6.100 / 21.56 / 1235 | 13.150 / 15.60 / 1363 | 18.030 / 64.25 / 8072 | 28.700 / 37.25 / 6628 |
| excalidraw | 0.520 / 1.86 / 273 | 0.860 / 0.85 / 361 | 2.970 / 10.78 / 1975 | 2.770 / 3.80 / 899 |
| tanstack-router | 0.300 / 1.09 / 282 | 0.610 / 0.64 / 302 | 2.040 / 6.91 / 977 | 2.590 / 3.52 / 673 |
| `-b` TypeScript6/src | 1.040 / 3.49 / 325 | - | 2.050 / 6.41 / 742 | 3.000 / 3.95 / 512 |

## bun canary with oven-sh/bun#45800 (2026-10-19, the human's laptop)
The bun developer claimed a 1.5-2x speedup in `bun check` over 1.4.3. It is oven-sh/bun PR #45800 ("`bun check`: 1.4x
to 7.8x faster on large projects"), merged 2026-10-19 00:42 UTC, and it is in `@oven/bun-linux-x64@1.4.3-canary.20261019.1`
(`--revision` 1.4.4-canary.1+6bb5dc195). The PR's ideas: one heavy-first file queue with no step barriers and results
shared in small commits; union order from type contents rather than ids (the shared graph's question); per-member
union property position sets; memoized inference and cached conditional/mapped parts; 20-byte expressions; labelled
tuples sharing members; a parallel loader. Same box as the section below, best of 3, wall s / CPU s / peak MB:
| corpus | tttt K12 | bun 1.4.3 | bun canary | 1.4.3 / canary wall | tttt K1 | bun 1.4.3 single | bun canary single | 1.4.3 / canary single |
|---|---|---|---|---|---|---|---|---|
| TypeScript6 | 0.25 / 1.20 / 439 | 0.36 / 2.07 / 350 | 0.27 / 1.54 / 280 | 1.33 | 0.45 / 0.44 / 225 | 1.08 / 1.07 / 214 | 0.99 / 0.98 / 162 | 1.09 |
| type-fest | 0.21 / 1.25 / 635 | 0.96 / 5.80 / 824 | 0.95 / 5.60 / 1020 | 1.01 | 0.65 / 0.64 / 428 | 3.74 / 3.73 / 1010 | 3.06 / 3.05 / 784 | 1.22 |
| valibot | 0.13 / 1.13 / 399 | 0.23 / 2.90 / 277 | 0.14 / 2.26 / 268 | 1.64 | 0.39 / 0.39 / 255 | 1.10 / 1.09 / 277 | 1.18 / 1.18 / 158 | 0.93 |
| bulletproof-react | 0.13 / 0.42 / 278 | 0.28 / 1.48 / 367 | 0.18 / 0.92 / 239 | 1.56 | 0.19 / 0.18 / 143 | 0.53 / 0.52 / 175 | 0.43 / 0.42 / 155 | 1.23 |
| nestjs-boilerplate | 0.09 / 0.52 / 274 | 0.11 / 0.68 / 235 | 0.06 / 0.61 / 196 | 1.83 | 0.19 / 0.19 / 135 | 0.39 / 0.38 / 154 | 0.21 / 0.21 / 119 | 1.86 |
| webpack | 0.31 / 2.83 / 606 | 0.57 / 5.60 / 526 | 0.40 / 4.35 / 515 | 1.42 | 1.00 / 0.99 / 527 | 1.96 / 1.95 / 374 | 1.71 / 1.70 / 335 | 1.15 |
| zod | 0.21 / 1.94 / 598 | 0.36 / 3.79 / 508 | 0.21 / 2.85 / 391 | 1.71 | 0.56 / 0.55 / 393 | 1.74 / 1.74 / 389 | 1.05 / 1.04 / 212 | 1.66 |
| xstate | 0.14 / 1.07 / 383 | 0.19 / 1.31 / 245 | 0.15 / 1.51 / 241 | 1.27 | 0.36 / 0.34 / 194 | 1.02 / 1.00 / 175 | 0.71 / 0.71 / 111 | 1.44 |
| drizzle | 0.30 / 2.94 / 703 | 0.46 / 5.50 / 613 | 0.26 / 4.21 / 444 | 1.77 | 0.74 / 0.73 / 398 | 1.46 / 1.45 / 376 | 1.61 / 1.60 / 274 | 0.91 |
| vue | 0.17 / 1.57 / 458 | 0.26 / 3.12 / 352 | 0.24 / 2.63 / 312 | 1.08 | 0.57 / 0.56 / 306 | 1.69 / 1.68 / 280 | 1.37 / 1.35 / 179 | 1.23 |
| effect | 0.24 / 2.53 / 616 | 0.49 / 8.01 / 503 | 0.53 / 6.41 / 520 | 0.92 | 1.12 / 1.11 / 518 | 3.13 / 3.12 / 375 | 3.28 / 3.27 / 329 | 0.95 |
| vscode | 2.44 / 25.09 / 1590 | 2.66 / 51.52 / 2128 | 2.00 / 42.95 / 2246 | 1.33 | 10.61 / 10.53 / 1369 | 19.27 / 19.21 / 1412 | 14.11 / 14.08 / 1654 | 1.37 |
| excalidraw | 0.25 / 2.47 / 558 | 0.36 / 4.75 / 413 | 0.22 / 2.90 / 380 | 1.64 | 0.72 / 0.71 / 365 | 1.56 / 1.55 / 357 | 1.69 / 1.68 / 254 | 0.92 |
| tanstack-router | 0.18 / 1.36 / 439 | 0.44 / 3.97 / 477 | 0.23 / 2.62 / 377 | 1.91 | 0.54 / 0.53 / 305 | 1.59 / 1.57 / 325 | 1.10 / 1.09 / 189 | 1.45 |
- **The claim holds at the defaults, partly.** The median speedup is 1.5x (0.92 on effect, 1.01 on type-fest, 1.33
  on vscode, up to 1.9 on tanstack and nest). Single-threaded it is 0.9-1.9x, with a median of 1.2.
- **The canary beats tttt's wall at the defaults on 4 / 14**: vscode 2.00 against 2.44, drizzle 0.26 against 0.30,
  excalidraw 0.22 against 0.25 and nest 0.06 against 0.09, with zod tied. tttt still uses less CPU everywhere (vscode
  25 against 43 s) and is 1.1-2.9x faster single-threaded (vscode 10.6 against 14.1 s). The canary's peak is below
  tttt's on 12 / 14 at the defaults; only type-fest and vscode are not.
- **bun's output isn't tsc's.** It printed 21 errors on drizzle against tsc's 41 (1.4.3: 18), 0 on vscode against 10,
  and 3 on effect against 5. bun targets 7.0.2. The difference has not been investigated.

## The 12-core box with PGO + LTO (2026-10-19, run by an agent on the human's laptop)
AMD Ryzen AI 9 HX 370 (12 cores / 24 threads), 62 GB, GCC 16.2, idle (load ~1). `scripts/bench.sh --runs 3
--checkers 24 --bun bun-1.4.3 --tscrs tsc-rs-0.7.0` with pgo/tttt; best wall s / CPU s (user + sys, median run) /
peak MB. tttt's default is now K12 (`default_checker_cap()`), K24 the old default; tsc-rs at its defaults is 4
checkers:
| corpus | tttt K12 | tttt K24 | tttt K1 | bun | bun single | tsc-rs | tsc-rs --fast | tsc-rs single | tsgo | tsgo single |
|---|---|---|---|---|---|---|---|---|---|---|
| TypeScript6 | 0.20 / 1.02 / 419 | 0.22 / 1.45 / 518 | 0.45 / 0.45 / 225 | 0.37 / 2.15 / 350 | 1.06 / 1.08 / 213 | 0.45 / 1.22 / 334 | 0.46 / 1.24 / 328 | 0.74 / 0.74 / 280 | 0.58 / 2.77 / 498 | 1.21 / 1.84 / 426 |
| type-fest | 0.23 / 1.41 / 697 | 0.24 / 1.94 / 924 | 0.67 / 0.69 / 429 | 1.12 / 5.99 / 757 | 3.43 / 4.74 / 1011 | 2.49 / 5.35 / 3138 | 1.56 / 5.63 / 3285 | 3.18 / 3.52 / 2302 | 3.93 / 19.33 / 5529 | 5.91 / 13.85 / 4399 |
| valibot | 0.14 / 1.28 / 368 | 0.14 / 1.88 / 465 | 0.40 / 0.39 / 254 | 0.22 / 2.90 / 280 | 1.56 / 1.71 / 277 | 0.39 / 1.48 / 487 | 0.21 / 2.14 / 646 | 1.07 / 1.06 / 402 | 0.67 / 4.44 / 822 | 1.60 / 3.16 / 633 |
| bulletproof-react | 0.13 / 0.43 / 272 | 0.11 / 1.11 / 493 | 0.19 / 0.19 / 142 | 0.28 / 1.45 / 376 | 0.42 / 0.50 / 179 | 0.21 / 0.66 / 311 | 0.24 / 0.67 / 304 | 0.37 / 0.38 / 235 | 0.40 / 2.42 / 448 | 0.59 / 1.30 / 288 |
| nestjs-boilerplate | 0.09 / 0.52 / 275 | 0.10 / 0.75 / 364 | 0.20 / 0.19 / 132 | 0.10 / 0.62 / 233 | 0.27 / 0.30 / 154 | 0.10 / 0.46 / 225 | 0.10 / 0.46 / 238 | 0.24 / 0.24 / 188 | 0.13 / 1.30 / 260 | 0.31 / 0.65 / 209 |
| webpack | 0.27 / 2.68 / 592 | 0.29 / 4.08 / 726 | 1.00 / 1.01 / 529 | 0.59 / 5.74 / 518 | 2.56 / 2.82 / 377 | 0.82 / 2.95 / 841 | 0.43 / 4.84 / 1205 | 1.79 / 1.83 / 716 | 1.14 / 6.86 / 1177 | 2.54 / 4.19 / 961 |
| zod | 0.23 / 2.14 / 579 | 0.23 / 3.18 / 807 | 0.56 / 0.55 / 392 | 0.37 / 4.29 / 506 | 1.56 / 1.74 / 389 | 0.66 / 2.26 / 791 | 0.30 / 2.40 / 866 | 1.39 / 1.40 / 587 | 1.02 / 6.80 / 1362 | 2.08 / 4.20 / 957 |
| xstate | 0.14 / 1.05 / 406 | 0.17 / 2.24 / 598 | 0.30 / 0.37 / 196 | 0.19 / 1.45 / 245 | 0.66 / 1.07 / 175 | 0.29 / 1.15 / 322 | 0.24 / 0.98 / 322 | 0.55 / 0.59 / 261 | 0.41 / 2.76 / 468 | 0.81 / 1.57 / 362 |
| drizzle | 0.34 / 3.31 / 691 | 0.37 / 6.57 / 986 | 0.75 / 0.75 / 398 | 0.49 / 6.43 / 600 | 2.07 / 2.07 / 377 | 0.66 / 2.78 / 1002 | 0.54 / 6.61 / 1681 | 1.30 / 1.59 / 595 | 1.44 / 9.76 / 1612 | 2.05 / 3.60 / 925 |
| vue | 0.16 / 1.49 / 461 | 0.18 / 2.77 / 533 | 0.59 / 0.59 / 305 | 0.26 / 2.84 / 355 | 1.11 / 1.15 / 288 | 0.47 / 1.67 / 497 | 0.34 / 3.11 / 675 | 1.26 / 1.53 / 417 | 0.87 / 5.09 / 738 | 1.80 / 2.83 / 665 |
| effect | 0.30 / 3.04 / 639 | 0.29 / 4.58 / 760 | 1.13 / 1.12 / 516 | 0.54 / 9.87 / 503 | 3.24 / 4.03 / 373 | 1.22 / 5.09 / 1261 | 0.82 / 8.68 / 1578 | 3.32 / 4.39 / 1049 | 1.43 / 10.60 / 1510 | 3.15 / 5.15 / 1297 |
| vscode | 2.59 / 27.34 / 1581 | 2.42 / 40.52 / 1909 | 10.41 / 10.37 / 1369 | 2.77 / 54.39 / 2138 | 18.30 / 19.73 / 1419 | 5.01 / 22.51 / 5494 | 3.40 / 35.75 / 6378 | 13.99 / 13.95 / 4925 | 7.88 / 60.84 / 7940 | 19.41 / 32.76 / 6533 |
| excalidraw | 0.26 / 2.49 / 557 | 0.27 / 3.81 / 708 | 0.72 / 0.73 / 365 | 0.38 / 5.55 / 419 | 1.90 / 1.91 / 358 | 0.79 / 3.40 / 612 | 0.78 / 10.80 / 946 | 1.92 / 1.90 / 474 | 1.43 / 11.66 / 1799 | 2.13 / 4.33 / 917 |
| tanstack-router | 0.17 / 1.39 / 428 | 0.19 / 2.44 / 603 | 0.53 / 0.52 / 308 | 0.44 / 4.06 / 490 | 1.15 / 1.80 / 324 | 0.50 / 1.77 / 538 | 0.44 / 2.15 / 531 | 1.44 / 1.44 / 420 | 1.18 / 6.74 / 980 | 2.11 / 3.76 / 692 |
| `-b` TypeScript6/src | 0.51 / 3.90 / 634 | - | - | - | - | - | - | - | 1.31 / 6.96 / 558 | 2.54 / 4.27 / 524 |
- **tttt has the best wall time on all 14 corpora at the defaults.** The closest is vscode at 2.59 s against bun's 2.77
  (the earlier release build tied there at 2.69-2.81). Everywhere else the lead over bun is 1.1x (nest) to 4.9x
  (type-fest), and over tsc-rs `--fast` 1.1x to 6.8x. Single-threaded, vscode takes 10.4 s against tsc-rs's 14.0 and
  bun's 18.3, and tttt is fastest everywhere.
- **CPU at the defaults: tttt uses 0.30-0.84x bun's** (vscode 27.3 against 54.4, drizzle 3.31 against 6.43; the old
  K24 default spent 6.4 s there). tsc-rs at 4 checkers uses less CPU on 3 / 14: nest 0.46 against 0.52, drizzle 2.78
  against 3.31, vscode 22.5 against 27.3. tttt's K12 still costs 2.3-4.4x its own K1 CPU (vscode 2.6x, drizzle 4.4x).
- **K12 rather than K24 (the physical-core policy) is confirmed.** K24 is 7% faster on vscode (2.42 against 2.59) and
  15% on bulletproof (0.11 against 0.13). On the other corpora it is the same or slower, while CPU rises 40-160% and
  peak memory 15-80%. Keep the cap.
- **Memory is still the weak axis.** Bun's peak is below tttt's at the defaults on 10 / 14 (tttt is lower on
  type-fest, bulletproof, vscode 1581 against 2138, and tanstack-router), the same as before PGO. xstate is 406
  against 245, excalidraw 557 against 419, effect 639 against 503. Single-threaded, tttt's peak is at or below bun's
  on 6 / 14. tsc-rs's peak at its defaults is 0.8-4.5x tttt's (below it on TypeScript6 and nest; type-fest 3138,
  vscode 5494).

## supabase studio and bun on the 12-core box (2026-10-19, a conversation with the human)
supabase `apps/studio` (b3757692, 12.7k files / 2.0M lines with dependencies, its own tsconfig; `pnpm install
--ignore-scripts`; not in the source sweep), median of 5, wall / CPU / peak MB. Its tsconfig is incremental: tsgo
runs with `--incremental false` (a leftover `.tsbuildinfo` made it a fake 1.35 s):
| | defaults | single-threaded |
|---|---|---|
| tttt | 2.27 s / 22.6 / 1671 | 5.07 s / 5.0 / 1043 |
| bun 1.4.3 | **2.02 s** / 28.9 / 1357 | 9.76 s / 9.8 / 1045 |
| tsgo | 5.39 s / 35.0 / 4317 | 9.60 s / 17.6 / 2869 |
**tttt loses to bun at the defaults here**: a recycle thrasher (each of 12 checkers recycles 8-14 times, rebuilding
React and friends): K12 `--checker-memory off` 1.30 s / 12.8 / 3154, `--checker-memory 1024` 1.65 s / 16.4 / 1980,
K4 off 1.83 s / 9.8 / 2331. perf_leads.md's snapshot recycle is aimed at exactly this. Output: tttt had tsgo's 10
plus 7 spurious TS2322 (4 fixed with ttttBindingDefaultGenericOptional, 3 in triage.md); bun reports 1 error (none
of the 9 TS7031). Bun on vscode the same day: 2.48-2.93 s over one sitting (same 1.4.3 binary that measured 2.92 /
3.01 on 2026-10-16 / 18: machine state, not bun; `npm view @oven/bun-linux-x64 dist-tags`: latest 1.4.3, the
1.4.4 canary no faster), tttt K12 2.69-2.81, K24 2.47-2.67: **a tie on vscode at the defaults**, tttt at ~57% of
bun's CPU.

## Many-core accounting on the 12-core box (2026-10-19, a conversation with the human; no code change)
vscode `-p`, release build, this laptop (12 cores / 24 threads, idle): wall / check phase / **sum of per-file check
times** (`TTTT_FILE_TIMES=1`) / recycles / peak:
| configuration | wall | check | per-file sum | recycles | peak |
|---|---|---|---|---|---|
| K1, `--threads 1` | 12.1 s | 10.5 s | 8.8 s | 8 | 1.4 GB |
| K1, `--threads 1`, **11 other cores spinning** (pure ALU loops, package at 2.3 GHz) | 19.1 s | 17.2 s | 14.2 s | 8 | |
| K12 defaults (24 threads) | 2.68 s | 2.06 s | 20.8 s | 171 | 1.46 GB |
| K12 `--checker-memory off` | 2.63 s | 1.90 s | 19.9 s | 0 | 5.0 GB |
| K12 `--threads 12 --checker-memory off` | 2.37 s | 1.75 s | 17.1 s | 0 | 5.0 GB |
| K6 `--threads 12` | 3.77 s | 3.19 s | 16.0 s | 150 | 1.37 GB |
| K4 `--threads 12` | 4.91 s | 4.31 s | 14.6 s | 119 | 1.27 GB |
- **All-core clocks alone slow a checker 1.62x** (the spinning-cores row). 8.8 x 1.62 x 1.19 (the K12
  duplication measured on the 4-vCPU box, 2026-10-15) = 17.0 s against the 17.1 measured without recycling: duplication ~1.2x, recycling 5-8% of check
  time (for 3.5 GB), the rest is clocks. The checker-side ceiling on vscode here: zero duplication and recycling
  would make the check ~14.2 / 12 = 1.2 s against 2.06, wall ~1.8 s against 2.68 (**1.5x**); the front end (libs
  0.04 + load/parse 0.33 + resolve 0.25 s at the defaults) is at its serial floor (loader ~0.3, declare/link 0.15).
  drizzle: per-file sum K1 0.69 s, K12 2.8-3.2 s (duplication 1.4x serialized), wall 0.39 -> floor ~0.15.
- The census of what checkers re-derive measured at `--threads 1` (serialized checkers), which hides clocks and memory contention: price
  many-core work by per-file sums under real parallel runs, with the spinning-cores row as the floor.
- Minor page faults: K1 215k, K12 401k (recycling) / 374k (off); kernel time 2.4 s of 24 s CPU at K12.
- Hardware level (drizzle K1, cachegrind): 6.4 G instructions, LL misses 19.3 M (3 per 1000 instructions, ~1.5 s of
  DRAM latency hidden into 0.85 s), I1 miss rate 2.1% (134 M), D1 2.5%. IPC from callgrind counts / cycles ~1.5 at
  boost. **PGO + LTO** (`-Db_lto=true -Db_pgo=generate`, trained on drizzle, zod, effect, excalidraw, webpack at K1
  and the defaults, then `use`; `-Wno-missing-profile` and `-Wno-error` at link for a GCC stringop-overflow false
  positive at `ty.h:96`): **-8-9% wall everywhere**, untrained corpora included (vscode K1 11.83 -> 10.86 s, defaults
  2.68 -> 2.48; tsc 0.51 -> 0.47; drizzle 0.85 -> 0.78). LTO alone 1-2%. Shipped 2026-10-19 as `scripts/pgobuild.sh`
  (the section above).

## The 12-core machine: tttt, tsgo, bun, tsc-rs (2026-10-18, 2d1ea533)
The human's laptop, release build (before PGO), tttt at 24 threads / K24 (the old default), tsgo 4 checkers, bun every
core, tsc-rs 0.7.0 4 checkers (`--fast`: one per core). The per-corpus table is superseded by "The 12-core box with
PGO + LTO" above (git history has it). Geometric means against tsgo at its defaults (wall / CPU / RSS): **tttt 0.26 /
0.41 / 0.56**, bun 0.41 / 0.60 / 0.41, tsc-rs 0.59 / 0.31 / 0.64, tsc-rs `--fast` 0.47 / 0.44 / 0.77; single-threaded
tttt 0.66 / 0.10 / 0.30, tsc-rs 1.38 / 0.21 / 0.51, bun 1.64 / 0.25 / 0.31, tsgo 1.88 / 0.59 / 0.74 (tsgo's CPU above
its wall single-threaded: GC threads). Output: tsc-rs byte-identical to tsgo on 13 / 14 (effect: its built-in Effect
plugin diagnostics); tttt on 11 / 14 (vue 2 spurious TS7006; webpack and drizzle same (file, line, code), other
elaboration text); bun differs on the five corpora with errors and reports none on vscode.

## Checkers per physical core (2026-10-18, 99676b23 + the policy)
**Oversubscribed** (`--threads 24 --checkers K` on these 4 vCPUs): wall means nothing, CPU (user + sys) and peak are
the many-core costs without the 12-core box's SMT and clock effects (which inflate its CPU further: drizzle K24 6.37
s there vs 3.81 here). Median of 3, CPU s / peak MB (K1 at 24 threads parses in parallel and has 23 materialize-ahead
helpers, so it isn't `--singleThreaded`):
| project | K1 | K4 | K8 | K12 | K24 | K24 / K12 cpu, MB |
|---|---|---|---|---|---|---|
| TypeScript6 | 0.64 / 363 | 0.93 / 333 | 0.95 / 385 | 1.01 / 434 | 1.07 / 440 | 1.06, 1.01 |
| type-fest | 0.90 / 527 | 1.41 / 462 | 1.49 / 672 | 1.52 / 721 | 1.59 / 918 | 1.05, 1.27 |
| valibot | 0.52 / 396 | 0.93 / 246 | 1.01 / 291 | 1.10 / 330 | 1.19 / 409 | 1.08, 1.24 |
| bulletproof-react | 0.24 / 213 | 0.40 / 296 | 0.48 / 351 | 0.55 / 370 | 0.60 / 437 | 1.09, 1.18 |
| nestjs-boilerplate | 0.26 / 220 | 0.30 / 257 | 0.32 / 274 | 0.34 / 288 | 0.36 / 314 | 1.06, 1.09 |
| webpack | 1.44 / 621 | 2.21 / 514 | 2.48 / 552 | 2.67 / 609 | 2.86 / 712 | 1.07, 1.17 |
| zod | 0.75 / 489 | 1.46 / 407 | 1.74 / 468 | 1.95 / 542 | 2.07 / 670 | 1.06, 1.24 |
| xstate | 0.39 / 323 | 0.74 / 310 | 0.91 / 314 | 1.05 / 393 | 1.41 / 540 | 1.34, 1.37 |
| drizzle | 1.00 / 544 | 2.26 / 507 | 2.89 / 592 | 3.05 / 689 | 3.81 / 894 | 1.25, 1.30 |
| vue | 0.82 / 465 | 1.32 / 322 | 1.43 / 361 | 1.53 / 472 | 1.77 / 505 | 1.16, 1.07 |
| effect | 1.60 / 636 | 2.35 / 490 | 2.47 / 562 | 2.64 / 618 | 2.86 / 775 | 1.08, 1.25 |
| excalidraw | 1.03 / 519 | 2.06 / 428 | 2.21 / 488 | 2.33 / 545 | 2.54 / 645 | 1.09, 1.18 |
| tanstack-router | 0.72 / 424 | 1.23 / 341 | 1.36 / 362 | 1.44 / 394 | 1.53 / 524 | 1.06, 1.33 |
| vscode | 15.52 / 1596 | 23.43 / 1291 | 25.55 / 1487 | 26.89 / 1538 | 28.64 / 1832 | 1.07, 1.19 |
| dokploy/apps/dokploy | 3.57 / 891 | 11.39 / 1348 | 21.46 / 1919 | 28.06 / 2744 | 48.06 / 4809 | 1.71, 1.75 |
| monkeytype/frontend | 0.86 / 504 | 5.01 / 643 | 8.07 / 856 | 9.57 / 1083 | 13.89 / 1719 | 1.45, 1.59 |
- On the corpora CPU grows slowly with K (vscode +15% K4 -> K12), the thrashers almost linearly: each dokploy checker
  rebuilds a ~170 MB warm state (`--time`: every checker holds 170-226 MB at most and recycles 2-4 times at any K),
  ~1.5-2 CPU s each. That is the next section's subject; the policy only stops paying it twice per core.
- Budgets at K12 (`--checker-memory M`, so no 16 MB start): 1152 (96 each) vs the default's growing start: monkeytype
  CPU 4.93 vs 9.57 (peak 1219 vs 1083), drizzle 2.61 vs 3.05 (775 vs 689), vscode 24.4 vs 26.9 (**2707 vs 1538**);
  dokploy at 200 each 21.2 vs 28.1 CPU at the same peak. The budget frontier again ("The several-checker budget"): not changed.
**Real threads** (`--threads 4`, this box: 2 cores x 2 SMT), wall s / CPU s / peak MB:
| project | K2 | K3 | K4 | K2 / K4 wall |
|---|---|---|---|---|
| TypeScript6 | 0.30 / 0.75 / 226 | 0.30 / 0.86 / 233 | 0.31 / 0.90 / 230 | 0.97 |
| type-fest | 0.49 / 0.96 / 335 | 0.46 / 1.25 / 300 | 0.49 / 1.39 / 422 | 1.00 |
| valibot | 0.31 / 0.66 / 140 | 0.28 / 0.84 / 150 | 0.26 / 0.92 / 164 | 1.19 |
| webpack | 0.69 / 1.59 / 378 | 0.66 / 1.98 / 383 | 0.60 / 2.21 / 420 | 1.15 |
| zod | 0.46 / 0.98 / 238 | 0.41 / 1.25 / 258 | 0.38 / 1.44 / 282 | 1.21 |
| drizzle | 0.75 / 1.55 / 272 | 0.65 / 1.93 / 284 | 0.60 / 2.27 / 370 | 1.25 |
| effect | 0.75 / 1.70 / 338 | 0.65 / 2.07 / 363 | 0.61 / 2.31 / 333 | 1.23 |
| excalidraw | 0.66 / 1.48 / 255 | 0.59 / 1.82 / 286 | 0.54 / 2.03 / 262 | 1.22 |
| tanstack-router | 0.39 / 0.82 / 196 | 0.36 / 1.03 / 220 | 0.31 / 1.16 / 268 | 1.26 |
| vscode | 7.70 / 16.58 / 1127 | 6.86 / 20.53 / 1175 | 6.11 / 23.05 / 1168 | 1.26 |
| dokploy/apps/dokploy | 3.62 / 7.54 / 929 | 2.62 / 7.32 / 1160 | 3.00 / 11.19 / 1214 | 1.21 |
| monkeytype/frontend | 1.16 / 2.36 / 443 | 1.31 / 3.86 / 433 | 1.31 / 4.92 / 521 | 0.89 |
  (react, nest, xstate, vue within 1.14.) The SMT sibling buys 15-26% wall on 2 cores: vscode K2 would tie bun
  (7.70 vs 7.73, "Memory against bun on the 4-vCPU box" below).
- **Policy shipped** (`default_checker_cap()`, main.c): without `--checkers`, the checkers are at most the physical
  cores (distinct package/core ids among the affinity's CPUs in sysfs), but every thread up to 4. Here: 4 as before
  (`--threads 24` now 4 too: oversubscribing takes `--checkers`). 12 cores / 24 threads: 12, where K12 -> K24 bought
  0-11% wall (vscode 2.81 vs 2.50 s, bun 2.92, 2026-10-16) for the CPU and peak above. Confirmed there on 2026-10-19 ("The 12-core box with PGO + LTO": K24 costs +40-160% CPU for 0-15% wall).

## What the checkers re-derive (2026-10-18, 6ae5f65a)
**Work vs inflation** (CPU s, best of 2): `--threads 1` runs every checker on one thread, so its CPU is the work
done; `--threads 4` K4 is this box's default (2 cores x 2 SMT). `off` is `--checker-memory off` (no recycling):
| project | K1 | K4 | K4 off | K12 | K12 off | threads 4: K4 | K4 off |
|---|---|---|---|---|---|---|---|
| drizzle | 0.93 | 1.46 | 1.02 | 1.59 | 1.30 | 2.31 | 1.70 |
| zod | 0.68 | 0.92 | 0.72 | 1.07 | 0.89 | 1.43 | 1.15 |
| effect | 1.40 | 1.51 | 1.39 | 1.52 | 1.44 | 2.30 | 2.22 |
| type-fest | 0.85 | 0.93 | 0.86 | 0.97 | 0.87 | 1.39 | 1.27 |
| excalidraw | 0.91 | 1.30 | 1.04 | 1.38 | 1.21 | 2.03 | 1.63 |
| monkeytype/frontend | 0.77 | 2.89 | 1.26 | 4.22 | 2.25 | 4.89 | 2.02 |
| dokploy/apps/dokploy | 3.20 (off 2.08) | 5.89 | 3.30 | 12.06 | 6.30 | 11.19 | 5.22 |
- At this box's default (real K4) the extra CPU over serial K1 is **mostly SMT/contention inflation** (the same work
  on 4 threads: drizzle 0.85 of 1.38 s, effect 0.79 of 0.90, monkeytype 2.0 of 4.1), then **recycling** (K4 - K4 off:
  drizzle 0.44, monkeytype 1.63, dokploy 2.59), and little **duplication** between checkers (K4 off - K1: 0.0-0.13 on
  the corpora, monkeytype 0.49). At K12 duplication catches up with recycling (drizzle 0.37 vs 0.29, dokploy K12 off
  3x K1 off).
- **The declared world is not what is re-derived.** A census build timed the outermost resolutions of declaration
  files' declarations (`ty_declared_type_of`, `ty_type_of_symbol`, `resolve_declared_members`, `signature_resolve`,
  `signature_return_type` and `prop_type` of uninstantiated declarations whose first declaration is in a `.d.ts`,
  lib included; reverted, method only) at `--threads 1`: K1 -> K12 typecheck ms, those frames' ms, their share of
  the extra: drizzle 792 -> 1452, 7 -> 23 (2.4%); zod 616 -> 1019, 6 -> 28 (5.4%); monkeytype 619 -> 4142, 31 ->
  219 (5.3%); dokploy 2691 -> 11773, 148 -> 680 (5.9%); vscode 11851 -> 14104, 145 -> 487 (15%, lib.dom); effect ~14%.
  TY_DECLARED is 1-5% of the types made; what multiplies is instantiations (dokploy K1 -> K12: 996k -> 5.3M types,
  anonymous 429k -> 2.3M, references 145k -> 860k, unions 133k -> 684k, conditionals 73k -> 439k). Callgrind of
  monkeytype at `--threads 1`, K1 5.9 G vs K12 39.2 G instructions: every hot function x6.6 (union_with_origin,
  ty_instantiate_mapped, intern_slot, recursive_related...), the same profile scaled. The rebuilt graph is the
  project's own `.ts` code instantiating its libraries (dokploy's trpc router of zod schemas, drizzle's own sources),
  which no up-front layer can hold. The shared declared layer was closed on this (vision.md "Decisions").

## Shared graph gate G2: `--shared-graph` on threads (2026-10-21; history: the lock, waits and redo it measures are deleted, see "Shared graph end state"; b97b6f4ac, this box: 16 vCPUs shared with other agents)
Release build (not PGO), `-q --keep-going --noEmit`, best of 3 by wall; load 4.8-6.8 (other agents). Columns: wall s,
CPU s (user+sys), peak RSS MB. K1 = `--threads 1 --checkers 1`; def = no flags; defK / shK = `--threads K --checkers K`
without / with `--shared-graph` (which turns recycling off).
| corpus | K1 | def | def4 | sh4 | def12 | sh12 |
|---|---|---|---|---|---|---|
| TypeScript6 | 0.74 .73 221 | 0.22 .84 354 | 0.22 .67 253 | 0.23 .72 264 | 0.21 .99 335 | 0.24 .94 320 |
| type-fest | 0.90 .90 427 | 0.44 1.56 590 | 0.38 1.02 414 | 0.44 1.19 638 | 0.26 1.41 566 | 0.39 1.68 693 |
| valibot | 0.48 .47 251 | 0.14 .92 296 | 0.19 .68 152 | 0.20 .70 298 | 0.12 1.04 296 | 0.13 .96 367 |
| bulletproof-react | 0.18 .18 141 | 0.13 .38 226 | 0.09 .30 197 | 0.08 .24 178 | 0.15 .56 320 | 0.09 .39 250 |
| nest | 0.19 .18 128 | 0.12 .37 212 | 0.08 .25 162 | 0.08 .25 160 | 0.08 .40 232 | 0.08 .37 237 |
| webpack | 1.35 1.34 542 | 0.40 2.73 546 | 0.48 1.82 437 | 0.48 1.77 613 | 0.32 2.85 560 | 0.30 2.43 665 |
| zod | 0.72 .72 377 | 0.21 1.46 449 | 0.27 1.03 268 | 0.27 .96 494 | 0.20 1.90 440 | 0.19 1.38 564 |
| xstate | 0.34 .33 188 | 0.12 .75 310 | 0.15 .54 199 | 0.14 .43 234 | 0.12 1.06 335 | 0.11 .66 285 |
| drizzle | 0.96 .95 394 | 0.31 2.13 494 | 0.44 1.65 357 | 0.48 1.21 556 | 0.28 2.73 575 | 0.43 1.60 631 |
| vue | 0.77 .77 301 | 0.16 1.18 370 | 0.24 .91 216 | 0.24 .91 349 | 0.15 1.44 383 | 0.16 1.36 409 |
| effect | 1.43 1.43 573 | 0.26 2.05 569 | 0.44 1.71 391 | 0.49 1.83 764 | 0.24 2.38 580 | 0.28 2.56 826 |
| excalidraw | 0.93 .93 365 | 0.24 1.76 425 | 0.38 1.44 270 | 0.30 1.12 417 | 0.22 2.10 445 | 0.18 1.62 492 |
| tanstack-router | 0.64 .63 300 | 0.17 1.04 367 | 0.22 .84 240 | 0.23 .83 371 | 0.16 1.27 391 | 0.16 1.15 424 |
| vscode | 13.26 13.25 1407 | 2.45 18.35 1410 | 4.19 16.11 1158 | 4.08 15.52 4384 | 2.21 22.44 1469 | 2.14 21.12 4467 |
- **CPU**: the shared graph saves CPU at K12 on 10 of 14 (drizzle 1.60 against 2.73, zod 1.38 / 1.90, excalidraw
  1.62 / 2.10, xstate 0.66 / 1.06, vscode 21.1 / 22.4); more on type-fest (1.68 / 1.41) and effect (2.56 / 2.38).
- **Wall**: equal or better at K12 on 11 of 14 (excalidraw 0.18 / 0.22, webpack 0.30 / 0.32, vscode 2.14 / 2.21); worse
  on drizzle (0.43 / 0.28: the variance lock, one measurement at a time, and a third of drizzle's work is under
  measurements; 3.0 s of its K12 workers' time is waiting, 0.7 s without the lock), type-fest (0.39 / 0.26) and effect
  (0.28 / 0.24).
- **Memory**: peak RSS is higher everywhere, since nothing is recycled with the flag: vscode 4.4-4.6 GB against 1.4 at
  the defaults (K1 1.4 with recycling, 4.1 without), effect 826 / 580 MB.
- **Work** (callgrind Ir in program_typecheck and the worker threads, `--checker-memory off`, K12 / K1 with the flag):
  zod 4.808 / 4.384 G = 1.097, drizzle 5.796 / 5.276 = 1.099, excalidraw 7.418 / 6.544 = 1.134 (the G2 target, 1.15,
  holds). Before memo publication at the computation's end (7b20a147d): 1.26, 2.63, 1.17.
- **Per-file check-time sums** (TTTT_FILE_TIMES, best of 3; wall per file, waits included): drizzle K1 801 ms, sh12
  3918, def12 2289; excalidraw 805 / 1400 / 1814; zod 661 / 1575 / 1699. At 12 threads every file runs ~2x slower
  per instruction (def12 is 2.2-2.9x K1 for 1.4-1.9x the work); drizzle's sh12 is mostly waiting (2968 ms summed).
- **Determinism** (K12 T12 against K1's output): 20 runs each on drizzle, zod, effect, excalidraw, nest all K1's;
  drizzle 300 loaded runs (an effect K12 loop beside it) all K1's; vscode 5 / 5 (2.16-2.26 s, 4.56-4.58 GB, no redo),
  TypeScript6's compiler 5 / 5; all 13 other `-p` corpora at K4 and K12 twice each K1's output.

## Shared graph end state (2026-10-22, c60fa6257 on 22d3ce85a; notes/shared_graph.md)
`TTTT_SHARED_GRAPH=1`, this box (16 vCPUs shared, load 5-18). No waits, no redos (deleted). 20-run K12 tables 20/20 on
drizzle, zod, effect, excalidraw, nest; 300 loaded drizzle runs 0 differ, 100 loaded effect 1 (Schema.tst.ts(2936,39));
vscode 5/5; seeds drizzle 191/200 (the query.ts(139,3) TS2322), effect 300/300, zod, excalidraw, nest 100/100; TSan 28
runs (vscode aside) one group, fixed. Work (cgtc.sh) K12/K1 drizzle 1.305 (6.714 / 5.145 G), zod 1.285 (5.602 / 4.360),
effect 1.134 (9.324 / 8.220); against plain K1 1.36x, 1.35x. Wall best of 7, K12 T12 shared / defaults: drizzle 0.239
/ 0.309 s, zod 0.211 / 0.241, effect 0.355 / 0.336. kdiff with the flag: 8 / 3814 (plain 3). Before the K1 overhead cuts below,
the flag's K1 cost was drizzle +14.6%, zod +12.9%, nest +10.4%, effect +10.2% instructions over plain (callgrind,
typecheck only; by function in the design log that notes/shared_graph.md points to). Default path over the stage (plain release, db9ca5304 to the head
before the deletions): drizzle -1.6%, zod, nest, effect within 0.1%, tsc +0.8% (other tracks' commits included).

## Shared graph K1 overhead (2026-10-22)
Callgrind whole process, K1 T1 `--checker-memory off`, `TTTT_SHARED_GRAPH=1` over the plain path (base 3fb334159):
drizzle +11.66% -> +4.06%, zod +11.30% -> +4.25%, nest +2.19% -> +0.71%. By group (drizzle / zod, % of plain): Overlay
and word slow paths 4.09/3.97 -> 1.92/1.93 (claims as a stack), shared intern tables + shard mutexes 5.13/4.80 ->
0.02/0.03 (nothing published or probed while one checker is alone on the graph), memo taint hit test 0.75/0.66 and
first-touch stamps 0.81/1.03 unchanged. K12 work (cgtc.sh) -1 to -2%; K12 wall within noise.

## Shared graph without the variance lock, first-touch stamps on under the flag (2026-10-22)
Release build, this box (16 vCPUs shared, load 7-16 during the runs). `--shared-graph` now turns stamps on; the lock in
claim_and_measure() is gone (not just off). Base = de94e1179 (lock on, stamps off), built in the same scratch.
- **Determinism** (K12 T12 against K1's output, `scripts/sharedgraph/`): 20-run tables drizzle 20, zod 20, excalidraw
  20, nest 20, effect 19 (one run added `internal/fiber.ts(293,21) TS7006`); 300 loaded drizzle runs (`t3loop.sh`, an
  effect K12 loop beside it) all K1's (redos 0-8 a run, median 2: without the lock redos are common again); vscode 5 /
  5. Effect's TS7006 is not the lock's: effect 60 more runs give 1 diff with this build, 1 with base + stamps, 1 with
  base as it is (`test/Schema/TestUtils.ts(45,20) TS7006` there): a lost contextual parameter type, ~1.5% of loaded
  effect runs with or without the lock (the by-construction design's subject, not chased).
- **checkerdiff / threadcheck equivalent** (`TTTT_SHARED_GRAPH=1`, pretty, `--keep-going --checker-memory off`; every
  corpus K1 against K4 twice, K4 at T4 three times, K12 at T12): all 16 corpora identical.
- **Work** (`cgtc.sh`, callgrind Ir in the typecheck and worker threads): drizzle flag K1 5.319 G (base 5.284: the
  stamps, +0.66%), K12 5.940 (base 5.972) = **1.117x** K1; zod K1 4.428 (base 4.390, +0.86%), K12 4.859 (base 4.807)
  = **1.097x**. Default path (no flag, callgrind whole process): nest, drizzle, zod identical to base to the 1e-3 G.
- **Wall** (best / median of 7, interleaved, load ~8): drizzle defaults 0.292 / 0.301 s, flag 0.253 / 0.269, base flag
  (lock) 0.417 / 0.439: the lock's +54% is gone and the flag now beats the defaults. Effect defaults 0.267 / 0.271,
  flag 0.293 / 0.297, base flag 0.281 / 0.286: effect is still +10% over the defaults (it was +17% at G2).

## Oracle totals (2026-10-23, the 7.1.0-beta re-pin)
- Merge-time caches at the beta from the main tree (1976b1579 + this): dtsdiff `--tsc` 1574 projects in 182 s (1503 emit),
  then `dtsdiff.py`: **1486 / 1503 projects fully match, 129309 / 129362 files** (was 1328 / 1350 at the nightly's cache, which
  had fewer emitting projects), 0 one-sided writes, 1 "no output" (b4run/packages/postgres-storage: triage.md); flagflip
  `--tsc --force` 168 cells in 882 s, then `flagflip.py`: **missing 16 / spurious 2 / other code 2, all vue pluginScoped.ts**
  (unchanged: tsgo's TS5115 runaway artifact).
The oracle moved from the 2026-10-08 nightly (fed0bf24) to **TypeScript 7.1.0-beta** (`v7.1-beta` = 3e923636), every
cache regenerated with it (wt-repin) but dtsdiff's and flagflip's (main tree only). Before = d7bc837b0's totals below.
- Suite **14137 / 14192** (dts 1897 / 1926; 14138 / 14193 rebased onto main's NoInfer repro), was 13999 / 14035 (dts
  1888 / 1910): the beta added 157 tests; 4 lost
  (objectLiteralNormalization, jsdocAccessibilityTagsDeclarations, jsdocReadonlyDeclarations, mappedTypeConstraints2)
  all fixed; 19 new configurations fail, written up (12 content mapper `outputExtensions`, the beta's new feature; 7
  others in bugs/type_core.md, harness.md, js.md, binder_modules.md). `--no-defer-bodies` the same, `--checker-memory
  0` 0 of 17845 differ.
- tsgo's own outputs barely moved: every corpus's diagnostics and text identical (only the pin), DT 1 package changed
  (types/sphere-engine-browser, in plain, nuia and eopt), `$ExpectType` 1 package (json-merge-patch), source sweep 11
  of 5464 verdicts, `-b` sweep babel's crash gone.
- tttt against them: every corpus 0/0 but vue 0/2 (as before), DT 8595 / 8595, `$ExpectType` 68640 / 68640, source
  sweep `--sample 4` 1322 / 1324 (as before), `-b` sweep 418 / 426 (was 411 / 416; bugs/sourcesweep_build.md),
  scenarios 221 / 221 (13 skipped), clidiff corpora 16 / 17 (vue), oracles.py --quick nothing STALE. checkerdiff K1
  against `-m 0` and K8, threadcheck: identical on drizzle, zod, effect, excalidraw.
- Instructions (`scripts/instrcount.sh`, callgrind K1, fast/tttt): drizzle 4.453 G -0.14%, zod 3.587 G +0.08%,
  vscode 66.463 G +0.19%: flat (the deleted undefinedProperties recheck never ran on these; the new esnext.iterator lib
  loads only with esnext).

## Instructions and the front end at d7bc837b0 (2026-10-23, the fourth coordinator's end)
That run's oracle totals (suite 13999 / 14035 at the nightly, the rest as in the re-pin's "before") are in git
(`git log -S'Oracle totals (2026-10-23, d7bc837b0)' -- agents/measurements.md`); the evening's section supersedes them.

**Instructions at d7bc837b0** (`scripts/instrcount.sh`, callgrind K1, fast/tttt, against 0a1f67836's save): flat
through the kernel round (vscode 66.374 G, -0.02%; the rest within +-0.25%, react +0.23%, type-fest -0.21%): its win
is kernel work, which callgrind doesn't count. wt-ktime's own gates: drizzle/zod/vue at `--checkers 4
--checker-memory 0` identical to K1, `--time` sys (median of 3, load ~23) typecheck vscode 991 -> 683 ms, drizzle
104 -> 88; peak RSS at the defaults on this box after all four commits: drizzle 850 -> ~866 MB, vscode 5015 -> ~5160,
nest 167 -> 172, zod 630 -> 646 (the laptop measured more: +4..15%, "at 3a77c827").

**Round four before it** (the same tool, `test/instrcount.txt` 0a400eb6f -> acb8dfe7f): flat. vscode 66.346 -> 66.388 G (+0.06%), type-fest 8.705 -> 8.663 (-0.49%), vue 5.015 ->
4.995 (-0.40%), valibot +0.17%, effect, zod, drizzle, xstate, nest -0.13..-0.18%, the rest within 0.07%. The round's
checker fixes (cor6, cor7) and the front end's (fe6) moved nothing at K1; better-drizzle at K1 2.08M -> 364k types
(tsgo 492k) is outside the corpora.
Front end on this box (16 vCPUs, loaded; the laptop to confirm): the checker cap (10398e204; since 8d4518ecc only for a
largest file >= 1 MB) took TypeScript6 8 -> 4 checkers, -5% instructions at T1, its longest checker +3%; AST slabs' first 4 MB per thread on 4 KB pages (50a59a729)
nest peak 190 -> 169 MB, react 285 -> 268 (huge pages forced; with malloc_trim at the defaults 210 -> 169 and 293 -> 255), the libs phase's sys down a third here, up on the
merger's run (triage.md); malloc_trim(0) before checking (03bca3127) -4 / -3 MB; the loader not blocking on libs
(b6f8a4650) nest libs + load+parse files 41.3 -> 36.8 ms (libs phase 15.7 -> 0.2), react 143 -> 135 ms wall.

## Oracle totals with and without the shared graph (2026-10-21/22, plain at 9032ae214 and 8d693aeb5)
Suite 13962 / 13996 (dts 1887 / 1909; `scripts/passcount.py`), `--no-defer-bodies` and `--checker-memory 0` unchanged,
every corpus 0/0 but vue 0/2, DT 8595 / 8595, `$ExpectType` 68640 / 68640, flagflip 18 / 3 / 2, source sweep `--sample
4` 1272 / 1274 (1271 at 9032ae214: CONDP's angular-cli regression, fixed at the cause by cff135035; full: 5235 / 5242
on 2026-10-20), `-b` sweep 411 / 416, clidiff 3824 / 3831, dtsdiff 1327 / 1350 projects (+7 as tsgo
`--singleThreaded`; the history dependence fixed: vscode 9929 / 9929, vitest/ui a tsgo check-order artifact), kdiff 3
and recycle 3 (rxjs/packages/rxjs, sequelize, sequelize/packages/core: tsgo-shared), perfsweep worst types ratio 1.40
(lit tsserver-plugin). **With `TTTT_SHARED_GRAPH=1`** (the double run at 9032ae214): the same everywhere but
kdiff 16 / 3814 at that commit (flag-only: mikro-orm x3 (C), trpc x3 and dokploy (V), folo renderer, trpc/examples/nuxt
(W), renovate, react-navigation x2, folo ssr, redux-toolkit), 8 / 3814 at the end state;
the release criterion wants them equal. **The final double run at 8d693aeb5** (the end state, code-identical to 5f2cd6996):
plain exactly as above; the flagged run identical on every line but two: the effect corpus 21 / 0 spurious once (the
Schema.tst.ts(2936,39) cascade, ~1% of loaded runs, residue C+V) and kdiff 8 / 3814 (plain's 3 plus folo/apps/desktop/
layer/renderer, mikro-orm x3, trpc/examples/nuxt). Beside it: TSan drizzle+effect T12 clean 3 / 3 runs (pair union
empty), fingerprint 5 runs each zod 0, drizzle 1 (the union-order tie-break), effect 0, output K1's in all 15. Instructions: `test/instrcount.txt`. `scripts/oracles.py` reprints all
of these (20-30 minutes when the box is loaded: a generous background timeout); compare against its output, not this
paragraph, once it's stale.

## Past conclusions
Condensed from older sections (2026-09-28 .. 2026-10-19); `git log -S'<heading>' -- agents/measurements.md`
finds the full section. The `###` headings are the names other notes cite.

### Checker density priced (2026-10-17, ad4e1701)
Census at one checker without recycling (`scripts/arenasites.sh -p P --checker-memory 100000`), arena MB / types:
valibot 137 / 224k, webpack 182 / 320k, zod 276 / 369k, excalidraw 140 / 151k, effect 359 / 533k, vscode 1482 /
2.39M; the big sites are `ty_alloc` (~108 B per type, vscode 278 MB), instantiated Props (56 B, vscode 135), Members
(`builder_finish`, 149), object literal Prop+PropDecl (152 B, vscode 184, excalidraw 28), intern keys (173), mappers
(~95; effect ~60). **Priced density items** (arena MB saved at K1, valibot / webpack / zod / excalidraw / effect /
vscode): instantiated Prop 56 -> 40 B (6.5, 6.6, 18, 3.2, 8.9, 39); instantiated Signature 160 -> ~64 B (1.7, 3.3,
8.6, 1.4, 8, 17); compact object literal PropDecl (2.9, 4.5, 1.3, 9.7, 2, 64); namespace export props without a
PropDecl (effect 25-42: members shared since, below); mappers sharing stable lists (a content-checked cache since, below); instantiated props_by_name from the
source's permutation (3.3, 5.4, 9.5, 3.5, 5.8, 30); intern keys -20% (3.6, 4.6, 5, 4, 6.5, 35); per-kind Ty layouts
-15% (3.4, 5.4, 6, 2.5, 8.6, 42). All eight: 18-24% of the arena, ~10-13% of the peak; most touch hundreds of
readers. bun's memory *falls* from one thread to two while its CPU rises 15-50% (it re-derives rather than keeps);
duplication across K4 without recycling is +27-49% of K1's arena, so a shared frozen declared layer saves less than
the budget does.

### The several-checker budget (2026-10-17; "Plan 81 stage 1" in code comments)
The several-checker budget: 16 MB each, doubling after a segment of fewer than 16 files ("16 b16", shipped as
`program_set_checker_memory_shared()`, only without `--checker-memory`). Corpora sum at 4 checkers: 96 MB each 11.3 s
/ 7184 MB, 16 b16 12.7 / 4865 (9 / 14 at or below bun's peak). The source sweep's 174 biggest: +14% wall, -22.5%
summed peak, 96% faster than bun. **No policy met the gate** (10 / 14 below bun, tail within +10%): the corpora want
low budgets, the tail wants few recycles. Other frontier points: 16 g4 b32 +8% / -19.5%, 24 b32 +10% / -18%.

### Namespace members and mapper lists (2026-10-17)
`import * as ns` shares the module namespace type's members (effect: 3068 namespace types, 42 MB) and mapper lists go
through a content-checked cache (effect copied lists 37 -> 8 MB); effect's K1 arena 359 -> 279 MB. The tail barely
moved (wall -0.4%, summed peak -0.7%). **What a recycle costs is the rebuild, not the recycle**: `checker_recycled()`
is ~1 ms; the next segment re-deriving a shared graph (react-select's tests rebuild csstype/React, ~20 MB, ~120 ms)
is the cost. Tried, not merged: the warm-up guard as the most any one file of the segment added (tail -5.8% wall,
+6% peak; drizzle and excalidraw above bun).

### Memory against bun on the 4-vCPU box (2026-10-14 .. 18)
Bun 1.4.3 checks each file "in a clean state" and bounds its checker memory (vscode at one thread flat at 1.36 GB
after load). tttt's checker grew linearly then (vscode K1 +150-170 MB/s, ~190 KB per checked file never read again;
4261 MB peak), which recycling fixed: byte-identical output at every budget, even a fresh checker per file. At 256 MB:
vscode K1 13.18 s / 4190 MB -> 13.50 / 2575 (9 recycles), T4 5.80 / 4786 -> 6.01 / 2858; type-fest K1 1.42 / 738 ->
1.65 / 380. A fresh checker's warm-up was ~16 ms on vscode; drizzle's files each pull in a big shared graph (+23% at
K1 B=128). The bench of 2026-10-18 (`scripts/bench.sh --runs 3 --bun`, best wall s / peak MB, defaults): tttt faster
than bun on all 14 corpora (vscode 6.36 / 1168 vs 7.73 / 1559, type-fest 0.52 / 419 vs 2.01 / 569, effect 0.64 / 327
vs 1.37 / 312), at or below bun's peak on 8 and within 1.16x on the rest; single-threaded at or below bun's peak on 7
(vscode 13.78 / 1361 vs 20.14 / 1409). tsgo used more memory on all 14 (vscode 8016 / 6628 MB). The full tables:
`git show af936072c:agents/plans/67_memory_against_bun.md`.

### The K1 budget (2026-10-17)
K1 stays at 256 MB: 128 saves vscode ~250 MB (1450 -> 1202) and the 15 corpora -15% peak for +2% wall, but the
sweep's 169 biggest pay +6% (folo renderer 3.02 -> 4.90 s). vscode's K1 peak is the checker's sawtooth (~1050 MB after
each recycle, ~1450 before the next). LazyType slot blocks (memo pages ~60% used, ~7 MB a checker) merged; boxing
ClassItem's FunctionImpl declined (methods are the biggest variant; exact sizes ~7 MB).

### The checkers' budget (2026-10-17)
Floor 128 -> 96 MB each at the defaults (vscode +1.7% wall for -200 MB; 80 costs the sweep +5%); since replaced by
the several-checker budget's growing start (itself gone since 2026-10-22). The tail is dokploy-like: one file grows a fresh checker by ~70 MB (a drizzle schema
behind tRPC) in ~100 ms; drizzle re-derives its graph across many files (db.ts, session.ts, select.ts: +20-27 MB each).
**Dropped**: a warm-up guard on the largest single-file growth (same frontier; type-fest's checkers grew to 767 MB); a
time-based guard would make cuts timing-dependent. `program_set_checker_memory_cap()` (1536 MB together, 64 each at
least) binds only past 16 checkers.

### A copying checker recycle (2026-10-17; declined)
Survivors from declaration-level roots at each recycle (`scripts/survivors.sh`): vscode 35% of the arena at 64 MB
(10-27 MB a recycle), dokploy 37-57%, drizzle 13-20%, type-fest 3-5%. Re-derivation per recycle is ~14-36 ms on
vscode; a precise copy of ~20 MB (~1.5M objects, intern tables rehashed) costs about as much, and at K1 the peak is
already the program's. Declined.

### Mapped source text (2026-10-17)
Source text mapped (MAP_PRIVATE) and released at recycles, ResolvedModule cache dropped: vscode after names 1029 ->
843 MB, peak K1 1584 -> 1447, wall +1.3-1.9% (re-faulting text). Mapping without releasing was *worse* than read()
(1640 MB); releasing per file cost +2.7% (TLB shootdowns), per 256 MB segment +0.9%.

### Deferred bodies' cost (2026-10-17)
Deferred bodies against eager (`--no-defer-bodies`) on vscode: K1 T4 +1.1%, defaults +2.0%, K1 T1 13.47 vs 13.08 s
(+3%). Huge pages for materialization slabs took the body drops' +4% back to that (page faults 636k -> 307k at T1);
carrying owner-made materializations across recycles kept their chunks (+150 MB peak), so cross-made ones use a lasting
small-page slab. Materializing costs ~1.69 s single-threaded on vscode against the eager parse and walk's ~1.38,
re-materializing 0.23 s (single-body re-materialization of 2026-10-18 took the defaults' 0.80 s CPU to 0.16).
Dropping materializations with the last checker incarnation took vscode K1 2490 -> 1574 MB; the upper
bound (every body skipped) was vscode -1263 MB after names.

### Compact AST (2026-10-16; no-go)
Compact AST **no-go**: vscode's AST 1367 MB holds 557 MB of zero words, but diffuse (no site past 50 MB; sparse
optional fields and empty vectors, not union padding); exact-size Expr variants reach ~165 MB at most, under the
400 MB gate. Other estimates: 12-byte spans ~60 MB, 8-byte spans ~120 MB (~570 `.buf` sites), 32-bit node references
~40 MB. A lone checker keeps 256 MB (128 costs drizzle +12%).

### Per-file AST arenas and huge pages (2026-10-14 .. 16)
Per-file AST arenas (`ast_owns()` by address; frees of arena memory are no-ops after the parse) and huge
pages. Madvising every arena chunk on a long-running, fragmented machine made vscode **9.39 s with 13.9k compaction
stalls** against 6.17 s without: chunks are now madvised only while a `/proc/buddyinfo` budget lasts
(`huge_pages_take()`), with `MADV_POPULATE_WRITE` prefaulting. Oracle: `compact_stall` and `thp_fault_alloc` in
`/proc/vmstat` around a vscode run. vscode 8.14 s / 2779 MB -> 5.91 / 2727 at the defaults (the glibc re-exec gone).

### vscode K1 against bun's program memory (2026-10-15)
vscode K1 against bun 1.4.3 at one thread then: program after names 2191 MB vs bun ~1300; check on top ~360 MB vs
~80; peak 2549 vs ~1400. The gap was the AST representation (782 MB of zero words) plus the recycling budget; the
struct diet was declared done (< 120 MB left). Since then deferred bodies, mapped source text and the budgets brought vscode K1
to ~1370 MB (the 12-core table above).

### The 12-core machine: checkers per K (2026-10-16, f760ecdc)
Release build, K24 default then. K4 -> K12 scaled 1.2-2.0x; **past 12 almost nothing** (K12 -> K24 0-11% on vscode,
webpack, effect; 0 elsewhere) while each checker past 4 added ~170 MB on vscode: the origin of the physical-core cap
and the 1.5 GB total budget cap. A fresh checker on a one-line esnext+dom program holds a 7 MB arena (13k types): the
libraries aren't the floor. The front end there: vscode load phase 389 ms (loader waited 6 ms, workers 59% busy),
declare + link 130 ms (5% of wall), webpack's 45 ms (15%: its serial declare, since fixed to 18 ms).

### Memory breakdown (2026-10-12, e043e9e2)
Before the memory work of 2026-10-12..17: vscode K1 5220 MB (AST 2.17 GB, binder ~0.6, checker arena 1.82 GB, LazyMemo 222 MB, intern
tables 202 MB). Today's per-corpus peaks are the bench tables above. The tuple fix (targets share element type
parameters) took type-fest's K1 arena 1906 -> 695 MB, types 4.78M -> 217k.

### Perf at breadth (2026-10-12)
The perf-at-breadth pass drained every project above 1.5x tsgo's types: types ratio median 0.76 -> 0.68, worst 16.4 -> 1.55
(date-fns docs, feathers typebox, nitro, rxjs, nuxt...); peak RSS ratio worst 2.48 -> 0.95; total types 96.0M ->
71.3M against tsgo's 127.9M. A types ratio above ~1.5 is a missing cache or a runaway.

### Relation counts against tsc (2026-10-06 and 2026-10-07)
Compare relation cache sizes (`tttt --time --checkers 1`) with tsc's `--extendedDiagnostics`: vue's strict-subtype
228k pairs against tsc's 15k led to three tsc shortcuts (same-index members of union pairs, an intersection's key
property in subtype reduction, object-to-primitive) worth vue -13.5%, drizzle -17.5%. On 2026-10-07 the four
checkers redid ~24% of K1's type work on vscode; work volume at K1 was close to tsc's (vscode types 2.57M vs 2.89M,
assignable 960k vs 1008k). Sharing types between checkers was judged not a lever ("What the checkers re-derive" confirmed it
2026-10-18).

### Smaller results worth remembering
- Checker work stealing (2026-10-17): -5.2% summed wall on the sweep's 285 biggest, peaks flat.
- Gated runs (options/syntax diagnostics) parse only deferred bodies for syntax errors instead of a full restart
  (2026-10-17): gated sweep projects -25% wall.
- The loader's serial work (2026-10-16): vscode load T4 940 -> 805 ms, `newfstatat` 194k -> 43k.
  Load discovery (a pre-scan) stays unjustified: the loader waits 6-8 ms.
- Output independent of K (2026-10-13/14): tsgo's import-graph FENNEL partitioner duplicated 2-21% more
  types than contiguous runs. Below 64 KB of checked source more checkers buy nothing (one per 64 KB).
- Declaration emit's cost (2026-10-15): +1.3-4.4% over `--noEmit --declaration` at K1; maps add ~0-4%.
- Checker recycling (2026-10-15): at 256 MB it took vscode K1 4190 -> 2575 MB for +2.4% wall.
- Parallel name resolution (2026-10-05): vscode resolve 593 -> 283 ms; declare and link stay serial.
- Refreshes (2026-10-17 and after): new TypeScript repos match >= 96% on day one, the checker generalizes;
  JavaScript (checkJs) is where new code finds bugs. The `-p` sweep only walked `tsconfig.json` until 2026-10-19.
- Instructions history: drizzle +50% in 2026-10-17 went unnoticed for two days (an intersection target's properties
  not seeing the intersection as `this`: variance measurements related ever longer intersections). Investigate any
  flagged instrcount rise before committing.

### Fresh repositories (2026-10-17)
How a refresh is run (repos.txt's "Plan 77" comment: GitHub topics, 300-3000 stars, the first 3 per topic with a root
lockfile): append rows to `test/sourcesweep/repos.txt`, `thirdparty/ensure-sources.sh NAMES`, `scripts/sourcesweep.py
--tsc-new`, `scripts/dtsdiff.py --tsc -f RE`, then both with `-f '^(names)(/|$)'`; record day one before any fix.
The first refresh's 28 repos: 195 / 203 projects on day one (96.1%), two checker root causes, 203 / 203 after.

## Instruction counts re-saved (2026-10-24 ~05:30, main at e38626540)
`scripts/instrcount.sh --save` after the day's correctness fixes and the perf agent's two fixes (5a2146435 per-file
`@link` gate, 87c39fb10 memoized tsc_could_contain_type_variables). Against the previous save (85b7c7520, 2026-10-23)
the drift was +0.5..+1.4% on every corpus; the Sonnet perf bisect attributed zod's +1.24% as: de8e8eef9 (erased
signature's param is a callback) +0.18%, 7156e6af4 (couldContainTypeVariables in inference, now memoized) +0.23%,
94a7c0195 (JSDoc {@link} scan, now gated per file) +0.30%, a118b2539 (object literal outer_instantiable) +0.21%, the
rest many commits under +0.1% each. The two behaviour costs stay; the two avoidable ones were recovered (zod -0.30%).
New reference: zod 3.643 G, drizzle 4.526 G, vscode 67.207 G (fast/tttt, callgrind).

## One-checker rerun cost (2026-10-25, this box, fast builds, abbench -n 5, d22d93ae vs main 273d8ff92)
Before the trigger fix (27cd0d2cf), under load: vue wall +177%, type-fest +495%, effect +306% (user +73/+113/+83%);
type-fest and effect reran on `@ts-expect-error`-suppressed TS2589/TS2590 that the output never shows. After it,
quiet box: vue +265% wall (0.19 -> 0.70 s; its shown TS5115 still reruns: the legitimate cost, about one K1 check on
top), type-fest +5.9%, effect +11.4%, drizzle +2.1%, excalidraw +6.2% (user +1..+5%; callgrind zod +1.74%, drizzle
+1.31%, excalidraw +1.70%, type-fest +1.52%, effect +1.72% over the same span: the 223 correctness commits' drift, no rerun involved). tsgo's caches have a limit code in 28 / 6990 source-sweep configs and a circularity code
in 112: the ceiling for legitimate reruns. agents/notes/one_checker_rerun.md (det3) has the design discussion.

## The d22d93ae..main per-file regressions, after the fixes (2026-10-25, this box, fast builds, interleaved best of 2, loaded box)
Three commits had made single files 4-12x slower (no rerun involved): e01e80779 (identity relates normalized types:
react-scan's babel file 28 -> 346 ms), 768a930c4 (union contextual type discriminated at any size: monkeytype
setters.ts 30 -> 130 ms), d06134025 (identity keyed by the pair in id order leaking into variance measurements:
renovate redis.ts 57k -> 76k types). Fixed by 0f65555de (memoized intersection-object/union-index accesses when
clean), 802676432 (constraint of an indexed access by a union of keys cached), c65872cc0 (no shared identity key
inside a variance measurement): 22 / 22 ms / 57k types. The 20 biggest sweep configs measured old vs main 99f68d63d:
median wall ratio 1.05, p90 1.20, max 1.73 (soybean-admin, a legitimate rerun); the previous run before the fixes had
react-scan 3.57x and three projects at 1.4-1.5x that were load noise (fixE's K1 re-measure: not slower).

## The d22d93ae..main instruction drift on zod, attributed (2026-10-25, perf2, release builds via abbuild.sh, callgrind)
zod 4.335 G (d22d93ae, and the first commit after it) -> 4.396 G at bee864e1f (+1.41%), 261 src commits. Curve by
commit index (1/33/65/97/129/161/193/225/257): 4.335 4.348 4.355 4.378 4.402 4.394 4.400 4.409 4.396; the last
step down is the recovery. Contributors, parent vs commit (3-digit resolution, ~0.02%):
- de8e8eef9 (erased signature's parameter is a callback unless generic in the instantiation it erases): +0.008 G
  (+0.18%); an extra per-parameter callback test on every erased signature; inherent (tsc's rule), kept.
- 7156e6af4 (inference asks what couldContainTypeVariables would): +0.012 G (+0.27%); an 89-line walk per inference
  candidate; avoidable, memoized since by 87c39fb10.
- 94a7c0195 (JSDoc `{@link x}` names a block local): +0.012 G (+0.27%); a JSDoc scan of every statement in every
  block; avoidable, gated per file since by 5a2146435.
- a118b2539 (object literal instantiated by outer type parameters isn't marked as containing type variables):
  +0.010 G (+0.23%); fewer early-outs downstream; inherent to the semantics, kept.
Not contributors (parent vs commit equal): 5f7885f36, dc9d07bb1, 22280be87. Sum of the four 0.042 G; two recovered
(-0.024 G in the later step), so the remaining ~0.02 G (+0.45%) is spread thin over the first 65 commits (+0.013 G in
1..33, +0.007 G in 33..65; a bump at index 25: faeef9d01 4.366 then 4.348 at 41, so a transient later undone) with
no commit found over the 0.15% threshold. Mechanisms are read from the commit messages and diffs' sizes, not
profiled. Drizzle/excalidraw were not re-confirmed (budget).

## tttt and bun, each against its own single thread (2026-10-28, perf, this box shared at load 5-23)
`scripts/bunsweep.py --bun bun-1.4.4-canary.20261027.1 --bin fast/tttt@5137f5cc7 -n 300`: the 300 source-sweep
projects tsgo checks slowest, tttt defaults (K8) / tttt `--threads 1 --checkers 1` / bun defaults / bun `--threads=1`,
interleaved, best of 3. tttt ST/MT median 3.16 (p10 1.74, min 0.81; MT <= ST on 7, all reruns); bun ST/MT 3.33 (p10
1.80); tttt MT / bun MT 0.76 (bun faster on 65); tttt ST / bun ST 0.70. Check phase: CPU MT/ST 1.66 = duplication
1.22 (K8/K1 at `--threads 1`, median; p90 2.24) x ~1.36 per-instruction, 6.3 of 8 cores busy; bun CPU MT/ST 1.90 at
7.3 busy. Summed loss over ST/8: 51.9 s of 66.6 s MT wall (dup 9.2, idle tail 12.8, front end 9.5, rerun 4.0).
Details and the verdicts: agents/notes/bun_breadth.md "2026-10-28".

## The 12-core box at 15f15fdeb: the shared graph against plain at the defaults and at 24/24 (2026-10-29)
Run by an agent on the human's laptop (Ryzen AI 9 HX 370, 24 threads), fast build of main 15f15fdeb, 5 rounds of
interleaved pairs per project from an empty directory, `TTTT_SYNC_EXIT=1`; the defaults chose 12 checkers on 24
threads everywhere; "24/24" is `--threads 24 --checkers 24`. dokploy and openbot installed there with ensure-sources.sh
(openbot/packages/i18n is the i18n project). Medians (best of 5 in brackets); user is CPU seconds.

| project | mode | plain wall | SG wall | wall | plain user | SG user | plain RSS MB | SG RSS MB |
|---|---|---|---|---|---|---|---|---|
| zod | def | 0.16 (0.15) | 0.16 (0.15) | 0 | 1.3 | 1.5 | 773 | 668 |
| zod | 24/24 | 0.16 (0.15) | 0.17 (0.15) | +6% | 2.2 | 2.1 | 900 | 815 |
| drizzle | def | 0.24 (0.22) | 0.19 (0.19) | -21% | 2.1 | 1.8 | 990 | 734 |
| drizzle | 24/24 | 0.22 (0.22) | 0.18 (0.18) | -18% | 3.7 | 2.8 | 1266 | 920 |
| effect | def | 0.24 (0.22) | 0.25 (0.24) | +4% | 2.2 | 2.6 | 911 | 890 |
| effect | 24/24 | 0.22 (0.20) | 0.25 (0.23) | +14% | 3.3 | 3.6 | 1001 | 1045 |
| vscode | def | 1.73 (1.64) | 1.91 (1.79) | +10% | 28.8 | 29.9 | 5255 | 4919 |
| vscode | 24/24 | 1.70 (1.52) | 1.90 (1.82) | +12% | 29.1 | 30.7 | 5244 | 4909 |
| dokploy | def | 0.86 (0.82) | 0.87 (0.74) | +1% (best -10%) | 8.6 | 8.2 | 2881 | 1736 |
| dokploy | 24/24 | 1.14 (1.02) | 1.01 (0.88) | -11% | 22.6 | 16.1 | 4683 | 2387 |
| i18n | def | 0.36 (0.35) | 0.16 (0.16) | -56% | 2.5 | 1.3 | 1278 | 662 |
| i18n | 24/24 | 0.43 (0.41) | 0.17 (0.16) | -60% | 4.8 | 2.2 | 1713 | 777 |

- The 24-thread point: 12 -> 24 checkers makes plain slower on the hub projects (dokploy 0.86 -> 1.14 s, i18n 0.36 ->
  0.43: the duplication); the shared graph holds steady there. On the corpora 24/24 changes little.
- Memory: SG uses less RSS everywhere but effect at 24/24 (+4%): -40..-50% on dokploy and i18n, -6% on vscode.
- vscode: runs varied 0.3-0.5 s between rounds, but SG lost all 5 pairs in both modes; its wall loss (+10..12%) is
  larger than its CPU loss (+4..5%): the critical path, not only the per-read cost (open: notes/hub_regime.md).
- Correctness: the shared graph is nondeterministic on drizzle (residue C, bugs/shared_graph.md): at 24/24 4 different
  outputs in 5 runs, 1 of 8 more matched K1 (the others add a TS2430 at mysql-core/query-builders/select.ts(1076,18),
  one also drops the TS2416 at mysql2/session.ts(281,17)); at the defaults 3 of 10 differ. Plain matched K1 in every
  run; the other five projects were identical in every run. Far more often than the 5/10 at K8 recorded on this box.

## The flag's wall loss on vscode: the mm-lock storm fixed (2026-10-29, sgwall, this box at load 12-22)
Fast builds: main 8c9afd512 ("flag base", and plain) against wt-sgwall 10f159e48 before its rebase onto becddb01b (as
a63f51e46 after; "flag new": ZeroPages for the graph's memory and the stamp chunks, 256 shards; notes/shared_graph.md "The wall loss beyond the CPU loss"). 5 interleaved
rounds, `TTTT_SYNC_EXIT=1`, whole-run wall and user+sys from /usr/bin/time; "def" is the defaults (16 threads, K16 on
vscode, K8 on effect), "16" is `--threads 16 --checkers 16`. Medians (best in brackets); other workers loaded the box.

| project | mode | plain wall | flag base wall | flag new wall | plain CPU | flag base CPU | flag new CPU |
|---|---|---|---|---|---|---|---|
| zod | def | 0.19 (0.19) | 0.22 (0.21) | 0.23 (0.22) | 1.1 | 1.3 | 1.3 |
| zod | 16 | 0.26 (0.24) | 0.24 (0.22) | 0.27 (0.24) | 1.5 | 1.5 | 1.6 |
| drizzle | def | 0.28 (0.26) | 0.27 (0.26) | 0.27 (0.27) | 1.7 | 1.7 | 1.8 |
| drizzle | 16 | 0.33 (0.31) | 0.29 (0.25) | 0.28 (0.25) | 2.4 | 2.0 | 2.1 |
| effect | def | 0.32 (0.31) | 0.35 (0.35) | 0.37 (0.36) | 2.0 | 2.3 | 2.5 |
| effect | 16 | 0.32 (0.29) | 0.35 (0.33) | 0.36 (0.35) | 2.3 | 2.6 | 2.7 |
| dokploy/apps/dokploy | def | 0.93 (0.90) | 0.96 (0.94) | 0.98 (0.94) | 6.3 | 6.5 | 6.6 |
| dokploy/apps/dokploy | 16 | 1.34 (1.33) | 1.22 (1.05) | 1.11 (0.99) | 11.0 | 9.2 | 8.3 |
| vscode | def | 2.01 (1.92) | 2.63 (2.62) | 2.12 (2.05) | 19.1 | 20.4 | 20.6 |
| vscode | 16 | 2.06 (1.98) | 2.70 (2.64) | 2.50 (2.19) | 19.1 | 20.9 | 20.9 |

- vscode: the flag's wall loss at the defaults falls from +31% to +5%, about its CPU loss (+8% in the check phase).
  The timeline's check phase at load 24 (3 runs each, defaults and 16/16): checker threads' blocked time, summed,
  plain 0.6-1.3 s, flag base 9.2-10.6 s, flag new 0.7-0.9 s; check wall 1.50-1.77 / 2.38-2.57 / 1.62-1.74 s.
  strace: mprotect() calls plain 41, flag base 11.3k, flag new 39.
- effect: 8 more interleaved rounds at the defaults, check CPU medians 2182 / 2206 (ZeroPages alone) / 2233 ms (with
  256 shards), mins 2052 / 2013 / 2092, check wall 380 / 344 / 333: noise at load 21; cachegrind at one thread K8 has
  the 256 shards' D1 misses +0.4%, LL +0.1%. effect's loss is CPU on its costliest files (notes/shared_graph.md).
- Flag K8 at one thread, callgrind: zod -1.2%, drizzle -1.2% instructions (ZeroPages: no memset of doubled tables).
- Peak RSS under the flag +6..10 MB on zod and drizzle (the 256 KB populated ahead per ZeroPages, one per checker).

## The shared graph's go/no-go table (2026-10-29, verdict, release build of main 33e018294, this box at load 10-15)
`scripts/sgbench.py --modes 8,16,def -n 10` (sgcheck's 58 projects, plain against `TTTT_SHARED_GRAPH=1`, interleaved,
best of 10, `TTTT_SYNC_EXIT=1`, from an empty directory, nice 15; load at run start median 10.6, p90 15.2). Flag/plain
medians, check wall (the typecheck phase) / whole wall / peak RSS: at the defaults (K8 on most) hubs 0.98 / 1.00 /
0.72, corpora 1.14 / 1.10 / 0.92, all 58 1.05 / 1.00 / 0.74 (30 worse by >5%, 9 within, 19 better); at 16/16 hubs
0.89 / 0.87 / 0.58, corpora 1.22 / 1.12 / 0.79. Corpora at the defaults: react +43%, xstate +29%, zod +27%, effect
+16%, vscode +6%, drizzle -9%; hubs: i18n -50%, team-client -34%, platform-fastify/test +47%. `sgcheck.py -k 8,16
-n 2`: 0 of 464 differ. Verdict: option for hub-shaped projects, not default-on (notes/hub_regime.md "The go/no-go
table"); a new K7/K8 deep-cut storm on monkeytype/frontend (bugs/shared_graph.md).
