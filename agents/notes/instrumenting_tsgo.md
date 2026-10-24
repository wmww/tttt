# Instrumenting tsgo (and the tttt side of the comparison)
When tttt and tsgo disagree and the mechanism isn't obvious from tsgo's Go sources, print what tsgo does. Recipe and
hooks from the correctness workers cor and cor2 (2026-10-22); `scripts/variancetrace.py`, `scripts/reentrytrace.py` and
`scripts/reltrace.py` automate three such builds, and `scripts/tsgotrace.py CODE TARGET [--funcs f,g]` the most common one (the stack of every
diagnostic with a code, `TSGO_DIAG_STACK` below, plus a stack at named functions) without writing any Go.
`scripts/overlaycheck.sh` compiles every overlay: run it after a re-pin (patches match tsgo's text exactly and rot).

## Relations, side by side: reltrace.py
`scripts/reltrace.py FILE.ts|PROJECT [--filter TEXT] [-- OPTIONS]` logs every structured comparison tsgo makes
(`recursiveTypeRelatedTo` as it finishes: depth, deep-nesting stack depths, result, `deep` when both sides expand),
maybe-stack hits and top-level queries, and tttt's `TTTT_DEBUG_RELATE{,_NESTED}` output normalized to the same lines,
then the first difference with depths left out. Use it when tttt and tsgo disagree on an assignability and the
relation is too deep to reason through. It found nest-excalibur's TS2345 (2026-10-24): tsgo got False for
`unknown -> T[P][P] extends ... ? 1 : QP<...>` where tttt got a deeply-nested Maybe, because tttt pushed the target on
the deep-nesting stacks along a source constraint chain. Expect noise: tttt alone logs a unit type against a union,
and it splits an indexed access over a union of keys (`T[P][string] | T[P][number] | ...`) that tsgo keeps as one.
So read the results column, not every line. Keep the input to one function.

## The recipe
1. Copy the Go files to patch into your scratch dir: `thirdparty/typescript/tsc/internal/checker/checker.go` and
   `flow.go` (`relater.go` if needed). Never edit the shared checkout.
2. Add prints to stderr guarded by an environment variable: `fmt.Fprintln(os.Stderr, ...)`, and
   `os.Stderr.Write(rtdebug.Stack())` for a stack. checker.go needs `import "os"` and `rtdebug "runtime/debug"`;
   flow.go `fmt` and `os`.
3. `overlay.json`: `{"Replace": {"<absolute original path>": "<absolute copy path>", ...}}`.
4. Build (~70 s; `--env` also cleans the shared Go cache past 4 GB):
   ```
   ENV=$(thirdparty/ensure-typescript.sh --env)
   cd thirdparty/typescript/tsc && (eval "$ENV"; go build -overlay /path/overlay.json -o /path/tsc ./cmd/tsc)
   ```
5. Run on a tiny project: `/path/tsc -p DIR --singleThreaded --pretty false` (`--noEmit` on anything shared).

## Hooks that paid off
| Variable | File / function | Prints |
|---|---|---|
| `TSGO_DIAG_STACK=CODE` | checker.go `addDiagnostic` | `diag CODE POS` and the Go stack for every diagnostic with that code, deferred ones included (`c.error` alone misses TS2339, which goes through `addDeferredDiagnostic`) |
| `TSGO_ASSIGN=1` | checker.go `assignParameterType` | `assignParameterType NAME RESOLVED?` |
| `TSGO_DEPTH=1` | checker.go `instantiateTypeWithAlias` at the depth/count limit | `depthlimit depth N count N` (TS2589: depth 100 or the 5M count) |
| `TSGO_CYCLE=1` | checker.go `pushTypeResolution` on a cycle | `cycle NAME PROP START LEN RESOLUTIONSTART` |
| `TSGO_PUSH=name` | checker.go `pushTypeResolution` | `push NAME ...` and a stack whenever a symbol of that name is pushed (use a unique probe name) |
| `TSGO_SIG=filesuffix` | checker.go `getResolvedSignature`, before the barrier reset | pos, cached, resolving, res, rs, loops (the first resolution is the barrier; flow loops in progress) |
| `TSGO_LOOP=1` | flow.go `getTypeAtFlowLoopLabel` | `loop cached KEY` / `loop enter KEY stack N inprogress B res N` |

Other useful sites: the deferral in `checkPropertyAccessExpressionOrQualifiedName`;
`getTypeOfVariableOrParameterOrPropertyWorker`; `signaturesRelatedTo` printing type ids, the `Instantiated` flag and
`source.Mapper()`.

## The tttt side
- gdb on `./debug/tttt` with `TTTT_NO_REEXEC=1 --checkers 1 --threads 1`, a breakpoint on `ty_resolving_cycle`: the
  path that made something circular.
- `TTTT_DEBUG_CYCLE=1` and `TTTT_DEBUG_REENTRY=1` count nested re-entries, to compare with `TSGO_PUSH`/`TSGO_SIG`
  counts.
- A temporary ids-and-mapper dump in relate.c's `TTTT_DEBUG_RELATE_NESTED` print tells apart two instances that print
  the same.

## Probing a shared repository with an edited file
The corpora and source sweep checkouts are shared: edit inside a bwrap overlay instead of the checkout.
```
bwrap --ro-bind / / --bind /tmp /tmp --dev /dev --proc /proc --overlay-src REPO --overlay UP WORK REPO \
  -- bash -c "cd REPO && sed -i ... && tsc ..."
```
(UP and WORK: two empty directories on the same filesystem, under /tmp.)
