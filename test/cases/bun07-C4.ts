// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
function f10<T extends {}, U>(x: T & U) { "a" in x; }
const sentinel: number = "";
