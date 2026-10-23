// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface Foo<A extends number, B> { a: A; b: B }
type T<U> = U extends Foo<infer X, (infer X) & string> ? Foo<X, 1> : never;
const sentinel: number = "";
