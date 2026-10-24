// @strict: true
// The parameters at a rest parameter of a generic type, `(...args: P) => any` instantiated with what `infer P` found, were
// generic before (tsc's isInstantiatedGenericParameter: tryGetTypeAtPosition gives `P[1]`), so two function-typed
// parameters there relate as types, here by the alias's variance, not signature against signature as callbacks
interface Base { kind: string }
type Narrow<T> = T extends { kind: "x" } ? T : never;
type Cb<T extends Base = Base> = (node: T, parent: Narrow<T>) => void;
declare function f(a: number, b: number, c: number): void;
declare function f<T extends Base>(t: T, cb: Cb<T>): void;
type P = Parameters<typeof f>;
declare const p: P;
const n: number = p;
