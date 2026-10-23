// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type A1<T = A1> = { x: T };
const a1: A1 = 0;
const sentinel: number = "";
