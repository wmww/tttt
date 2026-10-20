#!/bin/bash
# Plan 86 stage 4-0's taint prototype: which lazily computed results are clean (could be published to a graph checkers
# share) and, with --cleandiff, whether two histories agree on them (src/ty/taint.h says what clean means).
#   scripts/taintstats.sh [--lenient] [--cleandiff] [--describe|--print-types] [--top N] [--raw FILE | --from FILE] -p thirdparty/corpora/zod/packages/zod/tsconfig.test.json
# Builds a -g release copy with -DTTTT_TAINT in /tmp/tttt-taint (the same code otherwise) and runs it on one thread with
# one checker, no recycling and no deferred bodies (taint and fingerprints key on addresses). --cleandiff runs
# `--checker-memory 0` instead: a fresh checker, so a history of its own, for every file; each clean result is
# fingerprinted and compared with the first checker's that computed the same key. --describe prints mismatches
# with their fingerprints' fields (taint-A/taint-B lines in --raw's file; --print-types also prints the types with
# ty_to_cstr, which perturbs: printing resolves what it prints; --kind K only kind K's, which keeps the memory down).
# --lenient: the rule plan 86's G0 verdict uses (no done-ness peeks, flow_uncached only for expressions;
# src/ty/taint.c says why); --variances-clean also counts variances read while being measured as clean (cleandiff
# says that's wrong: drizzle's 92 mismatches); --twin taints instantiate_conditional()'s test of whether a type
# parameter's distributed twin exists yet (the named mechanism behind the conditional instance mismatches).
set -e
cd "$(dirname "$0")/.."
TOP=40
MODE=(--checker-memory off)
RAW=
FROM=
LENIENT=
VCLEAN=
TWIN=
KIND=
DESCRIBE=
ARGS=()
while test $# -gt 0; do
    case "$1" in
        --top) TOP=$2; shift 2 ;;
        --raw) RAW=$2; shift 2 ;;
        --cleandiff) MODE=(--checker-memory 0); shift ;;
        --describe) DESCRIBE=1; shift ;;
        --print-types) DESCRIBE=2; shift ;;
        --from) FROM=$2; shift 2 ;;
        --lenient) LENIENT=1; shift ;;
        --variances-clean) VCLEAN=1; shift ;;
        --twin) TWIN=1; shift ;;
        --kind) KIND=$2; shift 2 ;;
        *) ARGS+=("$1"); shift ;;
    esac
done
B=/tmp/tttt-taint
OUT=$(mktemp)
trap 'rm -f $OUT' EXIT
if test -n "$FROM"; then
    cp "$FROM" $OUT
else
    if test ! -e $B; then
        meson setup $B --buildtype release -Dc_args='-g -DTTTT_TAINT' > /dev/null
    fi
    ninja -C $B tttt > /dev/null
    TTTT_TAINT_DESCRIBE=$DESCRIBE TTTT_TAINT_DESCRIBE_KIND=$KIND TTTT_TAINT_LENIENT=$LENIENT TTTT_TAINT_VARIANCES_CLEAN=$VCLEAN \
        TTTT_TAINT_EXP_TWIN=$TWIN $B/tttt -q --noEmit \
        --keep-going --checkers 1 --threads 1 --no-defer-bodies --time "${MODE[@]}" "${ARGS[@]}" 2>&1 > /dev/null |
        grep '^taint' > $OUT || true
    if test -n "$RAW"; then cp $OUT "$RAW"; fi
fi
grep -q '^taint-total' $OUT || { echo "no taint-total line: the run died" >&2; exit 1; }
python3 - $OUT $TOP <<'PY'
import re, sys
body = open("src/ty/ty_internal.h").read()
body = body[body.index("KEY_LITERAL = 1"):]
names, i = {}, 0
for line in body[:body.index("}")].split("\n"):
    m = re.match(r"\s*(KEY_\w+)", line)
    if m:
        i = 1 if m.group(1) == "KEY_LITERAL" else i + 1
        names[i] = m.group(1)
rows, reasons, total, sites, causes = [], [], None, [], {}
for line in open(sys.argv[1]):
    f = line.split()
    if f[0] == "taint-kind":
        k = int(f[1])
        name = names.get(k, f[2])
        e, t, cc, ct, rep, unm, cmp_, mis, unfp = map(int, f[3:12])
        rows.append((cc + ct, name, e, t, cc, ct, rep, cmp_, mis, k))
    elif f[0] == "taint-reason":
        reasons.append((f[1], int(f[2])))
    elif f[0] == "taint-cause":
        causes.setdefault(int(f[1]), []).append((int(f[3]), f[2]))
    elif f[0] == "taint-site":
        sites.append((int(f[3]), f[1], f[2]))
    elif f[0] == "taint-total":
        total = (int(f[1]), int(f[2]))
print("taint events:", ", ".join(f"{r} {n}" for r, n in reasons))
print("top sites:", ", ".join(f"{s} ({r}) {n}" for n, s, r in sorted(sites, reverse=True)[:12]))
if total:
    print(f"run {total[0] / 1e9:.2f} Gcycles, in lazy results (exclusive) {total[1] / 1e9:.2f} ({100 * total[1] / total[0]:.0f}%)")
rows.sort(reverse=True)
allc = sum(r[0] for r in rows) or 1
cleanc = sum(r[4] for r in rows)
print(f"all kinds: clean {100 * cleanc / allc:.1f}% of exclusive time, entries clean "
      f"{100 * (1 - sum(r[3] for r in rows) / max(1, sum(r[2] for r in rows))):.1f}%")
print(f"{'kind':34} {'entries':>9} {'clean%':>7} {'time%':>6} {'clean t%':>8} {'reports':>7} {'compared':>9} {'mismatch':>8}")
for c, name, e, t, cc, ct, rep, cmp_, mis, k in rows[:int(sys.argv[2])]:
    print(f"{name:34} {e:9} {100 * (e - t) / e:6.1f}% {100 * c / allc:5.1f}% {100 * cc / max(1, c):7.1f}% {rep:7} {cmp_:9} {mis:8}")
mis = [(name, m, cmp_) for c, name, e, t, cc, ct, rep, cmp_, m, k in rows if m]
if any(r[7] for r in rows):
    print("cleandiff mismatches:", ", ".join(f"{n} {m}/{c}" for n, m, c in sorted(mis, key=lambda x: -x[1])) or "none")
def site_name(site):
    if site.startswith("read:"):
        k = int(site[5:])
        return "read " + names.get(k, {160: "MEMBERS", 161: "DECLARED_RAW", 162: "PROP_TYPE", 163: "SIGNATURE_RETURN",
            164: "PARAM_CONSTRAINT", 165: "SYMBOL_DECLARED", 166: "FUNCTION_RETURN", 167: "BASE_CONSTRAINT",
            168: "SIMPLIFIED", 169: "VARIANCES", 170: "FREE_PARAMS", 184: "LAZY_EXPR", 254: "INTERSECTION_PROPS"}.get(k, f"lazy decl {k - 176}" if 176 <= k < 184 else str(k)))
    return site
print("what tainted them first (share of the kind's tainted time):")
for c, name, e, t, cc, ct, rep, cmp_, mis, k in rows[:8]:
    top = sorted(causes.get(k, []), reverse=True)[:4]
    if top: print(f"  {name}: " + ", ".join(f"{site_name(site)} {100 * n / max(1, ct):.0f}%" for n, site in top))
PY
