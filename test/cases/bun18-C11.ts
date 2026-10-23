// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<T extends unknown[]>(a = 1, ...r: T) {}
const x1: 0 = f<[string]>;
const sentinel: number = "";
