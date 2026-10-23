// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Foo<T, U extends T> = [T, U];
type P<X> = X extends Foo<infer A, infer B> ? { b: B } : never;
function g<X>(x: P<X>) { x.b = 1; }
const sentinel: number = "";
