#!/usr/bin/env node
// tttt-ts's `bin` (packed as bin/tttt): finds the native binary in the platform package npm installed as an optional
// dependency (tttt-ts-linux-x64, ...) and runs it with our arguments, stdio inherited, its exit code and signal passed
// on. install.js replaces this file with the binary itself where it may and can (npm with tttt-ts's script allowed,
// Yarn 1; Linux and macOS), so those installs never start node; this file is what runs otherwise: npm 12 and pnpm 10
// block dependencies' install scripts by default, --ignore-scripts, pnpm and Yarn 2+ always, and on Windows (npm's
// .cmd shim runs node). Dependency-free on purpose: it is on every run's path. TTTT_BINARY=/path runs
// that binary instead (a source build on an unsupported platform).
'use strict';
const PLATFORMS = {
  'linux x64': ['tttt-ts-linux-x64', 'bin/tttt'],
  'linux arm64': ['tttt-ts-linux-arm64', 'bin/tttt'],
  'darwin arm64': ['tttt-ts-darwin-arm64', 'bin/tttt'],
  'win32 x64': ['tttt-ts-win32-x64', 'tttt.exe'],
};
const SUPPORTED = 'Linux x64 and arm64 (glibc 2.28 or newer), macOS arm64 (13 or newer) and Windows x64 (10 or newer)';
const FROM_SOURCE = 'tttt can be built from source (https://github.com/wmww/tttt); set TTTT_BINARY to the built binary to run it through this package';

function fail(message) {
  process.stderr.write(`tttt-ts: ${message}\n`);
  process.exit(1);
}

function isMusl() {
  if (process.platform !== 'linux') return false;
  try {
    return !process.report.getReport().header.glibcVersionRuntime;
  } catch {
    return false;
  }
}

function unsupported(key) {
  const musl = isMusl() ? ' with musl libc (Alpine and similar: only glibc Linux is supported)' : '';
  fail(`no tttt binary is published for this platform (${key}${musl}).\n` +
    `Supported: ${SUPPORTED}.\n${FROM_SOURCE}.`);
}

function findBinary() {
  if (process.env.TTTT_BINARY) return process.env.TTTT_BINARY;
  const key = `${process.platform} ${process.arch}`;
  const entry = PLATFORMS[key];
  if (!entry) unsupported(key);
  const [pkg, subpath] = entry;
  let pkgJson;
  try {
    pkgJson = require.resolve(`${pkg}/package.json`);
  } catch {
    if (isMusl()) unsupported(key);
    fail(`the platform package ${pkg} is not installed.\n` +
      `npm installs it as an optional dependency of tttt-ts: it was skipped, most likely by --omit=optional,\n` +
      `--no-optional or an \`optional=false\` npmrc, or a lockfile written on another platform.\n` +
      `Reinstall without those (\`npm install tttt-ts\`), or add ${pkg} as a dependency next to tttt-ts.`);
  }
  const ours = require('../package.json').version;
  const theirs = require(pkgJson).version;
  if (theirs !== ours) {
    fail(`version mismatch: tttt-ts is ${ours} but the installed ${pkg} is ${theirs}.\n` +
      `Reinstall so both are ${ours} (\`npm install tttt-ts@${ours}\`).`);
  }
  return require('path').join(pkgJson, '..', subpath);
}

const binary = findBinary();
const child = require('child_process').spawn(binary, process.argv.slice(2), { stdio: 'inherit' });
// The child shares our terminal's process group (Windows: our console), so ^C reaches it directly; on Unix, signals
// sent to this process alone (kill PID, a supervisor) are forwarded. While the child runs we don't die of them
// ourselves: its exit decides. (On Windows child.kill would be TerminateProcess, so nothing is forwarded there.)
const win = process.platform === 'win32';
const forwarded = win ? ['SIGINT', 'SIGBREAK'] : ['SIGINT', 'SIGTERM', 'SIGHUP', 'SIGQUIT'];
const forward = (signal) => {
  if (win) return;
  try {
    child.kill(signal);
  } catch {}
};
for (const signal of forwarded) process.on(signal, forward);
child.on('error', (err) => {
  if (err.code === 'ENOENT' && isMusl()) unsupported(`${process.platform} ${process.arch}`);
  fail(`could not run ${binary}: ${err.message}`);
});
child.on('exit', (code, signal) => {
  for (const s of forwarded) process.removeListener(s, forward);
  if (signal) {
    // Die of the same signal, so the shell sees 128+N like it would for the binary itself.
    process.kill(process.pid, signal);
    return;
  }
  process.exit(code);
});
