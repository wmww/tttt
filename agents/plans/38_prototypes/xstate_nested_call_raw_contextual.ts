// Plan 38 stage 3: not(...) is first resolved inside or(...)'s inference, inside machine(...)'s. Its contextual type must be
// the raw one (tsc's getContextualType), not the one instantiated with or's return mapper, or not's TContext infers
// machine's raw TContext (xstate guards.test.ts 948). The types still differ from tsc on NormalizeGuardArgArray (unrelated)
interface MachineContext { [key: string]: any }
interface EventObject { type: string }
interface ParameterizedObject { type: string; params?: unknown }
type Identity<T> = { [K in keyof T]: T[K] };
type DoNotInfer<T> = [T][T extends any ? 0 : never];
type Elements<T> = T[keyof T & `${number}`];
interface GuardArgs<TContext extends MachineContext, TExpressionEvent extends EventObject> { context: TContext; event: TExpressionEvent }
type GuardPredicate<TContext extends MachineContext, TExpressionEvent extends EventObject, TParams, TGuard extends ParameterizedObject> = {
  (args: GuardArgs<TContext, TExpressionEvent>, params: TParams): boolean;
  _out_TGuard?: TGuard;
};
type SingleGuardArg<TContext extends MachineContext, TExpressionEvent extends EventObject, TParams, TGuardArg> =
  [TGuardArg] extends [{ type: string }] ? Identity<TGuardArg>
  : [TGuardArg] extends [string] ? TGuardArg
  : GuardPredicate<TContext, TExpressionEvent, TParams, ParameterizedObject>;
type NormalizeGuardArg<TGuardArg> = TGuardArg extends { type: string } ? Identity<TGuardArg> & { params: unknown }
  : TGuardArg extends string ? { type: TGuardArg; params: undefined }
  : '_out_TGuard' extends keyof TGuardArg ? TGuardArg['_out_TGuard'] & ParameterizedObject : never;
type NormalizeGuardArgArray<TArg extends unknown[]> = Elements<{ [K in keyof TArg]: NormalizeGuardArg<TArg[K]> }>;

declare function not<TContext extends MachineContext, TExpressionEvent extends EventObject, TArg>(
  guard: SingleGuardArg<TContext, TExpressionEvent, unknown, TArg>
): GuardPredicate<TContext, TExpressionEvent, unknown, NormalizeGuardArg<DoNotInfer<TArg>>>;
declare function or<TContext extends MachineContext, TExpressionEvent extends EventObject, TArg extends unknown[]>(
  guards: readonly [...{ [K in keyof TArg]: SingleGuardArg<TContext, TExpressionEvent, unknown, TArg[K]> }]
): GuardPredicate<TContext, TExpressionEvent, unknown, NormalizeGuardArgArray<DoNotInfer<TArg>>>;

declare function machine<TContext extends MachineContext>(impl: {
  context?: TContext;
  guards: Record<string, GuardPredicate<TContext, EventObject, unknown, ParameterizedObject>>
}): TContext;

const m = machine({
  guards: {
    referenced: or([() => false, not('truthy')])
  }
});
const n = or([() => false, not('truthy')]);
