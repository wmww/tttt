// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type B10<T = { [K in keyof B10]: 1 }> = { x: T };
const sentinel: number = "";
