// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<T extends U["a"], U extends { a: T }>(t: T) { const z: boolean = t; }
const sentinel: number = "";
