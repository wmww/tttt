# Source sweep checker leftovers (plan 52 stage 2, agent ss-c, 2026-10-08)
Keys +TS2589, +TS7022/7023 and friends, what was left after the fixes of that session. `scripts/sourcesweep.py -f '^REPO/'`
re-checks one repo; `scripts/tscinstcount.js TSCONFIG FILE` gave tsc 6's per-node TS2589 counter (the budget tttt has to
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

## +TS7022 chatbox `packages/chatbox-core/src/application/session/message-tree.ts:323` (`rest`), not investigated
`const { [forkMessageId]: _removed, ...rest } = resultHash ?? {}` inside a loop that assigns `resultHash = ...rest...`.
TTTT_DEBUG_CYCLE shows the pattern `{ [forkMessageId]: _removed, ...rest }` reaching itself (no expression between).
A cut-down with `Record<string, { n: number }> | undefined` for `Session['messageForksHash']` doesn't reproduce: the
real type (zod-inferred Session) matters. Next step: copy message-tree.ts with chatbox's node_modules into a probe
project and cut it down.

## +TS7023 tiptap `packages/core/src/Extendable.test.ts` (11 lines, `addAttributes`)
Reduced (tsc: only TS2339 on `parent`; tttt adds TS7023 on addAttributes):
```ts
interface ExtensionConfig<O = any> { name?: string; addOptions?: (this: { parent: any }) => O; [key: string]: any }
interface NodeConfig<O = any> { name?: string; group?: string; addOptions?: (this: { parent: any }) => O }
declare class Ext<Options = any> {
  extend<EO = Options, EC = ExtensionConfig<EO> | NodeConfig<EO>>(config?: Partial<EC>): Ext<EO>
}
declare const x: Ext;
export const e = x.extend({ addAttributes() { return { ...this.parent?.(), bar: {} } } });
```
The TS7023 comes from Checker.print_probe (missing_property_type() prints the object type the way tsc's eager
TS2339 message would): in the deferred body check `this` is `Partial<{ addAttributes: () => ... }>`, whose method's
return type is in flight, so printing marks it circular. tsc had already resolved that return type during the call
(its `this` there was the uninstantiated contextual type, so `this.parent` was the error type and the return type
any), and prints `() => any`. Likely fix: resolve the method's return type during argument checking like tsc (or make
the print probe skip a return type whose first evaluation is the current deferred check).

## Spelling suggestions run out in a different order (angular-components `task-state-zone-interceptor.ts`, 3 projects)
tsc counts every unresolved name toward its 10 spelling suggestions in *check* order, which is lazy: a method's
parameter types are resolved when a call in the constructor first needs them, so line 55's `HasTaskState` gets its
"Did you mean 'TaskState'?" (TS2552) before line 48's names use up the budget. tttt resolves names in the binder, in
source order (suggestion_count_order() approximates some of tsc's order), so line 55 is TS2304. Architectural; only
worth it if more cases turn up.

## Agent tc leftovers (2026-10-08, triage entry 10)
Fixed this round: mikro-orm `{ ...where }` TS2698 (intersection base constraints in isValidSpreadType), the
query/react-navigation `expectTypeOf` +TS2344 (void-optional rest tuples, arity before instantiation, instantiation
expression defaults, union effects signatures), svelte's JSDoc-typed class field context and destructuring defaults.
solid `component.type-tests.ts:100` already matched at this base. Left:
- svelte `deriveds.js:352`, `runtime.js:672` (`unknown` to `T`/`V`), `sources.js:90` (`Value<unknown>` to
  `Source<V>`), `Identifier.js:50` (`name += ...` string to the runes union), `builders.js:376` (`Value` to an
  estree union): JS/JSDoc, not reduced.
- react-navigation `Link.native.tsx:15` +TS2339 `'in' does not exist on type 'Props<ParamList, RouteName>'` (32
  diags, 3 projects; its +TS2578 unused `@ts-expect-error` follows). Partly reduced (agent tc, second round): with
  useLinkProps.tsx's types and core's `NavigatorScreenParams`/`ParamListForNavigator` imported,
  `function L<PL extends {}>(p: (WithIn<NoInfer<PL>> | { in?: undefined }) & { style?: string }) { p.in }` errors in
  tttt only (`WithIn` = useLinkProps' `LinkPropsWithIn`). tsc's apparent type of the member `WithIn<NoInfer<PL>> & {...}`
  is never (getApparentTypeOfIntersectionType: the members' apparent types, WithIn's base constraint is never), so
  createUnionOrIntersectionProperty skips it; tttt's is `{ style? }` because its apparent type of `WithIn<NoInfer<PL>>`
  is unknown. Underneath: tsc's base constraint of `ForParams<Exclude<PL[keyof PL], undefined>>`
  (`LinkPropsWithInForParams`) is never (C2 = `[Params] extends [NavigatorScreenParams<infer Navigator>] ? ...` is
  deferred, its inferred true type takes Navigator = its implied constraint `{}`, and `ParamListForNavigator<{}>
  extends infer NPL extends {} ? LST<NPL> | WithIn<NPL> : never` then resolves to never), while tttt's keeps
  `{ in: Extract<keyof NestedParamList, string>; ... } | LST<NestedParamList>` with NestedParamList free:
  inferred_true_branch() (relate.c) on the nested conditional doesn't resolve it. A self-contained copy of those
  definitions (stub NavigatorLike/NSP) did not reproduce; the real core types are needed (probe dir layout: tsconfig
  extending react-navigation's with `"types": []`, a p.ts importing by absolute path; /tmp/tc-probes/tools has
  tsctype.js/tscbase.js, node scripts printing tsc's type and getBaseConstraintOfType at a span).
- query `mutation.test.tsx:1264`, svelte-query `queryOptions.test-d.ts:90` (+TS2554 expected 1 argument, got 0): not
  looked at; probably the void-parameter optionality on another path (getMinArgumentCount in call resolution).
- dokploy's react-hook-form `Control<...>` mismatches: not looked at.
