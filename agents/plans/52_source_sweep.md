# Plan 52: the source sweep (real TypeScript repositories at breadth)
The source-code analogue of plan 44: tttt on ~150 real TypeScript repositories, every `tsconfig.json` in each checked
with `-p`, against tsc 4d4f005c's cached diagnostics, mismatches ranked by root cause across repositories. Planned on
2026-10-08 from a 22-repository prototype (scripts and manifest in `52_prototypes/`, checkouts in `/home/ai/ss`, which
can be deleted).

## Why
The pinned corpora are at 0 and go quiet once drained, and DefinitelyTyped covers declarations plus small tests. Every
new real project found tens to hundreds of bugs on its first day. The prototype confirms it: of 655 projects in 22
repos, **344 match tsc; 139 differ only in checker diagnostics, 83 only in config/program-level ones, 89 in both**
(~2700 missing and ~2600 spurious checker diagnostics, ~30k spurious config ones, mostly one TS6059 bug). Two real bugs
were found while building the prototype (fixed in the same commit):
- `tttt -p` with no `target` loaded an ES5 + few-extras lib list (`main.c` `default_libs`, a pre-6.0 leftover) instead of
  tsc 6's default target ES2025's full lib. Every corpus sets a target, so nothing noticed.
- `sweep.py` dropped diagnostics without a file (`[TS2688] ...`, options diagnostics) when parsing tttt's output, while
  tsc's oracle has them as `('', 0)`: always "missing". The DT sweep is unchanged by the fix (8594 / 8595).

## What the prototype measured
- **Selection**: GitHub search (`language:TypeScript stars:>3000 sort:stars`, unauthenticated API is fine: 10 pages of
  100) gives apps, monorepos and libraries; many top hits aren't code (roadmaps, curricula) or are huge. Prototype set:
  zustand redux immer hono ky trpc date-fns mobx svelte query react-router vite socket.io quill pixijs mermaid
  gemini-cli jest nest hoppscotch tabby shadcn-ui (manifest with commits in `52_prototypes/manifest.txt`).
- **Node**: the system node 22.22.1 is too old for half of them (pnpm 11/12 need node >= 22.22.2/24; `engines` fields
  ask for node 24). A downloaded node 24 (`https://nodejs.org/dist/v24.x/node-v24.x-linux-x64.tar.xz`) on `PATH` for
  installs, with corepack picking each repo's `packageManager` version, installed **all 22** with scripts off.
- **Install** (`52_prototypes/install.py`): pnpm (`packageManager` or `pnpm-lock.yaml`; default pnpm@10) with
  `--frozen-lockfile --ignore-scripts --store-dir=<shared store>`; yarn 1 `--ignore-scripts --ignore-engines`
  (`--frozen-lockfile` to add); yarn berry with `YARN_NODE_LINKER=node-modules YARN_ENABLE_SCRIPTS=false`, `--mode=skip-build`;
  npm `ci --ignore-scripts --legacy-peer-deps`. ~10-40 s per repo from a warm cache. ky has no lockfile (npm
  install floated): drop lockless repos or commit a lock like `corpora-locks/`.
- **Disk**: 17 GB for 22 repos as installed. Pruning node_modules to `.ts/.tsx/.mts/.cts/.js/.jsx/.mjs/.cjs/.json`
  (`52_prototypes/prune.sh`; deletes `.map`, `.node`, `.wasm`, docs, binaries) took it to 9.4 GB, and **tsc's
  diagnostics were identical on all 655 projects after pruning** (tsc is deterministic run to run too). `hardlink -c`
  across the repos then took it to **6.6 GB (~300 MB a repo)**, but took 24 min (millions of small files): run it
  per newly installed repo against the rest, or only once at the end (pnpm's files are store hardlinks already; delete
  the store after installing, or prune it too). Budget ~40 GB for 150 repos, select by GitHub `size` to stay in it.
- **Projects**: every directory with a `tsconfig.json` outside `node_modules`, `.git`, `dist`, `build`, dot-dirs (655
  in 22 repos: jest's e2e fixtures alone are 130, query 102, nest 95). Only 78 are clean under tsc: solution configs,
  test configs without the test runner's types, unbuilt project references (TS6305), deliberately broken fixtures. That
  is fine: the oracle is tsc's output, not cleanliness, and the config-level mismatches are real tttt bugs (they
  exercise extends chains, `references` under `-p`, rootDir, composite file lists, `types`, which the corpora don't).
  Mining `package.json` scripts for upstream's own `tsc -p X` / `tsc -b` invocations is possible (`tsc`, `vue-tsc`,
  `svelte-check` in scripts) but heterogeneous; not needed for stage 1.
- **tsc oracle**: `test/sweep_tsc.js` works unchanged (cwd = the checkouts' parent, project dirs on stdin; checked equal
  to `tsc -p` on hono/benchmarks/jsx). 9 min wall on 4 cores (31 CPU-min) for the 655 projects (slowest: shadcn-ui's
  root 95 s, gemini-cli 62 s); 12 MB of JSON lines, 0.8 MB gzipped. 150 repos: ~1 h, ~6 MB gzipped in git.
- **tttt**: `sweep.run_tttt` (one checker, one thread, `ulimit -v`) on all 655 in **45 s wall**, no crash, no timeout.
- **Ranking**: `sweep.root_cause` keys per file outside the project, which in a monorepo makes one key per sibling file
  (nest: 46 projects x 60 files of TS6059). The prototype's `compare.py` keys by code + message shape, plus the
  `node_modules/[@scope/]name` dependency for files there, and ranks by **repositories**, then projects. Good enough to
  start; the root-cause keys will need what plan 44's did (a declaration in a shared dependency is one bug).

Top of the prototype's ranking (repos / projects / diags), to calibrate stage 2:
`+TS2322` 8/27/243, `+TS2345` 8/25/127, `+TS7006` 8/20/337 and `-TS7006` 7/57/687 (contextual typing both ways),
`+TS2304` 7/45/260 with `-TS2593` 6/42/257 (test globals: tsc's "do you need @types/jest" variant vs plain 2304),
`+TS6059` 6/97/28687 (rootDir against files pulled in through references/paths), `+TS2339` 6/20/85, `+TS18048`
6/15/54, `-TS6305` 5/76/1603 (date-fns/pkgs/core: under `-p`, tsc resolves imports of a referenced project to its
unbuilt outputs and reports TS6305, tttt reads the sources and reports nothing), `+TS18046`, `+TS2578`, `+TS2353`,
`-TS2307` (jest e2e), `+TS6307`, `+TS2589 in node_modules/@sinclair/typebox` (47 jest projects), `+TS2303 in
node_modules/mongoose` (circular import alias), `+TS7022`, `+TS1005` in a `.mjs` (`import x from` parse).

## Stages
### Stage 1 (todo entry 5): build `./sourcesweep.py`, cache tsc, first numbers
1. `thirdparty/ensure-sources.sh` (or a mode of ensure-corpora.sh): reads `test/sourcesweep/repos.txt` (`NAME URL
   COMMIT` per line, a comment per repo if it needed anything), fetches each at its commit (depth 1) into
   `thirdparty/sources/NAME` (untracked, like corpora; worktrees symlink it), fetches node 24 into
   `thirdparty/node/` (untracked, pinned version and sha256 in the script) for the installs only, installs as above
   with a shared store/cache under `thirdparty/sources/.cache`, prunes, hardlinks. Idempotent, like ensure-corpora.sh.
   Use the system node for running tsc (as the other oracles do) unless the stage finds a reason not to.
2. Selection: ~150 repos. Start from GitHub search (stars > 1000, pushed in the last year, not archived, `size` under
   ~300 MB, has a lockfile and a tsconfig.json), drop non-code repos and the existing corpora, and keep a mix: ~1/3
   libraries (the npm most-depended with TS sources: trpc, hono, zod-like), ~1/3 apps (React/Vue/Angular/Node
   servers), ~1/3 monorepos; some JSX-heavy, some checkJs/allowJs, some `moduleResolution` bundler/node16, some with
   `references`. Keep the prototype's 22. A repo whose frozen install fails under its own package manager is dropped
   (note it in repos.txt as a comment), not patched. Write `scripts/` or a `--select` mode only if it saves time; the
   output is the committed manifest.
3. `./sourcesweep.py` modelled on `./sweep.py` (reuse its `run_tttt`/`compare`/`shape` by import or by moving them to a
   small shared module): `-f regex` on `NAME/path`, `-k KEY`, `--names VERDICT`, `--last`, `--tsc` (regenerate cached
   verdicts, all or `-f`'s), crashes and timeouts first, slower-than-tsc list, ranking by repos then projects (the
   prototype's key, `52_prototypes/compare.py`). Oracle in `test/sourcesweep/tsc.jsonl.gz`. Report the totals as
   "projects match / total (clean under tsc), repos fully matching / total".
4. Run tsc once (~1 h, commit before starting it), run tttt, fix crashes/hangs/runaways found (none in the
   prototype), record the first numbers in `agents/measurements.md` and the oracle list in CLAUDE.md (one bullet, like
   sweep.py's) and `./oracles.py` (one summary line; it must stay a few minutes: if sourcesweep is over ~3 min wall,
   give oracles.py a sample, e.g. `-f` on a fixed subset).
5. Validation: the tool runs from cache in minutes, every manifest repo has a tsc verdict, crashes/hangs 0, numbers in
   measurements.md, the ranking makes sense on the top 10 keys (spot-check each is a tttt bug, not a harness artifact:
   the prototype's first run had two, the file-less diagnostics and an output-path mistake).

### Stage 2 (todo entry 8): drain, fan-out on root causes
Config/program-level first (TS6059/TS6307/TS6305/TS2593-vs-TS2304/TS5xxx: they cascade into checker mismatches, e.g.
unresolved references make `any` and hide TS7006), then the checker keys by repositories. Four sub-agents in
worktrees, disjoint key sets, 0 lost on every oracle at merge (`./oracles.py`). Fixes must be mechanisms; a key that
needs a special case goes to bugs/.

### Stage 3 (later): the missing direction and `-b`
- Flag flips (plan 46 cells) on the projects that match: `--strict` where the tsconfig isn't, `noUncheckedIndexedAccess`,
  `exactOptionalPropertyTypes`, cached per flag like `./sweep.py --flags`.
- Solution configs (`files: []` + `references`) checked with `-b`: tsc's oracle then needs `tsc -b` to emit into a
  scratch copy of the tree (hardlinked copy, outputs are new files). Only if stage 2 shows `-p` on each leaf misses
  what `-b` would find.
- Mining upstream's own `tsc` invocations from package.json scripts, if the every-tsconfig policy turns out to be noisy.

## Risks
- Disk (~40 GB budget, 99 GB free at planning time) and /tmp inodes: keep checkouts under `thirdparty/sources`, not /tmp.
- Upstream drift doesn't matter (pinned commits), but a dependency registry outage does: the first fetch needs the
  network; after that only the checkouts are read. Don't re-fetch in `./sourcesweep.py`'s normal run.
- Noise from broken projects (shadcn-ui's root alone: 74k TS17004, jsx not set): fine for comparison, but a project
  with > ~5000 tsc diagnostics costs time and can dominate a key's diag count; rank by repos/projects, not diags.
