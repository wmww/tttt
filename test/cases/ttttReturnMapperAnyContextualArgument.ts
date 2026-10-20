// @strict: true
// @declaration: true
// A literal argument's contextual type is the parameter type instantiated with the return mapper unless that is any or
// unknown (tsc's instantiateContextualType): S -> any from `Struct<any, any>` leaves S, whose constraint types `x`
// (snaps-cli's superstruct config, `builtIns?: any`)
declare class Struct<T, S> { t: T; s: S; }
declare function defaulted<T, S>(struct: Struct<T, S>, fallback: any): Struct<T, S>;
declare function object<S extends Record<string, Struct<any, any>>>(s: S): Struct<S, S>;
declare const anyv: any;
export const a: Struct<any, any> = object({ x: defaulted(anyv, {}) });
export const b = object({ y: object({ x: defaulted(anyv, {}) }) });
declare function box<T>(x: T): { value: T };
export const c: { value: any } = box({ n: 1 });
export const d: { value: unknown } = box([1, "a"]);
