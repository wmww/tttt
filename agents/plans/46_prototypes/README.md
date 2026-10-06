# Plan 46 stage 2 prototype: mutation testing on suite tests

`mutate.py N SEED` picks single-file suite tests that pass all three components (test/baseline_results.ts), applies one
regex mutation each (operators in OPS) and writes N mutants to /tmp/mut/t/tests/cases/conformance/mutNNN_OP.ts, with
/tmp/mut/log.json saying which test, operator, line and text. Then, from the repo root:

    T=/tmp/mut/t; mkdir -p $T/tests/cases/compiler; cp -r thirdparty/tsc/tests/lib $T/tests/lib
    echo "const tsCommit = '';" > $T/baseline_results.ts
    NODE_PATH=$PWD/thirdparty/tsc/node_modules/ node ./test/diffapp/tsc_custom_baseline.js $T </dev/null >/dev/null
    mv $T/tests/baselines/local $T/tests/baselines/reference
    ./fasttest.sh --dir $T/tests --results-path $T/baseline_results.ts       # then -f NAME -d --err per failure

tsc baselines 400 mutants in ~1.5 min (one warm process), tttt in seconds.
