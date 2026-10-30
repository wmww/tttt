#!/usr/bin/env bash
# A release binary for Windows x86-64 (agents/notes/release.md "Windows"): MSYS2 CLANG64 clang LTO + PGO, static
# (scripts/pgobuild.sh in dist-pgo/, trained like the fast build), checked and copied to OUTDIR with a
# build-info.txt and imports.txt (every imported DLL function, for the minimum-Windows audit). Run in an MSYS2 CLANG64
# shell from a clean checkout of the release commit (src, thirdparty, meson.build unmodified: refused otherwise),
# with the PGO training corpora in place.
#
#   scripts/release/build-windows.sh OUTDIR [COMMIT]   # OUTDIR/tttt.exe, build-info.txt, imports.txt
#
# COMMIT (default HEAD) is the release commit: HEAD may carry later script commits, but its src, thirdparty and
# meson.build must equal COMMIT's. Also builds dist-release/ (plain -O3) and requires the PGO binary's output to be
# byte-identical to it on the training corpora and TypeScript6 at the defaults and at one checker. By hand (notes):
# the suite against Linux, scripts/wincompare.py, running it from cmd.exe outside MSYS2.
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )/../.."
OUT=${1:?usage: $0 OUTDIR [COMMIT]}
# shellcheck source=scripts/release/lib.sh
. scripts/release/lib.sh
case "$(uname -s)/${MSYSTEM:-}" in MINGW64*/CLANG64|MINGW*/CLANG64) ;; *) fail "run this in an MSYS2 CLANG64 shell" ;; esac
COMMIT=$(release_commit "${2:-HEAD}")
BIN=$(PGO_DIR=dist-pgo scripts/pgobuild.sh --force)
test -e dist-release/build.ninja || meson setup dist-release --buildtype release >&2
ninja -C dist-release tttt.exe baseline_tests.exe >&2

# System DLLs only (the static build links libc++/compiler-rt and winpthreads in), the UTF-8 manifest embedded
DLLS=$(llvm-objdump -p "$BIN" | sed -n 's/^ *DLL Name: //p' | sort -u)
! echo "$DLLS" | grep -v -i -x -E 'kernel32\.dll|ntdll\.dll|kernelbase\.dll|ucrtbase\.dll|api-ms-win-[a-z0-9-]+\.dll|advapi32\.dll|bcrypt\.dll|ws2_32\.dll|user32\.dll|shell32\.dll|ole32\.dll|psapi\.dll|dbghelp\.dll' \
    || fail "non-system DLLs above"
llvm-readobj --coff-resources "$BIN" > /dev/null
RSRC=$(mktemp)
llvm-objcopy --dump-section=.rsrc="$RSRC" "$BIN" "$RSRC.exe"
MANIFEST=$(tr -d '\0' < "$RSRC")
rm -f "$RSRC" "$RSRC.exe"
echo "$MANIFEST" | grep -q -E '<activeCodePage[^>]*>UTF-8</activeCodePage>' || fail "no UTF-8 activeCodePage in the manifest"
echo "$MANIFEST" | grep -q -E '<longPathAware[^>]*>true</longPathAware>' || fail "no longPathAware in the manifest"
OSVER=$(llvm-readobj --file-headers "$BIN" | sed -n 's/^ *MajorOperatingSystemVersion: //p; s/^ *MinorOperatingSystemVersion: //p' | paste -sd.)
SUBVER=$(llvm-readobj --file-headers "$BIN" | sed -n 's/^ *MajorSubsystemVersion: //p; s/^ *MinorSubsystemVersion: //p' | paste -sd.)

# PGO against -O3: identical output
same_output "$BIN" dist-release/tttt.exe

mkdir -p "$OUT"
cp "$BIN" "$OUT/tttt.exe"
# Every imported function by DLL (what the minimum Windows depends on)
llvm-objdump -p "$BIN" | awk '/DLL Name:/ { dll = $3 } /^ +[0-9]+ +[A-Za-z_]/ && dll { print dll, $2 }' | sort -u > "$OUT/imports.txt"
SHA=$(sha256sum "$OUT/tttt.exe" | cut -d' ' -f1)
cat > "$OUT/build-info.txt" <<INFO
tttt $(git describe --tags --always "$COMMIT" 2>/dev/null || echo "$COMMIT") for Windows x86-64 (win32-x64)
commit: $COMMIT (built at $(git rev-parse HEAD): src, thirdparty and meson.build identical)
compiler: $(clang --version | head -1), lld, MSYS2 CLANG64 (UCRT, libc++/compiler-rt, winpthreads), static
build: scripts/release/build-windows.sh: scripts/pgobuild.sh (LTO + clang PGO, trained on drizzle, zod, effect, excalidraw, webpack), meson --buildtype release -Db_lto=true
flags: meson.build's (-static, -mno-ms-bitfields, -Wl,--stack,268435456, src/win32/tttt.manifest as a resource: activeCodePage UTF-8, longPathAware)
PE header: OS version $OSVER, subsystem version $SUBVER (console)
minimum Windows: 10 1903 for the UTF-8 code page (agents/notes/release.md "Windows")
DLLs: $(echo $DLLS)
built: $(date -u +%Y-%m-%dT%H:%M:%SZ) on $(cmd //c ver 2>/dev/null | tr -d '\r' | grep -v '^$' || uname -sr)
sha256: $SHA
INFO
cat "$OUT/build-info.txt" >&2
echo "$OUT/tttt.exe"
