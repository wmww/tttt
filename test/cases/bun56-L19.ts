// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type A<T> = T extends infer #a ? 1 : 2;
type B<T> = T extends [infer #a extends string] ? 1 : 2;
declare function f(x: any): asserts #a;
declare function g(x: any): asserts #a is string;
