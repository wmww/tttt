// @noImplicitAny: true
// Under recovery a node ends with its last token, not the trivia after it: a method signature missing its `(` (TS7010),
// a type argument list missing its `>` (TS2314 over the reference, a trailing comma included, never a later `=>`'s
// `>`); and a `>` in a type is one token of its own (tsc's scanner rescans `>>`, `>=` only after an expression's
// operand). Seeds 20261114/20261115: inferenceErasedSignatures extends-object, awaitedTypeJQuery delete-stmt,
// infiniteConstraints drop-prop
interface I {
    set<K extends Object this>(k: K): void;
}
interface P<A, B, C> { }
declare function f(
    a: (t: number) => P<number,
    b: (t: string) => void): void;
declare function g(a: P<number, string,
    c: number): void;
type A = Array<Array<new>>;
type B = Array<new>= 1;
let x = 1;
let y = x + 1 >>= 1;
