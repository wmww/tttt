// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function isS(x: unknown): (asserts x is string);
declare const u: unknown;
isS(u); u.length;
declare class P { m(): (this is Q); }
declare class Q extends P { q: number }
declare const p: P;
if (p.m()) { p.q; }
const sentinel: number = "";
