// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type M = { [K in "a" | "b" as Exclude<K, typeof o>]: N["a"] };
interface N { a: M["b"] }
const o = f().a;
declare function f(): M;
const sentinel: number = "";
