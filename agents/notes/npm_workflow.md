# The npm workflow (the only priority since 2026-10-23 late; the human's rules)

The coordinator's pipeline brief, copied here so a second box or a fresh agent can run finder jobs. Worker ids in it belong to the
coordinator's session; another box reports to its own coordinator and lands through the usual branch/land.sh route.

The human's words: "this workflow is the only priority right now. agents check new packages or make minimal repros
as long as these cases are scarce, once there's ample clean typecheck flipped cases all agents work on solving them,
no agents work on divergences between packages that don't result in a flip, bun cases, or other work until
saturation." And: untrusted code runs sandboxed (scripts/installsandbox.py, agents/notes/install_security.md) or is
reviewed; deps of random packages are as untrusted as the packages.

## Priority order (the human, 2026-10-23 late)
1. Any race condition or thread-count/checker-count dependence, found anywhere (a project whose output differs
   between the defaults and `--threads 1`, or between two default runs, or kdiff): report it to det3 (agent
   ae871f103047fd698) AND the coordinator at once, with the project kept until det3 has what it needs. det3 owns
   these; they outrank flips.
2. Flip cases (tsgo and tttt disagree on whether the project has any error).
3. Finding more cases (sampling, setup, compare, repros) while flips are scarce. A flip is EITHER direction (tsgo
   clean and tttt not, or tsgo erroring and tttt clean: quartz-engine was the second kind), so never pre-filter the
   sample on tsgo-clean projects; tttt's run is cheap next to the install anyway.
Nothing else (non-flip divergences, bun cases, text differences, other work) is a work AREA until the flips saturate.
This is about where agents are pointed, not a ban on other fixes: while solving a flip or a race, fixing related
problems you meet, or doing the refactor that gives a structural solution instead of a point fix, is welcome (the
human: "that sort of thing is great"). Say what else the change fixes in the commit message and flip those entries.
Perf must not regress: quickgate --vs-main per fix (instrcount within +0.3% or say so); landings are batched.

## The pipeline
1. **Sample** (worker npm only, because the registry rate-limits per host): `scripts/npmsample.py sample --seed S`
   for seeds 20261024, 20261025, ... in sequence; each seed's candidates file goes to test/npmsample/S.candidates.jsonl
   and is committed as soon as it exists (the finders pull it). Seeds are handed out by the coordinator.
2. **Set up and compare** (finders, one seed each, in their own worktree): `npmsample.py setup --seed S -N 50` and
   `compare --seed S` under the sandbox (the compare runs tttt at the defaults twice and at `--threads 1`: a difference between any two is a priority-1 finding); a project with no divergence is deleted at once; `clean` at the end of the
   batch. Disk: `df -h /` before a batch; stop under 25 GB free.
3. **Repro** (the finder of that seed): every divergence becomes a minimal repro in test/cases/, with `reduce.py`
   (`--project <clone> --config tsconfig.bench.json`; `--tsgo-clean` rejects cuts that break tsgo) then by hand.
   - Test file name: `npm_<package-slug>_<code>.ts` (`.tsx` when JSX); the package slug is the npm name with `@`/`/`
     turned into `_`.
   - First two lines of the file, ordinary comments (the harness ignores them):
     `// origin: npm <name>@<version> <repo url> <config path> seed <S>` and
     `// flip: yes` when tsgo and tttt disagree on whether that PROJECT has any error (tsgo clean, tttt not, or the
     reverse), `// flip: no` otherwise. A text-only difference (same file/line/code) is not a divergence here.
   - Then `scripts/owntests.sh test/cases/npm_<...>.ts` for its baselines; the test must FAIL on main (that's the
     point) and is committed failing. Also one line in `test/npmsample/repros.tsv`: `test name<TAB>flip<TAB>name@version
     <TAB>repo<TAB>tsgo errors<TAB>tttt errors<TAB>codes` (append-only, the registry of real-world cases; bugcount
     will count it).
   - An [open] bug entry in agents/bugs/npmsample.md (one bullet per repro, root-cause guess, `[flip]` as an
     extra tag before the status tag for flips).
   - Then the clone is deleted. Keep nothing of the project.
4. **Fix** (fixers): flips first, always. A fixer takes a test from repros.tsv marked flip whose bug entry isn't
   claimed (claim it by adding `(claimed: NAME)` in the entry and committing), fixes the root cause (never a special
   case), the test passes, quickgate --vs-main, instrcount within +0.3%, land request to the coordinator. Non-flip
   repros wait until flips are exhausted.
5. When flips run out, fixers become finders (take the next seed from the coordinator).

## Rules (unchanged)
Worktrees only; no test/baseline_results.ts in commits; literal rm paths only; one heavy run at a time per worker;
never a symlink from scratch into thirdparty/; tsgo on clones only through npmsample.py/projdiff.py (sandboxed);
report to the coordinator (SendMessage to "main") per batch and per fix.

## Roles (2026-10-24 early, the human: finders are disk-limited, so more of them in parallel doesn't help)
Finders: npm (also the sampler, one seed ahead), corH, cor9. Fixers: cor10, corB, corF (after their current batches),
flips first, else non-flip repros from repros.tsv, claimed in bugs/npmsample.md. A new flip does not preempt a fixer
mid-fix: it finishes what it's on, and the coordinator points the next free fixer at the flip (the human, 2026-10-24). det3: races/K-dependence.

## Seed ranges
- This box (the coordinator's): 20261023 upward, one per batch (20261035, 20261036, ... next).
- A second box: 20261100-20261199, so records never collide; commit each seed's files under test/npmsample/ as here.

Fixers take the top of `rank` when no flip is open; a finder checks `rank` before reducing a divergence whose key is already filed (`scripts/npmsample.py rank [--min N] [--json]`).

## Remote finder box (box3, 2026-10-24: a throwaway box the human lent, driven over ssh, seeds 20261200-20261299)
- The box: Ubuntu 26.10, kernel 7.3, 4 cores, 30 GB RAM, 225 GB disk. Installed with apt: bubblewrap (0.12.0), meson,
  ninja-build, clang, lld, valgrind (python3, git, gcc 15, curl, tmux were there). Go comes from ensure-typescript.sh.
- Everything runs as an unprivileged user (`finder`), not root: bwrap works for it with Ubuntu's
  `kernel.apparmor_restrict_unprivileged_userns=1` (the bwrap AppArmor profile allows the namespaces). The deploy
  key goes to that user's ~/.ssh (mode 600, github.com in known_hosts) and git user.name/email are set there.
- The repo is cloned at /home/finder/tttt. `scripts/fasttest.sh -f '^nothing$'` took 3m36s, and that covered the
  meson release build and ensure-typescript.sh (Go fetched, tsgo built). thirdparty/node (node v24.21.0, bun 1.4.3)
  was copied from the main box with tar over ssh. ensure-sources.sh's fetch_node/fetch_bun would fetch the same.
- The sandbox check: installsandbox.py `install` on a package.json whose postinstall writes to $HOME (it didn't run),
  then `run` (empty $HOME, read-only /etc, no network, the repo invisible unless bound).
- Seeds run in tmux sessions through a small wrapper (sample, then setup -N 50 --prefer-large, then compare). The
  second seed's sample waits for the first's, so only one pacer hits the registry from the host at a time.
- Writing test files over nested `ssh ... su -c "..."` quoting breaks on parentheses and apostrophes. Write them on
  the driving box and scp them over.
- Ubuntu 26.10's coreutils are the Rust uutils: `tail -3` is refused, write `tail -n 3`.
- reduce.py's `--files` globs are relative to the copy root. A package under a workspace member is at
  `packages/X/node_modules/*` there, not `node_modules/*`.
- `clean --all --except NAME` used to ignore --except and delete everything, which is how seed 20261201's octane
  clone went before its repro was cut. Fixed: --except holds a clone back under --all too.
- Throughput with two seeds at once on 4 cores: 54, 62 and 70 projects accepted, each seed's setup and compare done in
  about 20-30 minutes; load stays around 5-10, RSS well under the 30 GB.
- A reduce.py run with --tsgo-clean on a workspace package's sources can keep every file whole, because deleting one
  breaks tsgo's imports, and its line pass over ~150 files then takes hours. When the diagnostic sits inside one
  function, try that function alone in a test file first (octane's TS7022: 10 lines, in minutes).

- **Pull main right before every compare.** compare builds release/ from whatever tree is there. The first
  wrappers never pulled, so akash's two divergences were filed on a build hours old, and one of them was already
  fixed. The wrappers below pull just before compare.
- Large mode (seeds 20261400-20261404): without a GitHub token, `sample --large -n 60` takes about 75 minutes
  (languages API, 60 calls an hour per IP); with GITHUB_TOKEN it takes 3 minutes. `setup --large -N 50 --per-repo 3`
  takes 50-80 minutes on 4 cores, dominated by big monorepo installs (compass 393 s, teleport 318 s, both trial
  rejects). The large batches gave 0-6 flips each, many more than ordinary seeds (0-1). Reducing them all on one
  box was the bottleneck, so from 20261402 on the box only filed bullets with the details (repo, commit, directory,
  config, first diagnostics at file:line, flip, K-dependence) and the fixers cloned and reduced.
- The token lived in /home/finder/.github_token (mode 600), read by the wrappers into GITHUB_TOKEN, never printed or
  committed. It was deleted from the box at wind-down.
- Totals (2026-10-24, seeds 20261200-20261218 ordinary, 20261400-20261404 large): about 1500 projects. 27 repros in
  test/cases (npm_*/npm__* whose origin line has a seed 202612xx or 202614xx), 12 of them flips, counting lute's
  crash (the 32-word alias key), and a further dozen bullets without repros. One varies: cloudflare-os
  workshop-backend's TS5114, at every --checkers K but 1, with more K-dependent errors at K=2 and 8 (det3; the
  rule that came of it: limits always rerun).
- Redoing this on a new box: (1) apt install bubblewrap meson ninja-build clang lld valgrind; (2) `useradd -m
  finder`, copy the deploy key and known_hosts into ~finder/.ssh, set git user.name/email; (3) as finder, `git clone
  git@github.com:wmww/tttt.git`, copy thirdparty/node over (or run ensure-sources.sh's node/bun fetchers),
  `scripts/fasttest.sh -f '^nothing$'` (release build plus ensure-typescript.sh, about 4 minutes on 4 cores); (4) the
  sandbox check above; (5) the two wrappers, then one tmux session per seed. run-seed.sh (ordinary; run-large.sh is
  the same with `sample --large --seed $S -n 60` and `setup --large --seed $S -N 50 --per-repo 3`):

      #!/bin/bash
      # run-seed.sh SEED [WAITFILE]
      S=$1
      cd /home/finder/tttt || exit 1
      [ -r /home/finder/.github_token ] && export GITHUB_TOKEN=$(cat /home/finder/.github_token)
      if [ -n "$2" ]; then until [ -e "$2" ]; do sleep 10; done; fi
      [ -e test/npmsample/$S.candidates.jsonl ] || scripts/npmsample.py sample --seed $S || exit 1
      touch /home/finder/sampled-$S
      scripts/npmsample.py setup --seed $S -N 50 --prefer-large || exit 1
      git pull -q --rebase || echo PULL-FAILED
      scripts/npmsample.py compare --seed $S
      echo DONE-$S

  A second seed's wrapper takes the first's `sampled-S` file as WAITFILE, so only one sampler paces the registry
  from the host. Reducers launched together race on `ninja -C release` (one failed with CalledProcessError), so
  build once before starting several.

## The second box (2026-10-23/24, seeds 20261100-20261117; bugs in agents/bugs/npmsample_box2.md from 20261105)
Seeds 20261116 and 20261117 were abandoned in setup (clones and installs failed with the network down): their candidates are committed, no records; rerun them.
A coordinator plus two Sonnet finders in worktrees; the coordinator samples (one pacer per host), lands each finder's
commit by cherry-pick onto main and pushes. ~860 projects in 18 seeds gave 16 repros (6 flips), one unreduced flip
(soldy's TS2883 in an npm-linked workspace, its clone kept in thirdparty/npmsample/20261115) and one K-dependence
(discord-api-types' base cycle, fixed by c16033912). What it learned:
- **Check the tsgo pin first.** This box's thirdparty/typescript was a stale nightly (fed0bf24, `tsc --version` says
  7.1.0-dev, the pin says 7.1.0-beta); seeds 20261100-20261102 were first compared against it and 20261101's records
  still are. npmsample.py now runs ensure-typescript.sh before setup and compare (it refuses a stale checkout).
- A new box needs thirdparty/node (node 24.21.0, bun 1.4.3): ensure-sources.sh's ensure_node/ensure_bun, run alone.
- A fresh worktree has no release build; compare needs one (`meson setup release --buildtype release`).
- Land by `git cherry-pick <finder commit>`, not land.sh: a finder that didn't reset after its last landing replays
  landed commits. The two boxes' appends to the bug files conflict on nearly every landing (keep both sides; the
  other box also edits existing bullets, e.g. marking them fixed: keep its edit). repros.tsv merges as a union now.
- Never wait on `pgrep -f PATTERN` from a shell whose own command line contains PATTERN: it matches itself forever
  (two queued samples sat idle for an hour this way). Chain samples sequentially in one shell instead.
- A finder costs ~60k tokens for a clean seed and 90-130k with repros; retire it past ~100k of context and start a
  fresh one rather than resuming.
