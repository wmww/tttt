# Todo
Sessions in order. Delete entries as they complete. See `vision.md` for why.

1. **Execution - parser gaps, round 2: members.** Round 1 (see `measurements.md`) took the parser-rejected count from
   4572 to 2890 tests. The remaining list (`./failstats.py --syntax -n 60`) is led by member syntax: object literal
   members (shorthand `{ x }`, methods, `get`/`set`, computed keys `[k]: v`, spread `...o` - `parse_object_item` does
   none of these), class/interface computed names (`[Symbol.iterator]() {}`, `["s"]() {}`), `p!: T`, `#private`
   names, stray `;` class elements, string-literal enum member names. Then spread in call/`new`/array literals,
   `function*`/`yield`, `await`, decorators, `satisfies`. Same rules as round 1: parse + AST + name resolution +
   test-info traversal; give new nodes the simplest honest type (eg methods are callables, shorthand is the
   identifier's type, unknown computed keys degrade to `any`) and don't build checker features on the old type core.
   Two lessons from round 1: (a) newly-parsing tests reach checker paths never run before - the suite segfaulted on
   a self-referential namespace type - so run `./fasttest.sh --summary` after each construct and `./test.sh -f` a
   few touched tests for valgrind; (b) a parse function's `ret` is garbage on failure, never pass the caller's
   out-param straight through to a sub-parser that may fail. Re-run failstats and update `measurements.md` after.
2. **Planning - type core redesign.** `plans/01-type-core-investigation.md` (that file is a brief; the
   deliverable is the real plan, `plans/02-type-core.md`, staged into session-sized steps, and new todo entries
   for those stages inserted here).
3. **Execution - type core stage 1** (per `plans/02-type-core.md`).
4. **Execution - type core stage 2.** Continue until the plan's stages are done or the next insights session
   re-evaluates; if the stages number more than ~4, do the first 4 then fall through.
5. **Execution - harness fidelity.** Use `./failstats.py` (its header lists which options the 412 skipped multi-config tests vary; `target` and `module` dominate) to find which ignored `// @option`
   directives cost the most tests; implement option parsing properly (one `CompilerOptions` struct, not ad-hoc
   bools), including multi-value directives (`@strict: true,false` produce multiple baselines - check how
   `thirdparty/tsc/tests/baselines` names them). The 726 `undefined` tests are explained in `measurements.md`: 412 multi-config, 314 without types/symbols baselines.
6. **Planning - symbols + control flow analysis.** Investigate how persistent symbols and a bind-time flow graph
   fit the (new) type core; write `plans/03-flow-analysis.md`. Pay attention to what tsc observably does
   (narrowing rules, reachability errors, definite assignment), and to parallel-bind friendliness.
7. **Triage.** Drain `triage.md`, prune `agents/`, refresh this file.

Then: insights.
