// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function g(a: unknown): number;
declare class K { constructor(a: unknown); v: number }
function h1({ a = 1, b = g(a) }) { return b; }
const h2 = ([a = 1, b = Math.max(a, 2)]) => b;
class C { m({ a = 1, b = new K(a) }) { return b; } }
// not quick: TS7022 in both
function h3({ a = 1, b = a + 1 }) { return b; }
const sentinel: number = "";
