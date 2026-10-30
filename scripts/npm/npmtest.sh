#!/usr/bin/env bash
# Tests scripts/npmpack.sh's BINDIR-mode packages without publishing anything: a throwaway local registry
# (scripts/npm/registry.js) serves OUTDIR's tarballs, and npm, pnpm 10, Yarn 1 and Yarn 4 (installed into the
# scratch directory when not on PATH) install tttt-ts from it with HOME pointed into the scratch directory, so no
# package manager cache or config outside it is touched. The linux-x64 package must hold a real tttt (Linux x86-64
# host); the other platforms may be stand-ins (shell scripts printing their platform, NPMPACK_FAKE=1).
#
#   scripts/npm/npmtest.sh OUTDIR [SCRATCH]    # one PASS/FAIL line per check, exit 1 on any failure
#
# Checks: per package manager and flag set (scripts blocked/approved, --ignore-scripts, optional dependencies
# omitted, global, npx, Yarn PnP) whether bin/tttt ended up the native binary or the launcher (expected per case),
# `tttt --version`, exit codes 0 and 2 on real files, and for the native ones that no node process starts (strace);
# through the launcher: stdin/stdout, exit codes, a signal death, a forwarded kill, a version mismatch, an
# unsupported platform, musl, the other platforms' packages (npm --os/--cpu, platform faked in node); the launcher's
# overhead on `tttt --version` (median of 20, hyperfine).
set -uo pipefail
if test $# -lt 1; then sed -n '2,16p' "$0"; exit 1; fi
OUTDIR=$(realpath "$1")
HERE=$(realpath "$(dirname "${BASH_SOURCE[0]}")")
S=$(realpath -m "${2:-$(mktemp -d /tmp/npmtest-XXXXXX)}")
mkdir -p "$S"
VERSION=$(node -p "require('$OUTDIR/tttt-ts/package.json').version")
export HOME=$S/home npm_config_update_notifier=false npm_config_fund=false npm_config_audit=false
rm -rf "$HOME" "$S/p"; mkdir -p "$HOME"
FAILS=0
pass() { echo "PASS $*"; }
fail() { echo "FAIL $*"; FAILS=$((FAILS + 1)); }

# The package managers.
TOOLS=$S/tools
if ! test -x "$TOOLS/node_modules/.bin/pnpm"; then
    npm i --prefix "$TOOLS" pnpm@10 yarn@1 @yarnpkg/cli-dist@4 >/dev/null 2>&1 || { echo "can't install pnpm/yarn"; exit 1; }
fi
PNPM=$TOOLS/node_modules/.bin/pnpm
YARN1="node $TOOLS/node_modules/yarn/bin/yarn.js"
YARN4="node $TOOLS/node_modules/@yarnpkg/cli-dist/bin/yarn.js"

PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')
REG=http://localhost:$PORT/
node "$HERE/registry.js" "$OUTDIR" "$PORT" > "$S/registry.log" 2>&1 &
REGPID=$!
trap 'kill $REGPID 2>/dev/null' EXIT
for _ in $(seq 50); do grep -q registry "$S/registry.log" && break; sleep 0.1; done
export npm_config_registry=$REG

PLAIN='{"name": "p", "version": "1.0.0"}'
# A fresh project directory with package.json $2 (default: no dependencies).
project() {
    rm -rf "$S/p/$1"; mkdir -p "$S/p/$1"
    echo "${2:-$PLAIN}" > "$S/p/$1/package.json"
    printf 'export const x: number = "not a number";\n' > "$S/p/$1/bad.ts"
    printf 'export const x: number = 1;\n' > "$S/p/$1/good.ts"
    echo "$S/p/$1"
}
# What a bin path runs: native (an ELF/the binary itself) or launcher (the JS file).
kind() { if head -c 4 "$1" | grep -q ELF; then echo native; else echo launcher; fi; }
# Checks the installed tttt at $2 (case name $1, expected kind $3, how to run it: the rest, default $2).
check_install() {
    local name=$1 bin=$2 want=$3; shift 3
    local run=("$@"); test ${#run[@]} -gt 0 || run=("$bin")
    local got; got=$(kind "$(realpath "$bin")")
    test "$got" = "$want" && pass "$name: bin is the $got" || fail "$name: bin is the $got, expected the $want"
    local v; v=$("${run[@]}" --version 2>&1)
    test "$v" = "$TS_VERSION" && pass "$name: --version" || fail "$name: --version printed '$v'"
    "${run[@]}" good.ts >/dev/null 2>&1; local e0=$?
    "${run[@]}" bad.ts >/dev/null 2>&1; local e2=$?
    test "$e0/$e2" = 0/2 && pass "$name: exit 0 and 2" || fail "$name: exit $e0 on good.ts, $e2 on bad.ts"
    if test "$want" = native && command -v strace >/dev/null; then
        strace -f -qq -e trace=execve -o "$S/strace.log" "$bin" --version >/dev/null 2>&1
        if grep -qE 'execve\("[^"]*/node"' "$S/strace.log"; then fail "$name: a node process started"; else pass "$name: no node process ($(grep -c execve "$S/strace.log") execve)"; fi
    fi
}
TS_VERSION=$("$OUTDIR/tttt-ts-linux-x64/bin/tttt" --version)

echo "== npm $(npm --version)"
P=$(project npm-default); (cd "$P" && npm i "tttt-ts@$VERSION" >"$S/log" 2>&1) || fail "npm install: $(tail -3 "$S/log")"
# npm 12 blocks dependencies' install scripts by default: the launcher stays.
NPMMAJOR=$(npm --version | cut -d. -f1)
(cd "$P" && check_install npm-default node_modules/.bin/tttt "$([ "$NPMMAJOR" -ge 12 ] && echo launcher || echo native)")
(cd "$P" && v=$(npx tttt --version 2>/dev/null) && test "$v" = "$TS_VERSION") && pass "npm-default: npx tttt --version" || fail "npm-default: npx tttt --version"
APPROVED="{\"name\": \"p\", \"version\": \"1.0.0\", \"allowScripts\": {\"tttt-ts\": true}}"
P=$(project npm-approved "$APPROVED"); (cd "$P" && npm i "tttt-ts@$VERSION" >"$S/log" 2>&1) || fail "npm install: $(tail -3 "$S/log")"
(cd "$P" && check_install npm-approved node_modules/.bin/tttt native)
# A re-run of the postinstall over the binary (npm rebuild) keeps it working.
(cd "$P" && npm rebuild tttt-ts >/dev/null 2>&1 && check_install npm-rebuild node_modules/.bin/tttt native)
P=$(project npm-ignore-scripts "$APPROVED"); (cd "$P" && npm i --ignore-scripts "tttt-ts@$VERSION" >"$S/log" 2>&1) || fail "npm install: $(tail -3 "$S/log")"
(cd "$P" && check_install npm-ignore-scripts node_modules/.bin/tttt launcher)
for flag in --omit=optional --no-optional; do
    P=$(project "npm$flag" "$APPROVED"); (cd "$P" && npm i --foreground-scripts $flag "tttt-ts@$VERSION" >"$S/log" 2>&1) || fail "npm install $flag: $(tail -3 "$S/log")"
    test -e "$P/node_modules/tttt-ts-linux-x64" && fail "npm $flag: the platform package was installed anyway"
    grep -q 'tttt-ts-linux-x64 is not installed' "$S/log" && pass "npm $flag: postinstall notes the missing package" || fail "npm $flag: no postinstall note"
    out=$(cd "$P" && node_modules/.bin/tttt --version 2>&1); e=$?
    test $e = 1 && grep -q 'tttt-ts-linux-x64 is not installed' <<<"$out" && grep -q -- '--omit=optional' <<<"$out" \
        && pass "npm $flag: the launcher explains (exit 1)" || fail "npm $flag: exit $e: $out"
done
G=$S/global; rm -rf "$G"
npm i -g --prefix "$G" --allow-scripts=tttt-ts "tttt-ts@$VERSION" >"$S/log" 2>&1 || fail "npm -g: $(tail -3 "$S/log")"
(cd "$(project npm-global)" && check_install npm-global "$G/bin/tttt" native)
(cd "$(project npx)" && v=$(npx -y "tttt-ts@$VERSION" --version 2>/dev/null) && test "$v" = "$TS_VERSION") && pass "npx -y tttt-ts: --version" || fail "npx -y tttt-ts --version"

echo "== the launcher (npm-default's)"
L=$S/p/npm-default/node_modules/.bin/tttt
PROBE=$S/probe; cat > "$PROBE" <<'EOF'
#!/bin/sh
case "$1" in
    cat) cat ;;
    exit) exit "$2" ;;
    kill) kill -"$2" $$ ;;
    sleep) echo $$ > "$2"; exec sleep 30 ;;
    args) shift; for a in "$@"; do echo "[$a]"; done ;;
esac
EOF
chmod 755 "$PROBE"
out=$(printf 'line one\nline two\n' | TTTT_BINARY=$PROBE "$L" cat)
test "$out" = $'line one\nline two' && pass "launcher: stdin to stdout" || fail "launcher: stdin/stdout gave '$out'"
out=$(TTTT_BINARY=$PROBE "$L" args 'a b' '' '"q"' '$HOME')
test "$out" = $'[a b]\n[]\n["q"]\n[$HOME]' && pass "launcher: arguments verbatim" || fail "launcher: arguments gave '$out'"
TTTT_BINARY=$PROBE "$L" exit 7; e=$?; test $e = 7 && pass "launcher: exit 7" || fail "launcher: exit $e for 7"
{ (TTTT_BINARY=$PROBE "$L" kill TERM); e=$?; } 2>/dev/null; test $e = 143 && pass "launcher: child killed by SIGTERM -> 143" || fail "launcher: $e for a SIGTERM death"
{ (TTTT_BINARY=$PROBE "$L" kill SEGV); e=$?; } 2>/dev/null; test $e = 139 && pass "launcher: child SIGSEGV -> 139" || fail "launcher: $e for a SIGSEGV death"
rm -f "$S/child.pid"
TTTT_BINARY=$PROBE "$L" sleep "$S/child.pid" & LPID=$!
for _ in $(seq 50); do test -s "$S/child.pid" && break; sleep 0.1; done
kill -TERM $LPID; wait $LPID; e=$?
CPID=$(cat "$S/child.pid"); sleep 0.2
if test $e = 143 && ! kill -0 "$CPID" 2>/dev/null; then pass "launcher: SIGTERM to the launcher reaches the child (143)"; else fail "launcher: kill gave $e, child $(kill -0 "$CPID" 2>/dev/null && echo alive || echo gone)"; kill "$CPID" 2>/dev/null; fi
FAKE=$S/fake.js; cat > "$FAKE" <<'EOF'
const o = JSON.parse(process.env.NPMTEST_FAKE || '{}');
if (o.platform) Object.defineProperty(process, 'platform', { value: o.platform });
if (o.arch) Object.defineProperty(process, 'arch', { value: o.arch });
if (o.musl) process.report.getReport = () => ({ header: {} });
EOF
fake() { NODE_OPTIONS="--require $FAKE" NPMTEST_FAKE=$1 "${@:2}"; }
out=$(fake '{"arch":"riscv64"}' "$L" --version 2>&1); e=$?
test $e = 1 && grep -q 'linux riscv64' <<<"$out" && grep -q 'Supported: Linux x64 and arm64' <<<"$out" && grep -q 'built from source' <<<"$out" \
    && pass "launcher: unsupported platform explained" || fail "launcher: unsupported platform: exit $e: $out"
out=$(fake '{"platform":"darwin","arch":"x64"}' "$L" --version 2>&1); e=$?
test $e = 1 && grep -q 'darwin x64' <<<"$out" && pass "launcher: Intel macOS explained" || fail "launcher: Intel macOS: exit $e: $out"
out=$(cd "$S/p/npm--omit=optional" && fake '{"musl":true}' node_modules/.bin/tttt --version 2>&1); e=$?
test $e = 1 && grep -q 'musl' <<<"$out" && pass "launcher: musl explained (glibc package skipped by npm)" || fail "launcher: musl: exit $e: $out"
cp -r "$S/p/npm-default" "$S/p/npm-mismatch"
sed -i "s/\"version\": \"$VERSION\"/\"version\": \"0.0.1\"/" "$S/p/npm-mismatch/node_modules/tttt-ts-linux-x64/package.json"
out=$("$S/p/npm-mismatch/node_modules/.bin/tttt" --version 2>&1); e=$?
test $e = 1 && grep -q 'version mismatch' <<<"$out" && pass "launcher: version mismatch explained" || fail "launcher: mismatch: exit $e: $out"
for plat in linux-arm64 darwin-arm64 win32-x64; do
    os=${plat%-*}; cpu=${plat#*-}
    P=$(project "npm-$plat"); (cd "$P" && npm i --os="$os" --cpu="$cpu" "tttt-ts@$VERSION" >"$S/log" 2>&1) || fail "npm --os=$os: $(tail -3 "$S/log")"
    ls "$P/node_modules" | grep -q "^tttt-ts-$plat$" && test "$(ls "$P/node_modules" | grep -c '^tttt-ts-')" = 1 \
        && pass "npm --os=$os --cpu=$cpu: installs only tttt-ts-$plat" || fail "npm --os=$os --cpu=$cpu: installed $(ls "$P/node_modules" | tr '\n' ' ')"
    # (A stand-in prints its platform; a real release binary runs under binfmt emulation where the host has it (qemu
    # with the cross toolchain's sysroot for arm64, wine for win32: its first run's chatter goes to stderr) and prints
    # its version; a darwin one fails to start, its path in the launcher's message)
    out=$(cd "$P" && QEMU_LD_PREFIX=${QEMU_LD_PREFIX:-/usr/aarch64-linux-gnu} WINEDEBUG=-all \
        fake "{\"platform\":\"$os\",\"arch\":\"$cpu\"}" node node_modules/tttt-ts/bin/tttt --version 2>/dev/null | tr -d '\r')
    test -n "$out" || out=$(cd "$P" && fake "{\"platform\":\"$os\",\"arch\":\"$cpu\"}" node node_modules/tttt-ts/bin/tttt --version 2>&1)
    { grep -q "$plat" <<<"$out" || test "$out" = "$TS_VERSION"; } \
        && pass "launcher as $plat: runs tttt-ts-$plat's binary ($out)" || fail "launcher as $plat: $out"
done

echo "== pnpm $($PNPM --version)"
P=$(project pnpm-default); (cd "$P" && $PNPM add "tttt-ts@$VERSION" >"$S/log" 2>&1) || fail "pnpm add: $(tail -3 "$S/log")"
(cd "$P" && check_install pnpm-default node_modules/.bin/tttt launcher)
(cd "$P" && v=$($PNPM exec tttt --version 2>/dev/null) && test "$v" = "$TS_VERSION") && pass "pnpm-default: pnpm exec tttt" || fail "pnpm-default: pnpm exec tttt"
# Builds allowed: the postinstall runs but leaves pnpm's install alone (its .bin shim runs `node bin/tttt`).
P=$(project pnpm-approved '{"name": "p", "version": "1.0.0", "pnpm": {"onlyBuiltDependencies": ["tttt-ts"]}}')
(cd "$P" && $PNPM add "tttt-ts@$VERSION" >"$S/log" 2>&1) || fail "pnpm add: $(tail -3 "$S/log")"
grep -q 'Ignored build scripts' "$S/log" && fail "pnpm-approved: build scripts were still ignored"
(cd "$P" && check_install pnpm-approved node_modules/.bin/tttt launcher)
P=$(project pnpm-ignore-scripts); (cd "$P" && $PNPM add --ignore-scripts "tttt-ts@$VERSION" >"$S/log" 2>&1) || fail "pnpm add: $(tail -3 "$S/log")"
(cd "$P" && check_install pnpm-ignore-scripts node_modules/.bin/tttt launcher)
P=$(project pnpm-no-optional); (cd "$P" && $PNPM add "tttt-ts@$VERSION" >/dev/null 2>&1 && rm -rf node_modules && $PNPM install --no-optional >"$S/log" 2>&1) || fail "pnpm --no-optional: $(tail -3 "$S/log")"
out=$(cd "$P" && node_modules/.bin/tttt --version 2>&1); e=$?
test $e = 1 && grep -q 'is not installed' <<<"$out" && pass "pnpm --no-optional: the launcher explains (exit 1)" || fail "pnpm --no-optional: exit $e: $out"

echo "== yarn $($YARN1 --version)"
yarn1() { $YARN1 --cache-folder "$S/yarn1-cache" --registry "$REG" --no-progress --non-interactive "$@"; }
P=$(project yarn1-default); (cd "$P" && yarn1 add "tttt-ts@$VERSION" >"$S/log" 2>&1) || fail "yarn 1 add: $(tail -3 "$S/log")"
(cd "$P" && check_install yarn1-default node_modules/.bin/tttt native)
(cd "$P" && v=$(yarn1 -s tttt --version 2>/dev/null) && test "$v" = "$TS_VERSION") && pass "yarn1-default: yarn tttt" || fail "yarn1-default: yarn tttt"
P=$(project yarn1-ignore-scripts); (cd "$P" && yarn1 add --ignore-scripts "tttt-ts@$VERSION" >"$S/log" 2>&1) || fail "yarn 1 add: $(tail -3 "$S/log")"
(cd "$P" && check_install yarn1-ignore-scripts node_modules/.bin/tttt launcher)
P=$(project yarn1-ignore-optional); (cd "$P" && yarn1 add --ignore-optional "tttt-ts@$VERSION" >"$S/log" 2>&1) || fail "yarn 1 add: $(tail -3 "$S/log")"
out=$(cd "$P" && node_modules/.bin/tttt --version 2>&1); e=$?
test $e = 1 && grep -q 'is not installed' <<<"$out" && pass "yarn1 --ignore-optional: the launcher explains (exit 1)" || fail "yarn1 --ignore-optional: exit $e: $out"

echo "== yarn $($YARN4 --version)"
for linker in node-modules pnp; do
    P=$(project "yarn4-$linker")
    printf 'nodeLinker: %s\nnpmRegistryServer: "%s"\nunsafeHttpWhitelist: ["localhost"]\nenableGlobalCache: false\nenableTelemetry: false\n' "$linker" "${REG%/}" > "$P/.yarnrc.yml"
    touch "$P/yarn.lock"
    (cd "$P" && $YARN4 add "tttt-ts@$VERSION" >"$S/log" 2>&1) || fail "yarn 4 add ($linker): $(tail -3 "$S/log")"
    if test $linker = node-modules; then (cd "$P" && check_install yarn4-node-modules node_modules/.bin/tttt launcher $YARN4 tttt); fi
    (cd "$P" && v=$($YARN4 tttt --version 2>/dev/null) && test "$v" = "$TS_VERSION") && pass "yarn4-$linker: yarn tttt --version" || fail "yarn4-$linker: yarn tttt --version"
    (cd "$P" && $YARN4 tttt bad.ts >/dev/null 2>&1; test $? = 2) && pass "yarn4-$linker: yarn tttt bad.ts exits 2" || fail "yarn4-$linker: exit code"
done

echo "== overhead (tttt --version, median of 20)"
NATIVE=$S/p/npm-approved/node_modules/.bin/tttt
hyperfine -N -w 3 -r 20 --export-json "$S/overhead.json" "$NATIVE --version" "$L --version" >/dev/null 2>&1
python3 -I -c '
import json, sys
r = json.load(open(sys.argv[1]))["results"]
n, l = (sorted(m["time_wall_clock"]["value"] for m in x["measurements"])[len(x["measurements"]) // 2] * 1000 if "measurements" in x else x["median"] * 1000 for x in r)
print(f"native {n:.1f} ms, launcher {l:.1f} ms: +{l - n:.1f} ms")' "$S/overhead.json"

echo "$FAILS failures (scratch: $S)"
test $FAILS = 0
