// origin: npm artsy/metaphysics@main https://github.com/artsy/metaphysics tsconfig.bench.json seed 20261303
// flip: no
// @strict: true
// @noImplicitAny: false
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
