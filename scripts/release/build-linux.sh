#!/usr/bin/env bash
# The Linux release binary for this machine's architecture (x86-64 or aarch64), into OUTDIR/linux-{x64,arm64}/:
# `tttt` and `build-info.txt`. agents/notes/release.md says why each step is what it is.
#
#   scripts/release/build-linux.sh OUTDIR [--container-gcc] [--no-smoke]
#
# 1. An AlmaLinux 8 image (Containerfile.linux: glibc 2.28, gcc-toolset-14), exported as a sysroot (relbuild/sysroot).
# 2. LTO + PGO (scripts/pgobuild.sh, trained on its corpora) with the host's GCC against that sysroot: the binary
#    binds nothing newer than GLIBC_2.28 (RHEL 8+, Debian 10+, Ubuntu 20.04+), libgcc linked in (-static-libgcc), and
#    the compiler stays the one the benchmarks use. --container-gcc: the container's GCC 14 builds it instead (for a
#    host without a usable GCC: +1.6% instructions on x86-64 in v3, release.md). Into relbuild/pgo.
# 3. BOLT on the host (scripts/boltbuild.sh with PGO_BIN; the host's llvm-bolt, its training runs on the host's
#    glibc). Into relbuild/bolt.
# 4. Checks, any failure fatal: the dynamic symbols' glibc versions <= 2.28 and NEEDED only libc/libm/libpthread; the
#    output (stdout and exit code, `--noEmit --pretty false -p`) byte-identical to a plain host release build
#    (relbuild/plain) on the 14 instrcount corpora at the defaults and at `--checkers 1 --threads 1`; smoke runs in
#    Debian 10 and Ubuntu 26.04 containers (a tiny project with an error, and zod, from an unrelated directory)
#    against the host's output.
#
# The sources (src, thirdparty, meson.build) must be committed: build-info.txt names the commit. Needs podman (or
# CONTAINER=docker), gcc (CC=...), llvm-bolt (BOLT=...), the corpora (thirdparty/ensure-corpora.sh) and network for
# the images.
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )/../.."
OUT=${1:?usage: $0 OUTDIR [--container-gcc] [--no-smoke]}
shift
SMOKE=1
CONTAINER_GCC=
for A in "$@"; do
    case "$A" in
        --no-smoke) SMOKE= ;;
        --container-gcc) CONTAINER_GCC=1 ;;
        *) echo "build-linux: unknown option $A" >&2; exit 2 ;;
    esac
done
CONTAINER=${CONTAINER:-podman}
case "$(uname -m)" in
    x86_64) ARCH=x64 ;;
    aarch64) ARCH=arm64 ;;
    *) echo "build-linux: no release for $(uname -m)" >&2; exit 1 ;;
esac
FLOOR=2.28
COMMIT=$(git rev-parse HEAD)
if ! git diff --quiet HEAD -- src thirdparty meson.build || test -n "$(git ls-files --others --exclude-standard src)"; then
    echo "build-linux: src, thirdparty or meson.build differ from HEAD ($COMMIT): commit first" >&2
    exit 1
fi
CORPORA=(
    thirdparty/corpora/TypeScript6/src/compiler
    thirdparty/corpora/type-fest
    thirdparty/corpora/valibot/library/tsconfig.corpus.json
    thirdparty/corpora/bulletproof-react/apps/react-vite/tsconfig.corpus.json
    thirdparty/corpora/nestjs-boilerplate/tsconfig.corpus.json
    thirdparty/corpora/webpack
    thirdparty/corpora/zod/packages/zod/tsconfig.test.json
    thirdparty/corpora/xstate/packages/core/tsconfig.corpus.json
    thirdparty/corpora/drizzle/drizzle-orm/tsconfig.corpus.json
    thirdparty/corpora/vue/tsconfig.corpus.json
    thirdparty/corpora/effect/packages/effect/tsconfig.corpus.json
    thirdparty/corpora/vscode/src/tsconfig.corpus.json
    thirdparty/corpora/excalidraw/tsconfig.corpus.json
    thirdparty/corpora/tanstack-router/tsconfig.corpus.json
)
for P in "${CORPORA[@]}"; do test -e "$P" || { echo "build-linux: $P missing (thirdparty/ensure-corpora.sh)" >&2; exit 1; }; done
B=relbuild
# (Fresh build directories: a meson directory configured by the container's meson doesn't load in the host's)
rm -rf $B/pgo $B/bolt
mkdir -p $B
REPO=$(pwd -P)
CORPORA_DIR=$(realpath thirdparty/corpora)
WORK=$(mktemp -d /tmp/build-linux.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

echo "== image tttt-release-linux ($ARCH)" >&2
$CONTAINER build -q -t tttt-release-linux -f scripts/release/Containerfile.linux scripts/release >&2

# (GCC 14's LTO + PGO: a -Wstringop-overflow false positive, which werror makes fatal, at load_pool.c's
# pool_find_or_add(): atomic_init() on c_new()'s result, "likely at address zero" (malloc's failure path). A warning
# flag, no codegen change)
NOWARN=-Wno-stringop-overflow
GLIBC_VERSION=$($CONTAINER run --rm tttt-release-linux ldd --version | head -1)
if test -n "$CONTAINER_GCC"; then
    echo "== LTO + PGO in the container" >&2
    # (The repo at its own path, so the paths the profile and the binary record are the host's; the corpora
    # read-only. Rootless podman's root is this user, so the build's files are ours)
    $CONTAINER run --rm -v "$REPO:$REPO" -v "$CORPORA_DIR:$CORPORA_DIR:ro" -w "$REPO" \
        -e PGO_DIR=$B/pgo -e "PGO_CFLAGS=$NOWARN" -e "PGO_LDFLAGS=-static-libgcc $NOWARN" \
        tttt-release-linux scripts/pgobuild.sh --force >&2
    CC_VERSION="$($CONTAINER run --rm tttt-release-linux gcc --version | head -1) (in the container)"
else
    echo "== sysroot" >&2
    SR=$REPO/$B/sysroot
    test ! -e "$SR" || chmod -R u+w "$SR"
    rm -rf "$SR"
    mkdir -p "$SR"
    CID=$($CONTAINER create tttt-release-linux)
    # (Headers, crt files, libc's link scripts and libraries)
    $CONTAINER export "$CID" | tar -x -C "$SR" lib lib64 usr/include usr/lib usr/lib64
    $CONTAINER rm -f "$CID" > /dev/null
    test -f "$SR/usr/lib64/libc.so" && test -f "$SR/usr/include/stdio.h" || { echo "build-linux: no glibc in $SR" >&2; exit 1; }
    # The host GCC finds its own crt and library directories by paths relative to its install, which --sysroot
    # doesn't prefix (Arch's /usr/lib, Ubuntu's multiarch): -B and -L put the sysroot's first
    HOSTCC=${CC:-gcc}
    SRCC="$HOSTCC --sysroot=$SR -B$SR/usr/lib64 -L$SR/usr/lib64 -L$SR/lib64"
    # The host's libgcov.a (in the PGO-instrumented binary only) was built against the host's glibc and calls C23's
    # strtol (glibc 2.38): an archive member that supplies it, linked only where something asks for it, so never
    # into the release binary (whose own calls bind 2.28's strtol)
    printf '#include <stdlib.h>\nlong __isoc23_strtol(char const* s, char** e, int b) { return strtol(s, e, b); }\n' > "$WORK/gcovshim.c"
    $SRCC -O2 -c "$WORK/gcovshim.c" -o "$WORK/gcovshim.o"
    rm -f $B/libgcovshim.a
    ar rcs $B/libgcovshim.a "$WORK/gcovshim.o"
    echo "== LTO + PGO with the host's GCC against the sysroot" >&2
    CC="$SRCC" PGO_DIR=$B/pgo PGO_CFLAGS="$NOWARN" PGO_LDFLAGS="-static-libgcc $NOWARN $REPO/$B/libgcovshim.a" \
        scripts/pgobuild.sh --force >&2
    CC_VERSION="$($HOSTCC --version | head -1) (the host's, --sysroot)"
fi
test "$(nm $B/pgo/tttt | grep -c __isoc23 || true)" = 0 || { echo "build-linux: $B/pgo/tttt defines or calls __isoc23_*" >&2; exit 1; }

echo "== BOLT on the host" >&2
PGO_BIN=$B/pgo/tttt BOLT_DIR=$B/bolt scripts/boltbuild.sh --force >&2
BIN=$B/bolt/tttt

echo "== checks" >&2
FAIL=0
MAXV=$(objdump -T "$BIN" | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -Vu | tail -1)
if test "$(printf '%s\n%s\n' "$MAXV" "$FLOOR" | sort -V | tail -1)" != "$FLOOR"; then
    echo "FAIL glibc: binds GLIBC_$MAXV:" >&2
    objdump -T "$BIN" | grep -E "GLIBC_($(echo "$MAXV" | sed 's/\./\\./g'))" >&2
    FAIL=1
else
    echo "PASS glibc: newest symbol version GLIBC_$MAXV (floor $FLOOR)" >&2
fi
# (The dynamic loader is NEEDED on aarch64 (glibc 2.28's libc.so link script names it outside AS_NEEDED): always there)
NEEDED=$(readelf -d "$BIN" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | grep -v '^ld-linux' | sort | tr '\n' ' ')
case "$NEEDED" in
    # (libpthread: a library of its own before glibc 2.34, a stub after)
    "libc.so.6 libm.so.6 "|"libc.so.6 libm.so.6 libpthread.so.0 ") echo "PASS needed: $NEEDED" >&2 ;;
    *) echo "FAIL needed: $NEEDED" >&2; FAIL=1 ;;
esac

# The reference: a plain -O3 build with the host's compiler
test -e $B/plain/build.ninja || meson setup $B/plain --buildtype release > /dev/null
ninja -C $B/plain tttt > /dev/null
run() { local bin="$1" rc=0; shift; "$bin" "$@" --noEmit --pretty false 2>&1 || rc=$?; echo "exit $rc"; }
SAME=0 DIFF=0
for P in "${CORPORA[@]}"; do
    for MODE in defaults k1; do
        ARGS=(-p "$P")
        test $MODE = defaults || ARGS+=(--checkers 1 --threads 1)
        run $B/plain/tttt "${ARGS[@]}" > "$WORK/a" || true
        run "$BIN" "${ARGS[@]}" > "$WORK/b" || true
        if cmp -s "$WORK/a" "$WORK/b"; then SAME=$((SAME + 1)); else DIFF=$((DIFF + 1)); echo "  differs: $P ($MODE)" >&2; fi
    done
done
if test $DIFF = 0; then echo "PASS identity: $SAME / $SAME runs byte-identical to $B/plain/tttt" >&2
else echo "FAIL identity: $DIFF of $((SAME + DIFF)) runs differ from $B/plain/tttt" >&2; FAIL=1; fi

SMOKES=
if test -n "$SMOKE"; then
    # A tiny project with an error, outside the repo
    mkdir -p "$WORK/tiny/src"
    printf '{ "compilerOptions": { "strict": true, "noEmit": true }, "include": ["src"] }\n' > "$WORK/tiny/tsconfig.json"
    printf 'export const n: number = "one";\nexport function f(x: string) { return x.length; }\n' > "$WORK/tiny/src/a.ts"
    ZOD=$(realpath thirdparty/corpora/zod/packages/zod/tsconfig.test.json)
    # (The same commands on the host and in each container, from the same directory)
    SMOKE_SH="cd $WORK/run; rc=0; \$T -p $WORK/tiny --pretty false || rc=\$?; echo \"exit \$rc\"; rc=0; \$T -p $ZOD --noEmit --pretty false || rc=\$?; echo \"exit \$rc\""
    mkdir -p "$WORK/run"
    T="$REPO/$B/plain/tttt" sh -c "$SMOKE_SH" > "$WORK/host" 2>&1
    for IMAGE in docker.io/library/debian:10 docker.io/library/ubuntu:26.04; do
        $CONTAINER pull -q $IMAGE > /dev/null
        $CONTAINER run --rm -v "$REPO/$BIN:/opt/tttt/tttt:ro" -v "$WORK:$WORK" -v "$CORPORA_DIR:$CORPORA_DIR:ro" -w "$WORK/run" \
            -e T=/opt/tttt/tttt $IMAGE sh -c "$SMOKE_SH" > "$WORK/smoke" 2>&1 || true
        LIBC=$($CONTAINER run --rm $IMAGE sh -c 'ldd --version 2>&1 | head -1')
        if cmp -s "$WORK/host" "$WORK/smoke"; then echo "PASS smoke $IMAGE ($LIBC)" >&2; SMOKES+="$IMAGE ($LIBC): identical to the host's; "
        else echo "FAIL smoke $IMAGE ($LIBC):" >&2; diff "$WORK/host" "$WORK/smoke" | head -20 >&2; FAIL=1; fi
    done
fi
test $FAIL = 0 || { echo "build-linux: checks failed, nothing written to $OUT" >&2; exit 1; }

D="$OUT/linux-$ARCH"
mkdir -p "$D"
cp "$BIN" "$D/tttt.new" && mv -f "$D/tttt.new" "$D/tttt"
SHA=$(sha256sum "$D/tttt" | cut -d' ' -f1)
FLAGS=$(python3 -c '
import json, subprocess, sys
o = {x["name"]: x["value"] for x in json.loads(subprocess.check_output(["meson", "introspect", "--buildoptions", sys.argv[1]]))}
print("buildtype=%s b_lto=%s b_pgo=%s c_args=%s c_link_args=%s" % (o["buildtype"], o["b_lto"], o["b_pgo"], " ".join(o["c_args"]), " ".join(o["c_link_args"])))
' $B/pgo)
cat > "$D/build-info.txt" <<EOF
tttt linux-$ARCH
commit: $COMMIT
date: $(date -u +%Y-%m-%dT%H:%M:%SZ)
sha256: $SHA
compiler: $CC_VERSION
glibc: built against $GLIBC_VERSION (AlmaLinux 8, scripts/release/Containerfile.linux); newest symbol version bound GLIBC_$MAXV; needs $NEEDED(libgcc static)
build: meson $FLAGS (scripts/pgobuild.sh: PGO trained on drizzle, zod, effect, excalidraw, webpack)
bolt: $(${BOLT:-llvm-bolt} --version | sed -n 's/.*LLVM version \(.*\)/llvm-bolt \1/p') (scripts/boltbuild.sh: ext-tsp, cdsort, split-functions, icf=all; same training set)
symbols: not stripped
checks: output byte-identical to a plain -O3 host build on the 14 instrcount corpora at the defaults and --checkers 1 --threads 1 ($SAME runs)
smoke: ${SMOKES:-skipped}
EOF
cat "$D/build-info.txt"
