// @strict: true
// @noEmit: true
// tsc: r6/r8 are B. tttt: A | B (webpack's PackFileCacheStrategy/IdleFileCachePlugin Promise and LazyFunction
// TS2322s, ~9). The context-sensitive `(x) => ...` gets the parameter type fully instantiated with the current
// inferences, T -> its default A, so the inner call infers its R2 from the return type `P<A>`. tsc's
// instantiateContextualType only instantiates instantiable types (the return `P<T>` stays raw) and the inner call maps
// the outer T to silentNever (cloneInferenceContext NoDefault). See bugs/type_core.md "Inference".
interface P<T> { then<R1 = T, R2 = never>(ok?: ((v: T) => R1 | P<R1>) | null, bad?: ((r: any) => R2 | P<R2>) | null): P<R1 | R2> }
class A { a = 1; }
class B { b = 1; }
declare const p: P<A>;
declare const b: B;
declare function id3<T = A>(f: (x: A) => T | P<T>): T;
const r6 = id3((x) => p.then(() => b));
declare function id5<T = A>(f: (x: A) => P<T>): T;
const r8 = id5((x) => p.then(() => b));
const r9 = id5(() => p.then(() => b)); // (fine: not context sensitive)
