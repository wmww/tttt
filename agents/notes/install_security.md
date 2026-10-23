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

## Required before installs resume (not yet implemented)
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
