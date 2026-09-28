// @strict: true
// xstate actions.test.ts:3951 (TS2322, ActionFunction<{ count: number }> vs Action<{ count: number; text: string }>)
// and StateMachine.ts:393: `entry: assign(() => ({ count: 31 }))`, assign's parameter being
// Assigner<LowInfer<TContext>> whose return type is Partial<TContext & {}>. tsc: `keyof (T & {})` is a deferred index
// type (shouldDeferIndexType), inferToMappedType finds no inference for `T & {}` and infers nothing, so TContext comes
// from the contextual return type alone: { count: number; text: string }, no errors. tttt (before): inferring to a
// homomorphic `T[K]` mapped type always took the shortcut "the source is a candidate for the modifiers type", so
// { count: number } went into `T & {}` and T, at a higher priority than the return type's inference.
type DoNotInfer<T> = [T][T extends any ? 0 : any];
type LowInfer<T> = T & NonNullable<unknown>;
interface Action<TContext> { (args: { context: TContext }): void; _c?: TContext }
type Assigner<TContext> = (args: { context: TContext }) => Partial<TContext>;
declare function assign<TContext>(a: Assigner<LowInfer<TContext>>): Action<TContext>;
declare function createMachine<TContext>(config: { context?: TContext; entry?: Action<DoNotInfer<TContext>> }): TContext;
const r = createMachine({
  context: { count: 0, text: 'hello' },
  entry: assign(() => ({ count: 31 }))
});
