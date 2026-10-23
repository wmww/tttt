// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<T extends [string, number]>(v: T) { let a: string | number; [a] = v; a.length; }
const sentinel: number = "";
