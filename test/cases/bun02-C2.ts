// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface IFoo<X extends string> { x: X }
type T2<T> = T extends IFoo<infer U extends number> ? U : never;
type Two<A extends string, B extends A> = [A, B];
type T9<T> = T extends Two<infer X extends "a", infer Y extends "b"> ? [X, Y] : never;
type T11<T> = T extends [IFoo<infer U>, { n: infer U extends number }] ? U : never;
const sentinel: number = "";
