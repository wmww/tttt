#!/usr/bin/env bash
# hang.sh BIN CORPUS N SECS: N gdb runs of BIN at --shared-graph K12 T12; a run past SECS is interrupted and all its
# threads' stacks kept as hang.X.I
source "$(dirname "${BASH_SOURCE[0]}")/paths.sh"
BIN=$(sg_bin "$1"); c=$2
cd "$OUT" || exit 1
for i in $(seq 1 $3); do
timeout -s INT $4 gdb -batch -ex run -ex "thread apply all bt 25" --args $BIN --shared-graph --threads 12 --checkers 12 --noEmit --pretty false -p ${P[$c]} > hang.$c.out 2>&1
if grep -q "received signal SIGINT\|SIGINT" hang.$c.out; then grep -v "^\[New\|^\[Thread" hang.$c.out > hang.$c.$i; echo "hang $i"; fi
if grep -q "received signal SIGSEGV\|SIGABRT" hang.$c.out; then echo "crash $i"; fi
done
