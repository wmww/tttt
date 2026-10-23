// @strict: true
// A tuple array literal argument (here a const type parameter's) is a literal candidate like an array one: tsc's
// unionObjectAndArrayLiteralCandidates moves it after the others, so the common supertype of `U` and `readonly [1]`
// is U, and the argument then fails it. One taken apart into a slice that is all of it (`[...T]`) is not literal
// (tanstack-ai's ai-angular inject-web-mcp-tools test)
declare function f<const T extends readonly unknown[]>(a: T, b: T): T
function g<U extends readonly unknown[]>(u: U) {
    return f([1], u)
}
declare function ft<T extends unknown[]>(t1: [...T], t2: [...T, number?]): T;
ft([1, 2, 3], [1, 2, 3]);
ft(['a', 'b'], ['c', 'd'])
