// @strict: true
// Inference walks two deferred array references (arrays written as alias bodies) as objects, stopping at deeply
// nested ones; their recursion identity is their node, as tsc's getRecursionIdentity has it, not Array: BB2<number>
// to BB<T> reaches number to T (bun's 17-C1d inferred nothing, then at more checkers nothing at all)
type CC<T> = T[]; type BB<T> = CC<T>[]; type AA<T> = BB<T>[];
type CC2<T> = T[]; type BB2<T> = CC2<T>[]; type AA2<T> = BB2<T>[];
declare function f<T>(x: AA<T>): T;
declare let v: AA2<number>;
const r7: string = f(v);
declare function h<T>(x: BB<T>): T;
declare let u: BB2<number>;
const r8: string = h(u);
