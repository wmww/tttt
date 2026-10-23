// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Id<T> = { [K in keyof T]: T[K] };
function g<T>() { const n: { never: 1 } = null! as Id<T & 1[]>; }
const sentinel: number = "";
