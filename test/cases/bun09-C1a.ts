// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface Baz<A extends string> { a: A }
type W1<T> = T extends Baz<infer X extends number> ? 1 : 0;
const sentinel: number = "";
