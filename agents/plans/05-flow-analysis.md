# Plan 05: flow graph, narrowing, reachability, definite assignment
tttt has no control flow analysis at all. This plan adds tsc's: a flow graph built at bind time, and a lazy, backwards
"type of this reference at this flow node" query in the checker. 4 execution sessions (stages below), the suite and
`./realworld.py` run after every stage. `test/probes/plan05/narrowingBasics.ts` (checked against tsc with
`FAST=1 ./filetest.sh`) is the smoke test for all stages: tsc reports 4 errors on it, tttt 23 (20 spurious, 1 missing).

## Why / what the research found
- Real corpus @ 952f43d: 7754 diagnostics, of which TS2345 5249, TS2322 580, TS2769 290, TS2349 180, TS2367 126: almost
  all `X | undefined` / `A | B` that tsc narrowed. The forms the corpus lives on: truthiness (`if (!x) return`,
  `x && x.y`, `x || y`), `=== undefined`, `node.kind === SyntaxKind.X` (discriminant by enum literal), `switch (node.kind)`,
  type predicates (`isIdentifier(node)`), **assertion functions** (`Debug.assert(x)`, `Debug.assertIsDefined`),
  `Debug.fail()` (never-returning calls), optional chains, narrowing that survives into closures (`const`, parameters).
- Suite: ~310 tests are *about* this by name (`conformance/controlFlow` 55, `expressions/typeGuards` 63, ~190 in
  `compiler/`), but narrowing is behind a share of the 1179 missing / 995 spurious TS2322 and 937 missing TS2339 in every
  area. Don't expect the pass count to move like the corpus count: most tests fail on several things.
- **The type evaluation model needs one change, and it is the main design risk** (see "Re-entrancy" below): tttt memoises
  every `Expr`'s type with in-flight detection (`LazyType`), tsc memoises almost no expression types and relies on
  re-checking expressions while a loop's flow types are incomplete. Everything else maps cleanly: type identity is a
  pointer, errors are never reported from type evaluation (only from the `*_typecheck` walk, `errs` is null during
  evaluation), so re-evaluating an expression is side-effect free.
- The resolve walk (`*_lookup_names()`) visits everything with the scope at hand but **not in flow order** (a `for`
  loop resolves init, condition, afterthought, body). Names don't care about the order, so the walk is reordered where
  flow needs it rather than adding a second walk.
- `sizeof(Expr)` is 136 and `IdentifierExpr` (64) is its largest member. `keyword` + `this_kind` can share a word (and
  `Expr.currently_getting_type` is a parser-only `bool*`, worth a look): a `FlowNode*` on identifiers should cost 0 bytes per `Expr`. `PropertyExpr`
  is heap allocated, its pointer is free. Measure RSS on the corpus before/after (41 bytes per source byte today).
- `CompilerOptions` booleans have no "not set" state; TS7027/TS7028 are errors only when `allowUnreachableCode` /
  `allowUnusedLabels` is explicitly `false` (unset = editor suggestion, not in baselines). Needs a tri-state or a
  `*_is_set` bit for these two.

## Design
Follow tsc's observable model closely here; the `.types` baseline prints the narrowed type of every reference, so
"roughly right" narrowing fails the oracle. References: `thirdparty/tsc/src/compiler/binder.ts` (`bindWorker` flow
parts: `bindCondition`, `bindIfStatement`, `bindWhileStatement`, `bindTryStatement`, `bindSwitchStatement`,
`bindOptionalChain`, `bindCallExpressionFlow`, `finishFlowLabel`, `createFlowCondition`'s `isNarrowingExpression`) and
`checker.ts` (`getFlowTypeOfReference`, `isMatchingReference`, `getFlowCacheKey`, `narrowType*`, `getTypeFacts`,
`isReachableFlowNode`, `isPostSuperFlowNode`, `checkIdentifier`'s tail, `getFlowTypeOfAccessExpression`).

### Flow nodes (`src/flow.h`, `src/flow.c`)
```c
typedef struct FlowNode {
    u32 flags;      // FLOW_UNREACHABLE, START, BRANCH_LABEL, LOOP_LABEL, ASSIGNMENT, TRUE_CONDITION, FALSE_CONDITION,
                    // SWITCH_CLAUSE, CALL, REDUCE_LABEL, | FLOW_SHARED (more than one successor), FLOW_REFERENCED
    u32 id;         // per file, for dumps and deterministic debugging; caches key on the pointer
    union { FlowNode* antecedent; FlowList* antecedents; }; // labels have a list
    void const* node; // by kind, below
} FlowNode;
```
Arena allocated next to scopes (today `Binder.arena`; note it in the parallelism audit in `hacks.md`, same fix as
scopes: per-file arenas). There are no generic AST nodes, so `node` is typed by kind:
- condition: the `Expr const*` (only made when `isNarrowingExpression`, like tsc, else the antecedent is reused;
  `true`/`false` literals make the other branch unreachable).
- assignment: a small arena `FlowAssign { kind, ... }`: variable declaration (`InitializedBinding*`), binding element
  (`Binding*`, which knows its parent pattern), `x = e` / compound / logical assignment (`BinaryOpExpr*`), `x++`
  (`UnaryOpExpr*`), for-in/of variable (`ForEachLoopStatement*`), destructuring *assignment* target (`[a, b] = e`,
  `({ a } = e)`: tsc finds the assigned type through parent pointers, tttt has none, so the builder records the path
  while it walks the pattern: `{ parent path, element index | property name | rest | default, root BinaryOpExpr* }`).
  Every `FlowAssign` also has the target reference `Expr const*` for `isMatchingReference`.
- switch clause: `{ SwitchStatement*, clause_start, clause_end }`. call: the call `Expr const*`. start: the
  `FunctionImpl*` when the container is a function expression, arrow function or object literal method (the walk
  continues into the *containing* flow through `FunctionImpl.outer_flow` for narrowings that survive, see closures), null
  otherwise. reduce label: tsc's struct (try/finally).

Who points at flow nodes: `IdentifierExpr.flow` (incl. `this`/`super`), `PropertyExpr.flow` (dot and bracket),
`FunctionImpl.{outer_flow, end_flow, return_flow}` (`end_flow` reachable = the function can fall off its end;
`return_flow` for constructors and strictPropertyInitialization), class field initializers and static blocks (own
containers), and the places reachability is asked: a `Statement` doesn't need a pointer, the builder reports TS7027 /
sets a `unreachable` bit as it goes, like tsc's binder. `typeof x` in a *type* also has a flow node in tsc
(`TypeofType`; fixes the hack in `lower.c` "typeof x of an identifier is its declared type").

### Building it: in the resolve walk
A `FlowBuilder` rides along the resolve walk (thread it next to `Scope* names`; a struct that holds both is fine, the
signatures are already uniform): `current`, break/continue targets with labels, `true_target`/`false_target` for
conditions, `return_target`, the active switch's pre-switch flow, `has_explicit_return`. The walk order changes to
flow order where they differ: `for` (init, condition, body, afterthought), do-while (body, condition), `a && b` with
condition targets, `?:`, `??`, logical assignments, optional chains, `try`/`catch`/`finally` (port `bindTryStatement`
faithfully including the reduce label; approximations here give wrong definite assignment), `switch` (fallthrough,
`default` anywhere, exhaustiveness = no clause flow reaches the end + possibly-`never` needle is a *checker* question:
tsc adds the "no default" post-switch edge as a switch clause node with an empty range, keep that).
`IfStatement` is a flattened else-if chain (`conditions`/`statements`): fold it as nested ifs.
- New containers: functions, methods, accessors, constructors, class field initializers, static blocks, namespace
  bodies, source files. IIFEs are inlined into the enclosing flow in tsc; do that when cheap, else list it in hacks.md.
- **Assignment targets are marked during the walk** (no parent pointers): an `is_assignment_target` /
  `is_compound_target` bit on `IdentifierExpr`/`PropertyExpr`, set by whoever walks into the left side of `=`, `+=`,
  `++`, a destructuring assignment pattern, a for-in/of expression variable. The checker needs it: a reference that is
  being assigned has its *declared* type (`>x : string | number` in the baseline for `x = 1`), and the same walk sets
  `Symbol.is_assigned` / `is_assigned_in_closure`-style bits on *local* symbols (parameters, `let`, `var` of function
  scopes: symbols the file owns, so this doesn't break the per-file resolve contract; record on the symbol the
  function scope of the declaration and compare with the function the assignment is in. Function scopes aren't on
  their AST nodes yet: give `Scope` of kind function a pointer to its `FunctionImpl`, that's all that's needed).
- Call nodes: like `bindCallExpressionFlow` (top-level call expressions whose callee is a dotted name, not `super`
  with its own rule). Whether the call asserts or returns `never` is decided lazily by the checker.
- `TTTT_DUMP_FLOW=1`: print each function's graph (ids, kinds, antecedents, the source text of `node`). Build this in
  stage 1; every later debugging session starts here.

### The query (`src/ty/flow.c`)
`ty_flow_type(checker, reference Expr, declared, initial, flow)` = `getFlowTypeOfReference`. Port its structure, it is
load-bearing: walk antecedents backwards; assignment nodes that match the reference give the assigned type
(`getAssignmentReducedType(declared, assigned)`), ones that match a *prefix* of it (`x = ...` while asking about `x.y`)
reset to declared; conditions narrow if the expression mentions the reference (`narrowType`); branch labels union
their antecedents (subtype-reduced literal unions: `UnionReduction` differences show in baselines); loop labels iterate
to a fixpoint with incomplete types; start nodes of function expressions continue outside for references that can't
have changed; `FLOW_SHARED` nodes are cached within one query; unreachable gives `never`.
- **Reference matching** (`isMatchingReference`): identifiers by `target` symbol, `this`, `super`, `a.b` / `a["b"]` /
  `a[constIdentifier]` by object match + property name, seen through `x!` and `satisfies`/`as` where tsc does. Parens
  don't exist as nodes in tttt. Discriminant access (`x.kind` when asking about `x`), optional chain containment.
- **Reference keys** (`getFlowCacheKey`): u64 words `[symbol | this marker, property Atoms..., declared id, initial id]`
  interned in the checker to a small key id; references that have no key (calls in the chain, computed keys) don't
  narrow. Loop caches are `(loop FlowNode*, key id) -> Ty` and persist in the `Checker` (not the AST: several checkers
  must be able to share one flow graph, it is read-only after resolve). Guard with tsc's limits (flow depth 2000 ->
  TS2563, `flowLoopCount`).
- **Type facts** (`getTypeFacts`, one table): truthiness, `typeof` (both `===` directions, `switch (typeof x)`),
  `== null` / `=== undefined` and friends, under strict and non-strict null checks. `remove_definitely_falsy()` and
  its siblings in `ty/expr.c` (`&&`, `||`, `??`, `!` result types) are approximations of this: replace them with the
  facts table, don't keep two.
- Narrowing kinds, in the order the corpus wants them: truthiness, equality with literals / `undefined` / `null` /
  another reference, discriminant properties (incl. enum literal discriminants, `switch`), `typeof`, type predicates
  and assertion functions, `instanceof` (incl. `Symbol.hasInstance` later), `in`, optional chains (`x?.y === 1`
  narrows `x`), `switch (true)`, aliased conditions (`const ok = typeof x === "string"; if (ok)`, tsc 4.4; last, it
  needs "const with initializer, no assignments to the narrowed reference").
  `getNarrowedType` (predicates/instanceof) is: filter the union by relation to the candidate, else the candidate if it
  is a subtype, else the intersection. Check what tsc prints in each case on a probe before writing it.
- **Type predicates exist nowhere yet**: the parser drops them (`parse_type_predicate` returns `boolean`/`void`). They
  need a `TypeNode` form (`x is T`, `this is T`, `asserts x`, `asserts x is T`, parameter *index* resolved against the
  `FunctionImpl`), a `Signature.predicate`, instantiation, display (`(x: unknown) => x is string`, all over the
  baselines), and the relation rule (a predicate signature is only assignable to one with a matching predicate;
  `boolean`-returning target accepts it). TS 5.5 inferred predicates (`x => typeof x === "string"` returned from a
  one-return function) are out of scope: list in triage. `triage.md` has a note on predicates reaching inference
  (`arr.find(isNumber)`); that comes for free once the signature carries the predicate through overload selection.
- **Hooks**: `ty_identifier_reference_type()` takes the `Expr`/identifier *with* its flow node and does the tail of
  `checkIdentifier`: skip when the symbol isn't a variable/parameter (functions, classes, enums, namespaces, aliases
  never narrow), the declared type as-is for assignment targets, `assumeInitialized` (parameters, outer variables of
  another container, ambient, `var` without strict nulls...), initial type `declared | undefined` otherwise ->
  TS2454 when the result still has the `undefined` that `declared` doesn't, then the closure rule below. Property and
  element access get the same at the end of `ty_check_expr` for `EXPR_PROPERTY` (`getFlowTypeOfAccessExpression`:
  only when the access has a key, the declared property type is narrowable, and it's not an assignment target). `this`
  narrows too (`this is T` predicates, `this.x` keys).
- **Closures**: inside a function expression/arrow the flow start continues outside only for `const` variables,
  parameters and `let`s that are never assigned after the closure is created; everything else resets to the declared
  type at the function boundary (the probe has `[1].forEach(() => use(x))` and `() => use(p.q)`: property paths of
  mutable objects reset). tsc 5.4's exact rule is in `getFlowTypeOfReference`'s caller
  (`isPastLastAssignment`, `hasParentWithAssignmentsMarked`): port the rule, feeding it from the bits the resolve walk
  set instead of tsc's lazy `markNodeAssignments`.

### Re-entrancy: expression memos vs incomplete flow types (derisk this first in stage 2)
```ts
let x: string | number = 0;
while (cond()) { x = f(x); }   // x at `f(x)`: union of 0 and the type of `f(x)`, which asks for x at `f(x)`...
```
In tsc the statement walk checks `x = f(x)`, the reference `x` asks the loop label, the back edge reaches the
assignment node, which calls `getTypeOfExpression(f(x))` **again, re-entrantly**, whose `x` finds the loop label already
in progress for the same key and gets the *incomplete* type of the first antecedent; the loop iterates until the
union stops growing. Nothing computed while a type was incomplete is cached. In tttt the outer evaluation of
`f(x)` holds its `LazyType` in flight, the re-entrant request would come back as `TY_ERROR`, and a memo could pin an
incomplete type forever. The rule that mirrors tsc:
- The flow engine never asks for an expression's type through the memo when it may be re-entrant. It uses
  `ty_flow_expr_type(expr)`: a finished memo is used as is; otherwise the expression is evaluated in **uncached mode**
  (`Checker.flow_uncached` depth > 0): `lazy_type_get()` on `LAZY_TYPE_EXPR` slots neither marks in-flight nor stores,
  all the way down (so does `lazy_type_set()` from contextual checking). Termination comes from the loop machinery,
  like in tsc, not from the memo.
- Outside uncached mode every flow result is complete by construction (a top-level query runs its loops to the
  fixpoint), so normal memos stay valid. Assert it: a memo store while an incomplete type was observed since the
  evaluation started is a bug (`Checker.incomplete_seen` counter compared before/after, `check()` in debug builds).
- *Declaration* memos (binding, function return, class field `LazyType`s, `SymbolLinks`) keep their in-flight
  detection in every mode and, like tsc's `checkExpressionCached`, evaluate self-contained: save and zero
  `flow_uncached` and the loop-in-progress start index around them. That is what makes
  `while (c) { const y = x; x = y + 1; }` report TS7022 on `y` in tsc; tttt gets the same from the binding's in-flight
  state (the diagnostic itself can wait, the `any` can't).
- Cost: expressions on loop back edges are evaluated more than once. tsc pays the same. Watch the corpus profile.
First thing in stage 2: write this loop plus a nested-loop and a `const y = x` variant as a probe and make them print
tsc's types before building narrowing kinds on top.

### Reachability and definite assignment
- Binder side (stage 1): statements after `return`/`throw`/`break`/`continue`, infinite loops, exhaustive
  if/else + try/finally combinations are unreachable by construction: TS7027 when the option is explicitly false (first
  statement of each unreachable run, tsc's rules for what is exempt: function declarations, `var` without
  initializer, type declarations...), TS7028 unused labels likewise.
- Checker side: `isReachableFlowNode` (calls to `never`-returning functions with an *explicit* `never` annotation or
  declared signature, exhaustive `switch` by narrowing the needle to `never`, cached per flow node in the checker).
  Consumers: `ty_function_return_type()` gives `never` when the end is unreachable and nothing returns (fixes the TODO in
  `function.c`), TS2366 / TS7030 / TS2355 (end reachable, return type excludes `undefined`/`void`/`any`), the implicit
  `undefined` in inferred return types of functions that both return a value and fall off the end.
- Definite assignment: TS2454 falls out of the hook above. TS2564 (strictPropertyInitialization: flow type of
  `this.x` at the constructor's `return_flow` with initial type including `undefined`), TS2565, and `let x;` without
  annotation (auto types / evolving `any`, evolving arrays `const a = []`): **out of scope**, write a triage entry with
  what tests they cost once the rest is in.

## Stages (one session each; save baselines and update docs every time)
1. **Flow graph.** `flow.h`/`flow.c`, builder in the resolve walk in flow order, assignment-target and
   `is_assigned` bits, function scopes know their `FunctionImpl`, `TTTT_DUMP_FLOW`, `IdentifierExpr` repacked, binder-side
   reachability (TS7027/7028 with the tri-state option, `FunctionImpl.end_flow`), syntactic TS2366/TS7030 and `never`
   inference where no types are needed (the typed part comes in stage 3). No narrowing yet.
   *Done when:* dumps of the probe's functions read right (compare a few against tsc's understanding by reasoning,
   there is no tsc dump to diff), suite pass counts don't drop, resolve time on the corpus within +15% and RSS within
   +5% (`./bench.sh`, record in `measurements.md`), valgrind clean on the probe (`./filetest.sh` without `FAST`).
2. **The engine.** Re-entrancy probe first. `ty/flow.c` with keys, caches, labels, loops, assignments (incl.
   declarations, destructuring, for-in/of, `++`), the type facts table (replacing the `&&`/`||`/`??` approximations),
   truthiness / equality / `typeof` narrowing, hooks for identifiers and property access, assignment targets print
   declared types, closures rule, TS2454.
   *Done when:* the truthiness/typeofs/equality/assignments/loops/closures functions of the probe match tsc in errors
   and types; corpus diagnostics drop by thousands (expect the `X | undefined` bulk to go; record the number).
3. **The rest of narrowing.** Type predicates end to end (parser, `TypeNode`, signatures, display, relations),
   assertion functions and `never` calls (call flow nodes, typed reachability, the return type consumers),
   discriminants, `switch` (values, `typeof`, `true`, exhaustiveness), `instanceof`, `in`, optional chains, `typeof x`
   in types, aliased conditions if time allows.
   *Done when:* the whole probe passes its errors component and its types differ only by things listed as out of
   scope (`this` type, signature underlines), `./realworld.py` is re-grouped and its top remaining categories are
   written into `triage.md`.
4. **Fallout + measurement.** Expect what the type core migration taught: correct narrowing stops forgiving other
   approximations. Known candidates: no fresh/regular literal pairs (`hacks.md`; narrowing by `=== "a"` and assignment
   of literals lean on it: if stage 2/3 show it blocking whole categories, it becomes its own plan rather than a patch
   here), `ty_union` not dropping `null`/`undefined` without strictNullChecks (most tests are non-strict!), union
   subtype reduction, declared vs widened types of `let x = "a"`. Run `./failstats.py` against the saved stage 0
   numbers, look at tests that *regressed* (`./fasttest.sh --summary`), fix categories, not tests. Re-profile the
   corpus (callgrind) now that ~7000 error renderings are gone and flow queries are in: this is the first honest
   typecheck number next to tsgo. Update `documentation.md` (new "Control flow" section; resolve walk order; LazyType
   uncached mode), `vision.md` (foundational gap 3 closed), `hacks.md`, `measurements.md`; delete this plan.

## Validation of the whole plan
- `test/probes/plan05/*.ts` pass errors (and types modulo listed exceptions) against tsc.
- `./realworld.py`: from 7754 to the low thousands or better, no new categories; wall time and RSS recorded. If the
  count does *not* fall by more than half after stage 3, stop and find out why before stage 4.
- Tests matching `controlFlow|narrow|typeGuard|discriminat|definiteAssign|unreachable|assertion` (~310): record how
  many pass errors / types before stage 1 and after each stage in `measurements.md`.
- No drop in suite totals at any stage that isn't explained in the commit message.
- Valgrind clean on the probes; `./findcrash.py` clean (flow loops are a new way to hang: the suite must finish).

## Risks
- Re-entrancy (above). If uncached mode turns out to fight the contextual-typing memo rule ("whoever knows the
  contextual type checks first"): a condition or assigned expression evaluated by the flow engine *before* the
  statement walk reaches it is evaluated uncached without context and **must not** be stored, which the rule above
  already guarantees (only finished memos are read, nothing is written). Keep it that way even when it looks wasteful.
- Perf: tsc re-walks the graph per reference with only shared-node and loop caches, and it is fine for tsc; tttt also
  memoises each reference `Expr`. Don't add a global (flow node, key) cache before a profile asks for it. A cheap
  pre-filter ("no narrowable declared type -> declared type") is legitimate: tsc has the same shortcuts.
- Non-strict mode is the suite's default and the corpus is strict: check both on every narrowing kind.
- The walk-order change in resolve could change which duplicate/use-before-declare diagnostics come first: error
  order in baselines is by position, so it shouldn't, but look at the summary after stage 1.
