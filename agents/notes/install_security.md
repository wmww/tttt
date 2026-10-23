# Installing random repositories: what can execute, and the 2026-10-23 review

The human paused the random-npm sampler (scripts/npmsample.py) and the source-sweep installer
(thirdparty/ensure-sources.sh) on 2026-10-23 for a security review of what repository-provided code could have run.
Worker secreview's findings (read-only; the full report is in the coordinator's transcript):

## Execution surface of today's install commands (both scripts, unsandboxed as user ai, network on, full host env)
- npm `ci|install [--force] --ignore-scripts ...`: nothing from the repo runs (the flag beats .npmrc; pacote skips git
  deps' prepare).
- pnpm `corepack pnpm@VER install --ignore-scripts --frozen-lockfile ...`: NO `--ignore-pnpmfile`, so a `.pnpmfile.cjs`
  or a pnpm-workspace.yaml `pnpmfile`/`configDependencies` entry would run. None existed in any installed repo.
- yarn 1 `--ignore-scripts`: a `.yarnrc` `yarn-path` would run. None existed.
- yarn berry `--mode=skip-build` + YARN_ENABLE_SCRIPTS=false: does NOT stop `.yarnrc.yml` `yarnPath` (the repo's own
  yarn bundle), `plugins:` (repo .cjs) or plugin hooks (afterInstall). Two repos ran code this way:
  HanseltimeIndustries/esm-interop-tools (upstream plugin bundles, checksums match; its afterInstall ran the repo's
  own TypeScript: `yarn info` and a JSON write inside the repo; benign) and vijay8856/react-native-find-background-
  location-vd (its bundled yarn 4.11.0 is byte-identical to the registry-verified one; benign).
- bun 1.4.3 `--ignore-scripts`: bunfig `preload` applies to run/test only. Fine.
- corepack: `packageManager` picks any published pnpm/yarn version, registry-downloaded and hash-checked.
- `tsc --showConfig`, tsgo `--listFilesOnly`, tttt-oracle: read JSON only. git clone runs no hooks.

## Host checks (09:00 to 23:30): dotfiles, ~/.ssh, autostart, systemd user units, crontab, sockets, processes,
~/.npm logs, /tmp: nothing unexpected. Verdict: nothing suspicious ran.

## Required before installs resume (implemented 2026-10-23: scripts/installsandbox.py, below)
1. Every install in bwrap: the repo and the caches rw, everything else ro, an empty tmpfs over $HOME (hides ~/.ssh,
   ~/.claude, ~/.config), a scrubbed env (`env -i` + PATH, HOME, the YARN_/COREPACK_ vars; never os.environ).
   Network stays on for fetching.
2. pnpm: `--ignore-pnpmfile --config.ignore-scripts=true`.
3. yarn berry: refuse (or rewrite in a temp copy) a .yarnrc.yml with yarnPath, plugins or afterInstall-style hooks;
   run corepack's yarn with YARN_IGNORE_PATH=1 and YARN_PLUGINS= empty. yarn 1: YARN_IGNORE_PATH=1.
4. Pin package-manager majors (allowlist) instead of trusting packageManager/engines, or keep corepack's verified
   downloads only.
5. Optionally `--registry=https://registry.npmjs.org` against .npmrc mirrors (two repos used npmmirror.com).
6. Run tsgo/showConfig in the sandbox too (no execution risk; limits writes).

## Policy (2026-10-23, the human)
Random repositories AND their dependencies are equally untrusted (a lockfile can point anywhere; the fallbacks
re-resolve); dependencies of known-good software (the pinned corpora) are a different matter. Random installs run
sandboxed, or their build/hook steps are removed, or they are reviewed before anything executes; nothing from
node_modules is executed by us (no npx, no bins, no `yarn tsc`). The installer and the sampler share one
implementation, `scripts/installsandbox.py`.
## scripts/installsandbox.py (2026-10-23, worker npm; npmsample.py uses it, cor9 ports ensure-sources.sh to it)
- `sandbox_argv()`: bwrap `--unshare-all [--share-net] --ro-bind / / --dev /dev --proc /proc --tmpfs /tmp --tmpfs /run
  --tmpfs $HOME`, then the repository and the cache root `--bind`, node/bun (and a corepack-fetched manager already
  in the cache) `--ro-bind`. `clean_env()`: PATH (thirdparty/node first), HOME (the empty tmpfs), LANG, TERM, plus the
  package managers' settings; never os.environ. `run()` is the same for our own tools, without the network.
- Managers pinned by major from `ALLOWED` (pnpm 7.33.7/8.15.9/9.15.9/10.30.3/11.24.0, yarn 1.22.22, berry 4.11.0,
  bun 1.4.3, node's npm), always `corepack NAME@VERSION`; packageManager only picks the major. pnpm also needs
  `--config.manage-package-manager-versions=false --config.pm-on-fail=ignore` (9.7+ downloads and runs
  packageManager's version itself; 11 refuses another one).
- `.yarnrc.yml` keys that load or run code (yarnPath, plugins, afterInstall, *Hook, *Command) are stripped from the
  clone before installing (and with plugins, every key berry doesn't know: plugin settings fail the install), logged
  in the record's `changes`; `strip=False` refuses instead. A pnpm-workspace.yaml with configDependencies, pnpmfile,
  globalPnpmfile or hooks is refused. YARN_IGNORE_PATH=1 and YARN_PLUGINS= besides.
- Registry forced to registry.npmjs.org (--registry, NPM_CONFIG_REGISTRY, YARN_NPM_REGISTRY_SERVER,
  BUN_CONFIG_REGISTRY).
- Verified on three clones of seed 20261023: 0009 (pnpm 11) installs; 0105 (berry) installs with yarnPath stripped;
  0021 (berry) installs with afterInstall, plugins and correctFormattingLogs stripped, and its afterInstall's output
  file untouched. In the sandbox $HOME and /run are empty, /tmp private, /etc read-only.
- **Separate cache roots per trust class**: the sampler's (thirdparty/npmsample/.cache/pm) must not be the sweep
  repos' (ensure-sources.sh's), so a poisoned tarball fetched for a random repo never feeds a sweep repo's install.
- Residual risks: the network namespace is shared, so the host's abstract unix sockets are reachable; lockfile
  `resolved` URLs can point anywhere (fetched, never executed); the cache root is writable by every install of its
  class (a corepack-fetched manager becomes read-only once fetched, so one install can't alter the next one's
  manager).
