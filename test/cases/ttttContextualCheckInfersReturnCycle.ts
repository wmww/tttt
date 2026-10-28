// A function expression with a contextual signature infers its return type at its first check (tsc's
// contextuallyCheckFunctionExpressionOrObjectLiteralMethod): the thunk below reads ShowType while it is in flight (TS7022, TS7023)
// @strict: true
// @noImplicitAny: true
type Thunk<T> = (() => T) | T;
interface FieldConfig<S, C> { type: Obj<any, any>; resolve?: (source: S, args: any, ctx: C) => any }
interface Config<S, C> { name: string; fields: Thunk<{ [k: string]: FieldConfig<S, C> }> }
declare class Obj<S, C> { name: string; constructor(config: Config<S, C>); }
interface Ctx { a: number }
export const ShowType = new Obj<any, Ctx>({
  name: "Show",
  fields: () => ({
    self: { type: ShowType },
  }),
});
export const probe: { __x: 1 } = ShowType;
