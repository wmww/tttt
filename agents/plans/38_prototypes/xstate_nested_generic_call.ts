// @strict: true
// xstate createMachine({ context, entry: assign(({ context }) => ...) }): FIXED in the xstate session by skipping a
// generic call returning a function nested in a literal argument in the first pass (tsc's SkipGenericFunctions,
// resolvingSignature -> silentNever), see check_with() / GenericFunctionMode. Kept as a regression probe for plan 38:
// before, assign() was resolved in the first pass while createMachine had inferred nothing, inferred its TContext as
// createMachine's own TContext, which then leaked into the result ([TContext, TEvent]). tsc: [{ count: number },
// EventObject], context: { count: number }, no errors. Explained ~80 xstate diagnostics (assign/enqueueActions/sendTo/
// log/raise/spawnChild nested in machine configs). tttt's GenericFunctionMode only reaches through object and array
// literals; tsc's checkMode reaches every nested expression (parenthesized, conditional, ...).
interface EventObject { type: string }
interface ActionFunction<TContext, TEvent> {
  (args: { context: TContext; event: TEvent }): void;
}
declare function assign<TContext, TEvent extends EventObject>(
  a: (args: { context: TContext; event: TEvent }) => void): ActionFunction<TContext, TEvent>;
declare function createMachine<TContext, TEvent extends EventObject>(config: {
  context?: TContext;
  entry?: ActionFunction<TContext, TEvent>;
}): [TContext, TEvent];

const r = createMachine({
  context: { count: 0 },
  entry: assign(({ context }) => {
    // @ts-expect-error
    ((_accept: "ain't any") => {})(context.count);
  })
});
