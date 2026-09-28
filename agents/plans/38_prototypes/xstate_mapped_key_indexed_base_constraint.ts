// @strict: true
// xstate interpreter.test.ts:1319 (TS2322 + TS2728 'count' is declared here): `after: { 10: { actions: assign(...) } }`
// in createMachine. DelayedTransitions is `{ [K in Delay<DoNotInfer<TDelay>>]?: ... }` with DoNotInfer<T> =
// `[T][T extends any ? 0 : any]`. tsc: the contextual type of property `10` is the generic mapped type's template
// (getTypeOfPropertyOfContextualType: "10" is assignable to the constraint's base constraint, string), so assign()
// infers TContext = { count: number } from its contextual return type; no errors. tttt (before): base_constraint() of
// the indexed access `[TDelay][0]` stopped at TDelay instead of going on to its constraint (tsc's computeBaseConstraint
// takes the base constraint of the resolved access), "10" wasn't assignable to TDelay, so the property had no
// contextual type: assign inferred unknown (MachineContext in xstate), TS2322 + TS18046. The numeric key doesn't
// matter (`foo:` fails the same way); plain TDelay or NoInfer<TDelay> as the key worked.
type DoNotInfer<T> = [T][T extends any ? 0 : any];
interface Action<TContext> { (args: { context: TContext }): void; _c?: TContext }
declare function assign<TContext>(a: (args: { context: TContext }) => void): Action<TContext>;
declare function createMachine<TContext, TDelay extends string>(config: {
  context?: TContext;
  after?: { [K in DoNotInfer<TDelay> | number]?: Action<TContext> };
}): TContext;

const r = createMachine({
  context: { count: 0 },
  after: {
    10: assign(({ context }) => context.count)
  }
});
