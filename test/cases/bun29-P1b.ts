// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "declaration": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export function f() { class C { static readonly s: unique symbol = Symbol(); } return { [C.s]: 1 }; }
const sentinel: number = "";
