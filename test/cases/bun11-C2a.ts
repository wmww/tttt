// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type B2<T = B2 extends string ? 1 : 2> = { x: T };
const sentinel: number = "";
