// @strict: true
// arktype's `interface DynamicBase<t extends object> extends t {}` (merged with a class): an interface can extend a
// type parameter constrained to an object type, and its instances have the type argument's members (tsc resolves the
// bases of every instantiation). tttt had none, so `"root" extends keyof string.submodule` was false and every
// `type("string")` failed to parse. tsc: TS2430 at both interface declarations (tttt: only at I, it skips interfaces
// merged with a class in interface_heritage_typecheck()), TS2322 'string' -> 'number' at `a` and `b`, TS2322 '1' ->
// 'keyof M' at `k`
interface DynamicBase<t extends object> extends t {}
declare class DynamicBase<t extends object> {
    constructor(properties: t);
}
declare class RootModule<exports extends {} = {}> extends DynamicBase<exports> {
    get kind(): "module";
}
type M = RootModule<{ root: string; alpha: number }>;
type K = keyof M;
declare const m: M;
export const a: number = m.root;
export const k: K = 1;
interface I<t extends object> extends t { x: 1 }
declare const i: I<{ y: string }>;
export const b: number = i.y;
