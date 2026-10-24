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
