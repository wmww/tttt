// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<T extends [1, ...T]>(t: T, ...r: T) { }
const sentinel: number = "";
