#!/usr/bin/env bash
# hunt.sh BIN CORPUS N: N gdb runs of BIN (a -g build) at --shared-graph K12 T12, each crash's backtrace and registers
# kept as crash.X.I (HUNT_ENV=... passed to the run)
source "$(dirname "${BASH_SOURCE[0]}")/paths.sh"
BIN=$(sg_bin "$1"); c=$2; n=0
cd "$OUT" || exit 1
for i in $(seq 1 $3); do
env $HUNT_ENV timeout 600 gdb -batch -ex run -ex bt -ex "info registers" --args $BIN --shared-graph --threads 12 --checkers 12 --noEmit --pretty false -p ${P[$c]} > hunt.$c.out 2>&1
if grep -q "received signal" hunt.$c.out; then n=$((n+1)); grep -v "^\[New\|^\[Thread" hunt.$c.out > crash.$c.$i; fi
done
echo "$c: $n crashes in $3"
