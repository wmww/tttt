// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f6<T extends T[keyof T]>(t: T) { const b: {} = t; }
const sentinel: number = "";
