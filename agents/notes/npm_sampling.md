# Random npm projects as a correctness measure (the human's method, 2026-10-23)

Copied from the human's /tmp/NPM-SAMPLING.md. Their run: 50 projects, a private seed, tttt built from main that day:
44 perfect (byte-identical modulo formatting), 49 agree with tsgo 7.1 on "has errors". The source sweep and the
corpora are a training set (vision.md); this is the fresh measure, repeated with a new seed each time.
`scripts/npmsample.py` implements it (worker npm, 2026-10-23); what running it taught is at the end.

# Building a random npm project set to test tttt against tsgo

How to build a set of N TypeScript projects drawn at random from npm and check tttt's correctness on them against tsgo 7.1-beta (tttt's target). The projects are random, so most will be small, old or abandoned. That's intended; don't filter by size or popularity.

You'll need tsgo 7.1-beta (`npm i typescript@7.1.0-beta`), the tttt binary, TypeScript 6 (only for `tsc --showConfig`), and npm, pnpm, yarn (via corepack) and bun.

## Workflow

1. **Name list.** The `all-the-package-names` npm package is a JSON array of every package name (about 4.6M).
2. **Sample.** Pick a fresh random seed, record it, and sample about 2,000 names uniformly. For each name, fetch `https://registry.npmjs.org/<name>/latest`. Keep packages that look like TypeScript (a `types`/`typings` field, `types` in `exports`, or `typescript` in deps/devDeps) and whose `repository` is on GitHub. Keep `repository.directory` too. Dedupe by repo. Expect about 500 candidate repos.
3. **Set up candidates in sample order.**
   - Shallow-clone the default branch.
   - Skip it if there's no tsconfig.json in the package dir or repo root, or if there are fewer than 3 non-declaration .ts files.
   - Install with the package manager the lockfile implies, never running install scripts.
   - About half of every 50 candidates make it this far.
4. **Write a `tsconfig.bench.json`** next to the config being checked. It extends that config with `noEmit: true, incremental: false`, plus whatever overrides TS7 needs (see Gotchas).
5. **Trial-run tsgo.** Accept a project if it has ≥1 file of its own (outside node_modules, per `--listFilesOnly`), ≤100 errors and ≤5 TS2307s. More than that means a monorepo that needed a build step.
   - Take the **first N accepted in sample order**. Don't cherry-pick. If you run out, set up the next batch of candidates.
   - Write `name<TAB>config path` for each accepted project to a TSV. Also keep a list of repo, package@version and config changes for each.
6. **Compare.** Run tsgo and tttt once each (`--pretty false`) on every project and compare exit code plus the sorted output. Report:
   - which projects differ, with counts of missing and extra errors, the error codes and an example line
   - the percentage where tsgo and tttt agree on whether the project is clean (no errors)
   - whether each difference changes with `--threads 1`

   Differences that are only formatting don't count as bugs. For example, tttt prints lib files as `bundled:///libs/lib.*.d.ts`.

## Gotchas

- **The npm registry rate-limits.** Above about 6 concurrent requests most lookups return 429. Use backoff. 404s are unpublished packages; skip them.
- **tsgo checks nothing if the config has any TS5xxx error.** It exits 2 almost instantly. Old configs hit this all the time. Fix it in the bench config: read the merged options with tsc 6's `--showConfig`, then override.
  - `moduleResolution` node/node10/classic (also the implicit default with a CJS `module`) becomes `bundler`, with `module: commonjs`.
  - `target` es3/es5 becomes es2015. Pin `lib` to the es5 default if it wasn't set.
  - `baseUrl` becomes `null`, with `paths` rewritten relative to the config. A bare `baseUrl` becomes `"*": ["<baseUrl>/*"]`.
  - Set removed options (`downlevelIteration`, `outFile`, `importsNotUsedAsValues`, `preserveValueImports`, `keyofStringsOnly`, `charset`, …) to `null`.
  - `composite: true` becomes `composite: false, declaration: true`. Without `declaration`, `declarationMap` fails with TS5069.
  - Unknown options (TS5023) can't be overridden. Delete them from the project's own tsconfig and note it.
  - Don't drop projects with config errors. That would bias the set toward modern configs.
- **Solution-style configs** (`"files": []` plus `references`) check zero files and look like a clean pass. Use the first app/lib/build/src reference instead.
- **Install quirks:**
  - With `CI=1`, pnpm defaults to a frozen lockfile, so the fallback needs an explicit `--no-frozen-lockfile`.
  - Yarn 2+ (`__metadata:` in yarn.lock) rejects `--ignore-scripts`; use `--mode=skip-build`.
  - Lockfiles still reference `git://github.com/…`, which GitHub dropped. Rewrite it to https via git config in the environment (`GIT_CONFIG_COUNT`/`KEY`/`VALUE`).
  - A project `.npmrc` with `${NPM_TOKEN}` fails if the variable is unset. Set it to empty.
  - A project `.npmrc` with `engine-strict=true` fails on version mismatches. Pass `--engine-strict=false`.
  - Platform-restricted packages (`"os": ["darwin"]`) need `npm install --force`.
- **Genuine failures to skip:**
  - deleted or private repos (`could not read Username`)
  - tarballs on dead private registries
  - git dependencies on deleted repos
  - a `repository.directory` that no longer exists on the default branch. Don't fall back to the repo root, because that checks a different project.
- **Safety:** never run a repo's scripts or builds.
- **Disk:** about 8–10 GB per 50 clones. Once the set is final, ask the user before deleting clones that weren't accepted.

## Running it (scripts/npmsample.py, 2026-10-23)

- **Registry pacing.** Six threads retrying on their own each got 429 for minutes (885 lookups in 20 minutes): the
  limit is a sliding window that retries extend. A shared pacer (request starts 0.2 s apart, a 429 pauses every
  thread, Retry-After honoured) does 2000 lookups in ~5 minutes.
- **Never a full checkout.** One random candidate (fontsource's font-files) was a 12 GB checkout, and 120 candidates
  took 30 GB and the disk to 19 GB free. Clones are blobless (`--filter=blob:none`) with a sparse checkout that leaves
  out fonts, images, media, archives and binaries (`SPARSE_EXCLUDE`): only checked-out blobs are fetched. A
  candidate that isn't accepted is deleted at once, and the package managers' caches after each setup run.
- **Paths.** tsgo prints real paths: the clone root must be the realpath (the worktree reaches thirdparty/npmsample
  through a symlink), or `--listFilesOnly` counts 0 own files and everything is rejected.
- **Option errors** are TS5xxx/TS6xxx diagnostics without a file or in a tsconfig: TS5097 (`.ts` import extension)
  and TS5114 are checker errors in source files and don't stop tsgo.
- First 122 candidates of seed 20261023: ~20% clone failures (private/deleted repos), ~25% no tsconfig, a few gone
  `repository.directory`s and dead git dependencies; most of the rest pass the trial.
- **Sandboxed since 2026-10-23** (agents/notes/install_security.md: two berry repos had run their own yarn bundle and
  plugins under the first runs). Every clone, install and tool run goes through scripts/installsandbox.py; the
  package manager is ours, pinned by major. A `.yarnrc.yml` strip (yarnPath, plugins, hooks) is an edit of our clone,
  logged in the setup record's `install.changes`; a pnpm-workspace.yaml with hooks/pnpmfile/configDependencies is
  refused (a skip reason). Nothing from node_modules ever runs: only tsc 6 (installed by us into .cache/ts6), tsgo and
  tttt, all in bwrap without the network.
- **Caches per batch.** The package managers' caches are thirdparty/npmsample/.cache/pm/seeds/SEED (deleted at the
  end of `setup` and by `clean`), so one finder's cleanup never deletes a store another finder's install is reading;
  corepack's verified managers (.cache/pm/corepack) and ts6 are shared and kept. The shared .cache/pm/{npm,
  pnpm-store,yarn1,bun,berry} of the first runs grew to 7 GB with four finders; they go once no finder runs the old
  script.
