// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type B24<T = ReturnType<() => B24>> = { x: T };
const sentinel: number = "";
