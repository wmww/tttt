// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type A = { rest: any; k: 1 };
declare const u: { rest: A; k: 1 } | { rest: A; k: 2 };
const { ...rest } = u;
const one: 1 = rest.k;
const sentinel: number = "";
