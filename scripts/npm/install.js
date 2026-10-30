// tttt-ts's postinstall: replaces the JS launcher at bin/tttt with the platform package's native binary, so running
// `tttt` (npm's node_modules/.bin/tttt symlink points at bin/tttt) starts no node process. esbuild's trick
// (npm/esbuild/install.ts, maybeOptimizePackage). Never fails the install: whenever it can't, or shouldn't, it leaves
// the launcher, which works everywhere and explains a missing platform package at run time.
//
// Skipped: on Windows (the bin must stay a .js for npm's .cmd shim), under Yarn 2+ (it runs every bin through node,
// and PnP keeps packages in zip archives), under pnpm (its .bin entries are shell shims that `exec node` a target
// whose shebang was node when they were written), when the platform package is missing (--omit=optional) or of
// another version, or when its binary doesn't run here (too old a glibc or macOS). The binary goes in by a hard link
// (or a copy) to a temporary name next to bin/tttt, then a rename over it: atomic, and it replaces only this
// directory entry, never the file a package manager's store shares.
'use strict';
const fs = require('fs');
const path = require('path');
const childProcess = require('child_process');

const PLATFORMS = {
  'linux x64': 'tttt-ts-linux-x64',
  'linux arm64': 'tttt-ts-linux-arm64',
  'darwin arm64': 'tttt-ts-darwin-arm64',
};

function note(message) {
  process.stderr.write(`tttt-ts: ${message}\n`);
}

function main() {
  if (process.env.TTTT_BINARY) return; // the launcher runs that binary: it must stay
  const agent = process.env.npm_config_user_agent || '';
  if (/\byarn\/(?!1\.)/.test(agent) || process.versions.pnp) return;
  if (/\bpnpm\//.test(agent) || __dirname.split(path.sep).includes('.pnpm')) return;
  const pkg = PLATFORMS[`${process.platform} ${process.arch}`];
  if (!pkg) return; // Windows, or unsupported: the launcher explains
  let pkgJson;
  try {
    pkgJson = require.resolve(`${pkg}/package.json`);
  } catch {
    note(`the optional dependency ${pkg} is not installed (--omit=optional?): \`tttt\` will fail until it is`);
    return;
  }
  const version = require('./package.json').version;
  if (require(pkgJson).version !== version) return; // the launcher reports the mismatch
  const binary = path.join(path.dirname(pkgJson), 'bin', 'tttt');
  try {
    childProcess.execFileSync(binary, ['--version'], { stdio: 'pipe' });
  } catch (err) {
    note(`${binary} does not run here, keeping the launcher: ${(err.stderr || err.message).toString().trim()}`);
    return;
  }
  const target = path.join(__dirname, 'bin', 'tttt');
  const temp = path.join(__dirname, 'bin', `tttt-install-${process.pid}`);
  try {
    try {
      fs.linkSync(binary, temp);
    } catch {
      fs.copyFileSync(binary, temp);
      fs.chmodSync(temp, 0o755);
    }
    fs.renameSync(temp, target);
  } catch {
    try {
      fs.unlinkSync(temp);
    } catch {}
  }
}

try {
  main();
} catch {}
