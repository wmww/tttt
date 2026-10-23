// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f<U, T extends { a: { [k: string]: U } }>(v: T["a"]): U;
f({ x: "" }).length;
const sentinel: number = "";
