# Source sweep checker leftovers (plan 52 stage 2, agent ss-c, 2026-10-08)
Keys +TS2589, +TS7022/7023 and friends, what was left after the fixes of that session. `./sourcesweep.py -f '^REPO/'`
re-checks one repo; `./tscinstcount.js TSCONFIG FILE` gives tsc's per-node TS2589 counter (the budget tttt has to
stay under). A gdb breakpoint on `ty_instantiation_limit_reached` in `debug/tttt` (`TTTT_NO_REEXEC=1`, `--checkers 1
--threads 1`) prints `me->instantiation_depth` / `instantiation_count`: depth 100 is a runaway recursion, count 5M an
exponential (or missing cache) blow-up.

## +TS2589: tttt runs into the 5M instantiation count where tsc counts thousands
- date-fns/pkgs/docs `src/db.ts:10` `Typesaurus.Schema<typeof db>`: tsc 13214 instantiations at that node, tttt 5M.
  The stack at the limit is variance measurements nested ~8 deep (measure_variances inside relations inside
  measure_variances), with definitely_related's restrictive instantiations (conditional.c) and distributive
  constraints below. Suspects: variances measured again where tsc's cache hits (an alias's variance being measured
  while it is measured: tsc's `inVarianceComputation` / VarianceFlags.Unmeasurable path), or the restrictive
  instantiation of whole types per definitely_related() call (tsc caches getRestrictiveInstantiation on the type).
- styled-components `packages/styled-components/src/test/overriding.test.tsx:99,127,154`
  (`styled(Child).attrs({...})`): tsc ~7.9k, tttt 5M. Stack: mapped-type member resolution under a relation
  (expand_mapped -> keyof -> ty_mapped_info_instantiate, repeated). Since the active-mapper cache (efbf3b82) it is
  reported 3 times per call (callee, call, tagged template) instead of once.
- Not looked at: dokploy `packages/server/src/lib/auth.ts:513` (4 projects, better-auth types), rxjs
  `multicast.spec.ts:41`, `zip.spec.ts:75`, arktype `ark/type/match.ts:57` (and mikro-orm's copy of arktype in
  node_modules).

## +TS7022 lexical `LexicalCaretUtils.ts:561` (`adjacent`, 6 projects)
Standalone (tsc clean, tttt TS7022 on `adjacent`):
```ts
type Dir = 'next' | 'previous';
interface Base<D extends Dir> { direction: D; kind: string }
interface ChildCaret<D extends Dir = Dir> extends Base<D> { child: true }
interface SiblingCaret<D extends Dir = Dir> extends Base<D> { sib: true }
type PointCaret<D extends Dir = Dir> = ChildCaret<D> | SiblingCaret<D>;
declare function isChildCaret<D extends Dir>(caret: null | undefined | PointCaret<D>): caret is ChildCaret<D>;
declare function getAdjacentChildCaret<D extends Dir>(caret: null | PointCaret<D>): null | PointCaret<D>;
function deepest<Caret extends null | PointCaret<Dir>>(initialCaret: Caret): ChildCaret<NonNullable<Caret>['direction']> | Caret {
  let caret: ChildCaret<NonNullable<Caret>['direction']> | Caret = initialCaret;
  while (isChildCaret(caret)) {
    const adjacent = getAdjacentChildCaret(caret);
    if (!isChildCaret(adjacent)) break;
    caret = adjacent;
  }
  return caret;
}
```
Instrumenting tsc (log in getAssignmentReducedTypeWorker and getTypeAtFlowLoopLabel): for `caret` in
`isChildCaret(caret)`, checked inferentially (declared type *not* substituted), tsc's getInitialOrAssignedType still
substitutes the initial type (`Caret` -> `PointCaret<Dir> | null`): it calls getNarrowableTypeForReference without the
check mode. The reduction then gives up (`isTypeAssignableTo(assigned, reduced)` fails) and returns the declared type,
so the loop's first antecedent is the declared type and the back edge (`caret = adjacent`) is never walked. tttt
substitutes the assigned type only when the declared type was substituted (35ab441e,
`Checker.flow_substitutes_constraints`), so it walks the back edge and `adjacent` is circular.
Tried: substituting whenever the reference's position allows it (`constraint_position || contextual non-generic`,
ignoring inferential_depth). Fixes this, but regressed docmost (below), so it was reverted. The precise tsc rule
needs the contextual type tsc sees with no check mode (for an argument of the call being resolved: the
resolvingSignature's, i.e. any).

## docmost `apps/server/src/database/pagination/cursor-pagination.ts:288,293,298` +TS2345 (since 35ab441e)
`for (const [key, value] of values) switch (typeof value) { case 'string': cursor.set(key, value) }` with
`values: EncodeCursorValues<DB, TB, O, T>` (kysely types): tttt now reports key as
`string | number | symbol | (string & DynamicReferenceBuilder<any>) | ...`. Reproduces with the file copied alone
into a project whose node_modules links to docmost/apps/server's. Logging tsc shows it never reduces an assignment
for `key` there: its declared type isn't a union for tsc, i.e. tsc's getNarrowableTypeForReference does *not*
substitute the declared `ExtractSortFieldKey<DB, TB, O, ...>` (isGenericTypeWithUnionConstraint false, or the
substituted type isn't a union), while tttt does (has_generic_with_union_constraint / base_constraint_or_type in
expr.c disagree with tsc's base constraint of that conditional). Before 35ab441e the wrong substitution was masked by
reducing it with the unsubstituted initial type.

## +TS7022 chatbox `packages/chatbox-core/src/application/session/message-tree.ts:323` (`rest`), not investigated
`const { [forkMessageId]: _removed, ...rest } = resultHash ?? {}` inside a loop that assigns `resultHash = ...rest...`.
Likely the same family (a destructuring declaration's initial type through a loop back edge).
